/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dcscreen.cpp
// Purpose:     wxScreenDCImpl
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/dcscreen.h"
#include "wx/haiku/dcscreen.h"
#include "wx/haiku/private.h"

#include <Screen.h>

wxIMPLEMENT_ABSTRACT_CLASS(wxScreenDCImpl, wxHaikuDCImpl);

wxScreenDCImpl::wxScreenDCImpl(wxScreenDC *owner)
    : wxHaikuDCImpl(owner)
{
    BScreen screen(B_MAIN_SCREEN_ID);
    const wxRect frame = wxFromHaikuRect(screen.Frame());

    // no view: nothing can be drawn on the screen itself
    HaikuInit(NULL, wxPoint(0, 0), frame.GetSize(), false);
}

wxScreenDCImpl::~wxScreenDCImpl()
{
}

const wxBitmap *wxScreenDCImpl::HaikuGetSourceBitmap(wxPoint *origin) const
{
    if ( origin )
        *origin = wxPoint(0, 0);

    if ( !m_snapshot.IsOk() )
    {
        // the screen as it is the first time it is read from
        BScreen screen(B_MAIN_SCREEN_ID);
        BBitmap *shot = NULL;
        if ( screen.GetBitmap(&shot, false) != B_OK || !shot )
            return NULL;

        const int width = shot->Bounds().IntegerWidth() + 1,
                  height = shot->Bounds().IntegerHeight() + 1;

        if ( shot->ColorSpace() == B_RGB32 || shot->ColorSpace() == B_RGBA32 )
        {
            m_snapshot.Create(width, height, 24);
            if ( m_snapshot.IsOk() )
            {
                unsigned char * const bits = m_snapshot.HaikuGetBits();
                const int rowBytes = m_snapshot.HaikuGetRowBytes();
                for ( int y = 0; y < height; y++ )
                {
                    const unsigned char *s = (const unsigned char *)shot->Bits()
                                                + y * shot->BytesPerRow();
                    unsigned char *d = bits + y * rowBytes;
                    for ( int x = 0; x < width; x++, s += 4, d += 4 )
                    {
                        d[0] = s[0];
                        d[1] = s[1];
                        d[2] = s[2];
                        d[3] = 0xff;
                    }
                }
            }
        }

        delete shot;
    }

    return m_snapshot.IsOk() ? &m_snapshot : NULL;
}
