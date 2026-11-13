/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/nonownedwnd.cpp
// Purpose:     wxNonOwnedWindow: a window with its own BWindow; keyboard
//              input translation
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/toplevel.h"
    #include "wx/app.h"
    #include "wx/log.h"
    #include "wx/dialog.h"
#endif

#include "wx/nonownedwnd.h"
#include "wx/popupwin.h"
#include "wx/evtloop.h"

#include "wx/univ/renderer.h"

#include "wx/haiku/private.h"

#include <InterfaceDefs.h>
#include <Messenger.h>
#include <Screen.h>

#include <algorithm>
#include <map>
#include <vector>

#include <stdlib.h>

// implemented in window.cpp
void wxHaikuKillFocus();
wxWindowHaiku *wxHaikuGetFocusWindow();
void wxHaikuWindowRealized(wxNonOwnedWindow *tlw);

namespace
{

// position of the next window created without one
int gs_cascade = 0;

// ----------------------------------------------------------------------------
// decorations
// ----------------------------------------------------------------------------

struct DecorationInsets
{
    int left, top, right, bottom;
};

// The decorator draws the same border on the left, right and bottom, the rest
// of the height is the tab. Only the sizes are compared: the position BWindow
// knows may lag behind the one app_server has. The window must be locked.
bool ComputeDecorationInsets(BWindow *window, DecorationInsets *insets)
{
    insets->left = insets->top = insets->right = insets->bottom = 0;

    if ( window->Look() == B_NO_BORDER_WINDOW_LOOK )
        return true;

    const BRect frame = window->Frame();
    const BRect deco = window->DecoratorFrame();
    if ( !deco.IsValid() )
        return false;

    const int extraW = (int)(deco.Width() - frame.Width()),
              extraH = (int)(deco.Height() - frame.Height());
    if ( extraW < 0 || extraH < 0 || extraW > 200 || extraH > 400 )
        return false;

    insets->left = extraW / 2;
    insets->right = extraW - insets->left;
    insets->bottom = wxMin(insets->right, extraH);
    insets->top = extraH - insets->bottom;
    return true;
}

// The decorations of the windows of a kind are all the same: they are known
// before the window of a wxNonOwnedWindow exists. The flags may change the
// border (B_NOT_RESIZABLE), the feel does not.
typedef std::map<std::pair<int, wxUint32>, DecorationInsets> InsetsCache;
InsetsCache gs_insetsCache;

const wxUint32 INSETS_FLAGS = B_NOT_RESIZABLE | B_NOT_H_RESIZABLE
                                | B_NOT_V_RESIZABLE | B_NOT_CLOSABLE
                                | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE;

DecorationInsets GetDecorationInsets(int look, wxUint32 flags)
{
    DecorationInsets insets = { 0, 0, 0, 0 };
    if ( look == B_NO_BORDER_WINDOW_LOOK )
        return insets;

    const std::pair<int, wxUint32> key(look, flags & INSETS_FLAGS);
    const InsetsCache::const_iterator it = gs_insetsCache.find(key);
    if ( it != gs_insetsCache.end() )
        return it->second;

    // Asked once for every kind of window: a window which is never shown nor
    // run. A new looper is locked by the thread which created it, Quit()
    // deletes it at once as it never ran.
    BWindow * const probe = new BWindow(BRect(0, 0, 199, 99), "",
                                        (window_look)look,
                                        B_NORMAL_WINDOW_FEEL,
                                        flags & INSETS_FLAGS);
    if ( ComputeDecorationInsets(probe, &insets) )
        gs_insetsCache[key] = insets;
    probe->Quit();

    return insets;
}

void RememberDecorationInsets(int look, wxUint32 flags,
                              const DecorationInsets& insets)
{
    gs_insetsCache[std::make_pair(look, flags & INSETS_FLAGS)] = insets;
}

// ----------------------------------------------------------------------------
// windows which may give their BWindow back
// ----------------------------------------------------------------------------

// the handles of the hidden windows which still have their BWindow
std::vector<wxUint32> gs_hiddenRealized;

// how long a hidden window keeps its BWindow (WXHAIKU_HIDDEN_WINDOW_MS in
// the environment overrides it, for tests)
long HiddenWindowDelay()
{
    static long s_delay = -1;
    if ( s_delay < 0 )
    {
        const char * const env = getenv("WXHAIKU_HIDDEN_WINDOW_MS");
        s_delay = env ? atol(env) : 30000;
        if ( s_delay < 0 )
            s_delay = 30000;
    }

    return s_delay;
}

bool HasNativeContent(const wxWindow *win)
{
    if ( win->HaikuHasNativeContent() )
        return true;

    for ( wxWindowList::compatibility_iterator
            node = win->GetChildren().GetFirst();
          node;
          node = node->GetNext() )
    {
        const wxWindow * const child = node->GetData();
        if ( !child->IsTopLevel() && HasNativeContent(child) )
            return true;
    }

    return false;
}

// ----------------------------------------------------------------------------
// key translation
// ----------------------------------------------------------------------------

// key codes of the numeric keypad (see the keymap documentation)
enum
{
    KEY_NUMPAD_DIVIDE = 0x23,
    KEY_NUMPAD_MULTIPLY = 0x24,
    KEY_NUMPAD_SUBTRACT = 0x25,
    KEY_NUMPAD_7 = 0x37,
    KEY_NUMPAD_8 = 0x38,
    KEY_NUMPAD_9 = 0x39,
    KEY_NUMPAD_ADD = 0x3a,
    KEY_NUMPAD_4 = 0x48,
    KEY_NUMPAD_5 = 0x49,
    KEY_NUMPAD_6 = 0x4a,
    KEY_NUMPAD_1 = 0x58,
    KEY_NUMPAD_2 = 0x59,
    KEY_NUMPAD_3 = 0x5a,
    KEY_NUMPAD_ENTER = 0x5b,
    KEY_NUMPAD_0 = 0x64,
    KEY_NUMPAD_DECIMAL = 0x65,
    KEY_MENU = 0x68
};

// decodes the first UTF-8 character of the string
wxUint32 DecodeUTF8(const char *s, size_t len)
{
    if ( !len )
        return 0;

    const unsigned char c = (unsigned char)s[0];
    if ( c < 0x80 )
        return c;

    int extra;
    wxUint32 code;
    if ( (c & 0xe0) == 0xc0 )
    {
        extra = 1;
        code = c & 0x1f;
    }
    else if ( (c & 0xf0) == 0xe0 )
    {
        extra = 2;
        code = c & 0x0f;
    }
    else if ( (c & 0xf8) == 0xf0 )
    {
        extra = 3;
        code = c & 0x07;
    }
    else
    {
        return 0;
    }

    if ( len < (size_t)extra + 1 )
        return 0;

    for ( int n = 1; n <= extra; n++ )
        code = (code << 6) | ((unsigned char)s[n] & 0x3f);

    return code;
}

// the wx key code of the numeric keypad key or 0
int NumpadKeyCode(const wxHaikuEvent& he)
{
    const bool digit = he.numBytes == 1 && he.bytes[0] >= '0' && he.bytes[0] <= '9';

    switch ( he.key )
    {
        case KEY_NUMPAD_DIVIDE:   return WXK_NUMPAD_DIVIDE;
        case KEY_NUMPAD_MULTIPLY: return WXK_NUMPAD_MULTIPLY;
        case KEY_NUMPAD_SUBTRACT: return WXK_NUMPAD_SUBTRACT;
        case KEY_NUMPAD_ADD:      return WXK_NUMPAD_ADD;
        case KEY_NUMPAD_ENTER:    return WXK_NUMPAD_ENTER;

        case KEY_NUMPAD_0: return digit ? WXK_NUMPAD0 : WXK_NUMPAD_INSERT;
        case KEY_NUMPAD_1: return digit ? WXK_NUMPAD1 : WXK_NUMPAD_END;
        case KEY_NUMPAD_2: return digit ? WXK_NUMPAD2 : WXK_NUMPAD_DOWN;
        case KEY_NUMPAD_3: return digit ? WXK_NUMPAD3 : WXK_NUMPAD_PAGEDOWN;
        case KEY_NUMPAD_4: return digit ? WXK_NUMPAD4 : WXK_NUMPAD_LEFT;
        case KEY_NUMPAD_5: return digit ? WXK_NUMPAD5 : WXK_NUMPAD_BEGIN;
        case KEY_NUMPAD_6: return digit ? WXK_NUMPAD6 : WXK_NUMPAD_RIGHT;
        case KEY_NUMPAD_7: return digit ? WXK_NUMPAD7 : WXK_NUMPAD_HOME;
        case KEY_NUMPAD_8: return digit ? WXK_NUMPAD8 : WXK_NUMPAD_UP;
        case KEY_NUMPAD_9: return digit ? WXK_NUMPAD9 : WXK_NUMPAD_PAGEUP;

        case KEY_NUMPAD_DECIMAL:
            return he.numBytes == 1 && he.bytes[0] == B_DELETE
                    ? WXK_NUMPAD_DELETE : WXK_NUMPAD_DECIMAL;
    }

    return 0;
}

// the wx key code of a key which does not generate a character or 0
int SpecialKeyCode(const wxHaikuEvent& he)
{
    if ( he.key == KEY_MENU )
        return WXK_MENU;

    // raw_char is the character of the key without any modifiers: unlike
    // "bytes" it cannot be confused with the control characters Control
    // makes of the letters
    int32 raw = he.rawChar;
    if ( !raw && he.numBytes == 1 )
        raw = (unsigned char)he.bytes[0];

    switch ( raw )
    {
        case B_BACKSPACE:   return WXK_BACK;
        case B_TAB:         return WXK_TAB;
        case B_ENTER:       return WXK_RETURN;
        case B_ESCAPE:      return WXK_ESCAPE;
        case B_LEFT_ARROW:  return WXK_LEFT;
        case B_RIGHT_ARROW: return WXK_RIGHT;
        case B_UP_ARROW:    return WXK_UP;
        case B_DOWN_ARROW:  return WXK_DOWN;
        case B_INSERT:      return WXK_INSERT;
        case B_DELETE:      return WXK_DELETE;
        case B_HOME:        return WXK_HOME;
        case B_END:         return WXK_END;
        case B_PAGE_UP:     return WXK_PAGEUP;
        case B_PAGE_DOWN:   return WXK_PAGEDOWN;

        case B_FUNCTION_KEY:
            if ( he.key >= B_F1_KEY && he.key <= B_F12_KEY )
                return WXK_F1 + (he.key - B_F1_KEY);

            switch ( he.key )
            {
                case B_PRINT_KEY:  return WXK_PRINT;
                case B_SCROLL_KEY: return WXK_SCROLL;
                case B_PAUSE_KEY:  return WXK_PAUSE;
            }
            break;
    }

    return 0;
}

void FillKeyState(wxKeyEvent& event, int32 modifiers)
{
    const int mods = wxHaikuModifiers(modifiers);
    event.m_controlDown = (mods & wxMOD_CONTROL) != 0;
    event.m_shiftDown = (mods & wxMOD_SHIFT) != 0;
    event.m_altDown = (mods & wxMOD_ALT) != 0;
    event.m_metaDown = (mods & wxMOD_META) != 0;
}

} // anonymous namespace

