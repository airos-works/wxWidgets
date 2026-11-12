/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/window.h
// Purpose:     wxWindowHaiku: a wxWindow on a BView
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_WINDOW_H_
#define _WX_HAIKU_WINDOW_H_

#include "wx/region.h"

class BView;
class BWindow;
class BMessage;

class WXDLLIMPEXP_FWD_CORE wxNonOwnedWindow;
class WXDLLIMPEXP_FWD_CORE wxFont;
struct wxHaikuEvent;
class wxHaikuPaintSession;

// Every wxWindow owns one BView (a child of its parent's BView, or the root
// view of the BWindow for top level windows). The view covers the whole
// window including the non-client area which wxUniversal draws itself. All
// calls into the view are made from the wx (main) thread with the view's
// looper locked; the looper threads never call wx.
class WXDLLIMPEXP_CORE wxWindowHaiku : public wxWindowBase
{
public:
    wxWindowHaiku() { Init(); }

    wxWindowHaiku(wxWindow *parent,
                  wxWindowID id,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize,
                  long style = 0,
                  const wxString& name = wxASCII_STR(wxPanelNameStr))
    {
        Init();
        Create(parent, id, pos, size, style, name);
    }

    virtual ~wxWindowHaiku();

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxPanelNameStr));

    // implement base class (pure) virtual methods
    // -------------------------------------------

    virtual void SetLabel(const wxString& WXUNUSED(label)) wxOVERRIDE {}
    virtual wxString GetLabel() const wxOVERRIDE { return wxString(); }

    virtual void Raise() wxOVERRIDE;
    virtual void Lower() wxOVERRIDE;

    virtual bool Show(bool show = true) wxOVERRIDE;

    virtual void SetFocus() wxOVERRIDE;

    virtual bool Reparent(wxWindowBase *newParent) wxOVERRIDE;

    virtual void WarpPointer(int x, int y) wxOVERRIDE;

    // NB: as in the other wxUniversal ports the rectangle is in window (not
    //     client) coordinates here: wxUniv's wxWindow::Refresh() converts
    virtual void Refresh(bool eraseBackground = true,
                         const wxRect *rect = (const wxRect *) NULL) wxOVERRIDE;
    virtual void Update() wxOVERRIDE;

    virtual bool SetCursor(const wxCursor& cursor) wxOVERRIDE;
    virtual bool SetFont(const wxFont& font) wxOVERRIDE;

    virtual int GetCharHeight() const wxOVERRIDE;
    virtual int GetCharWidth() const wxOVERRIDE;

    virtual void ScrollWindow(int dx, int dy,
                              const wxRect* rect = NULL) wxOVERRIDE;

#if wxUSE_DRAG_AND_DROP
    virtual void SetDropTarget(wxDropTarget *dropTarget) wxOVERRIDE;
    virtual void DragAcceptFiles(bool accept) wxOVERRIDE;
