/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/pen.cpp
// Purpose:     wxPen
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/bitmap.h"
    #include "wx/colour.h"
#endif

#include "wx/pen.h"

class wxPenRefData : public wxGDIRefData
{
public:
    wxPenRefData(const wxColour& colour = wxNullColour,
                 int width = 1,
                 wxPenStyle style = wxPENSTYLE_SOLID)
        : m_colour(colour),
          m_width(width),
          m_style(style),
          m_join(wxJOIN_ROUND),
          m_cap(wxCAP_ROUND),
          m_countDashes(0),
          m_dashes(NULL)
    {
    }

    wxPenRefData(const wxPenRefData& data)
        : wxGDIRefData(),
          m_colour(data.m_colour),
          m_width(data.m_width),
          m_style(data.m_style),
          m_join(data.m_join),
          m_cap(data.m_cap),
          m_countDashes(data.m_countDashes),
          m_dashes(data.m_dashes),
          m_stipple(data.m_stipple)
    {
    }

    bool operator==(const wxPenRefData& data) const
    {
        if ( m_countDashes != data.m_countDashes )
            return false;

        if ( m_dashes )
        {
            if ( !data.m_dashes
                    || memcmp(m_dashes, data.m_dashes,
                              m_countDashes * sizeof(wxDash)) != 0 )
            {
                return false;
            }
        }
        else if ( data.m_dashes )
        {
            return false;
        }

        return m_style == data.m_style
            && m_width == data.m_width
            && m_join == data.m_join
            && m_cap == data.m_cap
            && m_colour == data.m_colour
            && (m_style != wxPENSTYLE_STIPPLE
                    || m_stipple.IsSameAs(data.m_stipple));
    }

    virtual bool IsOk() const wxOVERRIDE { return m_colour.IsOk(); }

    wxColour m_colour;
    int m_width;
    wxPenStyle m_style;
    wxPenJoin m_join;
    wxPenCap m_cap;
    int m_countDashes;
    wxDash *m_dashes; // not owned, as in the other ports
    wxBitmap m_stipple;
};

#define M_PENDATA (static_cast<wxPenRefData *>(m_refData))

wxIMPLEMENT_DYNAMIC_CLASS(wxPen, wxGDIObject);

wxPen::wxPen(const wxColour& colour, int width, wxPenStyle style)
{
    m_refData = new wxPenRefData(colour, width, style);
}

wxPen::wxPen(const wxColour& colour, int width, int style)
{
    m_refData = new wxPenRefData(colour, width, (wxPenStyle)style);
}

wxPen::wxPen(const wxBitmap& stipple, int width)
{
    wxPenRefData * const data = new wxPenRefData(*wxBLACK, width,
                                                 wxPENSTYLE_STIPPLE);
    data->m_stipple = stipple;
    m_refData = data;
}

wxPen::wxPen(const wxPenInfo& info)
{
    wxPenRefData * const data = new wxPenRefData(info.GetColour(),
                                                 info.GetWidth(),
                                                 info.GetStyle());
    data->m_join = info.GetJoin();
    data->m_cap = info.GetCap();
    data->m_countDashes = info.GetDashes(&data->m_dashes);
    data->m_stipple = info.GetStipple();
    m_refData = data;
}

bool wxPen::operator==(const wxPen& pen) const
{
    if ( m_refData == pen.m_refData )
        return true;

    if ( !m_refData || !pen.m_refData )
        return false;

    return *M_PENDATA == *static_cast<wxPenRefData *>(pen.m_refData);
}

void wxPen::SetColour(const wxColour& colour)
{
    AllocExclusive();
    M_PENDATA->m_colour = colour;
}

void wxPen::SetColour(unsigned char red, unsigned char green, unsigned char blue)
{
    AllocExclusive();
    M_PENDATA->m_colour.Set(red, green, blue);
}

void wxPen::SetCap(wxPenCap capStyle)
{
    AllocExclusive();
    M_PENDATA->m_cap = capStyle;
}

void wxPen::SetJoin(wxPenJoin joinStyle)
{
    AllocExclusive();
    M_PENDATA->m_join = joinStyle;
}

void wxPen::SetStyle(wxPenStyle style)
{
    AllocExclusive();
    M_PENDATA->m_style = style;
}

void wxPen::SetWidth(int width)
{
    AllocExclusive();
    M_PENDATA->m_width = width;
}

void wxPen::SetDashes(int number_of_dashes, const wxDash *dash)
{
    AllocExclusive();
    M_PENDATA->m_countDashes = number_of_dashes;
    M_PENDATA->m_dashes = const_cast<wxDash *>(dash);
}

void wxPen::SetStipple(const wxBitmap& stipple)
{
    AllocExclusive();
    M_PENDATA->m_stipple = stipple;
    M_PENDATA->m_style = wxPENSTYLE_STIPPLE;
}

int wxPen::GetDashes(wxDash **ptr) const
{
    wxCHECK_MSG( IsOk(), 0, wxS("invalid pen") );

    if ( ptr )
        *ptr = M_PENDATA->m_dashes;

    return M_PENDATA->m_countDashes;
}

int wxPen::GetDashCount() const
{
    wxCHECK_MSG( IsOk(), 0, wxS("invalid pen") );

    return M_PENDATA->m_countDashes;
}

wxDash *wxPen::GetDash() const
{
    wxCHECK_MSG( IsOk(), NULL, wxS("invalid pen") );

    return M_PENDATA->m_dashes;
}

wxPenCap wxPen::GetCap() const
{
    wxCHECK_MSG( IsOk(), wxCAP_INVALID, wxS("invalid pen") );

    return M_PENDATA->m_cap;
}

wxPenJoin wxPen::GetJoin() const
{
    wxCHECK_MSG( IsOk(), wxJOIN_INVALID, wxS("invalid pen") );

    return M_PENDATA->m_join;
}

wxPenStyle wxPen::GetStyle() const
{
    wxCHECK_MSG( IsOk(), wxPENSTYLE_INVALID, wxS("invalid pen") );

    return M_PENDATA->m_style;
}

int wxPen::GetWidth() const
{
    wxCHECK_MSG( IsOk(), -1, wxS("invalid pen") );

    return M_PENDATA->m_width;
}

wxColour wxPen::GetColour() const
{
    wxCHECK_MSG( IsOk(), wxNullColour, wxS("invalid pen") );

    return M_PENDATA->m_colour;
}

wxBitmap *wxPen::GetStipple() const
{
    wxCHECK_MSG( IsOk(), NULL, wxS("invalid pen") );

    return &M_PENDATA->m_stipple;
}

wxGDIRefData *wxPen::CreateGDIRefData() const
{
    return new wxPenRefData;
}

wxGDIRefData *wxPen::CloneGDIRefData(const wxGDIRefData *data) const
{
    return new wxPenRefData(*static_cast<const wxPenRefData *>(data));
}
