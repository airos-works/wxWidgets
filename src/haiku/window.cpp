/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/window.cpp
// Purpose:     wxWindowHaiku: wxWindow on a BView, input event translation
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/window.h"
    #include "wx/app.h"
    #include "wx/dcclient.h"
    #include "wx/log.h"
    #include "wx/nonownedwnd.h"
    #include "wx/toplevel.h"
#endif

#include "wx/caret.h"
#include "wx/dnd.h"
#include "wx/hashmap.h"
#include "wx/popupwin.h"
#include "wx/tooltip.h"
#include "wx/vector.h"

#include "wx/haiku/private.h"

#include <Cursor.h>
#include <Messenger.h>
#include <InterfaceDefs.h>
#include <Screen.h>

// The request set_mouse_position() of the Game Kit sends to input_server
// (private, see headers/private/input): not worth linking libgame for.
extern status_t _control_input_server_(BMessage *command, BMessage *reply);

// ============================================================================
// global state (wx thread only)
// ============================================================================

namespace
{

WX_DECLARE_HASH_MAP(wxUint32, wxWindowHaiku *,
                    wxIntegerHash, wxIntegerEqual, HandleMap);
HandleMap gs_handles;

// the window which has the wx focus
wxWindowHaiku *gs_focusWindow = NULL;
// the window which is about to get it, while the previous one loses it
wxWindowHaiku *gs_toBeFocused = NULL;

// the window which captured the mouse with CaptureMouse()
wxWindowHaiku *gs_captureWindow = NULL;
// the window a button was pressed in: it gets the mouse events until all
// buttons are released, like the other toolkits' implicit grab
wxWindowHaiku *gs_buttonWindow = NULL;
// the window the pointer is in, for the enter and leave events
wxWindowHaiku *gs_mouseWindow = NULL;
// the window of the last button press and its time, for double clicks
wxWindowHaiku *gs_lastClickWindow = NULL;

// the state of the last input event
int32 gs_buttons = 0;
int32 gs_modifiers = 0;
wxPoint gs_mousePos;

// the last mouse message handled: the same message is delivered to several
// views when one of them has the pointer event mask set
struct MouseSignature
{
    MouseSignature() : type(wxHAIKU_EVT_NONE), when(0), buttons(0) { }

