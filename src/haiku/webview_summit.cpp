/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/webview_summit.cpp
// Purpose:     wxWebView on air/OS' summit_webkit engine (WebKit2, BWebKitView)
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_WEBVIEW && wxUSE_WEBVIEW_SUMMIT

#include "wx/haiku/webview_summit.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/intl.h"
    #include "wx/log.h"
#endif

#include "wx/evtloop.h"
#include "wx/filesys.h"
#include "wx/mstream.h"
#include "wx/stream.h"

#include "wx/haiku/private.h"
#include "wx/haiku/private/summitwebbackend.h"

#include <stdio.h>
#include <string.h>

#include <deque>
#include <map>
#include <mutex>

using summitweb::SummitWebBackend;

// ----------------------------------------------------------------------------
// wxWebViewSummitImpl: the engine and the queue between its thread and wx
// ----------------------------------------------------------------------------

class wxWebViewSummitImpl : public wxHaikuCallbackTarget
{
public:
    explicit wxWebViewSummitImpl(wxWebViewSummit *owner)
        : m_historyFloor(false),
          m_backDepth(0),
          m_forwardDepth(0),
          m_historyMove(0),
          m_zoom(1.0),
          m_blankSerial(0),
          m_vetoed(0),
          m_owner(owner),
          m_backend(NULL),
          m_notified(false)
    {
        NewBackend();
    }

    virtual ~wxWebViewSummitImpl()
    {
        // After Destroy() the sink is not called any more; records still
        // queued are dropped with the queue, and a wake-up still under way
        // finds no target (handles are never reused).
        delete m_backend;
    }

    // The engine's view exists. It is made when the first page other than
    // about:blank is asked for (EnsureEngine()): every engine view has a web
    // process of its own from the moment it is created, 110 MiB and more,
    // and applications create web views they may never show a page in.
    bool HasEngine() const { return m_backend->IsCreated(); }
    bool EnsureEngine();
    // Back to the state before EnsureEngine(): the view's web process ends
    // (and the engine's other processes with the last view).
    void ReleaseEngine();

    // LoadURL("about:blank") without an engine: the events of that load
    void SynthesizeBlank(const wxString& url);

    SummitWebBackend& Backend() { return *m_backend; }

    // wx thread: the wake-up posted by the sink
    virtual void HaikuCallback(const wxHaikuEvent& WXUNUSED(event)) wxOVERRIDE
    {
        Dispatch();
    }

    // Turns everything queued into wx events. Also called while a
    // synchronous script waits, so handlers may run nested.
    void Dispatch()
    {
        for ( ;; )
        {
            summitweb::Event event;
            {
                std::lock_guard<std::mutex> guard(m_lock);
                if ( m_queue.empty() )
                {
                    m_notified = false;
                    return;
                }
                event = std::move(m_queue.front());
                m_queue.pop_front();
            }
            Handle(event);
        }
    }

    void Handle(const summitweb::Event& event);

    // ClearHistory() emulation, see the header
    bool m_historyFloor;
    int m_backDepth;
    int m_forwardDepth;
    int m_historyMove;      // -1 back, +1 forward, 2 reload: the next commit

    // what an engine view made later starts with
    std::vector<std::string> m_handlers;
    std::vector<std::pair<std::string, bool> > m_userScripts;
    double m_zoom;
    wxString m_blankURL;    // the address shown while there is no engine
    unsigned m_blankSerial;
    wxUint64 m_vetoed;      // the load whose NAVIGATING was vetoed last

    // DeleteCookiesNamed(): the cookie listings asked for, by request, with
    // the name and the part of the domain to look for
    std::map<wxUint64, std::pair<std::string, std::string> > m_cookieJobs;

private:
    void NewBackend();
    void ServeSchemeRequest(const summitweb::Event& event);

    wxWebViewSummit *m_owner;
    SummitWebBackend *m_backend;

    std::mutex m_lock;
    std::deque<summitweb::Event> m_queue;
    bool m_notified;

