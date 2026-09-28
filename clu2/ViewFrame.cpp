#include "ViewFrame.h"

#include <algorithm>
#include <cmath>

#include <Windows.h>
#include <wx/dcbuffer.h>

namespace
{
	constexpr int kThumbSize = 200;
	constexpr int kPadding = 8;
	constexpr int kCellSize = kThumbSize + kPadding * 2;
	constexpr std::size_t kCacheCap = 400; // resident thumbnails; bounds RAM regardless of photo count

	// Kept conservative on purpose: wxWidgets doesn't officially promise that
	// wxImage::LoadFile is safe to call from several threads at once (the
	// underlying libjpeg/libpng handlers are usually fine, but it isn't a hard
	// guarantee). If loading ever crashes or corrupts thumbnails, drop this to 1
	// first to check whether it's a threading issue before looking elsewhere.
	std::size_t WorkerCount()
	{
		auto const hw = std::thread::hardware_concurrency();
		return std::clamp<std::size_t>(hw == 0 ? 2 : hw, 2, 4);
	}

	void OdsLog(std::wstring const& message)
	{
		::OutputDebugStringW((L"[clu2] " + message + L"\n").c_str());
	}

	// Shown for a cell whose file failed to load, so it settles into a stable
	// "done, but broken" state instead of retrying forever or looking stuck.
	wxImage MakeBrokenPlaceholder()
	{
		wxImage image(kThumbSize, kThumbSize);
		image.SetRGB(wxRect(0, 0, kThumbSize, kThumbSize), 235, 220, 220);

		return image;
	}
}

ThumbnailView::ThumbnailView(wxWindow* parent, std::vector<std::filesystem::path> paths)
	: wxScrolledWindow(parent, wxID_ANY)
	, m_paths(std::move(paths))
{
	SetBackgroundColour(*wxWHITE);
	SetBackgroundStyle(wxBG_STYLE_PAINT); // required for wxAutoBufferedPaintDC
	SetScrollRate(0, 10);

	Bind(wxEVT_PAINT, &ThumbnailView::OnPaint, this);
	Bind(wxEVT_SIZE, &ThumbnailView::OnSize, this);

	auto const workerCount = WorkerCount();
	OdsLog(L"ViewFrame: starting " + std::to_wstring(workerCount) + L" thumbnail workers for " + std::to_wstring(m_paths.size()) + L" paths");

	m_workers.reserve(workerCount);

	for (std::size_t i = 0; i < workerCount; ++i)
	{
		m_workers.emplace_back([this](std::stop_token stop) { WorkerLoop(stop); });
	}

	RecomputeLayout();
}

ThumbnailView::~ThumbnailView()
{
	// Stop and join every worker BEFORE this object (and the wxWindow base)
	// starts tearing down. A worker that called CallAfter after that point
	// would be reaching into a half-destroyed or destroyed window.
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		for (auto& worker : m_workers)
		{
			worker.request_stop();
		}
	}

	m_workAvailable.notify_all();
	m_workers.clear(); // jthread destructor joins
}

int ThumbnailView::ColumnCount() const
{
	return m_columns;
}

void ThumbnailView::RecomputeLayout()
{
	auto const clientWidth = GetClientSize().GetWidth();
	m_columns = std::max(1, clientWidth / kCellSize);

	auto const rows = static_cast<int>(std::ceil(static_cast<double>(m_paths.size()) / m_columns));
	SetVirtualSize(m_columns * kCellSize, std::max(rows, 1) * kCellSize);
}

void ThumbnailView::OnSize(wxSizeEvent& event)
{
	RecomputeLayout();
	Refresh();
	event.Skip();
}

wxRect ThumbnailView::CellRect(std::size_t index) const
{
	auto const row = static_cast<int>(index) / m_columns;
	auto const col = static_cast<int>(index) % m_columns;

	return wxRect(col * kCellSize + kPadding, row * kCellSize + kPadding, kThumbSize, kThumbSize);
}

