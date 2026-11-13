/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/tglbtn.cpp
// Purpose:     wxToggleButton
// Author:      Vadim Zeitlin
// Modified by: David Bjorkevik
// Created:     16.05.06
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"


#if wxUSE_TOGGLEBTN

#include "wx/tglbtn.h"
#include "wx/univ/renderer.h"
#include "wx/univ/colschem.h"
#include "wx/univ/theme.h"

#include "wx/stockitem.h"
#include "wx/dcclient.h"

wxDEFINE_EVENT( wxEVT_TOGGLEBUTTON, wxCommandEvent );

wxIMPLEMENT_DYNAMIC_CLASS(wxToggleButton, wxToggleButtonBase);

wxToggleButton::wxToggleButton()
{
    Init();
}

wxToggleButton::wxToggleButton(wxWindow *parent,
                       wxWindowID id,
                       const wxString& label,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    Init();
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxToggleButton::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxString& lbl,
                            const wxPoint& pos,
                            const wxSize& size, long style,
                            const wxValidator& validator,
                            const wxString& name)
{
    wxString label(lbl);
    if (label.empty() && wxIsStockID(id))
        label = wxGetStockLabel(id);

    long ctrl_style = style & ~wxBU_ALIGN_MASK;
    ctrl_style = ctrl_style & ~wxALIGN_MASK;

    if((style & wxBU_RIGHT) == wxBU_RIGHT)
        ctrl_style |= wxALIGN_RIGHT;
    else if((style & wxBU_LEFT) == wxBU_LEFT)
        ctrl_style |= wxALIGN_LEFT;
    else
        ctrl_style |= wxALIGN_CENTRE_HORIZONTAL;

    if((style & wxBU_TOP) == wxBU_TOP)
        ctrl_style |= wxALIGN_TOP;
    else if((style & wxBU_BOTTOM) == wxBU_BOTTOM)
        ctrl_style |= wxALIGN_BOTTOM;
    else
        ctrl_style |= wxALIGN_CENTRE_VERTICAL;

    if ( !wxToggleButtonBase::Create(parent, id, pos, size, ctrl_style, validator, name) )
    {
        wxFAIL_MSG(wxT("wxToggleButton creation failed"));
        return false;
    }
    SetLabel(label);
    CreateInputHandler(wxINP_HANDLER_BUTTON);
    return true;
}

void wxToggleButton::Init()
{
    m_isPressed = false;
    m_value = false;
}

void wxToggleButton::Toggle()
{
    if ( m_isPressed )
        Release();
    else
        Press();

    if ( !m_isPressed )
    {
        // releasing button after it had been pressed generates a click event
        // and toggles value
        m_value = !m_value;
        Click();
    }
}

void wxToggleButton::Click()
{
    wxCommandEvent event(wxEVT_TOGGLEBUTTON, GetId());
    InitCommandEvent(event);
    event.SetInt(GetValue());
    Command(event);
}

void wxToggleButton::SetValue(bool state)
{
    m_value = state;
    Refresh();
}

// ----------------------------------------------------------------------------
// wxBitmapToggleButton
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxBitmapToggleButton, wxToggleButton);

bool wxBitmapToggleButton::Create(wxWindow *parent,
                                  wxWindowID id,
                                  const wxBitmap& label,
                                  const wxPoint& pos,
                                  const wxSize& size,
                                  long style,
                                  const wxValidator& validator,
                                  const wxString& name)
{
    if ( !wxToggleButton::Create(parent, id, wxEmptyString, pos, size, style,
                                 validator, name) )
        return false;

    if ( label.IsOk() )
    {
        m_bitmaps[State_Normal] = label;

        if ( size.x == wxDefaultCoord || size.y == wxDefaultCoord )
            SetInitialSize(size);
    }

    return true;
}

void wxBitmapToggleButton::DoSetBitmap(const wxBitmap& bitmap, State which)
{
    m_bitmaps[which] = bitmap;

    if ( which == State_Normal )
        InvalidateBestSize();

    Refresh();
}

wxSize wxBitmapToggleButton::DoGetBestClientSize() const
{
    const wxBitmap& bmp = m_bitmaps[State_Normal];
    if ( !bmp.IsOk() )
        return wxToggleButton::DoGetBestClientSize();

    wxSize best(bmp.GetWidth(), bmp.GetHeight());

    const wxString label = UnivGetLabel();
    if ( !label.empty() )
    {
        wxClientDC dc(wxConstCast(this, wxBitmapToggleButton));
        wxCoord width, height;
        dc.GetMultiLineTextExtent(label, &width, &height);
        best.x += width + 4;
        best.y = wxMax(best.y, height);
    }

    return best;
}

bool wxBitmapToggleButton::DoDrawBackground(wxDC& dc)
{
    // with a border it is a button, without one only the bitmap is drawn,
    // on the background of the parent
    if ( !HasFlag(wxBORDER_NONE) )
        return wxToggleButton::DoDrawBackground(dc);

    return wxControl::DoDrawBackground(dc);
}

void wxBitmapToggleButton::DoDraw(wxControlRenderer *renderer)
{
    if ( !HasFlag(wxBORDER_NONE) )
        renderer->DrawButtonBorder();

    // the bitmap of the state, if the application gave one for it
    State state = State_Normal;
    if ( !IsEnabled() )
        state = State_Disabled;
    else if ( m_isPressed && m_bitmaps[State_Pressed].IsOk() )
        state = State_Pressed;
    else if ( (GetStateFlags() & wxCONTROL_CURRENT) && m_bitmaps[State_Current].IsOk() )
        state = State_Current;
    else if ( (GetStateFlags() & wxCONTROL_FOCUSED) && m_bitmaps[State_Focused].IsOk() )
        state = State_Focused;

    const wxBitmap& bmp = m_bitmaps[state].IsOk() ? m_bitmaps[state]
                                                  : m_bitmaps[State_Normal];

    const wxString label = UnivGetLabel();
    if ( label.empty() )
    {
        if ( bmp.IsOk() )
        {
            const wxRect rect = renderer->GetRect();
            renderer->GetDC().DrawBitmap(bmp,
                                         rect.x + (rect.width - bmp.GetWidth()) / 2,
                                         rect.y + (rect.height - bmp.GetHeight()) / 2,
                                         true);
        }
    }
    else
    {
        renderer->DrawButtonLabel(bmp, m_marginBmpX, m_marginBmpY);
    }
}

#endif // wxUSE_TOGGLEBTN