    wxDECLARE_NO_COPY_CLASS(wxWebViewSummitImpl);
};

void wxWebViewSummitImpl::NewBackend()
{
    delete m_backend;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_queue.clear();
        m_notified = false;
    }

    // The sink runs on the engine's listener thread: it only queues the
    // record and wakes the wx thread, once per batch.
    m_backend = new SummitWebBackend(
        [this](summitweb::Event&& event)
        {
            bool notify;
            {
                std::lock_guard<std::mutex> guard(m_lock);
                m_queue.push_back(std::move(event));
                notify = !m_notified;
                m_notified = true;
            }
            if ( notify )
            {
                wxHaikuPostEvent(new wxHaikuEvent(wxHAIKU_EVT_CALLBACK,
                                                  GetHaikuCallbackHandle()));
            }
        });
}

bool wxWebViewSummitImpl::EnsureEngine()
{
    if ( HasEngine() )
        return true;

    wxWebViewSummit * const win = m_owner;
    BView * const host = win->GetHaikuView();
    if ( !host )
        return false;

    summitweb::Options options;
    options.privateSession = win->m_nonPersistent;
    options.profilePath = std::string(win->m_userDataPath.utf8_str());
    options.userAgent = std::string(win->m_customUserAgent.utf8_str());
    // The engine's view fills ours and follows it natively while the window
    // is resized; wxWebViewSummit::OnSize() makes sure of it.
    options.resizingMode = B_FOLLOW_ALL;

    BRect bounds;
    {
        wxHaikuViewLock lock(host);
        bounds = host->Bounds();
    }

    const status_t status = m_backend->Create(host, bounds, options);
    if ( status != B_OK )
    {
        wxLogDebug("wxWebViewSummit: no web view (%s)", strerror(status));
        return false;
    }

    // Everything set while there was no engine, in the order the engine
    // wants it: schemes, handlers and scripts before the first load.
    m_backend->EnableContextMenu(win->IsContextMenuEnabled());
    for ( size_t n = 0; n < win->m_handlerList.size(); n++ )
    {
        m_backend->RegisterScheme(
            std::string(win->m_handlerList[n]->GetName().utf8_str()));
    }
    for ( size_t n = 0; n < m_handlers.size(); n++ )
        m_backend->AddScriptMessageHandler(m_handlers[n]);
    for ( size_t n = 0; n < m_userScripts.size(); n++ )
        m_backend->AddUserScript(m_userScripts[n].first, m_userScripts[n].second);
    if ( m_zoom != 1.0 )
        m_backend->SetZoomFactor(m_zoom);

    m_blankURL.clear();
    m_historyFloor = false;
    m_backDepth = m_forwardDepth = m_historyMove = 0;
    return true;
}

void wxWebViewSummitImpl::ReleaseEngine()
{
    if ( HasEngine() )
        NewBackend();
}

void wxWebViewSummitImpl::SynthesizeBlank(const wxString& url)
{
    m_blankURL = url;
    const unsigned serial = ++m_blankSerial;
    if ( url.empty() )
        return;

    // Later, as the engine's own events come: the application binds its
    // handlers after Create().
    wxWebViewSummit * const win = m_owner;
    win->CallAfter([this, win, url, serial]()
        {
            if ( serial != m_blankSerial || HasEngine() )
                return;

            wxWebViewEvent navigating(wxEVT_WEBVIEW_NAVIGATING, win->GetId(),
                                      url, wxString());
            navigating.SetEventObject(win);
            win->HandleWindowEvent(navigating);
            if ( !navigating.IsAllowed() || serial != m_blankSerial || HasEngine() )
                return;

            wxWebViewEvent navigated(wxEVT_WEBVIEW_NAVIGATED, win->GetId(),
                                     url, wxString());
            navigated.SetEventObject(win);
            win->HandleWindowEvent(navigated);
            if ( serial != m_blankSerial || HasEngine() )
                return;

            wxWebViewEvent loaded(wxEVT_WEBVIEW_LOADED, win->GetId(),
                                  url, wxString());
            loaded.SetEventObject(win);
            win->HandleWindowEvent(loaded);
        });
}

