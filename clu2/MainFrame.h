#pragma once

#include "Wx.h"
#include <cstddef>
#include <filesystem>
#include <regex>
#include <string>
#include <thread>
#include <vector>

// Everything the worker thread found. It is built on the worker thread and
// handed to the GUI thread once, when the scan ends.
struct ScanResult
{
	std::vector<std::filesystem::path> paths;
	std::vector<std::wstring> messages;
	std::size_t scanned = 0;
	bool cancelled = false;
};

// One remembered search: what was in the boxes when Run was last clicked with
// that combination. Folders are stored ';'-joined, same convention as
// SaveSettings uses - see its comment for why.
struct SearchHistoryEntry
{
	wxString folders;
	wxString statusPattern;
	wxString idPattern;
};

class MainFrame : public wxFrame
{
public:
	MainFrame();
	~MainFrame() override;

private:
	void OnChooseButtonClicked(wxCommandEvent& event);
	void OnRunButtonClicked(wxCommandEvent& event);
	void OnCancelButtonClicked(wxCommandEvent& event);
	void OnViewButtonClicked(wxCommandEvent& event);
	void OnAdvancedPatternClicked(wxCommandEvent& event);
	void OnHistoryItemClicked(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

	void LoadSettings();
	void SaveSettings();
	void LoadResults();
	void SaveResults();
	void PopulateResultsList(std::vector<std::wstring> const& messages, std::vector<std::filesystem::path> const& paths);

	void LoadHistory();
	void RememberSearch(wxString const& folders, wxString const& statusPattern, wxString const& idPattern);
	void RebuildHistoryMenu();

	// These two are always called on the GUI thread (via CallAfter).
	void OnScanProgress(std::size_t scanned, std::size_t dmcMatches);
	void OnScanFinished(ScanResult const& result);

	void SetRunning(bool running);

	wxTextCtrl* m_textControl = nullptr;
	wxTextCtrl* m_secondTextControl = nullptr;
	wxListCtrl* m_listControl = nullptr;
	wxButton* m_runButton = nullptr;
	wxButton* m_cancelButton = nullptr;
	wxGauge* m_gauge = nullptr;
	wxMenu* m_historyMenu = nullptr;
	std::vector<std::filesystem::path> m_foundPaths;
	std::vector<std::wstring> m_foundMessages;
	std::vector<SearchHistoryEntry> m_history; // newest first, capped

	// Not shown in the main window on purpose - nobody watching a demo needs
	// to see a regex. Only File > Advanced ever touches this. Default
	// reproduces the original hardcoded rule (first '_', skip repeats, then
	// somewhere later "____", capturing the id and the text after it).
	wxString m_idPattern = "^([^_]+)_+.*?____(.*)$";

	// Keep this LAST: members are destroyed in reverse order, and the
	// jthread destructor stops and joins the worker before anything else goes.
	std::jthread m_worker;
};