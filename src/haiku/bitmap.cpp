/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/bitmap.cpp
// Purpose:     wxBitmap and wxMask on BBitmap
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/log.h"
    #include "wx/image.h"
    #include "wx/icon.h"
    #include "wx/cursor.h"
#endif

#include "wx/bitmap.h"
#include "wx/colour.h"
#include "wx/rawbmp.h"
#include "wx/scopedarray.h"
#include "wx/xpmdecod.h"

#include "wx/haiku/private.h"

#include <list>
#include <vector>

#include <pthread.h>

// ============================================================================
// wxBitmapRefData
// ============================================================================

class wxBitmapRefData;

namespace
{

// Bitmaps which accept views (i.e. which a wxMemoryDC drew on) own an
// off-screen window in app_server, with its thread and ports: an application
// with thousands of bitmaps cannot keep them all. The ones which are not
// selected into a DC any more are converted back to plain bitmaps, the
// smallest at once and the others when more than a few have accumulated,
// least recently used first.
std::list<wxBitmapRefData *> gs_idleDrawBitmaps;

const size_t MAX_IDLE_DRAW_BITMAPS = 12;
const int KEEP_DRAW_BITMAP_PIXELS = 128 * 128;

// see wxHaikuBitmapDrawn() in wx/haiku/private.h
pthread_mutex_t gs_pendingLock = PTHREAD_MUTEX_INITIALIZER;
std::vector<BView *> gs_pendingViews;
std::vector<BBitmap *> gs_graveyard;
size_t gs_graveyardBytes = 0;
wxUint32 gs_drawSerial = 0;
wxUint32 gs_syncedSerial = 0;

// deletes the native bitmap, now or when the drawing using it is done
void DeleteNativeBitmap(BBitmap *bitmap, wxUint32 drawSerial)
{
    if ( !bitmap )
        return;

    // A bitmap owned by a static object of the application is destroyed by
    // exit(), after the BApplication: deleting it needs app_server (libbe
    // calls the debugger without). It is left to the end of the team; the
    // lists below may be gone too by now.
    if ( !be_app )
        return;

    if ( drawSerial > gs_syncedSerial )
    {
        gs_graveyard.push_back(bitmap);
        gs_graveyardBytes += bitmap->BitsLength();

        // don't let them pile up when nothing synchronizes for long
        if ( gs_graveyard.size() > 256 || gs_graveyardBytes > 64 * 1024 * 1024 )
            wxHaikuSyncPendingDraws();
    }
    else
    {
        delete bitmap;
    }
}

BBitmap *NewNativeBitmap(int width, int height, bool acceptViews)
{
    BBitmap * const bitmap = new BBitmap(wxHaikuRect(0, 0, width, height),
                                         acceptViews ? B_BITMAP_ACCEPTS_VIEWS : 0,
                                         B_RGBA32);
    if ( bitmap->InitCheck() != B_OK )
    {
        delete bitmap;
        return NULL;
    }

    return bitmap;
}

} // anonymous namespace

class wxBitmapRefData : public wxGDIRefData
{
public:
    wxBitmapRefData()
    {
        Init();
    }

    wxBitmapRefData(int width, int height, int depth)
    {
        Init();
        m_width = width;
        m_height = height;
        m_depth = depth;
        m_bitmap = NewNativeBitmap(width, height, false);
    }

    wxBitmapRefData(const wxBitmapRefData& data)
        : wxGDIRefData()
    {
        Init();
        m_width = data.m_width;
        m_height = data.m_height;
        m_depth = data.m_depth;

        if ( data.m_bitmap )
        {
            data.Sync();

            m_bitmap = NewNativeBitmap(m_width, m_height, false);
            if ( m_bitmap )
                CopyPixels(m_bitmap, data.m_bitmap);
        }

        if ( data.m_mask )
            m_mask = new wxMask(*data.m_mask);
    }

    virtual ~wxBitmapRefData()
    {
        if ( m_idle )
            gs_idleDrawBitmaps.remove(this);

        // the view is deleted with the bitmap it is attached to
        DeleteNativeBitmap(m_bitmap, m_drawSerial);
        DeleteNativeBitmap(m_masked, m_drawSerial);
        delete m_mask;
    }

