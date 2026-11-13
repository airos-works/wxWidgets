/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/mediactrl.cpp
// Purpose:     wxMediaCtrl backend of the Haiku port: a placeholder
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_MEDIACTRL

#include "wx/mediactrl.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
#endif

#include "wx/uri.h"

// ----------------------------------------------------------------------------
// wxHaikuMediaBackend (wxMEDIABACKEND_HAIKU)
// ----------------------------------------------------------------------------

// PLACEHOLDER: the backend creates the control, so that a wxMediaCtrl can be
// created, laid out and destroyed like on the other platforms, but it plays
// nothing: Load() fails, the state stays wxMEDIASTATE_STOPPED and no media
// events are sent. A real backend would decode with the Media Kit
// (BMediaFile/BMediaTrack) or the system's ffmpeg and show the frames in the
// control's view.
class WXDLLIMPEXP_MEDIA wxHaikuMediaBackend : public wxMediaBackendCommonBase
{
public:
    wxHaikuMediaBackend() { }

    virtual bool CreateControl(wxControl* ctrl, wxWindow* parent,
                               wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style,
                               const wxValidator& validator,
                               const wxString& name) wxOVERRIDE;

    virtual bool Load(const wxString& fileName) wxOVERRIDE;
    virtual bool Load(const wxURI& location) wxOVERRIDE;
    virtual bool Load(const wxURI& location, const wxURI& proxy) wxOVERRIDE;

    virtual wxMediaState GetState() wxOVERRIDE { return wxMEDIASTATE_STOPPED; }

    virtual void Move(int WXUNUSED(x), int WXUNUSED(y),
                      int WXUNUSED(w), int WXUNUSED(h)) wxOVERRIDE { }

    virtual wxSize GetVideoSize() const wxOVERRIDE { return wxSize(0, 0); }

private:
    wxDECLARE_DYNAMIC_CLASS(wxHaikuMediaBackend);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxHaikuMediaBackend, wxMediaBackend);

bool wxHaikuMediaBackend::CreateControl(wxControl* ctrl, wxWindow* parent,
                                        wxWindowID id,
                                        const wxPoint& pos,
                                        const wxSize& size,
                                        long style,
                                        const wxValidator& validator,
                                        const wxString& name)
{
    m_ctrl = wxStaticCast(ctrl, wxMediaCtrl);

    return m_ctrl->wxControl::Create(parent, id, pos, size,
                                     (style & ~wxBORDER_MASK) | wxBORDER_NONE,
                                     validator, name);
}

bool wxHaikuMediaBackend::Load(const wxString& fileName)
{
    wxLogTrace("mediactrl",
               "wxMediaCtrl cannot play \"%s\": no playback in the Haiku backend",
               fileName);
    wxUnusedVar(fileName);
    return false;
}

bool wxHaikuMediaBackend::Load(const wxURI& location)
{
    return Load(location.BuildURI());
}

bool wxHaikuMediaBackend::Load(const wxURI& location,
                               const wxURI& WXUNUSED(proxy))
{
    return Load(location.BuildURI());
}

// Force link into main library so this backend can be loaded
#include "wx/html/forcelnk.h"
FORCE_LINK_ME(basewxmediabackends)

#endif // wxUSE_MEDIACTRL
