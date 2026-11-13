/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/font.cpp
// Purpose:     wxFont on BFont, font enumeration, private fonts, the cache
//              of text extents
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/log.h"
    #include "wx/gdicmn.h"
    #include "wx/arrstr.h"
#endif

#include "wx/font.h"
#include "wx/fontutil.h"
#include "wx/fontenum.h"
#include "wx/encinfo.h"
#include "wx/hashmap.h"
#include "wx/tokenzr.h"

#include "wx/haiku/private.h"

#include <map>

// ============================================================================
// system scale
// ============================================================================

namespace
{

double gs_scaleFactor = 0;

} // anonymous namespace

double wxHaikuScaleFactor()
{
    if ( gs_scaleFactor == 0 )
    {
        // air/OS scales the system fonts for HiDPI screens: 12 is the size
        // of the plain font at 100%
        gs_scaleFactor = be_plain_font ? be_plain_font->Size() / 12.0 : 1.0;
        if ( gs_scaleFactor < 0.5 )
            gs_scaleFactor = 1.0;
    }

    return gs_scaleFactor;
}

// ============================================================================
// the cache of font metrics and text extents
// ============================================================================

namespace
{

WX_DECLARE_STRING_HASH_MAP(int, TextWidthMap);

// Measuring text is a synchronous call to app_server and wx code does it all
// the time (layout, every paint of most controls): the widths of the strings
// are remembered per native font.
struct FontMetricsCache
{
    FontMetricsCache() : valid(false), ascent(0), descent(0), leading(0) { }

    bool valid;
    int ascent, descent, leading;
    TextWidthMap widths;
};

struct FontKey
{
    uint32 familyAndStyle;
    float size;
    float shear;
    float falseBold;
    uint16 face;

