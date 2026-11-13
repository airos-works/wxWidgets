/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/renderer.cpp
// Purpose:     wxHaikuLook (BControlLook drawing on a wxDC) and the native
//              wxRendererNative of the port, used by the generic controls
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/renderer.h"

#ifndef WX_PRECOMP
    #include "wx/dc.h"
    #include "wx/window.h"
    #include "wx/settings.h"
    #include "wx/control.h"
    #include "wx/pen.h"
    #include "wx/brush.h"
#endif

#include "wx/haiku/dc.h"
#include "wx/haiku/look.h"

#include <Font.h>

#include <math.h>

// ============================================================================
// wxHaikuLookPainter
// ============================================================================

wxHaikuLookPainter::wxHaikuLookPainter(wxDC& dc)
    : m_impl(NULL),
      m_view(NULL)
{
    if ( !be_control_look || !dc.IsOk() )
        return;

    wxHaikuDCImpl * const impl = wxDynamicCast(dc.GetImpl(), wxHaikuDCImpl);
    if ( !impl || !impl->HaikuBeginRawDraw() )
        return;

    m_impl = impl;
    m_view = impl->GetHaikuView();

    m_view->SetDrawingMode(B_OP_OVER);
    m_view->SetPenSize(1.0f);
    m_view->SetLineMode(B_BUTT_CAP, B_MITER_JOIN);
}

wxHaikuLookPainter::~wxHaikuLookPainter()
{
    if ( m_impl )
        m_impl->HaikuEndRawDraw();
}

BRect wxHaikuLookPainter::Rect(const wxRect& r) const
{
    return wxHaikuRect(m_impl->HaikuToView(r));
}

void wxHaikuLookPainter::SetBackground(rgb_color colour)
{
    m_view->SetLowColor(colour);
}

void wxHaikuLookPainter::SetPlainFont()
{
    m_view->SetFont(be_plain_font);
}

// ============================================================================
// wxHaikuLook
// ============================================================================

namespace wxHaikuLook
{

// ----------------------------------------------------------------------------
// colours
// ----------------------------------------------------------------------------

wxColour DisabledText(const wxColour& text, const wxColour& background)
{
    return wxFromHaikuColour(disable_color(wxHaikuColour(text),
                                           wxHaikuColour(background)));
}

wxColour FrameColour(const wxColour& base)
{
    // as BControlLook::DrawBorder()
    const rgb_color c = wxHaikuColour(base);
    return wxFromHaikuColour(tint_color(c, IsDark(c) ? B_LIGHTEN_1_TINT
                                                     : B_DARKEN_2_TINT));
}

wxColour SelectionColour(bool focused)
{
    if ( focused )
        return Colour(B_LIST_SELECTED_BACKGROUND_COLOR);

    // a list which is not the focus view shows its selection in grey
    const rgb_color bg = ui_color(B_LIST_BACKGROUND_COLOR);
    return wxFromHaikuColour(tint_color(bg, IsDark(bg) ? B_LIGHTEN_2_TINT
                                                       : B_DARKEN_1_TINT));
}

wxColour SelectionTextColour(bool focused)
{
    return Colour(focused ? B_LIST_SELECTED_ITEM_TEXT_COLOR
                          : B_LIST_ITEM_TEXT_COLOR);
}

// ----------------------------------------------------------------------------
// metrics
// ----------------------------------------------------------------------------

int Scaled(double n)
{
    const int scaled = (int)floor(n * wxHaikuScaleFactor() + 0.5);
    return n > 0 && scaled < 1 ? 1 : scaled;
}

int LabelSpacing()
{
    return be_control_look ? (int)be_control_look->DefaultLabelSpacing()
                           : Scaled(6);
}

int ScrollBarWidth()
{
    // B_V_SCROLL_BAR_WIDTH is the width of the frame rectangle of the view,
    // one less than its pixels
    return be_control_look ? (int)be_control_look->GetScrollBarWidth() + 1
                           : Scaled(15);
}

int CheckMarkSize()
{
    // BCheckBox::_CheckBoxFrame(): BRect(0, 2, ceilf(3 + ascent), ...). The
    // height of be_plain_font is cached by the font object.
    font_height fh;
    be_plain_font->GetHeight(&fh);
    return (int)ceilf(3.0f + fh.ascent) + 1;
}

int TextFrameWidth()
{
    // the recessed edge and the dark frame: one pixel each at any font size
    return 2;
}

int MenuFieldMarkerWidth()
{
    // kButtonPopUpIndicatorWidth of the control look
    return (int)BControlLook::ComposeSpacing(B_USE_ITEM_SPACING);
}

uint32 Flags(int flags)
{
    uint32 f = 0;
    if ( flags & wxCONTROL_DISABLED )
        f |= BControlLook::B_DISABLED;
    if ( flags & wxCONTROL_FOCUSED )
        f |= BControlLook::B_FOCUSED;
    if ( flags & wxCONTROL_PRESSED )
        f |= BControlLook::B_ACTIVATED;
    if ( flags & wxCONTROL_CURRENT )
        f |= BControlLook::B_HOVER;
    return f;
}

static inline wxRect Inset(const wxRect& rect,
                           float left, float top, float right, float bottom)
{
    wxRect r(rect.x + (int)left, rect.y + (int)top,
             rect.width - (int)left - (int)right,
             rect.height - (int)top - (int)bottom);
    if ( r.width < 0 )
        r.width = 0;
    if ( r.height < 0 )
        r.height = 0;
    return r;
}

static inline orientation HaikuOrientation(wxOrientation orient)
{
    return orient == wxVERTICAL ? B_VERTICAL : B_HORIZONTAL;
}

// ----------------------------------------------------------------------------
// parts
// ----------------------------------------------------------------------------

void Fill(wxDC& dc, const wxRect& rect, const wxColour& colour)
{
    if ( rect.width <= 0 || rect.height <= 0 || !colour.IsOk() )
        return;

    dc.SetBrush(colour);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(rect);
}

void DrawFocusFrame(wxDC& dc, const wxRect& rect)
{
    if ( rect.width <= 0 || rect.height <= 0 )
        return;

    dc.SetPen(Colour(B_KEYBOARD_NAVIGATION_COLOR));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(rect);
}

bool DrawButton(wxDC& dc, const wxRect& rect, int flags,
                const wxColour& base, const wxColour& outside,
                wxRect *inside, uint32 extraFlags)
{
    uint32 f = Flags(flags) | extraFlags;
    if ( flags & wxCONTROL_ISDEFAULT )
        f |= BControlLook::B_DEFAULT_BUTTON;

    if ( inside )
    {
        *inside = rect;
        if ( be_control_look )
        {
            float l, t, r, b;
            be_control_look->GetInsets(BControlLook::B_BUTTON_FRAME,
                                       BControlLook::B_BUTTON_BACKGROUND,
                                       f, l, t, r, b);
            *inside = Inset(rect, l, t, r, b);
        }
    }

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base),
                    colOutside = wxHaikuColour(outside);
    p.SetBackground(colOutside);

