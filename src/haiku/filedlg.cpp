/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/filedlg.cpp
// Purpose:     wxFileDialog on the system's file panel (BFilePanel)
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_FILEDLG

#include "wx/filedlg.h"

#ifndef WX_PRECOMP
    #include "wx/intl.h"
    #include "wx/msgdlg.h"
    #include "wx/utils.h"
#endif

#include "wx/filename.h"
#include "wx/modalhook.h"

#include "wx/haiku/private/filepanel.h"

wxIMPLEMENT_DYNAMIC_CLASS(wxFileDialog, wxFileDialogBase);

bool wxFileDialog::Create(wxWindow *parent,
                          const wxString& message,
                          const wxString& defaultDir,
                          const wxString& defaultFile,
                          const wxString& wildCard,
                          long style,
                          const wxPoint& pos,
                          const wxSize& sz,
                          const wxString& name)
{
    // Only the parameters are stored: no wx window is created, the panel is
    // made when it is shown.
    if ( !wxFileDialogBase::Create(parent, message, defaultDir, defaultFile,
                                   wildCard, style, pos, sz, name) )
        return false;

    // a default file given with its directory
    if ( m_dir.empty() && !m_fileName.empty() )
    {
        const wxFileName fn(m_fileName);
        if ( fn.IsAbsolute() )
        {
            m_dir = fn.GetPath();
            m_fileName = fn.GetFullName();
        }
    }

    if ( !m_dir.empty() && !m_fileName.empty() )
        m_path = wxFileName(m_dir, m_fileName).GetFullPath();

    return true;
}

bool wxFileDialog::Show(bool show)
{
    // nothing to show or hide: only ShowModal() brings up the panel
    wxUnusedVar(show);
    return false;
}

int wxFileDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();

    wxCHECK_MSG( !m_isShowingPanel, wxID_CANCEL,
                 "wxFileDialog::ShowModal() called twice" );

    const bool save = HasFdFlag(wxFD_SAVE);

    wxArrayString descriptions, patterns;
    wxParseCommonDialogsFilter(m_wildCard, descriptions, patterns);

    wxString dir = m_dir,
             name = m_fileName;

    m_paths.clear();

    for ( ;; )
    {
        wxHaikuFilePanel panel(save ? wxHaikuFilePanel::Mode_Save
                                    : wxHaikuFilePanel::Mode_Open);
        panel.SetTitle(m_message);
        panel.SetDirectory(dir);
        panel.SetFileName(name);
        panel.SetMultiple(!save && HasFdFlag(wxFD_MULTIPLE));
        panel.SetFilters(descriptions, patterns, m_filterIndex);

        m_isShowingPanel = true;
        const bool chosen = panel.ShowModal(GetParent());
        m_isShowingPanel = false;

        m_filterIndex =
        m_currentlySelectedFilterIndex = panel.GetFilterIndex();

        if ( !chosen )
        {
            SetReturnCode(wxID_CANCEL);
            return wxID_CANCEL;
        }

        m_paths = panel.GetPaths();

        if ( save )
        {
            // The panel returns the name as typed. Without extension it gets
            // the one of the filter in use, as in the other ports.
            wxFileName fn(m_paths[0]);
            const wxString ext = panel.GetDefaultExtension(m_filterIndex);
            if ( !fn.HasExt() && !ext.empty() )
            {
                fn.SetExt(ext);

                // The panel asked about replacing the file it was given the
                // name of. The name changed: ask about the one really used.
                if ( fn.FileExists() && HasFdFlag(wxFD_OVERWRITE_PROMPT) )
                {
                    const wxString msg = wxString::Format(
                        _("File '%s' already exists, do you really want to overwrite it?"),
                        fn.GetFullPath());

                    if ( wxMessageBox(msg, _("Confirm"),
                                      wxYES_NO | wxICON_QUESTION,
                                      GetParent()) != wxYES )
                    {
                        // back to the panel, where the user left it
                        dir = fn.GetPath();
                        name = fn.GetFullName();
                        continue;
                    }
                }

                m_paths[0] = fn.GetFullPath();
            }
        }

        break;
    }

    m_path = m_paths[0];

    const wxFileName fn(m_path);
    m_dir = fn.GetPath();
    m_fileName = fn.GetFullName();
    m_currentlySelectedFilename = m_path;

    if ( HasFdFlag(wxFD_CHANGE_DIR) )
        wxSetWorkingDirectory(m_dir);

    SetReturnCode(wxID_OK);
    return wxID_OK;
}

void wxFileDialog::GetPaths(wxArrayString& paths) const
{
    paths = m_paths;

    // before the dialog was shown: the default, as the base class does
    if ( paths.empty() && !m_path.empty() )
        paths.push_back(m_path);
}

void wxFileDialog::GetFilenames(wxArrayString& files) const
{
    files.clear();

    for ( size_t n = 0; n < m_paths.size(); n++ )
        files.push_back(wxFileName(m_paths[n]).GetFullName());

    if ( files.empty() && !m_fileName.empty() )
        files.push_back(m_fileName);
}

#endif // wxUSE_FILEDLG
