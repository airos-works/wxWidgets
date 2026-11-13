/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/taskbar.h
// Purpose:     wxTaskBarIcon for the Haiku port: a placeholder
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_TASKBAR_H_
#define _WX_HAIKU_TASKBAR_H_

// The class exists so that code using wxTaskBarIcon compiles and links. It
// shows nothing: a Deskbar replicant lives in the Deskbar's address space and
// needs an add-on of its own, which a static wx library cannot provide.
// IsAvailable() returns false, IsOk() and IsIconInstalled() too.
class WXDLLIMPEXP_ADV wxTaskBarIcon : public wxTaskBarIconBase
{
public:
    wxTaskBarIcon(wxTaskBarIconType iconType = wxTBI_DEFAULT_TYPE);
    virtual ~wxTaskBarIcon();

    // Accessors
    bool IsOk() const { return false; }
    bool IsIconInstalled() const { return false; }

    // Operations: all of them fail
    virtual bool SetIcon(const wxIcon& icon,
                         const wxString& tooltip = wxEmptyString) wxOVERRIDE;
    virtual bool RemoveIcon() wxOVERRIDE;
    virtual bool PopupMenu(wxMenu *menu) wxOVERRIDE;

private:
    wxDECLARE_DYNAMIC_CLASS_NO_COPY(wxTaskBarIcon);
};

#endif // _WX_HAIKU_TASKBAR_H_