    wxHaikuEventType type;
    bigtime_t when;
    BPoint where;
    int32 buttons;
} gs_lastMouse;

// windows with a pending update region
wxVector<wxWindowHaiku *> gs_dirtyWindows;
bool gs_inPaintPhase = false;

inline wxWindow *AsWindow(wxWindowHaiku *win)
{
    return static_cast<wxWindow *>(win);
}

void FillMouseState(wxMouseEvent& event, int32 buttons, int32 modifiers)
{
    event.m_leftDown = (buttons & B_PRIMARY_MOUSE_BUTTON) != 0;
    event.m_rightDown = (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;
    event.m_middleDown = (buttons & B_TERTIARY_MOUSE_BUTTON) != 0;
    event.m_aux1Down = (buttons & 0x08) != 0;
    event.m_aux2Down = (buttons & 0x10) != 0;

    const int mods = wxHaikuModifiers(modifiers);
    event.m_controlDown = (mods & wxMOD_CONTROL) != 0;
    event.m_shiftDown = (mods & wxMOD_SHIFT) != 0;
    event.m_altDown = (mods & wxMOD_ALT) != 0;
    event.m_metaDown = (mods & wxMOD_META) != 0;
}

// position of the pointer in the client coordinates of the window
wxPoint ClientPosFromScreen(wxWindowHaiku *win, const wxPoint& screen)
{
    return screen - win->HaikuGetScreenOrigin() - win->GetClientAreaOrigin();
}

void SendMouseEvent(wxWindowHaiku *win, wxEventType type,
                    const wxPoint& pos, const wxHaikuEvent& he,
                    int clickCount = -1)
{
    wxMouseEvent event(type);
    event.SetEventObject(win);
    event.SetId(win->GetId());
    event.SetTimestamp((long)(he.when / 1000));
    event.m_x = pos.x;
    event.m_y = pos.y;
    event.m_clickCount = clickCount;
    FillMouseState(event, he.buttons, he.modifiers);

    win->HandleWindowEvent(event);
}

// the pointer is in the given window now (NULL: in none of ours)
void SetMouseWindow(wxWindowHaiku *win, const wxHaikuEvent& he)
{
    if ( win == gs_mouseWindow )
        return;

    wxWindowHaiku * const old = gs_mouseWindow;
    gs_mouseWindow = win;

    const wxPoint screen((int)he.screenWhere.x, (int)he.screenWhere.y);

    if ( old )
    {
        SendMouseEvent(old, wxEVT_LEAVE_WINDOW,
                       ClientPosFromScreen(old, screen), he);
    }

    // the handler of the leave event may have destroyed the window
    if ( win && gs_mouseWindow == win )
    {
        SendMouseEvent(win, wxEVT_ENTER_WINDOW,
                       ClientPosFromScreen(win, screen), he);
    }
}

// true if the window may get input: it and its top level window are enabled
bool CanGetInput(wxWindowHaiku *win)
{
    return win->IsEnabled();
}

struct ButtonInfo
{
    int32 mask;
    wxEventType down, up, dclick;
};

const ButtonInfo gs_buttonInfo[] =
{
    { B_PRIMARY_MOUSE_BUTTON, wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK },
    { B_SECONDARY_MOUSE_BUTTON, wxEVT_RIGHT_DOWN, wxEVT_RIGHT_UP, wxEVT_RIGHT_DCLICK },
    { B_TERTIARY_MOUSE_BUTTON, wxEVT_MIDDLE_DOWN, wxEVT_MIDDLE_UP, wxEVT_MIDDLE_DCLICK },
    { 0x08, wxEVT_AUX1_DOWN, wxEVT_AUX1_UP, wxEVT_AUX1_DCLICK },
    { 0x10, wxEVT_AUX2_DOWN, wxEVT_AUX2_UP, wxEVT_AUX2_DCLICK },
};

// the deepest shown window of the tree at the screen position or NULL
wxWindowHaiku *FindWindowAt(wxWindowHaiku *win, const wxPoint& screen)
{
    if ( !win->IsShown() )
        return NULL;

    const wxRect rect(win->HaikuGetScreenOrigin(), win->HaikuGetViewSize());
    if ( !rect.Contains(screen) )
        return NULL;

    // the last child is the topmost one
    for ( wxWindowList::compatibility_iterator
            node = win->GetChildren().GetLast();
          node;
          node = node->GetPrevious() )
    {
        wxWindow * const child = node->GetData();
        if ( child->IsTopLevel() )
            continue;

        wxWindowHaiku * const found = FindWindowAt(child, screen);
        if ( found )
            return found;
    }

    return win;
}

void HandleMouseEvent(wxWindowHaiku *source, const wxHaikuEvent& he)
{
    // drop the copies of a message already handled
    if ( he.type == gs_lastMouse.type && he.when == gs_lastMouse.when
            && he.screenWhere == gs_lastMouse.where
            && he.buttons == gs_lastMouse.buttons )
    {
        return;
    }

    gs_lastMouse.type = he.type;
    gs_lastMouse.when = he.when;
    gs_lastMouse.where = he.screenWhere;
    gs_lastMouse.buttons = he.buttons;

    wxHAIKU_TRACE("mouse %s src=%u (%s) where=%g,%g screen=%g,%g buttons=%d "
                  "clicks=%d transit=%d capture=%u button=%u",
                  he.type == wxHAIKU_EVT_MOUSE_DOWN ? "down"
                    : he.type == wxHAIKU_EVT_MOUSE_UP ? "up" : "move",
                  (unsigned)source->GetHaikuHandle(),
                  (const char *)wxString(source->GetClassInfo()->GetClassName()).utf8_str(),
                  he.where.x, he.where.y, he.screenWhere.x, he.screenWhere.y,
                  (int)he.buttons, (int)he.clicks, (int)he.transit,
                  gs_captureWindow ? (unsigned)gs_captureWindow->GetHaikuHandle() : 0,
                  gs_buttonWindow ? (unsigned)gs_buttonWindow->GetHaikuHandle() : 0);

    const int32 oldButtons = gs_buttons;
    gs_buttons = he.buttons;
    gs_modifiers = he.modifiers;

    const wxPoint screen((int)floorf(he.screenWhere.x),
                         (int)floorf(he.screenWhere.y));
    gs_mousePos = screen;

    // who gets the event
    wxWindowHaiku *target = gs_captureWindow ? gs_captureWindow
                                             : gs_buttonWindow ? gs_buttonWindow
                                                               : source;

    // A popup captures the mouse to learn about the clicks outside of it
    // (wxPopupTransientWindow keeps the capture while it is shown): the
    // windows inside of the popup still get their events. The message may
    // have come through the capturing view or through the one under the
    // pointer, whichever was faster: find the window from the position.
    if ( gs_captureWindow && !gs_buttonWindow )
    {
        wxNonOwnedWindow * const popup = gs_captureWindow->GetTLW();
        if ( popup && popup->HaikuAvoidsFocus() )
        {
            wxWindowHaiku * const under = FindWindowAt(popup, screen);
            if ( under )
                target = under;
        }
    }
    else if ( gs_captureWindow && gs_buttonWindow
                && gs_buttonWindow != gs_captureWindow )
    {
        // a button pressed in such a window before: it keeps the events
        // until the buttons are released
        wxNonOwnedWindow * const popup = gs_captureWindow->GetTLW();
        if ( popup && popup->HaikuAvoidsFocus()
                && gs_buttonWindow->GetTLW() == popup )
        {
            target = gs_buttonWindow;
        }
    }

    // Without any capture the message belongs to the window under the
    // pointer. It may have come through a view which only got it because of
    // an event mask it had when the message was sent (a popup hidden since,
    // the view of the last capture): look the window up then.
    if ( !gs_captureWindow && !gs_buttonWindow
            && !(he.type == wxHAIKU_EVT_MOUSE_MOVED
                    && (he.transit == B_EXITED_VIEW
                            || he.transit == B_OUTSIDE_VIEW)) )
    {
        if ( !source->IsShownOnScreen()
                || !wxRect(source->HaikuGetScreenOrigin(),
                           source->HaikuGetViewSize()).Contains(screen) )
        {
            wxWindow * const under = wxFindWindowAtPoint(screen);
            if ( !under )
                return;

            target = under;
        }
    }

    const bool captured = gs_captureWindow || gs_buttonWindow;

    wxPoint pos;
    if ( target == source )
    {
        pos = wxPoint((int)floorf(he.where.x), (int)floorf(he.where.y))
                - target->GetClientAreaOrigin();
    }
    else
    {
        pos = ClientPosFromScreen(target, screen);
    }

    if ( !captured && !CanGetInput(target) )
    {
        // Input for a disabled window: when a modal dialog disabled it,
        // bring the dialog to the user's attention instead.
        if ( he.type == wxHAIKU_EVT_MOUSE_DOWN )
        {
            for ( wxWindowList::compatibility_iterator
                    node = wxTopLevelWindows.GetLast();
                  node;
                  node = node->GetPrevious() )
            {
                wxNonOwnedWindow * const
                    tlw = wxDynamicCast(node->GetData(), wxNonOwnedWindow);
                if ( tlw && tlw->HaikuIsModal() && tlw->IsShown() )
                {
                    tlw->Raise();
                    break;
                }
            }
        }

        if ( he.type == wxHAIKU_EVT_MOUSE_MOVED )
            SetMouseWindow(NULL, he);

        return;
    }

    switch ( he.type )
    {
        case wxHAIKU_EVT_MOUSE_MOVED:
            {
                // enter and leave
                if ( gs_captureWindow && target != gs_captureWindow
                        && !gs_buttonWindow )
                {
                    // over a window of a popup which captured the mouse
                    SetMouseWindow(target, he);
                }
                else if ( gs_captureWindow || gs_buttonWindow )
                {
                    // the capturing window is "entered" while the pointer
                    // is really over it
                    const wxRect rect(target->HaikuGetScreenOrigin(),
                                      target->HaikuGetViewSize());
                    SetMouseWindow(rect.Contains(screen) ? target : NULL, he);
                }
                else if ( he.transit == B_EXITED_VIEW
                            || he.transit == B_OUTSIDE_VIEW )
                {
                    // the pointer may have gone to another window of ours:
                    // its own message says so if it has not already
                    if ( gs_mouseWindow == source )
                        SetMouseWindow(NULL, he);
                    return;
                }
                else
                {
                    SetMouseWindow(target, he);
                }

                if ( wxWindowHaiku::HaikuFindByHandle(target->GetHaikuHandle()) != target )
                    return; // destroyed by an enter or leave handler

                SendMouseEvent(target, wxEVT_MOTION, pos, he);
            }
            break;

        case wxHAIKU_EVT_MOUSE_DOWN:
            {
                if ( !gs_captureWindow && !gs_buttonWindow )
                {
                    SetMouseWindow(target, he);
                    gs_buttonWindow = target;
                }
                else if ( !gs_buttonWindow && target != gs_captureWindow )
                {
                    // inside a popup which captured the mouse
                    gs_buttonWindow = target;
                }

                const int32 pressed = he.buttons & ~oldButtons;

                // Clicking a window gives it the focus if it wants it. The
                // windows of popups which don't contain controls never do:
                // they must not take the focus away from their owner.
                if ( pressed && !gs_captureWindow )
                {
                    wxNonOwnedWindow * const tlw = target->GetTLW();
                    const bool noFocus = tlw && tlw->HaikuAvoidsFocus()
                        && !tlw->HasFlag(wxPU_CONTAINS_CONTROLS);
                    if ( !noFocus && target != gs_focusWindow
                            && target->CanAcceptFocus() )
                    {
                        target->SetFocus();
                    }
                }

                const wxUint32 handle = target->GetHaikuHandle();
                for ( size_t n = 0; n < WXSIZEOF(gs_buttonInfo); n++ )
                {
                    const ButtonInfo& info = gs_buttonInfo[n];
                    if ( !(pressed & info.mask) )
                        continue;

                    // app_server counts the clicks: even counts are double
                    // clicks if they hit the same window
                    const bool dclick = he.clicks > 1 && (he.clicks % 2) == 0
                                            && gs_lastClickWindow == target;
                    gs_lastClickWindow = target;

                    SendMouseEvent(target, dclick ? info.dclick : info.down,
                                   pos, he, dclick ? 2 : 1);

                    if ( wxWindowHaiku::HaikuFindByHandle(handle) != target )
                        break;
                }
            }
            break;

        case wxHAIKU_EVT_MOUSE_UP:
            {
                const int32 released = oldButtons & ~he.buttons;

                const wxUint32 handle = target->GetHaikuHandle();
                for ( size_t n = 0; n < WXSIZEOF(gs_buttonInfo); n++ )
                {
                    const ButtonInfo& info = gs_buttonInfo[n];
                    if ( !(released & info.mask) )
                        continue;

                    SendMouseEvent(target, info.up, pos, he, 1);

                    if ( wxWindowHaiku::HaikuFindByHandle(handle) != target )
                        break;
                }

                if ( !he.buttons )
                {
                    gs_buttonWindow = NULL;

                    // without the implicit grab the pointer may be over
                    // another window by now: the next move tells, but a
                    // window the pointer left must know at once
                    if ( !gs_captureWindow && gs_mouseWindow
                            && gs_mouseWindow != source )
                    {
                        SetMouseWindow(NULL, he);
                    }
                }
            }
            break;

        default:
            break;
    }
}

void HandleWheelEvent(wxWindowHaiku *source, const wxHaikuEvent& he)
{
    gs_modifiers = he.modifiers;

    wxWindowHaiku *target = gs_captureWindow ? gs_captureWindow : source;
    if ( !gs_captureWindow && !CanGetInput(target) )
        return;

    for ( int axis = 0; axis < 2; axis++ )
    {
        const float delta = axis == 0 ? he.deltaY : he.deltaX;
        if ( delta == 0 )
            continue;

        // an unhandled wheel event goes up to the parents, as it does
        // natively in the other toolkits: scrolled windows are scrolled by
        // the wheel over their children
        for ( wxWindowHaiku *win = target; win; )
        {
            wxMouseEvent event(wxEVT_MOUSEWHEEL);
            event.SetEventObject(win);
            event.SetId(win->GetId());
            event.SetTimestamp((long)(he.when / 1000));
            const wxPoint pos = ClientPosFromScreen(win, gs_mousePos);
            event.m_x = pos.x;
            event.m_y = pos.y;
            FillMouseState(event, gs_buttons, he.modifiers);
            event.m_wheelDelta = 120;
            event.m_linesPerAction = 3;
            event.m_columnsPerAction = 3;
            if ( axis == 0 )
            {
                // positive deltas scroll down, positive rotations are away
                // from the user
                event.m_wheelRotation = (int)(-delta * 120);
                event.m_wheelAxis = wxMOUSE_WHEEL_VERTICAL;
            }
            else
            {
                event.m_wheelRotation = (int)(delta * 120);
                event.m_wheelAxis = wxMOUSE_WHEEL_HORIZONTAL;
            }

            const wxUint32 handle = win->GetHaikuHandle();
            if ( win->HandleWindowEvent(event) )
                break;

            if ( wxWindowHaiku::HaikuFindByHandle(handle) != win
                    || win->IsTopLevel() )
                break;

            win = win->GetParent();
        }
    }
}

#if wxUSE_DRAG_AND_DROP
void HandleDropEvent(wxWindowHaiku *source, const wxHaikuEvent& he);
#endif

} // anonymous namespace

// ----------------------------------------------------------------------------
// accessors for the other files of the port
// ----------------------------------------------------------------------------

int wxHaikuModifiers(int32 modifiers)
{
    int mods = wxMOD_NONE;

    if ( modifiers & B_SHIFT_KEY )
        mods |= wxMOD_SHIFT;

    // Command is the key of the standard shortcuts (Alt with the default key
    // map, Control if the user prefers), so it is what wx calls Control;
    // Control then plays the role of Alt and Option is the "Windows" key.
    if ( modifiers & B_COMMAND_KEY )
        mods |= wxMOD_CONTROL;
    if ( modifiers & B_CONTROL_KEY )
        mods |= wxMOD_ALT;
    if ( modifiers & B_OPTION_KEY )
        mods |= wxMOD_META;

    return mods;
}

int32 wxHaikuCurrentModifiers()
{
    return gs_modifiers;
}

void wxHaikuSetCurrentModifiers(int32 modifiers)
{
    gs_modifiers = modifiers;
}

int32 wxHaikuCurrentButtons()
{
    return gs_buttons;
}

wxPoint wxHaikuCurrentMousePosition()
{
    return gs_mousePos;
}

/* static */
wxWindowHaiku *wxWindowHaiku::HaikuFindByHandle(wxUint32 handle)
{
    HandleMap::iterator it = gs_handles.find(handle);
    return it == gs_handles.end() ? NULL : it->second;
}

bool wxHaikuPaintPendingWindows()
{
    if ( gs_inPaintPhase || gs_dirtyWindows.empty() )
        return false;

    gs_inPaintPhase = true;

    bool painted = false;

    // Painting may invalidate more windows (and destroy some): go on until
    // nothing is left, within reason.
    for ( int pass = 0; pass < 4 && !gs_dirtyWindows.empty(); pass++ )
    {
        wxVector<wxUint32> handles;
        handles.reserve(gs_dirtyWindows.size());
        for ( size_t n = 0; n < gs_dirtyWindows.size(); n++ )
            handles.push_back(gs_dirtyWindows[n]->GetHaikuHandle());
        gs_dirtyWindows.clear();

        for ( size_t n = 0; n < handles.size(); n++ )
        {
            wxWindowHaiku * const
                win = wxWindowHaiku::HaikuFindByHandle(handles[n]);
            if ( win && win->HaikuPaint() )
                painted = true;
        }
    }

    gs_inPaintPhase = false;

    return painted;
}

void wxHaikuWindowDestroyed(wxWindowHaiku *win)
{
    if ( gs_focusWindow == win )
        gs_focusWindow = NULL;
    if ( gs_toBeFocused == win )
        gs_toBeFocused = NULL;
    if ( gs_captureWindow == win )
        gs_captureWindow = NULL;
    if ( gs_buttonWindow == win )
        gs_buttonWindow = NULL;
    if ( gs_mouseWindow == win )
        gs_mouseWindow = NULL;
    if ( gs_lastClickWindow == win )
        gs_lastClickWindow = NULL;

    for ( size_t n = 0; n < gs_dirtyWindows.size(); n++ )
    {
        if ( gs_dirtyWindows[n] == win )
        {
            gs_dirtyWindows.erase(gs_dirtyWindows.begin() + n);
            break;
        }
    }
}

// ============================================================================
// wxWindowHaiku
// ============================================================================

wxIMPLEMENT_ABSTRACT_CLASS(wxWindowHaiku, wxWindowBase);

wxBEGIN_EVENT_TABLE(wxWindowHaiku, wxWindowBase)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// creation and destruction
// ----------------------------------------------------------------------------

void wxWindowHaiku::Init()
{
    m_isShown = true;
    m_view = NULL;
    m_handle = wxHaikuNewHandle();
    m_tlw = NULL;
    m_inDirtyList = false;
    m_paintSession = NULL;
    m_viewShown = true;
    m_acceptsFiles = false;

    gs_handles[m_handle] = this;
}

wxWindowHaiku::~wxWindowHaiku()
{
    SendDestroyEvent();

    if ( gs_captureWindow == this )
    {
        HaikuApplyCapture(false);
        gs_captureWindow = NULL;
    }

    if ( gs_focusWindow == this )
    {
        gs_focusWindow = NULL;
        if ( m_tlw && m_tlw != this && m_tlw->HaikuGetLastFocus() == this )
            m_tlw->HaikuSetLastFocus(NULL);
    }
    else if ( m_tlw && m_tlw != this
                && m_tlw->HaikuGetLastFocus() == AsWindow(this) )
    {
        m_tlw->HaikuSetLastFocus(NULL);
    }

    DestroyChildren();

    wxHaikuWindowDestroyed(this);
    gs_handles.erase(m_handle);

    // The view of a top level window is deleted with its BWindow by
    // wxNonOwnedWindow.
    if ( m_view && !IsTopLevel() )
    {
        {
            wxHaikuViewLock lock(m_view);
            if ( lock.CanTouch() )
                m_view->RemoveSelf();
        }

        delete m_view;
    }

    m_view = NULL;
}

bool wxWindowHaiku::Create(wxWindow *parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxString& name)
{
    wxCHECK_MSG( parent, false, "a child window needs a parent" );

    if ( !m_tlw )
        m_tlw = parent->GetTLW();

    if ( !CreateBase(parent, id, pos, size, style, wxDefaultValidator, name) )
        return false;

    parent->AddChild(this);

    int x = pos.x == wxDefaultCoord ? 0 : pos.x,
        y = pos.y == wxDefaultCoord ? 0 : pos.y;
    AdjustForParentClientOrigin(x, y, 0);

    m_rect = wxRect(x, y, WidthDefault(size.x), HeightDefault(size.y));

    m_view = HaikuCreateView(m_rect);
    if ( !m_view )
        return false;

    HaikuAttachView();

    return true;
}

BView *wxWindowHaiku::HaikuCreateView(const wxRect& rect)
{
    // a view cannot be empty: empty windows are hidden instead
    return new wxHaikuView(m_handle,
                           wxHaikuRect(rect.x, rect.y,
                                       wxMax(rect.width, 1),
                                       wxMax(rect.height, 1)),
                           B_FOLLOW_NONE);
}

void wxWindowHaiku::HaikuAttachView()
{
    wxWindowHaiku * const parent = GetParent();
    if ( !parent || !parent->m_view || !m_view )
        return;

    const bool visible = m_isShown && m_rect.width > 0 && m_rect.height > 0;
    if ( visible != m_viewShown )
    {
        // not attached yet: no lock needed
        if ( visible )
            m_view->Show();
        else
            m_view->Hide();
        m_viewShown = visible;
    }

    {
        wxHaikuViewLock lock(parent->m_view);
        if ( lock.CanTouch() )
            parent->m_view->AddChild(m_view);
    }

    HaikuAttachedToWindow();
}

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

wxPoint wxWindowHaiku::HaikuGetScreenOrigin() const
{
    wxPoint pt;
    const wxWindowHaiku *win = this;
    while ( win && !win->IsTopLevel() )
    {
        pt += win->m_rect.GetPosition();
        win = win->GetParent();
    }

    if ( win )
    {
        pt += static_cast<const wxNonOwnedWindow *>(
                static_cast<const wxWindow *>(win))->HaikuGetContentOrigin();
    }

    return pt;
}

void wxWindowHaiku::DoGetSize(int *width, int *height) const
{
    if ( width )
        *width = m_rect.width;
    if ( height )
        *height = m_rect.height;
}

void wxWindowHaiku::DoGetPosition(int *x, int *y) const
{
    // m_rect is in the coordinates of the parent's view, the position of a
    // window is relative to the client area of its parent
    wxPoint origin;
    if ( GetParent() )
        origin = GetParent()->GetClientAreaOrigin();

    if ( x )
        *x = m_rect.x - origin.x;
    if ( y )
        *y = m_rect.y - origin.y;
}

void wxWindowHaiku::DoGetClientSize(int *width, int *height) const
{
    DoGetSize(width, height);
}

void wxWindowHaiku::DoScreenToClient(int *x, int *y) const
{
    const wxPoint o = HaikuGetScreenOrigin() + GetClientAreaOrigin();

    if ( x )
        *x -= o.x;
    if ( y )
        *y -= o.y;
}

void wxWindowHaiku::DoClientToScreen(int *x, int *y) const
{
    const wxPoint o = HaikuGetScreenOrigin() + GetClientAreaOrigin();

    if ( x )
        *x += o.x;
    if ( y )
        *y += o.y;
}

void wxWindowHaiku::DoMoveWindow(int x, int y, int width, int height)
{
    const bool moved = x != m_rect.x || y != m_rect.y;
    const bool resized = width != m_rect.width || height != m_rect.height;

    m_rect = wxRect(x, y, width, height);

    if ( !m_view )
        return;

    const bool visible = m_isShown && width > 0 && height > 0;

    wxHaikuViewLock lock(m_view);
    if ( !lock.CanTouch() )
        return;

    if ( moved )
        m_view->MoveTo(x, y);
    if ( resized )
        m_view->ResizeTo(wxMax(width, 1) - 1, wxMax(height, 1) - 1);

    if ( visible != m_viewShown )
    {
        if ( visible )
            m_view->Show();
        else
            m_view->Hide();
        m_viewShown = visible;
    }
}

void wxWindowHaiku::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    const bool keepX = x == wxDefaultCoord && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE),
               keepY = y == wxDefaultCoord && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE);

    wxSize best(wxDefaultCoord, wxDefaultCoord);
    if ( width == wxDefaultCoord )
    {
        if ( sizeFlags & wxSIZE_AUTO_WIDTH )
        {
            best = DoGetBestSize();
            width = best.x;
        }
        else
        {
            width = m_rect.width;
        }
    }

    if ( height == wxDefaultCoord )
    {
        if ( sizeFlags & wxSIZE_AUTO_HEIGHT )
        {
            if ( best.y == wxDefaultCoord )
                best = DoGetBestSize();
            height = best.y;
        }
        else
        {
            height = m_rect.height;
        }
    }

    const int maxWidth = GetMaxWidth(),
              minWidth = GetMinWidth(),
              maxHeight = GetMaxHeight(),
              minHeight = GetMinHeight();

    if ( minWidth != wxDefaultCoord && width < minWidth )
        width = minWidth;
    if ( maxWidth != wxDefaultCoord && width > maxWidth )
        width = maxWidth;
    if ( minHeight != wxDefaultCoord && height < minHeight )
        height = minHeight;
    if ( maxHeight != wxDefaultCoord && height > maxHeight )
        height = maxHeight;

    if ( width < 0 )
        width = 0;
    if ( height < 0 )
        height = 0;

    // The native position depends on the client area origin of the parent,
    // which changes (a frame gets a menu bar): compare native positions.
    int ox = 0, oy = 0;
    AdjustForParentClientOrigin(ox, oy, sizeFlags);

    const int nx = keepX ? m_rect.x : x + ox,
              ny = keepY ? m_rect.y : y + oy;

    if ( nx == m_rect.x && ny == m_rect.y
            && width == m_rect.width && height == m_rect.height )
    {
        return;
    }

    const bool resized = width != m_rect.width || height != m_rect.height;

    DoMoveWindow(nx, ny, width, height);

    if ( resized )
    {
        wxSizeEvent event(wxSize(width, height), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }
}

