/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/toplevel.h
// Purpose:     wxTopLevelWindowHaiku: a wxTopLevelWindow on a BWindow
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_TOPLEVEL_H_
#define _WX_HAIKU_TOPLEVEL_H_

class WXDLLIMPEXP_CORE wxTopLevelWindowHaiku : public wxTopLevelWindowBase
{
public:
    wxTopLevelWindowHaiku() { Init(); }
    wxTopLevelWindowHaiku(wxWindow *parent,
                          wxWindowID id,
                          const wxString& title,
                          const wxPoint& pos = wxDefaultPosition,
                          const wxSize& size = wxDefaultSize,
                          long style = wxDEFAULT_FRAME_STYLE,
                          const wxString& name = wxASCII_STR(wxFrameNameStr))
    {
        Init();
        Create(parent, id, title, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxASCII_STR(wxFrameNameStr));

    virtual ~wxTopLevelWindowHaiku();

    // implement base class pure virtuals
    virtual void Maximize(bool maximize = true) wxOVERRIDE;
    virtual bool IsMaximized() const wxOVERRIDE;
    virtual void Iconize(bool iconize = true) wxOVERRIDE;
    virtual bool IsIconized() const wxOVERRIDE;
    virtual void Restore() wxOVERRIDE;

    virtual bool ShowFullScreen(bool show, long style = wxFULLSCREEN_ALL) wxOVERRIDE;
    virtual bool IsFullScreen() const wxOVERRIDE { return m_fsIsShowing; }

    virtual void SetTitle(const wxString& title) wxOVERRIDE;
    virtual wxString GetTitle() const wxOVERRIDE { return m_title; }

    virtual void SetIcons(const wxIconBundle& icons) wxOVERRIDE;

    virtual bool IsActive() wxOVERRIDE { return m_isActive; }
    virtual void RequestUserAttention(int flags = wxUSER_ATTENTION_INFO) wxOVERRIDE;

    virtual void SetWindowStyleFlag(long style) wxOVERRIDE;

    virtual bool Show(bool show = true) wxOVERRIDE;
    virtual void ShowWithoutActivating() wxOVERRIDE;

    virtual bool EnableCloseButton(bool enable = true) wxOVERRIDE;

    virtual void HaikuHandleEvent(const wxHaikuEvent& event) wxOVERRIDE;

protected:
    // common part of all ctors
    void Init();

    virtual void HaikuGetWindowType(int *look, int *feel,
                                    wxUint32 *flags) const wxOVERRIDE;
    virtual void HaikuActivated(bool active) wxOVERRIDE;

    wxString m_title;

    bool m_fsIsShowing;
    long m_fsSaveStyle;
    wxRect m_fsSaveFrame;

    bool m_isMaximized;
    wxRect m_savedFrame;

    bool m_isIconized;
    bool m_closeEnabled;
    bool m_showInactive;
};

#endif // _WX_HAIKU_TOPLEVEL_H_