    BRect r = p.Rect(rect);
    be_control_look->DrawButtonFrame(p.View(), r, r, colBase, colOutside, f);
    be_control_look->DrawButtonBackground(p.View(), r, r, colBase, f);

    return true;
}

bool DrawButtonBackground(wxDC& dc, const wxRect& rect, int flags,
                          const wxColour& base, uint32 borders,
                          wxOrientation orient)
{
    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);

    BRect r = p.Rect(rect);
    be_control_look->DrawButtonBackground(p.View(), r, r, colBase,
                                          Flags(flags), borders,
                                          HaikuOrientation(orient));
    return true;
}

static uint32 MarkFlags(int flags)
{
    uint32 f = 0;
    if ( flags & wxCONTROL_DISABLED )
        f |= BControlLook::B_DISABLED;
    if ( flags & wxCONTROL_FOCUSED )
        f |= BControlLook::B_FOCUSED;
    if ( flags & wxCONTROL_PRESSED )
        f |= BControlLook::B_CLICKED;
    if ( flags & wxCONTROL_CHECKED )
        f |= BControlLook::B_ACTIVATED;
    else if ( flags & wxCONTROL_UNDETERMINED )
        f |= BControlLook::B_PARTIALLY_ACTIVATED;
    return f;
}

bool DrawCheckBox(wxDC& dc, const wxRect& rect, int flags,
                  const wxColour& base)
{
    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);
    p.SetPlainFont();

    BRect r = p.Rect(rect);
    be_control_look->DrawCheckBox(p.View(), r, r, colBase, MarkFlags(flags));
    return true;
}

bool DrawRadioButton(wxDC& dc, const wxRect& rect, int flags,
                     const wxColour& base)
{
    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);
    p.SetPlainFont();

    // a radio button has no partially activated state
    BRect r = p.Rect(rect);
    be_control_look->DrawRadioButton(p.View(), r, r, colBase,
        MarkFlags(flags & ~wxCONTROL_UNDETERMINED));
    return true;
}