    virtual bool IsOk() const wxOVERRIDE { return m_bitmap != NULL; }

    // the drawing commands sent through the view are executed by app_server
    // asynchronously: wait for them before the pixels are read
    void Sync() const
    {
        if ( m_needsSync && m_view )
        {
            if ( m_bitmap->Lock() )
            {
                m_view->Sync();
                m_bitmap->Unlock();

                wxHaikuViewSynced(m_view);
            }
        }

        m_needsSync = false;
    }

    static void CopyPixels(BBitmap *dst, const BBitmap *src)
    {
        const int32 rowBytes = wxMin(dst->BytesPerRow(), src->BytesPerRow());
        const int rows = wxMin(dst->Bounds().IntegerHeight(),
                               src->Bounds().IntegerHeight()) + 1;

        const uint8 *s = (const uint8 *)src->Bits();
        uint8 *d = (uint8 *)dst->Bits();
        for ( int y = 0; y < rows; y++ )
        {
            memcpy(d, s, rowBytes);
            s += src->BytesPerRow();
            d += dst->BytesPerRow();
        }
    }

    // replaces the native bitmap by one which accepts views or not
    bool Convert(bool acceptViews)
    {
        if ( acceptViews == m_acceptsViews )
            return true;

        Sync();

        BBitmap * const bitmap = NewNativeBitmap(m_width, m_height, acceptViews);
        if ( !bitmap )
            return false;

        CopyPixels(bitmap, m_bitmap);

        DeleteNativeBitmap(m_bitmap, m_drawSerial);
        m_bitmap = bitmap;
        m_view = NULL;
        m_acceptsViews = acceptViews;

        return true;
    }

    void InvalidateMasked()
    {
        DeleteNativeBitmap(m_masked, m_drawSerial);
        m_masked = NULL;
    }

    int m_width, m_height, m_depth;

    BBitmap *m_bitmap;
    bool m_acceptsViews;
    wxHaikuDrawView *m_view;
    bool m_selected;
    bool m_idle;
    mutable bool m_needsSync;

    wxMask *m_mask;

    // the pixels with the mask applied to the alpha channel
    BBitmap *m_masked;

    // when the native bitmaps were last used by a drawing command
    mutable wxUint32 m_drawSerial;

private:
    void Init()
    {
        m_width = m_height = m_depth = 0;
        m_bitmap = NULL;
        m_acceptsViews = false;
        m_view = NULL;
        m_selected = false;
        m_idle = false;
        m_needsSync = false;
        m_mask = NULL;
        m_masked = NULL;
        m_drawSerial = 0;
    }
};

#define M_BITMAP (static_cast<wxBitmapRefData *>(m_refData))

void wxHaikuFreeBitmapViews()
{
    while ( !gs_idleDrawBitmaps.empty() )
    {
        wxBitmapRefData * const data = gs_idleDrawBitmaps.front();
        gs_idleDrawBitmaps.pop_front();
        data->m_idle = false;
        data->Convert(false);
    }
}

// ============================================================================
// wxMask
// ============================================================================

wxIMPLEMENT_DYNAMIC_CLASS(wxMask, wxObject);

wxMask::wxMask()
    : m_data(NULL), m_width(0), m_height(0)
{
}

wxMask::wxMask(const wxMask& mask)
    : wxMaskBase(), m_data(NULL), m_width(0), m_height(0)
{
    if ( mask.m_data )
        HaikuSetData(mask.m_data, mask.m_width, mask.m_height);
}

wxMask::wxMask(const wxBitmap& bitmap, const wxColour& colour)
    : m_data(NULL), m_width(0), m_height(0)
{
    InitFromColour(bitmap, colour);
}

#if wxUSE_PALETTE
wxMask::wxMask(const wxBitmap& bitmap, int WXUNUSED(paletteIndex))
    : m_data(NULL), m_width(0), m_height(0)
{
    InitFromMonoBitmap(bitmap);
}
#endif // wxUSE_PALETTE

wxMask::wxMask(const wxBitmap& bitmap)
    : m_data(NULL), m_width(0), m_height(0)
{
    InitFromMonoBitmap(bitmap);
}

wxMask::~wxMask()
{
    FreeData();
}

