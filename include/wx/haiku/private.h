/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/private.h
// Purpose:     private declarations of the native Haiku port: includes the
//              Be API headers, must not be included from public wx headers
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_PRIVATE_H_
#define _WX_HAIKU_PRIVATE_H_

#include <Application.h>
#include <Bitmap.h>
#include <Font.h>
#include <GraphicsDefs.h>
#include <Looper.h>
#include <Message.h>
#include <Point.h>
#include <Rect.h>
#include <Region.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <string>
#include <vector>

#include "wx/gdicmn.h"
#include "wx/colour.h"
#include "wx/region.h"
#include "wx/string.h"

class WXDLLIMPEXP_FWD_CORE wxWindow;
class WXDLLIMPEXP_FWD_CORE wxWindowHaiku;
class WXDLLIMPEXP_FWD_CORE wxNonOwnedWindow;
class WXDLLIMPEXP_FWD_CORE wxRegion;
class WXDLLIMPEXP_FWD_CORE wxKeyEvent;

// ----------------------------------------------------------------------------
// conversions
// ----------------------------------------------------------------------------

// A BRect covers the pixels from left to right inclusive.
inline BRect wxHaikuRect(const wxRect& r)
{
    return BRect(r.x, r.y, r.x + r.width - 1, r.y + r.height - 1);
}

inline BRect wxHaikuRect(int x, int y, int w, int h)
{
    return BRect(x, y, x + w - 1, y + h - 1);
}

inline wxRect wxFromHaikuRect(const BRect& r)
{
    const int x = (int)floorf(r.left),
              y = (int)floorf(r.top);
    return wxRect(x, y, (int)ceilf(r.right) - x + 1, (int)ceilf(r.bottom) - y + 1);
}

inline rgb_color wxHaikuColour(const wxColour& c)
{
    rgb_color rgb;
    rgb.red = c.Red();
    rgb.green = c.Green();
    rgb.blue = c.Blue();
    rgb.alpha = c.Alpha();
    return rgb;
}

inline wxColour wxFromHaikuColour(const rgb_color& c)
{
    return wxColour(c.red, c.green, c.blue, c.alpha);
}

// UTF-8 is the native encoding of all text in the Be API
#define wxHaikuStr(s) ((const char *)(s).utf8_str())

inline wxString wxFromHaikuStr(const char *s)
{
    return wxString::FromUTF8(s ? s : "");
}

// ----------------------------------------------------------------------------
// locking
// ----------------------------------------------------------------------------

// Locks the looper of a view for the lifetime of the object. A view which is
// not attached to a window has no looper: it can be manipulated (moved,
// resized, shown...) without a lock but cannot be drawn on.
class wxHaikuViewLock
{
public:
    explicit wxHaikuViewLock(BView *view)
        : m_view(view), m_looper(NULL)
    {
        // Remember the looper: the view may be removed from its window while
        // the lock is held (RemoveSelf()) and has no looper to unlock then.
        if ( m_view && m_view->Window() && m_view->LockLooper() )
            m_looper = m_view->Looper();
    }

    ~wxHaikuViewLock()
    {
        if ( m_looper )
            m_looper->Unlock();
    }

    // the view is attached to a window and its looper is locked
    bool IsLocked() const { return m_looper != NULL; }

    // the view may be manipulated: it is locked or not attached at all
    bool CanTouch() const
    {
        return m_view && (m_looper || !m_view->Window());
    }

private:
    BView *m_view;
    BLooper *m_looper;

    wxDECLARE_NO_COPY_CLASS(wxHaikuViewLock);
};

class wxHaikuWindowLock
{
public:
    explicit wxHaikuWindowLock(BWindow *window)
        : m_window(window), m_locked(window && window->Lock())
    {
    }

    ~wxHaikuWindowLock()
    {
        if ( m_locked )
            m_window->Unlock();
    }

    bool IsLocked() const { return m_locked; }

private:
    BWindow *m_window;
    bool m_locked;

    wxDECLARE_NO_COPY_CLASS(wxHaikuWindowLock);
};

// ----------------------------------------------------------------------------
// event records: looper threads -> wx thread
// ----------------------------------------------------------------------------

enum wxHaikuEventType
{
    wxHAIKU_EVT_NONE,

    // input, targeted at the window with the given handle
    wxHAIKU_EVT_MOUSE_DOWN,
    wxHAIKU_EVT_MOUSE_UP,
    wxHAIKU_EVT_MOUSE_MOVED,
    wxHAIKU_EVT_MOUSE_WHEEL,
    wxHAIKU_EVT_KEY_DOWN,
    wxHAIKU_EVT_KEY_UP,
    wxHAIKU_EVT_MODIFIERS,
    wxHAIKU_EVT_IME,

    // a part of the view needs to be painted (rect, view coordinates)
    wxHAIKU_EVT_DRAW,

