/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/brush.h
// Purpose:     wxBrush for the native Haiku port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_BRUSH_H_
#define _WX_HAIKU_BRUSH_H_

#include "wx/gdiobj.h"
#include "wx/bitmap.h"

class WXDLLIMPEXP_CORE wxBrush : public wxBrushBase
{
public:
    wxBrush() { }
    wxBrush(const wxColour& colour, wxBrushStyle style = wxBRUSHSTYLE_SOLID);
    wxBrush(const wxBitmap& stippleBitmap);

    bool operator==(const wxBrush& brush) const;
    bool operator!=(const wxBrush& brush) const { return !(*this == brush); }

    virtual wxBrushStyle GetStyle() const wxOVERRIDE;
    virtual wxColour GetColour() const wxOVERRIDE;
    virtual wxBitmap *GetStipple() const wxOVERRIDE;

    virtual void SetColour(const wxColour& col) wxOVERRIDE;
    virtual void SetColour(unsigned char r, unsigned char g, unsigned char b) wxOVERRIDE;
    virtual void SetStyle(wxBrushStyle style) wxOVERRIDE;
    virtual void SetStipple(const wxBitmap& stipple) wxOVERRIDE;

    wxDEPRECATED_MSG("use wxBRUSHSTYLE_XXX constants")
    wxBrush(const wxColour& col, int style);

    wxDEPRECATED_MSG("use wxBRUSHSTYLE_XXX constants")
    void SetStyle(int style) { SetStyle((wxBrushStyle)style); }

protected:
    virtual wxGDIRefData *CreateGDIRefData() const wxOVERRIDE;
    virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const wxOVERRIDE;

    wxDECLARE_DYNAMIC_CLASS(wxBrush);
};

#endif // _WX_HAIKU_BRUSH_H_
