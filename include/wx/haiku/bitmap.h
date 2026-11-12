/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/bitmap.h
// Purpose:     wxBitmap and wxMask on BBitmap
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_BITMAP_H_
#define _WX_HAIKU_BITMAP_H_

class BBitmap;
class BView;
class WXDLLIMPEXP_FWD_CORE wxPixelDataBase;
class WXDLLIMPEXP_FWD_CORE wxCursor;

// ----------------------------------------------------------------------------
// wxMask: one byte per pixel, 0xff where the bitmap is opaque
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxMask : public wxMaskBase
{
public:
    wxMask();
    wxMask(const wxMask& mask);
    wxMask(const wxBitmap& bitmap, const wxColour& colour);
#if wxUSE_PALETTE
    wxMask(const wxBitmap& bitmap, int paletteIndex);
#endif // wxUSE_PALETTE
    wxMask(const wxBitmap& bitmap);
    virtual ~wxMask();

    // the mask as a monochrome bitmap (white where opaque)
    wxBitmap GetBitmap() const;

    // implementation
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    const unsigned char *GetData() const { return m_data; }

    // creates the mask from a buffer of width*height bytes (takes a copy)
    void HaikuSetData(const unsigned char *data, int width, int height);

protected:
    virtual void FreeData() wxOVERRIDE;
    virtual bool InitFromColour(const wxBitmap& bitmap, const wxColour& colour) wxOVERRIDE;
    virtual bool InitFromMonoBitmap(const wxBitmap& bitmap) wxOVERRIDE;

private:
    unsigned char *m_data;
    int m_width, m_height;

    wxDECLARE_DYNAMIC_CLASS(wxMask);
};

// ----------------------------------------------------------------------------
// wxBitmap
// ----------------------------------------------------------------------------

// The pixels are always stored as B_RGBA32 (BGRA in memory, alpha not
// premultiplied). Bitmaps of depth 32 have an alpha channel, the others keep
// the alpha bytes at 0xff. Depth 1 bitmaps hold black or white pixels.
class WXDLLIMPEXP_CORE wxBitmap : public wxBitmapBase
{
public:
    wxBitmap() { }
    wxBitmap(int width, int height, int depth = wxBITMAP_SCREEN_DEPTH)
        { Create(width, height, depth); }
    wxBitmap(const wxSize& sz, int depth = wxBITMAP_SCREEN_DEPTH)
        { Create(sz, depth); }
    wxBitmap(const char bits[], int width, int height, int depth = 1);
    wxBitmap(const wxString& filename, wxBitmapType type = wxBITMAP_DEFAULT_TYPE);
    wxBitmap(const char* const* bits);
#if wxUSE_IMAGE
    wxBitmap(const wxImage& image, int depth = wxBITMAP_SCREEN_DEPTH, double scale = 1.0);
#endif
    explicit wxBitmap(const wxCursor& cursor);

    virtual bool Create(int width, int height, int depth = wxBITMAP_SCREEN_DEPTH) wxOVERRIDE;
    virtual bool Create(const wxSize& sz, int depth = wxBITMAP_SCREEN_DEPTH) wxOVERRIDE
        { return Create(sz.GetWidth(), sz.GetHeight(), depth); }
    bool Create(int width, int height, const wxDC& WXUNUSED(dc))
        { return Create(width, height); }

    virtual int GetHeight() const wxOVERRIDE;
    virtual int GetWidth() const wxOVERRIDE;
    virtual int GetDepth() const wxOVERRIDE;

#if wxUSE_IMAGE
    virtual wxImage ConvertToImage() const wxOVERRIDE;
#endif

    virtual wxMask *GetMask() const wxOVERRIDE;
    virtual void SetMask(wxMask *mask) wxOVERRIDE;

    virtual wxBitmap GetSubBitmap(const wxRect& rect) const wxOVERRIDE;

    virtual bool SaveFile(const wxString& name, wxBitmapType type,
                          const wxPalette *palette = NULL) const wxOVERRIDE;
    virtual bool LoadFile(const wxString& name,
                          wxBitmapType type = wxBITMAP_DEFAULT_TYPE) wxOVERRIDE;

#if wxUSE_PALETTE
    virtual wxPalette *GetPalette() const wxOVERRIDE;
    virtual void SetPalette(const wxPalette& palette) wxOVERRIDE;
#endif

    // copies the contents and mask of the given (colour) icon to the bitmap
    virtual bool CopyFromIcon(const wxIcon& icon) wxOVERRIDE;

    static void InitStandardHandlers();

    // raw bitmap access support functions
    void *GetRawData(wxPixelDataBase& data, int bpp);
    void UngetRawData(wxPixelDataBase& data);

    bool HasAlpha() const;
    void UseAlpha(bool use = true);

#if WXWIN_COMPATIBILITY_3_0
    wxDEPRECATED(virtual void SetHeight(int height) wxOVERRIDE);
    wxDEPRECATED(virtual void SetWidth(int width) wxOVERRIDE);
    wxDEPRECATED(virtual void SetDepth(int depth) wxOVERRIDE);
#endif

    // implementation from now on
    // --------------------------

    // The native bitmap to read pixels from or to draw somewhere else: any
    // drawing pending on a memory DC selected into it is flushed first.
    BBitmap *GetHaikuBitmap() const;

    // The bitmap to draw this one with: the pixels with the mask applied to
    // the alpha channel if useMask and there is a mask, otherwise the same as
    // GetHaikuBitmap(). The result is cached until the bitmap changes.
    BBitmap *GetHaikuDrawBitmap(bool useMask) const;

    // true if the bitmap GetHaikuDrawBitmap() returns must be alpha blended
    bool HaikuNeedsAlphaBlend(bool useMask) const;

    // Called by wxMemoryDC: makes the bitmap accept views, attaches a view
    // to draw with to it and returns it (NULL on failure); the view is
    // detached again by HaikuEndDrawing(). Drawing ends lazily: the server
    // side off-screen window is kept for big bitmaps until other bitmaps
    // need it more.
    BView *HaikuBeginDrawing();
    void HaikuEndDrawing();

    // the pixels were changed behind the back of this object (by drawing)
    void HaikuTouch();

    // direct access to the pixels after flushing pending drawing: 4 bytes
    // per pixel in B, G, R, A order, rows are HaikuGetRowBytes() apart
    unsigned char *HaikuGetBits() const;
    int HaikuGetRowBytes() const;

protected:
    virtual wxGDIRefData *CreateGDIRefData() const wxOVERRIDE;
    virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const wxOVERRIDE;

    wxDECLARE_DYNAMIC_CLASS(wxBitmap);
};

#endif // _WX_HAIKU_BITMAP_H_
