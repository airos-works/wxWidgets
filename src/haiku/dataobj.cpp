/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dataobj.cpp
// Purpose:     wxDataFormat, wxDataObject and friends on BMessage data
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_DATAOBJ

#include "wx/dataobj.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/image.h"
#endif

#include "wx/mstream.h"
#include "wx/scopedarray.h"
#include "wx/tokenzr.h"

#include "wx/haiku/private.h"

#include <Entry.h>
#include <Path.h>

// implemented in bitmap.cpp
wxBitmap wxHaikuBitmapFromNative(const BBitmap *bitmap);

// ----------------------------------------------------------------------------
// wxDataFormat
// ----------------------------------------------------------------------------

namespace
{

// the name of the field entry_refs are in
const char * const kRefsField = "refs";

wxString MimeTypeOf(wxDataFormatId formatId)
{
    switch ( formatId )
    {
        case wxDF_TEXT:
        case wxDF_UNICODETEXT:
        case wxDF_OEMTEXT:
            return "text/plain";

        case wxDF_BITMAP:
        case wxDF_PNG:
            return "image/png";

        case wxDF_TIFF:
            return "image/tiff";

        case wxDF_WAVE:
            return "audio/x-wav";

        case wxDF_HTML:
            return "text/html";

        case wxDF_FILENAME:
            return kRefsField;

        default:
            return wxString();
    }
}

} // anonymous namespace

wxDataFormat::wxDataFormat(wxDataFormatId formatId)
{
    SetType(formatId);
}

wxDataFormat::wxDataFormat(const wxString& id)
{
    SetId(id);
}

wxDataFormat::wxDataFormat(const char *id)
{
    SetId(wxString::FromUTF8(id));
}

wxDataFormat::wxDataFormat(const wchar_t *id)
{
    SetId(id);
}

void wxDataFormat::SetId(const wxString& id)
{
    m_mimeType = id;
    m_formatId = wxDF_PRIVATE;
}

void wxDataFormat::SetType(wxDataFormatId formatId)
{
    m_mimeType = MimeTypeOf(formatId);
    m_formatId = formatId;
}

bool wxDataFormat::operator==(wxDataFormatId format) const
{
    if ( m_formatId == format )
        return true;

    // the text formats are the same thing: UTF-8
    return !m_mimeType.empty() && m_mimeType == MimeTypeOf(format);
}

bool wxDataFormat::operator==(const wxDataFormat& format) const
{
    if ( !m_mimeType.empty() || !format.m_mimeType.empty() )
        return m_mimeType == format.m_mimeType;

    return m_formatId == format.m_formatId;
}

// ----------------------------------------------------------------------------
// wxDataObject
// ----------------------------------------------------------------------------

wxDataObject::wxDataObject()
{
}

wxDataObject::~wxDataObject()
{
}

bool wxDataObject::IsSupportedFormat(const wxDataFormat& format,
                                     Direction dir) const
{
    const size_t count = GetFormatCount(dir);
    if ( count == 1 )
        return format == GetPreferredFormat(dir);

    wxScopedArray<wxDataFormat> formats(count);
    GetAllFormats(formats.get(), dir);

    for ( size_t n = 0; n < count; n++ )
    {
        if ( formats[n] == format )
            return true;
    }

    return false;
}

/* static */
bool wxDataObject::HaikuMessageHasFormat(const BMessage *message,
                                         const wxDataFormat& format)
{
    if ( !message )
        return false;

    const wxString& mime = format.GetMimeType();
    if ( mime.empty() )
        return false;

    if ( mime == kRefsField )
        return message->HasRef(kRefsField);

    if ( message->HasData(mime.utf8_str(), B_MIME_TYPE) )
        return true;

    // the native applications put bitmaps as archived BBitmaps
    if ( mime == "image/png" )
        return message->HasMessage("image/bitmap");

    return false;
}