    bool operator<(const FontKey& other) const
    {
        if ( familyAndStyle != other.familyAndStyle )
            return familyAndStyle < other.familyAndStyle;
        if ( size != other.size )
            return size < other.size;
        if ( shear != other.shear )
            return shear < other.shear;
        if ( falseBold != other.falseBold )
            return falseBold < other.falseBold;
        return face < other.face;
    }
};

typedef std::map<FontKey, FontMetricsCache *> FontCacheMap;
FontCacheMap gs_fontCaches;

const size_t MAX_CACHED_WIDTHS = 8192;

FontMetricsCache *GetFontCache(const BFont& font)
{
    FontKey key;
    key.familyAndStyle = font.FamilyAndStyle();
    key.size = font.Size();
    key.shear = font.Shear();
    key.falseBold = font.FalseBoldWidth();
    key.face = font.Face();

    FontCacheMap::iterator it = gs_fontCaches.find(key);
    if ( it != gs_fontCaches.end() )
        return it->second;

    FontMetricsCache * const cache = new FontMetricsCache;
    gs_fontCaches[key] = cache;
    return cache;
}

void ClearFontCaches()
{
    for ( FontCacheMap::iterator it = gs_fontCaches.begin();
          it != gs_fontCaches.end();
          ++it )
    {
        // the fonts keep pointers to their caches: emptied, not deleted
        it->second->valid = false;
        it->second->widths.clear();
    }
}

// ----------------------------------------------------------------------------
// font matching
// ----------------------------------------------------------------------------

// the families of the private fonts loaded by this application
wxArrayString gs_privateFamilies;
std::vector<BFont *> gs_privateFonts;

// the wx weight a style name stands for
int WeightFromStyleName(const wxString& styleOrig)
{
    wxString style = styleOrig.Lower();
    style.Replace(" ", "");
    style.Replace("-", "");

    if ( style.Contains("thin") || style.Contains("hairline") )
        return wxFONTWEIGHT_THIN;
    if ( style.Contains("extralight") || style.Contains("ultralight") )
        return wxFONTWEIGHT_EXTRALIGHT;
    if ( style.Contains("semilight") || style.Contains("demilight") )
        return 350;
    if ( style.Contains("light") )
        return wxFONTWEIGHT_LIGHT;
    if ( style.Contains("medium") )
        return wxFONTWEIGHT_MEDIUM;
    if ( style.Contains("semibold") || style.Contains("demibold")
            || style.Contains("demi") )
        return wxFONTWEIGHT_SEMIBOLD;
    if ( style.Contains("extrabold") || style.Contains("ultrabold") )
        return wxFONTWEIGHT_EXTRABOLD;
    if ( style.Contains("extrablack") || style.Contains("ultrablack") )
        return wxFONTWEIGHT_EXTRAHEAVY;
    if ( style.Contains("black") || style.Contains("heavy") )
        return wxFONTWEIGHT_HEAVY;
    if ( style.Contains("bold") )
        return wxFONTWEIGHT_BOLD;

    return wxFONTWEIGHT_NORMAL;
}

bool IsItalicStyleName(const wxString& style)
{
    const wxString lower = style.Lower();
    return lower.Contains("italic") || lower.Contains("oblique");
}

bool FamilyExists(const wxString& name, font_family *familyOut = NULL)
{
    if ( name.empty() )
        return false;

    const wxScopedCharBuffer utf8 = name.utf8_str();
    if ( strlen(utf8) > B_FONT_FAMILY_LENGTH )
        return false;

    font_family family;
    strlcpy(family, utf8, sizeof(family));

    // Without the application object (a font made before wxApp is
    // initialized) app_server cannot be asked: the name is taken as it is,
    // the font falls back to the default family when it is first used if
    // there is no such family.
    if ( !be_app )
    {
        if ( familyOut )
            strlcpy(*familyOut, family, sizeof(*familyOut));
        return true;
    }

    // SetFamilyAndStyle() checks the name with app_server, which knows the
    // private fonts of this application too
    BFont font;
    if ( font.SetFamilyAndStyle(family, NULL) != B_OK )
        return false;

    font_family real;
    font_style style;
    font.GetFamilyAndStyle(&real, &style);
    if ( strcasecmp(real, family) != 0 )
        return false;

    if ( familyOut )
        strlcpy(*familyOut, real, sizeof(font_family));

    return true;
}

// The best matches for the families are looked up once per name.
WX_DECLARE_STRING_HASH_MAP(wxString, FamilyNameMap);
FamilyNameMap gs_familyNames;

// well known faces which are not installed are replaced by a similar one
wxString SubstituteFace(const wxString& face)
{
    FamilyNameMap::iterator it = gs_familyNames.find(face);
    if ( it != gs_familyNames.end() )
        return it->second;

    wxString result;
    font_family family;
    if ( FamilyExists(face, &family) )
    {
        result = wxString::FromUTF8(family);
    }
    else
    {
        // a face name with the style in it ("Foo Bold")?
        static const char *fixed[] =
            { "courier", "mono", "consolas", "fixed", "menlo", "terminal" };
        static const char *serif[] =
            { "times", "serif", "georgia", "roman", "garamond" };

        const wxString lower = face.Lower();
        bool found = false;
        for ( size_t n = 0; n < WXSIZEOF(fixed) && !found; n++ )
        {
            if ( lower.Contains(fixed[n]) )
            {
                font_family f;
                font_style s;
                be_fixed_font->GetFamilyAndStyle(&f, &s);
                result = wxString::FromUTF8(f);
                found = true;
            }
        }

        if ( !found && !lower.Contains("sans") )
        {
            for ( size_t n = 0; n < WXSIZEOF(serif) && !found; n++ )
            {
                if ( lower.Contains(serif[n]) && FamilyExists("Noto Serif") )
                {
                    result = "Noto Serif";
                    found = true;
                }
            }
        }
    }

    gs_familyNames[face] = result;
    return result;
}

} // anonymous namespace

void wxHaikuResetSystemSettings()
{
    gs_scaleFactor = 0;
    ClearFontCaches();
    gs_familyNames.clear();
}

// ============================================================================
// wxFontRefData
// ============================================================================

class wxFontRefData : public wxGDIRefData
{
public:
    wxFontRefData()
    {
        m_info.Init();
        m_info.pointSize = 9;
        m_info.family = wxFONTFAMILY_DEFAULT;
        m_info.style = wxFONTSTYLE_NORMAL;
        m_info.weight = wxFONTWEIGHT_NORMAL;
        m_info.underlined = false;
        m_info.strikethrough = false;
        m_info.encoding = wxFONTENCODING_UTF8;
        m_pixelHeight = 0;
        m_realized = false;
        m_cache = NULL;
    }

    wxFontRefData(const wxFontRefData& data)
        : wxGDIRefData(),
          m_info(data.m_info),
          m_pixelHeight(data.m_pixelHeight),
          m_realized(false),
          m_cache(NULL)
    {
    }

    // the attributes changed: the native font must be found again
    void Invalidate()
    {
        m_realized = false;
        m_cache = NULL;
    }

