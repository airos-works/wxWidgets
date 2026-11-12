/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dc.h
// Purpose:     wxHaikuDCImpl: wxDC drawing through a BView
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DC_H_
#define _WX_HAIKU_DC_H_

#include "wx/dc.h"
#include "wx/region.h"

class BView;
class BBitmap;
class BRegion;
struct pattern;

// The DC draws on a BView: the view of a window, of the off-screen buffer of
// the paint in progress or of a bitmap. Every operation locks the view's
// looper, makes sure that the state of the view (clipping, colours, pen,
// font) is the one of this DC -- several DCs can share a view -- and draws.
// Nothing is synchronous: drawing commands are batched by the Interface Kit
// and flushed when the DC is destroyed (window targets) or when the pixels
// are needed (bitmap targets).
class WXDLLIMPEXP_CORE wxHaikuDCImpl : public wxDCImpl
{
public:
    wxHaikuDCImpl(wxDC *owner);
    virtual ~wxHaikuDCImpl();

    virtual bool IsOk() const wxOVERRIDE { return m_view != NULL; }

    // implement base class pure virtuals
    // ----------------------------------

    virtual void Clear() wxOVERRIDE;

    virtual void SetFont(const wxFont& font) wxOVERRIDE;
    virtual void SetPen(const wxPen& pen) wxOVERRIDE;
    virtual void SetBrush(const wxBrush& brush) wxOVERRIDE;
    virtual void SetBackground(const wxBrush& brush) wxOVERRIDE;
    virtual void SetBackgroundMode(int mode) wxOVERRIDE;
#if wxUSE_PALETTE
    virtual void SetPalette(const wxPalette& palette) wxOVERRIDE;
#endif

    virtual void SetLogicalFunction(wxRasterOperationMode function) wxOVERRIDE;

    virtual void DestroyClippingRegion() wxOVERRIDE;

    virtual wxCoord GetCharHeight() const wxOVERRIDE;
    virtual wxCoord GetCharWidth() const wxOVERRIDE;
    virtual void DoGetTextExtent(const wxString& string,
                                 wxCoord *x, wxCoord *y,
                                 wxCoord *descent = NULL,
                                 wxCoord *externalLeading = NULL,
                                 const wxFont *theFont = NULL) const wxOVERRIDE;
    virtual void DoGetFontMetrics(int *height,
                                  int *ascent,
                                  int *descent,
                                  int *internalLeading,
                                  int *externalLeading,
                                  int *averageWidth) const wxOVERRIDE;
    virtual bool DoGetPartialTextExtents(const wxString& text,
                                         wxArrayInt& widths) const wxOVERRIDE;

    virtual bool CanDrawBitmap() const wxOVERRIDE { return true; }
    virtual bool CanGetTextExtent() const wxOVERRIDE { return true; }
    virtual int GetDepth() const wxOVERRIDE;
    virtual wxSize GetPPI() const wxOVERRIDE;

    virtual void Flush() wxOVERRIDE;

    virtual void* GetHandle() const wxOVERRIDE { return m_view; }

    // implementation from now on
    // --------------------------

    // the view this DC draws on (NULL if the DC is invalid)
    BView *GetHaikuView() const { return m_view; }

    // offset of the device origin (0, 0) of this DC in the view
    wxPoint HaikuGetViewOffset() const { return m_offset; }

    // true if the target has an alpha channel to maintain (bitmap of depth 32)
    bool HaikuTargetHasAlpha() const { return m_targetAlpha; }

    // device clipping region in view coordinates: the fixed clipping of the
    // DC (update region, client area) combined with the one set by the user;
    // returns false if drawing is not clipped at all
    bool HaikuGetClip(wxRegion& region) const;

    // Lock the looper of the view and take over its state for raw drawing by
    // code other than this class (wxGraphicsContext, the wxUniv renderer):
    // returns false if the view cannot be drawn on. With "apply" the DC's
    // own clipping is set on the view. HaikuEndRawDraw() unlocks; this DC
    // then sets all of its state again before it draws next.
    bool HaikuBeginRawDraw(bool applyClip = true);
    void HaikuEndRawDraw();

    // coordinate conversion helpers: logical to view coordinates
    wxCoord XV(wxCoord x) const { return LogicalToDeviceX(x) + m_offset.x; }
    wxCoord YV(wxCoord y) const { return LogicalToDeviceY(y) + m_offset.y; }
    wxCoord WV(wxCoord w) const { return LogicalToDeviceXRel(w); }
    wxCoord HV(wxCoord h) const { return LogicalToDeviceYRel(h); }

    // logical coordinates of this DC to coordinates of the view, for code
    // drawing on the view directly between HaikuBeginRawDraw() and
    // HaikuEndRawDraw(). Such code must leave the state of the view as it
    // found it (PushState()/PopState()) except for the colours, drawing
    // mode, pen size, line mode and font, which the DC sets before it uses
    // them.
    wxPoint HaikuToView(const wxPoint& pt) const
        { return wxPoint(XV(pt.x), YV(pt.y)); }
    wxRect HaikuToView(const wxRect& r) const
        { return wxRect(XV(r.x), YV(r.y), WV(r.width), HV(r.height)); }

    // the bitmap to read the pixels of this DC from (memory and screen DCs)
    // and the position of the device origin in it; NULL if not readable
    virtual const wxBitmap *HaikuGetSourceBitmap(wxPoint *origin) const;

    // the pixels of the target were modified
    virtual void HaikuTargetChanged() { }

protected:
    // set the target of the DC: must be called before any drawing
    void HaikuInit(BView *view, const wxPoint& offset, const wxSize& size,
                   bool targetAlpha);
    // fixed clipping (view coordinates) applied on top of the user's
    void HaikuSetBaseClip(const wxRegion& region);
    void HaikuResetBaseClip();

