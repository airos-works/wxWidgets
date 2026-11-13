/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dc.cpp
// Purpose:     wxHaikuDCImpl: wxDC drawing through a BView
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/dcmemory.h"
    #include "wx/log.h"
    #include "wx/icon.h"
    #include "wx/image.h"
#endif

#include "wx/haiku/dc.h"
#include "wx/haiku/private.h"

#include <AffineTransform.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <Polygon.h>

#include <math.h>

#if wxUSE_IMAGE
// implemented in src/common/image.cpp
extern bool wxDoFloodFill(wxDC *dc, wxCoord x, wxCoord y,
                          const wxColour& col, wxFloodFillStyle style);
#endif

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

namespace
{

// 8x8 patterns for the hatched brushes: the set bits are drawn in the brush
// colour, the others are left alone (B_OP_OVER)
const pattern kPatternBDiagonal = { { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 } };
const pattern kPatternFDiagonal = { { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 } };
const pattern kPatternCrossDiag = { { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 } };
const pattern kPatternHorizontal = { { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
const pattern kPatternVertical = { { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 } };
const pattern kPatternCross = { { 0xff, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 } };

// patterns for the dotted and dashed pens: they are aligned to the screen,
// not to the line, which is good enough for the thin straight lines such
// pens are used for (focus rectangles, rubber bands)
const pattern kPatternDot = { { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 } };
const pattern kPatternShortDash = { { 0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0x33, 0x33 } };
const pattern kPatternLongDash = { { 0xf0, 0xf0, 0xf0, 0xf0, 0x0f, 0x0f, 0x0f, 0x0f } };

pattern BrushPattern(const wxBrush& brush)
{
    switch ( brush.GetStyle() )
    {
        case wxBRUSHSTYLE_BDIAGONAL_HATCH:  return kPatternBDiagonal;
        case wxBRUSHSTYLE_FDIAGONAL_HATCH:  return kPatternFDiagonal;
        case wxBRUSHSTYLE_CROSSDIAG_HATCH:  return kPatternCrossDiag;
        case wxBRUSHSTYLE_HORIZONTAL_HATCH: return kPatternHorizontal;
        case wxBRUSHSTYLE_VERTICAL_HATCH:   return kPatternVertical;
        case wxBRUSHSTYLE_CROSS_HATCH:      return kPatternCross;
        default:                            return B_SOLID_HIGH;
    }
}

pattern PenPattern(const wxPen& pen)
{
    switch ( pen.GetStyle() )
    {
        case wxPENSTYLE_DOT:        return kPatternDot;
        case wxPENSTYLE_SHORT_DASH: return kPatternShortDash;
        case wxPENSTYLE_LONG_DASH:
        case wxPENSTYLE_DOT_DASH:
        case wxPENSTYLE_USER_DASH:  return kPatternLongDash;
        default:                    return B_SOLID_HIGH;
    }
}

cap_mode HaikuCap(wxPenCap cap)
{
    switch ( cap )
    {
        case wxCAP_PROJECTING: return B_SQUARE_CAP;
        case wxCAP_BUTT:       return B_BUTT_CAP;
        default:               return B_ROUND_CAP;
    }
}

join_mode HaikuJoin(wxPenJoin join)
{
    switch ( join )
    {
        case wxJOIN_BEVEL: return B_BEVEL_JOIN;
        case wxJOIN_MITER: return B_MITER_JOIN;
        default:           return B_ROUND_JOIN;
    }
}

} // anonymous namespace

// ============================================================================
// wxHaikuDCImpl
// ============================================================================

wxIMPLEMENT_ABSTRACT_CLASS(wxHaikuDCImpl, wxDCImpl);

wxHaikuDCImpl::wxHaikuDCImpl(wxDC *owner)
    : wxDCImpl(owner),
      m_view(NULL),
      m_targetAlpha(false),
      m_hasBaseClip(false),
      m_clipDirty(true),
      m_lockCount(0),
      m_pattern(new pattern[1])
{
    m_ok = false;
    m_pattern[0] = B_SOLID_HIGH;

    m_font = *wxNORMAL_FONT;
    m_pen = *wxBLACK_PEN;
    m_brush = *wxWHITE_BRUSH;
    m_backgroundBrush = *wxWHITE_BRUSH;
}

wxHaikuDCImpl::~wxHaikuDCImpl()
{
    // the view may be used by another DC next: it must not think that its
    // state is still ours if a new DC happens to get the same address
    if ( m_view )
    {
        wxHaikuDrawView * const view = dynamic_cast<wxHaikuDrawView *>(m_view);
        if ( view && view->m_stateOwner == this )
            view->m_stateOwner = NULL;
    }

    delete [] m_pattern;
}

void wxHaikuDCImpl::HaikuInit(BView *view, const wxPoint& offset,
                              const wxSize& size, bool targetAlpha)
{
    m_view = view;
    m_offset = offset;
    m_size = size;
    m_targetAlpha = targetAlpha;
    m_ok = view != NULL;
    m_clipDirty = true;
}

void wxHaikuDCImpl::HaikuSetBaseClip(const wxRegion& region)
{
    m_baseClip = region;
    m_hasBaseClip = true;
    m_clipDirty = true;
}

void wxHaikuDCImpl::HaikuResetBaseClip()
{
    m_baseClip.Clear();
    m_hasBaseClip = false;
    m_clipDirty = true;
}

bool wxHaikuDCImpl::HaikuGetClip(wxRegion& region) const
{
    if ( m_clipping )
    {
        region = m_userClip;
        if ( !region.IsEmpty() )
            region.Offset(m_offset);

        if ( m_hasBaseClip )
            region.Intersect(m_baseClip);

        return true;
    }

    if ( m_hasBaseClip )
    {
        region = m_baseClip;
        return true;
    }

    return false;
}

// ----------------------------------------------------------------------------
// locking and state
// ----------------------------------------------------------------------------

bool wxHaikuDCImpl::HaikuBeginRawDraw(bool applyClip)
{
    if ( !m_view || !m_view->Window() || !m_view->LockLooper() )
        return false;

    m_lockCount++;

    wxHaikuDrawView * const view = dynamic_cast<wxHaikuDrawView *>(m_view);
    if ( !view || view->m_stateOwner != this )
    {
        if ( view )
        {
            view->m_stateOwner = this;

            if ( view->m_transformed )
            {
                m_view->SetTransform(BAffineTransform());
                view->m_transformed = false;
            }
        }

        m_clipDirty = true;
    }

    if ( applyClip && m_clipDirty )
    {
        wxRegion clip;
        if ( HaikuGetClip(clip) )
        {
            BRegion region;
            if ( clip.GetHaikuRegion() )
                region = *clip.GetHaikuRegion();
            m_view->ConstrainClippingRegion(&region);
        }
        else
        {
            m_view->ConstrainClippingRegion(NULL);
        }

        m_clipDirty = false;
    }

    return true;
}

void wxHaikuDCImpl::HaikuEndRawDraw()
{
    wxCHECK_RET( m_lockCount > 0, wxS("unbalanced HaikuEndRawDraw()") );

    if ( --m_lockCount == 0 )
        HaikuTargetChanged();

    m_view->UnlockLooper();
}

bool wxHaikuDCImpl::HaikuBegin(DrawWith what)
{
    if ( !m_view || m_logicalFunction == wxNO_OP )
        return false;

    wxColour colour;
    pattern pat = B_SOLID_HIGH;

    switch ( what )
    {
        case Draw_Pen:
            if ( !m_pen.IsOk() || m_pen.GetStyle() == wxPENSTYLE_TRANSPARENT )
                return false;
            colour = m_pen.GetColour();
            pat = PenPattern(m_pen);
            break;

        case Draw_Brush:
            if ( !m_brush.IsOk()
                    || m_brush.GetStyle() == wxBRUSHSTYLE_TRANSPARENT )
                return false;
            colour = m_brush.GetColour();
            break;

        case Draw_Text:
            colour = m_textForegroundColour;
            break;

        case Draw_Bitmap:
            break;
    }

    if ( what != Draw_Bitmap && (!colour.IsOk() || colour.Alpha() == 0) )
        return false;

    if ( !HaikuBeginRawDraw() )
        return false;

    // the state of the view for this operation: BView filters out the calls
    // which don't change anything, so only what is needed is sent
    drawing_mode mode;
    bool alphaMode = false;
    switch ( m_logicalFunction )
    {
        case wxINVERT:
        case wxXOR:
        case wxEQUIV:
        case wxSRC_INVERT:
        case wxOR_REVERSE:
        case wxAND_REVERSE:
            mode = B_OP_INVERT;
            break;

        case wxAND:
            mode = B_OP_MIN;
            break;

        case wxOR:
            mode = B_OP_MAX;
            break;

        case wxCLEAR:
            colour = *wxBLACK;
            mode = B_OP_OVER;
            break;

        case wxSET:
            colour = *wxWHITE;
            mode = B_OP_OVER;
            break;

        default:
            if ( m_targetAlpha || (colour.IsOk() && colour.Alpha() != 255) )
            {
                mode = B_OP_ALPHA;
                alphaMode = true;
            }
            else
            {
                // B_OP_OVER blends the anti-aliased edges with what is
                // there (B_OP_COPY blends them with the low colour) and
                // leaves the unset bits of the patterns alone
                mode = B_OP_OVER;
            }
    }

    m_view->SetDrawingMode(mode);
    if ( alphaMode )
    {
        // With B_ALPHA_COMPOSITE the alpha channel of a bitmap target is
        // maintained (source over); B_ALPHA_OVERLAY sets it to opaque.
        m_view->SetBlendingMode(B_PIXEL_ALPHA,
                                m_targetAlpha ? B_ALPHA_COMPOSITE
                                              : B_ALPHA_OVERLAY);
    }

    if ( what != Draw_Bitmap )
        m_view->SetHighColor(wxHaikuColour(colour));

    if ( what == Draw_Pen )
    {
        const int width = m_pen.GetWidth();
        const float penSize = width <= 1 ? 1.0f
                                         : (float)(width * m_scaleX);
        m_view->SetPenSize(penSize < 1 ? 1 : penSize);

        if ( penSize > 1 )
        {
            m_view->SetLineMode(HaikuCap(m_pen.GetCap()),
                                HaikuJoin(m_pen.GetJoin()));
        }
        else
        {
            m_view->SetLineMode(B_BUTT_CAP, B_MITER_JOIN);
        }
    }

    m_pattern[0] = pat;

    return true;
}

void wxHaikuDCImpl::HaikuEnd()
{
    HaikuEndRawDraw();
}

void wxHaikuDCImpl::Flush()
{
    if ( m_view && m_view->Window() && m_view->LockLooper() )
    {
        m_view->Flush();
        m_view->UnlockLooper();
    }
}

// ----------------------------------------------------------------------------
// attributes
// ----------------------------------------------------------------------------

void wxHaikuDCImpl::SetFont(const wxFont& font)
{
    m_font = font;
}

void wxHaikuDCImpl::SetPen(const wxPen& pen)
{
    m_pen = pen;
}

void wxHaikuDCImpl::SetBrush(const wxBrush& brush)
{
    m_brush = brush;
}

void wxHaikuDCImpl::SetBackground(const wxBrush& brush)
{
    m_backgroundBrush = brush;
}

void wxHaikuDCImpl::SetBackgroundMode(int mode)
{
    m_backgroundMode = mode;
}

#if wxUSE_PALETTE
void wxHaikuDCImpl::SetPalette(const wxPalette& WXUNUSED(palette))
{
}
#endif // wxUSE_PALETTE

void wxHaikuDCImpl::SetLogicalFunction(wxRasterOperationMode function)
{
    m_logicalFunction = function;
}

int wxHaikuDCImpl::GetDepth() const
{
    return m_targetAlpha ? 32 : 24;
}

wxSize wxHaikuDCImpl::GetPPI() const
{
    const int dpi = wxRound(96 * wxHaikuScaleFactor());
    return wxSize(dpi, dpi);
}

void wxHaikuDCImpl::DoGetSize(int *width, int *height) const
{
    if ( width )
        *width = m_size.x;
    if ( height )
        *height = m_size.y;
}

void wxHaikuDCImpl::DoGetSizeMM(int *width, int *height) const
{
    const wxSize ppi = GetPPI();

    if ( width )
        *width = wxRound(m_size.x * 25.4 / ppi.x);
    if ( height )
        *height = wxRound(m_size.y * 25.4 / ppi.y);
}

// ----------------------------------------------------------------------------
// clipping
// ----------------------------------------------------------------------------

void wxHaikuDCImpl::DoSetClippingRegion(wxCoord x, wxCoord y,
                                        wxCoord width, wxCoord height)
{
    wxRect rect(LogicalToDeviceX(x), LogicalToDeviceY(y),
                LogicalToDeviceXRel(width), LogicalToDeviceYRel(height));
    if ( rect.width < 0 )
    {
        rect.x += rect.width;
        rect.width = -rect.width;
    }
    if ( rect.height < 0 )
    {
        rect.y += rect.height;
        rect.height = -rect.height;
    }

    if ( m_clipping )
        m_userClip.Intersect(rect);
    else
        m_userClip = wxRegion(rect);

    // keeps the clipping box in logical coordinates and sets m_clipping
    wxDCImpl::DoSetClippingRegion(x, y, width, height);

    m_clipDirty = true;
}

void wxHaikuDCImpl::DoSetDeviceClippingRegion(const wxRegion& region)
{
    if ( m_clipping )
        m_userClip.Intersect(region);
    else
        m_userClip = region;

    if ( !m_userClip.GetHaikuRegion() )
        m_userClip = wxRegion(0, 0, 0, 0);

    m_clipping = true;

    const wxRect box = m_userClip.GetBox();
    m_clipX1 = DeviceToLogicalX(box.x);
    m_clipY1 = DeviceToLogicalY(box.y);
    m_clipX2 = DeviceToLogicalX(box.x + box.width);
    m_clipY2 = DeviceToLogicalY(box.y + box.height);

    m_clipDirty = true;
}

void wxHaikuDCImpl::DestroyClippingRegion()
{
    ResetClipping();
    m_userClip.Clear();
    m_clipDirty = true;
}

// ----------------------------------------------------------------------------
// text metrics
// ----------------------------------------------------------------------------

wxCoord wxHaikuDCImpl::GetCharHeight() const
{
    if ( !m_font.IsOk() )
        return 0;

    int ascent, descent;
    m_font.HaikuGetMetrics(&ascent, &descent, NULL);
    return DeviceToLogicalYRel(wxRound((ascent + descent) * m_scaleY));
}

wxCoord wxHaikuDCImpl::GetCharWidth() const
{
    if ( !m_font.IsOk() )
        return 0;

    return DeviceToLogicalXRel(wxRound(m_font.HaikuGetTextWidth(wxS("0")) * m_scaleX));
}

void wxHaikuDCImpl::DoGetTextExtent(const wxString& string,
                                    wxCoord *x, wxCoord *y,
                                    wxCoord *descent,
                                    wxCoord *externalLeading,
                                    const wxFont *theFont) const
{
    const wxFont& font = theFont && theFont->IsOk() ? *theFont : m_font;

    int ascent = 0, fontDescent = 0, leading = 0, width = 0;
    if ( font.IsOk() )
    {
        font.HaikuGetMetrics(&ascent, &fontDescent, &leading);
        width = font.HaikuGetTextWidth(string);
    }

    // the font is not scaled by the DC scale: its pixel size is, when the
    // text is drawn, so the logical extents are those of the font itself
    if ( x )
        *x = width;
    if ( y )
        *y = ascent + fontDescent;
    if ( descent )
        *descent = fontDescent;
    if ( externalLeading )
        *externalLeading = leading;
}

void wxHaikuDCImpl::DoGetFontMetrics(int *height,
                                     int *ascent,
                                     int *descent,
                                     int *internalLeading,
                                     int *externalLeading,
                                     int *averageWidth) const
{
    int fontAscent = 0, fontDescent = 0, leading = 0;
    if ( m_font.IsOk() )
        m_font.HaikuGetMetrics(&fontAscent, &fontDescent, &leading);

    if ( height )
        *height = fontAscent + fontDescent;
    if ( ascent )
        *ascent = fontAscent;
    if ( descent )
        *descent = fontDescent;
    if ( internalLeading )
        *internalLeading = 0;
    if ( externalLeading )
        *externalLeading = leading;
    if ( averageWidth )
    {
        // as the generic implementation does
        static const wxString sample
            = wxS("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
        *averageWidth = m_font.IsOk()
            ? (m_font.HaikuGetTextWidth(sample) / 26 + 1) / 2 : 0;
    }
}

bool wxHaikuDCImpl::DoGetPartialTextExtents(const wxString& text,
                                            wxArrayInt& widths) const
{
    if ( !m_font.IsOk() )
        return false;

    return m_font.HaikuGetPartialExtents(text, widths);
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

void wxHaikuDCImpl::Clear()
{
    if ( !m_view )
        return;

    wxColour colour = m_backgroundBrush.IsOk() ? m_backgroundBrush.GetColour()
                                               : *wxWHITE;
    if ( !colour.IsOk() )
        colour = *wxWHITE;

    if ( m_targetAlpha && colour.Alpha() != 255 )
    {
        // app_server cannot fill with a translucent colour without blending
        // it with what is there: done on the pixels by the bitmap DC
        if ( HaikuClearTranslucent(colour) )
            return;
    }

    if ( !HaikuBeginRawDraw() )
        return;

    m_view->SetDrawingMode(B_OP_COPY);
    m_view->SetHighColor(wxHaikuColour(wxColour(colour.Red(), colour.Green(),
                                                colour.Blue())));
    m_view->FillRect(wxHaikuRect(m_offset.x, m_offset.y, m_size.x, m_size.y));

    HaikuEnd();
}

bool wxHaikuDCImpl::HaikuClearTranslucent(const wxColour& WXUNUSED(colour))
{
    return false;
}

void wxHaikuDCImpl::DoDrawPoint(wxCoord x, wxCoord y)
{
    if ( !HaikuBegin(Draw_Pen) )
        return;

    const BPoint pt(XV(x), YV(y));
    m_view->SetPenSize(1);
    m_view->StrokeLine(pt, pt);

    CalcBoundingBox(x, y);

    HaikuEnd();
}

void wxHaikuDCImpl::DoDrawLine(wxCoord x1, wxCoord y1, wxCoord x2, wxCoord y2)
{
    if ( !HaikuBegin(Draw_Pen) )
        return;

    int vx1 = XV(x1), vy1 = YV(y1),
        vx2 = XV(x2), vy2 = YV(y2);

    // The end point is not part of the line, as wxDC documents and wxUniv
    // counts on; app_server draws it. For thin horizontal and vertical lines
    // this is exact, the others are not shortened: one more anti-aliased
    // pixel at the end of a diagonal does not show.
    if ( m_pen.GetWidth() <= 1 )
    {
        if ( vy1 == vy2 && vx1 != vx2 )
            vx2 += vx2 > vx1 ? -1 : 1;
        else if ( vx1 == vx2 && vy1 != vy2 )
            vy2 += vy2 > vy1 ? -1 : 1;
    }

    m_view->StrokeLine(BPoint(vx1, vy1), BPoint(vx2, vy2), m_pattern[0]);

    CalcBoundingBox(x1, y1);
    CalcBoundingBox(x2, y2);

    HaikuEnd();
}

void wxHaikuDCImpl::DoCrossHair(wxCoord x, wxCoord y)
{
    if ( !HaikuBegin(Draw_Pen) )
        return;

    const int vx = XV(x), vy = YV(y);
    m_view->StrokeLine(BPoint(m_offset.x, vy),
                       BPoint(m_offset.x + m_size.x - 1, vy), m_pattern[0]);
    m_view->StrokeLine(BPoint(vx, m_offset.y),
                       BPoint(vx, m_offset.y + m_size.y - 1), m_pattern[0]);

    HaikuEnd();
}

void wxHaikuDCImpl::DoDrawArc(wxCoord x1, wxCoord y1,
                              wxCoord x2, wxCoord y2,
                              wxCoord xc, wxCoord yc)
{
    // the arc goes counter-clockwise from (x1, y1) to (x2, y2)
    const double dx1 = x1 - xc, dy1 = y1 - yc;
    const double radius = sqrt(dx1 * dx1 + dy1 * dy1);

    double start, end;
    if ( x1 == x2 && y1 == y2 )
    {
        start = 0;
        end = 360;
    }
    else
    {
        start = atan2(-dy1, dx1) * 180 / M_PI;
        end = atan2(-(double)(y2 - yc), (double)(x2 - xc)) * 180 / M_PI;
        while ( end <= start )
            end += 360;
    }

    const BPoint centre(XV(xc), YV(yc));
    const float rx = (float)(radius * m_scaleX),
                ry = (float)(radius * m_scaleY);

    if ( HaikuBegin(Draw_Brush) )
    {
        m_view->FillArc(centre, rx, ry, (float)start, (float)(end - start),
                        BrushPattern(m_brush));
        HaikuEnd();
    }

    if ( HaikuBegin(Draw_Pen) )
    {
        m_view->StrokeArc(centre, rx, ry, (float)start, (float)(end - start),
                          m_pattern[0]);

        // the sides of the pie if it is filled
        if ( m_brush.IsOk() && m_brush.GetStyle() != wxBRUSHSTYLE_TRANSPARENT
                && end - start < 360 )
        {
            m_view->StrokeLine(centre, BPoint(XV(x1), YV(y1)));
            m_view->StrokeLine(centre, BPoint(XV(x2), YV(y2)));
        }

        HaikuEnd();
    }

    CalcBoundingBox((wxCoord)(xc - radius), (wxCoord)(yc - radius));
    CalcBoundingBox((wxCoord)(xc + radius), (wxCoord)(yc + radius));
}

void wxHaikuDCImpl::DoDrawEllipticArc(wxCoord x, wxCoord y,
                                      wxCoord w, wxCoord h,
                                      double sa, double ea)
{
    const BRect rect = wxHaikuRect(XV(x), YV(y), wxMax(WV(w), 1), wxMax(HV(h), 1));

    double sweep = ea - sa;
    while ( sweep <= 0 )
        sweep += 360;

    if ( HaikuBegin(Draw_Brush) )
    {
        m_view->FillArc(rect, (float)sa, (float)sweep, BrushPattern(m_brush));
        HaikuEnd();
    }

    if ( HaikuBegin(Draw_Pen) )
    {
        m_view->StrokeArc(rect, (float)sa, (float)sweep, m_pattern[0]);
        HaikuEnd();
    }

    CalcBoundingBox(x, y);
    CalcBoundingBox(x + w, y + h);
}

void wxHaikuDCImpl::DoDrawRectangle(wxCoord x, wxCoord y,
                                    wxCoord width, wxCoord height)
{
    int vx = XV(x), vy = YV(y), vw = WV(width), vh = HV(height);
    if ( vw < 0 )
    {
        vx += vw;
        vw = -vw;
    }
    if ( vh < 0 )
    {
        vy += vh;
        vh = -vh;
    }

    if ( vw == 0 || vh == 0 )
        return;

    const BRect rect = wxHaikuRect(vx, vy, vw, vh);

    if ( HaikuBegin(Draw_Brush) )
    {
        if ( m_brush.GetStyle() == wxBRUSHSTYLE_STIPPLE
                || m_brush.GetStyle() == wxBRUSHSTYLE_STIPPLE_MASK_OPAQUE
                || m_brush.GetStyle() == wxBRUSHSTYLE_STIPPLE_MASK )
        {
            const wxBitmap * const stipple = m_brush.GetStipple();
            if ( stipple && stipple->IsOk() )
            {
                const bool blend = stipple->HaikuNeedsAlphaBlend(true);
                if ( blend )
                {
                    m_view->SetDrawingMode(B_OP_ALPHA);
                    m_view->SetBlendingMode(B_PIXEL_ALPHA,
                                            m_targetAlpha ? B_ALPHA_COMPOSITE
                                                          : B_ALPHA_OVERLAY);
                }
                else
                {
                    m_view->SetDrawingMode(B_OP_COPY);
                }

                m_view->DrawTiledBitmapAsync(stipple->GetHaikuDrawBitmap(true),
                                             rect, BPoint(0, 0));
                wxHaikuBitmapDrawn(m_view, *stipple);
            }
        }
        else
        {
            m_view->FillRect(rect, BrushPattern(m_brush));
        }

        HaikuEnd();
    }

    if ( HaikuBegin(Draw_Pen) )
    {
        m_view->StrokeRect(rect, m_pattern[0]);
        HaikuEnd();
    }

    CalcBoundingBox(x, y);
    CalcBoundingBox(x + width, y + height);
}

void wxHaikuDCImpl::DoDrawRoundedRectangle(wxCoord x, wxCoord y,
                                           wxCoord width, wxCoord height,
                                           double radius)
{
    if ( radius < 0 )
        radius = -radius * wxMin(width, height);

    int vx = XV(x), vy = YV(y), vw = WV(width), vh = HV(height);
    if ( vw <= 0 || vh <= 0 )
        return;

    const BRect rect = wxHaikuRect(vx, vy, vw, vh);
    const float rx = (float)(radius * m_scaleX),
                ry = (float)(radius * m_scaleY);

    if ( HaikuBegin(Draw_Brush) )
    {
        m_view->FillRoundRect(rect, rx, ry, BrushPattern(m_brush));
        HaikuEnd();
    }

    if ( HaikuBegin(Draw_Pen) )
    {
        m_view->StrokeRoundRect(rect, rx, ry, m_pattern[0]);
        HaikuEnd();
    }

    CalcBoundingBox(x, y);
    CalcBoundingBox(x + width, y + height);
}

void wxHaikuDCImpl::DoDrawEllipse(wxCoord x, wxCoord y,
                                  wxCoord width, wxCoord height)
{
    int vx = XV(x), vy = YV(y), vw = WV(width), vh = HV(height);
    if ( vw <= 0 || vh <= 0 )
        return;

    const BRect rect = wxHaikuRect(vx, vy, vw, vh);

    if ( HaikuBegin(Draw_Brush) )
    {
        m_view->FillEllipse(rect, BrushPattern(m_brush));
        HaikuEnd();
    }

    if ( HaikuBegin(Draw_Pen) )
    {
        m_view->StrokeEllipse(rect, m_pattern[0]);
        HaikuEnd();
    }

    CalcBoundingBox(x, y);
    CalcBoundingBox(x + width, y + height);
}

void wxHaikuDCImpl::DoDrawLines(int n, const wxPoint points[],
                                wxCoord xoffset, wxCoord yoffset)
{
    if ( n < 2 || !HaikuBegin(Draw_Pen) )
        return;

    std::vector<BPoint> pts(n);
    for ( int i = 0; i < n; i++ )
    {
        pts[i].Set(XV(points[i].x + xoffset), YV(points[i].y + yoffset));
        CalcBoundingBox(points[i].x + xoffset, points[i].y + yoffset);
    }

    if ( n == 2 )
        m_view->StrokeLine(pts[0], pts[1], m_pattern[0]);
    else
        m_view->StrokePolygon(&pts[0], n, false, m_pattern[0]);

    HaikuEnd();
}

void wxHaikuDCImpl::DoDrawPolygon(int n, const wxPoint points[],
                                  wxCoord xoffset, wxCoord yoffset,
                                  wxPolygonFillMode fillStyle)
{
    if ( n < 2 )
        return;

    std::vector<BPoint> pts(n);
    for ( int i = 0; i < n; i++ )
    {
        pts[i].Set(XV(points[i].x + xoffset), YV(points[i].y + yoffset));
        CalcBoundingBox(points[i].x + xoffset, points[i].y + yoffset);
    }

    if ( n > 2 && HaikuBegin(Draw_Brush) )
    {
        m_view->SetFillRule(fillStyle == wxWINDING_RULE ? B_NONZERO
                                                        : B_EVEN_ODD);
        m_view->FillPolygon(&pts[0], n, BrushPattern(m_brush));
        HaikuEnd();
    }

    if ( HaikuBegin(Draw_Pen) )
    {
        m_view->StrokePolygon(&pts[0], n, true, m_pattern[0]);
        HaikuEnd();
    }
}

void wxHaikuDCImpl::DoGradientFillLinear(const wxRect& rect,
                                         const wxColour& initialColour,
                                         const wxColour& destColour,
                                         wxDirection nDirection)
{
    const int vx = XV(rect.x), vy = YV(rect.y),
              vw = WV(rect.width), vh = HV(rect.height);
    if ( vw <= 0 || vh <= 0 || !m_view || m_logicalFunction == wxNO_OP )
        return;

    if ( !HaikuBeginRawDraw() )
        return;

    const BRect r = wxHaikuRect(vx, vy, vw, vh);

    BPoint start, end;
    switch ( nDirection )
    {
        case wxWEST:
            start.Set(r.right + 1, r.top);
            end.Set(r.left, r.top);
            break;

        case wxNORTH:
            start.Set(r.left, r.bottom + 1);
            end.Set(r.left, r.top);
            break;

        case wxSOUTH:
            start.Set(r.left, r.top);
            end.Set(r.left, r.bottom + 1);
            break;

        default: // wxEAST
            start.Set(r.left, r.top);
            end.Set(r.right + 1, r.top);
    }

    BGradientLinear gradient(start, end);
    gradient.AddColor(wxHaikuColour(initialColour), 0);
    gradient.AddColor(wxHaikuColour(destColour), 255);

    const bool alpha = m_targetAlpha || initialColour.Alpha() != 255
                        || destColour.Alpha() != 255;
    m_view->SetDrawingMode(alpha ? B_OP_ALPHA : B_OP_COPY);
    if ( alpha )
    {
        m_view->SetBlendingMode(B_PIXEL_ALPHA,
                                m_targetAlpha ? B_ALPHA_COMPOSITE
                                              : B_ALPHA_OVERLAY);
    }

    m_view->FillRect(r, gradient);

    CalcBoundingBox(rect.x, rect.y);
    CalcBoundingBox(rect.x + rect.width, rect.y + rect.height);

    HaikuEnd();
}

void wxHaikuDCImpl::DoGradientFillConcentric(const wxRect& rect,
                                             const wxColour& initialColour,
                                             const wxColour& destColour,
                                             const wxPoint& circleCenter)
{
    const int vx = XV(rect.x), vy = YV(rect.y),
              vw = WV(rect.width), vh = HV(rect.height);
    if ( vw <= 0 || vh <= 0 || !m_view || m_logicalFunction == wxNO_OP )
        return;

    if ( !HaikuBeginRawDraw() )
        return;

    const BRect r = wxHaikuRect(vx, vy, vw, vh);

    // from the initial colour in the centre to the destination colour on the
    // circle inscribed in the rectangle and everywhere outside of it
    const float radius = wxMin(vw, vh) / 2.0f;
    BGradientRadial gradient(BPoint(vx + WV(circleCenter.x),
                                    vy + HV(circleCenter.y)), radius);
    gradient.AddColor(wxHaikuColour(initialColour), 0);
    gradient.AddColor(wxHaikuColour(destColour), 255);

    const bool alpha = m_targetAlpha || initialColour.Alpha() != 255
                        || destColour.Alpha() != 255;
    m_view->SetDrawingMode(alpha ? B_OP_ALPHA : B_OP_COPY);
    if ( alpha )
    {
        m_view->SetBlendingMode(B_PIXEL_ALPHA,
                                m_targetAlpha ? B_ALPHA_COMPOSITE
                                              : B_ALPHA_OVERLAY);
    }

    m_view->FillRect(r, gradient);

    CalcBoundingBox(rect.x, rect.y);
    CalcBoundingBox(rect.x + rect.width, rect.y + rect.height);

    HaikuEnd();
}

// ----------------------------------------------------------------------------
// text
// ----------------------------------------------------------------------------

void wxHaikuDCImpl::DoDrawText(const wxString& text, wxCoord x, wxCoord y)
{
    if ( text.find(wxS('\n')) == wxString::npos )
    {
        DoDrawRotatedText(text, x, y, 0);
        return;
    }

    // one line below the other, as the ports whose text engine handles the
    // line breaks do it
    const int lineHeight = GetCharHeight();
    size_t start = 0;
    for ( ;; )
    {
        const size_t end = text.find(wxS('\n'), start);
        const wxString line = text.substr(start, end == wxString::npos
                                                    ? wxString::npos
                                                    : end - start);
        if ( !line.empty() )
            DoDrawRotatedText(line, x, y, 0);

        if ( end == wxString::npos )
            break;

        start = end + 1;
        y += lineHeight;
    }
}

void wxHaikuDCImpl::DoDrawRotatedText(const wxString& text,
                                      wxCoord x, wxCoord y, double angle)
{
    if ( text.empty() || !m_font.IsOk() )
        return;

    int ascent, descent;
    m_font.HaikuGetMetrics(&ascent, &descent, NULL);
    const int width = m_font.HaikuGetTextWidth(text);
    const int height = ascent + descent;

    const int vx = XV(x), vy = YV(y);

    const double rad = angle * M_PI / 180;
    const double cosA = cos(rad), sinA = sin(rad);

    // maps a point of the unrotated text box, relative to its top left
    // corner, to the view (the text is rotated counter-clockwise around it)
    #define TEXT_POINT(px, py) \
        BPoint((float)(vx + ((px) * cosA + (py) * sinA) * m_scaleX), \
               (float)(vy + (-(px) * sinA + (py) * cosA) * m_scaleY))

    // the background of the text if it is not transparent
    if ( m_backgroundMode == wxBRUSHSTYLE_SOLID
            && m_textBackgroundColour.IsOk()
            && m_textBackgroundColour.Alpha() != 0
            && HaikuBeginRawDraw() )
    {
        const bool alpha = m_targetAlpha
                            || m_textBackgroundColour.Alpha() != 255;
        m_view->SetDrawingMode(alpha ? B_OP_ALPHA : B_OP_OVER);
        if ( alpha )
        {
            m_view->SetBlendingMode(B_PIXEL_ALPHA,
                                    m_targetAlpha ? B_ALPHA_COMPOSITE
                                                  : B_ALPHA_OVERLAY);
        }
        m_view->SetHighColor(wxHaikuColour(m_textBackgroundColour));

        if ( angle == 0 )
        {
            m_view->FillRect(wxHaikuRect(vx, vy,
                                         wxRound(width * m_scaleX),
                                         wxRound(height * m_scaleY)));
        }
        else
        {
            const BPoint corners[4] =
            {
                TEXT_POINT(0, 0), TEXT_POINT(width, 0),
                TEXT_POINT(width, height), TEXT_POINT(0, height)
            };
            m_view->FillPolygon(corners, 4);
        }

        HaikuEnd();
    }

    if ( !HaikuBegin(Draw_Text) )
        return;

    BFont font(*m_font.GetHaikuFont());
    if ( m_scaleY != 1.0 )
        font.SetSize((float)(font.Size() * m_scaleY));
    if ( angle != 0 )
        font.SetRotation((float)angle);
    m_view->SetFont(&font);

    const wxScopedCharBuffer utf8 = text.utf8_str();
    m_view->DrawString(utf8, (int32)utf8.length(), TEXT_POINT(0, ascent));

    if ( m_font.GetUnderlined() || m_font.GetStrikethrough() )
    {
        const float thickness = wxMax(1.0f, (float)(height * m_scaleY / 14));
        m_view->SetPenSize(thickness);
        m_view->SetLineMode(B_BUTT_CAP, B_MITER_JOIN);

        if ( m_font.GetUnderlined() )
        {
            const double py = ascent + wxMax(1.0, descent / 3.0);
            m_view->StrokeLine(TEXT_POINT(0, py), TEXT_POINT(width, py));
        }

        if ( m_font.GetStrikethrough() )
        {
            const double py = ascent * 0.68;
            m_view->StrokeLine(TEXT_POINT(0, py), TEXT_POINT(width, py));
        }
    }

    #undef TEXT_POINT

    if ( angle == 0 )
    {
        CalcBoundingBox(x, y);
        CalcBoundingBox(x + width, y + height);
    }
    else
    {
        // the bounding box of the rotated rectangle
        const double xs[4] = { 0, width * cosA, width * cosA + height * sinA, height * sinA };
        const double ys[4] = { 0, -width * sinA, -width * sinA + height * cosA, height * cosA };
        for ( int i = 0; i < 4; i++ )
            CalcBoundingBox(x + (wxCoord)xs[i], y + (wxCoord)ys[i]);
    }

    HaikuEnd();
}

// ----------------------------------------------------------------------------
// bitmaps
// ----------------------------------------------------------------------------

void wxHaikuDCImpl::HaikuDrawBitmapPart(const wxBitmap& bmp, bool useMask,
                                        const wxRect& src, const wxRect& dst)
{
    BBitmap * const bitmap = bmp.GetHaikuDrawBitmap(useMask);
    const bool blend = bmp.HaikuNeedsAlphaBlend(useMask);

    if ( !bitmap || src.IsEmpty() || dst.IsEmpty() )
        return;

    if ( !HaikuBegin(Draw_Bitmap) )
        return;

    if ( m_logicalFunction == wxCOPY )
    {
        if ( blend )
        {
            m_view->SetDrawingMode(B_OP_ALPHA);
            m_view->SetBlendingMode(B_PIXEL_ALPHA,
                                    m_targetAlpha ? B_ALPHA_COMPOSITE
                                                  : B_ALPHA_OVERLAY);
        }
        else
        {
            // opaque pixels (their alpha bytes are 0xff)
            m_view->SetDrawingMode(B_OP_COPY);
        }
    }

    const BRect s = wxHaikuRect(src), d = wxHaikuRect(dst);
    if ( src.GetSize() != dst.GetSize() )
        m_view->DrawBitmapAsync(bitmap, s, d, B_FILTER_BITMAP_BILINEAR);
    else
        m_view->DrawBitmapAsync(bitmap, s, d);

    // the native bitmap must not be deleted before this was executed
    wxHaikuBitmapDrawn(m_view, bmp);

    HaikuEnd();
}

void wxHaikuDCImpl::DoDrawIcon(const wxIcon& icon, wxCoord x, wxCoord y)
{
    DoDrawBitmap(icon, x, y, true);
}

void wxHaikuDCImpl::DoDrawBitmap(const wxBitmap& bmp, wxCoord x, wxCoord y,
                                 bool useMask)
{
    wxCHECK_RET( bmp.IsOk(), wxS("invalid bitmap") );

    const int w = bmp.GetWidth(), h = bmp.GetHeight();

    wxBitmap toDraw(bmp);
    if ( bmp.GetDepth() == 1 )
    {
        // monochrome bitmaps are drawn in the text colours
        toDraw = HaikuColouriseMono(bmp);
    }

    HaikuDrawBitmapPart(toDraw, useMask,
                        wxRect(0, 0, w, h),
                        wxRect(XV(x), YV(y), WV(w), HV(h)));

    CalcBoundingBox(x, y);
    CalcBoundingBox(x + w, y + h);
}

wxBitmap wxHaikuDCImpl::HaikuColouriseMono(const wxBitmap& bmp) const
{
    const int w = bmp.GetWidth(), h = bmp.GetHeight();

    wxBitmap result(w, h, 24);
    if ( !result.IsOk() )
        return bmp;

    const wxColour fg = m_textForegroundColour.IsOk() ? m_textForegroundColour
                                                      : *wxBLACK,
                   bg = m_textBackgroundColour.IsOk() ? m_textBackgroundColour
                                                      : *wxWHITE;

    const unsigned char *srcBits = bmp.HaikuGetBits();
    unsigned char *dstBits = result.HaikuGetBits();
    const int srcRow = bmp.HaikuGetRowBytes(),
              dstRow = result.HaikuGetRowBytes();

    for ( int y = 0; y < h; y++ )
    {
        const unsigned char *s = srcBits + y * srcRow;
        unsigned char *d = dstBits + y * dstRow;
        for ( int x = 0; x < w; x++, s += 4, d += 4 )
        {
            // black pixels are the foreground
            const wxColour& c = (s[0] & 0x80) ? bg : fg;
            d[0] = c.Blue();
            d[1] = c.Green();
            d[2] = c.Red();
            d[3] = 0xff;
        }
    }

    if ( bmp.GetMask() )
        result.SetMask(new wxMask(*bmp.GetMask()));

    return result;
}

const wxBitmap *wxHaikuDCImpl::HaikuGetSourceBitmap(wxPoint *WXUNUSED(origin)) const
{
    return NULL;
}

bool wxHaikuDCImpl::DoBlit(wxCoord xdest, wxCoord ydest,
                           wxCoord width, wxCoord height,
                           wxDC *source, wxCoord xsrc, wxCoord ysrc,
                           wxRasterOperationMode rop, bool useMask,
                           wxCoord xsrcMask, wxCoord ysrcMask)
{
    return DoStretchBlit(xdest, ydest, width, height,
                         source, xsrc, ysrc, width, height,
                         rop, useMask, xsrcMask, ysrcMask);
}

bool wxHaikuDCImpl::DoStretchBlit(wxCoord xdest, wxCoord ydest,
                                  wxCoord dstWidth, wxCoord dstHeight,
                                  wxDC *source,
                                  wxCoord xsrc, wxCoord ysrc,
                                  wxCoord srcWidth, wxCoord srcHeight,
                                  wxRasterOperationMode rop,
                                  bool useMask,
                                  wxCoord WXUNUSED(xsrcMask),
                                  wxCoord WXUNUSED(ysrcMask))
{
    wxCHECK_MSG( source, false, wxS("invalid source dc") );

    wxHaikuDCImpl * const
        srcImpl = wxDynamicCast(source->GetImpl(), wxHaikuDCImpl);
    if ( !srcImpl )
        return false;

    // only the DCs with a bitmap behind them can be read: what is on a
    // window is not kept by anybody
    wxPoint srcOrigin;
    const wxBitmap * const bitmap = srcImpl->HaikuGetSourceBitmap(&srcOrigin);
    if ( !bitmap || !bitmap->IsOk() )
        return false;

    // the source rectangle in the pixels of the bitmap
    wxRect src(source->LogicalToDeviceX(xsrc) + srcOrigin.x,
               source->LogicalToDeviceY(ysrc) + srcOrigin.y,
               source->LogicalToDeviceXRel(srcWidth),
               source->LogicalToDeviceYRel(srcHeight));
    wxRect dst(XV(xdest), YV(ydest), WV(dstWidth), HV(dstHeight));

    if ( src.width <= 0 || src.height <= 0 || dst.width <= 0 || dst.height <= 0 )
        return true;

    // clip the source to the bitmap and the destination accordingly
    const wxRect bounds(0, 0, bitmap->GetWidth(), bitmap->GetHeight());
    const wxRect clipped = src.Intersect(bounds);
    if ( clipped.IsEmpty() )
        return true;

    if ( clipped != src )
    {
        const double fx = (double)dst.width / src.width,
                     fy = (double)dst.height / src.height;
        dst = wxRect(dst.x + wxRound((clipped.x - src.x) * fx),
                     dst.y + wxRound((clipped.y - src.y) * fy),
                     wxRound(clipped.width * fx),
                     wxRound(clipped.height * fy));
        src = clipped;
    }

    const wxRasterOperationMode oldRop = m_logicalFunction;
    m_logicalFunction = rop;

    HaikuDrawBitmapPart(*bitmap, useMask, src, dst);

    m_logicalFunction = oldRop;

    CalcBoundingBox(xdest, ydest);
    CalcBoundingBox(xdest + dstWidth, ydest + dstHeight);

    return true;
}

bool wxHaikuDCImpl::DoGetPixel(wxCoord x, wxCoord y, wxColour *col) const
{
    wxPoint origin;
    const wxBitmap * const bitmap = HaikuGetSourceBitmap(&origin);
    if ( !bitmap || !bitmap->IsOk() )
        return false;

    const int px = LogicalToDeviceX(x) + origin.x,
              py = LogicalToDeviceY(y) + origin.y;
    if ( px < 0 || py < 0 || px >= bitmap->GetWidth() || py >= bitmap->GetHeight() )
        return false;

    const unsigned char * const
        p = bitmap->HaikuGetBits() + py * bitmap->HaikuGetRowBytes() + px * 4;
    if ( col )
        col->Set(p[2], p[1], p[0], bitmap->HasAlpha() ? p[3] : 255);

    return true;
}

bool wxHaikuDCImpl::DoFloodFill(wxCoord x, wxCoord y, const wxColour& col,
                                wxFloodFillStyle style)
{
#if wxUSE_IMAGE
    // the generic implementation works on the pixels of the bitmap behind
    // the DC, if there is one
    if ( !HaikuGetSourceBitmap(NULL) )
        return false;

    return wxDoFloodFill(GetOwner(), x, y, col, style);
#else
    wxUnusedVar(x);
    wxUnusedVar(y);
    wxUnusedVar(col);
    wxUnusedVar(style);
    return false;
#endif
}
