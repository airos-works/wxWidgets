// Cost of many hidden top level windows, as an application like BambuStudio
// creates them at start-up: every combo box owns a drop-down popup, there are
// tool tip and side popups and dialogs which are created early and shown
// later, if ever.
//
//   manywin [--popups=N] [--dialogs=N] [--controls=N] [--wait=SECONDS]
//
// Prints the threads and the memory of this team and of app_server (sum of
// the RAM of their areas) after start-up, after showing and hiding some of
// the windows, and again after waiting (windows hidden for a while may give
// their native resources back). Build with build-test.sh manywin.
#include "wx/wx.h"
#include "wx/popupwin.h"
#include "wx/stopwatch.h"

#include <OS.h>

#include <stdio.h>
#include <string.h>

namespace
{

long gs_popups = 100;
long gs_dialogs = 10;
long gs_controls = 25;
long gs_wait = 0;

struct TeamCost
{
    int32 threads;
    size_t ram;
    int32 areas;
};

TeamCost CostOf(team_id team)
{
    TeamCost cost = { 0, 0, 0 };

    team_info info;
    if ( get_team_info(team, &info) == B_OK )
        cost.threads = info.thread_count;

    ssize_t cookie = 0;
    area_info area;
    while ( get_next_area_info(team, &cookie, &area) == B_OK )
    {
        cost.ram += area.ram_size;
        cost.areas++;
    }

    return cost;
}

team_id FindTeam(const char *name)
{
    int32 cookie = 0;
    team_info info;
    while ( get_next_team_info(&cookie, &info) == B_OK )
    {
        const char *base = strrchr(info.args, '/');
        base = base ? base + 1 : info.args;
        if ( strncmp(base, name, strlen(name)) == 0 )
            return info.team;
    }

    return -1;
}

void Report(const char *when)
{
    team_info self;
    get_team_info(B_CURRENT_TEAM, &self);

    const TeamCost own = CostOf(self.team);
    const team_id server = FindTeam("app_server");
    const TeamCost srv = CostOf(server);

    printf("COST %-28s threads %4d  ram %7.1f MiB (%d areas)  "
           "app_server: threads %4d ram %7.1f MiB (%d areas)\n",
           when, (int)own.threads, own.ram / 1048576.0, (int)own.areas,
           (int)srv.threads, srv.ram / 1048576.0, (int)srv.areas);
    fflush(stdout);
}

// a drop-down like BambuStudio's DropDown: paints itself, no children
class DropDown : public wxPopupTransientWindow
{
public:
    DropDown(wxWindow *parent)
        : wxPopupTransientWindow(parent, wxBORDER_SIMPLE)
    {
        SetSize(200, 160);
        Bind(wxEVT_PAINT, &DropDown::OnPaint, this);
    }

private:
    void OnPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        dc.SetBrush(*wxWHITE_BRUSH);
        dc.DrawRectangle(GetClientRect());
        for ( int n = 0; n < 8; n++ )
            dc.DrawText(wxString::Format("item %d", n), 4, 4 + n * 18);
    }
};

// a side popup with a few controls
class SidePopup : public wxPopupTransientWindow
{
public:
    SidePopup(wxWindow *parent)
        : wxPopupTransientWindow(parent, wxBORDER_SIMPLE)
    {
        wxPanel * const panel = new wxPanel(this);
        wxBoxSizer * const sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(panel, wxID_ANY, "A side popup"),
                   wxSizerFlags().Border());
        sizer->Add(new wxButton(panel, wxID_ANY, "Button"),
                   wxSizerFlags().Border());
        sizer->Add(new wxCheckBox(panel, wxID_ANY, "Check"),
                   wxSizerFlags().Border());
        panel->SetSizer(sizer);
        sizer->Fit(panel);
        SetClientSize(panel->GetSize());
    }
};

