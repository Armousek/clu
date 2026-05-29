#include "App.h"

#include "MainFrame.h"

bool App::OnInit()
{
	wxInitAllImageHandlers();
	auto* frame = new MainFrame();
	frame->Show(true);

	return true;
}
