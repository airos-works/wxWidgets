/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/toplevel.cpp
// Purpose:     wxTopLevelWindowHaiku
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/toplevel.h"
    #include "wx/app.h"
    #include "wx/dialog.h"
    #include "wx/frame.h"
#endif

#include "wx/display.h"

#include "wx/haiku/private.h"

#include <Screen.h>

// implemented in window.cpp
void wxHaikuKillFocus();
wxWindowHaiku *wxHaikuGetFocusWindow();

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

void wxTopLevelWindowHaiku::Init()
{
    m_fsIsShowing = false;
    m_fsSaveStyle = 0;
    m_isMaximized = false;
    m_isIconized = false;
    m_closeEnabled = true;
    m_showInactive = false;
}

bool wxTopLevelWindowHaiku::Create(wxWindow *parent,
                                   wxWindowID id,
                                   const wxString& title,
                                   const wxPoint& pos,
                                   const wxSize& sizeOrig,
                                   long style,
                                   const wxString& name)
{
    // always create a frame of some reasonable, even if arbitrary, size
    wxSize size(sizeOrig);
    if ( size.x == wxDefaultCoord || size.y == wxDefaultCoord )
    {
        const wxSize sizeDefault = GetDefaultSize();
        if ( size.x == wxDefaultCoord )
            size.x = sizeDefault.x;
        if ( size.y == wxDefaultCoord )
            size.y = sizeDefault.y;
    }

    if ( !wxNonOwnedWindow::Create(parent, id, pos, size, style, name) )
        return false;

    wxTopLevelWindows.Append(this);

    SetTitle(title);

    return true;
}

wxTopLevelWindowHaiku::~wxTopLevelWindowHaiku()
{
}

void wxTopLevelWindowHaiku::HaikuGetWindowType(int *look, int *feel,
                                               wxUint32 *flags) const
{
    const long style = GetWindowStyleFlag();

    // Not B_WILL_ACCEPT_FIRST_CLICK, as native windows: app_server activates
    // the window when its contents are clicked, before it delivers the click
    // (which it does unless the user turned "accept first click" off).
    *flags = B_ASYNCHRONOUS_CONTROLS;

    if ( m_fsIsShowing )
    {
        *look = B_NO_BORDER_WINDOW_LOOK;
        *flags |= B_NOT_RESIZABLE | B_NOT_MOVABLE;
    }
    else if ( style & wxCAPTION )
    {
        // the tab of the floating look is the small one of tool windows
        *look = (style & wxFRAME_TOOL_WINDOW) ? B_FLOATING_WINDOW_LOOK
                                               : B_TITLED_WINDOW_LOOK;
    }
    else if ( (style & wxRESIZE_BORDER)
                || (style & wxBORDER_MASK) == wxBORDER_SIMPLE
                || (style & wxBORDER_MASK) == wxBORDER_STATIC
                || (style & wxBORDER_MASK) == wxBORDER_RAISED
                || (style & wxBORDER_MASK) == wxBORDER_SUNKEN )
    {
        *look = B_BORDERED_WINDOW_LOOK;
    }
    else
    {
        *look = B_NO_BORDER_WINDOW_LOOK;
    }

    if ( !(style & wxRESIZE_BORDER) )
        *flags |= B_NOT_RESIZABLE;
    if ( !(style & wxMAXIMIZE_BOX) || !(style & wxRESIZE_BORDER) )
        *flags |= B_NOT_ZOOMABLE;
    if ( !(style & wxMINIMIZE_BOX) )
        *flags |= B_NOT_MINIMIZABLE;
    if ( !(style & wxCLOSE_BOX) || !m_closeEnabled )
        *flags |= B_NOT_CLOSABLE;

    if ( m_showInactive )
        *flags |= B_AVOID_FOCUS;

    // Floating windows stay above the others and can still be the active
    // window.
    if ( style & wxSTAY_ON_TOP )
        *feel = B_FLOATING_ALL_WINDOW_FEEL;
    else
        *feel = B_NORMAL_WINDOW_FEEL;
}

void wxTopLevelWindowHaiku::SetWindowStyleFlag(long style)
{
    wxTopLevelWindowBase::SetWindowStyleFlag(style);

    HaikuUpdateWindowType();
}

// ----------------------------------------------------------------------------
// showing and hiding
// ----------------------------------------------------------------------------

