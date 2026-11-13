/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/look.h
// Purpose:     drawing the parts of controls with the system's BControlLook
//              on a wxDC: shared by the native renderer and the wxUniversal
//              theme of the port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// This header includes the Be API: it is for the .cpp files of the library
// only (src/haiku/renderer.cpp, src/univ/themes/haiku.cpp).

#ifndef _WX_HAIKU_LOOK_H_
#define _WX_HAIKU_LOOK_H_

#include "wx/haiku/private.h"

#include <ControlLook.h>
#include <InterfaceDefs.h>

class WXDLLIMPEXP_FWD_CORE wxDC;
class WXDLLIMPEXP_FWD_CORE wxHaikuDCImpl;

// ----------------------------------------------------------------------------
// wxHaikuLookPainter: the view behind a wxDC, locked for native drawing
// ----------------------------------------------------------------------------

// The view is left in B_OP_OVER with a pen of 1: the shapes BControlLook
// draws blend their edges with what is on the target, whatever it is. The DC
// sets the colours, mode, pen and font again before it draws next, nothing
// else of the state of the view is changed here.
//
// BControlLook is not free: most of its functions change the clipping inside
// a PushState()/PopState() pair, which invalidates the state BView caches on
// the client side, and ShouldDraw() then asks app_server for the transform
// of the view (one synchronous round trip per call). So one call per control
// part at most, and plain wxDC drawing for what is a line or a filled
// rectangle anyway.
class wxHaikuLookPainter
{
public:
    explicit wxHaikuLookPainter(wxDC& dc);
    ~wxHaikuLookPainter();

    // false: the DC is not one of the port (wxGCDC, printing...) or its
    // target cannot be drawn on (yet); the caller draws with the wxDC API
    bool IsOk() const { return m_view != NULL; }

    BView *View() const { return m_view; }

    // logical coordinates of the DC to the pixels of the view
    BRect Rect(const wxRect& r) const;

    // the colour the anti-aliased edges and the low colour patterns use:
    // what is behind the part being drawn
    void SetBackground(rgb_color colour);

    // check boxes and radio buttons take the size of their marks from the
    // font of the view
    void SetPlainFont();

private:
    wxHaikuDCImpl *m_impl;
    BView *m_view;

    wxDECLARE_NO_COPY_CLASS(wxHaikuLookPainter);
};

// ----------------------------------------------------------------------------
// wxHaikuLook: colours, metrics and the parts of controls
// ----------------------------------------------------------------------------