bool wxHaikuTranslateKey(const wxHaikuEvent& he, wxKeyEvent& event,
                         wxChar *unicode)
{
    FillKeyState(event, he.modifiers);
    event.SetTimestamp((long)(he.when / 1000));
    event.m_rawCode = he.key;
    event.m_rawFlags = he.modifiers;

    if ( unicode )
        *unicode = 0;

    int code = NumpadKeyCode(he);
    if ( code )
    {
        // the digits and operators of the keypad are characters too
        if ( unicode && he.numBytes == 1
                && (unsigned char)he.bytes[0] >= 0x20
                && he.bytes[0] != B_DELETE )
        {
            *unicode = he.bytes[0];
        }

        event.m_keyCode = code;
        event.m_uniChar = unicode && *unicode ? *unicode : 0;
        return true;
    }

    code = SpecialKeyCode(he);
    if ( code )
    {
        event.m_keyCode = code;
        // the control characters wx knows as characters
        switch ( code )
        {
            case WXK_BACK:
            case WXK_TAB:
            case WXK_RETURN:
            case WXK_ESCAPE:
            case WXK_DELETE:
                event.m_uniChar = code;
                break;

            default:
                event.m_uniChar = 0;
        }
        return true;
    }

    // a key generating a character: the key code is the unmodified
    // character of the key, upper case for the letters
    wxUint32 raw = he.rawChar;
    if ( raw >= 0x80 )
    {
        // raw_char is a UTF-8 lead byte for the keys of non Latin layouts
        raw = 0;
    }

    const wxUint32 ch = DecodeUTF8(he.bytes, he.numBytes);

    if ( raw >= 'a' && raw <= 'z' )
        code = raw - 'a' + 'A';
    else if ( raw >= 0x20 && raw < 0x7f )
        code = raw;
    else if ( ch >= 0x20 && ch < 0x100 && ch != 0x7f )
        code = ch;
    else
        code = WXK_NONE;

    if ( code == WXK_NONE && ch < 0x20 )
        return false;

    event.m_keyCode = code;
    event.m_uniChar = code != WXK_NONE ? code : ch;

    if ( unicode && ch >= 0x20 && ch != 0x7f )
        *unicode = ch;

    return true;
}

