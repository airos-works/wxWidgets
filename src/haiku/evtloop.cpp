/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/evtloop.cpp
// Purpose:     the queue of event records posted by the looper threads and
//              the wx event loop draining it
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/log.h"
    #include "wx/window.h"
#endif

#include "wx/evtloop.h"
#include "wx/apptrait.h"
#include "wx/thread.h"
#include "wx/private/fdiodispatcher.h"
#include "wx/unix/private/timer.h"
#include "wx/hashmap.h"

#include "wx/haiku/private.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

// ============================================================================
// tracing
// ============================================================================

bool wxHaikuTraceEnabled = getenv("WXHAIKU_TRACE") != NULL;

void wxHaikuTracePrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[wx %7.3f] ", (double)(system_time() % 1000000000) / 1e6);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(args);
}

// ============================================================================
// the queue
// ============================================================================

namespace
{

// The queue is filled by the BApplication and BWindow looper threads and
// emptied by the wx thread. A pipe makes the wxFDIODispatcher the event loop
// sleeps in wake up: one byte is written when the queue stops being idle.
struct EventQueue
{
    EventQueue()
        : head(NULL), tail(NULL), wakePending(false)
    {
        pthread_mutex_init(&mutex, NULL);
        fds[0] = fds[1] = -1;
    }

    bool Init()
    {
        if ( fds[0] != -1 )
            return true;

        if ( pipe(fds) != 0 )
            return false;

        for ( int i = 0; i < 2; i++ )
        {
            int flags = fcntl(fds[i], F_GETFL, 0);
            fcntl(fds[i], F_SETFL, flags | O_NONBLOCK);
            fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        }

        return true;
    }

    pthread_mutex_t mutex;
    wxHaikuEvent *head, *tail;
    bool wakePending;
    int fds[2];
};

EventQueue gs_queue;

// input events put aside while yielding for non-input events only
wxHaikuEvent *gs_deferredHead = NULL,
             *gs_deferredTail = NULL;

wxUint32 gs_lastHandle = 0;

WX_DECLARE_HASH_MAP(wxUint32, wxHaikuCallbackTarget *,
                    wxIntegerHash, wxIntegerEqual, CallbackMap);
CallbackMap gs_callbacks;

// returns the first record of the queue or NULL; coalesces records which
// supersede each other
wxHaikuEvent *PopEvent()
{
    pthread_mutex_lock(&gs_queue.mutex);

    wxHaikuEvent *event = gs_queue.head;
    while ( event )
    {
        gs_queue.head = event->next;
        if ( !gs_queue.head )
            gs_queue.tail = NULL;

        // a mouse move or a frame change directly followed by another one
        // for the same window is of no interest any more
        wxHaikuEvent * const next = gs_queue.head;
        if ( next && next->type == event->type && next->handle == event->handle
                && ((event->type == wxHAIKU_EVT_FRAME && !event->flag)
                    || (event->type == wxHAIKU_EVT_MOUSE_MOVED
                        && next->transit == event->transit
                        && next->buttons == event->buttons)) )
        {
            delete event;
            event = next;
            continue;
        }

        break;
    }

    pthread_mutex_unlock(&gs_queue.mutex);

    if ( event )
        event->next = NULL;

    return event;
}

bool IsInputEvent(wxHaikuEventType type)
{
    switch ( type )
    {
        case wxHAIKU_EVT_MOUSE_DOWN:
        case wxHAIKU_EVT_MOUSE_UP:
        case wxHAIKU_EVT_MOUSE_MOVED:
        case wxHAIKU_EVT_MOUSE_WHEEL:
        case wxHAIKU_EVT_KEY_DOWN:
        case wxHAIKU_EVT_KEY_UP:
        case wxHAIKU_EVT_IME:
        case wxHAIKU_EVT_CLOSE:
        case wxHAIKU_EVT_DROP:
        case wxHAIKU_EVT_DRAG_MOVE:
        case wxHAIKU_EVT_APP_QUIT:
            return true;

        default:
            return false;
    }
}

void DispatchAppEvent(const wxHaikuEvent& event)
{
    wxApp * const app = wxTheApp;
    if ( !app )
        return;

    switch ( event.type )
    {
        case wxHAIKU_EVT_APP_REFS:
        case wxHAIKU_EVT_APP_ARGV:
            {
                wxArrayString strings;
                for ( size_t n = 0; n < event.strings.size(); n++ )
                    strings.push_back(wxString::FromUTF8(event.strings[n].c_str()));

                if ( event.type == wxHAIKU_EVT_APP_REFS )
                    app->MacOpenFiles(strings);
                else
                    app->HaikuArgvReceived(strings);
            }
            break;

        case wxHAIKU_EVT_APP_QUIT:
            app->HaikuQuitRequested();
            break;

        case wxHAIKU_EVT_APP_ABOUT:
            app->HaikuAboutRequested();
            break;

        case wxHAIKU_EVT_APP_ACTIVATED:
            {
                wxActivateEvent activate(wxEVT_ACTIVATE_APP, event.flag);
                activate.SetEventObject(app);
                app->ProcessEvent(activate);

                if ( event.flag )
                    app->MacReopenApp();
            }
            break;

        case wxHAIKU_EVT_COLORS_UPDATED:
        case wxHAIKU_EVT_FONTS_UPDATED:
            {
                wxHaikuResetSystemSettings();

                // tell all top level windows, they pass it on to their
                // children and repaint
                for ( wxWindowList::compatibility_iterator
                        node = wxTopLevelWindows.GetFirst();
                      node;
                      node = node->GetNext() )
                {
                    wxWindow * const win = node->GetData();
                    wxSysColourChangedEvent colourEvent;
                    colourEvent.SetEventObject(win);
                    win->HandleWindowEvent(colourEvent);
                    win->Refresh();
                }
            }
            break;

        default:
            break;
    }
}

void DispatchEvent(const wxHaikuEvent& event)
{
    switch ( event.type )
    {
        case wxHAIKU_EVT_APP_REFS:
        case wxHAIKU_EVT_APP_ARGV:
        case wxHAIKU_EVT_APP_QUIT:
        case wxHAIKU_EVT_APP_ABOUT:
        case wxHAIKU_EVT_APP_ACTIVATED:
        case wxHAIKU_EVT_COLORS_UPDATED:
        case wxHAIKU_EVT_FONTS_UPDATED:
            DispatchAppEvent(event);
            break;

        case wxHAIKU_EVT_CALLBACK:
            {
                CallbackMap::iterator it = gs_callbacks.find(event.handle);
                if ( it != gs_callbacks.end() )
                    it->second->HaikuCallback(event);
            }
            break;

        default:
            {
                wxWindowHaiku * const
                    win = wxWindowHaiku::HaikuFindByHandle(event.handle);
                if ( win )
                    win->HaikuHandleEvent(event);
                //else: the window was destroyed after the record was posted
            }
            break;
    }
}

} // anonymous namespace