bool DrawTextFrame(wxDC& dc, const wxRect& rect, int flags,
                   const wxColour& base, wxRect *inside)
{
    if ( inside )
    {
        const int w = TextFrameWidth();
        *inside = Inset(rect, w, w, w, w);
    }

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);

    uint32 f = 0;
    if ( flags & wxCONTROL_DISABLED )
        f |= BControlLook::B_DISABLED;
    if ( flags & wxCONTROL_FOCUSED )
        f |= BControlLook::B_FOCUSED;

    BRect r = p.Rect(rect);
    be_control_look->DrawTextControlBorder(p.View(), r, r, colBase, f);
    return true;
}

bool DrawMenuField(wxDC& dc, const wxRect& rect, int flags,
                   const wxColour& base, const wxColour& outside,
                   bool marker, wxRect *inside)
{
    // a menu field has no pressed look: its menu is open then
    const uint32 f = Flags(flags & ~wxCONTROL_PRESSED);

    if ( inside )
    {
        *inside = rect;
        if ( be_control_look )
        {
            float l, t, r, b;
            be_control_look->GetInsets(BControlLook::B_MENU_FIELD_FRAME,
                                       BControlLook::B_MENU_FIELD_BACKGROUND,
                                       f, l, t, r, b);
            *inside = Inset(rect, l, t,
                            r + (marker ? MenuFieldMarkerWidth() : 0), b);
        }
    }

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base),
                    colOutside = wxHaikuColour(outside);
    p.SetBackground(colOutside);

    BRect r = p.Rect(rect);
    be_control_look->DrawMenuFieldFrame(p.View(), r, r, colBase, colOutside,
                                        f);
    be_control_look->DrawMenuFieldBackground(p.View(), r, r, colBase, marker,
                                             f);
    return true;
}

bool DrawMenuFieldEnd(wxDC& dc, const wxRect& rect, int flags,
                      const wxColour& base)
{
    // the surface of a menu field which starts further left, clipped to
    // the rectangle
    wxDCClipper clip(dc, rect);

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);

    wxRect rectField = rect;
    const int extra = 2*MenuFieldMarkerWidth();
    rectField.x -= extra;
    rectField.width += extra;

    BRect r = p.Rect(rectField);
    be_control_look->DrawMenuFieldBackground(p.View(), r, r, colBase, true,
        Flags(flags & ~wxCONTROL_PRESSED));
    return true;
}

bool DrawArrow(wxDC& dc, const wxRect& rect, wxDirection dir,
               const wxColour& colour, int flags)
{
    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    uint32 direction;
    switch ( dir )
    {
        case wxLEFT:    direction = BControlLook::B_LEFT_ARROW; break;
        case wxRIGHT:   direction = BControlLook::B_RIGHT_ARROW; break;
        case wxUP:      direction = BControlLook::B_UP_ARROW; break;
        default:        direction = BControlLook::B_DOWN_ARROW; break;
    }

    // the colour as it is: the caller has already faded it if disabled
    wxUnusedVar(flags);
    BRect r = p.Rect(rect);
    be_control_look->DrawArrowShape(p.View(), r, r, wxHaikuColour(colour),
                                    direction, 0, B_NO_TINT);
    return true;
}

bool DrawBorder(wxDC& dc, const wxRect& rect, border_style style, int flags,
                const wxColour& base, uint32 borders, wxRect *inside)
{
    if ( inside )
    {
        const int w = style == B_FANCY_BORDER ? 2
                        : style == B_PLAIN_BORDER ? 1 : 0;
        *inside = Inset(rect,
            borders & BControlLook::B_LEFT_BORDER ? w : 0,
            borders & BControlLook::B_TOP_BORDER ? w : 0,
            borders & BControlLook::B_RIGHT_BORDER ? w : 0,
            borders & BControlLook::B_BOTTOM_BORDER ? w : 0);
    }

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);

    BRect r = p.Rect(rect);
    be_control_look->DrawBorder(p.View(), r, r, colBase, style,
        flags & wxCONTROL_FOCUSED ? BControlLook::B_FOCUSED : 0, borders);
    return true;
}

bool DrawRaisedBorder(wxDC& dc, const wxRect& rect, int flags,
                      const wxColour& base, wxRect *inside)
{
    if ( inside )
        *inside = Inset(rect, 1, 1, 1, 1);

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);

    BRect r = p.Rect(rect);
    be_control_look->DrawRaisedBorder(p.View(), r, r, colBase,
        flags & wxCONTROL_DISABLED ? BControlLook::B_DISABLED : 0);
    return true;
}

bool DrawGroupFrame(wxDC& dc, const wxRect& rect, const wxColour& base,
                    uint32 borders)
{
    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);

    BRect r = p.Rect(rect);
    be_control_look->DrawGroupFrame(p.View(), r, r, colBase, borders);
    return true;
}

