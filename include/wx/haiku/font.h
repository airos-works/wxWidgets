/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/font.h
// Purpose:     wxFont on BFont
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_FONT_H_
#define _WX_HAIKU_FONT_H_

class BFont;

#include "wx/dynarray.h"

class WXDLLIMPEXP_CORE wxFont : public wxFontBase
{
public:
    wxFont() { }
    wxFont(const wxFontInfo& info);
    wxFont(const wxNativeFontInfo& info) { Create(info); }
    wxFont(const wxString& nativeFontInfoString);

    wxFont(int size,
           wxFontFamily family,
           wxFontStyle style,
           wxFontWeight weight,
           bool underlined = false,
           const wxString& face = wxEmptyString,
           wxFontEncoding encoding = wxFONTENCODING_DEFAULT)
    {
        Create(size, family, style, weight, underlined, face, encoding);
    }

    wxFont(const wxSize& pixelSize,
           wxFontFamily family,
           wxFontStyle style,
           wxFontWeight weight,
           bool underlined = false,
           const wxString& face = wxEmptyString,
           wxFontEncoding encoding = wxFONTENCODING_DEFAULT)
    {
        Create(10, family, style, weight, underlined, face, encoding);
        SetPixelSize(pixelSize);
    }

    bool Create(int size,
                wxFontFamily family,
                wxFontStyle style,
                wxFontWeight weight,
                bool underlined = false,
                const wxString& face = wxEmptyString,
                wxFontEncoding encoding = wxFONTENCODING_DEFAULT);
    bool Create(const wxNativeFontInfo& fontinfo);

    // implement base class pure virtuals
    virtual double GetFractionalPointSize() const wxOVERRIDE;
    virtual wxSize GetPixelSize() const wxOVERRIDE;
    virtual bool IsUsingSizeInPixels() const wxOVERRIDE;
    virtual wxFontStyle GetStyle() const wxOVERRIDE;
    virtual int GetNumericWeight() const wxOVERRIDE;
    virtual wxString GetFaceName() const wxOVERRIDE;
    virtual bool GetUnderlined() const wxOVERRIDE;
    virtual bool GetStrikethrough() const wxOVERRIDE;
    virtual wxFontEncoding GetEncoding() const wxOVERRIDE;
    virtual bool IsFixedWidth() const wxOVERRIDE;
    virtual const wxNativeFontInfo *GetNativeFontInfo() const wxOVERRIDE;

    virtual void SetFractionalPointSize(double pointSize) wxOVERRIDE;
    virtual void SetPixelSize(const wxSize& pixelSize) wxOVERRIDE;
    virtual void SetFamily(wxFontFamily family) wxOVERRIDE;
    virtual void SetStyle(wxFontStyle style) wxOVERRIDE;
    virtual void SetNumericWeight(int weight) wxOVERRIDE;
    virtual bool SetFaceName(const wxString& faceName) wxOVERRIDE;
    virtual void SetUnderlined(bool underlined) wxOVERRIDE;
    virtual void SetStrikethrough(bool strikethrough) wxOVERRIDE;
    virtual void SetEncoding(wxFontEncoding encoding) wxOVERRIDE;

    wxDECLARE_COMMON_FONT_METHODS();

    wxDEPRECATED_MSG("use wxFONT{FAMILY,STYLE,WEIGHT}_XXX constants")
    wxFont(int size,
           int family,
           int style,
           int weight,
           bool underlined = false,
           const wxString& face = wxEmptyString,
           wxFontEncoding encoding = wxFONTENCODING_DEFAULT)
    {
        (void)Create(size, (wxFontFamily)family, (wxFontStyle)style,
                     (wxFontWeight)weight, underlined, face, encoding);
    }

    // implementation from now on
    // --------------------------

    // the native font (never NULL for a valid wxFont)
    const BFont *GetHaikuFont() const;

    // metrics in pixels, rounded up: cached, no server round trip after the
    // first call for a given font
    void HaikuGetMetrics(int *ascent, int *descent, int *leading) const;

    // width of the string in pixels: goes through a per font cache because
    // measuring is a synchronous app_server call
    int HaikuGetTextWidth(const wxString& text) const;

    // fills widths with the cumulative widths of the characters of the string
    bool HaikuGetPartialExtents(const wxString& text, wxArrayInt& widths) const;

    // font size in pixels of the native font
    double HaikuGetPixelHeight() const;

protected:
    virtual wxGDIRefData *CreateGDIRefData() const wxOVERRIDE;
    virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const wxOVERRIDE;

    virtual wxFontFamily DoGetFamily() const wxOVERRIDE;

private:
    wxDECLARE_DYNAMIC_CLASS(wxFont);
};

#endif // _WX_HAIKU_FONT_H_