void wxMask::FreeData()
{
    delete [] m_data;
    m_data = NULL;
    m_width = m_height = 0;
}

void wxMask::HaikuSetData(const unsigned char *data, int width, int height)
{
    FreeData();

    m_width = width;
    m_height = height;
    m_data = new unsigned char[width * height];
    memcpy(m_data, data, width * height);
}

bool wxMask::InitFromColour(const wxBitmap& bitmap, const wxColour& colour)
{
    wxCHECK_MSG( bitmap.IsOk(), false, wxS("invalid bitmap") );

    FreeData();

    m_width = bitmap.GetWidth();
    m_height = bitmap.GetHeight();
    m_data = new unsigned char[m_width * m_height];

    const unsigned char r = colour.Red(),
                        g = colour.Green(),
                        b = colour.Blue();

    const unsigned char *bits = bitmap.HaikuGetBits();
    const int rowBytes = bitmap.HaikuGetRowBytes();

    unsigned char *dst = m_data;
    for ( int y = 0; y < m_height; y++ )
    {
        const unsigned char *src = bits + y * rowBytes;
        for ( int x = 0; x < m_width; x++, src += 4 )
            *dst++ = (src[0] == b && src[1] == g && src[2] == r) ? 0 : 0xff;
    }

    return true;
}

bool wxMask::InitFromMonoBitmap(const wxBitmap& bitmap)
{
    wxCHECK_MSG( bitmap.IsOk(), false, wxS("invalid bitmap") );

    FreeData();

    m_width = bitmap.GetWidth();
    m_height = bitmap.GetHeight();
    m_data = new unsigned char[m_width * m_height];

    const unsigned char *bits = bitmap.HaikuGetBits();
    const int rowBytes = bitmap.HaikuGetRowBytes();

    // white is opaque
    unsigned char *dst = m_data;
    for ( int y = 0; y < m_height; y++ )
    {
        const unsigned char *src = bits + y * rowBytes;
        for ( int x = 0; x < m_width; x++, src += 4 )
            *dst++ = (src[0] | src[1] | src[2]) & 0x80 ? 0xff : 0;
    }

    return true;
}

wxBitmap wxMask::GetBitmap() const
{
    wxBitmap bitmap;
    if ( !m_data )
        return bitmap;

    bitmap.Create(m_width, m_height, 1);

    unsigned char *bits = bitmap.HaikuGetBits();
    const int rowBytes = bitmap.HaikuGetRowBytes();

    const unsigned char *src = m_data;
    for ( int y = 0; y < m_height; y++ )
    {
        unsigned char *dst = bits + y * rowBytes;
        for ( int x = 0; x < m_width; x++, dst += 4 )
        {
            dst[0] = dst[1] = dst[2] = *src++;
            dst[3] = 0xff;
        }
    }

    return bitmap;
}

// ============================================================================
// wxBitmap
// ============================================================================

wxIMPLEMENT_DYNAMIC_CLASS(wxBitmap, wxGDIObject);

wxGDIRefData *wxBitmap::CreateGDIRefData() const
{
    return new wxBitmapRefData;
}

wxGDIRefData *wxBitmap::CloneGDIRefData(const wxGDIRefData *data) const
{
    return new wxBitmapRefData(*static_cast<const wxBitmapRefData *>(data));
}

bool wxBitmap::Create(int width, int height, int depth)
{
    UnRef();

    wxCHECK_MSG( width > 0 && height > 0, false, wxS("invalid bitmap size") );
    wxCHECK_MSG( be_app, false, wxS("bitmaps need the application object") );

    if ( depth == wxBITMAP_SCREEN_DEPTH )
        depth = 24;
    else if ( depth != 1 && depth != 32 )
        depth = 24;

    wxBitmapRefData * const data = new wxBitmapRefData(width, height, depth);
    if ( !data->m_bitmap )
    {
        delete data;
        return false;
    }

    m_refData = data;

    // bitmaps with an alpha channel start transparent, the others black
    uint8 * const bits = (uint8 *)data->m_bitmap->Bits();
    const int32 length = data->m_bitmap->BitsLength();
    memset(bits, 0, length);
    if ( depth != 32 )
    {
        for ( int32 i = 3; i < length; i += 4 )
            bits[i] = 0xff;
    }

    return true;
}

