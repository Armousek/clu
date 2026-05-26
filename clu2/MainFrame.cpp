#include "MainFrame.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <Windows.h>

namespace
{
	void OdsLog(std::wstring const& message)
	{
		::OutputDebugStringW((L"[clu2] " + message + L"\n").c_str());
	}

	std::wstring Trim(std::wstring value)
	{
		auto const first = std::find_if_not(value.begin(), value.end(), [](wchar_t character)
		{
			return std::iswspace(character) != 0;
		});

		auto const last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character)
		{
			return std::iswspace(character) != 0;
		}).base();

		if (first >= last)
		{
			return {};
		}

		return std::wstring(first, last);
	}

	std::vector<std::filesystem::path> GetRoots(wxString const& text)
	{
		auto rootsText = text.ToStdWstring();
		std::replace(rootsText.begin(), rootsText.end(), L';', L'\n');

		std::vector<std::filesystem::path> roots;
		std::wistringstream stream(rootsText);
		std::wstring line;

		while (std::getline(stream, line))
		{
			auto const root = Trim(line);

			if (!root.empty())
			{
				roots.emplace_back(root);
			}
		}

		return roots;
	}

	bool IsImageFile(std::filesystem::path const& path)
	{
		auto extension = path.extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value)
		{
			return static_cast<wchar_t>(std::towlower(value));
		});

		return extension == L".bmp"
			|| extension == L".gif"
			|| extension == L".jpeg"
			|| extension == L".jpg"
			|| extension == L".png"
			|| extension == L".tif"
			|| extension == L".tiff"
			|| extension == L".webp";
	}

	bool TryExtractDdd(std::filesystem::path const& path, std::wregex const& pattern, std::wstring& ddd)
	{
		auto const stem = path.stem().wstring();
		auto const lastUnderscore = stem.rfind(L'_');

		if (lastUnderscore == std::wstring::npos)
		{
			return false;
		}

		auto const previousUnderscore = stem.rfind(L'_', lastUnderscore - 1);

		if (previousUnderscore == std::wstring::npos)
		{
			return false;
		}

		auto const suffix = stem.substr(lastUnderscore + 1);

		if (!std::regex_search(suffix, pattern))
		{
			return false;
		}

		ddd = stem.substr(previousUnderscore + 1, lastUnderscore - previousUnderscore - 1);

		return !ddd.empty();
	}

	void AddMatchingImages(std::filesystem::path const& directory, std::wstring const& ddd, std::set<std::filesystem::path>& paths)
	{
		std::error_code errorCode;
		auto const before = paths.size();

		OdsLog(L"Scanning parent folder: " + directory.wstring() + L" for DDD: " + ddd);

		for (auto const& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, errorCode))
		{
			if (errorCode)
			{
				break;
			}

			if (!entry.is_regular_file(errorCode) || errorCode)
			{
				errorCode.clear();
				continue;
			}

			auto const path = entry.path();

			if (!IsImageFile(path))
			{
				continue;
			}

			if (path.stem().wstring().find(ddd) == std::wstring::npos)
			{
				continue;
			}

			paths.insert(path);
			OdsLog(L"Matched image: " + path.wstring());
		}

		OdsLog(L"Parent folder matches added: " + std::to_wstring(paths.size() - before));
	}
}

MainFrame::MainFrame()
	: wxFrame(nullptr, wxID_ANY, "clu2", wxDefaultPosition, wxSize(800, 600))
{
	SetBackgroundColour(*wxWHITE);

	auto* menuBar = new wxMenuBar();
	auto* fileMenu = new wxMenu();
	fileMenu->Append(wxID_OPEN, "Choose...");
	menuBar->Append(fileMenu, "File");
	SetMenuBar(menuBar);

	auto* mainSizer = new wxBoxSizer(wxVERTICAL);
	auto* groupSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Input");
	auto* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

	m_textControl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 72), wxTE_MULTILINE);
	m_secondTextControl = new wxTextCtrl(this, wxID_ANY);
	auto* chooseButton = new wxButton(this, wxID_ANY, "Choose");
	auto* runButton = new wxButton(this, wxID_ANY, "Run");
	m_textControl->SetBackgroundColour(*wxWHITE);
	m_secondTextControl->SetBackgroundColour(*wxWHITE);
	chooseButton->SetBackgroundColour(*wxWHITE);
	runButton->SetBackgroundColour(*wxWHITE);

	buttonSizer->AddStretchSpacer();
	buttonSizer->Add(chooseButton, 0, wxRIGHT, 8);
	buttonSizer->Add(runButton, 0);

	groupSizer->Add(m_textControl, 0, wxEXPAND | wxALL, 8);
	groupSizer->Add(m_secondTextControl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	groupSizer->Add(buttonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	m_listControl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_listControl->SetBackgroundColour(*wxWHITE);
	m_listControl->AppendColumn("Text", wxLIST_FORMAT_LEFT, 700);

	mainSizer->Add(groupSizer, 0, wxEXPAND | wxALL, 8);
	mainSizer->Add(m_listControl, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	chooseButton->Bind(wxEVT_BUTTON, &MainFrame::OnChooseButtonClicked, this);
	runButton->Bind(wxEVT_BUTTON, &MainFrame::OnRunButtonClicked, this);
	Bind(wxEVT_MENU, &MainFrame::OnChooseButtonClicked, this, wxID_OPEN);

	SetSizer(mainSizer);
	Centre();
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

void MainFrame::OnRunButtonClicked(wxCommandEvent&)
{
	auto const rootText = m_textControl->GetValue();
	auto const patternText = m_secondTextControl->GetValue();
	auto const roots = GetRoots(rootText);

	OdsLog(L"Run clicked");
	OdsLog(L"Root: " + rootText.ToStdWstring());
	OdsLog(L"Pattern: " + patternText.ToStdWstring());

	if (roots.empty())
	{
		OdsLog(L"Run stopped: no roots");
		return;
	}

	std::wregex pattern;

	try
	{
		pattern = std::wregex(patternText.ToStdWstring());
	}
	catch (std::regex_error const&)
	{
		OdsLog(L"Run stopped: invalid regex pattern");
		wxMessageBox("Invalid regex pattern.", "Run", wxOK | wxICON_ERROR, this);
		return;
	}

	m_listControl->DeleteAllItems();

	std::set<std::filesystem::path> paths;

	for (auto const& root : roots)
	{
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
			if (!errorCode && iterator->is_regular_file(errorCode))
			{
				auto const path = iterator->path();
				auto const dmcDirectory = path.parent_path();

				if (IsImageFile(path) && dmcDirectory.filename().wstring() == L"DMC")
				{
					std::wstring ddd;

					if (TryExtractDdd(path, pattern, ddd))
					{
						OdsLog(L"DMC match: " + path.wstring() + L" DDD: " + ddd);
						AddMatchingImages(dmcDirectory.parent_path(), ddd, paths);
					}
				}
			}

			errorCode.clear();
			iterator.increment(errorCode);
		}
	}

	for (auto const& path : paths)
	{
		auto const index = m_listControl->GetItemCount();
		m_listControl->InsertItem(index, wxString(path.filename().wstring()));
	}

	OdsLog(L"Run finished. Listed files: " + std::to_wstring(paths.size()));
}
