/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dcmemory.h
// Purpose:     wxMemoryDCImpl: drawing on a wxBitmap
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DCMEMORY_H_
#define _WX_HAIKU_DCMEMORY_H_

#include "wx/haiku/dc.h"
#include "wx/bitmap.h"

class WXDLLIMPEXP_CORE wxMemoryDCImpl : public wxHaikuDCImpl
{
public:
    wxMemoryDCImpl(wxMemoryDC *owner);
    wxMemoryDCImpl(wxMemoryDC *owner, wxBitmap& bitmap);
    wxMemoryDCImpl(wxMemoryDC *owner, wxDC *dc); // create compatible DC
    virtual ~wxMemoryDCImpl();

    // override wxMemoryDC-specific base class virtual methods
    virtual const wxBitmap& GetSelectedBitmap() const wxOVERRIDE { return m_bmp; }
    virtual wxBitmap& GetSelectedBitmap() wxOVERRIDE { return m_bmp; }
    virtual void DoSelect(const wxBitmap& bitmap) wxOVERRIDE;

    virtual wxBitmap DoGetAsBitmap(const wxRect *subrect) const wxOVERRIDE;

    virtual const wxBitmap *HaikuGetSourceBitmap(wxPoint *origin) const wxOVERRIDE;
    virtual bool HaikuClearTranslucent(const wxColour& colour) wxOVERRIDE;
    virtual void HaikuTargetChanged() wxOVERRIDE;

private:
    void Init();

    wxBitmap m_bmp;

    wxDECLARE_DYNAMIC_CLASS(wxMemoryDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxMemoryDCImpl);
};

#endif // _WX_HAIKU_DCMEMORY_H_