    void Realize() const;

    const BFont& GetFont() const
    {
        if ( !m_realized )
            Realize();
        return m_font;
    }

    FontMetricsCache *GetCache() const
    {
        if ( !m_realized )
            Realize();
        if ( !m_cache )
            m_cache = GetFontCache(m_font);
        return m_cache;
    }

    double GetPixelHeight() const
    {
        if ( m_pixelHeight > 0 )
            return m_pixelHeight;

        // points are 1/72 inch and the screen has 96 DPI at 100%
        return m_info.pointSize * 96.0 / 72.0 * wxHaikuScaleFactor();
    }

    wxNativeFontInfo m_info;

    // not 0 if the size was given in pixels
    double m_pixelHeight;

    mutable bool m_realized;
    mutable BFont m_font;
    mutable wxString m_realFace;
    mutable bool m_fixedWidth;
    mutable FontMetricsCache *m_cache;
};

void wxFontRefData::Realize() const
{
    m_realized = true;
    m_cache = NULL;

    // the family
    font_family family;
    font_style defaultStyle;

    const BFont *base;
    switch ( m_info.family )
    {
        case wxFONTFAMILY_TELETYPE:
        case wxFONTFAMILY_MODERN:
            base = be_fixed_font;
            break;

        default:
            base = be_plain_font;
    }

    base->GetFamilyAndStyle(&family, &defaultStyle);

    if ( !m_info.faceName.empty() )
    {
        const wxString face = SubstituteFace(m_info.faceName);
        if ( !face.empty() )
            strlcpy(family, face.utf8_str(), sizeof(family));
    }
    else if ( m_info.family == wxFONTFAMILY_ROMAN && FamilyExists("Noto Serif") )
    {
        strlcpy(family, "Noto Serif", sizeof(family));
    }

    // the style of the family closest to the weight and slant wanted
    const bool wantItalic = m_info.style != wxFONTSTYLE_NORMAL;
    const int wantWeight = m_info.weight;

    font_style bestStyle;
    bestStyle[0] = '\0';
    int bestScore = INT_MAX;
    bool bestItalic = false;
    int bestWeight = wxFONTWEIGHT_NORMAL;

    const int32 count = count_font_styles(family);
    for ( int32 i = 0; i < count; i++ )
    {
        font_style style;
        uint16 face;
        if ( get_font_style(family, i, &style, &face) != B_OK )
            continue;

        const wxString name = wxString::FromUTF8(style);
        const bool italic = (face & B_ITALIC_FACE) || IsItalicStyleName(name);
        const int weight = WeightFromStyleName(name);

        int score = abs(weight - wantWeight);
        if ( italic != wantItalic )
            score += 2000;

        // prefer the plain variants to the condensed, extended etc. ones
        const wxString lower = name.Lower();
        if ( lower.Contains("condensed") || lower.Contains("narrow")
                || lower.Contains("extended") || lower.Contains("expanded")
                || lower.Contains("caption") || lower.Contains("display") )
        {
            score += 500;
        }

        if ( score < bestScore )
        {
            bestScore = score;
            strlcpy(bestStyle, style, sizeof(bestStyle));
            bestItalic = italic;
            bestWeight = weight;
        }
    }

    BFont font(base);
    if ( font.SetFamilyAndStyle(family, bestStyle[0] ? bestStyle : NULL) != B_OK )
    {
        // a private font: its styles are not enumerated, try the names
        const char *style = wantWeight >= wxFONTWEIGHT_SEMIBOLD
                                ? (wantItalic ? "Bold Italic" : "Bold")
                                : (wantItalic ? "Italic" : "Regular");
        if ( font.SetFamilyAndStyle(family, style) != B_OK
                && font.SetFamilyAndStyle(family, NULL) != B_OK )
        {
            font = *base;
        }

        font_family f;
        font_style s;
        font.GetFamilyAndStyle(&f, &s);
        const wxString name = wxString::FromUTF8(s);
        bestItalic = IsItalicStyleName(name);
        bestWeight = WeightFromStyleName(name);
    }

    // what the family lacks is synthesized
    if ( wantItalic && !bestItalic )
        font.SetShear(102);
    if ( wantWeight >= wxFONTWEIGHT_SEMIBOLD && bestWeight < wxFONTWEIGHT_MEDIUM )
        font.SetFalseBoldWidth(0.6f);

    font.SetSize((float)(floor(GetPixelHeight() * 2 + 0.5) / 2));
    font.SetRotation(0);
    font.SetSpacing(B_BITMAP_SPACING);

    m_font = font;

    font_family realFamily;
    font_style realStyle;
    font.GetFamilyAndStyle(&realFamily, &realStyle);
    m_realFace = wxString::FromUTF8(realFamily);
    m_fixedWidth = font.IsFixed();
}

