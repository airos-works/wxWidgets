/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/app.h
// Purpose:     wxApp for the native Haiku (Be API) port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_APP_H_
#define _WX_HAIKU_APP_H_

#include "wx/vidmode.h"

class WXDLLIMPEXP_FWD_BASE wxArrayString;

// wx code runs on the thread that called wxEntry() only. The BApplication
// looper runs on its own thread and every top level window's BWindow on its
// own: they translate BMessages into plain event records which the wx event
// loop drains on the main thread (see src/haiku/evtloop.cpp).
class WXDLLIMPEXP_CORE wxApp : public wxAppBase
{
public:
    wxApp();
    virtual ~wxApp();

    virtual bool Initialize(int& argc, wxChar **argv) wxOVERRIDE;
    virtual void CleanUp() wxOVERRIDE;

    virtual void WakeUpIdle() wxOVERRIDE;
    virtual bool ProcessIdle() wxOVERRIDE;

    virtual wxVideoMode GetDisplayMode() const wxOVERRIDE;
    virtual bool SetDisplayMode(const wxVideoMode& WXUNUSED(mode)) wxOVERRIDE
        { return false; }

    // The application signature the BApplication is created with. By default
    // it is the signature in the executable's resources (BEOS:APP_SIG) or,
    // failing that, "application/x-vnd.wx-<executable name>". Must be called
    // before wxEntry()/wxInitialize() (e.g. from the wxApp constructor).
    static void SetHaikuSignature(const wxString& signature);
    static wxString GetHaikuSignature();

    // Called on the main thread when the BApplication gets B_REFS_RECEIVED
    // (files opened with or dropped on the application), B_ARGV_RECEIVED
    // from another launch of a single-launch application, or is activated
    // again. They have the names of the wxOSX hooks on purpose so that
    // applications can share their document opening code; the default
    // MacOpenFiles() calls MacOpenFile() for every file.
    virtual void MacOpenFiles(const wxArrayString& fileNames);
    virtual void MacOpenFile(const wxString& fileName);
    virtual void MacOpenURL(const wxString& url);
    virtual void MacNewFile();
    virtual void MacReopenApp();

    // B_ARGV_RECEIVED from a second launch: argv[0] is the program. The
    // default treats arguments which are not options as files or URLs to open.
    virtual void HaikuArgvReceived(const wxArrayString& args);

    // B_QUIT_REQUESTED sent to the application (Deskbar, shutdown): the
    // default closes all top level windows, like wxOSX does for Cmd-Q.
    virtual void HaikuQuitRequested();

    // B_ABOUT_REQUESTED: the default sends wxID_ABOUT to the top window.
    virtual void HaikuAboutRequested();

private:
    wxDECLARE_DYNAMIC_CLASS(wxApp);
};

#endif // _WX_HAIKU_APP_H_
