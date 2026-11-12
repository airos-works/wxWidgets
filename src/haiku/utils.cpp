/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/utils.cpp
// Purpose:     miscellaneous utility functions, wxDisplay on BScreen
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/utils.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/window.h"
    #include "wx/toplevel.h"
#endif

#include "wx/apptrait.h"
#include "wx/display.h"
#include "wx/private/display.h"
#include "wx/private/launchbrowser.h"

#include "wx/haiku/private.h"

#include <Beep.h>
#include <Deskbar.h>
#include <InterfaceDefs.h>
#include <Roster.h>
#include <Screen.h>

// ----------------------------------------------------------------------------
// toolkit info
// ----------------------------------------------------------------------------

wxPortId wxGUIAppTraits::GetToolkitVersion(int *verMaj,
                                           int *verMin,
                                           int *verMicro) const
{
    // the version of the system is the version of the toolkit
    if ( verMaj )
        *verMaj = 1;
    if ( verMin )
        *verMin = 0;
    if ( verMicro )
        *verMicro = 0;

    return wxPORT_HAIKU;
}

// ----------------------------------------------------------------------------
// display
// ----------------------------------------------------------------------------

namespace
{

class wxDisplayImplHaiku : public wxDisplayImpl
{
public:
    wxDisplayImplHaiku(unsigned n) : wxDisplayImpl(n) { }

    virtual wxRect GetGeometry() const wxOVERRIDE
    {
        BScreen screen(B_MAIN_SCREEN_ID);
        return wxFromHaikuRect(screen.Frame());
    }

    virtual wxRect GetClientArea() const wxOVERRIDE
    {
        wxRect rect = GetGeometry();

        // the Deskbar takes the edge of the screen it is at, unless it hides
        // itself or is the small vertical one in a corner
        BDeskbar deskbar;
        if ( deskbar.IsRunning() && !deskbar.IsAutoHide() )
        {
            const wxRect bar = wxFromHaikuRect(deskbar.Frame());

            switch ( deskbar.Location() )
            {
                case B_DESKBAR_TOP:
                    rect.SetTop(bar.GetBottom() + 1);
                    break;

                case B_DESKBAR_BOTTOM:
                    rect.SetBottom(bar.GetTop() - 1);
                    break;

                case B_DESKBAR_LEFT_TOP:
                case B_DESKBAR_LEFT_BOTTOM:
                    if ( deskbar.IsExpanded() )
                        rect.SetLeft(bar.GetRight() + 1);
                    break;

                case B_DESKBAR_RIGHT_TOP:
                case B_DESKBAR_RIGHT_BOTTOM:
                    if ( deskbar.IsExpanded() )
                        rect.SetRight(bar.GetLeft() - 1);
                    break;
            }
        }

        return rect;
    }

    virtual int GetDepth() const wxOVERRIDE
    {
        return 32;
    }

    virtual wxSize GetPPI() const wxOVERRIDE
    {
        // derived from the scale of the system fonts, as everything else
        const int dpi = wxRound(96 * wxHaikuScaleFactor());
        return wxSize(dpi, dpi);
    }

    virtual wxString GetName() const wxOVERRIDE
    {
        return wxString();
    }

    virtual bool IsPrimary() const wxOVERRIDE
    {
        return true;
    }

#if wxUSE_DISPLAY
    virtual wxArrayVideoModes GetModes(const wxVideoMode& WXUNUSED(mode)) const wxOVERRIDE
    {
        wxArrayVideoModes modes;
        modes.Add(GetCurrentMode());
        return modes;
    }

    virtual wxVideoMode GetCurrentMode() const wxOVERRIDE
    {
        const wxRect r = GetGeometry();
        return wxVideoMode(r.width, r.height, 32);
    }

    virtual bool ChangeMode(const wxVideoMode& WXUNUSED(mode)) wxOVERRIDE
    {
        return false;
    }
#endif // wxUSE_DISPLAY
};

class wxDisplayFactoryHaiku : public wxDisplayFactory
{
public:
    virtual wxDisplayImpl *CreateDisplay(unsigned n) wxOVERRIDE
    {
        return new wxDisplayImplHaiku(n);
    }

    virtual unsigned GetCount() wxOVERRIDE
    {
        // app_server drives one screen
        return 1;
    }

    virtual int GetFromPoint(const wxPoint& WXUNUSED(pt)) wxOVERRIDE
    {
        return 0;
    }
};

} // anonymous namespace

/* static */
wxDisplayFactory *wxDisplay::CreateFactory()
{
    return new wxDisplayFactoryHaiku;
}

// ----------------------------------------------------------------------------
// mouse and keyboard state
// ----------------------------------------------------------------------------

