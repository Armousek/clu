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

ViewFrame::ViewFrame()
	: wxFrame(nullptr, wxID_ANY, "picture viewer", wxDefaultPosition, wxSize(800, 600))
{
	SetBackgroundColour(*wxWHITE);


}