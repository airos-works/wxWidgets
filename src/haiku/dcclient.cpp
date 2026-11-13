/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dcclient.cpp
// Purpose:     wxWindowDCImpl, wxClientDCImpl, wxPaintDCImpl and the
//              off-screen buffers windows are painted through
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/window.h"
    #include "wx/nonownedwnd.h"
#endif

#include "wx/dcclient.h"
#include "wx/haiku/dcclient.h"
#include "wx/haiku/private.h"

#include <AffineTransform.h>

// ============================================================================
// paint buffers
// ============================================================================

namespace
{

// The buffers are bitmaps which accept views, with a view attached: a window
// is painted into one of them and the result is copied to the window in one
// go. One buffer is enough unless a paint handler makes another window paint
// itself (Update(), wxYield()); it grows to the biggest area painted so far.
struct PaintBuffer
{
    BBitmap *bitmap;
    wxHaikuDrawView *view;
    int width, height;
    bool inUse;
};

std::vector<PaintBuffer> gs_buffers;

bool AllocBuffer(PaintBuffer& buffer, int width, int height)
{
    // in steps, so that the buffer is not reallocated for every pixel a
    // window grows by
    width = (width + 127) / 128 * 128;
    height = (height + 127) / 128 * 128;

    buffer.bitmap = new BBitmap(wxHaikuRect(0, 0, width, height),
                                B_BITMAP_ACCEPTS_VIEWS, B_RGB32);
    if ( buffer.bitmap->InitCheck() != B_OK )
    {
        delete buffer.bitmap;
        buffer.bitmap = NULL;
        buffer.view = NULL;
        return false;
    }

    buffer.view = new wxHaikuDrawView(buffer.bitmap->Bounds(), "wxbuffer",
                                      B_FOLLOW_NONE, 0);
    buffer.bitmap->Lock();
    buffer.bitmap->AddChild(buffer.view);
    buffer.bitmap->Unlock();

    buffer.width = width;
    buffer.height = height;

    return true;
}

int AcquireBuffer(int width, int height)
{
    int freeIndex = -1;
    for ( size_t n = 0; n < gs_buffers.size(); n++ )
    {
        PaintBuffer& buffer = gs_buffers[n];
        if ( buffer.inUse )
            continue;

        if ( buffer.bitmap && buffer.width >= width && buffer.height >= height )
        {
            buffer.inUse = true;
            return (int)n;
        }

        freeIndex = (int)n;
    }

    if ( freeIndex == -1 )
    {
        PaintBuffer buffer = { NULL, NULL, 0, 0, false };
        gs_buffers.push_back(buffer);
        freeIndex = (int)gs_buffers.size() - 1;
    }

    PaintBuffer& buffer = gs_buffers[freeIndex];

    // replace the buffer which is too small by one which fits both
    width = wxMax(width, buffer.width);
    height = wxMax(height, buffer.height);

    delete buffer.bitmap; // deletes the view too
    buffer.bitmap = NULL;
    buffer.view = NULL;
    buffer.width = buffer.height = 0;

    if ( !AllocBuffer(buffer, width, height) )
        return -1;

    buffer.inUse = true;
    return freeIndex;
}

} // anonymous namespace

void wxHaikuFreePaintBuffers()
{
    for ( size_t n = 0; n < gs_buffers.size(); n++ )
        delete gs_buffers[n].bitmap;

    gs_buffers.clear();
}

// ============================================================================
// wxHaikuPaintSession
// ============================================================================

wxHaikuPaintSession::wxHaikuPaintSession(wxWindowHaiku *win,
                                         const wxRegion& region)
    : m_winHandle(win->GetHaikuHandle()),
      m_view(NULL),
      m_bitmap(NULL),
      m_bufferIndex(-1),
      m_region(region)
{
    m_box = region.GetBox();
    if ( m_box.IsEmpty() )
        return;

    m_bufferIndex = AcquireBuffer(m_box.width, m_box.height);
    if ( m_bufferIndex == -1 )
        return;

    PaintBuffer& buffer = gs_buffers[m_bufferIndex];
    m_bitmap = buffer.bitmap;
    m_view = buffer.view;
    m_offset = wxPoint(-m_box.x, -m_box.y);

    // What the handlers leave unpainted must not show what the previous
    // user of the buffer drew: start from the background colour.
    if ( m_bitmap->Lock() )
    {
        buffer.view->m_stateOwner = NULL;
        if ( buffer.view->m_transformed )
        {
            m_view->SetTransform(BAffineTransform());
            buffer.view->m_transformed = false;
        }

        wxColour bg = win->GetBackgroundColour();
        if ( !bg.IsOk() )
            bg = *wxWHITE;

        m_view->ConstrainClippingRegion(NULL);
        m_view->SetDrawingMode(B_OP_COPY);
        m_view->SetHighColor(bg.Red(), bg.Green(), bg.Blue());
        m_view->FillRect(wxHaikuRect(0, 0, m_box.width, m_box.height));

        m_bitmap->Unlock();
    }
}

