/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/webviewhistoryitem.h
// Purpose:     wxWebViewHistoryItem for the Haiku port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_WEBVIEWHISTORYITEM_H_
#define _WX_HAIKU_WEBVIEWHISTORYITEM_H_

#include "wx/setup.h"

#if wxUSE_WEBVIEW && defined(__WXHAIKU__)

// Part of the web view seam (see src/haiku/webview_haiku.cpp): the class does
// not depend on the engine. A backend identifies the entry of its engine's
// back/forward list with the opaque value.
class WXDLLIMPEXP_WEBVIEW wxWebViewHistoryItem
{
public:
    wxWebViewHistoryItem(const wxString& url, const wxString& title)
        : m_url(url), m_title(title), m_item(0) { }

    wxString GetUrl() { return m_url; }
    wxString GetTitle() { return m_title; }

    // for the backend
    void HaikuSetItem(wxUint64 item) { m_item = item; }
    wxUint64 HaikuGetItem() const { return m_item; }

private:
    wxString m_url, m_title;
    wxUint64 m_item;
};

#endif // wxUSE_WEBVIEW && defined(__WXHAIKU__)

#endif // _WX_HAIKU_WEBVIEWHISTORYITEM_H_