namespace wxHaikuLook
{

// --- colours: ui_color() is a read of shared memory, always current (the
// appearance can switch between light and dark at any time), never cached

inline wxColour Colour(color_which which)
{
    return wxFromHaikuColour(ui_color(which));
}

inline bool IsDark(const rgb_color& c)
{
    return c.red + c.green + c.blue <= 128 * 3;
}

inline wxColour Tint(const wxColour& colour, float tint)
{
    return wxFromHaikuColour(tint_color(wxHaikuColour(colour), tint));
}

// the colour of disabled text of the given colour on a background
wxColour DisabledText(const wxColour& text, const wxColour& background);

// the frame the system draws around scroll views, tabs, column titles...
wxColour FrameColour(const wxColour& base);

// B_LIST_SELECTED_BACKGROUND_COLOR, or grey for a list without the focus
wxColour SelectionColour(bool focused);
wxColour SelectionTextColour(bool focused);

// --- metrics: all follow the size of be_plain_font, which air/OS scales
// for HiDPI screens

// n pixels of an unscaled (12 pixel font) desktop
int Scaled(double n);

// BControlLook::DefaultLabelSpacing(): half of the font size
int LabelSpacing();

// the width of a scroll bar including its frame
int ScrollBarWidth();

// the side of a check box or radio button mark (BCheckBox: from the ascent
// of the plain font)
int CheckMarkSize();

// the frame of a text control or scroll view: edge and dark frame
int TextFrameWidth();

// --- flags: wxCONTROL_XXX to BControlLook flags; a pressed control is
// B_ACTIVATED (push buttons, scroll bar buttons), a hot one B_HOVER
uint32 Flags(int flags);

// --- parts; all return false without drawing anything if the DC cannot be
// drawn on with the Be API (see wxHaikuLookPainter::IsOk())

// A push button: its frame, with the default button indicator inside "rect"
// if wxCONTROL_ISDEFAULT, and its surface. "base" is the colour of the
// button, "outside" what is behind it. extraFlags: BControlLook flags
// (B_FLAT...).
bool DrawButton(wxDC& dc, const wxRect& rect, int flags,
                const wxColour& base, const wxColour& outside,
                wxRect *inside = NULL, uint32 extraFlags = 0);

// The surface of a button only (column titles, tool bar and scroll bar
// buttons): "borders" are the sides with a bevel
bool DrawButtonBackground(wxDC& dc, const wxRect& rect, int flags,
                          const wxColour& base,
                          uint32 borders = BControlLook::B_ALL_BORDERS,
                          wxOrientation orient = wxHORIZONTAL);

// wxCONTROL_CHECKED, wxCONTROL_UNDETERMINED, wxCONTROL_PRESSED (clicked),
// wxCONTROL_DISABLED, wxCONTROL_FOCUSED
bool DrawCheckBox(wxDC& dc, const wxRect& rect, int flags,
                  const wxColour& base);
bool DrawRadioButton(wxDC& dc, const wxRect& rect, int flags,
                     const wxColour& base);

// The frame of a text control (two pixels at scale 1): in the keyboard
// navigation colour if wxCONTROL_FOCUSED
bool DrawTextFrame(wxDC& dc, const wxRect& rect, int flags,
                   const wxColour& base, wxRect *inside = NULL);

// A menu field (BMenuField: the native wxChoice), frame and surface with
// the pop-up marker at the right if "marker"
bool DrawMenuField(wxDC& dc, const wxRect& rect, int flags,
                   const wxColour& base, const wxColour& outside,
                   bool marker, wxRect *inside = NULL);

// the width the pop-up marker of a menu field takes at its right end
int MenuFieldMarkerWidth();

// The right end of the surface of a menu field with its marker and the line
// which separates it, without the frame: the drop button of a combo box
bool DrawMenuFieldEnd(wxDC& dc, const wxRect& rect, int flags,
                      const wxColour& base);

// dir: wxLEFT, wxRIGHT, wxUP, wxDOWN. The arrow is stroked in "colour"
// inside the middle third of the rectangle, as BControlLook does it.
bool DrawArrow(wxDC& dc, const wxRect& rect, wxDirection dir,
               const wxColour& colour, int flags = 0);

// borders
bool DrawBorder(wxDC& dc, const wxRect& rect, border_style style, int flags,
                const wxColour& base,
                uint32 borders = BControlLook::B_ALL_BORDERS,
                wxRect *inside = NULL);
bool DrawRaisedBorder(wxDC& dc, const wxRect& rect, int flags,
                      const wxColour& base, wxRect *inside = NULL);
bool DrawGroupFrame(wxDC& dc, const wxRect& rect, const wxColour& base,
                    uint32 borders = BControlLook::B_ALL_BORDERS);

// a progress bar (BStatusBar) filled up to "pos" out of "range"
bool DrawGauge(wxDC& dc, const wxRect& rect, int pos, int range,
               bool vertical, const wxColour& base, const wxColour& bar);

// the dotted sash of a split view
bool DrawSplitter(wxDC& dc, const wxRect& rect, wxOrientation sash,
                  int flags, const wxColour& base);

// a rectangle in the keyboard navigation colour
void DrawFocusFrame(wxDC& dc, const wxRect& rect);

// a filled rectangle with the wxDC API (works on any DC)
void Fill(wxDC& dc, const wxRect& rect, const wxColour& colour);

} // namespace wxHaikuLook

#endif // _WX_HAIKU_LOOK_H_