bool wxTopLevelWindowHaiku::Show(bool show)
{
    if ( show && !m_shownOnce && m_autoPosition )
    {
        // Nobody said where the window should be: dialogs appear over the
        // window they belong to (or in the middle of the screen), as the
        // window managers of the other platforms place them; frames keep
        // their cascaded position.
        if ( wxDynamicCast(this, wxDialog) )
        {
            wxWindow * const parent = wxGetTopLevelParent(GetParent());
            if ( parent && parent != this && parent->IsShown() )
                CentreOnParent();
            else
                CentreOnScreen();
        }

        m_autoPosition = false;
    }

    if ( show && m_showInactive )
    {
        // shown normally this time
        m_showInactive = false;
        HaikuUpdateWindowType();
    }

    if ( !wxNonOwnedWindow::Show(show) )
        return false;

    if ( !show && m_isActive )
    {
        // app_server reports the deactivation too, but wx code run before
        // then must not think that a hidden window is the active one
        HaikuActivated(false);

        // The window this one belonged to gets the activation back (the
        // parent of a dialog, as in the other ports): app_server does not
        // necessarily pick it.
        wxWindow *next = wxGetTopLevelParent(GetParent());
        if ( !next || !next->IsShown() || !next->IsEnabled() )
            next = wxTheApp ? wxTheApp->GetTopWindow() : NULL;

        wxTopLevelWindow * const
            tlw = wxDynamicCast(next, wxTopLevelWindow);
        if ( tlw && tlw != this && tlw->IsShown() && tlw->IsEnabled()
                && !tlw->IsIconized() )
        {
            tlw->Raise();
        }
    }

    return true;
}

void wxTopLevelWindowHaiku::ShowWithoutActivating()
{
    if ( IsShown() )
        return;

    // a window which avoids the focus is not activated when it is shown;
    // the flag is removed again once it is on screen
    m_showInactive = true;
    HaikuUpdateWindowType();

    wxNonOwnedWindow::Show(true);

    m_showInactive = false;
    HaikuUpdateWindowType();
}

bool wxTopLevelWindowHaiku::ShowFullScreen(bool show, long style)
{
    if ( show == m_fsIsShowing )
        return true;

    if ( show )
    {
        m_fsSaveStyle = style;
        m_fsSaveFrame = GetRect();
        m_fsIsShowing = true;

        // no decorations any more
        HaikuUpdateWindowType();

        wxRect screen;
        if ( m_window )
        {
            wxHaikuWindowLock lock(m_window);
            BScreen bscreen(m_window);
            screen = wxFromHaikuRect(bscreen.Frame());
        }
        else
        {
            BScreen bscreen(B_MAIN_SCREEN_ID);
            screen = wxFromHaikuRect(bscreen.Frame());
        }

        SetSize(screen);
    }
    else
    {
        m_fsIsShowing = false;
        HaikuUpdateWindowType();

        SetSize(m_fsSaveFrame);
    }

    wxFullScreenEvent event(GetId(), show);
    event.SetEventObject(this);
    HandleWindowEvent(event);

    return true;
}

// ----------------------------------------------------------------------------
// maximize, minimize etc.
// ----------------------------------------------------------------------------

void wxTopLevelWindowHaiku::Maximize(bool maximize)
{
    if ( maximize == m_isMaximized )
        return;

    if ( maximize )
    {
        m_savedFrame = GetRect();
        m_isMaximized = true;

        SetSize(wxDisplay(this).GetClientArea());
    }
    else
    {
        m_isMaximized = false;

        SetSize(m_savedFrame);
    }
}

bool wxTopLevelWindowHaiku::IsMaximized() const
{
    return m_isMaximized;
}

void wxTopLevelWindowHaiku::Restore()
{
    if ( IsIconized() )
        Iconize(false);
    else if ( IsMaximized() )
        Maximize(false);
}

void wxTopLevelWindowHaiku::Iconize(bool iconize)
{
    if ( !m_window || iconize == m_isIconized )
        return;

    {
        wxHaikuWindowLock lock(m_window);
        if ( !lock.IsLocked() )
            return;

        m_window->Minimize(iconize);
    }

    m_isIconized = iconize;

    wxIconizeEvent event(GetId(), iconize);
    event.SetEventObject(this);
    HandleWindowEvent(event);
}

bool wxTopLevelWindowHaiku::IsIconized() const
{
    return m_isIconized;
}

// ----------------------------------------------------------------------------
// title, icons, attention
// ----------------------------------------------------------------------------

void wxTopLevelWindowHaiku::HaikuWindowCreated(BWindow *window)
{
    wxNonOwnedWindow::HaikuWindowCreated(window);

    // (the window does not run yet: this thread has its lock)
    window->SetTitle(wxHaikuStr(m_title));
}

