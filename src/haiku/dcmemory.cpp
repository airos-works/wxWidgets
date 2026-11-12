/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dcmemory.cpp
// Purpose:     wxMemoryDCImpl
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/bitmap.h"
#endif

#include "wx/dcmemory.h"
#include "wx/haiku/dcmemory.h"
#include "wx/haiku/private.h"

wxIMPLEMENT_ABSTRACT_CLASS(wxMemoryDCImpl, wxHaikuDCImpl);

void wxMemoryDCImpl::Init()
{
}

wxMemoryDCImpl::wxMemoryDCImpl(wxMemoryDC *owner)
    : wxHaikuDCImpl(owner)
{
    Init();
}

wxMemoryDCImpl::wxMemoryDCImpl(wxMemoryDC *owner, wxBitmap& bitmap)
    : wxHaikuDCImpl(owner)
{
    Init();
    DoSelect(bitmap);
}

wxMemoryDCImpl::wxMemoryDCImpl(wxMemoryDC *owner, wxDC *WXUNUSED(dc))
    : wxHaikuDCImpl(owner)
{
    Init();
}

wxMemoryDCImpl::~wxMemoryDCImpl()
{
    if ( m_bmp.IsOk() )
        m_bmp.HaikuEndDrawing();
}

void wxMemoryDCImpl::DoSelect(const wxBitmap& bitmap)
{
    if ( m_bmp.IsOk() )
    {
        // the view of the old bitmap must not be used any more
        HaikuInit(NULL, wxPoint(), wxSize(), false);
        m_bmp.HaikuEndDrawing();
    }

    m_bmp = bitmap;

    if ( !m_bmp.IsOk() )
    {
        HaikuInit(NULL, wxPoint(), wxSize(), false);
        return;
    }

    // the bitmap gets a view to draw with; nothing is drawn if this fails
    BView * const view = m_bmp.HaikuBeginDrawing();

    HaikuInit(view, wxPoint(0, 0),
              wxSize(m_bmp.GetWidth(), m_bmp.GetHeight()),
              m_bmp.HasAlpha());
}

void wxMemoryDCImpl::HaikuTargetChanged()
{
    m_bmp.HaikuTouch();
}

const wxBitmap *wxMemoryDCImpl::HaikuGetSourceBitmap(wxPoint *origin) const
{
    if ( origin )
        *origin = wxPoint(0, 0);

    return m_bmp.IsOk() ? &m_bmp : NULL;
}

bool wxMemoryDCImpl::HaikuClearTranslucent(const wxColour& colour)
{
    if ( !m_bmp.IsOk() )
        return false;

    // waits for the drawing in progress, then the pixels are ours
    unsigned char * const bits = m_bmp.HaikuGetBits();
    const int rowBytes = m_bmp.HaikuGetRowBytes();
    const int width = m_bmp.GetWidth(), height = m_bmp.GetHeight();

    const unsigned char b = colour.Blue(), g = colour.Green(),
                        r = colour.Red(), a = colour.Alpha();

    for ( int y = 0; y < height; y++ )
    {
        unsigned char *p = bits + y * rowBytes;
        for ( int x = 0; x < width; x++, p += 4 )
        {
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = a;
        }
    }

    return true;
}

wxBitmap wxMemoryDCImpl::DoGetAsBitmap(const wxRect *subrect) const
{
    if ( !m_bmp.IsOk() )
        return wxNullBitmap;

    return subrect ? m_bmp.GetSubBitmap(*subrect) : m_bmp;
}