wxBitmap::wxBitmap(const char bits[], int width, int height, int depth)
{
    wxCHECK_RET( depth == 1, wxS("can only create mono bitmap from XBM data") );

    if ( !Create(width, height, 1) )
        return;

    // XBM data: rows padded to bytes, least significant bit first, set bits
    // are the foreground (black)
    unsigned char * const dstBits = HaikuGetBits();
    const int rowBytes = HaikuGetRowBytes();
    const int srcRowBytes = (width + 7) / 8;

    for ( int y = 0; y < height; y++ )
    {
        const unsigned char *src = (const unsigned char *)bits + y * srcRowBytes;
        unsigned char *dst = dstBits + y * rowBytes;
        for ( int x = 0; x < width; x++, dst += 4 )
        {
            const bool set = (src[x / 8] >> (x % 8)) & 1;
            dst[0] = dst[1] = dst[2] = set ? 0 : 0xff;
            dst[3] = 0xff;
        }
    }
}

wxBitmap::wxBitmap(const wxString& filename, wxBitmapType type)
{
    LoadFile(filename, type);
}

wxBitmap::wxBitmap(const wxCursor& WXUNUSED(cursor))
{
    // the pixels of a BCursor cannot be read back
}

#if wxUSE_IMAGE

wxBitmap::wxBitmap(const wxImage& imageOrig, int depth, double WXUNUSED(scale))
{
    wxCHECK_RET( imageOrig.IsOk(), wxS("invalid image") );

    const int width = imageOrig.GetWidth(),
              height = imageOrig.GetHeight();

    const bool hasAlpha = imageOrig.HasAlpha();

    if ( depth != 1 )
        depth = hasAlpha ? 32 : 24;

    if ( !Create(width, height, depth) )
        return;

    const unsigned char *src = imageOrig.GetData();
    const unsigned char *alpha = hasAlpha ? imageOrig.GetAlpha() : NULL;

    unsigned char * const bits = HaikuGetBits();
    const int rowBytes = HaikuGetRowBytes();

    for ( int y = 0; y < height; y++ )
    {
        unsigned char *dst = bits + y * rowBytes;

        if ( depth == 1 )
        {
            for ( int x = 0; x < width; x++, src += 3, dst += 4 )
            {
                const bool white = (src[0] | src[1] | src[2]) & 0x80;
                dst[0] = dst[1] = dst[2] = white ? 0xff : 0;
                dst[3] = 0xff;
            }
        }
        else if ( alpha )
        {
            for ( int x = 0; x < width; x++, src += 3, dst += 4 )
            {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = *alpha++;
            }
        }
        else
        {
            for ( int x = 0; x < width; x++, src += 3, dst += 4 )
            {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = 0xff;
            }
        }
    }

    if ( imageOrig.HasMask() )
    {
        const unsigned char mr = imageOrig.GetMaskRed(),
                            mg = imageOrig.GetMaskGreen(),
                            mb = imageOrig.GetMaskBlue();

        wxScopedArray<unsigned char> maskData(width * height);
        src = imageOrig.GetData();
        for ( int n = 0; n < width * height; n++, src += 3 )
            maskData[n] = (src[0] == mr && src[1] == mg && src[2] == mb) ? 0 : 0xff;

        wxMask * const mask = new wxMask;
        mask->HaikuSetData(maskData.get(), width, height);
        SetMask(mask);
    }
}