#define M_FONTDATA (static_cast<wxFontRefData *>(m_refData))

// ============================================================================
// wxFont
// ============================================================================

wxFont::wxFont(const wxFontInfo& info)
{
    Create(wxRound(info.GetFractionalPointSize()),
           info.GetFamily(),
           info.GetStyle(),
           info.GetWeight(),
           info.IsUnderlined(),
           info.GetFaceName(),
           info.GetEncoding());

    // (a wxFontInfo without size has -1 here: Create() used the default)
    if ( info.IsUsingSizeInPixels() )
        SetPixelSize(info.GetPixelSize());
    else if ( info.GetFractionalPointSize() > 0 )
        SetFractionalPointSize(info.GetFractionalPointSize());

    SetNumericWeight(info.GetNumericWeight());

    if ( info.IsStrikethrough() )
        SetStrikethrough(true);
}

wxFont::wxFont(const wxString& nativeFontInfoString)
{
    wxNativeFontInfo info;
    if ( info.FromString(nativeFontInfoString) )
        Create(info);
}

bool wxFont::Create(int size,
                    wxFontFamily family,
                    wxFontStyle style,
                    wxFontWeight weight,
                    bool underlined,
                    const wxString& face,
                    wxFontEncoding WXUNUSED(encoding))
{
    UnRef();

    wxFontRefData * const data = new wxFontRefData;
    m_refData = data;

    data->m_info.pointSize = size == wxDEFAULT || size <= 0
                                ? wxNORMAL_FONT->GetFractionalPointSize()
                                : size;
    data->m_info.family = family == wxFONTFAMILY_UNKNOWN ? wxFONTFAMILY_DEFAULT
                                                         : family;
    data->m_info.style = style;
    data->m_info.weight = weight;
    data->m_info.underlined = underlined;
    data->m_info.faceName = face;

    return true;
}

bool wxFont::Create(const wxNativeFontInfo& info)
{
    UnRef();

    wxFontRefData * const data = new wxFontRefData;
    m_refData = data;

    data->m_info = info;
    data->m_info.encoding = wxFONTENCODING_UTF8;

    return true;
}

wxGDIRefData *wxFont::CreateGDIRefData() const
{
    return new wxFontRefData;
}

wxGDIRefData *wxFont::CloneGDIRefData(const wxGDIRefData *data) const
{
    return new wxFontRefData(*static_cast<const wxFontRefData *>(data));
}

// ----------------------------------------------------------------------------
// accessors
// ----------------------------------------------------------------------------

double wxFont::GetFractionalPointSize() const
{
    wxCHECK_MSG( IsOk(), 0, wxS("invalid font") );

    if ( M_FONTDATA->m_pixelHeight > 0 )
        return M_FONTDATA->m_pixelHeight * 72.0 / 96.0 / wxHaikuScaleFactor();

    return M_FONTDATA->m_info.pointSize;
}

wxSize wxFont::GetPixelSize() const
{
    wxCHECK_MSG( IsOk(), wxSize(), wxS("invalid font") );

    return wxSize(0, wxRound(M_FONTDATA->GetPixelHeight()));
}

bool wxFont::IsUsingSizeInPixels() const
{
    wxCHECK_MSG( IsOk(), false, wxS("invalid font") );

    return M_FONTDATA->m_pixelHeight > 0;
}

double wxFont::HaikuGetPixelHeight() const
{
    wxCHECK_MSG( IsOk(), 0, wxS("invalid font") );

    return M_FONTDATA->GetFont().Size();
}

wxString wxFont::GetFaceName() const
{
    wxCHECK_MSG( IsOk(), wxString(), wxS("invalid font") );

    if ( !M_FONTDATA->m_info.faceName.empty() )
        return M_FONTDATA->m_info.faceName;

    M_FONTDATA->GetFont();
    return M_FONTDATA->m_realFace;
}

wxFontFamily wxFont::DoGetFamily() const
{
    return M_FONTDATA->m_info.family;
}

