/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/cursor.h
// Purpose:     wxCursor on BCursor
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_CURSOR_H_
#define _WX_HAIKU_CURSOR_H_

#include "wx/gdiobj.h"
#include "wx/gdicmn.h"

class BCursor;
class WXDLLIMPEXP_FWD_CORE wxImage;

class WXDLLIMPEXP_CORE wxCursor : public wxCursorBase
{
public:
    wxCursor() { }
    wxCursor(wxStockCursor id) { InitFromStock(id); }
#if WXWIN_COMPATIBILITY_2_8
    wxCursor(int id) { InitFromStock((wxStockCursor)id); }
#endif
#if wxUSE_IMAGE
    wxCursor(const wxImage& image);
    wxCursor(const char* const* xpmData);
#endif
    wxCursor(const wxString& name,
             wxBitmapType type = wxCURSOR_DEFAULT_TYPE,
             int hotSpotX = 0, int hotSpotY = 0);

    virtual wxPoint GetHotSpot() const wxOVERRIDE;

    // the native cursor (NULL for an invalid cursor or wxCURSOR_BLANK...)
    const BCursor *GetHaikuCursor() const;

protected:
    void InitFromStock(wxStockCursor id);
#if wxUSE_IMAGE
    void InitFromImage(const wxImage& image);
#endif

    virtual wxGDIRefData *CreateGDIRefData() const wxOVERRIDE;
    virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const wxOVERRIDE;

    wxDECLARE_DYNAMIC_CLASS(wxCursor);
};

#endif // _WX_HAIKU_CURSOR_H_