void wxWindowHaiku::DoSetClientSize(int width, int height)
{
    SetSize(width, height);
}

// ----------------------------------------------------------------------------
// state
// ----------------------------------------------------------------------------

bool wxWindowHaiku::Show(bool show)
{
    if ( !wxWindowBase::Show(show) )
        return false;

    if ( m_view && !IsTopLevel() )
    {
        const bool visible = show && m_rect.width > 0 && m_rect.height > 0;
        if ( visible != m_viewShown )
        {
            wxHaikuViewLock lock(m_view);
            if ( lock.CanTouch() )
            {
                if ( visible )
                    m_view->Show();
                else
                    m_view->Hide();
                m_viewShown = visible;
            }
        }
    }

    if ( !show )
    {
        // a hidden window does not keep the mouse or the focus
        if ( gs_mouseWindow == this )
            gs_mouseWindow = NULL;
        if ( gs_buttonWindow == this )
            gs_buttonWindow = NULL;
    }

    return true;
}

void wxWindowHaiku::Raise()
{
    wxWindowHaiku * const parent = GetParent();
    if ( !parent || !parent->m_view || !m_view )
        return;

    // siblings are stacked in the order they were added
    wxHaikuViewLock lock(parent->m_view);
    if ( !lock.CanTouch() )
        return;

    if ( m_view->NextSibling() )
    {
        m_view->RemoveSelf();
        parent->m_view->AddChild(m_view);
    }
}