    // top level window events
    wxHAIKU_EVT_FRAME,          // rect: new frame of the BWindow on screen
    wxHAIKU_EVT_ACTIVATE,       // flag: active
    wxHAIKU_EVT_CLOSE,          // QuitRequested()
    wxHAIKU_EVT_MINIMIZE,       // flag: minimized
    wxHAIKU_EVT_ZOOM,           // zoom button
    wxHAIKU_EVT_SCREEN_CHANGED,
    wxHAIKU_EVT_WORKSPACE,      // flag: the window's workspace is current

    // drag and drop: strings are the dropped paths or the text, value the
    // kind of data (wxHaikuDropKind)
    wxHAIKU_EVT_DROP,
    wxHAIKU_EVT_DRAG_MOVE,      // a drag is over the view (transit as in mouse)

    // application events
    wxHAIKU_EVT_APP_REFS,       // strings: paths
    wxHAIKU_EVT_APP_ARGV,       // strings: arguments
    wxHAIKU_EVT_APP_QUIT,
    wxHAIKU_EVT_APP_ABOUT,
    wxHAIKU_EVT_APP_ACTIVATED,  // flag: active
    wxHAIKU_EVT_COLORS_UPDATED,
    wxHAIKU_EVT_FONTS_UPDATED,

    // a native object (file panel, menu...) reports back to the object
    // registered under "handle" with wxHaikuRegisterCallback(); value,
    // strings and message are free for the user
    wxHAIKU_EVT_CALLBACK
};

enum wxHaikuDropKind
{
    wxHAIKU_DROP_FILES,
    wxHAIKU_DROP_TEXT
};

struct wxHaikuEvent
{
    wxHaikuEvent(wxHaikuEventType type_ = wxHAIKU_EVT_NONE, wxUint32 handle_ = 0)
        : type(type_), handle(handle_), when(0),
          buttons(0), modifiers(0), clicks(0), transit(0),
          key(0), rawChar(0), numBytes(0), repeat(false),
          deltaX(0), deltaY(0), flag(false), value(0),
          message(NULL), next(NULL)
    {
        bytes[0] = '\0';
    }

    ~wxHaikuEvent() { delete message; }

    wxHaikuEventType type;
    wxUint32 handle;            // identity of the target wxWindow (or callback)
    bigtime_t when;

    // mouse
    BPoint where;               // in the coordinates of the view
    BPoint screenWhere;
    int32 buttons;
    int32 modifiers;
    int32 clicks;
    int32 transit;

    // keyboard
    int32 key;                  // the key code (scan code like)
    int32 rawChar;
    char bytes[16];             // the UTF-8 character generated by the key
    uint8 numBytes;
    bool repeat;

    BRect rect;
    float deltaX, deltaY;       // wheel
    bool flag;
    int32 value;
    std::vector<std::string> strings;
    BMessage *message;          // owned

    wxHaikuEvent *next;

    wxDECLARE_NO_COPY_CLASS(wxHaikuEvent);
};

// Called from any thread: appends the record (takes ownership) to the queue
// and wakes up the wx event loop.
void wxHaikuPostEvent(wxHaikuEvent *event);

// wx thread only --

// file descriptor which becomes readable when records are queued
int wxHaikuGetQueueFD();

// true if there are queued records
bool wxHaikuHasQueuedEvents();

// processes all queued records (and those queued meanwhile); returns true
// if anything was processed
bool wxHaikuProcessQueuedEvents();

// paints all windows with pending update regions; returns true if any did
bool wxHaikuPaintPendingWindows();

// handles of objects (not windows) receiving wxHAIKU_EVT_CALLBACK
class wxHaikuCallbackTarget
{
public:
    wxHaikuCallbackTarget();
    virtual ~wxHaikuCallbackTarget();

    wxUint32 GetHaikuCallbackHandle() const { return m_callbackHandle; }

    virtual void HaikuCallback(const wxHaikuEvent& event) = 0;

private:
    wxUint32 m_callbackHandle;
};

// allocates a new handle (never 0, never reused)
wxUint32 wxHaikuNewHandle();

// ----------------------------------------------------------------------------
// views and windows
// ----------------------------------------------------------------------------

// Base of all views wx draws on: remembers which DC set the state of the view
// last so that DCs sharing a view know when they have to set theirs again.
class wxHaikuDrawView : public BView
{
public:
    wxHaikuDrawView(BRect frame, const char *name, uint32 resizingMode,
                    uint32 flags)
        : BView(frame, name, resizingMode, flags),
          m_stateOwner(NULL)
    {
    }

    const void *m_stateOwner;
};

// Helpers for the hooks of views which stand for a wxWindow: they only build
// event records, they never touch wx objects.
void wxHaikuPostMouseEvent(BView *view, wxUint32 handle,
                           wxHaikuEventType type, BPoint where,
                           int32 transit = 0, const BMessage *drag = NULL);