bool DrawGauge(wxDC& dc, const wxRect& rect, int pos, int range,
               bool vertical, const wxColour& base, const wxColour& bar)
{
    if ( rect.width <= 2 || rect.height <= 2 )
        return true;

    if ( pos < 0 )
        pos = 0;
    if ( range < 1 )
        range = 1;
    if ( pos > range )
        pos = range;

    if ( vertical )
    {
        // BControlLook only has the horizontal bar of BStatusBar: the same
        // recessed frame, the bar growing from the bottom
        wxRect inside;
        if ( !DrawTextFrame(dc, rect, wxCONTROL_DISABLED, base, &inside) )
            return false;

        Fill(dc, inside, Tint(base, B_DARKEN_1_TINT));

        const int filled = (int)((double)inside.height * pos / range + 0.5);
        if ( filled > 0 )
        {
            wxRect rectBar(inside.x, inside.GetBottom() - filled + 1,
                           inside.width, filled);
            dc.GradientFillLinear(rectBar, Tint(bar, B_LIGHTEN_1_TINT), bar,
                                  wxEAST);
        }

        return true;
    }

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);

    // the bar is drawn inside a recessed frame of one pixel: the position
    // is the first pixel after the filled part, in view coordinates
    BRect r = p.Rect(rect);
    const float inner = r.Width() - 1;      // pixels inside the frame
    const float where = r.left + 1 + floorf(inner * pos / range + 0.5f);
    be_control_look->DrawStatusBar(p.View(), r, r, colBase,
                                   wxHaikuColour(bar), where);
    return true;
}

bool DrawSplitter(wxDC& dc, const wxRect& rect, wxOrientation sash,
                  int flags, const wxColour& base)
{
    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);

    // BSplitView names the splitter after the direction of its layout: a
    // vertical sash divides a horizontal split view
    BRect r = p.Rect(rect);
    be_control_look->DrawSplitter(p.View(), r, r, colBase,
        sash == wxVERTICAL ? B_HORIZONTAL : B_VERTICAL,
        flags & wxCONTROL_PRESSED ? BControlLook::B_ACTIVATED : 0, 0);
    return true;
}

} // namespace wxHaikuLook

// ============================================================================
// wxRendererHaiku: what the generic controls (wxDataViewCtrl, wxListCtrl,
// wxTreeCtrl, wxGrid, wxSplitterWindow, wxComboCtrl...) draw themselves with
// ============================================================================

namespace
{

class wxRendererHaiku : public wxDelegateRendererNative
{
public:
    // what BControlLook has nothing for (the contents of header buttons,
    // title bar buttons...) is left to the generic renderer
    wxRendererHaiku()
        : wxDelegateRendererNative(wxRendererNative::GetGeneric())
    {
    }

    virtual int DrawHeaderButton(wxWindow *win,
                                 wxDC& dc,
                                 const wxRect& rect,
                                 int flags = 0,
                                 wxHeaderSortIconType sortArrow = wxHDR_SORT_ICON_NONE,
                                 wxHeaderButtonParams *params = NULL) wxOVERRIDE;

    virtual int GetHeaderButtonHeight(wxWindow *win) wxOVERRIDE;

    virtual int GetHeaderButtonMargin(wxWindow *win) wxOVERRIDE;

    virtual void DrawTreeItemButton(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags = 0) wxOVERRIDE;

    virtual void DrawSplitterBorder(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags = 0) wxOVERRIDE;

    virtual void DrawSplitterSash(wxWindow *win,
                                  wxDC& dc,
                                  const wxSize& size,
                                  wxCoord position,
                                  wxOrientation orient,
                                  int flags = 0) wxOVERRIDE;

    virtual void DrawComboBoxDropButton(wxWindow *win,
                                        wxDC& dc,
                                        const wxRect& rect,
                                        int flags = 0) wxOVERRIDE;

    virtual void DrawDropArrow(wxWindow *win,
                               wxDC& dc,
                               const wxRect& rect,
                               int flags = 0) wxOVERRIDE;

    virtual void DrawCheckBox(wxWindow *win,
                              wxDC& dc,
                              const wxRect& rect,
                              int flags = 0) wxOVERRIDE;

    virtual void DrawCheckMark(wxWindow *win,
                               wxDC& dc,
                               const wxRect& rect,
                               int flags = 0) wxOVERRIDE;

    virtual wxSize GetCheckBoxSize(wxWindow *win, int flags = 0) wxOVERRIDE;

    virtual wxSize GetCheckMarkSize(wxWindow *win) wxOVERRIDE;

    virtual wxSize GetExpanderSize(wxWindow *win) wxOVERRIDE;