wxImage wxBitmap::ConvertToImage() const
{
    wxCHECK_MSG( IsOk(), wxNullImage, wxS("invalid bitmap") );

    const int width = M_BITMAP->m_width,
              height = M_BITMAP->m_height;

    wxImage image(width, height, false);
    if ( !image.IsOk() )
        return wxNullImage;

    const bool hasAlpha = HasAlpha();
    if ( hasAlpha )
        image.InitAlpha();

    const unsigned char * const bits = HaikuGetBits();
    const int rowBytes = HaikuGetRowBytes();

    unsigned char *dst = image.GetData();
    unsigned char *alpha = hasAlpha ? image.GetAlpha() : NULL;

    for ( int y = 0; y < height; y++ )
    {
        const unsigned char *src = bits + y * rowBytes;
        for ( int x = 0; x < width; x++, src += 4, dst += 3 )
        {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            if ( alpha )
                *alpha++ = src[3];
        }
    }

    const wxMask * const mask = M_BITMAP->m_mask;
    if ( mask && mask->GetData()
            && mask->GetWidth() == width && mask->GetHeight() == height )
    {
        // a colour the image does not use stands for the masked pixels
        unsigned char r, g, b;
        if ( !image.FindFirstUnusedColour(&r, &g, &b) )
        {
            r = 255;
            g = 0;
            b = 255;
        }

        const unsigned char *m = mask->GetData();
        dst = image.GetData();
        for ( int n = 0; n < width * height; n++, dst += 3 )
        {
            if ( !m[n] )
            {
                dst[0] = r;
                dst[1] = g;
                dst[2] = b;
            }
        }

        image.SetMaskColour(r, g, b);
    }

    return image;
}

#endif // wxUSE_IMAGE

// ----------------------------------------------------------------------------
// accessors
// ----------------------------------------------------------------------------

int wxBitmap::GetHeight() const
{
    wxCHECK_MSG( IsOk(), -1, wxS("invalid bitmap") );

    return M_BITMAP->m_height;
}

int wxBitmap::GetWidth() const
{
    wxCHECK_MSG( IsOk(), -1, wxS("invalid bitmap") );

    return M_BITMAP->m_width;
}

int wxBitmap::GetDepth() const
{
    wxCHECK_MSG( IsOk(), -1, wxS("invalid bitmap") );

    return M_BITMAP->m_depth;
}

bool wxBitmap::HasAlpha() const
{
    return IsOk() && M_BITMAP->m_depth == 32;
}

void wxBitmap::UseAlpha(bool use)
{
    if ( !IsOk() || use == HasAlpha() || M_BITMAP->m_depth == 1 )
        return;

    AllocExclusive();
    M_BITMAP->m_depth = use ? 32 : 24;

    if ( !use )
    {
        // opaque bitmaps keep their alpha bytes at 0xff
        unsigned char * const bits = HaikuGetBits();
        const int32 length = M_BITMAP->m_bitmap->BitsLength();
        for ( int32 i = 3; i < length; i += 4 )
            bits[i] = 0xff;
    }

    M_BITMAP->InvalidateMasked();
}

wxMask *wxBitmap::GetMask() const
{
    wxCHECK_MSG( IsOk(), NULL, wxS("invalid bitmap") );

    return M_BITMAP->m_mask;
}

void wxBitmap::SetMask(wxMask *mask)
{
    wxCHECK_RET( IsOk(), wxS("invalid bitmap") );

    AllocExclusive();

    delete M_BITMAP->m_mask;
    M_BITMAP->m_mask = mask;
    M_BITMAP->InvalidateMasked();
}

wxBitmap wxBitmap::GetSubBitmap(const wxRect& rect) const
{
    wxCHECK_MSG( IsOk()
                    && rect.x >= 0 && rect.y >= 0
                    && rect.x + rect.width <= M_BITMAP->m_width
                    && rect.y + rect.height <= M_BITMAP->m_height
                    && rect.width > 0 && rect.height > 0,
                 wxNullBitmap,
                 wxS("invalid bitmap or bitmap region") );

    wxBitmap sub;
    if ( !sub.Create(rect.width, rect.height, M_BITMAP->m_depth) )
        return wxNullBitmap;

    const unsigned char * const srcBits = HaikuGetBits();
    const int srcRowBytes = HaikuGetRowBytes();
    unsigned char * const dstBits = sub.HaikuGetBits();
    const int dstRowBytes = sub.HaikuGetRowBytes();

    for ( int y = 0; y < rect.height; y++ )
    {
        memcpy(dstBits + y * dstRowBytes,
               srcBits + (rect.y + y) * srcRowBytes + rect.x * 4,
               rect.width * 4);
    }

    const wxMask * const mask = M_BITMAP->m_mask;
    if ( mask && mask->GetData() )
    {
        wxScopedArray<unsigned char> data(rect.width * rect.height);
        for ( int y = 0; y < rect.height; y++ )
        {
            memcpy(data.get() + y * rect.width,
                   mask->GetData() + (rect.y + y) * mask->GetWidth() + rect.x,
                   rect.width);
        }

        wxMask * const subMask = new wxMask;
        subMask->HaikuSetData(data.get(), rect.width, rect.height);
        sub.SetMask(subMask);
    }

    return sub;
}