void wxWindowHaiku::Lower()
{
    wxWindowHaiku * const parent = GetParent();
    if ( !parent || !parent->m_view || !m_view )
        return;

    wxHaikuViewLock lock(parent->m_view);
    if ( !lock.CanTouch() )
        return;

    BView * const first = parent->m_view->ChildAt(0);
    if ( first && first != m_view )
    {
        m_view->RemoveSelf();
        parent->m_view->AddChild(m_view, first);
    }
}

static void wxHaikuSetTLWRecursively(wxWindowHaiku *win, wxNonOwnedWindow *tlw);

bool wxWindowHaiku::Reparent(wxWindowBase *newParent)
{
    if ( !wxWindowBase::Reparent(newParent) )
        return false;

    if ( m_view && !IsTopLevel() )
    {
        {
            wxHaikuViewLock lock(m_view);
            if ( lock.CanTouch() )
                m_view->RemoveSelf();
        }

        wxWindowHaiku * const parent = GetParent();
        if ( parent )
        {
            wxHaikuSetTLWRecursively(this, parent->GetTLW());
            HaikuAttachView();
        }
    }

    return true;
}

void wxWindowHaiku::DoEnable(bool WXUNUSED(enable))
{
    // disabled windows are not given input by the event translation code
}

void wxWindowHaiku::DoFreeze()
{
}