wxHaikuPaintSession::~wxHaikuPaintSession()
{
    if ( m_bufferIndex == -1 )
        return;

    // the window may have been destroyed by a paint handler
    wxWindowHaiku * const win = wxWindowHaiku::HaikuFindByHandle(m_winHandle);
    BView * const target = win ? win->GetHaikuView() : NULL;

    if ( target && m_bitmap->Lock() )
    {
        // everything drawn must be in the bitmap before it is copied
        m_view->Sync();
        m_bitmap->Unlock();

        wxHaikuViewLock lock(target);
        if ( lock.IsLocked() )
        {
            wxHaikuDrawView * const
                view = dynamic_cast<wxHaikuDrawView *>(target);
            if ( view )
            {
                view->m_stateOwner = NULL;
                if ( view->m_transformed )
                {
                    target->SetTransform(BAffineTransform());
                    view->m_transformed = false;
                }
            }

            BRegion clip;
            if ( m_region.GetHaikuRegion() )
                clip = *m_region.GetHaikuRegion();

            target->ConstrainClippingRegion(&clip);
            target->SetDrawingMode(B_OP_COPY);

            // The synchronous call: app_server copies the pixels in the
            // thread of the window, the buffer must not be painted on again
            // before it has.
            target->DrawBitmap(m_bitmap,
                               wxHaikuRect(0, 0, m_box.width, m_box.height),
                               wxHaikuRect(m_box));

            target->ConstrainClippingRegion(NULL);
        }
    }

    gs_buffers[m_bufferIndex].inUse = false;
}

// ============================================================================
// wxWindowDCImpl
// ============================================================================

wxIMPLEMENT_ABSTRACT_CLASS(wxWindowDCImpl, wxHaikuDCImpl);

wxWindowDCImpl::wxWindowDCImpl(wxDC *owner)
    : wxHaikuDCImpl(owner),
      m_onScreen(false)
{
}

wxWindowDCImpl::wxWindowDCImpl(wxDC *owner, wxWindow *win)
    : wxHaikuDCImpl(owner),
      m_onScreen(false)
{
    wxCHECK_RET( win, wxS("invalid window") );

    InitForWin(win, wxRect(win->HaikuGetViewSize()), false);
}

void wxWindowDCImpl::InitForWin(wxWindow *win, const wxRect& rect,
                                bool WXUNUSED(clipToUpdate))
{
    m_window = win;

    const wxRect whole(win->HaikuGetViewSize());

    wxHaikuPaintSession * const session = win->HaikuGetPaintSession();
    if ( session && session->IsOk() )
    {
        // the window is being painted: draw into the buffer of the paint,
        // and only on what is being painted
        m_onScreen = false;

        const wxPoint offset = session->GetOffset();
        HaikuInit(session->GetView(), offset + rect.GetPosition(),
                  rect.GetSize(), false);

        wxRegion clip(session->GetRegion());
        clip.Intersect(rect);
        if ( !clip.IsEmpty() )
            clip.Offset(offset);
        else
            clip = wxRegion(0, 0, 0, 0);

        HaikuSetBaseClip(clip);
    }
    else
    {
        m_onScreen = true;

        HaikuInit(win->GetHaikuView(), rect.GetPosition(), rect.GetSize(),
                  false);

        if ( rect != whole )
        {
            wxRect r(rect);
            r.Intersect(whole);
            HaikuSetBaseClip(r.IsEmpty() ? wxRegion(0, 0, 0, 0) : wxRegion(r));
        }
    }

    // the attributes of the window
    const wxFont font = win->GetFont();
    if ( font.IsOk() )
        m_font = font;

    const wxColour fg = win->GetForegroundColour();
    if ( fg.IsOk() )
        m_textForegroundColour = fg;

    const wxColour bg = win->GetBackgroundColour();
    if ( bg.IsOk() )
    {
        m_textBackgroundColour = bg;
        m_backgroundBrush = wxBrush(bg);
    }
}

wxWindowDCImpl::~wxWindowDCImpl()
{
    // send what was drawn on the screen to app_server now
    if ( m_onScreen && m_isBBoxValid )
        Flush();
}

// ============================================================================
// wxClientDCImpl
// ============================================================================

wxIMPLEMENT_ABSTRACT_CLASS(wxClientDCImpl, wxWindowDCImpl);

wxClientDCImpl::wxClientDCImpl(wxDC *owner, wxWindow *win)
    : wxWindowDCImpl(owner)
{
    wxCHECK_RET( win, wxS("invalid window") );

    InitForWin(win, wxRect(win->GetClientAreaOrigin(), win->GetClientSize()),
               false);
}

// ============================================================================
// wxPaintDCImpl
// ============================================================================

wxIMPLEMENT_ABSTRACT_CLASS(wxPaintDCImpl, wxWindowDCImpl);

wxPaintDCImpl::wxPaintDCImpl(wxDC *owner, wxWindow *win)
    : wxWindowDCImpl(owner)
{
    wxCHECK_RET( win, wxS("invalid window") );

    InitForWin(win, wxRect(win->GetClientAreaOrigin(), win->GetClientSize()),
               true);
}