void wxHaikuPostEvent(wxHaikuEvent *event)
{
    event->next = NULL;

    pthread_mutex_lock(&gs_queue.mutex);

    if ( gs_queue.tail )
        gs_queue.tail->next = event;
    else
        gs_queue.head = event;
    gs_queue.tail = event;

    const bool wake = !gs_queue.wakePending;
    gs_queue.wakePending = true;

    pthread_mutex_unlock(&gs_queue.mutex);

    if ( wake && gs_queue.fds[1] != -1 )
    {
        char c = 0;
        while ( write(gs_queue.fds[1], &c, 1) == -1 && errno == EINTR )
            ;
    }
}

int wxHaikuGetQueueFD()
{
    gs_queue.Init();
    return gs_queue.fds[0];
}

bool wxHaikuHasQueuedEvents()
{
    pthread_mutex_lock(&gs_queue.mutex);
    const bool has = gs_queue.head != NULL;
    pthread_mutex_unlock(&gs_queue.mutex);
    return has;
}

bool wxHaikuProcessQueuedEvents()
{
    // Empty the pipe and allow the next post to wake us again before looking
    // at the queue: a record queued after this point writes to the pipe.
    // A wake up byte counts as something processed even if the records it
    // announced were already taken by the previous call: the caller must
    // not go to sleep in the dispatcher without having had its idle time
    // (painting!) first, which it skips while the pipe is readable.
    bool processed = false;

    if ( gs_queue.fds[0] != -1 )
    {
        char buf[64];
        while ( read(gs_queue.fds[0], buf, sizeof(buf)) > 0 )
            processed = true;
    }

    pthread_mutex_lock(&gs_queue.mutex);
    gs_queue.wakePending = false;
    pthread_mutex_unlock(&gs_queue.mutex);

    // while yielding for some event categories only, input is put aside
    wxEventLoopBase * const loop = wxEventLoopBase::GetActive();
    const bool inputAllowed = !loop
        || loop->IsEventAllowedInsideYield(wxEVT_CATEGORY_USER_INPUT);

    if ( inputAllowed && gs_deferredHead )
    {
        // put the deferred records back in front of the queue
        pthread_mutex_lock(&gs_queue.mutex);
        gs_deferredTail->next = gs_queue.head;
        if ( !gs_queue.tail )
            gs_queue.tail = gs_deferredTail;
        gs_queue.head = gs_deferredHead;
        pthread_mutex_unlock(&gs_queue.mutex);

        gs_deferredHead = gs_deferredTail = NULL;
    }

    // Records are taken one by one: a handler may run a nested event loop
    // (modal dialog, popup menu) which must see the following ones.
    while ( wxHaikuEvent *event = PopEvent() )
    {
        if ( !inputAllowed && IsInputEvent(event->type) )
        {
            if ( gs_deferredTail )
                gs_deferredTail->next = event;
            else
                gs_deferredHead = event;
            gs_deferredTail = event;
            continue;
        }

        if ( event->type != wxHAIKU_EVT_MOUSE_MOVED )
            wxHAIKU_TRACE("event %d handle=%u", (int)event->type,
                          (unsigned)event->handle);

        DispatchEvent(*event);
        delete event;

        // Events the handlers posted to themselves or to others come before
        // the next record: a popup dismissed by a click passes the click on
        // this way, and the release of the button must come after it.
        if ( wxTheApp && wxTheApp->HasPendingEvents() )
            wxTheApp->ProcessPendingEvents();

        processed = true;
    }

    return processed;
}