void wxWindowHaiku::DoThaw()
{
    // what was invalidated while frozen is painted now
    if ( !m_invalid.IsEmpty() && !m_inDirtyList )
    {
        m_inDirtyList = true;
        gs_dirtyWindows.push_back(this);
    }
}

// ----------------------------------------------------------------------------
// focus
// ----------------------------------------------------------------------------

void wxWindowHaiku::SetFocus()
{
    if ( gs_focusWindow == this )
        return;

    wxNonOwnedWindow * const tlw = GetTLW();
    if ( tlw )
    {
        tlw->HaikuSetLastFocus(AsWindow(this));

        // The focus really changes only inside the active window: a window
        // of an inactive one gets it when its top level window is activated.
        // Popups never become active: their owner is.
        bool active = false;
        for ( wxWindow *top = tlw; top; )
        {
            wxTopLevelWindow * const
                realTLW = wxDynamicCast(top, wxTopLevelWindow);
            if ( realTLW )
            {
                active = realTLW->IsActive();
                break;
            }

            // a popup: look at the window it belongs to
            top = wxGetTopLevelParent(top->GetParent());
        }

        if ( !active )
            return;
    }

    wxWindowHaiku * const oldFocus = gs_focusWindow;
    const wxUint32 handle = m_handle;

    if ( oldFocus )
    {
        gs_toBeFocused = this;
        gs_focusWindow = NULL;

        if ( !oldFocus->IsBeingDeleted() )
        {
#if wxUSE_CARET
            wxCaret * const caret = oldFocus->GetCaret();
            if ( caret )
                caret->OnKillFocus();
#endif // wxUSE_CARET

            wxFocusEvent event(wxEVT_KILL_FOCUS, oldFocus->GetId());
            event.SetEventObject(oldFocus);
            event.SetWindow(AsWindow(this));
            oldFocus->HandleWindowEvent(event);
        }

        gs_toBeFocused = NULL;

        // the kill focus handler may have destroyed us or given the focus
        // to another window
        if ( HaikuFindByHandle(handle) != this || gs_focusWindow )
            return;
    }

    gs_focusWindow = this;

    // The native focus view gets the input method events. BView::MakeFocus()
    // talks to input_server synchronously when input method aware views are
    // involved, which can take until the next input event: the looper thread
    // does it, this thread must not wait.
    if ( m_view && m_view->Window() )
        BMessenger(m_view).SendMessage(wxHAIKU_MSG_MAKE_FOCUS);

    wxChildFocusEvent eventFocus(AsWindow(this));
    HandleWindowEvent(eventFocus);

    if ( HaikuFindByHandle(handle) != this || gs_focusWindow != this )
        return;

    wxFocusEvent event(wxEVT_SET_FOCUS, GetId());
    event.SetEventObject(this);
    event.SetWindow(oldFocus && HaikuFindByHandle(oldFocus->GetHaikuHandle())
                        ? AsWindow(oldFocus) : NULL);
    HandleWindowEvent(event);

#if wxUSE_CARET
    if ( HaikuFindByHandle(handle) == this )
    {
        wxCaret * const caret = GetCaret();
        if ( caret )
            caret->OnSetFocus();
    }
#endif // wxUSE_CARET
}

