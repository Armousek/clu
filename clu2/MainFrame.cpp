#include "MainFrame.h"
#include "ParsingUtils.h"
#include "ViewFrame.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <stop_token>
#include <string>
#include <vector>

#include <Windows.h>
#include <wx/config.h>

using clu2::AddMatchingFiles;
using clu2::GetRoots;
using clu2::IsImageFile;
using clu2::OdsLog;
using clu2::Trim;
using clu2::TryExtractDdd;

namespace
{
	constexpr int kAdvancedPatternMenuId = wxID_HIGHEST + 1;
	constexpr int kHistoryMenuBaseId = wxID_HIGHEST + 100; // reserves a block of kHistoryCap ids after it
	constexpr std::size_t kHistoryCap = 8;

	using namespace std::chrono_literals;

	// The actual scan. This runs on the WORKER thread, so it must never touch
	// any wx window or control. It only reads its arguments, checks the stop
	// token, and reports progress through the callback.
	ScanResult Scan(
		std::vector<std::filesystem::path> const& roots,
		std::wregex const& idPattern,
		std::wregex const& suffixPattern,
		std::stop_token const& stop,
		std::function<void(std::size_t, std::size_t)> const& reportProgress)
	{
		ScanResult result;
		std::set<std::filesystem::path> paths;
		std::size_t dmcMatches = 0;
		auto lastReport = std::chrono::steady_clock::now();

		for (auto const& root : roots)
		{
			if (stop.stop_requested())
			{
				break;
			}

			std::error_code rootErrorCode;

			if (!std::filesystem::is_directory(root, rootErrorCode))
			{
				OdsLog(L"Skipping invalid root: " + root.wstring());
				continue;
			}

			OdsLog(L"Scanning root: " + root.wstring());

			std::error_code errorCode;
			auto iterator = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, errorCode);
			auto const end = std::filesystem::recursive_directory_iterator();

			while (iterator != end)
			{
				if (stop.stop_requested())
				{
					break;
				}

				++result.scanned;

				if (!errorCode && iterator->is_regular_file(errorCode))
				{
					auto const path = iterator->path();
					auto const dmcDirectory = path.parent_path();

					if (IsImageFile(path) && dmcDirectory.filename().wstring() == L"DMC")
					{
						std::wstring ddd;

						if (TryExtractDdd(path, idPattern, suffixPattern, ddd))
						{
							++dmcMatches;
							OdsLog(L"DMC match: " + path.wstring() + L" DDD: " + ddd);
							auto const added = AddMatchingFiles(dmcDirectory.parent_path(), ddd, paths, stop);

							if (added == 0 && !stop.stop_requested())
							{
								result.messages.emplace_back(L"No parent folder files found for " + ddd + L" from " + path.filename().wstring());
							}
						}
					}
				}

				// Tell the GUI how we're doing, but at most ~7x per second.
				// Posting on every file would flood the GUI's event queue.
				auto const now = std::chrono::steady_clock::now();

				if (now - lastReport >= 150ms)
				{
					reportProgress(result.scanned, dmcMatches);
					lastReport = now;
				}

				errorCode.clear();
				iterator.increment(errorCode);
			}
		}

		result.cancelled = stop.stop_requested();
		result.paths.assign(paths.begin(), paths.end());

		return result;
	}
}

