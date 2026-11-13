/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/webview_summit.h
// Purpose:     wxWebView on air/OS' summit_webkit engine (WebKit2, BWebKitView)
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_WEBVIEW_SUMMIT_H_
#define _WX_HAIKU_WEBVIEW_SUMMIT_H_

#include "wx/defs.h"

#if wxUSE_WEBVIEW && wxUSE_WEBVIEW_SUMMIT && defined(__WXHAIKU__)

#include "wx/sharedptr.h"
#include "wx/vector.h"
#include "wx/webview.h"

class wxWebViewSummitImpl;

// The engine's view (a BWebKitView) is a child of this window's own BView
// and gets its input straight from app_server. Its notifications arrive on a
// private looper, are queued, and become wx events on the wx thread; see
// wx/haiku/private/summitwebbackend.h for the threading rules.
class WXDLLIMPEXP_WEBVIEW wxWebViewSummit : public wxWebView
{
public:
    wxWebViewSummit();

    wxWebViewSummit(wxWindow* parent,
           wxWindowID id,
           const wxString& url = wxWebViewDefaultURLStr,
           const wxPoint& pos = wxDefaultPosition,
           const wxSize& size = wxDefaultSize, long style = 0,
           const wxString& name = wxASCII_STR(wxWebViewNameStr));

    virtual ~wxWebViewSummit();

    virtual bool Create(wxWindow* parent,
           wxWindowID id,
           const wxString& url = wxASCII_STR(wxWebViewDefaultURLStr),
           const wxPoint& pos = wxDefaultPosition,
           const wxSize& size = wxDefaultSize, long style = 0,
           const wxString& name = wxASCII_STR(wxWebViewNameStr)) wxOVERRIDE;

    // Website data (cookies, storage, caches). Both before Create(). All
    // web views with the same choice share one data store and one set of
    // engine processes.
    //  - a directory of the application's choice;
    virtual void SetUserDataPathOption(const wxString& path) wxOVERRIDE
        { m_userDataPath = path; }
    //  - nothing kept on disk.
    virtual void SetNonPersistentWebsiteDataStore(bool enable = true) wxOVERRIDE
        { m_nonPersistent = enable; }
    virtual bool IsNonPersistentWebsiteDataStore() const wxOVERRIDE
        { return m_nonPersistent; }
    // Otherwise it is the application's default WebKit profile, unless the
    // application made "nothing on disk" the default for its web views. A
    // persistent profile works in one process at a time: pages of a second
    // instance of the application using the same one do not load.
    static void SetDefaultNonPersistent(bool nonPersistent)
        { ms_defaultNonPersistent = nonPersistent; }
    static bool GetDefaultNonPersistent() { return ms_defaultNonPersistent; }

    virtual void EnableContextMenu(bool enable = true) wxOVERRIDE;
    // summit_webkit has no inspector window of its own (Summit's is part of
    // the browser): the setting is kept but has no effect yet.
    virtual void EnableAccessToDevTools(bool enable = true) wxOVERRIDE
        { m_devTools = enable; }
    virtual bool IsAccessToDevToolsEnabled() const wxOVERRIDE
        { return m_devTools; }

    virtual wxString GetCurrentTitle() const wxOVERRIDE;
    virtual wxString GetCurrentURL() const wxOVERRIDE;
    virtual bool IsBusy() const wxOVERRIDE;
    virtual bool IsEditable() const wxOVERRIDE { return m_editable; }
    virtual void LoadURL(const wxString& url) wxOVERRIDE;
    virtual void Print() wxOVERRIDE;
    virtual void RegisterHandler(wxSharedPtr<wxWebViewHandler> handler) wxOVERRIDE;
    virtual void Reload(wxWebViewReloadFlags flags = wxWEBVIEW_RELOAD_DEFAULT) wxOVERRIDE;
    virtual bool SetUserAgent(const wxString& userAgent) wxOVERRIDE;
    virtual wxString GetUserAgent() const wxOVERRIDE;

    // Returns when the script has run: waits for the engine's answer with a
    // timeout (GetScriptTimeout()), dispatching the events that keep the UI
    // alive meanwhile (paint, timers, pending calls; no user input). The
    // answer does not travel through the wx event queue, so the wait cannot
    // deadlock on it.
    virtual bool RunScript(const wxString& javascript, wxString* output = NULL) const wxOVERRIDE;
    // Queues the script and returns at once; nothing is reported back.
    // (What BambuStudio's WebView::RunScript does with the native API of the
    // other platforms.)
    bool RunScriptNoWait(const wxString& javascript);

    // Removes the cookies called name whose domain contains domainPart (of
    // every domain when it is empty) from this view's website data, which
    // it shares with the other views of the same data store. Asynchronous.
    void DeleteCookiesNamed(const wxString& name,
                            const wxString& domainPart = wxString());

    static void SetScriptTimeout(long milliseconds) { ms_scriptTimeout = milliseconds; }
    static long GetScriptTimeout() { return ms_scriptTimeout; }

