// The first click into an inactive window (WX-REQUESTS 26): it must act as
// well as activate, as in native applications. Frame "clicktest A" at
// (40, 40) has a wxAuiToolBar tool at the top left and a wxButton below it,
// frame "clicktest B" at (500, 40) is activated after start. Click the tool or
// the button of A once: the line "TOOL" or "BUTTON" must be printed.
// The second tool pops up a menu from its dropdown event as BambuStudio's
// File item does ("DROPDOWN", the menu must stay open, "MENU CLOSED" after).
// Prints ACTIVATE lines too, to see the order.
#include "wx/wx.h"
#include "wx/aui/auibar.h"
#include "wx/artprov.h"

#include <stdio.h>

class App : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        wxFrame * const a = new wxFrame(NULL, wxID_ANY, "clicktest A",
                                        wxPoint(40, 40), wxSize(400, 300));
        wxPanel * const panel = new wxPanel(a);
        wxAuiToolBar * const bar = new wxAuiToolBar(panel, wxID_ANY,
                                                    wxPoint(0, 0),
                                                    wxSize(380, 40));
        bar->AddTool(wxID_OPEN, "Open",
                     wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_TOOLBAR));
        // the File menu of BambuStudio's top bar: a menu popped up from the
        // dropdown event, which wxAuiToolBar sends on the button press
        bar->AddTool(wxID_SAVE, "Menu",
                     wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_TOOLBAR));
        bar->SetToolDropDown(wxID_SAVE, true);
        bar->Realize();
        bar->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, [bar](wxAuiToolBarEvent&)
        {
            printf("DROPDOWN\n");
            fflush(stdout);
            wxMenu menu;
            menu.Append(wxID_ANY, "Item");
            bar->PopupMenu(&menu, wxPoint(40, 40));
            printf("MENU CLOSED\n");
            fflush(stdout);
        }, wxID_SAVE);
        bar->Bind(wxEVT_TOOL, [](wxCommandEvent&)
        {
            printf("TOOL\n");
            fflush(stdout);
        });
        wxButton * const button = new wxButton(panel, wxID_ANY, "Button",
                                               wxPoint(20, 80));
        button->Bind(wxEVT_BUTTON, [](wxCommandEvent&)
        {
            printf("BUTTON\n");
            fflush(stdout);
        });
        wxSizer * const sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(bar, wxSizerFlags().Expand());
        sizer->Add(button, wxSizerFlags().Border());
        panel->SetSizer(sizer);

        wxFrame * const b = new wxFrame(NULL, wxID_ANY, "clicktest B",
                                        wxPoint(500, 40), wxSize(300, 200));

        const auto report = [](wxActivateEvent& event)
        {
            printf("ACTIVATE %s %d\n",
                   (const char *)static_cast<wxTopLevelWindow *>(event.GetEventObject())
                        ->GetTitle().utf8_str(),
                   (int)event.GetActive());
            fflush(stdout);
            event.Skip();
        };
        a->Bind(wxEVT_ACTIVATE, report);
        b->Bind(wxEVT_ACTIVATE, report);

        a->Show();
        b->Show();
        b->CallAfter([b] { b->Raise(); });
        return true;
    }
};

wxIMPLEMENT_APP(App);
