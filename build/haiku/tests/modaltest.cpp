// Two modal dialogs opened one after the other, the second from a CallAfter()
// while the first runs (WX-REQUESTS 21): the newer must be in front.
//   modaltest          prints which dialog app_server has in front, then
//                      closes both
#include "wx/wx.h"

#include <Application.h>
#include <Window.h>

#include <stdio.h>

namespace
{

class TestDialog : public wxDialog
{
public:
    TestDialog(wxWindow *parent, const wxString& title, const wxPoint& pos)
        : wxDialog(parent, wxID_ANY, title, pos, wxSize(320, 180))
    {
        new wxStaticText(this, wxID_ANY, title, wxPoint(20, 20));
    }
};

// the BWindow app_server has in front among ours: the first in the list of
// the application's windows that is shown and active, if any
wxString FrontTitle()
{
    for ( int32 n = 0; ; n++ )
    {
        BWindow * const window = be_app->WindowAt(n);
        if ( !window )
            break;

        if ( window->Lock() )
        {
            const bool front = window->IsActive() && !window->IsHidden();
            const wxString title = wxString::FromUTF8(window->Title());
            window->Unlock();
            if ( front )
                return title;
        }
    }

    return "(none active)";
}

} // anonymous namespace

class App : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        wxFrame * const frame = new wxFrame(NULL, wxID_ANY, "modaltest",
                                            wxPoint(40, 40), wxSize(500, 400));
        frame->Show();

        frame->CallAfter([frame]
        {
            TestDialog first(frame, "first dialog", wxPoint(100, 120));

            first.CallAfter([frame, &first]
            {
                TestDialog second(frame, "second dialog", wxPoint(160, 180));

                wxTimer timer;
                timer.Bind(wxEVT_TIMER, [&second](wxTimerEvent&)
                {
                    printf("front while both are open: %s\n",
                           (const char *)FrontTitle().utf8_str());
                    fflush(stdout);
                    second.EndModal(wxID_OK);
                });
                timer.StartOnce(2000);

                second.ShowModal();

                // (outlives this function: the first dialog runs on)
                wxTimer * const timer2 = new wxTimer;
                timer2->Bind(wxEVT_TIMER, [&first, timer2](wxTimerEvent&)
                {
                    printf("front after the second closed: %s\n",
                           (const char *)FrontTitle().utf8_str());
                    fflush(stdout);
                    first.EndModal(wxID_OK);
                    timer2->Stop();
                });
                timer2->StartOnce(1000);
            });

            first.ShowModal();
            frame->Close(true);
        });

        return true;
    }
};

wxIMPLEMENT_APP(App);
