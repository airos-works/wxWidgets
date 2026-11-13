///////////////////////////////////////////////////////////////////////////////
// Name:        src/gauge/gauge.cpp
// Purpose:     wxGauge for wxUniversal
// Author:      Vadim Zeitlin
// Modified by:
// Created:     20.02.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_GAUGE

#include "wx/gauge.h"

#ifndef WX_PRECOMP
#endif //WX_PRECOMP

#include "wx/univ/renderer.h"

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxGauge creation
// ----------------------------------------------------------------------------

void wxGauge::Init()
{
    m_gaugePos =
    m_rangeMax = 0;
}

bool wxGauge::Create(wxWindow *parent,
                     wxWindowID id,
                     int range,
                     const wxPoint& pos,
                     const wxSize& size,
                     long style,
                     const wxValidator& validator,
                     const wxString& name)
{
    if ( !wxGaugeBase::Create(parent, id, range, pos, size, style,
                              validator, name) )
    {
        return false;
    }

    SetInitialSize(size);

    return true;
}

// ----------------------------------------------------------------------------
// wxGauge range/position
// ----------------------------------------------------------------------------

void wxGauge::SetRange(int range)
{
    wxGaugeBase::SetRange(range);

    Refresh();
}

void wxGauge::SetValue(int pos)
{
    wxGaugeBase::SetValue(pos);

    Refresh();
}

// ----------------------------------------------------------------------------
// wxGauge geometry
// ----------------------------------------------------------------------------

wxSize wxGauge::DoGetBestClientSize() const
{
    wxSize size = GetRenderer()->GetProgressBarStep();

    // these adjustments are really ridiculous - they are just done to find the
    // "correct" result under Windows (FIXME)
    if ( IsVertical() )
    {
        size.x = (3*size.y) / 2 + 2;
        size.y = wxDefaultCoord;
    }
    else
    {
        size.y = (3*size.x) / 2 + 2;
        size.x = wxDefaultCoord;
    }

    return size;
}

// ----------------------------------------------------------------------------
// wxGauge drawing
// ----------------------------------------------------------------------------

wxBorder wxGauge::GetDefaultBorder() const
{
#ifdef __WXHAIKU__
    // the native gauge has its frame
    return wxBORDER_NONE;
#else
    return wxBORDER_STATIC;
#endif
}

void wxGauge::DoDraw(wxControlRenderer *renderer)
{
#ifdef __WXHAIKU__
    // drawn as the BStatusBar of the system by the native renderer
    wxRenderer * const rend = renderer->GetRenderer();
    rend->DrawBackground(renderer->GetDC(), wxNullColour,
                         renderer->GetRect(), GetStateFlags(), this);
    rend->DrawGauge(this, renderer->GetDC(), renderer->GetRect(),
                    GetValue(), GetRange(),
                    IsVertical() ? wxCONTROL_SPECIAL : 0);
#else
    renderer->DrawProgressBar(this);
#endif
}

#endif // wxUSE_GAUGE
