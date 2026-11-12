/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dataform.h
// Purpose:     wxDataFormat for the native Haiku port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DATAFORM_H_
#define _WX_HAIKU_DATAFORM_H_

// The formats are the MIME types the data has in the BMessages of the
// clipboard and of drag and drop ("text/plain", "image/png"...); file names
// travel as entry_refs in the "refs" field, as Tracker does it.
class WXDLLIMPEXP_CORE wxDataFormat
{
public:
    wxDataFormat(wxDataFormatId formatId = wxDF_INVALID);
    wxDataFormat(const wxString& id);
    wxDataFormat(const char *id);
    wxDataFormat(const wchar_t *id);

    const wxString& GetId() const { return m_mimeType; }
    void SetId(const wxString& id);

    wxDataFormatId GetType() const { return m_formatId; }
    void SetType(wxDataFormatId type);

    bool operator==(wxDataFormatId format) const;
    bool operator!=(wxDataFormatId format) const
        { return !(*this == format); }
    bool operator==(const wxDataFormat& format) const;
    bool operator!=(const wxDataFormat& format) const
        { return !(*this == format); }

    // the MIME type, also for the standard formats
    const wxString& GetMimeType() const { return m_mimeType; }

private:
    wxString m_mimeType;
    wxDataFormatId m_formatId;
};

#endif // _WX_HAIKU_DATAFORM_H_
