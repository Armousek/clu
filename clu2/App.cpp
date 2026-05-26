#include "App.h"

#include "MainFrame.h"

bool App::OnInit()
{
	auto* frame = new MainFrame();
	frame->Show(true);

	return true;
}
