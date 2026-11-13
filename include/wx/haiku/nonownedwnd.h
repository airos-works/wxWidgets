/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/nonownedwnd.h
// Purpose:     wxNonOwnedWindow: a window with its own BWindow
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_NONOWNEDWND_H_
#define _WX_HAIKU_NONOWNEDWND_H_

#include "wx/window.h"

// Common base of wxTopLevelWindow and wxPopupWindow: owns a BWindow (and so a
// looper thread) whose root view is the view of the wxWindow.
//
// The BWindow is created when the window is shown for the first time (or
// when something needs it, see HaikuRealize()): applications create many
// windows early which are shown late or never (popups of combo boxes,
// dialogs kept for later), and each BWindow costs a thread here and one in
// app_server. Until then the views of the window and its children exist
// without a window, the geometry is kept on the wx side. Popups hidden for
// a while give their BWindow back (HaikuUnrealize()).
//
// The position and size of the wx window are those of the window including
// its decorations (tab and border); the client area is the BWindow's frame.
class WXDLLIMPEXP_CORE wxNonOwnedWindow : public wxNonOwnedWindowBase
{
public:
    wxNonOwnedWindow() { Init(); }
    wxNonOwnedWindow(wxWindow *parent,
                     wxWindowID id,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = 0,
                     const wxString& name = wxASCII_STR(wxPanelNameStr))
    {
        Init();
        Create(parent, id, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxPanelNameStr));

    virtual ~wxNonOwnedWindow();

    virtual bool Show(bool show = true) wxOVERRIDE;
    virtual void Raise() wxOVERRIDE;
    virtual void Lower() wxOVERRIDE;

    virtual bool CanSetTransparent() wxOVERRIDE { return false; }

    // implementation from now on
    // --------------------------

    // the BWindow, NULL while there is none (see HaikuRealize())
    BWindow *GetHaikuWindow() const { return m_window; }

    // creates the BWindow if there is none yet; false if that failed
    bool HaikuRealize();

    // gives the BWindow of a hidden window back, keeping the views; false
    // if the window has none or cannot do without it now
    bool HaikuUnrealize();

    // true if the BWindow of the window may be given back while it is hidden
    virtual bool HaikuCanUnrealize() const { return false; }

    // gives the BWindows of the windows hidden for a while back (called
    // from idle time)
    static void HaikuReleaseHiddenWindows();

    // the window which had the focus in this top level window the last time
    // it was active (NULL if none): it gets the focus back on activation
    wxWindow *HaikuGetLastFocus() const { return m_lastFocus; }
    void HaikuSetLastFocus(wxWindow *win) { m_lastFocus = win; }

    // sizes of the decorations: left, top, right, bottom
    void HaikuGetDecorationInsets(int *left, int *top,
                                  int *right, int *bottom) const;

    // screen position of the content area (the BWindow's frame)
    wxPoint HaikuGetContentOrigin() const;

    virtual void HaikuHandleEvent(const wxHaikuEvent& event) wxOVERRIDE;

    // true if this window never becomes the active window (popups)
    virtual bool HaikuAvoidsFocus() const { return false; }

    // true while the window is shown application modally
    bool HaikuIsModal() const { return m_isModal; }

    // called after the size event of the window was processed, whether a
    // handler of the application let it through to the base classes or not
    virtual void HaikuAfterSizeEvent() { }
    void HaikuSetModal(bool modal);

protected:
    // common part of all ctors
    void Init();

    virtual void DoGetPosition(int *x, int *y) const wxOVERRIDE;
    virtual void DoGetSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) wxOVERRIDE;
    virtual void DoGetClientSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoSetClientSize(int width, int height) wxOVERRIDE;
    virtual void DoMoveWindow(int x, int y, int width, int height) wxOVERRIDE;
    virtual void DoSetSizeHints(int minW, int minH,
                                int maxW, int maxH,
                                int incW, int incH) wxOVERRIDE;
    virtual void DoEnable(bool enable) wxOVERRIDE;

    virtual bool DoSetRegionShape(const wxRegion& WXUNUSED(region)) wxOVERRIDE
        { return false; }

    // the BWindow look, feel and flags for the current wx styles
    virtual void HaikuGetWindowType(int *look, int *feel,
                                    wxUint32 *flags) const;

    // applies HaikuGetWindowType() to the existing window
    void HaikuUpdateWindowType();

    // updates the cached decoration sizes from the window's decorator
    void HaikuUpdateDecorationInsets();

    // the frame of the BWindow changed: update the wx side geometry and
    // send the size and move events
    void HaikuFrameChanged(const wxRect& content);

    // moves and resizes the BWindow to the content rectangle (screen
    // coordinates); must not be called with the window locked
    void HaikuSetContentRect(const wxRect& content);

    // keyboard input always arrives at the top level window
    void HaikuHandleKey(const wxHaikuEvent& event);
    void HaikuHandleModifiers(const wxHaikuEvent& event);

    // activation changed (WindowActivated() hook)
    virtual void HaikuActivated(bool active);

    // called when the BWindow was created, before its looper runs: this
    // thread owns its lock, the state kept while there was no window (title,
    // size limits...) is applied here
    virtual void HaikuWindowCreated(BWindow *window);

    // applies the size limits of the wx window to the BWindow
    void HaikuApplySizeLimits();

protected:
    BWindow *m_window;

    // screen rectangle of the content area
    wxRect m_content;

    // sizes of the decorations
    int m_decoLeft, m_decoTop, m_decoRight, m_decoBottom;
    bool m_decoValid;

    wxWindow *m_lastFocus;

    // serial number of the last frame change this thread asked the looper
    // for: frame reports with a smaller one are ignored
    wxInt32 m_frameSerial;

    bool m_isModal;
    bool m_isActive;
    bool m_windowShown;
    bool m_sizeEventSent;
    bool m_shownOnce;

    // created without a position and not moved since: the window is put
    // in a sensible place when it is shown for the first time
    bool m_autoPosition;

    // when the window was hidden (milliseconds, wxGetLocalTimeMillis()),
    // for HaikuReleaseHiddenWindows()
    wxLongLong m_hiddenSince;

    wxDECLARE_ABSTRACT_CLASS(wxNonOwnedWindow);
};

#endif // _WX_HAIKU_NONOWNEDWND_H_
