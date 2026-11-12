/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/app.cpp
// Purpose:     wxApp for the native Haiku port and the BApplication thread
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/log.h"
    #include "wx/toplevel.h"
    #include "wx/frame.h"
#endif

#include "wx/evtloop.h"
#include "wx/filename.h"
#include "wx/stdpaths.h"

#include "wx/haiku/private.h"

#include <AppFileInfo.h>
#include <Entry.h>
#include <File.h>
#include <Path.h>
#include <Screen.h>
#include <image.h>

// ============================================================================
// the BApplication
// ============================================================================

namespace
{

const uint32 kMsgReallyQuit = 'wxQT';

// The BApplication only turns its messages into event records for the wx
// thread: it never calls wx.
class wxHaikuApplication : public BApplication
{
public:
    wxHaikuApplication(const char *signature)
        : BApplication(signature),
          m_ready(false),
          m_reallyQuit(false)
    {
    }

    virtual void ReadyToRun()
    {
        m_ready = true;
    }

    virtual void RefsReceived(BMessage *message)
    {
        wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_APP_REFS);

        entry_ref ref;
        for ( int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++ )
        {
            BPath path(&ref);
            if ( path.InitCheck() == B_OK )
                event->strings.push_back(path.Path());
        }

        if ( event->strings.empty() )
            delete event;
        else
            wxHaikuPostEvent(event);
    }

    virtual void ArgvReceived(int32 argc, char **argv)
    {
        // The arguments of this launch are delivered here too, before
        // ReadyToRun(): wx got them from main() already.
        if ( !m_ready )
            return;

        wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_APP_ARGV);
        for ( int32 i = 0; i < argc; i++ )
            event->strings.push_back(argv[i]);
        wxHaikuPostEvent(event);
    }

    virtual bool QuitRequested()
    {
        if ( m_reallyQuit )
            return true;

        // let wx close its windows: the application ends when the main
        // loop does
        wxHaikuPostEvent(new wxHaikuEvent(wxHAIKU_EVT_APP_QUIT));
        return false;
    }

    virtual void AboutRequested()
    {
        wxHaikuPostEvent(new wxHaikuEvent(wxHAIKU_EVT_APP_ABOUT));
    }

    virtual void AppActivated(bool active)
    {
        wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_APP_ACTIVATED);
        event->flag = active;
        wxHaikuPostEvent(event);
    }

    virtual void MessageReceived(BMessage *message)
    {
        switch ( message->what )
        {
            case kMsgReallyQuit:
                m_reallyQuit = true;
                Quit();
                break;

            case B_COLORS_UPDATED:
                wxHaikuPostEvent(new wxHaikuEvent(wxHAIKU_EVT_COLORS_UPDATED));
                BApplication::MessageReceived(message);
                break;

            case B_FONTS_UPDATED:
                wxHaikuPostEvent(new wxHaikuEvent(wxHAIKU_EVT_FONTS_UPDATED));
                BApplication::MessageReceived(message);
                break;

            default:
                BApplication::MessageReceived(message);
        }
    }

private:
    bool m_ready;
    bool m_reallyQuit;
};

thread_id gs_appThread = -1;
sem_id gs_appReady = -1;
std::string gs_signature;

// path of the executable of this team
wxString GetExecutablePath()
{
    image_info info;
    int32 cookie = 0;
    while ( get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK )
    {
        if ( info.type == B_APP_IMAGE )
            return wxString::FromUTF8(info.name);
    }

    return wxString();
}

std::string GetDefaultSignature()
{
    const wxString path = GetExecutablePath();

    // the signature in the resources or attributes of the executable
    BFile file(path.utf8_str(), B_READ_ONLY);
    if ( file.InitCheck() == B_OK )
    {
        BAppFileInfo info(&file);
        char signature[B_MIME_TYPE_LENGTH];
        if ( info.InitCheck() == B_OK && info.GetSignature(signature) == B_OK
                && signature[0] )
        {
            return signature;
        }
    }

    wxString name = wxFileName(path).GetName();
    if ( name.empty() )
        name = "app";

    std::string signature = "application/x-vnd.wx-";
    const wxScopedCharBuffer utf8 = name.utf8_str();
    for ( const char *p = utf8; *p; p++ )
    {
        const unsigned char c = *p;
        signature += (isalnum(c) || c == '-' || c == '_' || c == '.')
                        ? (char)c : '_';
    }

    return signature;
}

int32 AppThreadEntry(void *)
{
    BApplication *app = new wxHaikuApplication(gs_signature.c_str());

    release_sem(gs_appReady);

    app->Run();

    // all windows and bitmaps are gone when wxApp::CleanUp() asks us to quit
    delete app;

    return 0;
}