// returns true if the message was consumed (wheel, drop, input method)
bool wxHaikuHandleViewMessage(BView *view, wxUint32 handle, BMessage *message);

// The view of a wxWindow.
class wxHaikuView : public wxHaikuDrawView
{
public:
    wxHaikuView(wxUint32 handle, BRect frame, uint32 resizingMode);

    wxUint32 GetHandle() const { return m_handle; }

    virtual void Draw(BRect updateRect);
    virtual void MouseDown(BPoint where);
    virtual void MouseUp(BPoint where);
    virtual void MouseMoved(BPoint where, uint32 transit,
                            const BMessage *dragMessage);
    virtual void MessageReceived(BMessage *message);

private:
    wxUint32 m_handle;
};

// The window of a wxNonOwnedWindow.
class wxHaikuWindow : public BWindow
{
public:
    wxHaikuWindow(wxUint32 handle, BRect frame, const char *title,
                  window_look look, window_feel feel, uint32 flags);

    virtual void DispatchMessage(BMessage *message, BHandler *target);
    virtual void MessageReceived(BMessage *message);
    virtual void FrameMoved(BPoint newPosition);
    virtual void FrameResized(float newWidth, float newHeight);
    virtual void WindowActivated(bool active);
    virtual bool QuitRequested();
    virtual void Minimize(bool minimize);
    virtual void Zoom(BPoint origin, float width, float height);
    virtual void ScreenChanged(BRect frame, color_space mode);
    virtual void WorkspaceActivated(int32 workspace, bool active);

private:
    void PostFrame(bool sync = false);

    wxUint32 m_handle;
};

// Posted by the wx thread to its BWindows after it moved or resized them:
// answered with a wxHAIKU_EVT_FRAME record with the flag set.
const uint32 wxHAIKU_MSG_FRAME_SYNC = 'wxFS';

// private window feel of menus and tool tips: above everything, all
// workspaces, never the active window (see headers/private/interface)
const window_feel wxHAIKU_MENU_WINDOW_FEEL = (window_feel)1025;

// ----------------------------------------------------------------------------
// paint sessions
// ----------------------------------------------------------------------------

// The off-screen buffer a window is painted into: created by
// wxWindowHaiku::HaikuPaint() around the paint events, all DCs created on the
// window meanwhile draw into the buffer, which is copied to the window when
// the session ends.
class wxHaikuPaintSession
{
public:
    // region: the area to paint in window coordinates
    wxHaikuPaintSession(wxWindowHaiku *win, const wxRegion& region);
    ~wxHaikuPaintSession();

    bool IsOk() const { return m_view != NULL; }

    // the view of the buffer and the position of the window's origin in it
    BView *GetView() const { return m_view; }
    wxPoint GetOffset() const { return m_offset; }

    // the area being painted, in window coordinates
    const wxRect& GetBox() const { return m_box; }
    const wxRegion& GetRegion() const { return m_region; }

private:
    wxUint32 m_winHandle;
    BView *m_view;
    BBitmap *m_bitmap;
    int m_bufferIndex;
    wxPoint m_offset;
    wxRect m_box;
    wxRegion m_region;

    wxDECLARE_NO_COPY_CLASS(wxHaikuPaintSession);
};

// ----------------------------------------------------------------------------
// input state helpers (wx thread)
// ----------------------------------------------------------------------------

// wx modifier flags (wxMOD_XXX) for Be modifiers(): Command is wxMOD_CONTROL
// (the key of the standard shortcuts), Control is wxMOD_RAW_CONTROL, Option
// is wxMOD_ALT.
int wxHaikuModifiers(int32 modifiers);

// the modifiers and buttons of the last input event
int32 wxHaikuCurrentModifiers();
void wxHaikuSetCurrentModifiers(int32 modifiers);
int32 wxHaikuCurrentButtons();
wxPoint wxHaikuCurrentMousePosition();

// fills the key event from the record; returns false for keys wx has no
// code for
bool wxHaikuTranslateKey(const wxHaikuEvent& event, wxKeyEvent& keyEvent,
                         wxChar *unicode);

// the font size (be_plain_font) relative to the 12 pixels of an unscaled
// system: air/OS scales the system fonts for HiDPI screens
double wxHaikuScaleFactor();

// called when the system fonts or colours changed
void wxHaikuResetSystemSettings();

// frees the paint buffers (application shutdown)
void wxHaikuFreePaintBuffers();

// frees the server side resources of bitmaps kept for drawing
void wxHaikuFreeBitmapViews();

// the window the mouse events go to at the moment was destroyed etc.
void wxHaikuWindowDestroyed(wxWindowHaiku *win);

#endif // _WX_HAIKU_PRIVATE_H_
