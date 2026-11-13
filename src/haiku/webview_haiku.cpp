/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/webview_haiku.cpp
// Purpose:     wxWebView on Haiku: the place where a backend plugs in
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// PLACEHOLDER - THERE IS NO WEB VIEW BACKEND IN THIS FILE
// ============================================================================
//
// With wxUSE_WEBVIEW the Haiku port builds the "webview" library from
// wxWebView's common code (src/common/webview.cpp, webviewarchivehandler.cpp,
// webviewfshandler.cpp) and this file. Without a backend
//
//      wxWebView::New()                  returns NULL
//      wxWebView::IsBackendAvailable()   returns false
//
// for every backend name, and nothing else of the class can be reached.
//
// Where a backend plugs in
// ------------------------
//
// 1. The factory. wxWebView::InitFactoryMap() (src/common/webview.cpp) calls
//    wxHaikuCreateDefaultWebViewFactory() below and registers what it returns
//    under the name wxWebViewBackendWebKit ("wxWebViewWebKit"), which is what
//    wxWebViewBackendDefault is on every platform but MSW, Haiku included:
//    applications which ask for the default backend or for WebKit by name get
//    it. A backend makes this function return its factory:
//
//          return new wxWebViewFactoryXXX;
//
//    (the function is the reference which pulls the backend's object files
//    out of the static library; a factory object registering itself from a
//    static initializer would not be linked). An application can also
//    register a factory itself, before its first wxWebView::New():
//
//          wxWebView::RegisterFactory(wxWebViewBackendDefault,
//              wxSharedPtr<wxWebViewFactory>(new MyFactory));
//
//    wxWebViewFactory::IsAvailable() should tell whether the engine can be
//    used on this machine and GetVersionInfo() describe it.
//
// 2. The sources. Add them to WEBVIEW_HAIKU_SRC/WEBVIEW_HAIKU_HDR in
//    build/cmake/files.cmake and the engine's include directories and
//    libraries in build/cmake/lib/webview/CMakeLists.txt (WXHAIKU branch);
//    what is linked PUBLIC there ends up in "wx-config --libs webview".
//
// 3. wxWebViewHistoryItem is wx/haiku/webviewhistoryitem.h: URL, title and
//    an opaque 64 bit value for the backend.
//
// 4. The native view. A wxWebView is a wxControl, i.e. a wxWindow of this
//    port, which owns a BView (see wx/haiku/window.h):
//
//    - Override the virtual wxWindowHaiku::HaikuCreateView(const wxRect&) to
//      return the engine's BView instead of the port's wxHaikuView, or keep
//      the default view and add the engine's view as its child
//      (GetHaikuView()->AddChild() with the looper locked: wxHaikuViewLock),
//      resizing it with the wx window (B_FOLLOW_ALL does it).
//    - Override HaikuHasNativeContent() to return true: wx then neither
//      paints the window nor keeps an off-screen buffer for it.
//    - HaikuAttachedToWindow() is called when the view gets a BWindow or
//      loses it (Reparent(), creation of the top level window).
//    - Keyboard: wxHaikuWindow::DispatchMessage() (src/haiku/looper.cpp)
//      sends all key messages to wx except when the BWindow's focus view has
//      a Name() which does not start with "wx": name the engine's view
//      accordingly and it gets its keys natively. Mouse events arrive at the
//      native view directly from app_server.
//    - Threads: wx runs on the main thread only. The engine's notifications
//      arrive on looper threads, which must not call wx: derive from
//      wxHaikuCallbackTarget (wx/haiku/private.h), post wxHaikuEvent records
//      of type wxHAIKU_EVT_CALLBACK carrying GetHaikuCallbackHandle() with
//      wxHaikuPostEvent() and send the wx events from HaikuCallback(), which
//      runs on the main thread. The main thread must never wait for a
//      looper; a synchronous RunScript() runs a nested wxEventLoop until its
//      answer arrives.
//
// ============================================================================

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_WEBVIEW

#include "wx/haiku/private/webview.h"

wxWebViewFactory *wxHaikuCreateDefaultWebViewFactory()
{
    // PLACEHOLDER: no backend
    return NULL;
}

#endif // wxUSE_WEBVIEW