void wxWebViewSummitImpl::Handle(const summitweb::Event& ev)
{
    wxWebViewSummit * const win = m_owner;
    const wxString url = wxString::FromUTF8(ev.url.c_str());

    switch ( ev.type )
    {
        case summitweb::EventType::Navigating:
            {
                wxWebViewEvent event(wxEVT_WEBVIEW_NAVIGATING, win->GetId(),
                                     url, wxString(),
                                     ev.userGesture ? wxWEBVIEW_NAV_ACTION_USER
                                                    : wxWEBVIEW_NAV_ACTION_OTHER);
                event.SetEventObject(win);
                win->HandleWindowEvent(event);

                // A load the engine cannot show (bambu://close, app://...) is
                // dropped by it already; any other is stopped while it is
                // still provisional.
                if ( !event.IsAllowed() && !ev.blocked )
                {
                    m_historyMove = 0;
                    m_vetoed = ev.id;
                    m_backend->Veto(ev.id);
                }
            }
            break;

        case summitweb::EventType::Navigated:
            {
                switch ( m_historyMove )
                {
                    case -1:
                        m_backDepth--;
                        m_forwardDepth++;
                        break;
                    case 1:
                        m_backDepth++;
                        m_forwardDepth--;
                        break;
                    case 2:
                        break;
                    default:
                        m_backDepth++;
                        m_forwardDepth = 0;
                }
                m_historyMove = 0;

                wxWebViewEvent event(wxEVT_WEBVIEW_NAVIGATED, win->GetId(),
                                     url, wxString());
                event.SetEventObject(win);
                win->HandleWindowEvent(event);
            }
            break;

        case summitweb::EventType::Loaded:
            {
                wxWebViewEvent event(wxEVT_WEBVIEW_LOADED, win->GetId(),
                                     url, wxString());
                event.SetEventObject(win);
                win->HandleWindowEvent(event);
            }
            break;

        case summitweb::EventType::Error:
            {
                m_historyMove = 0;

                // The application vetoed this load: that it also failed
                // (before the veto could stop it) is not its business.
                if ( ev.id && ev.id == m_vetoed )
                    break;

                wxWebViewEvent event(wxEVT_WEBVIEW_ERROR, win->GetId(),
                                     url, wxString());
                event.SetEventObject(win);
                event.SetString(wxString::FromUTF8(ev.text.c_str()));
                // summitweb::ErrorClass is in the order of
                // wxWebViewNavigationError
                event.SetInt(static_cast<int>(ev.errorClass));
                win->HandleWindowEvent(event);
            }
            break;

        case summitweb::EventType::NewWindow:
            {
                // The engine has declined the request already: as with wx's
                // other backends the application opens what it wants itself.
                wxWebViewEvent event(wxEVT_WEBVIEW_NEWWINDOW, win->GetId(),
                                     url, wxString(),
                                     ev.userGesture ? wxWEBVIEW_NAV_ACTION_USER
                                                    : wxWEBVIEW_NAV_ACTION_OTHER);
                event.SetEventObject(win);
                win->HandleWindowEvent(event);
            }
            break;

        case summitweb::EventType::TitleChanged:
            {
                wxWebViewEvent event(wxEVT_WEBVIEW_TITLE_CHANGED, win->GetId(),
                                     url, wxString());
                event.SetEventObject(win);
                event.SetString(wxString::FromUTF8(ev.text.c_str()));
                win->HandleWindowEvent(event);
            }
            break;

        case summitweb::EventType::ScriptMessage:
            {
                wxWebViewEvent event(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
                                     win->GetId(), url, wxString(),
                                     wxWEBVIEW_NAV_ACTION_NONE,
                                     wxString::FromUTF8(ev.name.c_str()));
                event.SetEventObject(win);
                event.SetString(wxString::FromUTF8(ev.text.c_str()));
                win->HandleWindowEvent(event);
            }
            break;

        case summitweb::EventType::SchemeRequest:
            ServeSchemeRequest(ev);
            break;

        case summitweb::EventType::FullscreenChanged:
            {
                wxWebViewEvent event(wxEVT_WEBVIEW_FULLSCREEN_CHANGED,
                                     win->GetId(), wxString(), wxString());
                event.SetEventObject(win);
                event.SetInt(ev.flag ? 1 : 0);
                win->HandleWindowEvent(event);
            }
            break;

        case summitweb::EventType::ProcessExited:
            {
                // wx 3.1.5 has no event for it: report it as a failed load
                // so that the application at least hears of it. Reload()
                // starts a new web process.
                wxWebViewEvent event(wxEVT_WEBVIEW_ERROR, win->GetId(),
                                     win->GetCurrentURL(), wxString());
                event.SetEventObject(win);
                event.SetString(wxString::Format("The web process exited (%s)",
                                    wxString::FromUTF8(ev.text.c_str())));
                event.SetInt(wxWEBVIEW_NAV_ERR_OTHER);
                win->HandleWindowEvent(event);
            }
            break;

        case summitweb::EventType::Cookies:
            {
                std::map<wxUint64, std::pair<std::string, std::string> >::iterator
                    job = m_cookieJobs.find(ev.id);
                if ( job == m_cookieJobs.end() )
                    break;
                const std::string name = job->second.first;
                const std::string domainPart = job->second.second;
                m_cookieJobs.erase(job);

                for ( size_t n = 0; n < ev.cookies.size(); n++ )
                {
                    const summitweb::Cookie& cookie = ev.cookies[n];
                    if ( cookie.name != name
                            || cookie.domain.find(domainPart) == std::string::npos )
                        continue;

                    // the engine deletes by address: one the cookie is sent to
                    std::string host = cookie.domain;
                    if ( !host.empty() && host[0] == '.' )
                        host.erase(0, 1);
                    m_backend->DeleteCookies(
                        (cookie.secure ? "https://" : "http://") + host
                            + (cookie.path.empty() ? std::string("/") : cookie.path),
                        name);
                }
            }
            break;

        case summitweb::EventType::ScriptResult:
        case summitweb::EventType::SchemeRequestStopped:
            break;
    }
}