// called when the focus window loses the focus without another wx window
// getting it (its top level window is deactivated)
void wxHaikuKillFocus()
{
    wxWindowHaiku * const win = gs_focusWindow;
    if ( !win )
        return;

    gs_focusWindow = NULL;

    if ( win->IsBeingDeleted() )
        return;

#if wxUSE_CARET
    wxCaret * const caret = win->GetCaret();
    if ( caret )
        caret->OnKillFocus();
#endif // wxUSE_CARET

    wxFocusEvent event(wxEVT_KILL_FOCUS, win->GetId());
    event.SetEventObject(win);
    win->HandleWindowEvent(event);
}

wxWindowHaiku *wxHaikuGetFocusWindow()
{
    return gs_focusWindow;
}

wxWindow *wxWindowBase::DoFindFocus()
{
    return AsWindow(gs_focusWindow);
}

wxWindow *wxGetActiveWindow()
{
    for ( wxWindowList::compatibility_iterator
            node = wxTopLevelWindows.GetFirst();
          node;
          node = node->GetNext() )
    {
        wxTopLevelWindow * const
            tlw = wxDynamicCast(node->GetData(), wxTopLevelWindow);
        if ( tlw && tlw->IsActive() )
            return tlw;
    }

    return NULL;
}

// ----------------------------------------------------------------------------
// mouse
// ----------------------------------------------------------------------------

void wxWindowHaiku::HaikuApplyCapture(bool capture)
{
    if ( !m_view )
        return;

    // With the pointer event mask the view gets all mouse messages, wherever
    // the pointer is, buttons down or not: this is what CaptureMouse() means
    // (popups are dismissed by a click outside of them).
    wxHaikuViewLock lock(m_view);
    status_t status = B_ERROR;
    if ( lock.IsLocked() )
    {
        status = m_view->SetEventMask(capture ? B_POINTER_EVENTS : 0,
                                      capture ? B_NO_POINTER_HISTORY : 0);
    }

    wxHAIKU_TRACE("capture %s win=%u (%s) status=%d", capture ? "on" : "off",
                  (unsigned)m_handle, (const char *)wxString(GetClassInfo()->GetClassName()).utf8_str(),
                  (int)status);
}

void wxWindowHaiku::DoCaptureMouse()
{
    if ( gs_captureWindow && gs_captureWindow != this )
        gs_captureWindow->HaikuApplyCapture(false);

    gs_captureWindow = this;
    HaikuApplyCapture(true);
}

void wxWindowHaiku::DoReleaseMouse()
{
    if ( gs_captureWindow == this )
    {
        HaikuApplyCapture(false);
        gs_captureWindow = NULL;
    }
}

/* static */
wxWindow *wxWindowBase::GetCapture()
{
    return AsWindow(gs_captureWindow);
}

void wxWindowHaiku::WarpPointer(int x, int y)
{
    ClientToScreen(&x, &y);

    BMessage command('Ismp' /* IS_SET_MOUSE_POSITION */);
    BMessage reply;
    command.AddPoint("where", BPoint(x, y));
    _control_input_server_(&command, &reply);
}

void wxWindowHaiku::HaikuApplyCursor()
{
    if ( !m_view )
        return;

    // a window without a cursor of its own uses its parent's
    const wxCursor *cursor = NULL;
    for ( const wxWindowHaiku *win = this; win; win = win->GetParent() )
    {
        if ( win->m_cursor.IsOk() )
        {
            cursor = &win->m_cursor;
            break;
        }

        if ( win->IsTopLevel() )
            break;
    }

    const BCursor *native = cursor ? cursor->GetHaikuCursor() : NULL;

    wxHaikuViewLock lock(m_view);
    if ( !lock.IsLocked() )
        return;

    if ( native )
    {
        m_view->SetViewCursor(native, true);
    }
    else
    {
        BCursor standard(B_CURSOR_ID_SYSTEM_DEFAULT);
        m_view->SetViewCursor(&standard, true);
    }
}

static void wxHaikuApplyCursorRecursively(wxWindowHaiku *win)
{
    // only to the windows which inherit the cursor
    for ( wxWindowList::compatibility_iterator
            node = win->GetChildren().GetFirst();
          node;
          node = node->GetNext() )
    {
        wxWindow * const child = node->GetData();
        if ( child->IsTopLevel() || child->GetCursor().IsOk() )
            continue;

        wxHaikuApplyCursorRecursively(child);
    }

    BView * const view = win->GetHaikuView();
    if ( view )
    {
        const wxCursor *cursor = NULL;
        for ( const wxWindowHaiku *w = win; w; w = w->GetParent() )
        {
            if ( w->GetCursor().IsOk() )
            {
                cursor = &w->GetCursor();
                break;
            }

            if ( w->IsTopLevel() )
                break;
        }

        const BCursor *native = cursor ? cursor->GetHaikuCursor() : NULL;

        wxHaikuViewLock lock(view);
        if ( lock.IsLocked() )
        {
            if ( native )
            {
                view->SetViewCursor(native, true);
            }
            else
            {
                BCursor standard(B_CURSOR_ID_SYSTEM_DEFAULT);
                view->SetViewCursor(&standard, true);
            }
        }
    }
}

bool wxWindowHaiku::SetCursor(const wxCursor& cursor)
{
    if ( !wxWindowBase::SetCursor(cursor) )
        return false;

    wxHaikuApplyCursorRecursively(this);

    return true;
}

