/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/brush.cpp
// Purpose:     wxBrush
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/bitmap.h"
    #include "wx/colour.h"
#endif

#include "wx/brush.h"

class wxBrushRefData : public wxGDIRefData
{
public:
    wxBrushRefData(const wxColour& colour = wxNullColour,
                   wxBrushStyle style = wxBRUSHSTYLE_SOLID)
        : m_colour(colour), m_style(style)
    {
    }

    wxBrushRefData(const wxBrushRefData& data)
        : wxGDIRefData(),
          m_colour(data.m_colour),
          m_style(data.m_style),
          m_stipple(data.m_stipple)
    {
    }

    bool operator==(const wxBrushRefData& data) const
    {
        return m_style == data.m_style
            && m_colour == data.m_colour
            && m_stipple.IsSameAs(data.m_stipple);
    }

    virtual bool IsOk() const wxOVERRIDE { return m_colour.IsOk(); }

    wxColour m_colour;
    wxBrushStyle m_style;
    wxBitmap m_stipple;
};

#define M_BRUSHDATA (static_cast<wxBrushRefData *>(m_refData))

wxIMPLEMENT_DYNAMIC_CLASS(wxBrush, wxGDIObject);

wxBrush::wxBrush(const wxColour& colour, wxBrushStyle style)
{
    m_refData = new wxBrushRefData(colour, style);
}

wxBrush::wxBrush(const wxColour& colour, int style)
{
    m_refData = new wxBrushRefData(colour, (wxBrushStyle)style);
}

wxBrush::wxBrush(const wxBitmap& stippleBitmap)
{
    wxBrushRefData * const data = new wxBrushRefData(*wxBLACK);
    data->m_stipple = stippleBitmap;
    data->m_style = stippleBitmap.GetMask() ? wxBRUSHSTYLE_STIPPLE_MASK_OPAQUE
                                            : wxBRUSHSTYLE_STIPPLE;
    m_refData = data;
}

bool wxBrush::operator==(const wxBrush& brush) const
{
    if ( m_refData == brush.m_refData )
        return true;

    if ( !m_refData || !brush.m_refData )
        return false;

    return *M_BRUSHDATA == *static_cast<wxBrushRefData *>(brush.m_refData);
}

wxBrushStyle wxBrush::GetStyle() const
{
    wxCHECK_MSG( IsOk(), wxBRUSHSTYLE_INVALID, wxS("invalid brush") );

    return M_BRUSHDATA->m_style;
}

wxColour wxBrush::GetColour() const
{
    wxCHECK_MSG( IsOk(), wxNullColour, wxS("invalid brush") );

    return M_BRUSHDATA->m_colour;
}

wxBitmap *wxBrush::GetStipple() const
{
    wxCHECK_MSG( IsOk(), NULL, wxS("invalid brush") );

    return &M_BRUSHDATA->m_stipple;
}

void wxBrush::SetColour(const wxColour& col)
{
    AllocExclusive();
    M_BRUSHDATA->m_colour = col;
}

void wxBrush::SetColour(unsigned char r, unsigned char g, unsigned char b)
{
    AllocExclusive();
    M_BRUSHDATA->m_colour.Set(r, g, b);
}

void wxBrush::SetStyle(wxBrushStyle style)
{
    AllocExclusive();
    M_BRUSHDATA->m_style = style;
}

void wxBrush::SetStipple(const wxBitmap& stipple)
{
    AllocExclusive();
    M_BRUSHDATA->m_stipple = stipple;
    M_BRUSHDATA->m_style = stipple.GetMask() ? wxBRUSHSTYLE_STIPPLE_MASK_OPAQUE
                                             : wxBRUSHSTYLE_STIPPLE;
}

wxGDIRefData *wxBrush::CreateGDIRefData() const
{
    return new wxBrushRefData;
}

wxGDIRefData *wxBrush::CloneGDIRefData(const wxGDIRefData *data) const
{
    return new wxBrushRefData(*static_cast<const wxBrushRefData *>(data));
}