bool StartApplicationThread()
{
    if ( be_app )
        return true;

    if ( gs_signature.empty() )
        gs_signature = GetDefaultSignature();

    gs_appReady = create_sem(0, "wx app ready");
    gs_appThread = spawn_thread(AppThreadEntry, "wx BApplication",
                                B_NORMAL_PRIORITY, NULL);
    if ( gs_appThread < 0 || resume_thread(gs_appThread) != B_OK )
        return false;

    // the BApplication constructor connects to app_server: nothing of the
    // Interface Kit may be used before
    while ( acquire_sem(gs_appReady) == B_INTERRUPTED )
        ;
    delete_sem(gs_appReady);
    gs_appReady = -1;

    return be_app != NULL;
}

void StopApplicationThread()
{
    if ( gs_appThread < 0 )
        return;

    if ( be_app )
        be_app->PostMessage(kMsgReallyQuit);

    status_t result;
    wait_for_thread(gs_appThread, &result);
    gs_appThread = -1;
}

} // anonymous namespace

// ============================================================================
// wxApp
// ============================================================================

wxIMPLEMENT_DYNAMIC_CLASS(wxApp, wxEvtHandler);

wxApp::wxApp()
{
}

wxApp::~wxApp()
{
}

/* static */
void wxApp::SetHaikuSignature(const wxString& signature)
{
    gs_signature = signature.utf8_str();
}

/* static */
wxString wxApp::GetHaikuSignature()
{
    if ( gs_signature.empty() )
        gs_signature = GetDefaultSignature();

    return wxString::FromUTF8(gs_signature.c_str());
}

bool wxApp::Initialize(int& argc_, wxChar **argv_)
{
    if ( !wxAppBase::Initialize(argc_, argv_) )
        return false;

    // the queue must exist before the loopers can post to it
    wxHaikuGetQueueFD();

    if ( !StartApplicationThread() )
    {
        wxLogError(_("Could not connect to the application server."));
        return false;
    }

    return true;
}

void wxApp::CleanUp()
{
    wxAppBase::CleanUp();

    wxHaikuFreePaintBuffers();
    wxHaikuFreeBitmapViews();

    StopApplicationThread();
}

void wxApp::WakeUpIdle()
{
    // may be called from any thread
    wxEventLoopBase * const loop = wxEventLoopBase::GetActive();
    if ( loop )
        loop->WakeUp();
}

bool wxApp::ProcessIdle()
{
    // The paint phase: everything the event handlers invalidated is painted
    // once the queue is empty, before the idle events.
    wxHaikuPaintPendingWindows();

    const bool needMore = wxAppBase::ProcessIdle();

    // idle handlers usually update the UI: show it now
    wxHaikuPaintPendingWindows();

    return needMore;
}

wxVideoMode wxApp::GetDisplayMode() const
{
    BScreen screen(B_MAIN_SCREEN_ID);
    const BRect frame = screen.Frame();

    int depth = 32;
    switch ( screen.ColorSpace() )
    {
        case B_CMAP8:
            depth = 8;
            break;
        case B_RGB15:
            depth = 15;
            break;
        case B_RGB16:
            depth = 16;
            break;
        case B_RGB24:
            depth = 24;
            break;
        default:
            break;
    }

    return wxVideoMode(frame.IntegerWidth() + 1, frame.IntegerHeight() + 1,
                       depth);
}

// ----------------------------------------------------------------------------
// application events
// ----------------------------------------------------------------------------

void wxApp::MacOpenFiles(const wxArrayString& fileNames)
{
    for ( size_t n = 0; n < fileNames.size(); n++ )
        MacOpenFile(fileNames[n]);
}

void wxApp::MacOpenFile(const wxString& WXUNUSED(fileName))
{
}

void wxApp::MacOpenURL(const wxString& WXUNUSED(url))
{
}

void wxApp::MacNewFile()
{
}

void wxApp::MacReopenApp()
{
}

void wxApp::HaikuArgvReceived(const wxArrayString& args)
{
    wxArrayString files;
    for ( size_t n = 1; n < args.size(); n++ )
    {
        const wxString& arg = args[n];
        if ( arg.empty() || arg[0] == '-' )
            continue;

        if ( arg.Contains("://") )
            MacOpenURL(arg);
        else
            files.push_back(arg);
    }

    if ( !files.empty() )
        MacOpenFiles(files);
}

void wxApp::HaikuQuitRequested()
{
    // close the windows as if the user did: any of them may veto
    wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetLast();
    while ( node )
    {
        wxWindow * const win = node->GetData();
        node = node->GetPrevious();

        if ( !win->Close() )
            return;
    }
}

void wxApp::HaikuAboutRequested()
{
    wxWindow * const win = GetTopWindow();
    if ( win )
    {
        wxCommandEvent event(wxEVT_MENU, wxID_ABOUT);
        event.SetEventObject(win);
        win->HandleWindowEvent(event);
    }
}