void wxGetMousePosition(int *x, int *y)
{
    BPoint where;
    uint32 buttons;
    if ( get_mouse(&where, &buttons) != B_OK )
    {
        const wxPoint last = wxHaikuCurrentMousePosition();
        where.Set(last.x, last.y);
    }

    if ( x )
        *x = (int)where.x;
    if ( y )
        *y = (int)where.y;
}

wxPoint wxGetMousePosition()
{
    wxPoint pt;
    wxGetMousePosition(&pt.x, &pt.y);
    return pt;
}

wxMouseState wxGetMouseState()
{
    wxMouseState state;

    BPoint where;
    uint32 buttons = 0;
    if ( get_mouse(&where, &buttons) == B_OK )
    {
        state.SetX((int)where.x);
        state.SetY((int)where.y);
    }

    state.SetLeftDown((buttons & B_PRIMARY_MOUSE_BUTTON) != 0);
    state.SetRightDown((buttons & B_SECONDARY_MOUSE_BUTTON) != 0);
    state.SetMiddleDown((buttons & B_TERTIARY_MOUSE_BUTTON) != 0);

    const int mods = wxHaikuModifiers(modifiers());
    state.SetControlDown((mods & wxMOD_CONTROL) != 0);
    state.SetShiftDown((mods & wxMOD_SHIFT) != 0);
    state.SetAltDown((mods & wxMOD_ALT) != 0);
    state.SetMetaDown((mods & wxMOD_META) != 0);

    return state;
}

bool wxGetKeyState(wxKeyCode key)
{
    wxASSERT_MSG( key != WXK_LBUTTON && key != WXK_RBUTTON && key != WXK_MBUTTON,
                  wxS("can't use wxGetKeyState() for mouse buttons") );

    const uint32 mods = modifiers();

    switch ( key )
    {
        case WXK_SHIFT:
            return (mods & B_SHIFT_KEY) != 0;

        case WXK_CONTROL:
            return (mods & B_COMMAND_KEY) != 0;

        case WXK_ALT:
            return (mods & B_CONTROL_KEY) != 0;

        case WXK_WINDOWS_LEFT:
        case WXK_WINDOWS_RIGHT:
            return (mods & B_OPTION_KEY) != 0;

        case WXK_CAPITAL:
            return (mods & B_CAPS_LOCK) != 0;

        case WXK_NUMLOCK:
            return (mods & B_NUM_LOCK) != 0;

        case WXK_SCROLL:
            return (mods & B_SCROLL_LOCK) != 0;

        default:
            break;
    }

    // the keys with a fixed code in the key map
    int code;
    switch ( key )
    {
        case WXK_ESCAPE: code = 0x01; break;
        case WXK_BACK:   code = 0x1e; break;
        case WXK_TAB:    code = 0x26; break;
        case WXK_RETURN: code = 0x47; break;
        case WXK_SPACE:  code = 0x5e; break;
        case WXK_UP:     code = 0x57; break;
        case WXK_LEFT:   code = 0x61; break;
        case WXK_DOWN:   code = 0x62; break;
        case WXK_RIGHT:  code = 0x63; break;
        case WXK_INSERT: code = 0x1f; break;
        case WXK_HOME:   code = 0x20; break;
        case WXK_PAGEUP: code = 0x21; break;
        case WXK_DELETE: code = 0x34; break;
        case WXK_END:    code = 0x35; break;
        case WXK_PAGEDOWN: code = 0x36; break;
        default:
            if ( key >= WXK_F1 && key <= WXK_F12 )
                code = 0x02 + (key - WXK_F1);
            else
                return false;
    }

    key_info info;
    if ( get_key_info(&info) != B_OK )
        return false;

    return (info.key_states[code >> 3] & (1 << (7 - (code & 7)))) != 0;
}

// ----------------------------------------------------------------------------
// misc
// ----------------------------------------------------------------------------

void wxBell()
{
    beep();
}

wxWindow *wxFindWindowAtPointer(wxPoint& pt)
{
    pt = wxGetMousePosition();
    return wxFindWindowAtPoint(pt);
}

wxWindow *wxFindWindowAtPoint(const wxPoint& pt)
{
    return wxGenericFindWindowAtPoint(pt);
}

bool wxDoLaunchDefaultBrowser(const wxLaunchBrowserParams& params)
{
    // the roster finds the application handling the URL scheme or the file
    const wxScopedCharBuffer url = params.url.utf8_str();
    const char *args[] = { url.data(), NULL };

    wxString scheme = params.scheme;
    if ( scheme.empty() )
        scheme = "http";

    const wxString mime = "application/x-vnd.Be.URL." + scheme;
    if ( be_roster->Launch(mime.utf8_str(), 1, args) == B_OK )
        return true;

    return be_roster->Launch("text/html", 1, args) == B_OK;
}