    virtual void DrawPushButton(wxWindow *win,
                                wxDC& dc,
                                const wxRect& rect,
                                int flags = 0) wxOVERRIDE;

    virtual void DrawCollapseButton(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags = 0) wxOVERRIDE;

    virtual wxSize GetCollapseButtonSize(wxWindow *win, wxDC& dc) wxOVERRIDE;

    virtual void DrawItemSelectionRect(wxWindow *win,
                                       wxDC& dc,
                                       const wxRect& rect,
                                       int flags = 0) wxOVERRIDE;

    virtual void DrawFocusRect(wxWindow *win,
                               wxDC& dc,
                               const wxRect& rect,
                               int flags = 0) wxOVERRIDE;

    virtual void DrawChoice(wxWindow *win,
                            wxDC& dc,
                            const wxRect& rect,
                            int flags = 0) wxOVERRIDE;

    virtual void DrawComboBox(wxWindow *win,
                              wxDC& dc,
                              const wxRect& rect,
                              int flags = 0) wxOVERRIDE;

    virtual void DrawTextCtrl(wxWindow *win,
                              wxDC& dc,
                              const wxRect& rect,
                              int flags = 0) wxOVERRIDE;

    virtual void DrawRadioBitmap(wxWindow *win,
                                 wxDC& dc,
                                 const wxRect& rect,
                                 int flags = 0) wxOVERRIDE;

    virtual void DrawGauge(wxWindow *win,
                           wxDC& dc,
                           const wxRect& rect,
                           int value,
                           int max,
                           int flags = 0) wxOVERRIDE;

    virtual void DrawItemText(wxWindow *win,
                              wxDC& dc,
                              const wxString& text,
                              const wxRect& rect,
                              int align = wxALIGN_LEFT | wxALIGN_TOP,
                              int flags = 0,
                              wxEllipsizeMode ellipsizeMode = wxELLIPSIZE_END) wxOVERRIDE;

    virtual wxSplitterRenderParams GetSplitterParams(const wxWindow *win) wxOVERRIDE;

    virtual wxRendererVersion GetVersion() const wxOVERRIDE
    {
        return wxRendererVersion(wxRendererVersion::Current_Version,
                                 wxRendererVersion::Current_Age);
    }

private:
    // the colour behind what is drawn in the window
    static wxColour Background(const wxWindow *win)
    {
        return win ? win->GetBackgroundColour()
                   : wxHaikuLook::Colour(B_PANEL_BACKGROUND_COLOR);
    }

    // the colour around a control drawn in the rectangle: the one of the
    // parent if the control fills the window
    static wxColour Outside(const wxWindow *win, const wxRect& rect)
    {
        if ( win && win->GetParent() && rect.x <= 0 && rect.y <= 0
                && rect.GetSize() == win->GetClientSize() )
        {
            return win->GetParent()->GetBackgroundColour();
        }

        return Background(win);
    }

    // the colour of arrows and marks on a background
    static wxColour MarkColour(const wxColour& background, int flags)
    {
        const rgb_color bg = wxHaikuColour(background);
        const rgb_color mark = tint_color(bg, wxHaikuLook::IsDark(bg)
                                                ? B_LIGHTEN_2_TINT
                                                : B_DARKEN_4_TINT);
        const wxColour colour = wxFromHaikuColour(mark);
        return flags & wxCONTROL_DISABLED
                ? wxHaikuLook::DisabledText(colour, background)
                : colour;
    }

    wxDECLARE_NO_COPY_CLASS(wxRendererHaiku);
};

} // anonymous namespace

/* static */
wxRendererNative& wxRendererNative::GetDefault()
{
    static wxRendererHaiku s_renderer;

    return s_renderer;
}

// ----------------------------------------------------------------------------
// column titles: as the titles of a BColumnListView
// ----------------------------------------------------------------------------

int wxRendererHaiku::DrawHeaderButton(wxWindow *win,
                                      wxDC& dc,
                                      const wxRect& rect,
                                      int flags,
                                      wxHeaderSortIconType sortArrow,
                                      wxHeaderButtonParams *params)
{
    const wxColour panel = wxHaikuLook::Colour(B_PANEL_BACKGROUND_COLOR);

    // the surface between the line under the titles and the separator at
    // the right of each of them
    wxRect rectBg = rect;
    rectBg.width--;
    rectBg.height--;

    if ( !wxHaikuLook::DrawButtonBackground(dc, rectBg,
            flags & (wxCONTROL_PRESSED | wxCONTROL_CURRENT |
                     wxCONTROL_DISABLED),
            panel,
            BControlLook::B_TOP_BORDER | BControlLook::B_BOTTOM_BORDER) )
    {
        return m_rendererNative.DrawHeaderButton(win, dc, rect, flags,
                                                 sortArrow, params);
    }

    dc.SetPen(wxHaikuLook::FrameColour(panel));
    dc.DrawLine(rect.x, rect.GetBottom(), rect.GetRight() + 1,
                rect.GetBottom());
    dc.DrawLine(rect.GetRight(), rect.y, rect.GetRight(), rect.GetBottom());

    return DrawHeaderButtonContents(win, dc, rect, flags, sortArrow, params);
}