#if WXWIN_COMPATIBILITY_3_0
void wxBitmap::SetHeight(int height)
{
    AllocExclusive();
    M_BITMAP->m_height = height;
}

void wxBitmap::SetWidth(int width)
{
    AllocExclusive();
    M_BITMAP->m_width = width;
}

void wxBitmap::SetDepth(int depth)
{
    AllocExclusive();
    M_BITMAP->m_depth = depth;
}
#endif // WXWIN_COMPATIBILITY_3_0

// ----------------------------------------------------------------------------
// files
// ----------------------------------------------------------------------------

bool wxBitmap::LoadFile(const wxString& name, wxBitmapType type)
{
    UnRef();

#if wxUSE_IMAGE
    wxImage image;
    if ( !image.LoadFile(name, type) || !image.IsOk() )
        return false;

    *this = wxBitmap(image);
    return IsOk();
#else
    wxUnusedVar(name);
    wxUnusedVar(type);
    return false;
#endif
}

bool wxBitmap::SaveFile(const wxString& name, wxBitmapType type,
                        const wxPalette *WXUNUSED(palette)) const
{
    wxCHECK_MSG( IsOk(), false, wxS("invalid bitmap") );

#if wxUSE_IMAGE
    wxImage image = ConvertToImage();
    return image.IsOk() && image.SaveFile(name, type);
#else
    wxUnusedVar(name);
    wxUnusedVar(type);
    return false;
#endif
}

#if wxUSE_PALETTE
wxPalette *wxBitmap::GetPalette() const
{
    return NULL;
}

void wxBitmap::SetPalette(const wxPalette& WXUNUSED(palette))
{
}
#endif // wxUSE_PALETTE

bool wxBitmap::CopyFromIcon(const wxIcon& icon)
{
    *this = icon;
    return IsOk();
}

/* static */
void wxBitmap::InitStandardHandlers()
{
    // files are loaded through wxImage and its handlers
}

// ----------------------------------------------------------------------------
// raw access
// ----------------------------------------------------------------------------

void *wxBitmap::GetRawData(wxPixelDataBase& data, int bpp)
{
    wxCHECK_MSG( IsOk(), NULL, wxS("invalid bitmap") );

    // both the native and the alpha pixel formats have 32 bits per pixel
    if ( bpp != 32 )
        return NULL;

    AllocExclusive();

    data.m_width = M_BITMAP->m_width;
    data.m_height = M_BITMAP->m_height;
    data.m_stride = HaikuGetRowBytes();

    return HaikuGetBits();
}

void wxBitmap::UngetRawData(wxPixelDataBase& WXUNUSED(data))
{
    if ( !IsOk() )
        return;

    if ( M_BITMAP->m_depth != 32 )
    {
        // whatever was written to the unused bytes, opaque bitmaps keep
        // them at 0xff
        unsigned char * const bits = (unsigned char *)M_BITMAP->m_bitmap->Bits();
        const int32 length = M_BITMAP->m_bitmap->BitsLength();
        for ( int32 i = 3; i < length; i += 4 )
            bits[i] = 0xff;
    }

    M_BITMAP->InvalidateMasked();
}

// ----------------------------------------------------------------------------
// native bitmap
// ----------------------------------------------------------------------------

BBitmap *wxBitmap::GetHaikuBitmap() const
{
    if ( !IsOk() )
        return NULL;

    M_BITMAP->Sync();
    return M_BITMAP->m_bitmap;
}

unsigned char *wxBitmap::HaikuGetBits() const
{
    BBitmap * const bitmap = GetHaikuBitmap();
    return bitmap ? (unsigned char *)bitmap->Bits() : NULL;
}

int wxBitmap::HaikuGetRowBytes() const
{
    return IsOk() ? M_BITMAP->m_bitmap->BytesPerRow() : 0;
}

bool wxBitmap::HaikuNeedsAlphaBlend(bool useMask) const
{
    if ( !IsOk() )
        return false;

    return M_BITMAP->m_depth == 32 || (useMask && M_BITMAP->m_mask);
}