MainFrame::MainFrame()
	: wxFrame(nullptr, wxID_ANY, "clu", wxDefaultPosition, wxSize(800, 600))
{
	SetBackgroundColour(*wxWHITE);

	auto* menuBar = new wxMenuBar();
	auto* fileMenu = new wxMenu();
	fileMenu->Append(wxID_OPEN, "Choose...");
	fileMenu->AppendSeparator();
	m_historyMenu = new wxMenu();
	fileMenu->AppendSubMenu(m_historyMenu, "Recent searches");
	fileMenu->AppendSeparator();
	fileMenu->Append(kAdvancedPatternMenuId, "Advanced pattern...", "Change how the app finds the ID and status inside each file name");
	menuBar->Append(fileMenu, "File");
	SetMenuBar(menuBar);

	CreateStatusBar();
	SetStatusText("Ready");

	auto* mainSizer = new wxBoxSizer(wxVERTICAL);
	auto* groupSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Input");
	auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

	m_textControl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 72), wxTE_MULTILINE);
	m_secondTextControl = new wxTextCtrl(this, wxID_ANY);
	auto* viewButton = new wxButton(this, wxID_ANY, "View");
	auto* chooseButton = new wxButton(this, wxID_ANY, "Choose");
	m_runButton = new wxButton(this, wxID_ANY, "Run");
	m_cancelButton = new wxButton(this, wxID_ANY, "Cancel");
	m_cancelButton->Disable();
	m_textControl->SetBackgroundColour(*wxWHITE);
	m_secondTextControl->SetBackgroundColour(*wxWHITE);
	chooseButton->SetBackgroundColour(*wxWHITE);
	m_runButton->SetBackgroundColour(*wxWHITE);
	m_cancelButton->SetBackgroundColour(*wxWHITE);

	buttonSizer->AddStretchSpacer();
	buttonSizer->Add(viewButton, 0, wxRIGHT, 8);
	buttonSizer->Add(chooseButton, 0, wxRIGHT, 8);
	buttonSizer->Add(m_runButton, 0, wxRIGHT, 8);
	buttonSizer->Add(m_cancelButton, 0);

	groupSizer->Add(m_textControl, 0, wxEXPAND | wxALL, 8);
	groupSizer->Add(new wxStaticText(this, wxID_ANY, "Status to find (e.g. NOK)"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
	groupSizer->Add(m_secondTextControl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	groupSizer->Add(buttonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 14), wxGA_HORIZONTAL);

	m_listControl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_listControl->SetBackgroundColour(*wxWHITE);
	m_listControl->AppendColumn("Text", wxLIST_FORMAT_LEFT, 700);

	mainSizer->Add(groupSizer, 0, wxEXPAND | wxALL, 8);
	mainSizer->Add(m_gauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	mainSizer->Add(m_listControl, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	viewButton->Bind(wxEVT_BUTTON, &MainFrame::OnViewButtonClicked, this);
	chooseButton->Bind(wxEVT_BUTTON, &MainFrame::OnChooseButtonClicked, this);
	m_runButton->Bind(wxEVT_BUTTON, &MainFrame::OnRunButtonClicked, this);
	m_cancelButton->Bind(wxEVT_BUTTON, &MainFrame::OnCancelButtonClicked, this);
	Bind(wxEVT_MENU, &MainFrame::OnChooseButtonClicked, this, wxID_OPEN);
	Bind(wxEVT_MENU, &MainFrame::OnAdvancedPatternClicked, this, kAdvancedPatternMenuId);
	Bind(wxEVT_MENU, &MainFrame::OnHistoryItemClicked, this, kHistoryMenuBaseId, kHistoryMenuBaseId + static_cast<int>(kHistoryCap) - 1);
	Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);

	SetSizer(mainSizer);
	Centre();

	LoadSettings();
	LoadResults();
	LoadHistory();
}

MainFrame::~MainFrame()
{
	// Closing the window mid-scan: ask the worker to stop and wait for it,
	// so it can never call back into a frame that no longer exists.
	m_worker.request_stop();

	if (m_worker.joinable())
	{
		m_worker.join();
	}
}

void MainFrame::OnClose(wxCloseEvent& event)
{
	// Catches "typed something, closed the app without pressing Run again" -
	// SaveSettings() also runs from OnRunButtonClicked, so this is a top-up,
	// not the only place settings get saved.
	SaveSettings();
	event.Skip(); // let the close continue normally (destructor still runs)
}

void MainFrame::LoadSettings()
{
	// wxConfig is registry-backed on Windows (HKCU\Software\clu2). Nothing here
	// touches disk directly, and a fresh machine with no saved settings yet
	// just leaves everything at its built-in defaults - Read() returns false
	// and we simply don't overwrite what the constructor already set up.
	wxConfig config("clu2");
	wxString folders;

	if (config.Read("Folders", &folders) && !folders.IsEmpty())
	{
		folders.Replace(";", "\n");
		m_textControl->SetValue(folders);
	}

	wxString statusPattern;

	if (config.Read("StatusPattern", &statusPattern))
	{
		m_secondTextControl->SetValue(statusPattern);
	}

	wxString idPattern;

	if (config.Read("AdvancedPattern", &idPattern) && !idPattern.IsEmpty())
	{
		m_idPattern = idPattern;
	}

	OdsLog(L"Settings loaded");
}

void MainFrame::SaveSettings()
{
	wxConfig config("clu2");

	// Semicolon-joined rather than kept multi-line: GetRoots() already treats
	// ';' and newlines as the same separator, and it sidesteps any quirks the
	// registry backend might have with embedded newlines in a string value.
	auto folders = m_textControl->GetValue();
	folders.Replace("\n", ";");

	config.Write("Folders", folders);
	config.Write("StatusPattern", m_secondTextControl->GetValue());
	config.Write("AdvancedPattern", m_idPattern);
	config.Flush();

	OdsLog(L"Settings saved");
}

void MainFrame::PopulateResultsList(std::vector<std::wstring> const& messages, std::vector<std::filesystem::path> const& paths)
{
	m_listControl->DeleteAllItems();

	for (auto const& message : messages)
	{
		auto const index = m_listControl->GetItemCount();
		m_listControl->InsertItem(index, wxString(message));
	}

	for (auto const& path : paths)
	{
		auto const index = m_listControl->GetItemCount();
		m_listControl->InsertItem(index, wxString(path.filename().wstring()));
	}
}

void MainFrame::LoadResults()
{
	// Same store as LoadSettings(), just its own keys. A fresh machine simply
	// reads a count of 0 for both and leaves the list empty, same as today.
	wxConfig config("clu2");

	long pathCount = 0;
	config.Read("ResultPaths/Count", &pathCount, 0l);

	std::vector<std::filesystem::path> paths;
	paths.reserve(static_cast<std::size_t>(std::max<long>(pathCount, 0)));

	for (long i = 0; i < pathCount; ++i)
	{
		wxString value;

		if (config.Read(wxString::Format("ResultPaths/%lu", static_cast<unsigned long>(i)), &value))
		{
			paths.emplace_back(value.ToStdWstring());
		}
	}

	long messageCount = 0;
	config.Read("ResultMessages/Count", &messageCount, 0l);

	std::vector<std::wstring> messages;
	messages.reserve(static_cast<std::size_t>(std::max<long>(messageCount, 0)));

	for (long i = 0; i < messageCount; ++i)
	{
		wxString value;

		if (config.Read(wxString::Format("ResultMessages/%lu", static_cast<unsigned long>(i)), &value))
		{
			messages.push_back(value.ToStdWstring());
		}
	}

	if (paths.empty() && messages.empty())
	{
		return;
	}

	m_foundPaths = std::move(paths);
	m_foundMessages = std::move(messages);
	PopulateResultsList(m_foundMessages, m_foundPaths);

	SetStatusText(wxString::Format("Showing %zu files from the last run", m_foundPaths.size()));
	OdsLog(L"Restored previous results: " + std::to_wstring(m_foundPaths.size()) + L" files");
}

void MainFrame::SaveResults()
{
	wxConfig config("clu2");

	// Each path/message is its own key rather than one joined string - avoids
	// ever having to escape a delimiter inside a real file name or message.
	// DeleteGroup first so a shorter result list doesn't leave old entries
	// behind that Count would no longer reach but that would sit there unused.
	config.DeleteGroup("ResultPaths");
	config.Write("ResultPaths/Count", static_cast<long>(m_foundPaths.size()));

	for (std::size_t i = 0; i < m_foundPaths.size(); ++i)
	{
		config.Write(wxString::Format("ResultPaths/%lu", static_cast<unsigned long>(i)), wxString(m_foundPaths[i].wstring()));
	}

	config.DeleteGroup("ResultMessages");
	config.Write("ResultMessages/Count", static_cast<long>(m_foundMessages.size()));

	for (std::size_t i = 0; i < m_foundMessages.size(); ++i)
	{
		config.Write(wxString::Format("ResultMessages/%lu", static_cast<unsigned long>(i)), wxString(m_foundMessages[i]));
	}

	config.Flush();

	OdsLog(L"Results saved: " + std::to_wstring(m_foundPaths.size()) + L" files");
}

void MainFrame::LoadHistory()
{
	wxConfig config("clu2");
	long count = 0;
	config.Read("History/Count", &count, 0l);
	count = std::clamp<long>(count, 0, static_cast<long>(kHistoryCap));

	m_history.clear();
	m_history.reserve(static_cast<std::size_t>(count));

	for (long i = 0; i < count; ++i)
	{
		auto const prefix = wxString::Format("History/%lu/", static_cast<unsigned long>(i));
		SearchHistoryEntry entry;
		config.Read(prefix + "Folders", &entry.folders);
		config.Read(prefix + "StatusPattern", &entry.statusPattern);
		config.Read(prefix + "IdPattern", &entry.idPattern);
		m_history.push_back(std::move(entry));
	}

	RebuildHistoryMenu();
	OdsLog(L"History loaded: " + std::to_wstring(m_history.size()) + L" entries");
}

void MainFrame::RememberSearch(wxString const& folders, wxString const& statusPattern, wxString const& idPattern)
{
	// Don't add a duplicate of what's already the most recent entry - repeatedly
	// running the exact same search shouldn't push everything else further down.
	if (!m_history.empty()
		&& m_history.front().folders == folders
		&& m_history.front().statusPattern == statusPattern
		&& m_history.front().idPattern == idPattern)
	{
		return;
	}

	m_history.insert(m_history.begin(), SearchHistoryEntry{ folders, statusPattern, idPattern });

	if (m_history.size() > kHistoryCap)
	{
		m_history.resize(kHistoryCap);
	}

	wxConfig config("clu2");
	config.DeleteGroup("History");
	config.Write("History/Count", static_cast<long>(m_history.size()));

	for (std::size_t i = 0; i < m_history.size(); ++i)
	{
		auto const prefix = wxString::Format("History/%lu/", static_cast<unsigned long>(i));
		config.Write(prefix + "Folders", m_history[i].folders);
		config.Write(prefix + "StatusPattern", m_history[i].statusPattern);
		config.Write(prefix + "IdPattern", m_history[i].idPattern);
	}

	config.Flush();

	RebuildHistoryMenu();
}

void MainFrame::RebuildHistoryMenu()
{
	while (m_historyMenu->GetMenuItemCount() > 0)
	{
		m_historyMenu->Destroy(m_historyMenu->FindItemByPosition(0));
	}

	if (m_history.empty())
	{
		m_historyMenu->Append(wxID_ANY, "(no recent searches yet)")->Enable(false);
		return;
	}

	for (std::size_t i = 0; i < m_history.size(); ++i)
	{
		auto const& entry = m_history[i];
		auto const folderCount = GetRoots(entry.folders.ToStdWstring()).size();

		auto label = entry.statusPattern.IsEmpty()
			? wxString::Format("%zu folder(s)", folderCount)
			: wxString::Format("%zu folder(s) - \"%s\"", folderCount, entry.statusPattern);

		m_historyMenu->Append(kHistoryMenuBaseId + static_cast<int>(i), label);
	}
}

void MainFrame::OnHistoryItemClicked(wxCommandEvent& event)
{
	auto const index = static_cast<std::size_t>(event.GetId() - kHistoryMenuBaseId);

	if (index >= m_history.size())
	{
		return;
	}

	auto const& entry = m_history[index];
	auto folders = entry.folders;
	folders.Replace(";", "\n");

	m_textControl->SetValue(folders);
	m_secondTextControl->SetValue(entry.statusPattern);
	m_idPattern = entry.idPattern;

	SetStatusText("Loaded from history - click Run to search");
	OdsLog(L"History entry loaded: index " + std::to_wstring(index));
}

void MainFrame::OnViewButtonClicked(wxCommandEvent&)
{
	OdsLog(L"View clicked");

	auto* frame = new ViewFrame(m_foundPaths);
	frame->Show(true);
}

void MainFrame::OnChooseButtonClicked(wxCommandEvent&)
{
	OdsLog(L"Choose clicked");

	wxDirDialog dialog(this, "Choose folder", wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

	if (dialog.ShowModal() != wxID_OK)
	{
		OdsLog(L"Choose canceled");
		return;
	}

	auto const selectedPath = dialog.GetPath();
	auto const currentText = m_textControl->GetValue();

	if (!currentText.IsEmpty() && currentText.Last() != '\n')
	{
		m_textControl->AppendText("\n");
	}

	m_textControl->AppendText(selectedPath);
	OdsLog(L"Folder selected: " + dialog.GetPath().ToStdWstring());
}

void MainFrame::OnAdvancedPatternClicked(wxCommandEvent&)
{
	// Deliberately not part of the main window - this is the one place the
	// "find the ID and status text inside a file name" rule lives, as a regex
	// with two capture groups: (id) ... (status text). Nobody but us should
	// ever need to open this.
	wxTextEntryDialog dialog(
		this,
		"Regex with 2 capture groups: (id) ... (status text).\n"
		"Only change this if file names don't match what Run is finding.",
		"Advanced pattern",
		m_idPattern);

	if (dialog.ShowModal() != wxID_OK)
	{
		return;
	}

	auto const text = dialog.GetValue();

	try
	{
		std::wregex const parsed(text.ToStdWstring());

		if (parsed.mark_count() < 2)
		{
			wxMessageBox("Needs two capture groups: (id)...(status text). Keeping the previous pattern.", "Advanced pattern", wxOK | wxICON_ERROR, this);
			return;
		}
	}
	catch (std::regex_error const&)
	{
		wxMessageBox("That isn't a valid regex. Keeping the previous pattern.", "Advanced pattern", wxOK | wxICON_ERROR, this);
		return;
	}

	m_idPattern = text;
	OdsLog(L"Advanced pattern set: " + text.ToStdWstring());
}

void MainFrame::OnRunButtonClicked(wxCommandEvent&)
{
	auto const rootText = m_textControl->GetValue();
	auto const idPatternText = m_idPattern;
	auto const suffixPatternText = m_secondTextControl->GetValue();
	auto roots = GetRoots(rootText.ToStdWstring());

	OdsLog(L"Run clicked");
	OdsLog(L"Root: " + rootText.ToStdWstring());
	OdsLog(L"ID pattern: " + idPatternText.ToStdWstring());
	OdsLog(L"Status pattern: " + suffixPatternText.ToStdWstring());

	if (roots.empty())
	{
		OdsLog(L"Run stopped: no roots");
		SetStatusText("Enter at least one folder first");
		return;
	}

	std::wregex idPattern;

	try
	{
		idPattern = std::wregex(idPatternText.ToStdWstring());

		// Fail fast here rather than silently matching nothing for the whole
		// scan: the pattern needs 2 capture groups (id, status text).
		if (idPattern.mark_count() < 2)
		{
			wxMessageBox("The ID pattern needs two capture groups: (id)...(status text).", "Run", wxOK | wxICON_ERROR, this);
			return;
		}
	}
	catch (std::regex_error const&)
	{
		OdsLog(L"Run stopped: invalid ID pattern");
		wxMessageBox("Invalid ID pattern.", "Run", wxOK | wxICON_ERROR, this);
		return;
	}

	std::wregex suffixPattern;

	try
	{
		suffixPattern = std::wregex(suffixPatternText.ToStdWstring());
	}
	catch (std::regex_error const&)
	{
		OdsLog(L"Run stopped: invalid status pattern");
		wxMessageBox("Invalid status pattern.", "Run", wxOK | wxICON_ERROR, this);
		return;
	}

	SaveSettings();

	auto normalizedFolders = rootText;
	normalizedFolders.Replace("\n", ";");
	RememberSearch(normalizedFolders, suffixPatternText, idPatternText);

	m_listControl->DeleteAllItems();
	SetRunning(true);

	// Start the worker. The lambda gets its OWN copies of everything it needs,
	// so the GUI thread and the worker never share data while it runs.
	m_worker = std::jthread([this, roots = std::move(roots), idPattern = std::move(idPattern), suffixPattern = std::move(suffixPattern)](std::stop_token stop)
		{
			auto const result = Scan(roots, idPattern, suffixPattern, stop, [this](std::size_t scanned, std::size_t dmcMatches)
				{
					// CallAfter is thread-safe: it queues the lambda so it runs later
					// on the GUI thread, where touching controls is allowed.
					CallAfter([this, scanned, dmcMatches]
						{
							OnScanProgress(scanned, dmcMatches);
						});
				});

			CallAfter([this, result]
				{
					OnScanFinished(result);
				});
		});
}

void MainFrame::OnCancelButtonClicked(wxCommandEvent&)
{
	OdsLog(L"Cancel clicked");

	// Only asks the worker to stop. It notices at its next check, sends its
	// partial result, and OnScanFinished then switches the UI back.
	m_worker.request_stop();
	m_cancelButton->Disable();
	SetStatusText("Cancelling...");
}

void MainFrame::OnScanProgress(std::size_t scanned, std::size_t dmcMatches)
{
	m_gauge->Pulse();
	SetStatusText(wxString::Format("Scanning... %zu files checked, %zu DMC matches", scanned, dmcMatches));
}

void MainFrame::OnScanFinished(ScanResult const& result)
{
	m_foundPaths = result.paths;
	m_foundMessages = result.messages;
	PopulateResultsList(m_foundMessages, m_foundPaths);
	SaveResults();

	SetRunning(false);

	if (result.cancelled)
	{
		SetStatusText(wxString::Format("Cancelled. %zu files listed so far (%zu files checked)", result.paths.size(), result.scanned));
	}
	else
	{
		SetStatusText(wxString::Format("Done. %zu files listed (%zu files checked)", result.paths.size(), result.scanned));
	}

	OdsLog(L"Run finished. Listed files: " + std::to_wstring(result.paths.size()));
}

void MainFrame::SetRunning(bool running)
{
	m_runButton->Enable(!running);
	m_cancelButton->Enable(running);

	if (running)
	{
		SetStatusText("Scanning...");
		m_gauge->Pulse();
	}
	else
	{
		m_gauge->SetValue(0);
	}
}