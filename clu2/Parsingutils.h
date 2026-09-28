#pragma once

// Pure logic used by clu2's scan: no wxWidgets types anywhere in this file, on
// purpose. MainFrame.cpp uses these functions directly, and so does the test
// project (clu2_tests), so there is exactly one copy of this logic - never a
// "test version" that could quietly drift from what actually ships.

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <regex>
#include <set>
#include <sstream>
#include <stop_token>
#include <string>
#include <vector>

#include <Windows.h>

namespace clu2
{
	inline void OdsLog(std::wstring const& message)
	{
		::OutputDebugStringW((L"[clu2] " + message + L"\n").c_str());
	}

	inline std::wstring Trim(std::wstring value)
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

	// text is whatever the "Folders" box contains: one path per line, and/or
	// ';'-separated - both are accepted and treated the same way.
	inline std::vector<std::filesystem::path> GetRoots(std::wstring text)
	{
		std::replace(text.begin(), text.end(), L';', L'\n');

		std::vector<std::filesystem::path> roots;
		std::wistringstream stream(text);
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

	inline bool IsImageFile(std::filesystem::path const& path)
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

	// idPattern is user-supplied and must have (at least) two capture groups:
	//   group 1 -> the unit/DDD identifier
	//   group 2 -> the text to test against the separate suffix pattern (e.g. NOK/OK)
	// This used to be a hardcoded "first '_', then later '____'" rule; making it
	// a regex means a different naming scheme needs a new pattern typed into the
	// UI, not a code change.
	inline bool TryExtractDdd(std::filesystem::path const& path, std::wregex const& idPattern, std::wregex const& suffixPattern, std::wstring& ddd)
	{
		auto const stem = path.stem().wstring();
		std::wsmatch match;

		if (!std::regex_search(stem, match, idPattern) || match.size() < 3)
		{
			return false;
		}

		auto const id = match[1].str();
		auto const suffix = match[2].str();

		if (id.empty() || !std::regex_search(suffix, suffixPattern))
		{
			return false;
		}

		ddd = id;

		return true;
	}

	inline std::size_t AddMatchingFiles(std::filesystem::path const& directory, std::wstring const& ddd, std::set<std::filesystem::path>& paths, std::stop_token const& stop)
	{
		std::error_code errorCode;
		auto const before = paths.size();

		OdsLog(L"Scanning parent folder: " + directory.wstring() + L" for DDD: " + ddd);

		for (auto const& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, errorCode))
		{
			if (errorCode || stop.stop_requested())
			{
				break;
			}

			if (!entry.is_regular_file(errorCode) || errorCode)
			{
				errorCode.clear();
				continue;
			}

			auto const path = entry.path();

			if (path.filename().wstring().find(ddd) == std::wstring::npos)
			{
				continue;
			}

			paths.insert(path);
			OdsLog(L"Matched file: " + path.wstring());
		}

		OdsLog(L"Parent folder matches added: " + std::to_wstring(paths.size() - before));

		return paths.size() - before;
	}
}