// A wxWebViewHandler's file, read here on the wx thread (wxFileSystem is not
// thread-safe) and handed to the engine whole.
void wxWebViewSummitImpl::ServeSchemeRequest(const summitweb::Event& ev)
{
    const wxString scheme = wxString::FromUTF8(ev.name.c_str());
    const wxString url = wxString::FromUTF8(ev.url.c_str());

    wxSharedPtr<wxWebViewHandler> handler;
    for ( size_t n = 0; n < m_owner->m_handlerList.size(); n++ )
    {
        if ( m_owner->m_handlerList[n]->GetName().IsSameAs(scheme, false) )
        {
            handler = m_owner->m_handlerList[n];
            break;
        }
    }

    wxFSFile * const file = handler ? handler->GetFile(url) : NULL;
    wxInputStream * const stream = file ? file->GetStream() : NULL;
    if ( !stream || !stream->IsOk() )
    {
        delete file;
        m_backend->FailSchemeRequest(ev.id, "File not found");
        return;
    }

    wxMemoryOutputStream body;
    stream->Read(body);
    const size_t size = body.GetSize();
    wxCharBuffer data(size);
    if ( size )
        body.CopyTo(data.data(), size);

    // An empty type lets the engine tell from the address.
    const wxString mime = file->GetMimeType();
    std::vector<std::pair<std::string, std::string> > headers;
    headers.push_back(std::make_pair(std::string("Access-Control-Allow-Origin"),
                                     std::string("*")));
    m_backend->RespondToSchemeRequest(ev.id, 200,
                                      std::string(mime.utf8_str()),
                                      data.data(), size, headers);
    delete file;
}

// ----------------------------------------------------------------------------
// wxWebViewSummit
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxWebViewSummit, wxWebView);