// ============================================================================
// wxNonOwnedWindow
// ============================================================================

wxIMPLEMENT_ABSTRACT_CLASS(wxNonOwnedWindow, wxNonOwnedWindowBase);

void wxNonOwnedWindow::Init()
{
    m_window = NULL;
    m_decoLeft = m_decoTop = m_decoRight = m_decoBottom = 0;
    m_decoValid = false;
    m_lastFocus = NULL;
    m_frameSerial = 0;
    m_isModal = false;
    m_isActive = false;
    m_windowShown = false;
    m_sizeEventSent = false;
    m_shownOnce = false;
    m_autoPosition = false;
    m_hiddenSince = 0;

    m_isShown = false;
}

bool wxNonOwnedWindow::Create(wxWindow *parent,
                              wxWindowID id,
                              const wxPoint& pos,
                              const wxSize& size,
                              long style,
                              const wxString& name)
{
    wxCHECK_MSG( be_app, false, "wxApp must be initialized first" );

    m_tlw = this;

    if ( !CreateBase(parent, id, pos, size, style, wxDefaultValidator, name) )
        return false;

    if ( parent )
        parent->AddChild(this);

    const int w = wxMax(size.x == wxDefaultCoord ? 400 : size.x, 1),
              h = wxMax(size.y == wxDefaultCoord ? 300 : size.y, 1);

    // The root view: the views of the children are added to it. There is no
    // BWindow yet (see HaikuRealize()); once there is, the view follows the
    // window, app_server resizes it together with the window.
    m_view = new wxHaikuView(m_handle, wxHaikuRect(0, 0, w, h), B_FOLLOW_ALL);

    m_content = wxRect(100, 100, w, h);
    m_rect = wxRect(0, 0, w, h);

    HaikuUpdateDecorationInsets();

    wxPoint position(pos);
    if ( position.x == wxDefaultCoord || position.y == wxDefaultCoord )
    {
        m_autoPosition = true;

        // cascade the windows which don't care where they are
        const int offset = 40 + (gs_cascade++ % 10) * 24;
        if ( position.x == wxDefaultCoord )
            position.x = offset;
        if ( position.y == wxDefaultCoord )
            position.y = offset + 20;
    }

    HaikuSetContentRect(wxRect(position.x + m_decoLeft, position.y + m_decoTop,
                               wxMax(w - m_decoLeft - m_decoRight, 1),
                               wxMax(h - m_decoTop - m_decoBottom, 1)));

    return true;
}

wxNonOwnedWindow::~wxNonOwnedWindow()
{
    SendDestroyEvent();

    // children first: their views are removed from the window
    DestroyChildren();

    if ( m_window )
    {
        // Quit() needs the lock and waits for the looper thread to end; the
        // looper never waits for this thread, so this cannot deadlock. The
        // root view is deleted with the window.
        if ( m_window->Lock() )
            m_window->Quit();

        m_window = NULL;
        m_view = NULL;
    }
    else
    {
        // never shown, or its window was given back: the root view is ours
        delete m_view;
        m_view = NULL;
    }
}

// ----------------------------------------------------------------------------
// window type
// ----------------------------------------------------------------------------