#endif // wxUSE_DRAG_AND_DROP

    virtual WXWidget GetHandle() const wxOVERRIDE { return m_view; }

    // everything is painted through an off-screen buffer and shown at once
    virtual bool IsDoubleBuffered() const wxOVERRIDE { return true; }

    virtual wxSize GetDPI() const wxOVERRIDE;
    virtual double GetDPIScaleFactor() const wxOVERRIDE;

    virtual bool IsTransparentBackgroundSupported(wxString* reason = NULL) const wxOVERRIDE;

    // implementation from now on
    // --------------------------

    // the view of this window (NULL before Create() and after destruction)
    BView *GetHaikuView() const { return m_view; }

    // the identity of this window in the event records posted by the looper
    // threads (never reused, so a record for a deleted window is dropped)
    wxUint32 GetHaikuHandle() const { return m_handle; }

    // returns the window for the handle or NULL if it does not exist any more
    static wxWindowHaiku *HaikuFindByHandle(wxUint32 handle);

    // the top level window this window belongs to (may be NULL)
    wxNonOwnedWindow *GetTLW() const { return m_tlw; }

    // position of the top left corner of the view in screen coordinates,
    // computed from the wx side geometry without asking app_server
    wxPoint HaikuGetScreenOrigin() const;

    // position of the view in its parent's view and its size (for a top
    // level window this is the client area, not the decorated window)
    wxPoint HaikuGetViewPosition() const { return m_rect.GetPosition(); }
    wxSize HaikuGetViewSize() const { return m_rect.GetSize(); }

    // DragAcceptFiles() was called: files dropped on the window are reported
    // with wxDropFilesEvent if it has no drop target
    bool HaikuAcceptsFiles() const { return m_acceptsFiles; }

    // adds the rectangle (window coordinates) to the area to repaint in the
    // next paint phase
    void HaikuInvalidate(const wxRect& rect);

    // sends the erase, non-client paint and paint events for the pending
    // update region; returns true if anything was painted
    bool HaikuPaint();

    // the paint session (off-screen buffer) in progress for this window
    wxHaikuPaintSession *HaikuGetPaintSession() const { return m_paintSession; }

    // event record handlers, called from the event loop
    virtual void HaikuHandleEvent(const wxHaikuEvent& event);

    // true for windows whose view draws itself natively (OpenGL, web view):
    // no wx painting buffer is used for them
    virtual bool HaikuHasNativeContent() const { return false; }

    // the window is being attached to/detached from a BWindow: (un)register
    // native state that needs the window
    virtual void HaikuAttachedToWindow() { }

    // called by the top level window when it is shown or hidden
    void HaikuInvalidateAll();

    virtual void OnInternalIdle() wxOVERRIDE;

protected:
    // implement the base class pure virtuals
    virtual void DoGetTextExtent(const wxString& string,
                                 int *x, int *y,
                                 int *descent = NULL,
                                 int *externalLeading = NULL,
                                 const wxFont *theFont = NULL) const wxOVERRIDE;
    virtual void DoClientToScreen(int *x, int *y) const wxOVERRIDE;
    virtual void DoScreenToClient(int *x, int *y) const wxOVERRIDE;
    virtual void DoGetPosition(int *x, int *y) const wxOVERRIDE;
    virtual void DoGetSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoGetClientSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) wxOVERRIDE;
    virtual void DoSetClientSize(int width, int height) wxOVERRIDE;

    virtual void DoCaptureMouse() wxOVERRIDE;
    virtual void DoReleaseMouse() wxOVERRIDE;

    virtual void DoFreeze() wxOVERRIDE;
    virtual void DoThaw() wxOVERRIDE;

    virtual void DoEnable(bool enable) wxOVERRIDE;

#if wxUSE_TOOLTIPS
    virtual void DoSetToolTip(wxToolTip *tip) wxOVERRIDE;
#endif

    // move the window to the specified location and resize it; x and y are
    // relative to the parent's view (i.e. already adjusted for the parent's
    // client area origin)
    virtual void DoMoveWindow(int x, int y, int width, int height) wxOVERRIDE;

    // creates the BView of this window; overridden by windows which need a
    // different view class (wxGLCanvas)
    virtual BView *HaikuCreateView(const wxRect& rect);

    // (re)attaches the view to the parent's view
    void HaikuAttachView();

    // sets or clears the view's pointer event mask for CaptureMouse()
    void HaikuApplyCapture(bool capture);

    // applies m_cursor (or the inherited one) to the view
    void HaikuApplyCursor();

private:
    // common part of all ctors
    void Init();

protected:
    BView *m_view;
    wxUint32 m_handle;

    // the top level window this window belongs to
    wxNonOwnedWindow *m_tlw;

    // position (relative to the parent's view) and size of the view
    wxRect m_rect;

    // area that needs repainting, in window coordinates
    wxRegion m_invalid;
    bool m_inDirtyList;

    // set while HaikuPaint() runs
    wxHaikuPaintSession *m_paintSession;

    bool m_viewShown;
    bool m_acceptsFiles;

    friend class wxNonOwnedWindow;
    friend class wxHaikuPaintSession;

    wxDECLARE_DYNAMIC_CLASS(wxWindowHaiku);
    wxDECLARE_NO_COPY_CLASS(wxWindowHaiku);
    wxDECLARE_EVENT_TABLE();
};

#endif // _WX_HAIKU_WINDOW_H_