long wxWebViewSummit::ms_scriptTimeout = 10000;
bool wxWebViewSummit::ms_releaseEngineOnBlank = true;
bool wxWebViewSummit::ms_defaultNonPersistent = false;

static bool wxIsBlankURL(const wxString& url)
{
    return url.empty() || url == "about:blank";
}

wxWebViewSummit::wxWebViewSummit()
    : m_impl(NULL),
      m_nonPersistent(ms_defaultNonPersistent),
      m_devTools(false),
      m_editable(false)
{
}

wxWebViewSummit::wxWebViewSummit(wxWindow* parent,
                                 wxWindowID id,
                                 const wxString& url,
                                 const wxPoint& pos,
                                 const wxSize& size,
                                 long style,
                                 const wxString& name)
    : m_impl(NULL),
      m_nonPersistent(ms_defaultNonPersistent),
      m_devTools(false),
      m_editable(false)
{
    Create(parent, id, url, pos, size, style, name);
}

wxWebViewSummit::~wxWebViewSummit()
{
    // Removes the engine's view from ours and ends its listener before the
    // BView of this window goes.
    delete m_impl;
    m_impl = NULL;
}

bool wxWebViewSummit::Create(wxWindow* parent,
                             wxWindowID id,
                             const wxString& url,
                             const wxPoint& pos,
                             const wxSize& size,
                             long style,
                             const wxString& name)
{
    if ( !wxControl::Create(parent, id, pos, size, style,
                            wxDefaultValidator, name) )
        return false;

    // No engine, no web view: the application then shows something else
    // (BambuStudio its placeholder and a message). Initialising the engine
    // starts none of its processes.
    if ( SummitWebBackend::InitializeEngine() != B_OK )
    {
        wxLogDebug("wxWebViewSummit: the summit_webkit engine is not available");
        return false;
    }

    m_impl = new wxWebViewSummitImpl(this);

    Bind(wxEVT_SIZE, &wxWebViewSummit::OnSize, this);

    // about:blank (the default) and an empty address do not need the engine
    // yet: see wxWebViewSummitImpl::HasEngine().
    LoadURL(url);

    return true;
}

void wxWebViewSummit::OnSize(wxSizeEvent& event)
{
    if ( m_impl && m_impl->HasEngine() )
    {
        const wxSize size = GetClientSize();
        m_impl->Backend().SetFrame(wxHaikuRect(0, 0, size.x, size.y));
    }
    event.Skip();
}

void wxWebViewSummit::SetFocus()
{
    // The keyboard goes to the engine's view, not to ours: wx's window
    // leaves the keys of views that are not its own alone.
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().SetFocus();
    else
        wxWebView::SetFocus();
}

void wxWebViewSummit::EnableContextMenu(bool enable)
{
    wxWebView::EnableContextMenu(enable);
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().EnableContextMenu(enable);
}

wxString wxWebViewSummit::GetCurrentTitle() const
{
    if ( !m_impl || !m_impl->HasEngine() )
        return wxString();
    return wxString::FromUTF8(m_impl->Backend().GetState().title.c_str());
}

wxString wxWebViewSummit::GetCurrentURL() const
{
    if ( !m_impl )
        return wxString();
    if ( !m_impl->HasEngine() )
        return m_impl->m_blankURL;
    return wxString::FromUTF8(m_impl->Backend().GetState().url.c_str());
}

bool wxWebViewSummit::IsBusy() const
{
    return m_impl && m_impl->HasEngine() && m_impl->Backend().GetState().loading;
}

void wxWebViewSummit::LoadURL(const wxString& url)
{
    if ( !m_impl )
        return;

    if ( wxIsBlankURL(url) )
    {
        // A blank view needs no web process: give the engine back, or do
        // not make it in the first place.
        if ( ms_releaseEngineOnBlank )
            m_impl->ReleaseEngine();
        if ( !m_impl->HasEngine() )
        {
            m_impl->SynthesizeBlank(url);
            return;
        }
    }

    if ( !m_impl->EnsureEngine() )
        return;

    m_impl->m_historyMove = 0;
    m_impl->Backend().LoadURL(std::string(url.utf8_str()));
}