// ----------------------------------------------------------------------------
// fonts and text
// ----------------------------------------------------------------------------

bool wxWindowHaiku::SetFont(const wxFont& font)
{
    m_font = font;
    return true;
}

int wxWindowHaiku::GetCharHeight() const
{
    const wxFont font = GetFont();
    if ( !font.IsOk() )
        return 0;

    int ascent, descent;
    font.HaikuGetMetrics(&ascent, &descent, NULL);
    return ascent + descent;
}

int wxWindowHaiku::GetCharWidth() const
{
    const wxFont font = GetFont();
    return font.IsOk() ? font.HaikuGetTextWidth(wxS("0")) : 0;
}

void wxWindowHaiku::DoGetTextExtent(const wxString& string,
                                    int *x, int *y,
                                    int *descent,
                                    int *externalLeading,
                                    const wxFont *theFont) const
{
    const wxFont font = theFont && theFont->IsOk() ? *theFont : GetFont();
    if ( !font.IsOk() )
    {
        if ( x )
            *x = 0;
        if ( y )
            *y = 0;
        if ( descent )
            *descent = 0;
        if ( externalLeading )
            *externalLeading = 0;
        return;
    }

    int ascent, fontDescent, leading;
    font.HaikuGetMetrics(&ascent, &fontDescent, &leading);

    if ( x )
        *x = font.HaikuGetTextWidth(string);
    if ( y )
        *y = ascent + fontDescent;
    if ( descent )
        *descent = fontDescent;
    if ( externalLeading )
        *externalLeading = leading;
}

wxSize wxWindowHaiku::GetDPI() const
{
    const int dpi = wxRound(96 * wxHaikuScaleFactor());
    return wxSize(dpi, dpi);
}

double wxWindowHaiku::GetDPIScaleFactor() const
{
    return wxHaikuScaleFactor();
}

bool wxWindowHaiku::IsTransparentBackgroundSupported(wxString* reason) const
{
    if ( reason )
        *reason = _("Views are opaque in this port.");

    return false;
}

// ----------------------------------------------------------------------------
// tool tips, drag and drop
// ----------------------------------------------------------------------------

#if wxUSE_TOOLTIPS

void wxWindowHaiku::DoSetToolTip(wxToolTip *tip)
{
    wxWindowBase::DoSetToolTip(tip);

    if ( m_tooltip )
        m_tooltip->SetWindow(AsWindow(this));

    if ( m_view )
    {
        wxHaikuViewLock lock(m_view);
        if ( lock.CanTouch() )
        {
            if ( m_tooltip && !m_tooltip->GetTip().empty() )
                m_view->SetToolTip(wxHaikuStr(m_tooltip->GetTip()));
            else
                m_view->SetToolTip((const char *)NULL);
        }
    }
}

#endif // wxUSE_TOOLTIPS

#if wxUSE_DRAG_AND_DROP

void wxWindowHaiku::SetDropTarget(wxDropTarget *dropTarget)
{
    if ( m_dropTarget != dropTarget )
    {
        delete m_dropTarget;
        m_dropTarget = dropTarget;
    }
}

void wxWindowHaiku::DragAcceptFiles(bool accept)
{
    // files dropped on a window without a drop target are reported with
    // wxDropFilesEvent if it asked for them
    m_acceptsFiles = accept;
}

#endif // wxUSE_DRAG_AND_DROP

// ----------------------------------------------------------------------------
// painting
// ----------------------------------------------------------------------------

void wxWindowHaiku::HaikuInvalidate(const wxRect& rectOrig)
{
    wxRect rect(rectOrig);
    rect.Intersect(wxRect(m_rect.GetSize()));
    if ( rect.IsEmpty() )
        return;

    m_invalid.Union(rect);

    if ( !m_inDirtyList && !IsFrozen() )
    {
        m_inDirtyList = true;
        gs_dirtyWindows.push_back(this);
    }
}

void wxWindowHaiku::HaikuInvalidateAll()
{
    HaikuInvalidate(wxRect(m_rect.GetSize()));

    for ( wxWindowList::compatibility_iterator node = GetChildren().GetFirst();
          node;
          node = node->GetNext() )
    {
        wxWindow * const child = node->GetData();
        if ( !child->IsTopLevel() && child->IsShown() )
            child->HaikuInvalidateAll();
    }
}

void wxWindowHaiku::Refresh(bool WXUNUSED(eraseBackground), const wxRect *rect)
{
    if ( !m_view || !IsShown() )
        return;

    if ( rect )
        HaikuInvalidate(*rect);
    else
        HaikuInvalidate(wxRect(m_rect.GetSize()));
}

void wxWindowHaiku::Update()
{
    if ( IsFrozen() )
        return;

    HaikuPaint();

    for ( wxWindowList::compatibility_iterator node = GetChildren().GetFirst();
          node;
          node = node->GetNext() )
    {
        wxWindow * const child = node->GetData();
        if ( !child->IsTopLevel() && child->IsShown() )
            child->Update();
    }
}

bool wxWindowHaiku::HaikuPaint()
{
    m_inDirtyList = false;

    if ( m_invalid.IsEmpty() )
        return false;

    if ( IsFrozen() )
    {
        // painted when thawed
        return false;
    }

    if ( m_paintSession )
    {
        // invalidated while being painted: next round
        m_inDirtyList = true;
        gs_dirtyWindows.push_back(this);
        return false;
    }

    wxRegion region(m_invalid);
    m_invalid.Clear();

    if ( !m_view || !IsShownOnScreen() )
        return false;

    const wxRect rectWindow(m_rect.GetSize());
    region.Intersect(rectWindow);
    if ( region.IsEmpty() )
        return false;

    const wxPoint clientOrigin = GetClientAreaOrigin();
    wxRect clientRect(clientOrigin, GetClientSize());

    // wx code sees the update region in client coordinates
    m_updateRegion = region;
    m_updateRegion.Offset(-clientOrigin.x, -clientOrigin.y);

    const wxUint32 handle = m_handle;
    const wxRect box = region.GetBox();

    if ( HaikuHasNativeContent() )
    {
        // the view draws itself (OpenGL): the paint event only tells the
        // application to render
        wxPaintEvent eventPaint(this);
        HandleWindowEvent(eventPaint);
    }
    else
    {
        wxHaikuPaintSession session(this, region);
        m_paintSession = &session;

        // The background first, on the whole window: wxUniv draws the
        // border over it then.
        if ( GetBackgroundStyle() != wxBG_STYLE_PAINT
                && GetBackgroundStyle() != wxBG_STYLE_TRANSPARENT )
        {
            wxWindowDC dc(AsWindow(this));
            wxEraseEvent eventErase(GetId(), &dc);
            eventErase.SetEventObject(this);
            HandleWindowEvent(eventErase);
        }

        if ( HaikuFindByHandle(handle) == this && !clientRect.Contains(box) )
        {
            wxNcPaintEvent eventNc(this);
            HandleWindowEvent(eventNc);
        }

        if ( HaikuFindByHandle(handle) == this && box.Intersects(clientRect) )
        {
            wxPaintEvent eventPaint(this);
            HandleWindowEvent(eventPaint);
        }

        if ( HaikuFindByHandle(handle) != this )
        {
            // destroyed by one of the handlers (the session only uses the
            // view, which is gone: it must not show anything)
            return true;
        }

        m_paintSession = NULL;
    }

    if ( HaikuFindByHandle(handle) == this )
        m_updateRegion.Clear();

    return true;
}

