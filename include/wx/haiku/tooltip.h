/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/tooltip.h
// Purpose:     wxToolTip on the tool tips of BView
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_TOOLTIP_H_
#define _WX_HAIKU_TOOLTIP_H_

#include "wx/string.h"
#include "wx/object.h"

class WXDLLIMPEXP_FWD_CORE wxWindow;

class WXDLLIMPEXP_CORE wxToolTip : public wxObject
{
public:
    wxToolTip(const wxString& tip);

    // globally change the tooltip parameters
    static void Enable(bool flag);
    static void SetDelay(long msecs);
    static void SetAutoPop(long msecs);
    static void SetReshow(long msecs);

    // get/set the tooltip text
    void SetTip(const wxString& tip);
    wxString GetTip() const { return m_text; }

    wxWindow *GetWindow() const { return m_window; }

    // implementation
    void SetWindow(wxWindow *win);

    // tool tips are globally enabled
    static bool HaikuIsEnabled();

private:
    void Apply();

    wxString m_text;
    wxWindow *m_window;

    wxDECLARE_ABSTRACT_CLASS(wxToolTip);
};

#endif // _WX_HAIKU_TOOLTIP_H_
