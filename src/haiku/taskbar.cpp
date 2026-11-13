/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/taskbar.cpp
// Purpose:     wxTaskBarIcon for the Haiku port: a placeholder
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_TASKBARICON

#include "wx/taskbar.h"

#ifndef WX_PRECOMP
    #include "wx/icon.h"
    #include "wx/menu.h"
#endif

// PLACEHOLDER: nothing is shown in the Deskbar, see wx/haiku/taskbar.h.

wxIMPLEMENT_DYNAMIC_CLASS(wxTaskBarIcon, wxEvtHandler);

/* static */
bool wxTaskBarIconBase::IsAvailable()
{
    return false;
}

wxTaskBarIcon::wxTaskBarIcon(wxTaskBarIconType WXUNUSED(iconType))
{
}

wxTaskBarIcon::~wxTaskBarIcon()
{
}

bool wxTaskBarIcon::SetIcon(const wxIcon& WXUNUSED(icon),
                            const wxString& WXUNUSED(tooltip))
{
    return false;
}

bool wxTaskBarIcon::RemoveIcon()
{
    return false;
}

bool wxTaskBarIcon::PopupMenu(wxMenu *WXUNUSED(menu))
{
    return false;
}

#endif // wxUSE_TASKBARICON
