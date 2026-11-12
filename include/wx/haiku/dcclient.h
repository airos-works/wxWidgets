/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dcclient.h
// Purpose:     wxWindowDCImpl, wxClientDCImpl and wxPaintDCImpl
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DCCLIENT_H_
#define _WX_HAIKU_DCCLIENT_H_

#include "wx/haiku/dc.h"

class WXDLLIMPEXP_FWD_CORE wxWindow;

// A DC on the whole window (including the non-client area wxUniv draws).
// While the window is being painted (wxWindowHaiku::HaikuPaint()) all DCs
// created on it draw into the off-screen buffer of the paint which is shown
// at once when the paint is over; otherwise they draw on the screen.
class WXDLLIMPEXP_CORE wxWindowDCImpl : public wxHaikuDCImpl
{
public:
    wxWindowDCImpl(wxDC *owner);
    wxWindowDCImpl(wxDC *owner, wxWindow *win);
    virtual ~wxWindowDCImpl();

protected:
    // rect: the part of the window (window coordinates) the DC covers; its
    // top left corner is the device origin
    void InitForWin(wxWindow *win, const wxRect& rect, bool clipToUpdate);

    bool m_onScreen;

    wxDECLARE_DYNAMIC_CLASS(wxWindowDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxWindowDCImpl);
};

class WXDLLIMPEXP_CORE wxClientDCImpl : public wxWindowDCImpl
{
public:
    wxClientDCImpl(wxDC *owner) : wxWindowDCImpl(owner) { }
    wxClientDCImpl(wxDC *owner, wxWindow *win);

    wxDECLARE_DYNAMIC_CLASS(wxClientDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxClientDCImpl);
};

class WXDLLIMPEXP_CORE wxPaintDCImpl : public wxWindowDCImpl
{
public:
    wxPaintDCImpl(wxDC *owner) : wxWindowDCImpl(owner) { }
    wxPaintDCImpl(wxDC *owner, wxWindow *win);

    wxDECLARE_DYNAMIC_CLASS(wxPaintDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxPaintDCImpl);
};

#endif // _WX_HAIKU_DCCLIENT_H_
