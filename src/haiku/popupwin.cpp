/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/popupwin.cpp
// Purpose:     wxPopupWindow
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_POPUPWIN

#include "wx/popupwin.h"

#include "wx/haiku/private.h"

bool wxPopupWindow::Create(wxWindow *parent, int flags)
{
    if ( !wxPopupWindowBase::Create(parent) )
        return false;

    return wxNonOwnedWindow::Create
           (
             parent,
             wxID_ANY,
             wxPoint(0, 0), wxSize(1, 1),
             (flags & (wxBORDER_MASK | wxPU_CONTAINS_CONTROLS)) | wxPOPUP_WINDOW
           );
}

void wxPopupWindow::HaikuGetWindowType(int *look, int *feel,
                                       wxUint32 *flags) const
{
    // The window of a menu: without decorations, above all other windows
    // including modal ones, never the active window (its owner stays
    // active and keeps the keyboard, which wx routes to the popup's
    // children if they have the wx focus) and clicks are always delivered.
    *look = B_NO_BORDER_WINDOW_LOOK;
    *feel = wxHAIKU_MENU_WINDOW_FEEL;
    *flags = B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE
                | B_NOT_MINIMIZABLE | B_AVOID_FOCUS | B_WILL_ACCEPT_FIRST_CLICK
                | B_ASYNCHRONOUS_CONTROLS | B_NO_WORKSPACE_ACTIVATION;
}

#endif // wxUSE_POPUPWIN