BBitmap *wxBitmap::GetHaikuDrawBitmap(bool useMask) const
{
    if ( !IsOk() )
        return NULL;

    const wxMask * const mask = M_BITMAP->m_mask;
    if ( !useMask || !mask || !mask->GetData()
            || mask->GetWidth() != M_BITMAP->m_width
            || mask->GetHeight() != M_BITMAP->m_height )
    {
        return GetHaikuBitmap();
    }

    if ( M_BITMAP->m_masked && !M_BITMAP->m_needsSync )
        return M_BITMAP->m_masked;

    M_BITMAP->InvalidateMasked();

    BBitmap * const src = GetHaikuBitmap();
    BBitmap * const masked = NewNativeBitmap(M_BITMAP->m_width,
                                             M_BITMAP->m_height, false);
    if ( !masked )
        return src;

    const unsigned char *m = mask->GetData();
    for ( int y = 0; y < M_BITMAP->m_height; y++ )
    {
        const unsigned char *s = (const unsigned char *)src->Bits()
                                    + y * src->BytesPerRow();
        unsigned char *d = (unsigned char *)masked->Bits()
                                    + y * masked->BytesPerRow();
        for ( int x = 0; x < M_BITMAP->m_width; x++, s += 4, d += 4 )
        {
            if ( *m++ )
            {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = s[3];
            }
            else
            {
                d[0] = d[1] = d[2] = d[3] = 0;
            }
        }
    }

    M_BITMAP->m_masked = masked;
    return masked;
}

void wxBitmap::HaikuSetDrawSerial(wxUint32 serial) const
{
    if ( IsOk() )
        M_BITMAP->m_drawSerial = serial;
}

void wxBitmap::HaikuTouch()
{
    if ( !IsOk() )
        return;

    M_BITMAP->m_needsSync = true;
    if ( M_BITMAP->m_masked )
        M_BITMAP->InvalidateMasked();
}

BView *wxBitmap::HaikuBeginDrawing()
{
    wxCHECK_MSG( IsOk(), NULL, wxS("invalid bitmap") );

    wxBitmapRefData * const data = M_BITMAP;

    if ( data->m_idle )
    {
        gs_idleDrawBitmaps.remove(data);
        data->m_idle = false;
    }

    if ( !data->Convert(true) )
        return NULL;

    if ( !data->m_view )
    {
        data->m_view = new wxHaikuDrawView(data->m_bitmap->Bounds(), "wxbitmap",
                                           B_FOLLOW_NONE, 0);
        if ( !data->m_bitmap->Lock() )
        {
            delete data->m_view;
            data->m_view = NULL;
            return NULL;
        }

        data->m_bitmap->AddChild(data->m_view);
        data->m_bitmap->Unlock();
    }

    data->m_view->m_stateOwner = NULL;
    data->m_selected = true;

    return data->m_view;
}

void wxBitmap::HaikuEndDrawing()
{
    if ( !IsOk() )
        return;

    wxBitmapRefData * const data = M_BITMAP;
    if ( !data->m_selected )
        return;

    data->m_selected = false;

    if ( !data->m_acceptsViews )
        return;

    if ( data->m_width * data->m_height < KEEP_DRAW_BITMAP_PIXELS )
    {
        // small: not worth an off-screen window in app_server
        data->Convert(false);
        return;
    }

    // keep it ready for the next wxMemoryDC for a while
    data->Sync();
    data->m_idle = true;
    gs_idleDrawBitmaps.push_back(data);

    while ( gs_idleDrawBitmaps.size() > MAX_IDLE_DRAW_BITMAPS )
    {
        wxBitmapRefData * const oldest = gs_idleDrawBitmaps.front();
        gs_idleDrawBitmaps.pop_front();
        oldest->m_idle = false;
        oldest->Convert(false);
    }
}

// ----------------------------------------------------------------------------
// bitmaps drawn asynchronously
// ----------------------------------------------------------------------------