class SettingsDialog : public wxDialog
{
public:
    SettingsDialog(wxWindow *parent, int n)
        : wxDialog(parent, wxID_ANY, wxString::Format("Dialog %d", n))
    {
        wxFlexGridSizer * const sizer = new wxFlexGridSizer(2, 4, 4);
        for ( long i = 0; i < gs_controls; i++ )
        {
            sizer->Add(new wxStaticText(this, wxID_ANY,
                                        wxString::Format("Setting %ld", i)));
            if ( i % 3 == 0 )
                sizer->Add(new wxTextCtrl(this, wxID_ANY, "0.20"));
            else if ( i % 3 == 1 )
                sizer->Add(new wxCheckBox(this, wxID_ANY, "on"));
            else
                sizer->Add(new wxButton(this, wxID_ANY, "..."));
        }
        SetSizerAndFit(sizer);
    }
};

class Frame : public wxFrame
{
public:
    Frame()
        : wxFrame(NULL, wxID_ANY, "manywin", wxPoint(40, 40), wxSize(500, 300))
    {
        CreateStatusBar();
        new wxPanel(this);
    }

    void Run();

private:
    void Step(int step);

    wxVector<wxPopupTransientWindow *> m_popups;
    wxVector<wxDialog *> m_dialogs;
};

void Frame::Run()
{
    Report("before the windows");

    wxStopWatch watch;
    for ( long n = 0; n < gs_popups; n++ )
    {
        if ( n % 10 == 9 )
            m_popups.push_back(new SidePopup(this));
        else
            m_popups.push_back(new DropDown(this));
    }
    const long popupTime = watch.Time();

    watch.Start();
    for ( long n = 0; n < gs_dialogs; n++ )
        m_dialogs.push_back(new SettingsDialog(this, (int)n));
    const long dialogTime = watch.Time();

    printf("TIME %ld popups created in %ld ms, %ld dialogs in %ld ms\n",
           gs_popups, popupTime, gs_dialogs, dialogTime);
    fflush(stdout);

    CallAfter([this] { Step(0); });
}

void Frame::Step(int step)
{
    switch ( step )
    {
        case 0:
            Report("all created (hidden)");

            // the user opens some of them
            for ( size_t n = 0; n < 5 && n < m_popups.size(); n++ )
            {
                m_popups[n]->Position(ClientToScreen(wxPoint(20 + 40 * n, 40)),
                                      wxSize(0, 0));
                m_popups[n]->Show();
            }
            for ( size_t n = 0; n < 2 && n < m_dialogs.size(); n++ )
                m_dialogs[n]->Show();
            break;

        case 1:
            Report("5 popups + 2 dialogs shown");
            for ( size_t n = 0; n < 5 && n < m_popups.size(); n++ )
                m_popups[n]->Hide();
            for ( size_t n = 0; n < 2 && n < m_dialogs.size(); n++ )
                m_dialogs[n]->Hide();
            break;

        case 2:
            Report("hidden again");
            if ( gs_wait <= 0 )
            {
                Close(true);
                return;
            }
            break;

        case 3:
            Report("after waiting");

            // shown once more: what it costs to get a window back
            {
                wxStopWatch watch;
                m_popups[0]->Show();
                printf("TIME showing a popup again: %ld ms\n", watch.Time());
                m_popups[0]->Hide();
            }
            Close(true);
            return;
    }

    // let the windows appear and the application settle
    const int delay = step == 2 ? (int)(gs_wait * 1000) : 1500;
    wxTimer * const timer = new wxTimer;
    timer->Bind(wxEVT_TIMER, [this, step, timer](wxTimerEvent&)
    {
        delete timer;
        Step(step + 1);
    });
    timer->StartOnce(delay);
}

class App : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        for ( int n = 1; n < argc; n++ )
        {
            const wxString arg = argv[n];
            long value;
            if ( arg.AfterFirst('=').ToLong(&value) )
            {
                if ( arg.StartsWith("--popups=") )
                    gs_popups = value;
                else if ( arg.StartsWith("--dialogs=") )
                    gs_dialogs = value;
                else if ( arg.StartsWith("--controls=") )
                    gs_controls = value;
                else if ( arg.StartsWith("--wait=") )
                    gs_wait = value;
            }
        }

        Frame * const frame = new Frame;
        frame->Show();
        frame->CallAfter([frame] { frame->Run(); });
        return true;
    }
};

} // anonymous namespace

wxIMPLEMENT_APP(App);