void wxWebViewSummit::DoSetPage(const wxString& html, const wxString& baseUrl)
{
    if ( m_impl && m_impl->EnsureEngine() )
    {
        m_impl->m_historyMove = 0;
        m_impl->Backend().LoadHTML(std::string(html.utf8_str()),
                                   std::string(baseUrl.utf8_str()));
    }
}

void wxWebViewSummit::Print()
{
    // BWebKitView can draw pages for printing (DrawPagesForPrinting), but
    // the port has no print dialog to drive it with yet.
    wxLogDebug("wxWebViewSummit::Print() is not implemented");
}

void wxWebViewSummit::RegisterHandler(wxSharedPtr<wxWebViewHandler> handler)
{
    m_handlerList.push_back(handler);
    // Without an engine yet the schemes are registered when it is made.
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().RegisterScheme(std::string(handler->GetName().utf8_str()));
}

void wxWebViewSummit::Reload(wxWebViewReloadFlags flags)
{
    if ( !m_impl )
        return;

    if ( !m_impl->HasEngine() )
    {
        m_impl->SynthesizeBlank(m_impl->m_blankURL);
        return;
    }

    m_impl->m_historyMove = 2;
    m_impl->Backend().Reload(flags == wxWEBVIEW_RELOAD_NO_CACHE);
}

void wxWebViewSummit::Stop()
{
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().Stop();
}

bool wxWebViewSummit::SetUserAgent(const wxString& userAgent)
{
    // Before Create() it becomes the view's initial setting; afterwards it
    // applies at once, also to the document being loaded.
    m_customUserAgent = userAgent;
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().SetUserAgent(std::string(userAgent.utf8_str()));
    return true;
}

wxString wxWebViewSummit::GetUserAgent() const
{
    if ( !m_customUserAgent.empty() )
        return m_customUserAgent;
    return wxWebView::GetUserAgent();
}

// ----------------------------------------------------------------------------
// scripts
// ----------------------------------------------------------------------------

bool wxWebViewSummit::RunScript(const wxString& javascript, wxString* output) const
{
    if ( !m_impl )
    {
        if ( output )
            *output = "There is no web view";
        return false;
    }

    wxWebViewSummitImpl * const impl = m_impl;

    // A blank view without an engine: a script that reports nothing has
    // nothing to act on; one whose value is wanted gets the engine (and the
    // empty document it starts with).
    if ( !impl->HasEngine() && (!output || !impl->EnsureEngine()) )
    {
        if ( output )
            *output = "There is no web view";
        return !output;
    }

    std::string result;
    const bool ok = impl->Backend().RunScriptSync(
        std::string(javascript.utf8_str()),
        output ? &result : NULL,
        static_cast<bigtime_t>(ms_scriptTimeout) * 1000,
        [impl]()
        {
            // Keep the UI alive, without user input: a click handled inside
            // somebody's RunScript() would surprise the application. The
            // page's own messages are delivered, as with wx's other
            // backends.
            impl->Dispatch();
            wxEventLoopBase * const loop = wxEventLoopBase::GetActive();
            if ( loop && !loop->IsYielding() )
                loop->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_TIMER);
        });

    if ( output )
        *output = wxString::FromUTF8(result.c_str());

    if ( !ok && output )
        wxLogDebug("wxWebViewSummit: error running JavaScript: %s", *output);

    return ok;
}

bool wxWebViewSummit::RunScriptNoWait(const wxString& javascript)
{
    if ( !m_impl )
        return false;
    // nothing to run it in: see RunScript()
    if ( !m_impl->HasEngine() )
        return true;
    return m_impl->Backend().RunScript(std::string(javascript.utf8_str())) != 0;
}