void wxNonOwnedWindow::HaikuGetWindowType(int *look, int *feel,
                                          wxUint32 *flags) const
{
    *look = B_NO_BORDER_WINDOW_LOOK;
    *feel = B_NORMAL_WINDOW_FEEL;
    // app_server activates the window on a click, see toplevel.cpp
    *flags = B_ASYNCHRONOUS_CONTROLS;
}

void wxNonOwnedWindow::HaikuUpdateWindowType()
{
    if ( !m_window )
    {
        // only the decorations matter until there is a window: keep the
        // outer rectangle if they change
        const int left = m_decoLeft, top = m_decoTop,
                  right = m_decoRight, bottom = m_decoBottom;
        HaikuUpdateDecorationInsets();
        if ( left != m_decoLeft || top != m_decoTop
                || right != m_decoRight || bottom != m_decoBottom )
        {
            const wxRect outer(m_content.x - left, m_content.y - top,
                               m_content.width + left + right,
                               m_content.height + top + bottom);
            HaikuSetContentRect(wxRect(outer.x + m_decoLeft, outer.y + m_decoTop,
                                       wxMax(outer.width - m_decoLeft - m_decoRight, 1),
                                       wxMax(outer.height - m_decoTop - m_decoBottom, 1)));
        }
        return;
    }

    int look, feel;
    wxUint32 flags;
    HaikuGetWindowType(&look, &feel, &flags);

    if ( m_isModal )
        feel = B_MODAL_APP_WINDOW_FEEL;

    bool lookChanged = false;

    {
        wxHaikuWindowLock lock(m_window);
        if ( !lock.IsLocked() )
            return;

        if ( m_window->Look() != (window_look)look )
        {
            m_window->SetLook((window_look)look);
            lookChanged = true;
        }

        if ( m_window->Feel() != (window_feel)feel )
            m_window->SetFeel((window_feel)feel);

        if ( m_window->Flags() != flags )
            m_window->SetFlags(flags);
    }

    if ( lookChanged )
    {
        // the decorations changed: keep the outer rectangle
        const wxRect outer(m_content.x - m_decoLeft, m_content.y - m_decoTop,
                           m_content.width + m_decoLeft + m_decoRight,
                           m_content.height + m_decoTop + m_decoBottom);

        HaikuUpdateDecorationInsets();

        HaikuSetContentRect(wxRect(outer.x + m_decoLeft, outer.y + m_decoTop,
                                   wxMax(outer.width - m_decoLeft - m_decoRight, 1),
                                   wxMax(outer.height - m_decoTop - m_decoBottom, 1)));
    }
}

void wxNonOwnedWindow::HaikuUpdateDecorationInsets()
{
    m_decoValid = true;

    int look, feel;
    wxUint32 flags;
    HaikuGetWindowType(&look, &feel, &flags);

    DecorationInsets insets = { 0, 0, 0, 0 };
    if ( m_window )
    {
        wxHaikuWindowLock lock(m_window);
        if ( lock.IsLocked() && ComputeDecorationInsets(m_window, &insets) )
            RememberDecorationInsets(m_window->Look(), m_window->Flags(), insets);
    }
    else
    {
        insets = GetDecorationInsets(look, flags);
    }

    m_decoLeft = insets.left;
    m_decoTop = insets.top;
    m_decoRight = insets.right;
    m_decoBottom = insets.bottom;
}

void wxNonOwnedWindow::HaikuGetDecorationInsets(int *left, int *top,
                                                int *right, int *bottom) const
{
    if ( left )
        *left = m_decoLeft;
    if ( top )
        *top = m_decoTop;
    if ( right )
        *right = m_decoRight;
    if ( bottom )
        *bottom = m_decoBottom;
}

void wxNonOwnedWindow::HaikuSetModal(bool modal)
{
    if ( modal == m_isModal )
        return;

    m_isModal = modal;
    HaikuUpdateWindowType();

    // A dialog is shown before it becomes modal (wxWindowDisabler comes
    // after Show() in ShowModal()): while another modal dialog runs it
    // appeared behind that one, which had the modal feel then. It is the
    // window the user deals with now: in front of the others.
    if ( modal && m_window && IsShown() )
    {
        wxHaikuWindowLock lock(m_window);
        if ( lock.IsLocked() )
            m_window->Activate(true);
    }
}

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

wxPoint wxNonOwnedWindow::HaikuGetContentOrigin() const
{
    return m_content.GetPosition();
}

void wxNonOwnedWindow::DoGetPosition(int *x, int *y) const
{
    if ( x )
        *x = m_content.x - m_decoLeft;
    if ( y )
        *y = m_content.y - m_decoTop;
}

void wxNonOwnedWindow::DoGetSize(int *width, int *height) const
{
    if ( width )
        *width = m_content.width + m_decoLeft + m_decoRight;
    if ( height )
        *height = m_content.height + m_decoTop + m_decoBottom;
}

// The border wxUniversal draws inside the window (a popup menu, a popup with
// wxBORDER_SIMPLE...): the client area is what is inside it, as
// wxWindow::GetClientAreaOrigin() has it.
static wxRect wxHaikuUnivBorder(const wxNonOwnedWindow *win)
{
    wxRenderer * const renderer = win->GetRenderer();
    return renderer ? renderer->GetBorderDimensions(win->GetBorder())
                    : wxRect();
}

void wxNonOwnedWindow::DoGetClientSize(int *width, int *height) const
{
    const wxRect border = wxHaikuUnivBorder(this);

    if ( width )
        *width = wxMax(m_content.width - border.x - border.width, 0);
    if ( height )
        *height = wxMax(m_content.height - border.y - border.height, 0);
}

