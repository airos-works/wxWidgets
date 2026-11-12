/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/clipbrd.h
// Purpose:     wxClipboard on be_clipboard
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_CLIPBRD_H_
#define _WX_HAIKU_CLIPBRD_H_

#if wxUSE_CLIPBOARD

#include "wx/dataobj.h"

class WXDLLIMPEXP_CORE wxClipboard : public wxClipboardBase
{
public:
    wxClipboard();
    virtual ~wxClipboard();

    virtual bool Open() wxOVERRIDE;
    virtual void Close() wxOVERRIDE;
    virtual bool IsOpened() const wxOVERRIDE;

    virtual bool SetData(wxDataObject *data) wxOVERRIDE;
    virtual bool AddData(wxDataObject *data) wxOVERRIDE;
    virtual bool IsSupported(const wxDataFormat& format) wxOVERRIDE;
    virtual bool GetData(wxDataObject& data) wxOVERRIDE;
    virtual void Clear() wxOVERRIDE;

    // the data is copied to the system clipboard when it is set: nothing is
    // lost when the application exits
    virtual bool Flush() wxOVERRIDE { return true; }

private:
    bool m_open;

    wxDECLARE_DYNAMIC_CLASS(wxClipboard);
};

#endif // wxUSE_CLIPBOARD

#endif // _WX_HAIKU_CLIPBRD_H_