    // Every engine view has a web process of its own (110 MiB and more).
    // A wxWebViewSummit therefore makes its engine view only when it is
    // given a page other than about:blank, and, unless this is turned off,
    // gives it back when the application loads about:blank into it again
    // (the page's history goes with it). The events of an about:blank load
    // are sent either way.
    static void SetReleaseEngineOnBlank(bool release) { ms_releaseEngineOnBlank = release; }
    static bool GetReleaseEngineOnBlank() { return ms_releaseEngineOnBlank; }
    // The engine view exists (GetNativeBackend() is not NULL).
    bool HasEngine() const;

    virtual bool AddScriptMessageHandler(const wxString& name) wxOVERRIDE;
    virtual bool RemoveScriptMessageHandler(const wxString& name) wxOVERRIDE;
    virtual bool AddUserScript(const wxString& javascript,
        wxWebViewUserScriptInjectionTime injectionTime = wxWEBVIEW_INJECT_AT_DOCUMENT_START) wxOVERRIDE;
    virtual void RemoveAllUserScripts() wxOVERRIDE;

    virtual void SetEditable(bool enable = true) wxOVERRIDE;
    virtual void Stop() wxOVERRIDE;

    // History. The engine keeps it; ClearHistory() cannot empty the engine's
    // list, so the entries present when it is called are hidden instead:
    // CanGoBack() and GoBack() stop at the page shown at that moment.
    virtual bool CanGoBack() const wxOVERRIDE;
    virtual bool CanGoForward() const wxOVERRIDE;
    virtual void GoBack() wxOVERRIDE;
    virtual void GoForward() wxOVERRIDE;
    virtual void ClearHistory() wxOVERRIDE;
    virtual void EnableHistory(bool enable = true) wxOVERRIDE;
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem> > GetBackwardHistory() wxOVERRIDE;
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem> > GetForwardHistory() wxOVERRIDE;
    virtual void LoadHistoryItem(wxSharedPtr<wxWebViewHistoryItem> item) wxOVERRIDE;

    // Zoom: the whole page (layout); the engine has no text-only factor.
    virtual bool CanSetZoomType(wxWebViewZoomType type) const wxOVERRIDE;
    virtual float GetZoomFactor() const wxOVERRIDE;
    virtual wxWebViewZoomType GetZoomType() const wxOVERRIDE;
    virtual void SetZoomFactor(float zoom) wxOVERRIDE;
    virtual void SetZoomType(wxWebViewZoomType zoomType) wxOVERRIDE;

    virtual void SelectAll() wxOVERRIDE;
    virtual void Cut() wxOVERRIDE;
    virtual void Copy() wxOVERRIDE;
    virtual void Paste() wxOVERRIDE;

    virtual bool CanUndo() const wxOVERRIDE;
    virtual bool CanRedo() const wxOVERRIDE;
    virtual void Undo() wxOVERRIDE;
    virtual void Redo() wxOVERRIDE;

    // The engine's view: a BWebKitView* (<WebKit/WebKitView.h>).
    virtual void* GetNativeBackend() const wxOVERRIDE;

    virtual void SetFocus() wxOVERRIDE;
    virtual bool AcceptsFocus() const wxOVERRIDE { return true; }

protected:
    virtual void DoSetPage(const wxString& html, const wxString& baseUrl) wxOVERRIDE;

private:
    void OnSize(wxSizeEvent& event);

    friend class wxWebViewSummitImpl;

    wxWebViewSummitImpl* m_impl;
    wxString m_userDataPath;
    wxString m_customUserAgent;
    bool m_nonPersistent;
    bool m_devTools;
    bool m_editable;
    wxVector<wxSharedPtr<wxWebViewHandler> > m_handlerList;

    static long ms_scriptTimeout;
    static bool ms_releaseEngineOnBlank;
    static bool ms_defaultNonPersistent;

    wxDECLARE_DYNAMIC_CLASS(wxWebViewSummit);
};

class WXDLLIMPEXP_WEBVIEW wxWebViewFactorySummit : public wxWebViewFactory
{
public:
    virtual wxWebView* Create() wxOVERRIDE { return new wxWebViewSummit; }
    virtual wxWebView* Create(wxWindow* parent,
                              wxWindowID id,
                              const wxString& url = wxWebViewDefaultURLStr,
                              const wxPoint& pos = wxDefaultPosition,
                              const wxSize& size = wxDefaultSize,
                              long style = 0,
                              const wxString& name = wxASCII_STR(wxWebViewNameStr)) wxOVERRIDE
    { return new wxWebViewSummit(parent, id, url, pos, size, style, name); }
    // The engine can be initialised (the summit_webkit package is there and
    // the application is running).
    virtual bool IsAvailable() wxOVERRIDE;
    virtual wxVersionInfo GetVersionInfo() wxOVERRIDE;
};

#endif // wxUSE_WEBVIEW && wxUSE_WEBVIEW_SUMMIT && defined(__WXHAIKU__)

#endif // _WX_HAIKU_WEBVIEW_SUMMIT_H_