wxHaikuDrawView::~wxHaikuDrawView()
{
    // may run on a looper thread (the root view of a window being quit)
    pthread_mutex_lock(&gs_pendingLock);
    for ( size_t n = 0; n < gs_pendingViews.size(); n++ )
    {
        if ( gs_pendingViews[n] == this )
        {
            gs_pendingViews.erase(gs_pendingViews.begin() + n);
            break;
        }
    }
    pthread_mutex_unlock(&gs_pendingLock);
}

void wxHaikuBitmapDrawn(BView *target, const wxBitmap& bitmap)
{
    bitmap.HaikuSetDrawSerial(++gs_drawSerial);

    pthread_mutex_lock(&gs_pendingLock);
    bool found = false;
    for ( size_t n = 0; n < gs_pendingViews.size(); n++ )
    {
        if ( gs_pendingViews[n] == target )
        {
            found = true;
            break;
        }
    }
    if ( !found )
        gs_pendingViews.push_back(target);
    pthread_mutex_unlock(&gs_pendingLock);
}

static void FreeGraveyardIfDone()
{
    pthread_mutex_lock(&gs_pendingLock);
    const bool done = gs_pendingViews.empty();
    pthread_mutex_unlock(&gs_pendingLock);

    if ( !done )
        return;

    // everything sent so far has been executed
    gs_syncedSerial = gs_drawSerial;

    for ( size_t n = 0; n < gs_graveyard.size(); n++ )
        delete gs_graveyard[n];
    gs_graveyard.clear();
    gs_graveyardBytes = 0;
}

void wxHaikuViewSynced(BView *view)
{
    pthread_mutex_lock(&gs_pendingLock);
    for ( size_t n = 0; n < gs_pendingViews.size(); n++ )
    {
        if ( gs_pendingViews[n] == view )
        {
            gs_pendingViews.erase(gs_pendingViews.begin() + n);
            break;
        }
    }
    pthread_mutex_unlock(&gs_pendingLock);

    if ( !gs_graveyard.empty() )
        FreeGraveyardIfDone();
}

void wxHaikuSyncPendingDraws()
{
    for ( ;; )
    {
        pthread_mutex_lock(&gs_pendingLock);
        BView * const view = gs_pendingViews.empty() ? NULL
                                                     : gs_pendingViews.back();
        if ( view )
            gs_pendingViews.pop_back();
        pthread_mutex_unlock(&gs_pendingLock);

        if ( !view )
            break;

        // The views are only destroyed by this thread, except for the root
        // views of windows, which this thread destroys by quitting the
        // window: the pointer is valid here.
        if ( view->Window() && view->LockLooper() )
        {
            view->Sync();
            view->UnlockLooper();
        }
    }

    FreeGraveyardIfDone();
}

// ----------------------------------------------------------------------------
// conversion from native bitmaps
// ----------------------------------------------------------------------------

// a copy of the native bitmap, whatever its colour space
wxBitmap wxHaikuBitmapFromNative(const BBitmap *bitmap)
{
    wxBitmap result;
    if ( !bitmap || bitmap->InitCheck() != B_OK )
        return result;

    const int width = bitmap->Bounds().IntegerWidth() + 1,
              height = bitmap->Bounds().IntegerHeight() + 1;

    const color_space space = bitmap->ColorSpace();
    const bool hasAlpha = space == B_RGBA32;

    if ( !result.Create(width, height, hasAlpha ? 32 : 24) )
        return result;

    BBitmap * const dst = result.GetHaikuBitmap();
    if ( space == B_RGBA32 || space == B_RGB32 )
    {
        const unsigned char *s = (const unsigned char *)bitmap->Bits();
        unsigned char *d = (unsigned char *)dst->Bits();
        for ( int y = 0; y < height; y++ )
        {
            memcpy(d, s, width * 4);
            if ( !hasAlpha )
            {
                for ( int x = 0; x < width; x++ )
                    d[x * 4 + 3] = 0xff;
            }

            s += bitmap->BytesPerRow();
            d += dst->BytesPerRow();
        }
    }
    else
    {
        // let the Interface Kit convert the pixels
        if ( dst->ImportBits(bitmap) != B_OK )
            return wxBitmap();

        unsigned char * const bits = (unsigned char *)dst->Bits();
        const int32 length = dst->BitsLength();
        for ( int32 i = 3; i < length; i += 4 )
            bits[i] = 0xff;
    }

    return result;
}