int wxRendererHaiku::GetHeaderButtonHeight(wxWindow *win)
{
    // the titles of a BColumnListView: the font and 8 pixels, with the line
    // under them
    const int heightFont = win ? win->GetCharHeight()
                               : wxHaikuLook::Scaled(16);
    return heightFont + wxHaikuLook::Scaled(8) + 1;
}

int wxRendererHaiku::GetHeaderButtonMargin(wxWindow *WXUNUSED(win))
{
    return wxHaikuLook::LabelSpacing();
}

// ----------------------------------------------------------------------------
// tree items: the latch of BOutlineListView
// ----------------------------------------------------------------------------

void wxRendererHaiku::DrawTreeItemButton(wxWindow *win,
                                         wxDC& dc,
                                         const wxRect& rect,
                                         int flags)
{
    // The arrow is stroked in the middle third of its rectangle, with a pen
    // of a sixth of its width: the latch of an outline list is as wide as
    // the font is high.
    wxRect rectLatch(0, 0, wxHaikuLook::Scaled(12), wxHaikuLook::Scaled(16));
    rectLatch = rectLatch.CentreIn(rect);

    if ( !wxHaikuLook::DrawArrow(dc, rectLatch,
            flags & wxCONTROL_EXPANDED ? wxDOWN : wxRIGHT,
            MarkColour(wxHaikuLook::Colour(B_PANEL_BACKGROUND_COLOR), flags)) )
    {
        m_rendererNative.DrawTreeItemButton(win, dc, rect, flags);
    }
}

wxSize wxRendererHaiku::GetExpanderSize(wxWindow *WXUNUSED(win))
{
    const int size = wxHaikuLook::Scaled(12);
    return wxSize(size, size);
}

void wxRendererHaiku::DrawCollapseButton(wxWindow *win,
                                         wxDC& dc,
                                         const wxRect& rect,
                                         int flags)
{
    wxRect rectArrow(0, 0, wxHaikuLook::Scaled(15), wxHaikuLook::Scaled(15));
    rectArrow = rectArrow.CentreIn(rect);

    if ( !wxHaikuLook::DrawArrow(dc, rectArrow,
            flags & wxCONTROL_CHECKED ? wxDOWN : wxRIGHT,
            MarkColour(wxHaikuLook::Colour(B_PANEL_BACKGROUND_COLOR), flags)) )
    {
        m_rendererNative.DrawCollapseButton(win, dc, rect, flags);
    }
}

wxSize wxRendererHaiku::GetCollapseButtonSize(wxWindow *WXUNUSED(win),
                                              wxDC& WXUNUSED(dc))
{
    const int size = wxHaikuLook::Scaled(15);
    return wxSize(size, size);
}

// ----------------------------------------------------------------------------
// splitter: BSplitView
// ----------------------------------------------------------------------------

wxSplitterRenderParams
wxRendererHaiku::GetSplitterParams(const wxWindow *WXUNUSED(win))
{
    // the splitter of a BSplitLayout is 6 pixels, no border, no hot state
    return wxSplitterRenderParams(wxHaikuLook::Scaled(6), 0, false);
}

void wxRendererHaiku::DrawSplitterBorder(wxWindow *WXUNUSED(win),
                                         wxDC& WXUNUSED(dc),
                                         const wxRect& WXUNUSED(rect),
                                         int WXUNUSED(flags))
{
    // a split view has no border
}

void wxRendererHaiku::DrawSplitterSash(wxWindow *win,
                                       wxDC& dc,
                                       const wxSize& size,
                                       wxCoord position,
                                       wxOrientation orient,
                                       int flags)
{
    const int width = GetSplitterParams(win).widthSash;

    wxRect rect;
    if ( orient == wxVERTICAL )
        rect = wxRect(position, 0, width, size.y);
    else
        rect = wxRect(0, position, size.x, width);

    if ( !wxHaikuLook::DrawSplitter(dc, rect, orient, flags, Background(win)) )
        m_rendererNative.DrawSplitterSash(win, dc, size, position, orient,
                                          flags);
}

