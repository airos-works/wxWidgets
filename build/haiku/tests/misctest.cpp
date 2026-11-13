// Test of the small platform pieces of the Haiku port which have no sample:
// prints what the stubs and native pieces answer, shows a notification and
// exits by itself. Options:
//   --launch <file or URL>    wxLaunchDefaultApplication()
//   --browse <URL>            wxLaunchDefaultBrowser()
//   --sound <file>            play it with wxSound
//   --stay <seconds>          keep the window open that long (default 2)
#include "wx/wxprec.h"

#include "wx/wx.h"
#include "wx/artprov.h"
#include "wx/mediactrl.h"
#include "wx/notifmsg.h"
#include "wx/power.h"
#include "wx/sound.h"
#include "wx/stdpaths.h"
#include "wx/taskbar.h"
#include "wx/webview.h"

static void P(const char *what, const wxString& value)
{
    wxPrintf("%-40s %s\n", what, value);
    fflush(stdout);
}

static const char *YesNo(bool b) { return b ? "yes" : "no"; }

class MyFrame : public wxFrame
{
public:
    MyFrame() : wxFrame(NULL, wxID_ANY, "wx misc test", wxDefaultPosition, wxSize(420, 260))
    {
        // a media control must be creatable and fail gracefully in Load()
        m_media = new wxMediaCtrl;
        const bool created = m_media->Create(this, wxID_ANY, wxString(),
                                             wxDefaultPosition, wxSize(200, 100));
        P("wxMediaCtrl::Create (default backend)", YesNo(created));
        if ( created )
        {
            P("wxMediaCtrl::Load(file)", YesNo(m_media->Load("/boot/home/nothing.mp4")));
            P("wxMediaCtrl::Load(URI)", YesNo(m_media->Load(wxURI("http://127.0.0.1/x.mp4"))));
            P("wxMediaCtrl::Play / GetState",
              wxString::Format("%s / %d", YesNo(m_media->Play()), (int)m_media->GetState()));
        }

        wxMediaCtrl * const named = new wxMediaCtrl;
        P("wxMediaCtrl::Create (wxMEDIABACKEND_HAIKU)",
          YesNo(named->Create(this, wxID_ANY, wxString(), wxDefaultPosition,
                              wxSize(10, 10), 0, wxMEDIABACKEND_HAIKU)));
        named->Hide();

        wxMediaCtrl * const other = new wxMediaCtrl;
        P("wxMediaCtrl::Create (wxMEDIABACKEND_GSTREAMER)",
          YesNo(other->Create(this, wxID_ANY, wxString(), wxDefaultPosition,
                              wxSize(10, 10), 0, wxMEDIABACKEND_GSTREAMER)));
        other->Destroy();
    }

    wxMediaCtrl *m_media;
};

class MyApp : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        SetAppName("MiscTest");
        SetAppDisplayName("wx misc test");

        long stay = 2;
        wxString launch, browse, sound;
        for ( int n = 1; n + 1 < argc; n += 2 )
        {
            const wxString opt = argv[n], value = argv[n + 1];
            if ( opt == "--launch" ) launch = value;
            else if ( opt == "--browse" ) browse = value;
            else if ( opt == "--sound" ) sound = value;
            else if ( opt == "--stay" ) value.ToLong(&stay);
        }

        const wxPlatformInfo& pi = wxPlatformInfo::Get();
        P("wxPlatformInfo port", wxString::Format("%s, toolkit %d.%d.%d",
            pi.GetPortIdName(),
            pi.GetToolkitMajorVersion(), pi.GetToolkitMinorVersion(),
            pi.GetToolkitMicroVersion()));
        P("wxGetOsDescription", wxGetOsDescription());
        P("GetUserDataDir", wxStandardPaths::Get().GetUserDataDir());
        P("GetDataDir", wxStandardPaths::Get().GetDataDir());

        MyFrame * const frame = new MyFrame;
        frame->Show();

        // task bar icon: a stub
        P("wxTaskBarIcon::IsAvailable", YesNo(wxTaskBarIcon::IsAvailable()));
        {
            wxTaskBarIcon icon;
            P("wxTaskBarIcon::SetIcon", YesNo(icon.SetIcon(wxArtProvider::GetIcon(wxART_INFORMATION), "tip")));
            P("wxTaskBarIcon::IsOk / IsIconInstalled",
              wxString::Format("%s / %s", YesNo(icon.IsOk()), YesNo(icon.IsIconInstalled())));
            P("wxTaskBarIcon::RemoveIcon", YesNo(icon.RemoveIcon()));
        }

        // web view: common code only
        P("wxWebViewBackendDefault", wxWebViewBackendDefault);
        P("wxWebView::IsBackendAvailable(default)",
          YesNo(wxWebView::IsBackendAvailable(wxWebViewBackendDefault)));
        P("wxWebView::IsBackendAvailable(WebKit)",
          YesNo(wxWebView::IsBackendAvailable(wxWebViewBackendWebKit)));
        wxWebView * const web = wxWebView::New();
        P("wxWebView::New()", web ? "an object (UNEXPECTED)" : "NULL");
        P("wxWebView::New(parent...)",
          wxWebView::New(frame, wxID_ANY) ? "an object (UNEXPECTED)" : "NULL");

        // power: stubs
        {
            wxPowerResourceBlocker blocker(wxPOWER_RESOURCE_SCREEN, "testing");
            P("wxPowerResourceBlocker::IsInEffect", YesNo(blocker.IsInEffect()));
        }
#ifdef wxHAS_POWER_EVENTS
        P("wxHAS_POWER_EVENTS", "defined");
#else
        P("wxHAS_POWER_EVENTS", "not defined");
#endif

        // sound
        {
            wxSound none;
            P("wxSound(): IsOk / Play", wxString::Format("%s / %s", YesNo(none.IsOk()), YesNo(none.Play())));
            wxSound missing("/boot/home/does-not-exist.wav");
            P("wxSound(missing file): IsOk", YesNo(missing.IsOk()));
            if ( !sound.empty() )
            {
                wxSound snd(sound);
                P("wxSound(file): IsOk", YesNo(snd.IsOk()));
                const bool played = snd.Play(wxSOUND_ASYNC);
                P("wxSound(file): Play(async)", YesNo(played));
                P("wxSound::IsPlaying", YesNo(wxSound::IsPlaying()));
                wxMilliSleep(300);
                wxSound::Stop();
                P("wxSound::IsPlaying after Stop", YesNo(wxSound::IsPlaying()));
            }
        }

        // notification: lands in the system's notification area
        {
            wxNotificationMessage note("wx notification",
                "This came from wxNotificationMessage on BNotification.",
                frame, wxICON_INFORMATION);
            P("wxNotificationMessage::Show", YesNo(note.Show()));
            P("wxNotificationMessage::Close", YesNo(note.Close()));

            wxNotificationMessage warn("wx warning", "A second one, with wxICON_WARNING.",
                                       frame, wxICON_WARNING);
            P("wxNotificationMessage(warning)::Show", YesNo(warn.Show(5)));
        }

        if ( !launch.empty() )
            P("wxLaunchDefaultApplication", YesNo(wxLaunchDefaultApplication(launch)));
        if ( !browse.empty() )
            P("wxLaunchDefaultBrowser", YesNo(wxLaunchDefaultBrowser(browse)));

        m_timer.Bind(wxEVT_TIMER, [frame](wxTimerEvent&) { frame->Close(); });
        m_timer.StartOnce(stay * 1000);

        P("done", "window closes by itself");
        return true;
    }

    wxTimer m_timer;
};

wxIMPLEMENT_APP(MyApp);
