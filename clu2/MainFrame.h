#pragma once

#include "Wx.h"

class wxCommandEvent;

class MainFrame : public wxFrame
{
public:
	MainFrame();

private:
	void OnChooseButtonClicked(wxCommandEvent& event);
	void OnRunButtonClicked(wxCommandEvent& event);
	void OnViewButtonClicked(wxCommandEvent& event);

	wxTextCtrl* m_textControl = nullptr;
	wxTextCtrl* m_secondTextControl = nullptr;
	wxListCtrl* m_listControl = nullptr;
};