wxFontStyle wxFont::GetStyle() const
{
    wxCHECK_MSG( IsOk(), wxFONTSTYLE_MAX, wxS("invalid font") );

    return M_FONTDATA->m_info.style;
}

int wxFont::GetNumericWeight() const
{
    wxCHECK_MSG( IsOk(), wxFONTWEIGHT_MAX, wxS("invalid font") );

    return M_FONTDATA->m_info.weight;
}

bool wxFont::GetUnderlined() const
{
    wxCHECK_MSG( IsOk(), false, wxS("invalid font") );

    return M_FONTDATA->m_info.underlined;
}

bool wxFont::GetStrikethrough() const
{
    wxCHECK_MSG( IsOk(), false, wxS("invalid font") );

    return M_FONTDATA->m_info.strikethrough;
}

wxFontEncoding wxFont::GetEncoding() const
{
    wxCHECK_MSG( IsOk(), wxFONTENCODING_DEFAULT, wxS("invalid font") );

    return wxFONTENCODING_UTF8;
}

bool wxFont::IsFixedWidth() const
{
    wxCHECK_MSG( IsOk(), false, wxS("invalid font") );

    M_FONTDATA->GetFont();
    return M_FONTDATA->m_fixedWidth;
}

const wxNativeFontInfo *wxFont::GetNativeFontInfo() const
{
    wxCHECK_MSG( IsOk(), NULL, wxS("invalid font") );

    if ( M_FONTDATA->m_pixelHeight > 0 )
        M_FONTDATA->m_info.pointSize = GetFractionalPointSize();

    return &M_FONTDATA->m_info;
}

// ----------------------------------------------------------------------------
// setters
// ----------------------------------------------------------------------------

void wxFont::SetFractionalPointSize(double pointSize)
{
    AllocExclusive();

    // never a font without size: its metrics are all 0
    if ( pointSize <= 0 )
        pointSize = wxNORMAL_FONT->GetFractionalPointSize();

    M_FONTDATA->m_info.pointSize = pointSize;
    M_FONTDATA->m_pixelHeight = 0;
    M_FONTDATA->Invalidate();
}

void wxFont::SetPixelSize(const wxSize& pixelSize)
{
    AllocExclusive();

    M_FONTDATA->m_pixelHeight = pixelSize.y;
    M_FONTDATA->m_info.pointSize
        = pixelSize.y * 72.0 / 96.0 / wxHaikuScaleFactor();
    M_FONTDATA->Invalidate();
}

void wxFont::SetFamily(wxFontFamily family)
{
    AllocExclusive();

    M_FONTDATA->m_info.family = family;
    M_FONTDATA->Invalidate();
}

void wxFont::SetStyle(wxFontStyle style)
{
    AllocExclusive();

    M_FONTDATA->m_info.style = style;
    M_FONTDATA->Invalidate();
}

void wxFont::SetNumericWeight(int weight)
{
    AllocExclusive();

    M_FONTDATA->m_info.weight = weight;
    M_FONTDATA->Invalidate();
}

bool wxFont::SetFaceName(const wxString& faceName)
{
    AllocExclusive();

    M_FONTDATA->m_info.faceName = faceName;
    M_FONTDATA->Invalidate();

    return faceName.empty() || !SubstituteFace(faceName).empty();
}

void wxFont::SetUnderlined(bool underlined)
{
    AllocExclusive();

    M_FONTDATA->m_info.underlined = underlined;
}

void wxFont::SetStrikethrough(bool strikethrough)
{
    AllocExclusive();

    M_FONTDATA->m_info.strikethrough = strikethrough;
}

void wxFont::SetEncoding(wxFontEncoding WXUNUSED(encoding))
{
    // all text is UTF-8
}

// ----------------------------------------------------------------------------
// native font and metrics
// ----------------------------------------------------------------------------

const BFont *wxFont::GetHaikuFont() const
{
    wxCHECK_MSG( IsOk(), be_plain_font, wxS("invalid font") );

    return &M_FONTDATA->GetFont();
}

void wxFont::HaikuGetMetrics(int *ascent, int *descent, int *leading) const
{
    FontMetricsCache * const cache = M_FONTDATA->GetCache();
    if ( !cache->valid )
    {
        font_height height;
        M_FONTDATA->GetFont().GetHeight(&height);
        cache->ascent = (int)ceilf(height.ascent);
        cache->descent = (int)ceilf(height.descent);
        cache->leading = (int)ceilf(height.leading);
        cache->valid = true;
    }

    if ( ascent )
        *ascent = cache->ascent;
    if ( descent )
        *descent = cache->descent;
    if ( leading )
        *leading = cache->leading;
}