bool wxDataObject::HaikuAddToMessage(BMessage *message) const
{
    const size_t count = GetFormatCount(Get);
    if ( !count )
        return false;

    wxScopedArray<wxDataFormat> formats(count);
    GetAllFormats(formats.get(), Get);

    bool added = false;
    wxArrayString done;

    for ( size_t n = 0; n < count; n++ )
    {
        const wxDataFormat& format = formats[n];
        const wxString& mime = format.GetMimeType();
        if ( mime.empty() || done.Index(mime) != wxNOT_FOUND )
            continue;

        const size_t size = GetDataSize(format);
        wxCharBuffer buf(size);
        if ( size && !GetDataHere(format, buf.data()) )
            continue;

        done.Add(mime);

        if ( mime == kRefsField )
        {
            // one path per line
            wxStringTokenizer tokens(wxString::FromUTF8(buf.data(), size),
                                     "\r\n", wxTOKEN_STRTOK);
            while ( tokens.HasMoreTokens() )
            {
                entry_ref ref;
                if ( get_ref_for_path(tokens.GetNextToken().utf8_str(), &ref) == B_OK )
                {
                    message->AddRef(kRefsField, &ref);
                    added = true;
                }
            }

            continue;
        }

        if ( mime == "image/png" )
        {
            // also as the archived BBitmap native applications understand
            const wxBitmapDataObject * const
                bitmapData = dynamic_cast<const wxBitmapDataObject *>(this);
            if ( bitmapData && bitmapData->GetBitmap().IsOk() )
            {
                BMessage archive;
                if ( bitmapData->GetBitmap().GetHaikuBitmap()->Archive(&archive) == B_OK )
                    message->AddMessage("image/bitmap", &archive);
            }
        }

        if ( message->AddData(mime.utf8_str(), B_MIME_TYPE, buf.data(), size) == B_OK )
            added = true;
    }

    return added;
}

bool wxDataObject::HaikuSetFromMessage(const BMessage *message)
{
    if ( !message )
        return false;

    const size_t count = GetFormatCount(Set);
    if ( !count )
        return false;

    wxScopedArray<wxDataFormat> formats(count);
    GetAllFormats(formats.get(), Set);

    for ( size_t n = 0; n < count; n++ )
    {
        const wxDataFormat& format = formats[n];
        const wxString& mime = format.GetMimeType();
        if ( mime.empty() )
            continue;

        if ( mime == kRefsField )
        {
            std::string paths;
            entry_ref ref;
            for ( int32 i = 0; message->FindRef(kRefsField, i, &ref) == B_OK; i++ )
            {
                BPath path(&ref);
                if ( path.InitCheck() == B_OK )
                {
                    paths += path.Path();
                    paths += '\n';
                }
            }

            if ( paths.empty() )
                continue;

            return SetData(format, paths.size(), paths.data());
        }

        const void *data;
        ssize_t size;
        if ( message->FindData(mime.utf8_str(), B_MIME_TYPE, &data, &size) == B_OK )
            return SetData(format, size, data);

        if ( mime == "image/png" )
        {
            BMessage archive;
            if ( message->FindMessage("image/bitmap", &archive) == B_OK )
            {
                BBitmap native(&archive);
                if ( native.InitCheck() == B_OK )
                {
                    wxBitmapDataObject * const
                        bitmapData = dynamic_cast<wxBitmapDataObject *>(this);
                    const wxBitmap bitmap = wxHaikuBitmapFromNative(&native);
                    if ( bitmapData && bitmap.IsOk() )
                    {
                        bitmapData->SetBitmap(bitmap);
                        return true;
                    }

#if wxUSE_LIBPNG
                    // any other object taking PNG data
                    if ( bitmap.IsOk() )
                    {
                        wxMemoryOutputStream stream;
                        if ( bitmap.ConvertToImage().SaveFile(stream, wxBITMAP_TYPE_PNG) )
                        {
                            const size_t len = stream.GetSize();
                            wxCharBuffer png(len);
                            stream.CopyTo(png.data(), len);
                            return SetData(format, len, png.data());
                        }
                    }
#endif // wxUSE_LIBPNG
                }
            }
        }
    }

    return false;
}