void wxNonOwnedWindow::DoSetClientSize(int width, int height)
{
    const wxRect border = wxHaikuUnivBorder(this);

    HaikuSetContentRect(wxRect(m_content.x, m_content.y,
                               wxMax(width + border.x + border.width, 1),
                               wxMax(height + border.y + border.height, 1)));
}

void wxNonOwnedWindow::DoSetSize(int x, int y, int width, int height,
                                 int sizeFlags)
{
    int curX, curY, curW, curH;
    DoGetPosition(&curX, &curY);
    DoGetSize(&curW, &curH);

    if ( x == wxDefaultCoord && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE) )
        x = curX;
    if ( y == wxDefaultCoord && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE) )
        y = curY;

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
            width = curW;
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
            height = curH;
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

    if ( x == curX && y == curY && width == curW && height == curH )
        return;

    if ( x != curX || y != curY )
        m_autoPosition = false;

    DoMoveWindow(x, y, width, height);
}

void wxNonOwnedWindow::DoMoveWindow(int x, int y, int width, int height)
{
    // the rectangle includes the decorations
    HaikuSetContentRect(wxRect(x + m_decoLeft, y + m_decoTop,
                               wxMax(width - m_decoLeft - m_decoRight, 1),
                               wxMax(height - m_decoTop - m_decoBottom, 1)));
}

void wxNonOwnedWindow::HaikuSetContentRect(const wxRect& content)
{
    if ( !m_window )
    {
        // no window yet: the geometry is ours alone, the root view (not
        // attached, so not locked) gets the size the window will have
        if ( m_view )
            m_view->ResizeTo(wxMax(content.width, 1) - 1,
                             wxMax(content.height, 1) - 1);

        wxHAIKU_TRACE("set frame tlw=%u (no window) %d,%d %dx%d",
                      (unsigned)m_handle, content.x, content.y,
                      content.width, content.height);

        HaikuFrameChanged(content);
        return;
    }

    // The looper thread moves and resizes the window (see
    // wxHAIKU_MSG_SET_FRAME): this thread waits for it, which is safe as the
    // looper never waits for this thread, but it must not hold the lock of
    // the window meanwhile.
    wxASSERT_MSG( !m_window->IsLocked(),
                  wxS("window must not be locked when its frame is set") );

    wxRect actual(content);

    BMessage request(wxHAIKU_MSG_SET_FRAME);
    request.AddRect("frame", wxHaikuRect(content));

    BMessage reply;
    BRect frame;
    int32 serial;
    if ( BMessenger(m_window).SendMessage(&request, &reply, 2000000, 2000000) == B_OK
            && reply.FindRect("frame", &frame) == B_OK
            && reply.FindInt32("serial", &serial) == B_OK )
    {
        actual = wxFromHaikuRect(frame);
        m_frameSerial = serial;
    }

    wxHAIKU_TRACE("set frame tlw=%u want=%d,%d %dx%d got=%d,%d %dx%d serial=%d",
                  (unsigned)m_handle, content.x, content.y,
                  content.width, content.height,
                  actual.x, actual.y, actual.width, actual.height,
                  (int)m_frameSerial);

    HaikuFrameChanged(actual);
}