// ----------------------------------------------------------------------------
// buttons and marks
// ----------------------------------------------------------------------------

void wxRendererHaiku::DrawPushButton(wxWindow *win,
                                     wxDC& dc,
                                     const wxRect& rect,
                                     int flags)
{
    if ( !wxHaikuLook::DrawButton(dc, rect, flags,
            wxHaikuLook::Colour(B_CONTROL_BACKGROUND_COLOR),
            Outside(win, rect)) )
    {
        m_rendererNative.DrawPushButton(win, dc, rect, flags);
    }
}

void wxRendererHaiku::DrawComboBoxDropButton(wxWindow *win,
                                             wxDC& dc,
                                             const wxRect& rect,
                                             int flags)
{
    // the button at the end of an editable combo box: the end of a menu
    // field with its marker
    if ( !wxHaikuLook::DrawMenuFieldEnd(dc, rect, flags,
            wxHaikuLook::Colour(B_CONTROL_BACKGROUND_COLOR)) )
    {
        m_rendererNative.DrawComboBoxDropButton(win, dc, rect, flags);
    }
}

void wxRendererHaiku::DrawDropArrow(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags)
{
    const wxColour bg = wxHaikuLook::Colour(B_CONTROL_BACKGROUND_COLOR);
    wxColour colour = wxHaikuLook::Colour(B_CONTROL_TEXT_COLOR);
    if ( flags & wxCONTROL_DISABLED )
        colour = wxHaikuLook::DisabledText(colour, bg);

    // the arrow takes the middle third of a square
    const int side = wxMax(wxHaikuLook::Scaled(15),
                           wxMin(rect.width, rect.height));
    wxRect rectArrow(0, 0, side, side);
    rectArrow = rectArrow.CentreIn(rect);

    if ( !wxHaikuLook::DrawArrow(dc, rectArrow, wxDOWN, colour) )
        m_rendererNative.DrawDropArrow(win, dc, rect, flags);
}

void wxRendererHaiku::DrawCheckBox(wxWindow *win,
                                   wxDC& dc,
                                   const wxRect& rect,
                                   int flags)
{
    // wxCONTROL_CURRENT has no look of its own
    if ( !wxHaikuLook::DrawCheckBox(dc, rect, flags, Background(win)) )
        m_rendererNative.DrawCheckBox(win, dc, rect, flags);
}

void wxRendererHaiku::DrawRadioBitmap(wxWindow *win,
                                      wxDC& dc,
                                      const wxRect& rect,
                                      int flags)
{
    if ( !wxHaikuLook::DrawRadioButton(dc, rect, flags, Background(win)) )
        m_rendererNative.DrawRadioBitmap(win, dc, rect, flags);
}

void wxRendererHaiku::DrawCheckMark(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags)
{
    // the mark of a marked BMenuItem (BMenuItem::_DrawMarkSymbol())
    wxColour colour = dc.GetTextForeground();
    if ( !colour.IsOk() )
        colour = wxHaikuLook::Colour(B_MENU_ITEM_TEXT_COLOR);
    if ( flags & wxCONTROL_DISABLED )
        colour = wxHaikuLook::DisabledText(colour, Background(win));

    const double size = 0.3 * wxMin(rect.width, rect.height);
    const double cx = rect.x + rect.width / 2.0,
                 cy = rect.y + rect.height / 2.0;

    wxPoint points[3];
    points[0] = wxPoint(wxRound(cx - size), wxRound(cy - size * 0.25));
    points[1] = wxPoint(wxRound(cx - size * 0.25), wxRound(cy + size));
    points[2] = wxPoint(wxRound(cx + size), wxRound(cy - size));

    wxPen pen(colour, wxMax(2, wxHaikuLook::Scaled(2)));
    pen.SetCap(wxCAP_BUTT);
    pen.SetJoin(wxJOIN_MITER);
    dc.SetPen(pen);
    dc.DrawLines(3, points);
}

wxSize wxRendererHaiku::GetCheckBoxSize(wxWindow *WXUNUSED(win),
                                        int WXUNUSED(flags))
{
    const int size = wxHaikuLook::CheckMarkSize();
    return wxSize(size, size);
}

wxSize wxRendererHaiku::GetCheckMarkSize(wxWindow *WXUNUSED(win))
{
    const int size = wxHaikuLook::Scaled(13);
    return wxSize(size, size);
}

// ----------------------------------------------------------------------------
// items of lists
// ----------------------------------------------------------------------------