void ThumbnailView::OnPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc(this);
	DoPrepareDC(dc);
	dc.Clear();

	if (m_paths.empty() || m_columns <= 0)
	{
		return;
	}

	// Only the region wx actually needs redrawn - typically just the newly
	// exposed strip after a scroll, not the whole window.
	auto const updateRect = GetUpdateRegion().GetBox();
	auto const topLeft = CalcUnscrolledPosition(updateRect.GetTopLeft());
	auto const bottomRight = CalcUnscrolledPosition(updateRect.GetBottomRight());

	auto const firstRow = std::max(0, topLeft.y / kCellSize - 1); // -1 row of margin so
	auto const lastRow = bottomRight.y / kCellSize + 1;           // pre-fetch is a bit ahead of the scroll

	auto const rowCount = static_cast<int>(std::ceil(static_cast<double>(m_paths.size()) / m_columns));

	dc.SetPen(*wxLIGHT_GREY_PEN);
	dc.SetBrush(*wxLIGHT_GREY_BRUSH);

	for (int row = firstRow; row <= lastRow && row < rowCount; ++row)
	{
		for (int col = 0; col < m_columns; ++col)
		{
			auto const index = static_cast<std::size_t>(row) * m_columns + col;

			if (index >= m_paths.size())
			{
				break;
			}

			auto const rect = CellRect(index);
			bool haveCachedBitmap = false;
			wxBitmap bitmap;

			{
				std::lock_guard<std::mutex> lock(m_mutex);
				auto const it = m_cache.find(index);

				if (it != m_cache.end())
				{
					bitmap = it->second.first;
					haveCachedBitmap = true;

					// Touch: move to front of the LRU list.
					m_lruOrder.erase(it->second.second);
					m_lruOrder.push_front(index);
					it->second.second = m_lruOrder.begin();
				}
			}

			if (haveCachedBitmap)
			{
				dc.DrawBitmap(bitmap, rect.GetTopLeft());
			}
			else
			{
				dc.DrawRectangle(rect);
				RequestLoad(index);
			}
		}
	}
}

void ThumbnailView::RequestLoad(std::size_t index)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (m_cache.contains(index) || m_inFlight.contains(index))
		{
			return;
		}

		// Push to the back = "most recently requested". Workers pop from the
		// back too, so whatever just scrolled into view is served first,
		// ahead of older requests for cells the user has since scrolled past.
		m_pending.push_back(index);
	}

	m_workAvailable.notify_one();
}

void ThumbnailView::WorkerLoop(std::stop_token stop)
{
	while (true)
	{
		std::size_t index = 0;

		{
			std::unique_lock<std::mutex> lock(m_mutex);

			m_workAvailable.wait(lock, stop, [this] { return !m_pending.empty(); });

			if (stop.stop_requested())
			{
				return;
			}

			index = m_pending.back();
			m_pending.pop_back();
			m_inFlight[index] = 0;
		}

		auto const& path = m_paths[index];
		wxImage image(path.wstring(), wxBITMAP_TYPE_ANY);

		if (image.IsOk())
		{
			image = image.Scale(kThumbSize, kThumbSize, wxIMAGE_QUALITY_HIGH);
		}
		else
		{
			OdsLog(L"Failed to load picture: " + path.wstring());
			image = MakeBrokenPlaceholder();
		}

		if (stop.stop_requested())
		{
			return;
		}

		CallAfter([this, index, image]() mutable
			{
				OnThumbnailReady(index, std::move(image));
			});
	}
}

void ThumbnailView::OnThumbnailReady(std::size_t index, wxImage image)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_inFlight.erase(index);

		m_lruOrder.push_front(index);
		m_cache[index] = { wxBitmap(image), m_lruOrder.begin() };

		while (m_cache.size() > kCacheCap)
		{
			auto const evictIndex = m_lruOrder.back();
			m_lruOrder.pop_back();
			m_cache.erase(evictIndex);
		}
	}

	RefreshRect(CellRect(index));
}

ViewFrame::ViewFrame(const std::vector<std::filesystem::path>& paths)
	: wxFrame(nullptr, wxID_ANY, "picture viewer", wxDefaultPosition, wxSize(800, 600))
{
	auto* view = new ThumbnailView(this, paths);

	auto* mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(view, 1, wxEXPAND);
	SetSizer(mainSizer);
}