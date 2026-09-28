#pragma once

#include "Wx.h"
#include <condition_variable>
#include <filesystem>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// A scrolled grid of thumbnails that never creates one GUI window per photo
// and never decodes more images than fit on screen at once:
//
//  - OnPaint only draws (and only requests) thumbnails for the rows currently
//    visible, plus a small margin. Scrolling exposes new rows, which wx repaints
//    automatically, so scrolling IS what drives loading - no separate scroll
//    handler needed.
//  - Decoding + scaling happens on a small pool of background threads (I/O and
//    CPU heavy, must never run on the GUI thread). Only the cheap final step -
//    turning a decoded wxImage into a wxBitmap - happens back on the GUI thread,
//    via CallAfter.
//  - A capped, least-recently-used cache holds only a bounded number of
//    thumbnails at a time, so RAM use doesn't grow with the number of photos.
class ThumbnailView : public wxScrolledWindow
{
public:
	ThumbnailView(wxWindow* parent, std::vector<std::filesystem::path> paths);
	~ThumbnailView() override;

private:
	void OnPaint(wxPaintEvent& event);
	void OnSize(wxSizeEvent& event);

	void RecomputeLayout();
	wxRect CellRect(std::size_t index) const;
	int ColumnCount() const;

	// Called from OnPaint (GUI thread) when a visible cell has no cached
	// thumbnail yet. Thread-safe: just registers interest and wakes a worker.
	void RequestLoad(std::size_t index);

	// Runs on each background worker thread until told to stop.
	void WorkerLoop(std::stop_token stop);

	// Runs on the GUI thread (via CallAfter) once a worker finishes one image.
	void OnThumbnailReady(std::size_t index, wxImage image);

	std::vector<std::filesystem::path> m_paths;

	// --- shared state between the GUI thread and the worker threads ---
	// Every member below is only ever touched while holding m_mutex.
	std::mutex m_mutex;
	std::condition_variable_any m_workAvailable;
	std::vector<std::size_t> m_pending;       // wanted, not yet started (LIFO: back = most recently requested)
	std::unordered_map<std::size_t, char> m_inFlight; // currently being decoded, value unused (set-like)

	std::list<std::size_t> m_lruOrder;        // front = most recently used
	std::unordered_map<std::size_t, std::pair<wxBitmap, std::list<std::size_t>::iterator>> m_cache;
	// --- end shared state ---

	std::vector<std::jthread> m_workers;

	int m_columns = 1;
};

class ViewFrame : public wxFrame
{
public:
	ViewFrame(const std::vector<std::filesystem::path>& paths);
};