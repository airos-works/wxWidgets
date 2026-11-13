/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/filedlg.h
// Purpose:     wxFileDialog on the system's file panel (BFilePanel)
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_FILEDLG_H_
#define _WX_HAIKU_FILEDLG_H_

// The dialog is Tracker's file panel, a window of its own shown application
// modally: this object is never created as a window, only ShowModal() and
// the accessors do something.
//
// Notes:
//  - With several filters ("Text (*.txt)|*.txt|All files|*.*") the panel has
//    a menu to choose from; GetFilterIndex() returns the choice.
//  - The save panel asks before replacing a file whether or not
//    wxFD_OVERWRITE_PROMPT is given, and the dialog does not ask again.
//  - A name typed without extension gets the one of the chosen filter when
//    saving.
//  - The open panel only returns existing files: wxFD_FILE_MUST_EXIST is
//    always in effect.
class WXDLLIMPEXP_CORE wxFileDialog : public wxFileDialogBase
{
public:
    wxFileDialog() { m_isShowingPanel = false; }
    wxFileDialog(wxWindow *parent,
                 const wxString& message = wxASCII_STR(wxFileSelectorPromptStr),
                 const wxString& defaultDir = wxEmptyString,
                 const wxString& defaultFile = wxEmptyString,
                 const wxString& wildCard = wxASCII_STR(wxFileSelectorDefaultWildcardStr),
                 long style = wxFD_DEFAULT_STYLE,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& sz = wxDefaultSize,
                 const wxString& name = wxASCII_STR(wxFileDialogNameStr))
    {
        m_isShowingPanel = false;
        Create(parent, message, defaultDir, defaultFile, wildCard, style,
               pos, sz, name);
    }

    bool Create(wxWindow *parent,
                const wxString& message = wxASCII_STR(wxFileSelectorPromptStr),
                const wxString& defaultDir = wxEmptyString,
                const wxString& defaultFile = wxEmptyString,
                const wxString& wildCard = wxASCII_STR(wxFileSelectorDefaultWildcardStr),
                long style = wxFD_DEFAULT_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& sz = wxDefaultSize,
                const wxString& name = wxASCII_STR(wxFileDialogNameStr));

    virtual int ShowModal() wxOVERRIDE;
    virtual bool IsModal() const wxOVERRIDE { return m_isShowingPanel; }

    virtual void GetPaths(wxArrayString& paths) const wxOVERRIDE;
    virtual void GetFilenames(wxArrayString& files) const wxOVERRIDE;

    // there is no wx window to show
    virtual bool Show(bool show = true) wxOVERRIDE;

private:
    wxArrayString m_paths;
    bool m_isShowingPanel;

    wxDECLARE_DYNAMIC_CLASS(wxFileDialog);
};

#endif // _WX_HAIKU_FILEDLG_H_