void wxRendererHaiku::DrawItemSelectionRect(wxWindow *WXUNUSED(win),
                                            wxDC& dc,
                                            const wxRect& rect,
                                            int flags)
{
    if ( flags & wxCONTROL_SELECTED )
    {
        wxHaikuLook::Fill(dc, rect,
            wxHaikuLook::SelectionColour((flags & wxCONTROL_FOCUSED) != 0));
    }

    // the item with the keyboard focus of the focus view
    if ( (flags & wxCONTROL_CURRENT) && (flags & wxCONTROL_FOCUSED) )
        wxHaikuLook::DrawFocusFrame(dc, rect);
}

void wxRendererHaiku::DrawFocusRect(wxWindow *WXUNUSED(win),
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int WXUNUSED(flags))
{
    wxHaikuLook::DrawFocusFrame(dc, rect);
}

void wxRendererHaiku::DrawItemText(wxWindow *win,
                                   wxDC& dc,
                                   const wxString& text,
                                   const wxRect& rect,
                                   int align,
                                   int flags,
                                   wxEllipsizeMode ellipsizeMode)
{
    // the text of a normal item keeps the colour the caller set: items can
    // have colours of their own
    wxColour colour;
    if ( flags & wxCONTROL_SELECTED )
    {
        colour = wxHaikuLook::SelectionTextColour(
                    (flags & wxCONTROL_FOCUSED) != 0);
    }
    else if ( flags & wxCONTROL_DISABLED )
    {
        colour = wxHaikuLook::DisabledText(
                    wxHaikuLook::Colour(B_LIST_ITEM_TEXT_COLOR),
                    win ? win->GetBackgroundColour()
                        : wxHaikuLook::Colour(B_LIST_BACKGROUND_COLOR));
    }

    const wxString paintText = wxControl::Ellipsize(text, dc, ellipsizeMode,
                                                    rect.GetWidth());

    wxDCTextColourChanger changeFg(dc);
    if ( colour.IsOk() )
        changeFg.Set(colour);
    dc.DrawLabel(paintText, rect, align);
}

// ----------------------------------------------------------------------------
// whole controls
// ----------------------------------------------------------------------------

void wxRendererHaiku::DrawChoice(wxWindow *win,
                                 wxDC& dc,
                                 const wxRect& rect,
                                 int flags)
{
    if ( !wxHaikuLook::DrawMenuField(dc, rect, flags,
            wxHaikuLook::Colour(B_CONTROL_BACKGROUND_COLOR),
            Outside(win, rect), true) )
    {
        m_rendererNative.DrawChoice(win, dc, rect, flags);
    }
}

void wxRendererHaiku::DrawTextCtrl(wxWindow *win,
                                   wxDC& dc,
                                   const wxRect& rect,
                                   int flags)
{
    wxRect inside;
    if ( !wxHaikuLook::DrawTextFrame(dc, rect, flags, Outside(win, rect),
                                     &inside) )
    {
        m_rendererNative.DrawTextCtrl(win, dc, rect, flags);
        return;
    }

    wxHaikuLook::Fill(dc, inside,
        wxHaikuLook::Colour(flags & wxCONTROL_DISABLED
                                ? B_PANEL_BACKGROUND_COLOR
                                : B_DOCUMENT_BACKGROUND_COLOR));
}

void wxRendererHaiku::DrawComboBox(wxWindow *win,
                                   wxDC& dc,
                                   const wxRect& rect,
                                   int flags)
{
    // a text control with a drop button at its end
    wxRect inside;
    if ( !wxHaikuLook::DrawTextFrame(dc, rect, flags, Outside(win, rect),
                                     &inside) )
    {
        m_rendererNative.DrawComboBox(win, dc, rect, flags);
        return;
    }

    wxHaikuLook::Fill(dc, inside,
        wxHaikuLook::Colour(flags & wxCONTROL_DISABLED
                                ? B_PANEL_BACKGROUND_COLOR
                                : B_DOCUMENT_BACKGROUND_COLOR));

    wxRect rectButton = inside;
    rectButton.width = wxMin(inside.width, wxHaikuLook::ScrollBarWidth());
    rectButton.x = inside.GetRight() - rectButton.width + 1;
    DrawComboBoxDropButton(win, dc, rectButton,
                           flags & ~wxCONTROL_FOCUSED);
}

void wxRendererHaiku::DrawGauge(wxWindow *win,
                                wxDC& dc,
                                const wxRect& rect,
                                int value,
                                int max,
                                int flags)
{
    if ( !wxHaikuLook::DrawGauge(dc, rect, value, max,
            (flags & wxCONTROL_SPECIAL) != 0,
            Outside(win, rect),
            wxHaikuLook::Colour(B_STATUS_BAR_COLOR)) )
    {
        m_rendererNative.DrawGauge(win, dc, rect, value, max, flags);
    }
}
