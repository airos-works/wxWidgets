/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/tooltip.cpp
// Purpose:     wxToolTip
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_TOOLTIPS

#include "wx/tooltip.h"

#ifndef WX_PRECOMP
    #include "wx/window.h"
#endif

#include "wx/haiku/private.h"

wxIMPLEMENT_ABSTRACT_CLASS(wxToolTip, wxObject);

namespace
{

bool gs_enabled = true;

} // anonymous namespace

wxToolTip::wxToolTip(const wxString& tip)
    : m_text(tip),
      m_window(NULL)
{
}

/* static */
void wxToolTip::Enable(bool flag)
{
    gs_enabled = flag;
}

/* static */
bool wxToolTip::HaikuIsEnabled()
{
    return gs_enabled;
}

/* static */
void wxToolTip::SetDelay(long WXUNUSED(msecs))
{
    // the delays are those of the system
}

/* static */
void wxToolTip::SetAutoPop(long WXUNUSED(msecs))
{
}

/* static */
void wxToolTip::SetReshow(long WXUNUSED(msecs))
{
}

void wxToolTip::SetTip(const wxString& tip)
{
    m_text = tip;
    Apply();
}

void wxToolTip::SetWindow(wxWindow *win)
{
    m_window = win;
}

void wxToolTip::Apply()
{
    BView * const view = m_window ? m_window->GetHaikuView() : NULL;
    if ( !view )
        return;

    wxHaikuViewLock lock(view);
    if ( !lock.CanTouch() )
        return;

    if ( gs_enabled && !m_text.empty() )
        view->SetToolTip(wxHaikuStr(m_text));
    else
        view->SetToolTip((const char *)NULL);
}

#endif // wxUSE_TOOLTIPS