void wxTopLevelWindowHaiku::SetTitle(const wxString& title)
{
    m_title = title;

    if ( m_window )
    {
        wxHaikuWindowLock lock(m_window);
        if ( lock.IsLocked() )
            m_window->SetTitle(wxHaikuStr(title));
    }
}

void wxTopLevelWindowHaiku::SetIcons(const wxIconBundle& icons)
{
    // windows have no icons of their own here: the Deskbar and the window
    // switcher show the icon of the application file
    wxTopLevelWindowBase::SetIcons(icons);
}

void wxTopLevelWindowHaiku::RequestUserAttention(int WXUNUSED(flags))
{
    // nothing native to flash

}

bool wxTopLevelWindowHaiku::EnableCloseButton(bool enable)
{
    m_closeEnabled = enable;
    HaikuUpdateWindowType();
    return true;
}

// ----------------------------------------------------------------------------
// activation
// ----------------------------------------------------------------------------

void wxTopLevelWindowHaiku::HaikuActivated(bool active)
{
    wxHAIKU_TRACE("activate %d tlw=%u (%s) was=%d", (int)active,
                  (unsigned)m_handle, (const char *)wxString(GetClassInfo()->GetClassName()).utf8_str(),
                  (int)m_isActive);

    if ( active == m_isActive )
        return;

    // a disabled window (behind a modal dialog) does not become active for
    // wx even if the user managed to activate its BWindow
    if ( active && (!IsShown() || !IsThisEnabled()) )
        return;

    m_isActive = active;

    const wxUint32 handle = m_handle;

    if ( !active )
    {
        // the focus leaves with the activation: remember who had it
        wxWindowHaiku * const focus = wxHaikuGetFocusWindow();
        if ( focus )
        {
            wxWindow *top = wxGetTopLevelParent(static_cast<wxWindow *>(focus));
            while ( top && top != this && !wxDynamicCast(top, wxTopLevelWindow) )
                top = wxGetTopLevelParent(top->GetParent());

            if ( top == this )
            {
                if ( focus->GetTLW() == this )
                    m_lastFocus = static_cast<wxWindow *>(focus);
                wxHaikuKillFocus();
            }
        }

        if ( HaikuFindByHandle(handle) != this )
            return;
    }

    wxActivateEvent event(wxEVT_ACTIVATE, active, GetId());
    event.SetEventObject(this);
    HandleWindowEvent(event);

    if ( !active || HaikuFindByHandle(handle) != this || !m_isActive )
        return;

    // give the focus back to the window which had it, or to the first one
    // which takes it
    wxWindowHaiku * const focus = wxHaikuGetFocusWindow();
    if ( focus && focus->GetTLW() == this )
        return;

    if ( m_lastFocus && m_lastFocus != this && m_lastFocus->IsShownOnScreen()
            && m_lastFocus->IsEnabled() )
    {
        m_lastFocus->SetFocus();
    }
    else
    {
        m_lastFocus = NULL;
        SetFocus();
    }
}

// ----------------------------------------------------------------------------
// event records
// ----------------------------------------------------------------------------

void wxTopLevelWindowHaiku::HaikuHandleEvent(const wxHaikuEvent& event)
{
    switch ( event.type )
    {
        case wxHAIKU_EVT_CLOSE:
            // the close button, or Command-W and the like: the modal dialog
            // in front decides, not the window it blocks
            if ( IsEnabled() )
                Close();
            break;

        case wxHAIKU_EVT_MINIMIZE:
            if ( event.flag != m_isIconized )
            {
                m_isIconized = event.flag;

                wxIconizeEvent iconizeEvent(GetId(), event.flag);
                iconizeEvent.SetEventObject(this);
                HandleWindowEvent(iconizeEvent);
            }
            break;

        case wxHAIKU_EVT_ZOOM:
            {
                const bool maximize = !m_isMaximized;
                Maximize(maximize);

                if ( maximize )
                {
                    wxMaximizeEvent maximizeEvent(GetId());
                    maximizeEvent.SetEventObject(this);
                    HandleWindowEvent(maximizeEvent);
                }
            }
            break;

        case wxHAIKU_EVT_SCREEN_CHANGED:
            {
                wxDisplayChangedEvent displayEvent;
                displayEvent.SetEventObject(this);
                HandleWindowEvent(displayEvent);
            }
            break;

        default:
            wxTopLevelWindowBase::HaikuHandleEvent(event);
    }
}