// ----------------------------------------------------------------------------
// wxTextDataObject: the rest is in common/dobjcmn.cpp
// ----------------------------------------------------------------------------

void wxTextDataObject::GetAllFormats(wxDataFormat *formats,
                                     wxDataObjectBase::Direction WXUNUSED(dir)) const
{
    // both are UTF-8 under the same MIME type here
    *formats++ = GetPreferredFormat();
    *formats = wxDF_TEXT;
}

// ----------------------------------------------------------------------------
// wxBitmapDataObject
// ----------------------------------------------------------------------------

wxBitmapDataObject::wxBitmapDataObject()
    : m_pngSize(0), m_pngData(NULL)
{
}

wxBitmapDataObject::wxBitmapDataObject(const wxBitmap& bitmap)
    : wxBitmapDataObjectBase(bitmap),
      m_pngSize(0), m_pngData(NULL)
{
    DoConvertToPng();
}

wxBitmapDataObject::~wxBitmapDataObject()
{
    Clear();
}

void wxBitmapDataObject::Clear()
{
    free(m_pngData);
    m_pngData = NULL;
    m_pngSize = 0;
}

void wxBitmapDataObject::SetBitmap(const wxBitmap& bitmap)
{
    Clear();
    wxBitmapDataObjectBase::SetBitmap(bitmap);
    DoConvertToPng();
}

bool wxBitmapDataObject::GetDataHere(void *buf) const
{
    if ( !m_pngSize )
        return false;

    memcpy(buf, m_pngData, m_pngSize);
    return true;
}

bool wxBitmapDataObject::SetData(size_t size, const void *buf)
{
    Clear();

#if wxUSE_LIBPNG
    m_pngSize = size;
    m_pngData = malloc(size);
    memcpy(m_pngData, buf, size);

    wxMemoryInputStream stream(m_pngData, m_pngSize);
    wxImage image;
    if ( !image.LoadFile(stream, wxBITMAP_TYPE_PNG) )
        return false;

    m_bitmap = wxBitmap(image);
    return m_bitmap.IsOk();
#else
    wxUnusedVar(size);
    wxUnusedVar(buf);
    return false;
#endif
}

void wxBitmapDataObject::DoConvertToPng()
{
#if wxUSE_LIBPNG
    if ( !m_bitmap.IsOk() )
        return;

    wxMemoryOutputStream stream;
    if ( !m_bitmap.ConvertToImage().SaveFile(stream, wxBITMAP_TYPE_PNG) )
        return;

    m_pngSize = stream.GetSize();
    m_pngData = malloc(m_pngSize);
    stream.CopyTo(m_pngData, m_pngSize);
#endif
}

// ----------------------------------------------------------------------------
// wxFileDataObject
// ----------------------------------------------------------------------------

void wxFileDataObject::AddFile(const wxString& filename)
{
    m_filenames.Add(filename);
}

size_t wxFileDataObject::GetDataSize() const
{
    size_t size = 0;
    for ( size_t n = 0; n < m_filenames.size(); n++ )
        size += strlen(m_filenames[n].utf8_str()) + 1;

    return size;
}

bool wxFileDataObject::GetDataHere(void *buf) const
{
    char *p = (char *)buf;
    for ( size_t n = 0; n < m_filenames.size(); n++ )
    {
        const wxScopedCharBuffer utf8 = m_filenames[n].utf8_str();
        const size_t len = strlen(utf8);
        memcpy(p, utf8, len);
        p += len;
        *p++ = '\n';
    }

    return true;
}

bool wxFileDataObject::SetData(size_t len, const void *buf)
{
    m_filenames.Empty();

    wxStringTokenizer tokens(wxString::FromUTF8((const char *)buf, len),
                             "\r\n", wxTOKEN_STRTOK);
    while ( tokens.HasMoreTokens() )
    {
        wxString path = tokens.GetNextToken();
        if ( path.StartsWith("file://") )
            path = path.Mid(7);

        if ( !path.empty() )
            m_filenames.Add(path);
    }

    return true;
}

#endif // wxUSE_DATAOBJ
