/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dirdlg.cpp
// Purpose:     wxDirDialog on the system's file panel (BFilePanel)
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_DIRDLG

#include "wx/dirdlg.h"

#ifndef WX_PRECOMP
    #include "wx/utils.h"
#endif

#include "wx/filename.h"
#include "wx/modalhook.h"

#include "wx/haiku/private/filepanel.h"

wxIMPLEMENT_DYNAMIC_CLASS(wxDirDialog, wxDialog);

bool wxDirDialog::Create(wxWindow *parent,
                         const wxString& message,
                         const wxString& defaultPath,
                         long style,
                         const wxPoint& WXUNUSED(pos),
                         const wxSize& WXUNUSED(size),
                         const wxString& WXUNUSED(name))
{
    // only the parameters are stored: no wx window is created, the panel is
    // made when it is shown
    m_parent = parent;
    m_message = message;
    m_path = defaultPath;
    m_windowStyle = style;

    return true;
}

bool wxDirDialog::Show(bool show)
{
    // nothing to show or hide: only ShowModal() brings up the panel
    wxUnusedVar(show);
    return false;
}

int wxDirDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();

    wxCHECK_MSG( !m_isShowingPanel, wxID_CANCEL,
                 "wxDirDialog::ShowModal() called twice" );

    wxHaikuFilePanel panel(wxHaikuFilePanel::Mode_Dir);
    panel.SetTitle(m_message);
    panel.SetMultiple(HasFlag(wxDD_MULTIPLE));

    // start in the default directory: the user selects it right away or one
    // of its neighbours by going up once
    if ( !m_path.empty() )
    {
        wxString dir = m_path;
        while ( dir.length() > 1 && dir.Last() == '/' )
            dir.RemoveLast();
        panel.SetDirectory(dir);
    }

    m_isShowingPanel = true;
    const bool chosen = panel.ShowModal(GetParent());
    m_isShowingPanel = false;

    if ( !chosen )
    {
        SetReturnCode(wxID_CANCEL);
        return wxID_CANCEL;
    }

    m_paths = panel.GetPaths();
    m_path = m_paths[0];

    if ( HasFlag(wxDD_CHANGE_DIR) )
        wxSetWorkingDirectory(m_path);

    SetReturnCode(wxID_OK);
    return wxID_OK;
}

#endif // wxUSE_DIRDLG