int wxFont::HaikuGetTextWidth(const wxString& text) const
{
    if ( text.empty() )
        return 0;

    FontMetricsCache * const cache = M_FONTDATA->GetCache();

    TextWidthMap::iterator it = cache->widths.find(text);
    if ( it != cache->widths.end() )
        return it->second;

    const wxScopedCharBuffer utf8 = text.utf8_str();
    const int width = (int)ceilf(
        M_FONTDATA->GetFont().StringWidth(utf8, (int32)utf8.length()));

    if ( cache->widths.size() >= MAX_CACHED_WIDTHS )
        cache->widths.clear();
    cache->widths[text] = width;

    return width;
}

bool wxFont::HaikuGetPartialExtents(const wxString& text,
                                    wxArrayInt& widths) const
{
    widths.Empty();

    const size_t len = text.length();
    if ( !len )
        return true;

    widths.Add(0, len);

    const BFont& font = M_FONTDATA->GetFont();

    // one request for the escapements of all the characters
    const wxScopedCharBuffer utf8 = text.utf8_str();
    std::vector<BPoint> escapements(len);
    escapement_delta delta = { 0, 0 };
    font.GetEscapements(utf8, (int32)len, &delta, &escapements[0]);

    const float size = font.Size();
    double x = 0;
    for ( size_t n = 0; n < len; n++ )
    {
        x += escapements[n].x * size;
        widths[n] = (int)ceil(x);
    }

    // the whole string is as wide as GetTextExtent() says
    const int total = HaikuGetTextWidth(text);
    if ( widths[len - 1] != total && widths[len - 1] > 0 )
    {
        const double factor = (double)total / widths[len - 1];
        for ( size_t n = 0; n < len; n++ )
            widths[n] = (int)(widths[n] * factor + 0.5);
        widths[len - 1] = total;
    }

    return true;
}

// ----------------------------------------------------------------------------
// private fonts
// ----------------------------------------------------------------------------

#if wxUSE_PRIVATE_FONTS

/* static */
bool wxFontBase::AddPrivateFont(const wxString& filename)
{
    // app_server loads the file for this application only; the font stays
    // usable by its family name for as long as the BFont is not unloaded
    BFont * const font = new BFont;
    const status_t status = font->LoadFont(filename.utf8_str());
    if ( status != B_OK )
    {
        delete font;
        wxLogError(_("Font file \"%s\" couldn't be loaded"), filename);
        return false;
    }

    gs_privateFonts.push_back(font);

    font_family family;
    font_style style;
    font->GetFamilyAndStyle(&family, &style);

    const wxString name = wxString::FromUTF8(family);
    if ( gs_privateFamilies.Index(name) == wxNOT_FOUND )
        gs_privateFamilies.Add(name);

    // names looked up before may resolve differently now
    gs_familyNames.clear();

    return true;
}

#endif // wxUSE_PRIVATE_FONTS

// ============================================================================
// wxFontEnumerator
// ============================================================================

#if wxUSE_FONTENUM

bool wxFontEnumerator::EnumerateFacenames(wxFontEncoding WXUNUSED(encoding),
                                          bool fixedWidthOnly)
{
    wxArrayString names;

    const int32 count = count_font_families();
    for ( int32 i = 0; i < count; i++ )
    {
        font_family family;
        uint32 flags;
        if ( get_font_family(i, &family, &flags) != B_OK )
            continue;

        if ( fixedWidthOnly && !(flags & B_IS_FIXED) )
            continue;

        names.Add(wxString::FromUTF8(family));
    }

    if ( !fixedWidthOnly )
    {
        for ( size_t n = 0; n < gs_privateFamilies.size(); n++ )
        {
            if ( names.Index(gs_privateFamilies[n]) == wxNOT_FOUND )
                names.Add(gs_privateFamilies[n]);
        }
    }

    names.Sort();

    for ( size_t n = 0; n < names.size(); n++ )
    {
        if ( !OnFacename(names[n]) )
            break;
    }

    return true;
}

bool wxFontEnumerator::EnumerateEncodings(const wxString& WXUNUSED(facename))
{
    // all fonts are Unicode ones
    OnFontEncoding(wxEmptyString, "UTF-8");

    return true;
}

#endif // wxUSE_FONTENUM
