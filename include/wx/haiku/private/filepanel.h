/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/private/filepanel.h
// Purpose:     the BFilePanel behind wxFileDialog and wxDirDialog
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_PRIVATE_FILEPANEL_H_
#define _WX_HAIKU_PRIVATE_FILEPANEL_H_

#include "wx/arrstr.h"
#include "wx/haiku/private.h"

class WXDLLIMPEXP_FWD_BASE wxEventLoopBase;
class WXDLLIMPEXP_FWD_CORE wxWindow;

// Shows a Tracker file panel application modally from the wx thread: the
// panel lives in its own window thread and reports what happens to it with
// event records, ShowModal() runs a nested event loop until it is closed.
class wxHaikuFilePanel : public wxHaikuCallbackTarget
{
public:
    enum Mode
    {
        Mode_Open,
        Mode_Save,
        Mode_Dir        // choose directories instead of files
    };

    explicit wxHaikuFilePanel(Mode mode);
    virtual ~wxHaikuFilePanel();

    void SetTitle(const wxString& title) { m_title = title; }
    void SetDirectory(const wxString& dir) { m_dir = dir; }
    void SetFileName(const wxString& name) { m_name = name; }
    void SetMultiple(bool multiple) { m_multiple = multiple; }

    // The filters as wxParseCommonDialogsFilter() returns them ("*.a;*.b"
    // for each description) and the one to start with. With more than one
    // the panel gets a menu to choose from.
    void SetFilters(const wxArrayString& descriptions,
                    const wxArrayString& patterns,
                    int index);

    // Returns true if the user chose something, false if the panel was
    // closed. The panel is centred on the top level window of parent.
    bool ShowModal(wxWindow *parent);

    // Valid after ShowModal(): full paths, and the filter chosen last
    const wxArrayString& GetPaths() const { return m_paths; }
    int GetFilterIndex() const { return m_filterIndex; }

    // the first extension without wildcards of the filter ("txt" for
    // "*.txt;*.text"), empty if it has none
    wxString GetDefaultExtension(int filter) const;

    virtual void HaikuCallback(const wxHaikuEvent& event) wxOVERRIDE;

private:
    Mode m_mode;
    wxString m_title, m_dir, m_name;
    bool m_multiple;

    wxArrayString m_filterDescriptions, m_filterPatterns;
    int m_filterIndex;

    wxArrayString m_paths;
    bool m_chosen;
    wxEventLoopBase *m_loop;

    wxDECLARE_NO_COPY_CLASS(wxHaikuFilePanel);
};

#endif // _WX_HAIKU_PRIVATE_FILEPANEL_H_