void wxWindowHaiku::ScrollWindow(int dx, int dy, const wxRect *rect)
{
    if ( !m_view || (!dx && !dy) )
        return;

    // rect is in client coordinates
    const wxPoint origin = GetClientAreaOrigin();
    wxRect area = rect ? *rect : wxRect(GetClientSize());
    area.Offset(origin);
    area.Intersect(wxRect(m_rect.GetSize()));

    // what is waiting to be painted moves too
    if ( !m_invalid.IsEmpty() )
    {
        wxRegion moved(m_invalid);
        moved.Intersect(area);
        if ( !moved.IsEmpty() )
        {
            m_invalid.Subtract(area);
            moved.Offset(dx, dy);
            moved.Intersect(area);
            m_invalid.Union(moved);
        }
    }

    wxRect src(area), dst(area);
    dst.Offset(dx, dy);
    dst.Intersect(area);
    src = dst;
    src.Offset(-dx, -dy);

    if ( !dst.IsEmpty() && IsShownOnScreen() )
    {
        wxHaikuViewLock lock(m_view);
        if ( lock.IsLocked() )
        {
            // the parts of the source which are not on screen are invalidated
            // by app_server and come back as Draw()
            m_view->CopyBits(wxHaikuRect(src), wxHaikuRect(dst));
        }
    }

    // the uncovered area
    if ( dst.IsEmpty() )
    {
        HaikuInvalidate(area);
    }
    else
    {
        if ( dx > 0 )
            HaikuInvalidate(wxRect(area.x, area.y, dx, area.height));
        else if ( dx < 0 )
            HaikuInvalidate(wxRect(area.GetRight() + 1 + dx, area.y,
                                   -dx, area.height));

        if ( dy > 0 )
            HaikuInvalidate(wxRect(area.x, area.y, area.width, dy));
        else if ( dy < 0 )
            HaikuInvalidate(wxRect(area.x, area.GetBottom() + 1 + dy,
                                   area.width, -dy));
    }

    // scroll the children too, except for the scrollbars of the window
    // itself which are outside of its client area
    for ( wxWindowList::compatibility_iterator node = GetChildren().GetFirst();
          node;
          node = node->GetNext() )
    {
        wxWindow * const child = node->GetData();
        if ( child->IsTopLevel() || child->CanBeOutsideClientArea() )
            continue;

        if ( rect && !rect->Intersects(child->GetRect()) )
            continue;

        child->Move(child->GetPosition() + wxPoint(dx, dy),
                    wxSIZE_ALLOW_MINUS_ONE);
    }
}

void wxWindowHaiku::OnInternalIdle()
{
    wxWindowBase::OnInternalIdle();
}

// ----------------------------------------------------------------------------
// event records
// ----------------------------------------------------------------------------

void wxWindowHaiku::HaikuHandleEvent(const wxHaikuEvent& event)
{
    switch ( event.type )
    {
        case wxHAIKU_EVT_DRAW:
            HaikuInvalidate(wxFromHaikuRect(event.rect));
            break;

        case wxHAIKU_EVT_MOUSE_DOWN:
        case wxHAIKU_EVT_MOUSE_UP:
        case wxHAIKU_EVT_MOUSE_MOVED:
            HandleMouseEvent(this, event);
            break;

        case wxHAIKU_EVT_MOUSE_WHEEL:
            HandleWheelEvent(this, event);
            break;

#if wxUSE_DRAG_AND_DROP
        case wxHAIKU_EVT_DROP:
            HandleDropEvent(this, event);
            break;
#endif // wxUSE_DRAG_AND_DROP

        default:
            break;
    }
}

static void wxHaikuSetTLWRecursively(wxWindowHaiku *win, wxNonOwnedWindow *tlw)
{
    // m_tlw is protected: go through a friend-less accessor trick is not
    // needed, wxNonOwnedWindow is a friend of wxWindowHaiku
    struct Setter : wxWindowHaiku
    {
        static void Set(wxWindowHaiku *w, wxNonOwnedWindow *t)
        {
            static_cast<Setter *>(w)->m_tlw = t;
        }
    };

    Setter::Set(win, tlw);

    for ( wxWindowList::compatibility_iterator
            node = win->GetChildren().GetFirst();
          node;
          node = node->GetNext() )
    {
        wxWindow * const child = node->GetData();
        if ( !child->IsTopLevel() )
            wxHaikuSetTLWRecursively(child, tlw);
    }
}

// ============================================================================
// drag and drop: drops from other applications (Tracker)
// ============================================================================

#if wxUSE_DRAG_AND_DROP

// implemented in dnd.cpp
bool wxHaikuDeliverDrop(wxDropTarget *target, const wxHaikuEvent& event,
                        const wxPoint& pos);

namespace
{

void HandleDropEvent(wxWindowHaiku *source, const wxHaikuEvent& he)
{
    const wxPoint screen((int)he.screenWhere.x, (int)he.screenWhere.y);

    // the window under the drop or the first of its parents which takes drops
    for ( wxWindowHaiku *win = source; win; win = win->GetParent() )
    {
        if ( !win->IsEnabled() )
            return;

        wxDropTarget * const target = win->GetDropTarget();
        if ( target )
        {
            if ( wxHaikuDeliverDrop(target, he, ClientPosFromScreen(win, screen)) )
                return;
        }
        else if ( he.value == wxHAIKU_DROP_FILES
                    && win->HaikuAcceptsFiles() )
        {
            const size_t count = he.strings.size();
            wxString * const files = new wxString[count];
            for ( size_t n = 0; n < count; n++ )
                files[n] = wxString::FromUTF8(he.strings[n].c_str());

            wxDropFilesEvent event(wxEVT_DROP_FILES, (int)count, files);
            event.SetEventObject(win);
            event.m_pos = ClientPosFromScreen(win, screen);
            win->HandleWindowEvent(event);
            return;
        }

        if ( win->IsTopLevel() )
            break;
    }
}

} // anonymous namespace

#endif // wxUSE_DRAG_AND_DROP