void wxWebViewSummit::DeleteCookiesNamed(const wxString& name,
                                         const wxString& domainPart)
{
    if ( !m_impl || !m_impl->HasEngine() )
        return;

    if ( domainPart.empty() )
    {
        m_impl->Backend().DeleteCookies(std::string(),
                                        std::string(name.utf8_str()));
        return;
    }

    const wxUint64 request = m_impl->Backend().GetCookies(std::string());
    if ( request )
    {
        m_impl->m_cookieJobs[request] =
            std::make_pair(std::string(name.utf8_str()),
                           std::string(domainPart.utf8_str()));
    }
}

bool wxWebViewSummit::AddScriptMessageHandler(const wxString& name)
{
    // Offers window.webkit.messageHandlers.<name> and window.<name> to the
    // document being shown and to all later ones. Nothing is waited for.
    if ( !m_impl || name.empty() )
        return false;

    const std::string handler(name.utf8_str());
    for ( size_t n = 0; n < m_impl->m_handlers.size(); n++ )
    {
        if ( m_impl->m_handlers[n] == handler )
            return true;
    }

    if ( m_impl->HasEngine() && !m_impl->Backend().AddScriptMessageHandler(handler) )
        return false;

    m_impl->m_handlers.push_back(handler);
    return true;
}

bool wxWebViewSummit::RemoveScriptMessageHandler(const wxString& name)
{
    if ( !m_impl )
        return false;

    const std::string handler(name.utf8_str());
    for ( size_t n = 0; n < m_impl->m_handlers.size(); n++ )
    {
        if ( m_impl->m_handlers[n] == handler )
        {
            m_impl->m_handlers.erase(m_impl->m_handlers.begin() + n);
            if ( m_impl->HasEngine() )
                m_impl->Backend().RemoveScriptMessageHandler(handler);
            return true;
        }
    }
    return false;
}

bool wxWebViewSummit::AddUserScript(const wxString& javascript,
                                    wxWebViewUserScriptInjectionTime injectionTime)
{
    if ( !m_impl )
        return false;

    const std::string source(javascript.utf8_str());
    const bool atStart = injectionTime == wxWEBVIEW_INJECT_AT_DOCUMENT_START;
    m_impl->m_userScripts.push_back(std::make_pair(source, atStart));
    if ( m_impl->HasEngine() )
        m_impl->Backend().AddUserScript(source, atStart);
    return true;
}

void wxWebViewSummit::RemoveAllUserScripts()
{
    if ( !m_impl )
        return;

    m_impl->m_userScripts.clear();
    if ( m_impl->HasEngine() )
        m_impl->Backend().RemoveAllUserScripts();
}

// ----------------------------------------------------------------------------
// history
// ----------------------------------------------------------------------------

bool wxWebViewSummit::CanGoBack() const
{
    if ( !m_impl || !m_impl->HasEngine() || !m_impl->Backend().GetState().canGoBack )
        return false;
    return !m_impl->m_historyFloor || m_impl->m_backDepth > 0;
}

bool wxWebViewSummit::CanGoForward() const
{
    if ( !m_impl || !m_impl->HasEngine() || !m_impl->Backend().GetState().canGoForward )
        return false;
    return !m_impl->m_historyFloor || m_impl->m_forwardDepth > 0;
}

void wxWebViewSummit::GoBack()
{
    if ( CanGoBack() )
    {
        m_impl->m_historyMove = -1;
        m_impl->Backend().GoBack();
    }
}

void wxWebViewSummit::GoForward()
{
    if ( CanGoForward() )
    {
        m_impl->m_historyMove = 1;
        m_impl->Backend().GoForward();
    }
}

void wxWebViewSummit::ClearHistory()
{
    if ( m_impl )
    {
        m_impl->m_historyFloor = true;
        m_impl->m_backDepth = 0;
        m_impl->m_forwardDepth = 0;
    }
}

void wxWebViewSummit::EnableHistory(bool WXUNUSED(enable))
{
    // the engine always keeps its history
}

wxVector<wxSharedPtr<wxWebViewHistoryItem> > wxWebViewSummit::GetBackwardHistory()
{
    // BWebKitView::GetBackForwardList() answers asynchronously; not wired up
    return wxVector<wxSharedPtr<wxWebViewHistoryItem> >();
}

