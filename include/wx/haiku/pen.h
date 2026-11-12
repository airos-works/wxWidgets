/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/pen.h
// Purpose:     wxPen for the native Haiku port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_PEN_H_
#define _WX_HAIKU_PEN_H_

#include "wx/gdiobj.h"
#include "wx/gdicmn.h"

class WXDLLIMPEXP_FWD_CORE wxBitmap;

class WXDLLIMPEXP_CORE wxPen : public wxPenBase
{
public:
    wxPen() { }
    wxPen(const wxColour& colour, int width = 1, wxPenStyle style = wxPENSTYLE_SOLID);
    wxPen(const wxBitmap& stipple, int width);
    wxPen(const wxPenInfo& info);

    bool operator==(const wxPen& pen) const;
    bool operator!=(const wxPen& pen) const { return !(*this == pen); }

    virtual void SetColour(const wxColour& colour) wxOVERRIDE;
    virtual void SetColour(unsigned char red, unsigned char green, unsigned char blue) wxOVERRIDE;
    virtual void SetCap(wxPenCap capStyle) wxOVERRIDE;
    virtual void SetJoin(wxPenJoin joinStyle) wxOVERRIDE;
    virtual void SetStyle(wxPenStyle style) wxOVERRIDE;
    virtual void SetWidth(int width) wxOVERRIDE;
    virtual void SetDashes(int number_of_dashes, const wxDash *dash) wxOVERRIDE;
    virtual void SetStipple(const wxBitmap& stipple) wxOVERRIDE;

    virtual wxColour GetColour() const wxOVERRIDE;
    virtual wxPenCap GetCap() const wxOVERRIDE;
    virtual wxPenJoin GetJoin() const wxOVERRIDE;
    virtual wxPenStyle GetStyle() const wxOVERRIDE;
    virtual int GetWidth() const wxOVERRIDE;
    virtual int GetDashes(wxDash **ptr) const wxOVERRIDE;
    int GetDashCount() const;
    wxDash* GetDash() const;
    virtual wxBitmap *GetStipple() const wxOVERRIDE;

    wxDEPRECATED_MSG("use wxPENSTYLE_XXX constants")
    wxPen(const wxColour& col, int width, int style);

    wxDEPRECATED_MSG("use wxPENSTYLE_XXX constants")
    void SetStyle(int style) { SetStyle((wxPenStyle)style); }

protected:
    virtual wxGDIRefData *CreateGDIRefData() const wxOVERRIDE;
    virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const wxOVERRIDE;

    wxDECLARE_DYNAMIC_CLASS(wxPen);
};

#endif // _WX_HAIKU_PEN_H_
