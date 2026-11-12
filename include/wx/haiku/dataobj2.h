/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dataobj2.h
// Purpose:     wxBitmapDataObject and wxFileDataObject
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DATAOBJ2_H_
#define _WX_HAIKU_DATAOBJ2_H_

// ----------------------------------------------------------------------------
// wxBitmapDataObject: the data is the bitmap as a PNG file
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxBitmapDataObject : public wxBitmapDataObjectBase
{
public:
    wxBitmapDataObject();
    wxBitmapDataObject(const wxBitmap& bitmap);
    virtual ~wxBitmapDataObject();

    // override base class virtual to update PNG data too
    virtual void SetBitmap(const wxBitmap& bitmap) wxOVERRIDE;

    // implement base class pure virtuals
    virtual size_t GetDataSize() const wxOVERRIDE { return m_pngSize; }
    virtual bool GetDataHere(void *buf) const wxOVERRIDE;
    virtual bool SetData(size_t len, const void *buf) wxOVERRIDE;

    // Must provide overloads to avoid hiding them (and warnings about it)
    virtual size_t GetDataSize(const wxDataFormat&) const wxOVERRIDE
    {
        return GetDataSize();
    }
    virtual bool GetDataHere(const wxDataFormat&, void *buf) const wxOVERRIDE
    {
        return GetDataHere(buf);
    }
    virtual bool SetData(const wxDataFormat&, size_t len, const void *buf) wxOVERRIDE
    {
        return SetData(len, buf);
    }

protected:
    void Clear();
    void DoConvertToPng();

    size_t m_pngSize;
    void *m_pngData;
};

// ----------------------------------------------------------------------------
// wxFileDataObject: the data is the list of paths, one per line, in UTF-8
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxFileDataObject : public wxFileDataObjectBase
{
public:
    void AddFile(const wxString& filename);

    // implement base class pure virtuals
    virtual size_t GetDataSize() const wxOVERRIDE;
    virtual bool GetDataHere(void *buf) const wxOVERRIDE;
    virtual bool SetData(size_t len, const void *buf) wxOVERRIDE;

    // Must provide overloads to avoid hiding them (and warnings about it)
    virtual size_t GetDataSize(const wxDataFormat&) const wxOVERRIDE
    {
        return GetDataSize();
    }
    virtual bool GetDataHere(const wxDataFormat&, void *buf) const wxOVERRIDE
    {
        return GetDataHere(buf);
    }
    virtual bool SetData(const wxDataFormat&, size_t len, const void *buf) wxOVERRIDE
    {
        return SetData(len, buf);
    }
};

#endif // _WX_HAIKU_DATAOBJ2_H_
