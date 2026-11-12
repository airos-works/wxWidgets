/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dcscreen.h
// Purpose:     wxScreenDCImpl
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DCSCREEN_H_
#define _WX_HAIKU_DCSCREEN_H_

#include "wx/haiku/dc.h"
#include "wx/bitmap.h"

// app_server does not let applications draw on the screen outside of their
// windows: this DC is a snapshot of the screen taken when it is created,
// which is what the DC is used for in practice (Blit() from it, GetPixel()).
// Drawing on it changes the snapshot only.
class WXDLLIMPEXP_CORE wxScreenDCImpl : public wxHaikuDCImpl
{
public:
    wxScreenDCImpl(wxScreenDC *owner);
    virtual ~wxScreenDCImpl();

    virtual bool IsOk() const wxOVERRIDE { return true; }

    virtual const wxBitmap *HaikuGetSourceBitmap(wxPoint *origin) const wxOVERRIDE;

private:
    mutable wxBitmap m_snapshot;

    wxDECLARE_DYNAMIC_CLASS(wxScreenDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxScreenDCImpl);
};

#endif // _WX_HAIKU_DCSCREEN_H_
