#pragma once

#include "Wx.h"
#include <filesystem>
#include <vector>

class wxCommandEvent;

class ViewFrame : public wxFrame
{
public:
	ViewFrame(const std::vector<std::filesystem::path>& paths);
private:
	std::vector<std::filesystem::path> m_paths;
};