wxUint32 wxHaikuNewHandle()
{
    // wx thread only; 0 is never a valid handle
    if ( ++gs_lastHandle == 0 )
        ++gs_lastHandle;
    return gs_lastHandle;
}

wxHaikuCallbackTarget::wxHaikuCallbackTarget()
    : m_callbackHandle(wxHaikuNewHandle())
{
    gs_callbacks[m_callbackHandle] = this;
}

wxHaikuCallbackTarget::~wxHaikuCallbackTarget()
{
    gs_callbacks.erase(m_callbackHandle);
}

// ============================================================================
// wxGUIEventLoop
// ============================================================================

namespace
{

// makes the dispatcher return from its wait when records are queued; the
// records themselves are processed by wxGUIEventLoop::Dispatch()
class QueueFDHandler : public wxFDIOHandler
{
public:
    QueueFDHandler() : m_registered(false) { }

    void Register()
    {
        if ( m_registered )
            return;

        wxFDIODispatcher * const dispatcher = wxFDIODispatcher::Get();
        wxCHECK_RET( dispatcher, "wxHaiku requires wxFDIODispatcher" );

        dispatcher->RegisterFD(wxHaikuGetQueueFD(), this, wxFDIO_INPUT);
        m_registered = true;
    }

    virtual void OnReadWaiting() wxOVERRIDE
    {
        wxHaikuProcessQueuedEvents();
    }

    virtual void OnWriteWaiting() wxOVERRIDE { }
    virtual void OnExceptionWaiting() wxOVERRIDE { }

private:
    bool m_registered;
};

QueueFDHandler gs_queueHandler;

} // anonymous namespace

wxGUIEventLoop::wxGUIEventLoop()
{
    gs_queueHandler.Register();
}

bool wxGUIEventLoop::Pending() const
{
    if ( wxHaikuHasQueuedEvents() )
        return true;

    return wxConsoleEventLoop::Pending();
}

bool wxGUIEventLoop::Dispatch()
{
    // records queued before the wake up byte was written or left over by a
    // nested loop
    if ( wxHaikuProcessQueuedEvents() )
    {
        // still let the timers run: mouse moves can keep the queue busy
#if wxUSE_TIMER
        wxTimerScheduler::Get().NotifyExpired();
#endif
        return true;
    }

    return wxConsoleEventLoop::Dispatch();
}

int wxGUIEventLoop::DispatchTimeout(unsigned long timeout)
{
    if ( wxHaikuProcessQueuedEvents() )
    {
#if wxUSE_TIMER
        wxTimerScheduler::Get().NotifyExpired();
#endif
        return 1;
    }

    return wxConsoleEventLoop::DispatchTimeout(timeout);
}

void wxGUIEventLoop::DoYieldFor(long eventsToProcess)
{
    const bool ui = (eventsToProcess &
                        (wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT)) != 0;

    if ( ui )
        wxHaikuProcessQueuedEvents();

    // sockets and timers
    for ( int n = 0; n < 100 && wxConsoleEventLoop::Pending(); n++ )
        wxConsoleEventLoop::DispatchTimeout(0);

    // show what the handlers changed: code which yields in a long
    // computation (progress dialogs) expects the windows to be redrawn
    if ( ui )
        wxHaikuPaintPendingWindows();

    // the pending wx events
    wxConsoleEventLoop::DoYieldFor(eventsToProcess);

    if ( ui )
        wxHaikuPaintPendingWindows();
}

// ----------------------------------------------------------------------------
// wxGUIAppTraits
// ----------------------------------------------------------------------------

wxEventLoopBase *wxGUIAppTraits::CreateEventLoop()
{
    return new wxEventLoop;
}

#if wxUSE_TIMER
wxTimerImpl *wxGUIAppTraits::CreateTimerImpl(wxTimer *timer)
{
    return new wxUnixTimerImpl(timer);
}
#endif // wxUSE_TIMER

#if wxUSE_EVENTLOOP_SOURCE
wxEventLoopSourcesManagerBase *wxGUIAppTraits::GetEventLoopSourcesManager()
{
    return wxAppTraits::GetEventLoopSourcesManager();
}
#endif // wxUSE_EVENTLOOP_SOURCE