wxVector<wxSharedPtr<wxWebViewHistoryItem> > wxWebViewSummit::GetForwardHistory()
{
    return wxVector<wxSharedPtr<wxWebViewHistoryItem> >();
}

void wxWebViewSummit::LoadHistoryItem(wxSharedPtr<wxWebViewHistoryItem> item)
{
    if ( item )
        LoadURL(item->GetUrl());
}

// ----------------------------------------------------------------------------
// zoom, editing
// ----------------------------------------------------------------------------

bool wxWebViewSummit::CanSetZoomType(wxWebViewZoomType type) const
{
    return type == wxWEBVIEW_ZOOM_TYPE_LAYOUT;
}

wxWebViewZoomType wxWebViewSummit::GetZoomType() const
{
    return wxWEBVIEW_ZOOM_TYPE_LAYOUT;
}

void wxWebViewSummit::SetZoomType(wxWebViewZoomType WXUNUSED(zoomType))
{
}

float wxWebViewSummit::GetZoomFactor() const
{
    if ( !m_impl )
        return 1.0f;
    return static_cast<float>(m_impl->HasEngine() ? m_impl->Backend().GetState().pageZoom
                                                  : m_impl->m_zoom);
}

void wxWebViewSummit::SetZoomFactor(float zoom)
{
    if ( m_impl )
    {
        m_impl->m_zoom = zoom;
        if ( m_impl->HasEngine() )
            m_impl->Backend().SetZoomFactor(zoom);
    }
}

void wxWebViewSummit::SetEditable(bool enable)
{
    m_editable = enable;
    RunScriptNoWait(wxString::Format("document.body.contentEditable='%s';",
                                     enable ? "true" : "false"));
}

void wxWebViewSummit::SelectAll()
{
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().ExecuteEditCommand(SummitWebBackend::EditCommand::SelectAll);
}

void wxWebViewSummit::Cut()
{
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().ExecuteEditCommand(SummitWebBackend::EditCommand::Cut);
}

void wxWebViewSummit::Copy()
{
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().ExecuteEditCommand(SummitWebBackend::EditCommand::Copy);
}

void wxWebViewSummit::Paste()
{
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().ExecuteEditCommand(SummitWebBackend::EditCommand::Paste);
}

bool wxWebViewSummit::CanUndo() const
{
    return m_impl && m_impl->HasEngine() && QueryCommandEnabled("undo");
}

bool wxWebViewSummit::CanRedo() const
{
    return m_impl && m_impl->HasEngine() && QueryCommandEnabled("redo");
}

void wxWebViewSummit::Undo()
{
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().ExecuteEditCommand(SummitWebBackend::EditCommand::Undo);
}

void wxWebViewSummit::Redo()
{
    if ( m_impl && m_impl->HasEngine() )
        m_impl->Backend().ExecuteEditCommand(SummitWebBackend::EditCommand::Redo);
}

bool wxWebViewSummit::HasEngine() const
{
    return m_impl && m_impl->HasEngine();
}

void* wxWebViewSummit::GetNativeBackend() const
{
    // NULL while the view is blank and has no engine yet
    return m_impl ? m_impl->Backend().View() : NULL;
}

// ----------------------------------------------------------------------------
// wxWebViewFactorySummit
// ----------------------------------------------------------------------------

bool wxWebViewFactorySummit::IsAvailable()
{
    return SummitWebBackend::EngineAvailable();
}

wxVersionInfo wxWebViewFactorySummit::GetVersionInfo()
{
    // "627.1.4": WebKit's own version; the port's is in the description.
    int major = 0, minor = 0, micro = 0;
    const std::string version = SummitWebBackend::EngineVersionNumber();
    sscanf(version.c_str(), "%d.%d.%d", &major, &minor, &micro);
    return wxVersionInfo("summit_webkit", major, minor, micro,
                         wxString::FromUTF8(SummitWebBackend::EngineVersion().c_str()));
}

#endif // wxUSE_WEBVIEW && wxUSE_WEBVIEW_SUMMIT
