#include "ViewFrame.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <Windows.h>
#include <wx/wrapsizer.h>

namespace
{
    void OdsLog(std::wstring const& message)
    {
        ::OutputDebugStringW((L"[clu2] " + message + L"\n").c_str());
    }
}

ViewFrame::ViewFrame(const std::vector<std::filesystem::path>& paths)
    : wxFrame(nullptr, wxID_ANY, "picture viewer", wxDefaultPosition, wxSize(800, 600))
    , m_paths(paths)
{
    SetBackgroundColour(*wxWHITE);

    auto* scrolled = new wxScrolledWindow(this, wxID_ANY);
    scrolled->SetBackgroundColour(*wxWHITE);

    auto* wrapSizer = new wxWrapSizer(wxHORIZONTAL);

    for (auto const& path : m_paths)
    {
        wxImage img(path.wstring(), wxBITMAP_TYPE_ANY);

        if (!img.IsOk())
        {
            OdsLog(L"Failed to load picture");
            continue;
        }

        img = img.Scale(200, 200, wxIMAGE_QUALITY_HIGH);
        wxBitmap bmp(img);

        auto* picture = new wxStaticBitmap(scrolled, wxID_ANY, bmp);
        wrapSizer->Add(picture, 0, wxALL, 4);
    }

    scrolled->SetSizer(wrapSizer);
    scrolled->SetScrollRate(10, 10);
    scrolled->FitInside();

    auto* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(scrolled, 1, wxEXPAND);
    SetSizer(mainSizer);
}