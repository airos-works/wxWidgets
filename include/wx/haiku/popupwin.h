/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/popupwin.h
// Purpose:     wxPopupWindow: a borderless BWindow which never takes focus
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_POPUPWIN_H_
#define _WX_HAIKU_POPUPWIN_H_

class WXDLLIMPEXP_CORE wxPopupWindow : public wxPopupWindowBase
{
public:
    wxPopupWindow() { }

    wxPopupWindow(wxWindow *parent, int flags = wxBORDER_NONE)
        { Create(parent, flags); }

    bool Create(wxWindow *parent, int flags = wxBORDER_NONE);

    virtual bool HaikuAvoidsFocus() const wxOVERRIDE { return true; }

    // popups are shown and hidden often and most are hidden most of the
    // time: one hidden for a while gives its BWindow back
    virtual bool HaikuCanUnrealize() const wxOVERRIDE { return true; }

protected:
    virtual void HaikuGetWindowType(int *look, int *feel,
                                    wxUint32 *flags) const wxOVERRIDE;

    wxDECLARE_DYNAMIC_CLASS(wxPopupWindow);
};

#endif // _WX_HAIKU_POPUPWIN_H_
