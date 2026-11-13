/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dirdlg.h
// Purpose:     wxDirDialog on the system's file panel (BFilePanel)
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DIRDLG_H_
#define _WX_HAIKU_DIRDLG_H_

// Tracker's file panel showing directories only, with a "Select" button
// which returns the selected directory (or directories with wxDD_MULTIPLE)
// or, without a selection, the directory shown. As wxFileDialog this object
// is never created as a wx window.
//
// The panel only returns existing directories (wxDD_DIR_MUST_EXIST is always
// in effect); new ones can be made with its "New folder" menu item.
class WXDLLIMPEXP_CORE wxDirDialog : public wxDirDialogBase
{
public:
    wxDirDialog() { m_isShowingPanel = false; }

    wxDirDialog(wxWindow *parent,
                const wxString& message = wxASCII_STR(wxDirSelectorPromptStr),
                const wxString& defaultPath = wxEmptyString,
                long style = wxDD_DEFAULT_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                const wxString& name = wxASCII_STR(wxDirDialogNameStr))
    {
        m_isShowingPanel = false;
        Create(parent, message, defaultPath, style, pos, size, name);
    }

    bool Create(wxWindow *parent,
                const wxString& message = wxASCII_STR(wxDirSelectorPromptStr),
                const wxString& defaultPath = wxEmptyString,
                long style = wxDD_DEFAULT_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                const wxString& name = wxASCII_STR(wxDirDialogNameStr));

    virtual int ShowModal() wxOVERRIDE;
    virtual bool IsModal() const wxOVERRIDE { return m_isShowingPanel; }

    // there is no wx window to show
    virtual bool Show(bool show = true) wxOVERRIDE;

private:
    bool m_isShowingPanel;

    wxDECLARE_DYNAMIC_CLASS(wxDirDialog);
};

#endif // _WX_HAIKU_DIRDLG_H_