    virtual bool DoFloodFill(wxCoord x, wxCoord y, const wxColour& col,
                             wxFloodFillStyle style = wxFLOOD_SURFACE) wxOVERRIDE;
    virtual bool DoGetPixel(wxCoord x, wxCoord y, wxColour *col) const wxOVERRIDE;

    virtual void DoDrawPoint(wxCoord x, wxCoord y) wxOVERRIDE;
    virtual void DoDrawLine(wxCoord x1, wxCoord y1, wxCoord x2, wxCoord y2) wxOVERRIDE;

    virtual void DoDrawArc(wxCoord x1, wxCoord y1,
                           wxCoord x2, wxCoord y2,
                           wxCoord xc, wxCoord yc) wxOVERRIDE;
    virtual void DoDrawEllipticArc(wxCoord x, wxCoord y, wxCoord w, wxCoord h,
                                   double sa, double ea) wxOVERRIDE;

    virtual void DoDrawRectangle(wxCoord x, wxCoord y, wxCoord width, wxCoord height) wxOVERRIDE;
    virtual void DoDrawRoundedRectangle(wxCoord x, wxCoord y,
                                        wxCoord width, wxCoord height,
                                        double radius) wxOVERRIDE;
    virtual void DoDrawEllipse(wxCoord x, wxCoord y, wxCoord width, wxCoord height) wxOVERRIDE;

    virtual void DoCrossHair(wxCoord x, wxCoord y) wxOVERRIDE;

    virtual void DoDrawIcon(const wxIcon& icon, wxCoord x, wxCoord y) wxOVERRIDE;
    virtual void DoDrawBitmap(const wxBitmap& bmp, wxCoord x, wxCoord y,
                              bool useMask = false) wxOVERRIDE;

    virtual void DoDrawText(const wxString& text, wxCoord x, wxCoord y) wxOVERRIDE;
    virtual void DoDrawRotatedText(const wxString& text, wxCoord x, wxCoord y,
                                   double angle) wxOVERRIDE;

    virtual bool DoBlit(wxCoord xdest, wxCoord ydest, wxCoord width, wxCoord height,
                        wxDC *source, wxCoord xsrc, wxCoord ysrc,
                        wxRasterOperationMode rop = wxCOPY, bool useMask = false,
                        wxCoord xsrcMask = -1, wxCoord ysrcMask = -1) wxOVERRIDE;
    virtual bool DoStretchBlit(wxCoord xdest, wxCoord ydest,
                               wxCoord dstWidth, wxCoord dstHeight,
                               wxDC *source,
                               wxCoord xsrc, wxCoord ysrc,
                               wxCoord srcWidth, wxCoord srcHeight,
                               wxRasterOperationMode rop = wxCOPY,
                               bool useMask = false,
                               wxCoord xsrcMask = wxDefaultCoord,
                               wxCoord ysrcMask = wxDefaultCoord) wxOVERRIDE;

    virtual void DoSetClippingRegion(wxCoord x, wxCoord y,
                                     wxCoord width, wxCoord height) wxOVERRIDE;
    virtual void DoSetDeviceClippingRegion(const wxRegion& region) wxOVERRIDE;

    virtual void DoGetSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoGetSizeMM(int* width, int* height) const wxOVERRIDE;

    virtual void DoDrawLines(int n, const wxPoint points[],
                             wxCoord xoffset, wxCoord yoffset) wxOVERRIDE;
    virtual void DoDrawPolygon(int n, const wxPoint points[],
                               wxCoord xoffset, wxCoord yoffset,
                               wxPolygonFillMode fillStyle = wxODDEVEN_RULE) wxOVERRIDE;

    virtual void DoGradientFillLinear(const wxRect& rect,
                                      const wxColour& initialColour,
                                      const wxColour& destColour,
                                      wxDirection nDirection = wxEAST) wxOVERRIDE;
    virtual void DoGradientFillConcentric(const wxRect& rect,
                                          const wxColour& initialColour,
                                          const wxColour& destColour,
                                          const wxPoint& circleCenter) wxOVERRIDE;

    // what an operation is about to draw with
    enum DrawWith
    {
        Draw_Pen,
        Draw_Brush,
        Draw_Text,
        Draw_Bitmap
    };

    // Locks the view and prepares its state for drawing with the pen, brush
    // or text colour; returns false if nothing should be drawn (invalid or
    // unlockable target, transparent pen or brush, wxNO_OP...). Must be
    // matched by HaikuEnd() if it returned true.
    bool HaikuBegin(DrawWith what);
    void HaikuEnd();

    // draws a part of a native bitmap
    void HaikuDrawBitmapPart(BBitmap *bitmap, bool blend,
                             const wxRect& src, const wxRect& dst);

    // a colour copy of a monochrome bitmap in the text colours
    wxBitmap HaikuColouriseMono(const wxBitmap& bmp) const;

    // fills the whole target with a colour which is not opaque, replacing
    // (not blending with) what is there; returns false if not possible
    virtual bool HaikuClearTranslucent(const wxColour& colour);

    BView *m_view;
    wxPoint m_offset;
    wxSize m_size;
    bool m_targetAlpha;

    // user clipping region in device coordinates, valid if m_clipping
    wxRegion m_userClip;
    // fixed clipping in view coordinates
    wxRegion m_baseClip;
    bool m_hasBaseClip;
    bool m_clipDirty;

    // lock nesting of HaikuBegin()/HaikuBeginRawDraw()
    int m_lockCount;

    // the pattern of the pen HaikuBegin() selected
    pattern *m_pattern;

    wxDECLARE_ABSTRACT_CLASS(wxHaikuDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxHaikuDCImpl);
};

#endif // _WX_HAIKU_DC_H_