void wxNonOwnedWindow::HaikuFrameChanged(const wxRect& content)
{
    const bool moved = content.GetPosition() != m_content.GetPosition();
    const bool resized = content.GetSize() != m_content.GetSize();

    if ( !moved && !resized && m_sizeEventSent )
        return;

    m_content = content;
    m_rect = wxRect(0, 0, content.width, content.height);

    const wxUint32 handle = m_handle;

    if ( resized || !m_sizeEventSent )
    {
        m_sizeEventSent = true;

        wxSizeEvent event(GetSize(), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);

        if ( HaikuFindByHandle(handle) != this )
            return;

        HaikuAfterSizeEvent();
    }

    if ( moved )
    {
        wxMoveEvent event(GetPosition(), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }
}

void wxNonOwnedWindow::DoSetSizeHints(int minW, int minH,
                                      int maxW, int maxH,
                                      int incW, int incH)
{
    wxNonOwnedWindowBase::DoSetSizeHints(minW, minH, maxW, maxH, incW, incH);

    HaikuApplySizeLimits();
}

void wxNonOwnedWindow::HaikuApplySizeLimits()
{
    if ( !m_window )
        return;

    wxHaikuWindowLock lock(m_window);
    if ( !lock.IsLocked() )
        return;

    const int minW = GetMinWidth(), minH = GetMinHeight(),
              maxW = GetMaxWidth(), maxH = GetMaxHeight();

    // the limits of the BWindow are those of its content, and a width of n
    // pixels is n - 1 in the Be API
    const int decoW = m_decoLeft + m_decoRight,
              decoH = m_decoTop + m_decoBottom;

    const float minWidth = minW > 0 ? wxMax(minW - decoW, 1) - 1 : 0,
                minHeight = minH > 0 ? wxMax(minH - decoH, 1) - 1 : 0,
                maxWidth = maxW > 0 ? wxMax(maxW - decoW, 1) - 1 : 32767,
                maxHeight = maxH > 0 ? wxMax(maxH - decoH, 1) - 1 : 32767;

    m_window->SetSizeLimits(minWidth, maxWidth, minHeight, maxHeight);
}

// ----------------------------------------------------------------------------
// showing
// ----------------------------------------------------------------------------

bool wxNonOwnedWindow::Show(bool show)
{
    // this is the base class implementation, the window of wxWindowHaiku has
    // no view to show or hide by itself
    if ( !wxWindowBase::Show(show) )
        return false;

    if ( show && !m_shownOnce )
    {
        // A size event before the window appears for the first time: the
        // children created since the window itself got its size are laid
        // out by it.
        m_shownOnce = true;
        m_sizeEventSent = true;

        wxSizeEvent event(GetSize(), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);

        HaikuAfterSizeEvent();
    }

    // The window is created now if it does not exist: after the layout of
    // the first show, which is cheaper without it (the views of the children
    // are moved without talking to app_server).
    if ( show )
        HaikuRealize();

    if ( !show )
    {
        // a hidden window has neither the mouse nor the focus
        wxWindowHaiku * const focus = wxHaikuGetFocusWindow();
        if ( focus && focus->GetTLW() == this )
            wxHaikuKillFocus();

        // it may give its BWindow back if it stays hidden
        if ( m_window && HaikuCanUnrealize() )
        {
            m_hiddenSince = wxGetLocalTimeMillis();
            if ( std::find(gs_hiddenRealized.begin(), gs_hiddenRealized.end(),
                           m_handle) == gs_hiddenRealized.end() )
                gs_hiddenRealized.push_back(m_handle);
        }
    }

    wxHAIKU_TRACE("%s tlw=%u (%s)%s", show ? "show" : "hide", (unsigned)m_handle,
                  (const char *)wxString(GetClassInfo()->GetClassName()).utf8_str(),
                  m_window ? "" : " (no window)");

    if ( m_window )
    {
        wxHaikuWindowLock lock(m_window);
        if ( lock.IsLocked() && show != m_windowShown )
        {
            if ( show )
                m_window->Show();
            else
                m_window->Hide();
            m_windowShown = show;
        }
    }

    // (not with the window locked: handlers may do anything)
    wxShowEvent eventShow(GetId(), show);
    eventShow.SetEventObject(this);
    HandleWindowEvent(eventShow);

    return true;
}

// ----------------------------------------------------------------------------
// the BWindow
// ----------------------------------------------------------------------------

bool wxNonOwnedWindow::HaikuRealize()
{
    if ( m_window )
        return true;

    if ( !m_view || IsBeingDeleted() )
        return false;

    int look, feel;
    wxUint32 flags;
    HaikuGetWindowType(&look, &feel, &flags);
    if ( m_isModal )
        feel = B_MODAL_APP_WINDOW_FEEL;

    // The reports of a window this one had before carry smaller serial
    // numbers: they are ignored.
    m_frameSerial++;

    wxHaikuWindow * const
        window = new wxHaikuWindow(m_handle, wxHaikuRect(m_content), "",
                                   (window_look)look, (window_feel)feel, flags,
                                   m_frameSerial);

    // the root view fills the window and follows it
    m_view->MoveTo(0, 0);
    m_view->ResizeTo(wxMax(m_content.width, 1) - 1,
                     wxMax(m_content.height, 1) - 1);
    window->AddChild(m_view);

    m_window = window;
    m_windowShown = false;

    // A new BWindow is locked by the thread which created it until it runs:
    // what was set while there was no window is applied without waiting.
    HaikuWindowCreated(window);

    window->Run();

    // the decorations as the decorator really draws them
    HaikuUpdateDecorationInsets();

    wxHAIKU_TRACE("realized tlw=%u (%s) %d,%d %dx%d", (unsigned)m_handle,
                  (const char *)wxString(GetClassInfo()->GetClassName()).utf8_str(),
                  m_content.x, m_content.y, m_content.width, m_content.height);

    // capture, focus, cursors of the windows inside
    wxHaikuWindowRealized(this);

    return true;
}

void wxNonOwnedWindow::HaikuWindowCreated(BWindow *WXUNUSED(window))
{
    HaikuApplySizeLimits();
}

bool wxNonOwnedWindow::HaikuUnrealize()
{
    if ( !m_window || IsShown() || m_windowShown || IsBeingDeleted() )
        return false;

    // not while one of its windows has the mouse or keeps a native view of
    // its own (OpenGL, web view) which would have to start over
    wxWindow * const capture = wxWindow::GetCapture();
    if ( capture && capture->GetTLW() == this )
        return false;
    if ( HasNativeContent(this) )
        return false;

    BWindow * const window = m_window;
    m_window = NULL;
    m_isActive = false;

    // The views stay: the root view is taken out of the window first. Quit()
    // waits for the looper thread, which never waits for this one.
    if ( window->Lock() )
    {
        m_view->RemoveSelf();
        window->Quit();
    }

    wxHAIKU_TRACE("unrealized tlw=%u (%s)", (unsigned)m_handle,
                  (const char *)wxString(GetClassInfo()->GetClassName()).utf8_str());

    return true;
}

/* static */
void wxNonOwnedWindow::HaikuReleaseHiddenWindows()
{
    if ( gs_hiddenRealized.empty() )
        return;

    // not more often than every second
    static wxLongLong s_lastRun = 0;
    const wxLongLong now = wxGetLocalTimeMillis();
    if ( now - s_lastRun < 1000 )
        return;
    s_lastRun = now;

    const long delay = HiddenWindowDelay();

    std::vector<wxUint32> keep;
    for ( size_t n = 0; n < gs_hiddenRealized.size(); n++ )
    {
        wxNonOwnedWindow * const win = dynamic_cast<wxNonOwnedWindow *>(
                HaikuFindByHandle(gs_hiddenRealized[n]));
        if ( !win || !win->m_window || win->IsShown() )
            continue;

        if ( now - win->m_hiddenSince < delay )
        {
            keep.push_back(gs_hiddenRealized[n]);
            continue;
        }

        // (if it cannot do without its window now it is not asked again
        // before it is shown and hidden once more)
        win->HaikuUnrealize();
    }

    gs_hiddenRealized.swap(keep);
}

void wxNonOwnedWindow::Raise()
{
    // popups have the feel of menus: they are above everything already and
    // never become the active window
    if ( !m_window || HaikuAvoidsFocus() )
        return;

    wxHaikuWindowLock lock(m_window);
    if ( lock.IsLocked() )
        m_window->Activate(true);
}

void wxNonOwnedWindow::Lower()
{
    if ( !m_window )
        return;

    // behind all other windows
    wxHaikuWindowLock lock(m_window);
    if ( lock.IsLocked() )
        m_window->SendBehind(NULL);
}

static void wxHaikuUpdateModalWindows()
{
    // A shown and enabled top level window while others are disabled is
    // shown modally (wxWindowDisabler disabled the others for it): it gets
    // the modal feel which keeps it above the windows it blocks.
    bool anyDisabled = false;
    for ( wxWindowList::compatibility_iterator
            node = wxTopLevelWindows.GetFirst();
          node;
          node = node->GetNext() )
    {
        wxWindow * const win = node->GetData();
        if ( win->IsShown() && !win->IsThisEnabled() )
        {
            anyDisabled = true;
            break;
        }
    }

    for ( wxWindowList::compatibility_iterator
            node = wxTopLevelWindows.GetFirst();
          node;
          node = node->GetNext() )
    {
        // NB: wxDynamicCast() cannot be used, wxNonOwnedWindow is not in the
        //     wx RTTI chain of wxTopLevelWindow (which
        //     wxIMPLEMENT_ABSTRACT_CLASS derives from wxWindow)
        wxNonOwnedWindow * const
            win = dynamic_cast<wxNonOwnedWindow *>(node->GetData());
        if ( !win )
            continue;

        win->HaikuSetModal(anyDisabled && win->IsShown()
                            && win->IsThisEnabled()
                            && wxDynamicCast(win, wxDialog) != NULL);
    }
}

void wxNonOwnedWindow::DoEnable(bool WXUNUSED(enable))
{
    wxHaikuUpdateModalWindows();
}

// ----------------------------------------------------------------------------
// activation
// ----------------------------------------------------------------------------

void wxNonOwnedWindow::HaikuActivated(bool WXUNUSED(active))
{
}

// ----------------------------------------------------------------------------
// keyboard
// ----------------------------------------------------------------------------

void wxNonOwnedWindow::HaikuHandleKey(const wxHaikuEvent& he)
{
    // the window with the wx focus gets the keys, whatever BWindow they were
    // sent to: the windows of popups never are the active ones
    wxHaikuSetCurrentModifiers(he.modifiers);

    wxWindowHaiku *target = wxHaikuGetFocusWindow();
    if ( !target )
        target = this;

    if ( !target->IsEnabled() )
        return;

    wxWindow * const win = static_cast<wxWindow *>(target);
    const wxUint32 handle = target->GetHaikuHandle();

    wxHAIKU_TRACE("key %s key=0x%x raw=0x%x bytes=%d:0x%02x mods=0x%x "
                  "target=%u (%s)",
                  he.type == wxHAIKU_EVT_KEY_DOWN ? "down" : "up",
                  (unsigned)he.key, (unsigned)he.rawChar, (int)he.numBytes,
                  (unsigned)(unsigned char)he.bytes[0], (unsigned)he.modifiers,
                  (unsigned)handle, (const char *)wxString(win->GetClassInfo()->GetClassName()).utf8_str());

    wxKeyEvent event(he.type == wxHAIKU_EVT_KEY_DOWN ? wxEVT_KEY_DOWN
                                                     : wxEVT_KEY_UP);
    event.SetEventObject(win);
    event.SetId(win->GetId());

    wxChar unicode = 0;
    if ( !wxHaikuTranslateKey(he, event, &unicode) )
        return;

    const wxPoint pos = win->ScreenToClient(wxHaikuCurrentMousePosition());
    event.m_x = pos.x;
    event.m_y = pos.y;

    if ( he.type == wxHAIKU_EVT_KEY_UP )
    {
        win->HandleWindowEvent(event);
        return;
    }

    // wxEVT_CHAR_HOOK first: the dialogs handle Escape and such there
    {
        wxKeyEvent eventHook(wxEVT_CHAR_HOOK, event);
        if ( win->HandleWindowEvent(eventHook)
                && !eventHook.IsNextEventAllowed() )
        {
            return;
        }

        if ( HaikuFindByHandle(handle) != target )
            return;
    }

    // wxUniv looks for the accelerators when the key event is not handled
    if ( win->HandleWindowEvent(event) )
        return;

    if ( HaikuFindByHandle(handle) != target )
        return;

    const int keyDown = event.m_keyCode;

    // wxEVT_CHAR: the character the key generates, or the same code for the
    // keys which don't generate any
    bool sendChar = true;
    wxKeyEvent eventChar(wxEVT_CHAR, event);

    if ( unicode )
    {
        if ( event.m_controlDown || event.m_altDown )
        {
            // not text: Ctrl-letter is the control character as in the
            // other ports
            if ( keyDown >= 'A' && keyDown <= 'Z' && event.m_controlDown
                    && !event.m_altDown )
            {
                eventChar.m_keyCode = keyDown - 'A' + 1;
                eventChar.m_uniChar = eventChar.m_keyCode;
            }
            else
            {
                eventChar.m_keyCode = keyDown;
                eventChar.m_uniChar = keyDown != WXK_NONE ? keyDown : unicode;
            }
        }
        else
        {
            eventChar.m_uniChar = unicode;
            eventChar.m_keyCode = unicode < 0x100 ? (int)unicode : WXK_NONE;

            // Option makes other characters of the keys: it is not a
            // modifier of the character itself
            eventChar.m_metaDown = false;
        }
    }
    else if ( keyDown == WXK_NONE || keyDown == WXK_MENU )
    {
        sendChar = false;
    }
    else if ( keyDown >= 0x20 && keyDown < WXK_START && keyDown != WXK_DELETE )
    {
        // a character key which generated nothing (a dead key)
        sendChar = event.m_controlDown || event.m_altDown;
        if ( keyDown >= 'A' && keyDown <= 'Z' && event.m_controlDown
                && !event.m_altDown )
        {
            eventChar.m_keyCode = keyDown - 'A' + 1;
            eventChar.m_uniChar = eventChar.m_keyCode;
        }
    }

    if ( sendChar && win->HandleWindowEvent(eventChar) )
        return;

    if ( HaikuFindByHandle(handle) != target )
        return;

    // keyboard navigation
    if ( keyDown == WXK_TAB && !event.m_altDown )
    {
        wxWindow * const parent = win->GetParent();
        if ( parent && parent->HasFlag(wxTAB_TRAVERSAL) )
        {
            wxNavigationKeyEvent navEvent;
            navEvent.SetEventObject(parent);
            navEvent.SetDirection(!event.m_shiftDown);
            navEvent.SetWindowChange(event.m_controlDown);
            navEvent.SetCurrentFocus(win);
            if ( parent->HandleWindowEvent(navEvent) )
                return;
        }
        else if ( win->IsTopLevel() )
        {
            win->Navigate(event.m_shiftDown ? wxNavigationKeyEvent::IsBackward
                                            : wxNavigationKeyEvent::IsForward);
            return;
        }
    }

    // the shortcuts every native window has, unless the application uses
    // the keys itself
    if ( event.m_controlDown && !event.m_altDown && !event.m_shiftDown )
    {
        wxTopLevelWindow * const
            tlw = wxDynamicCast(wxGetTopLevelParent(win), wxTopLevelWindow);
        if ( keyDown == 'Q' )
        {
            if ( wxTheApp )
                wxTheApp->HaikuQuitRequested();
        }
        else if ( keyDown == 'W' && tlw && tlw->HasFlag(wxCLOSE_BOX) )
        {
            tlw->Close();
        }
    }
}

void wxNonOwnedWindow::HaikuHandleModifiers(const wxHaikuEvent& he)
{
    // the modifier keys are keys too for wx
    static const struct
    {
        int32 mask;
        int keyCode;
    } modifierKeys[] =
    {
        { B_SHIFT_KEY, WXK_SHIFT },
        { B_COMMAND_KEY, WXK_CONTROL },
        { B_CONTROL_KEY, WXK_ALT },
        { B_OPTION_KEY, WXK_WINDOWS_LEFT },
        { B_CAPS_LOCK, WXK_CAPITAL },
        { B_NUM_LOCK, WXK_NUMLOCK },
        { B_SCROLL_LOCK, WXK_SCROLL },
    };

    wxHaikuSetCurrentModifiers(he.modifiers);

    wxWindowHaiku *target = wxHaikuGetFocusWindow();
    if ( !target )
        target = this;

    if ( !target->IsEnabled() )
        return;

    wxWindow * const win = static_cast<wxWindow *>(target);
    const wxUint32 handle = target->GetHaikuHandle();

    const int32 changed = he.modifiers ^ he.value;
    for ( size_t n = 0; n < WXSIZEOF(modifierKeys); n++ )
    {
        if ( !(changed & modifierKeys[n].mask) )
            continue;

        const bool down = (he.modifiers & modifierKeys[n].mask) != 0;

        wxKeyEvent event(down ? wxEVT_KEY_DOWN : wxEVT_KEY_UP);
        event.SetEventObject(win);
        event.SetId(win->GetId());
        event.SetTimestamp((long)(he.when / 1000));
        FillKeyState(event, he.modifiers);
        event.m_keyCode = modifierKeys[n].keyCode;
        event.m_rawFlags = he.modifiers;

        win->HandleWindowEvent(event);

        if ( HaikuFindByHandle(handle) != target )
            return;
    }
}

// ----------------------------------------------------------------------------
// event records
// ----------------------------------------------------------------------------

void wxNonOwnedWindow::HaikuHandleEvent(const wxHaikuEvent& event)
{
    switch ( event.type )
    {
        case wxHAIKU_EVT_FRAME:
            // reports from before the last change this thread asked for
            // are out of date
            wxHAIKU_TRACE("frame tlw=%u %g,%g %gx%g serial=%d (ours %d)",
                          (unsigned)m_handle, event.rect.left, event.rect.top,
                          event.rect.Width() + 1, event.rect.Height() + 1,
                          (int)event.value, (int)m_frameSerial);

            if ( m_window && event.value >= m_frameSerial )
                HaikuFrameChanged(wxFromHaikuRect(event.rect));
            break;

        case wxHAIKU_EVT_KEY_DOWN:
        case wxHAIKU_EVT_KEY_UP:
            HaikuHandleKey(event);
            break;

        case wxHAIKU_EVT_MODIFIERS:
            HaikuHandleModifiers(event);
            break;

        case wxHAIKU_EVT_ACTIVATE:
            HaikuActivated(event.flag);
            break;

        default:
            wxNonOwnedWindowBase::HaikuHandleEvent(event);
    }
}
