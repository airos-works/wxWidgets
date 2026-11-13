///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/themes/haiku.cpp
// Purpose:     wxUniversal theme of the wxHaiku port: the controls are drawn
//              by the system's BControlLook, in the colours of ui_color()
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ===========================================================================
// declarations
// ===========================================================================

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/univ/theme.h"

#if wxUSE_THEME_HAIKU

#ifndef WX_PRECOMP
    #include "wx/timer.h"
    #include "wx/intl.h"
    #include "wx/dc.h"
    #include "wx/window.h"

    #include "wx/dcmemory.h"
    #include "wx/dcclient.h"

    #include "wx/button.h"
    #include "wx/bmpbuttn.h"
    #include "wx/listbox.h"
    #include "wx/checklst.h"
    #include "wx/combobox.h"
    #include "wx/scrolbar.h"
    #include "wx/slider.h"
    #include "wx/textctrl.h"
    #include "wx/toolbar.h"
    #include "wx/statusbr.h"
    #include "wx/menu.h"
    #include "wx/settings.h"
    #include "wx/toplevel.h"
    #include "wx/frame.h"
#endif // WX_PRECOMP

#include "wx/notebook.h"
#include "wx/spinbutt.h"
#include "wx/artprov.h"
#include "wx/tglbtn.h"
#include "wx/popupwin.h"
#include "wx/region.h"
#include "wx/dcscreen.h"

#include "wx/univ/scrtimer.h"
#include "wx/univ/stdrend.h"
#include "wx/univ/inpcons.h"
#include "wx/univ/inphand.h"
#include "wx/univ/colschem.h"

#include "wx/haiku/look.h"

#include <math.h>

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

namespace
{

inline int Scaled(double n) { return wxHaikuLook::Scaled(n); }

inline wxColour UIColour(color_which which)
{
    return wxHaikuLook::Colour(which);
}

// A button without border is a flat one. wxToggleButton has no border in
// wxUniversal by default (its style then says wxBORDER_NONE as if the
// application had asked for it): it always is a button.
bool IsFlatButton(const wxWindow *win)
{
    if ( !win || !win->HasFlag(wxBORDER_NONE) )
        return false;

#if wxUSE_TOGGLEBTN
    if ( wxDynamicCast(win, wxToggleButton) )
        return false;
#endif // wxUSE_TOGGLEBTN

    return true;
}

// The tint of the control look: lightening tints of a dark colour move a
// quarter as far in the dark appearance (HaikuControlLook.cpp, _Tint())
wxColour LookTint(const wxColour& colour, float tint)
{
    const rgb_color c = wxHaikuColour(colour);
    if ( tint < B_NO_TINT && wxHaikuLook::IsDark(c)
            && wxHaikuLook::IsDark(ui_color(B_PANEL_BACKGROUND_COLOR)) )
    {
        tint = B_NO_TINT - (B_NO_TINT - tint) / 4;
    }

    return wxFromHaikuColour(tint_color(c, tint));
}

// wxAnyButton has no class information of its own
const wxAnyButton *AsButton(const wxWindow *win)
{
    if ( !win )
        return NULL;

#if wxUSE_BUTTON
    if ( wxDynamicCast(win, wxButton) )
        return static_cast<const wxAnyButton *>(win);
#endif // wxUSE_BUTTON
#if wxUSE_TOGGLEBTN
    if ( wxDynamicCast(win, wxToggleButton) )
        return static_cast<const wxAnyButton *>(win);
#endif // wxUSE_TOGGLEBTN

    return NULL;
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// wxHaikuColourScheme: the colours of the system, read when they are needed
// ----------------------------------------------------------------------------

class wxHaikuColourScheme : public wxColourScheme
{
public:
    virtual wxColour Get(StdColour col) const wxOVERRIDE;
    virtual wxColour GetBackground(wxWindow *win) const wxOVERRIDE;

    // the background of what is behind the window
    wxColour GetParentBackground(wxWindow *win) const
    {
        wxWindow * const parent = win && !win->IsTopLevel() ? win->GetParent()
                                                             : NULL;
        return parent ? GetBackground(parent)
                      : UIColour(B_PANEL_BACKGROUND_COLOR);
    }
};

// ----------------------------------------------------------------------------
// wxHaikuRenderer
// ----------------------------------------------------------------------------

class wxHaikuRenderer : public wxStdRenderer
{
public:
    wxHaikuRenderer(const wxHaikuColourScheme *scheme);

    virtual void DrawBackground(wxDC& dc,
                                const wxColour& col,
                                const wxRect& rect,
                                int flags = 0,
                                wxWindow *window = NULL) wxOVERRIDE;
    virtual void DrawButtonSurface(wxDC& dc,
                                   const wxColour& col,
                                   const wxRect& rect,
                                   int flags) wxOVERRIDE;

    virtual void DrawFocusRect(wxWindow *win, wxDC& dc, const wxRect& rect,
                               int flags = 0) wxOVERRIDE;

    virtual void DrawLabel(wxDC& dc,
                           const wxString& label,
                           const wxRect& rect,
                           int flags = 0,
                           int alignment = wxALIGN_LEFT | wxALIGN_TOP,
                           int indexAccel = -1,
                           wxRect *rectBounds = NULL) wxOVERRIDE;
    virtual void DrawButtonLabel(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& image,
                                 const wxRect& rect,
                                 int flags = 0,
                                 int alignment = wxALIGN_LEFT | wxALIGN_TOP,
                                 int indexAccel = -1,
                                 wxRect *rectBounds = NULL) wxOVERRIDE;

    virtual void DrawBorder(wxDC& dc,
                            wxBorder border,
                            const wxRect& rect,
                            int flags = 0,
                            wxRect *rectIn = NULL) wxOVERRIDE;
    virtual void DrawTextBorder(wxDC& dc,
                                wxBorder border,
                                const wxRect& rect,
                                int flags = 0,
                                wxRect *rectIn = NULL) wxOVERRIDE;
    virtual void DrawButtonBorder(wxDC& dc,
                                  const wxRect& rect,
                                  int flags = 0,
                                  wxRect *rectIn = NULL) wxOVERRIDE;

    virtual void DrawHorizontalLine(wxDC& dc,
                                    wxCoord y, wxCoord x1, wxCoord x2) wxOVERRIDE;
    virtual void DrawVerticalLine(wxDC& dc,
                                  wxCoord x, wxCoord y1, wxCoord y2) wxOVERRIDE;
    virtual void DrawFrame(wxDC& dc,
                           const wxString& label,
                           const wxRect& rect,
                           int flags = 0,
                           int alignment = wxALIGN_LEFT,
                           int indexAccel = -1) wxOVERRIDE;

    virtual void DrawArrow(wxDC& dc,
                           wxDirection dir,
                           const wxRect& rect,
                           int flags = 0) wxOVERRIDE;
    virtual void DrawScrollbarArrow(wxDC& dc,
                                    wxDirection dir,
                                    const wxRect& rect,
                                    int flags = 0) wxOVERRIDE;
    virtual void DrawScrollbarThumb(wxDC& dc,
                                    wxOrientation orient,
                                    const wxRect& rect,
                                    int flags = 0) wxOVERRIDE;
    virtual void DrawScrollbarShaft(wxDC& dc,
                                    wxOrientation orient,
                                    const wxRect& rect,
                                    int flags = 0) wxOVERRIDE;
    virtual void DrawScrollCorner(wxDC& dc, const wxRect& rect) wxOVERRIDE;

    virtual void DrawItem(wxDC& dc,
                          const wxString& label,
                          const wxRect& rect,
                          int flags = 0) wxOVERRIDE;

    virtual void DrawCheckButton(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& bitmap,
                                 const wxRect& rect,
                                 int flags = 0,
                                 wxAlignment align = wxALIGN_LEFT,
                                 int indexAccel = -1) wxOVERRIDE;
    virtual void DrawRadioButton(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& bitmap,
                                 const wxRect& rect,
                                 int flags = 0,
                                 wxAlignment align = wxALIGN_LEFT,
                                 int indexAccel = -1) wxOVERRIDE;

#if wxUSE_TOOLBAR
    virtual void DrawToolBarButton(wxDC& dc,
                                   const wxString& label,
                                   const wxBitmap& bitmap,
                                   const wxRect& rect,
                                   int flags = 0,
                                   long style = 0,
                                   int tbarStyle = 0) wxOVERRIDE;
#endif // wxUSE_TOOLBAR

#if wxUSE_TEXTCTRL
    virtual void DrawTextLine(wxDC& dc,
                              const wxString& text,
                              const wxRect& rect,
                              int selStart = -1,
                              int selEnd = -1,
                              int flags = 0) wxOVERRIDE;
#endif // wxUSE_TEXTCTRL

#if wxUSE_NOTEBOOK
    virtual void DrawTab(wxDC& dc,
                         const wxRect& rect,
                         wxDirection dir,
                         const wxString& label,
                         const wxBitmap& bitmap = wxNullBitmap,
                         int flags = 0,
                         int indexAccel = -1) wxOVERRIDE;
#endif // wxUSE_NOTEBOOK

#if wxUSE_SLIDER
    virtual void DrawSliderShaft(wxDC& dc,
                                 const wxRect& rect,
                                 int lenThumb,
                                 wxOrientation orient,
                                 int flags = 0,
                                 long style = 0,
                                 wxRect *rectShaft = NULL) wxOVERRIDE;
    virtual void DrawSliderThumb(wxDC& dc,
                                 const wxRect& rect,
                                 wxOrientation orient,
                                 int flags = 0,
                                 long style = 0) wxOVERRIDE;
    virtual void DrawSliderTicks(wxDC& dc,
                                 const wxRect& rect,
                                 int lenThumb,
                                 wxOrientation orient,
                                 int start,
                                 int end,
                                 int step = 1,
                                 int flags = 0,
                                 long style = 0) wxOVERRIDE;
#endif // wxUSE_SLIDER

#if wxUSE_MENUS
    virtual void DrawMenuBarItem(wxDC& dc,
                                 const wxRect& rect,
                                 const wxString& label,
                                 int flags = 0,
                                 int indexAccel = -1) wxOVERRIDE;
    virtual void DrawMenuItem(wxDC& dc,
                              wxCoord y,
                              const wxMenuGeometryInfo& geometryInfo,
                              const wxString& label,
                              const wxString& accel,
                              const wxBitmap& bitmap = wxNullBitmap,
                              int flags = 0,
                              int indexAccel = -1) wxOVERRIDE;
    virtual void DrawMenuSeparator(wxDC& dc,
                                   wxCoord y,
                                   const wxMenuGeometryInfo& geomInfo) wxOVERRIDE;
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    virtual void DrawStatusField(wxDC& dc,
                                 const wxRect& rect,
                                 const wxString& label,
                                 int flags = 0, int style = 0) wxOVERRIDE;

    virtual wxSize GetStatusBarBorders() const wxOVERRIDE
        { return wxSize(Scaled(2), Scaled(2)); }
    virtual wxCoord GetStatusBarBorderBetweenFields() const wxOVERRIDE
        { return Scaled(4); }
    virtual wxSize GetStatusBarFieldMargins() const wxOVERRIDE
        { return wxSize(Scaled(4), Scaled(2)); }
#endif // wxUSE_STATUSBAR

#if wxUSE_COMBOBOX
    // not used: the combo box is a wxComboCtrl, which draws its button with
    // wxRendererNative::DrawComboBoxDropButton()
    virtual void GetComboBitmaps(wxBitmap *WXUNUSED(bmpNormal),
                                 wxBitmap *WXUNUSED(bmpFocus),
                                 wxBitmap *WXUNUSED(bmpPressed),
                                 wxBitmap *WXUNUSED(bmpDisabled)) wxOVERRIDE
    {
    }
#endif // wxUSE_COMBOBOX

    // geometry

    virtual wxRect GetBorderDimensions(wxBorder border) const wxOVERRIDE;

    // as in a BScrollView: the scroll bars are inside the frame
    virtual bool AreScrollbarsInsideBorder() const wxOVERRIDE { return true; }

    virtual void AdjustSize(wxSize *size, const wxWindow *window) wxOVERRIDE;

#if wxUSE_SCROLLBAR
    virtual wxSize GetScrollbarArrowSize() const wxOVERRIDE
    {
        const int width = wxHaikuLook::ScrollBarWidth();
        return wxSize(width, width);
    }
#endif // wxUSE_SCROLLBAR

    virtual wxCoord GetListboxItemHeight(wxCoord fontHeight) wxOVERRIDE
    {
        // BStringItem: the height of the font and 4 pixels; a check list
        // box needs room for the mark
        return wxMax(fontHeight + Scaled(4),
                     wxHaikuLook::CheckMarkSize() + 2);
    }

    virtual wxSize GetCheckBitmapSize() const wxOVERRIDE
    {
        const int size = wxHaikuLook::CheckMarkSize();
        return wxSize(size, size);
    }
    virtual wxSize GetRadioBitmapSize() const wxOVERRIDE
        { return GetCheckBitmapSize(); }
    virtual wxCoord GetCheckItemMargin() const wxOVERRIDE
        { return Scaled(2); }

#if wxUSE_TOOLBAR
    virtual wxSize GetToolBarButtonSize(wxCoord *separator) const wxOVERRIDE
    {
        if ( separator )
            *separator = Scaled(7);
        return wxSize(Scaled(16), Scaled(16));
    }
    virtual wxSize GetToolBarMargin() const wxOVERRIDE
        { return wxSize(Scaled(4), Scaled(4)); }
#endif // wxUSE_TOOLBAR

#if wxUSE_NOTEBOOK
    // x: the distance of the first tab from the edge of the tab view; y:
    // what the selected tab is higher than the others (HaikuControlLook
    // lowers an inactive tab by 4 pixels at any scale)
    virtual wxSize GetTabIndent() const wxOVERRIDE
        { return wxSize(Scaled(6), 4); }

    // BTabView: a tab is ceil(3.3 * label spacing) wider than its label and
    // 8 pixels higher than its font, of which the 4 above are the indent
    virtual wxSize GetTabPadding() const wxOVERRIDE
    {
        return wxSize((int)ceil(wxHaikuLook::LabelSpacing() * 3.3 / 2),
                      Scaled(2));
    }
#endif // wxUSE_NOTEBOOK

#if wxUSE_SLIDER
    virtual wxCoord GetSliderDim() const wxOVERRIDE
        { return ThumbAcross() + 2*Scaled(2); }
    virtual wxCoord GetSliderTickLen() const wxOVERRIDE
        { return Scaled(6); }
    virtual wxRect GetSliderShaftRect(const wxRect& rect,
                                      int lenThumb,
                                      wxOrientation orient,
                                      long style = 0) const wxOVERRIDE;
    virtual wxSize GetSliderThumbSize(const wxRect& rect,
                                      int lenThumb,
                                      wxOrientation orient) const wxOVERRIDE;
#endif // wxUSE_SLIDER

    virtual wxSize GetProgressBarStep() const wxOVERRIDE
        { return wxSize(Scaled(10), Scaled(10)); }

#if wxUSE_MENUS
    virtual wxSize GetMenuBarItemSize(const wxSize& sizeText) const wxOVERRIDE;
    virtual wxMenuGeometryInfo *GetMenuGeometry(wxWindow *win,
                                                const wxMenu& menu) const wxOVERRIDE;
#endif // wxUSE_MENUS

protected:
    virtual void DrawCheckItemBitmap(wxDC& dc,
                                     const wxBitmap& bitmap,
                                     const wxRect& rect,
                                     int flags) wxOVERRIDE;

    // the marks are drawn, there are no bitmaps of them; the windows have
    // the decorations of the system
    virtual wxBitmap GetRadioBitmap(int WXUNUSED(flags)) wxOVERRIDE
        { return wxNullBitmap; }
    virtual wxBitmap GetCheckBitmap(int WXUNUSED(flags)) wxOVERRIDE
        { return wxNullBitmap; }
    virtual wxBitmap GetFrameButtonBitmap(FrameButtonType WXUNUSED(type)) wxOVERRIDE
        { return wxNullBitmap; }

#if wxUSE_TEXTCTRL
    // the margin between the frame of a text control and its text
    virtual int GetTextBorderWidth(const wxTextCtrl *WXUNUSED(text)) const wxOVERRIDE
        { return Scaled(2); }
#endif // wxUSE_TEXTCTRL

private:
    const wxHaikuColourScheme *Scheme() const
        { return static_cast<const wxHaikuColourScheme *>(m_scheme); }

    static wxColour Panel() { return UIColour(B_PANEL_BACKGROUND_COLOR); }

    // the background of the window the DC draws on
    wxColour WindowBackground(wxDC& dc) const
    {
        wxWindow * const win = dc.GetWindow();
        return win ? Scheme()->GetBackground(win) : Panel();
    }

    // the background around the window the DC draws on: the one of its
    // parent
    wxColour ParentBackground(wxDC& dc) const
    {
        wxWindow * const win = dc.GetWindow();
        return win ? Scheme()->GetParentBackground(win) : Panel();
    }

    // The colour of the text of a control: the UI colour "which" unless the
    // window (or its parent) was given a foreground colour, which the
    // control has set on the DC then; faded if disabled.
    wxColour TextColour(wxDC& dc, color_which which, int flags,
                        const wxColour& background) const
    {
        wxWindow * const win = dc.GetWindow();
        wxColour colour = win && win->UseFgCol() ? dc.GetTextForeground()
                                                 : UIColour(which);
        if ( !colour.IsOk() )
            colour = UIColour(which);
        if ( flags & wxCONTROL_DISABLED )
            colour = wxHaikuLook::DisabledText(colour, background);
        return colour;
    }

    // label drawing common to all controls: no mnemonic underlines and no
    // focus rectangles, the system has neither
    void DoDrawLabel(wxDC& dc, const wxString& label, const wxBitmap& image,
                     const wxRect& rect, int alignment, wxRect *rectBounds,
                     const wxColour& colour);

    // a check box or radio button with its label
    void DrawMarkAndLabel(wxDC& dc, bool radio, const wxString& label,
                          const wxBitmap& bitmap, const wxRect& rect,
                          int flags, wxAlignment align);

    // the frame lines of a scroll bar and the part of "rect" inside them
    wxRect DrawScrollbarFrame(wxDC& dc, wxOrientation orient,
                              const wxRect& rect, bool first, bool last);
    static uint32 ScrollbarFlags(wxDC& dc, int flags);

#if wxUSE_NOTEBOOK
    // the tab row and the box of the pages
    void DrawNotebookFrame(wxDC& dc, wxNotebook *notebook,
                           const wxRect& rectPage);
#endif // wxUSE_NOTEBOOK

#if wxUSE_MENUS
    bool DrawMenuBarBackground(wxDC& dc, const wxRect& rect);
    void DrawMenuSelection(wxDC& dc, const wxRect& rect);
#endif // wxUSE_MENUS

#if wxUSE_SLIDER
    static int ThumbAcross() { return Scaled(14); }
    static int ThumbAlong() { return Scaled(18); }
    static int BarThickness() { return Scaled(6); }

    // on which sides of the shaft the ticks are
    static void GetSliderTickSides(wxOrientation orient, long style,
                                   bool *before, bool *after);
#endif // wxUSE_SLIDER
};

// ----------------------------------------------------------------------------
// input handlers: the standard ones with the keys and clicks of the system
// ----------------------------------------------------------------------------

class wxHaikuInputHandler : public wxInputHandler
{
public:
    wxHaikuInputHandler() { }

    virtual bool HandleKey(wxInputConsumer *WXUNUSED(control),
                           const wxKeyEvent& WXUNUSED(event),
                           bool WXUNUSED(pressed)) wxOVERRIDE
    {
        return false;
    }

    virtual bool HandleMouse(wxInputConsumer *control,
                             const wxMouseEvent& event) wxOVERRIDE;
};

#if wxUSE_CHECKBOX
class wxHaikuCheckboxInputHandler : public wxStdInputHandler
{
public:
    wxHaikuCheckboxInputHandler(wxInputHandler *handler)
        : wxStdInputHandler(handler) { }

    virtual bool HandleKey(wxInputConsumer *control,
                           const wxKeyEvent& event,
                           bool pressed) wxOVERRIDE;
};
#endif // wxUSE_CHECKBOX

// ----------------------------------------------------------------------------
// wxHaikuArtProvider: nothing of its own, the port has the system's icons
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// wxHaikuTheme
// ----------------------------------------------------------------------------

WX_DEFINE_ARRAY_PTR(wxInputHandler *, wxHaikuArrayHandlers);

class wxHaikuTheme : public wxTheme
{
public:
    wxHaikuTheme();
    virtual ~wxHaikuTheme();

    virtual wxRenderer *GetRenderer() wxOVERRIDE;
    virtual wxArtProvider *GetArtProvider() wxOVERRIDE;
    virtual wxInputHandler *GetInputHandler(const wxString& control,
                                            wxInputConsumer *consumer) wxOVERRIDE;
    virtual wxColourScheme *GetColourScheme() wxOVERRIDE;

private:
    wxHaikuRenderer *m_renderer;

    // the names of the already created handlers and the handlers themselves
    // (these arrays are synchronized)
    wxSortedArrayString m_handlerNames;
    wxHaikuArrayHandlers m_handlers;

    wxHaikuColourScheme *m_scheme;

    WX_DECLARE_THEME(haiku)
};

// ============================================================================
// implementation
// ============================================================================

WX_IMPLEMENT_THEME(wxHaikuTheme, haiku, wxTRANSLATE("Haiku theme"));

// ----------------------------------------------------------------------------
// wxHaikuTheme
// ----------------------------------------------------------------------------

wxHaikuTheme::wxHaikuTheme()
{
    m_scheme = NULL;
    m_renderer = NULL;
}

wxHaikuTheme::~wxHaikuTheme()
{
    delete m_renderer;
    delete m_scheme;
}

wxRenderer *wxHaikuTheme::GetRenderer()
{
    if ( !m_renderer )
    {
        GetColourScheme();
        m_renderer = new wxHaikuRenderer(m_scheme);
    }

    return m_renderer;
}

wxArtProvider *wxHaikuTheme::GetArtProvider()
{
    return NULL;
}

wxColourScheme *wxHaikuTheme::GetColourScheme()
{
    if ( !m_scheme )
        m_scheme = new wxHaikuColourScheme;

    return m_scheme;
}

wxInputHandler *
wxHaikuTheme::GetInputHandler(const wxString& control,
                              wxInputConsumer *consumer)
{
    wxInputHandler *handler = NULL;
    int n = m_handlerNames.Index(control);
    if ( n == wxNOT_FOUND )
    {
        static wxHaikuInputHandler s_handlerDef;

        wxInputHandler * const
          handlerStd = consumer->DoGetStdInputHandler(&s_handlerDef);

#if wxUSE_CHECKBOX
        if ( control == wxINP_HANDLER_CHECKBOX )
        {
            static wxHaikuCheckboxInputHandler s_handler(handlerStd);

            handler = &s_handler;
        }
        else
#endif // wxUSE_CHECKBOX
#if wxUSE_SCROLLBAR
        if ( control == wxINP_HANDLER_SCROLLBAR )
        {
            // as BScrollBar: the thumb follows the mouse wherever it goes
            // while it is dragged
            static wxStdScrollBarInputHandler
                s_handler(GetRenderer(), handlerStd);

            handler = &s_handler;
        }
        else
#endif // wxUSE_SCROLLBAR
        {
            // the standard handler of the control: the windows have the
            // decorations and the resize corner of the system
            handler = handlerStd;
        }

        n = m_handlerNames.Add(control);
        m_handlers.Insert(handler, n);
    }
    else // we already have it
    {
        handler = m_handlers[n];
    }

    return handler;
}

// ============================================================================
// wxHaikuColourScheme
// ============================================================================

wxColour wxHaikuColourScheme::GetBackground(wxWindow *win) const
{
    // the colour the application gave the window
    if ( win->UseBgCol() )
        return win->GetBackgroundColour();

#if wxUSE_TEXTCTRL
    if ( wxDynamicCast(win, wxTextCtrl) )
    {
        return UIColour(win->IsEnabled() ? B_DOCUMENT_BACKGROUND_COLOR
                                         : B_PANEL_BACKGROUND_COLOR);
    }
#endif // wxUSE_TEXTCTRL

#if wxUSE_LISTBOX
    if ( wxDynamicCast(win, wxListBox) )
        return UIColour(B_LIST_BACKGROUND_COLOR);
#endif // wxUSE_LISTBOX

    if ( AsButton(win) )
        return UIColour(B_CONTROL_BACKGROUND_COLOR);

#if wxUSE_MENUS
    if ( wxDynamicCast(win, wxMenuBar) )
        return UIColour(B_MENU_BACKGROUND_COLOR);
#endif // wxUSE_MENUS

#if wxUSE_POPUPWIN
    // menus are the only transient popups without a colour of their own
    if ( wxDynamicCast(win, wxPopupTransientWindow) )
        return UIColour(B_MENU_BACKGROUND_COLOR);
#endif // wxUSE_POPUPWIN

    // Everything else shows what is behind it, as the views of the system
    // which adopt the colour of their parent do: a control on a white
    // dialog is not a grey rectangle.
    if ( !win->IsTopLevel() )
    {
        wxWindow * const parent = win->GetParent();
        if ( parent )
            return GetBackground(parent);
    }

    return UIColour(B_PANEL_BACKGROUND_COLOR);
}

wxColour wxHaikuColourScheme::Get(wxHaikuColourScheme::StdColour col) const
{
    const rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);

    rgb_color c = panel;
    switch ( col )
    {
        case WINDOW:
            c = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
            break;

        case CONTROL:
        case CONTROL_PRESSED:
        case CONTROL_CURRENT:
        case FRAME:
        case CONTROL_TEXT_DISABLED_SHADOW:
            break;

        case CONTROL_TEXT:
            c = ui_color(B_PANEL_TEXT_COLOR);
            break;

        case CONTROL_TEXT_DISABLED:
            c = disable_color(ui_color(B_PANEL_TEXT_COLOR), panel);
            break;

        case SCROLLBAR:
            c = tint_color(panel, B_DARKEN_1_TINT);
            break;

        case SCROLLBAR_PRESSED:
            c = tint_color(panel, B_DARKEN_2_TINT);
            break;

        case HIGHLIGHT:
            c = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
            break;

        case HIGHLIGHT_TEXT:
            c = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
            break;

        case SHADOW_DARK:
            c = tint_color(panel, B_DARKEN_3_TINT);
            break;

        case SHADOW_HIGHLIGHT:
            c = tint_color(panel, B_LIGHTEN_MAX_TINT);
            break;

        case SHADOW_IN:
            c = tint_color(panel, B_LIGHTEN_1_TINT);
            break;

        case SHADOW_OUT:
            c = tint_color(panel, B_DARKEN_2_TINT);
            break;

        case TITLEBAR:
            c = ui_color(B_WINDOW_INACTIVE_TAB_COLOR);
            break;

        case TITLEBAR_ACTIVE:
            c = ui_color(B_WINDOW_TAB_COLOR);
            break;

        case TITLEBAR_TEXT:
            c = ui_color(B_WINDOW_INACTIVE_TEXT_COLOR);
            break;

        case TITLEBAR_ACTIVE_TEXT:
            c = ui_color(B_WINDOW_TEXT_COLOR);
            break;

        case GAUGE:
            c = ui_color(B_STATUS_BAR_COLOR);
            break;

        case DESKTOP:
            c = ui_color(B_DESKTOP_COLOR);
            break;

        case MAX:
        default:
            wxFAIL_MSG(wxT("invalid standard colour"));
    }

    return wxColour(c.red, c.green, c.blue);
}

// ============================================================================
// wxHaikuRenderer
// ============================================================================

wxHaikuRenderer::wxHaikuRenderer(const wxHaikuColourScheme *scheme)
               : wxStdRenderer(scheme)
{
    // The pens wxStdRenderer keeps are not used: its drawing functions which
    // need them are all overridden here, with the colours of the moment.
}

// ----------------------------------------------------------------------------
// backgrounds
// ----------------------------------------------------------------------------

void wxHaikuRenderer::DrawBackground(wxDC& dc,
                                     const wxColour& col,
                                     const wxRect& rect,
                                     int WXUNUSED(flags),
                                     wxWindow *window)
{
    wxWindow * const win = window ? window : dc.GetWindow();

#if wxUSE_MENUS
    // the menu bar has a gradient of its own: always the whole of it, the
    // DC clips it to what is being painted
    if ( win && win == dc.GetWindow() && wxDynamicCast(win, wxMenuBar)
            && !win->UseBgCol() )
    {
        if ( DrawMenuBarBackground(dc, wxRect(win->GetSize())) )
            return;
    }
#endif // wxUSE_MENUS

    wxColour colBg = col;
    if ( !colBg.IsOk() )
        colBg = win ? Scheme()->GetBackground(win) : Panel();

    wxHaikuLook::Fill(dc, rect, colBg);

#if wxUSE_STATUSBAR
    // a line between the window and its status bar
    if ( win && win == dc.GetWindow() && wxDynamicCast(win, wxStatusBar) )
    {
        dc.SetPen(LookTint(colBg, B_DARKEN_2_TINT));
        dc.DrawLine(0, 0, win->GetSize().x, 0);
    }
#endif // wxUSE_STATUSBAR
}

void wxHaikuRenderer::DrawButtonSurface(wxDC& dc,
                                        const wxColour& col,
                                        const wxRect& rect,
                                        int flags)
{
    wxWindow * const win = dc.GetWindow();
    const wxColour base = col.IsOk() ? col
                                     : UIColour(B_CONTROL_BACKGROUND_COLOR);

    // a button without border is a flat surface, darker while pressed
    if ( IsFlatButton(win) )
    {
        wxHaikuLook::Fill(dc, rect,
            flags & wxCONTROL_PRESSED ? LookTint(base, B_DARKEN_1_TINT)
                                      : base);
        return;
    }

    // the whole button: its frame is not a border of the window, the
    // corners show the parent
    const wxColour outside = ParentBackground(dc);
    if ( !wxHaikuLook::DrawButton(dc, rect, flags, base, outside) )
    {
        wxHaikuLook::Fill(dc, rect, base);
        dc.SetPen(wxHaikuLook::FrameColour(base));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(rect);
    }
}

void wxHaikuRenderer::DrawButtonBorder(wxDC& WXUNUSED(dc),
                                       const wxRect& rect,
                                       int flags,
                                       wxRect *rectIn)
{
    // drawn by DrawButtonSurface() with the surface: only the rectangle
    // inside the frame is needed here
    if ( rectIn )
    {
        *rectIn = rect;
        if ( be_control_look )
        {
            float l, t, r, b;
            be_control_look->GetInsets(BControlLook::B_BUTTON_FRAME,
                BControlLook::B_BUTTON_BACKGROUND,
                flags & wxCONTROL_ISDEFAULT ? BControlLook::B_DEFAULT_BUTTON
                                            : 0,
                l, t, r, b);

            // the client area of the button already is inside the border of
            // the window, which stands for one pixel of the frame
            rectIn->x += (int)l - 1;
            rectIn->y += (int)t - 1;
            rectIn->width -= (int)(l + r) - 2;
            rectIn->height -= (int)(t + b) - 2;
        }
    }
}

// ----------------------------------------------------------------------------
// labels
// ----------------------------------------------------------------------------

void wxHaikuRenderer::DrawFocusRect(wxWindow *WXUNUSED(win),
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int WXUNUSED(flags))
{
    wxHaikuLook::DrawFocusFrame(dc, rect);
}

void wxHaikuRenderer::DoDrawLabel(wxDC& dc,
                                  const wxString& label,
                                  const wxBitmap& image,
                                  const wxRect& rect,
                                  int alignment,
                                  wxRect *rectBounds,
                                  const wxColour& colour)
{
    wxDCTextColourChanger changer(dc, colour);
    dc.DrawLabel(label, image, rect, alignment, -1, rectBounds);
}

void wxHaikuRenderer::DrawLabel(wxDC& dc,
                                const wxString& label,
                                const wxRect& rect,
                                int flags,
                                int alignment,
                                int WXUNUSED(indexAccel),
                                wxRect *rectBounds)
{
    const wxColour bg = WindowBackground(dc);
    DoDrawLabel(dc, label, wxNullBitmap, rect, alignment, rectBounds,
                TextColour(dc, B_PANEL_TEXT_COLOR, flags, bg));
}

void wxHaikuRenderer::DrawButtonLabel(wxDC& dc,
                                      const wxString& label,
                                      const wxBitmap& image,
                                      const wxRect& rect,
                                      int flags,
                                      int alignment,
                                      int WXUNUSED(indexAccel),
                                      wxRect *rectBounds)
{
    const wxColour bg = WindowBackground(dc);
    DoDrawLabel(dc, label, image, rect, alignment, rectBounds,
                TextColour(dc, B_CONTROL_TEXT_COLOR, flags, bg));
}

// ----------------------------------------------------------------------------
// borders
// ----------------------------------------------------------------------------

wxRect wxHaikuRenderer::GetBorderDimensions(wxBorder border) const
{
    wxCoord width;
    switch ( border )
    {
        case wxBORDER_SIMPLE:
        case wxBORDER_STATIC:
            width = 1;
            break;

        case wxBORDER_RAISED:
        case wxBORDER_SUNKEN:
        case wxBORDER_THEME:
            width = wxHaikuLook::TextFrameWidth();
            break;

        default:
            wxFAIL_MSG(wxT("unknown border type"));
            wxFALLTHROUGH;

        case wxBORDER_DEFAULT:
        case wxBORDER_NONE:
            width = 0;
            break;
    }

    return wxRect(width, width, width, width);
}

void wxHaikuRenderer::DrawBorder(wxDC& dc,
                                 wxBorder border,
                                 const wxRect& rectTotal,
                                 int flags,
                                 wxRect *rectIn)
{
    wxWindow * const win = dc.GetWindow();

    const wxRect dim = GetBorderDimensions(border);
    wxRect rectInside = rectTotal;
    rectInside.x += dim.x;
    rectInside.y += dim.y;
    rectInside.width -= dim.x + dim.width;
    rectInside.height -= dim.y + dim.height;
    if ( rectIn )
        *rectIn = rectInside;

    // The border of a button is a part of the button the surface is drawn
    // with (DrawButtonSurface()).
    if ( AsButton(win) )
        return;

    const wxColour base = win ? Scheme()->GetParentBackground(win) : Panel();

    switch ( border )
    {
        case wxBORDER_SUNKEN:
        case wxBORDER_THEME:
            // the frame of scroll views and text controls: in the keyboard
            // navigation colour around the focus view
            if ( !wxHaikuLook::DrawTextFrame(dc, rectTotal, flags, base) )
            {
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.SetPen(base);
                dc.DrawRectangle(rectTotal);
                dc.SetPen(wxHaikuLook::FrameColour(base));
                dc.DrawRectangle(rectInside.Inflate(1));
            }
            break;

        case wxBORDER_STATIC:
        case wxBORDER_SIMPLE:
            // a plain border
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxHaikuLook::FrameColour(base));
            dc.DrawRectangle(rectTotal);
            break;

        case wxBORDER_RAISED:
#if wxUSE_NOTEBOOK
            // wxNotebook draws the border of its page area with this
            if ( win && rectTotal.GetSize() != win->GetSize() )
            {
                wxNotebook * const notebook = wxDynamicCast(win, wxNotebook);
                if ( notebook )
                {
                    DrawNotebookFrame(dc, notebook, rectTotal);
                    break;
                }
            }
#endif // wxUSE_NOTEBOOK

            // menus and raised panels: a dark frame with a bevel inside
            {
                const wxColour bg = win ? Scheme()->GetBackground(win)
                                        : Panel();
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.SetPen(LookTint(Panel(), B_DARKEN_2_TINT));
                dc.DrawRectangle(rectTotal);

                wxRect rectBevel = rectTotal;
                rectBevel.Deflate(1);
                if ( !wxHaikuLook::DrawRaisedBorder(dc, rectBevel, flags, bg) )
                {
                    dc.SetPen(LookTint(bg, B_LIGHTEN_1_TINT));
                    dc.DrawRectangle(rectBevel);
                }
            }
            break;

        default:
            break;
    }
}

void wxHaikuRenderer::DrawTextBorder(wxDC& dc,
                                     wxBorder border,
                                     const wxRect& rect,
                                     int flags,
                                     wxRect *rectIn)
{
    // any border of a text control is the frame of a BTextControl
    if ( border == wxBORDER_SIMPLE || border == wxBORDER_STATIC )
        border = wxBORDER_SUNKEN;

    DrawBorder(dc, border, rect, flags, rectIn);
}

// ----------------------------------------------------------------------------
// lines and boxes
// ----------------------------------------------------------------------------

void
wxHaikuRenderer::DrawHorizontalLine(wxDC& dc, wxCoord y, wxCoord x1, wxCoord x2)
{
    // a separator: a dark line over a light one
    const wxColour base = WindowBackground(dc);

    dc.SetPen(LookTint(base, 1.30f));
    dc.DrawLine(x1, y, x2 + 1, y);

    dc.SetPen(LookTint(base, 0.8f));
    dc.DrawLine(x1, y + 1, x2 + 1, y + 1);
}

void
wxHaikuRenderer::DrawVerticalLine(wxDC& dc, wxCoord x, wxCoord y1, wxCoord y2)
{
    const wxColour base = WindowBackground(dc);

    dc.SetPen(LookTint(base, 1.30f));
    dc.DrawLine(x, y1, x, y2 + 1);

    dc.SetPen(LookTint(base, 0.8f));
    dc.DrawLine(x + 1, y1, x + 1, y2 + 1);
}

void wxHaikuRenderer::DrawFrame(wxDC& dc,
                                const wxString& label,
                                const wxRect& rect,
                                int flags,
                                int WXUNUSED(alignment),
                                int WXUNUSED(indexAccel))
{
    // a BBox: the frame starts at the middle of the label, which interrupts
    // it with a gap of 4 pixels on both sides
    const wxColour base = WindowBackground(dc);

    wxRect rectFrame = rect;
    wxRect rectLabel;
    if ( !label.empty() )
    {
        wxCoord width, height;
        dc.GetTextExtent(label, &width, &height);

        rectFrame.y += height / 2;
        rectFrame.height -= height / 2;

        rectLabel = wxRect(rect.x + Scaled(6), rect.y,
                           wxMin(width + 2*Scaled(4),
                                 rect.width - 2*Scaled(6)),
                           height);
    }

    {
        // everything but the label
        wxRegion region(wxRect(dc.LogicalToDeviceX(rect.x),
                               dc.LogicalToDeviceY(rect.y),
                               dc.LogicalToDeviceXRel(rect.width),
                               dc.LogicalToDeviceYRel(rect.height)));
        if ( !rectLabel.IsEmpty() )
        {
            region.Subtract(wxRect(dc.LogicalToDeviceX(rectLabel.x),
                                   dc.LogicalToDeviceY(rectLabel.y),
                                   dc.LogicalToDeviceXRel(rectLabel.width),
                                   dc.LogicalToDeviceYRel(rectLabel.height)));
        }

        wxDCClipper clip(dc, region);
        if ( !wxHaikuLook::DrawGroupFrame(dc, rectFrame, base) )
        {
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxHaikuLook::FrameColour(base));
            dc.DrawRectangle(wxRect(rectFrame).Deflate(1));
        }
    }

    if ( !label.empty() )
    {
        wxRect rectText = rectLabel;
        rectText.x += Scaled(4);
        rectText.width -= 2*Scaled(4);

        wxDCClipper clip(dc, rectLabel);
        DoDrawLabel(dc, label, wxNullBitmap, rectText,
                    wxALIGN_LEFT | wxALIGN_TOP, NULL,
                    TextColour(dc, B_PANEL_TEXT_COLOR, flags, base));
    }
}

// ----------------------------------------------------------------------------
// check boxes, radio buttons, list items
// ----------------------------------------------------------------------------

void wxHaikuRenderer::DrawMarkAndLabel(wxDC& dc,
                                       bool radio,
                                       const wxString& label,
                                       const wxBitmap& bitmap,
                                       const wxRect& rect,
                                       int flags,
                                       wxAlignment align)
{
    const wxColour base = WindowBackground(dc);

    const wxSize sizeMark = bitmap.IsOk() ? bitmap.GetSize()
                                          : GetCheckBitmapSize();

    wxRect rectMark(rect.x, rect.y + (rect.height - sizeMark.y) / 2,
                    sizeMark.x, sizeMark.y);
    wxRect rectLabel = rect;

    const int spacing = wxHaikuLook::LabelSpacing();
    if ( align == wxALIGN_RIGHT )
    {
        rectMark.x = rect.GetRight() - sizeMark.x + 1;
        rectLabel.width -= sizeMark.x + spacing;
    }
    else
    {
        rectLabel.x += sizeMark.x + spacing;
        rectLabel.width -= sizeMark.x + spacing;
    }

    if ( bitmap.IsOk() )
    {
        // the application's own bitmaps
        dc.DrawBitmap(bitmap, rectMark.x, rectMark.y, true /* use mask */);
    }
    else
    {
        const bool ok = radio
            ? wxHaikuLook::DrawRadioButton(dc, rectMark, flags, base)
            : wxHaikuLook::DrawCheckBox(dc, rectMark, flags, base);
        if ( !ok )
        {
            // any other DC: a plain mark
            dc.SetPen(wxHaikuLook::FrameColour(base));
            dc.SetBrush(UIColour(B_DOCUMENT_BACKGROUND_COLOR));
            if ( radio )
                dc.DrawEllipse(rectMark);
            else
                dc.DrawRectangle(rectMark);

            if ( flags & wxCONTROL_CHECKED )
            {
                dc.SetBrush(UIColour(B_CONTROL_MARK_COLOR));
                dc.SetPen(*wxTRANSPARENT_PEN);
                wxRect rectIn = rectMark;
                rectIn.Deflate(sizeMark.x / 4);
                if ( radio )
                    dc.DrawEllipse(rectIn);
                else
                    dc.DrawRectangle(rectIn);
            }
        }
    }

    if ( !label.empty() )
    {
        DoDrawLabel(dc, label, wxNullBitmap, rectLabel,
                    wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, NULL,
                    TextColour(dc, B_PANEL_TEXT_COLOR, flags, base));
    }
}

void wxHaikuRenderer::DrawCheckButton(wxDC& dc,
                                      const wxString& label,
                                      const wxBitmap& bitmap,
                                      const wxRect& rect,
                                      int flags,
                                      wxAlignment align,
                                      int WXUNUSED(indexAccel))
{
    DrawMarkAndLabel(dc, false, label, bitmap, rect, flags, align);
}

void wxHaikuRenderer::DrawRadioButton(wxDC& dc,
                                      const wxString& label,
                                      const wxBitmap& bitmap,
                                      const wxRect& rect,
                                      int flags,
                                      wxAlignment align,
                                      int WXUNUSED(indexAccel))
{
    DrawMarkAndLabel(dc, true, label, bitmap, rect, flags, align);
}

void wxHaikuRenderer::DrawCheckItemBitmap(wxDC& dc,
                                          const wxBitmap& bitmap,
                                          const wxRect& rect,
                                          int flags)
{
    // the mark of an item of a check list box: its state only, the item
    // shows the selection and the focus
    wxRect rectMark = rect;
    rectMark.x += GetCheckItemMargin();
    DrawMarkAndLabel(dc, false, wxString(), bitmap, rectMark,
                     flags & (wxCONTROL_CHECKED | wxCONTROL_DISABLED),
                     wxALIGN_LEFT);
}

void wxHaikuRenderer::DrawItem(wxDC& dc,
                               const wxString& label,
                               const wxRect& rect,
                               int flags)
{
    wxColour colText;
    if ( flags & wxCONTROL_SELECTED )
    {
        wxHaikuLook::Fill(dc, rect, wxHaikuLook::SelectionColour(true));
        colText = wxHaikuLook::SelectionTextColour(true);
    }
    else
    {
        colText = TextColour(dc, B_LIST_ITEM_TEXT_COLOR, flags,
                             WindowBackground(dc));
    }

    wxRect rectText = rect;
    rectText.x += Scaled(4);
    rectText.width -= 2*Scaled(4);
    DoDrawLabel(dc, label, wxNullBitmap, rectText,
                wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, NULL, colText);

    // the current item of a list with the focus, if it is not the selected
    // one anyway
    if ( (flags & wxCONTROL_FOCUSED) && !(flags & wxCONTROL_SELECTED) )
        wxHaikuLook::DrawFocusFrame(dc, rect);
}

#if wxUSE_TEXTCTRL

void wxHaikuRenderer::DrawTextLine(wxDC& dc,
                                   const wxString& text,
                                   const wxRect& rect,
                                   int selStart,
                                   int selEnd,
                                   int flags)
{
    // the text of a disabled control is faded
    wxDCTextColourChanger changer(dc);
    if ( flags & wxCONTROL_DISABLED )
    {
        changer.Set(wxHaikuLook::DisabledText(dc.GetTextForeground(),
                                              WindowBackground(dc)));
    }

    wxStdRenderer::DrawTextLine(dc, text, rect, selStart, selEnd, flags);
}

#endif // wxUSE_TEXTCTRL

// ----------------------------------------------------------------------------
// arrows and scroll bars
// ----------------------------------------------------------------------------

void wxHaikuRenderer::DrawArrow(wxDC& dc,
                                wxDirection dir,
                                const wxRect& rect,
                                int flags)
{
    // the buttons of a spin button: as the ones of a BSpinner, a button
    // surface in a thin frame
    const wxColour base = UIColour(B_CONTROL_BACKGROUND_COLOR);

    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxHaikuLook::FrameColour(Panel()));
    dc.DrawRectangle(rect);

    wxRect rectIn = rect;
    rectIn.Deflate(1);
    if ( !wxHaikuLook::DrawButtonBackground(dc, rectIn, flags, base) )
        wxHaikuLook::Fill(dc, rectIn, base);

    wxColour colour = UIColour(B_CONTROL_TEXT_COLOR);
    if ( flags & wxCONTROL_DISABLED )
        colour = wxHaikuLook::DisabledText(colour, base);

    // the arrow is stroked in the middle third of a square
    const int side = wxMax(rect.width, rect.height);
    wxRect rectArrow(0, 0, side, side);
    wxHaikuLook::DrawArrow(dc, rectArrow.CentreIn(rect), dir, colour);
}

uint32 wxHaikuRenderer::ScrollbarFlags(wxDC& dc, int flags)
{
    uint32 f = 0;

    // a scroll bar with nothing to scroll is drawn disabled, as BScrollBar
    bool scrollable = true;
#if wxUSE_SCROLLBAR
    wxScrollBar * const
        scrollbar = wxDynamicCast(dc.GetWindow(), wxScrollBar);
    if ( scrollbar )
        scrollable = scrollbar->GetRange() > scrollbar->GetThumbSize();
#endif // wxUSE_SCROLLBAR

    if ( (flags & wxCONTROL_DISABLED) || !scrollable )
        f |= BControlLook::B_DISABLED;
    else
        f |= BControlLook::B_PARTIALLY_ACTIVATED;

    if ( flags & wxCONTROL_CURRENT )
        f |= BControlLook::B_HOVER;

    return f;
}

wxRect wxHaikuRenderer::DrawScrollbarFrame(wxDC& dc,
                                           wxOrientation orient,
                                           const wxRect& rect,
                                           bool first,
                                           bool last)
{
    // BScrollBar has a frame of one pixel. A scroll bar of a window lies
    // inside the frame of that window, where a BScrollView puts its scroll
    // bars over the frame: it only has the line towards the contents and,
    // if there is a second scroll bar, the one towards the corner.
    bool own = true,        // not the scroll bar of a window
         corner = false;
#if wxUSE_SCROLLBAR
    wxWindow * const win = dc.GetWindow();
    wxWindow * const parent = win ? win->GetParent() : NULL;
    if ( parent )
    {
        wxScrollBar * const vert = parent->GetScrollbar(wxVERTICAL);
        wxScrollBar * const horz = parent->GetScrollbar(wxHORIZONTAL);
        if ( win == vert || win == horz )
        {
            own = false;
            corner = vert && horz;
        }
    }
#endif // wxUSE_SCROLLBAR

    const bool vertical = orient == wxVERTICAL;

    wxRect r = rect;
    dc.SetPen(LookTint(Panel(), B_DARKEN_2_TINT));

    // the line towards the contents
    if ( vertical )
    {
        dc.DrawLine(r.x, r.y, r.x, r.GetBottom() + 1);
        r.x++;
        r.width--;
    }
    else
    {
        dc.DrawLine(r.x, r.y, r.GetRight() + 1, r.y);
        r.y++;
        r.height--;
    }

    // the outer one
    if ( own )
    {
        if ( vertical )
        {
            dc.DrawLine(r.GetRight(), r.y, r.GetRight(), r.GetBottom() + 1);
            r.width--;
        }
        else
        {
            dc.DrawLine(r.x, r.GetBottom(), r.GetRight() + 1, r.GetBottom());
            r.height--;
        }
    }

    // the ends
    if ( first && own )
    {
        if ( vertical )
        {
            dc.DrawLine(r.x, r.y, r.GetRight() + 1, r.y);
            r.y++;
            r.height--;
        }
        else
        {
            dc.DrawLine(r.x, r.y, r.x, r.GetBottom() + 1);
            r.x++;
            r.width--;
        }
    }

    if ( last && (own || corner) )
    {
        if ( vertical )
        {
            dc.DrawLine(r.x, r.GetBottom(), r.GetRight() + 1, r.GetBottom());
            r.height--;
        }
        else
        {
            dc.DrawLine(r.GetRight(), r.y, r.GetRight(), r.GetBottom() + 1);
            r.width--;
        }
    }

    return r;
}

void wxHaikuRenderer::DrawScrollbarArrow(wxDC& dc,
                                         wxDirection dir,
                                         const wxRect& rect,
                                         int flags)
{
    const wxOrientation
        orient = dir == wxUP || dir == wxDOWN ? wxVERTICAL : wxHORIZONTAL;
    const bool first = dir == wxUP || dir == wxLEFT;

    const wxRect r = DrawScrollbarFrame(dc, orient, rect, first, !first);
    if ( r.IsEmpty() )
        return;

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
    {
        wxHaikuLook::Fill(dc, r, Panel());
        return;
    }

    uint32 direction;
    switch ( dir )
    {
        case wxLEFT:    direction = BControlLook::B_LEFT_ARROW; break;
        case wxRIGHT:   direction = BControlLook::B_RIGHT_ARROW; break;
        case wxUP:      direction = BControlLook::B_UP_ARROW; break;
        default:        direction = BControlLook::B_DOWN_ARROW; break;
    }

    const bool down = (flags & wxCONTROL_PRESSED) != 0;
    const rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
    p.SetBackground(base);

    const BRect br = p.Rect(r);
    be_control_look->DrawScrollBarButton(p.View(), br, br, base,
        ui_color(B_PANEL_TEXT_COLOR),
        ScrollbarFlags(dc, flags) | (down ? BControlLook::B_ACTIVATED : 0),
        direction, orient == wxVERTICAL ? B_VERTICAL : B_HORIZONTAL, down);
}

void wxHaikuRenderer::DrawScrollbarShaft(wxDC& dc,
                                         wxOrientation orient,
                                         const wxRect& rect,
                                         int flags)
{
    const wxRect r = DrawScrollbarFrame(dc, orient, rect, false, false);
    if ( r.IsEmpty() )
        return;

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
    {
        wxHaikuLook::Fill(dc, r, LookTint(Panel(), B_DARKEN_1_TINT));
        return;
    }

    const rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
    p.SetBackground(base);

    BRect br = p.Rect(r);
    be_control_look->DrawScrollBarBackground(p.View(), br, br, base,
        ScrollbarFlags(dc, flags & ~wxCONTROL_CURRENT),
        orient == wxVERTICAL ? B_VERTICAL : B_HORIZONTAL);
}

void wxHaikuRenderer::DrawScrollbarThumb(wxDC& dc,
                                         wxOrientation orient,
                                         const wxRect& rect,
                                         int flags)
{
    const wxRect r = DrawScrollbarFrame(dc, orient, rect, false, false);
    if ( r.IsEmpty() )
        return;

    const uint32 f = ScrollbarFlags(dc, flags);

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
    {
        wxHaikuLook::Fill(dc, r, UIColour(B_SCROLL_BAR_THUMB_COLOR));
        return;
    }

    const rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
    p.SetBackground(base);

    BRect br = p.Rect(r);
    if ( f & BControlLook::B_DISABLED )
    {
        // no thumb where there is nothing to scroll
        be_control_look->DrawScrollBarBackground(p.View(), br, br, base, f,
            orient == wxVERTICAL ? B_VERTICAL : B_HORIZONTAL);
        return;
    }

    be_control_look->DrawScrollBarThumb(p.View(), br, br,
        ui_color(B_SCROLL_BAR_THUMB_COLOR), f,
        orient == wxVERTICAL ? B_VERTICAL : B_HORIZONTAL,
        BControlLook::B_KNOB_NONE);
}

void wxHaikuRenderer::DrawScrollCorner(wxDC& dc, const wxRect& rect)
{
    wxHaikuLook::Fill(dc, rect, Panel());
}

// ----------------------------------------------------------------------------
// tool bars
// ----------------------------------------------------------------------------

#if wxUSE_TOOLBAR

void wxHaikuRenderer::DrawToolBarButton(wxDC& dc,
                                        const wxString& label,
                                        const wxBitmap& bitmap,
                                        const wxRect& rectOrig,
                                        int flags,
                                        long style,
                                        int tbarStyle)
{
    if ( style == wxTOOL_STYLE_BUTTON )
    {
        // as the buttons of a BToolBar: flat until the mouse is over them
        // or they are down
        const wxColour bg = WindowBackground(dc);

        int flagsButton = flags & (wxCONTROL_PRESSED | wxCONTROL_CURRENT |
                                   wxCONTROL_DISABLED);
        if ( flags & wxCONTROL_CHECKED )
            flagsButton |= wxCONTROL_PRESSED;

        if ( (flagsButton & (wxCONTROL_PRESSED | wxCONTROL_CURRENT))
                && !(flags & wxCONTROL_DISABLED) )
        {
            wxHaikuLook::DrawButton(dc, rectOrig, flagsButton,
                                    UIColour(B_CONTROL_BACKGROUND_COLOR), bg);
        }

        wxRect rect = rectOrig;
        rect.Deflate(Scaled(2));

        if ( tbarStyle & wxTB_TEXT )
        {
            DoDrawLabel(dc, label, bitmap, rect,
                        tbarStyle & wxTB_HORIZONTAL
                            ? wxALIGN_CENTRE
                            : wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL,
                        NULL,
                        TextColour(dc, B_PANEL_TEXT_COLOR, flags, bg));
        }
        else if ( bitmap.IsOk() )
        {
            const int x = rect.x + (rect.width - bitmap.GetWidth()) / 2,
                      y = rect.y + (rect.height - bitmap.GetHeight()) / 2;
            dc.DrawBitmap(bitmap, x, y, true /* use mask */);
        }
    }
    else if ( style == wxTOOL_STYLE_SEPARATOR )
    {
        // the two lines of a separator, with a margin at their ends
        const int margin = Scaled(3);
        if ( rectOrig.height > rectOrig.width )
        {
            DrawVerticalLine(dc, rectOrig.x + rectOrig.width / 2 - 1,
                             rectOrig.y + margin,
                             rectOrig.GetBottom() - margin);
        }
        else
        {
            DrawHorizontalLine(dc, rectOrig.y + rectOrig.height / 2 - 1,
                               rectOrig.x + margin,
                               rectOrig.GetRight() - margin);
        }
    }
    // don't draw wxTOOL_STYLE_CONTROL
}

#endif // wxUSE_TOOLBAR

// ----------------------------------------------------------------------------
// notebook: BTabView
// ----------------------------------------------------------------------------

#if wxUSE_NOTEBOOK

namespace
{

// the side of a tab view for the orientation of the tabs of a notebook
uint32 TabSide(wxDirection dir)
{
    switch ( dir )
    {
        case wxBOTTOM:  return BControlLook::B_BOTTOM_BORDER;
        case wxLEFT:    return BControlLook::B_LEFT_BORDER;
        case wxRIGHT:   return BControlLook::B_RIGHT_BORDER;
        default:        return BControlLook::B_TOP_BORDER;
    }
}

wxDirection NotebookTabDirection(const wxNotebook *notebook)
{
    const long style = notebook->GetWindowStyle();
    if ( style & wxBK_BOTTOM )
        return wxBOTTOM;
    if ( style & wxBK_LEFT )
        return wxLEFT;
    if ( style & wxBK_RIGHT )
        return wxRIGHT;
    return wxTOP;
}

} // anonymous namespace

void wxHaikuRenderer::DrawNotebookFrame(wxDC& dc,
                                        wxNotebook *notebook,
                                        const wxRect& rectPage)
{
    const wxColour base = Scheme()->GetBackground(notebook);
    const wxDirection dir = NotebookTabDirection(notebook);
    const wxSize size = notebook->GetClientSize();

    // The row of the tabs: an inactive tab frame behind all of them, which
    // shares its last line with the box of the pages.
    wxRect rectTabs;
    uint32 borders = BControlLook::B_ALL_BORDERS;
    switch ( dir )
    {
        case wxBOTTOM:
            rectTabs = wxRect(0, rectPage.GetBottom(),
                              size.x, size.y - rectPage.GetBottom());
            borders &= ~BControlLook::B_BOTTOM_BORDER;
            break;

        case wxLEFT:
            rectTabs = wxRect(0, 0, rectPage.x + 1, size.y);
            borders &= ~BControlLook::B_LEFT_BORDER;
            break;

        case wxRIGHT:
            rectTabs = wxRect(rectPage.GetRight(), 0,
                              size.x - rectPage.GetRight(), size.y);
            borders &= ~BControlLook::B_RIGHT_BORDER;
            break;

        default:
            rectTabs = wxRect(0, 0, size.x, rectPage.y + 1);
            borders &= ~BControlLook::B_TOP_BORDER;
            break;
    }

    bool drawn = false;
    if ( notebook->GetPageCount() == 0 )
    {
        // no tabs: just a box
        borders = BControlLook::B_ALL_BORDERS;
    }
    else
    {
        wxHaikuLookPainter p(dc);
        if ( p.IsOk() )
        {
            const rgb_color colBase = wxHaikuColour(base);
            p.SetBackground(colBase);

            BRect r = p.Rect(rectTabs);
            be_control_look->DrawTabFrame(p.View(), r, r, colBase, 0,
                BControlLook::B_ALL_BORDERS, B_FANCY_BORDER, TabSide(dir));
            drawn = true;
        }
    }

    if ( !wxHaikuLook::DrawGroupFrame(dc, rectPage, base, borders) || !drawn )
    {
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxHaikuLook::FrameColour(base));
        dc.DrawRectangle(wxRect(rectPage).Deflate(1));
    }
}

void wxHaikuRenderer::DrawTab(wxDC& dc,
                              const wxRect& rectOrig,
                              wxDirection dir,
                              const wxString& label,
                              const wxBitmap& bitmap,
                              int flags,
                              int WXUNUSED(indexAccel))
{
    const bool selected = (flags & wxCONTROL_SELECTED) != 0;
    const bool vertical = dir == wxLEFT || dir == wxRIGHT;
    const int indent = GetTabIndent().y;

    // The frame of the tab as BTabView::TabFrame() has it: over the whole
    // height of the row of the tabs, and one more line which is the first
    // of the box of the pages. HaikuControlLook lowers inactive tabs by
    // the indent itself.
    wxRect rect = rectOrig;
    switch ( dir )
    {
        case wxBOTTOM:
            rect.y--;
            rect.height += indent + 1;
            break;

        case wxLEFT:
            rect.x -= indent;
            rect.width += indent + 1;
            break;

        case wxRIGHT:
            rect.x--;
            rect.width += indent + 1;
            break;

        default:
            rect.y -= indent;
            rect.height += indent + 1;
            break;
    }

    const wxColour base = WindowBackground(dc);

    // the rectangle of the label: inside the frame of an active tab, and
    // lowered with an inactive one
    wxRect rectLabel = rect;
    if ( !selected )
    {
        switch ( dir )
        {
            case wxBOTTOM:  rectLabel.height -= indent; break;
            case wxLEFT:    rectLabel.x += indent; rectLabel.width -= indent; break;
            case wxRIGHT:   rectLabel.width -= indent; break;
            default:        rectLabel.y += indent; rectLabel.height -= indent; break;
        }
    }

    {
        wxHaikuLookPainter p(dc);
        if ( p.IsOk() )
        {
            const rgb_color colBase = wxHaikuColour(base);
            p.SetBackground(colBase);

            // the sides along the row: every tab draws the separator from
            // its neighbour
            const uint32 borders = vertical
                ? BControlLook::B_LEFT_BORDER | BControlLook::B_RIGHT_BORDER
                : BControlLook::B_TOP_BORDER | BControlLook::B_BOTTOM_BORDER;

            BRect r = p.Rect(rect);
            if ( selected )
            {
                be_control_look->DrawActiveTab(p.View(), r, r, colBase, 0,
                    borders, TabSide(dir));
            }
            else
            {
                be_control_look->DrawInactiveTab(p.View(), r, r, colBase, 0,
                    borders, TabSide(dir));
            }
        }
        else
        {
            wxHaikuLook::Fill(dc, rectLabel,
                selected ? base : LookTint(base, B_DARKEN_1_TINT));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxHaikuLook::FrameColour(base));
            dc.DrawRectangle(rectLabel);
        }
    }

    const wxColour colText = TextColour(dc, B_PANEL_TEXT_COLOR, flags, base);

    if ( vertical )
    {
        // the label runs along the tab: upwards at the left, downwards at
        // the right
        wxCoord width, height;
        dc.GetTextExtent(label, &width, &height);

        wxDCTextColourChanger changer(dc, colText);
        if ( dir == wxLEFT )
        {
            dc.DrawRotatedText(label,
                rectLabel.x + (rectLabel.width - height) / 2,
                rectLabel.y + (rectLabel.height + width) / 2, 90);
        }
        else
        {
            dc.DrawRotatedText(label,
                rectLabel.x + (rectLabel.width + height) / 2,
                rectLabel.y + (rectLabel.height - width) / 2, -90);
        }
    }
    else
    {
        wxRect rectBounds;
        DoDrawLabel(dc, label, bitmap, rectLabel, wxALIGN_CENTRE,
                    &rectBounds, colText);

        // BTab::DrawFocusMark(): a line under the label
        if ( (flags & wxCONTROL_FOCUSED) && selected )
        {
            dc.SetPen(UIColour(B_KEYBOARD_NAVIGATION_COLOR));
            const int y = dir == wxBOTTOM ? rect.y + 3
                                          : rect.GetBottom() - 3;
            dc.DrawLine(rectBounds.x, y, rectBounds.GetRight() + 1, y);
        }
    }
}

#endif // wxUSE_NOTEBOOK

// ----------------------------------------------------------------------------
// slider: BSlider with a block thumb
// ----------------------------------------------------------------------------

#if wxUSE_SLIDER

/* static */
void wxHaikuRenderer::GetSliderTickSides(wxOrientation orient, long style,
                                         bool *before, bool *after)
{
    *before =
    *after = false;

    if ( !(style & wxSL_AUTOTICKS) )
        return;

    if ( style & wxSL_BOTH )
    {
        *before =
        *after = true;
    }
    else if ( ((style & wxSL_TOP) && orient == wxHORIZONTAL) ||
                ((style & wxSL_LEFT) && orient == wxVERTICAL) )
    {
        *before = true;
    }
    else
    {
        *after = true;
    }
}

wxSize
wxHaikuRenderer::GetSliderThumbSize(const wxRect& WXUNUSED(rect),
                                    int WXUNUSED(lenThumb),
                                    wxOrientation orient) const
{
    return orient == wxHORIZONTAL ? wxSize(ThumbAlong(), ThumbAcross())
                                  : wxSize(ThumbAcross(), ThumbAlong());
}

wxRect wxHaikuRenderer::GetSliderShaftRect(const wxRect& rectOrig,
                                           int WXUNUSED(lenThumb),
                                           wxOrientation orient,
                                           long style) const
{
    bool before, after;
    GetSliderTickSides(orient, style, &before, &after);

    const int margin = Scaled(2),
              thickness = BarThickness(),
              half = ThumbAcross() / 2 + margin;

    // the centre line of the bar: away from the ticks, as far as the thumb
    // needs
    wxRect rect = rectOrig;
    if ( orient == wxHORIZONTAL )
    {
        int centre;
        if ( before == after )
            centre = rect.y + rect.height / 2;
        else if ( before )
            centre = wxMax(rect.GetBottom() + 1 - half, rect.y + half);
        else
            centre = wxMin(rect.y + half, rect.GetBottom() + 1 - half);

        rect.x += margin;
        rect.width -= 2*margin;
        rect.y = centre - thickness / 2;
        rect.height = thickness;
    }
    else // vertical
    {
        int centre;
        if ( before == after )
            centre = rect.x + rect.width / 2;
        else if ( before )
            centre = wxMax(rect.GetRight() + 1 - half, rect.x + half);
        else
            centre = wxMin(rect.x + half, rect.GetRight() + 1 - half);

        rect.y += margin;
        rect.height -= 2*margin;
        rect.x = centre - thickness / 2;
        rect.width = thickness;
    }

    return rect;
}

void wxHaikuRenderer::DrawSliderShaft(wxDC& dc,
                                      const wxRect& rectOrig,
                                      int lenThumb,
                                      wxOrientation orient,
                                      int flags,
                                      long style,
                                      wxRect *rectShaft)
{
    const wxRect rect = GetSliderShaftRect(rectOrig, lenThumb, orient, style);
    if ( rectShaft )
        *rectShaft = rect;

    const wxColour base = WindowBackground(dc);

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
    {
        dc.SetPen(wxHaikuLook::FrameColour(base));
        dc.SetBrush(LookTint(base, B_DARKEN_1_TINT));
        dc.DrawRectangle(rect);
        return;
    }

    const rgb_color colBase = wxHaikuColour(base);
    p.SetBackground(colBase);

    const BRect br = p.Rect(rect);
    be_control_look->DrawSliderBar(p.View(), br, br, colBase,
        be_control_look->SliderBarColor(colBase),
        flags & wxCONTROL_DISABLED ? BControlLook::B_DISABLED : 0,
        orient == wxVERTICAL ? B_VERTICAL : B_HORIZONTAL);
}

void wxHaikuRenderer::DrawSliderThumb(wxDC& dc,
                                      const wxRect& rect,
                                      wxOrientation orient,
                                      int flags,
                                      long WXUNUSED(style))
{
    const wxColour base = UIColour(B_CONTROL_BACKGROUND_COLOR);

    {
        wxHaikuLookPainter p(dc);
        if ( p.IsOk() )
        {
            p.SetBackground(wxHaikuColour(WindowBackground(dc)));

            uint32 f = 0;
            if ( flags & wxCONTROL_DISABLED )
                f |= BControlLook::B_DISABLED;
            if ( flags & wxCONTROL_PRESSED )
                f |= BControlLook::B_CLICKED;
            if ( flags & wxCONTROL_CURRENT )
                f |= BControlLook::B_HOVER;

            BRect br = p.Rect(rect);
            be_control_look->DrawSliderThumb(p.View(), br, br,
                wxHaikuColour(base), f,
                orient == wxVERTICAL ? B_VERTICAL : B_HORIZONTAL);
        }
        else
        {
            dc.SetPen(wxHaikuLook::FrameColour(base));
            dc.SetBrush(base);
            dc.DrawRectangle(rect);
        }
    }

    // BSlider::DrawFocusMark(): a rectangle inside the thumb
    if ( (flags & wxCONTROL_FOCUSED) && !(flags & wxCONTROL_DISABLED) )
    {
        wxRect rectFocus(rect.x + 2, rect.y + 2,
                         rect.width - 5, rect.height - 5);
        wxHaikuLook::DrawFocusFrame(dc, rectFocus);
    }
}

void wxHaikuRenderer::DrawSliderTicks(wxDC& dc,
                                      const wxRect& rect,
                                      int lenThumb,
                                      wxOrientation orient,
                                      int start,
                                      int end,
                                      int step,
                                      int flags,
                                      long style)
{
    if ( end == start || step <= 0 )
        return;

    bool before, after;
    GetSliderTickSides(orient, style, &before, &after);

    const bool vertical = orient == wxVERTICAL;
    const wxRect rectShaft = GetSliderShaftRect(rect, lenThumb, orient, style);
    const int lenTick = GetSliderTickLen(),
              gap = (ThumbAcross() - BarThickness()) / 2 + Scaled(1),
              halfThumb = ThumbAlong() / 2;

    // along the shaft, the thumb is centred on the tick of its value
    const int a1 = (vertical ? rectShaft.y : rectShaft.x) + halfThumb,
              a2 = (vertical ? rectShaft.GetBottom() : rectShaft.GetRight())
                    - (ThumbAlong() - halfThumb - 1),
              len = a2 - a1;

    // across it
    const int b1 = (vertical ? rectShaft.x : rectShaft.y) - gap - lenTick,
              b2 = (vertical ? rectShaft.GetRight() : rectShaft.GetBottom())
                    + 1 + gap;

    // the hash marks of a BSlider: a dark line and a light one beside it
    const wxColour base = WindowBackground(dc);
    wxColour dark = LookTint(base, B_DARKEN_2_TINT),
             light = LookTint(base, B_LIGHTEN_2_TINT);
    if ( flags & wxCONTROL_DISABLED )
        dark = LookTint(base, B_DARKEN_1_TINT);

    const int range = end - start;
    for ( int n = 0; ; n += step )
    {
        const bool lastTick = n >= range;
        const int a = lastTick ? a2 : a1 + (int)((double)len * n / range);

        for ( int side = 0; side < 2; side++ )
        {
            if ( side == 0 ? !before : !after )
                continue;

            const int b = side == 0 ? b1 : b2;
            if ( vertical )
            {
                dc.SetPen(dark);
                dc.DrawLine(b, a, b + lenTick, a);
                dc.SetPen(light);
                dc.DrawLine(b, a + 1, b + lenTick, a + 1);
            }
            else
            {
                dc.SetPen(dark);
                dc.DrawLine(a, b, a, b + lenTick);
                dc.SetPen(light);
                dc.DrawLine(a + 1, b, a + 1, b + lenTick);
            }
        }

        if ( lastTick )
            break;
    }
}

#endif // wxUSE_SLIDER

// ----------------------------------------------------------------------------
// menus: BMenuBar, BMenu and BMenuItem
// ----------------------------------------------------------------------------

#if wxUSE_MENUS

// the layout of a menu, calculated by GetMenuGeometry()
class wxHaikuMenuGeometryInfo : public wxMenuGeometryInfo
{
public:
    virtual wxSize GetSize() const wxOVERRIDE { return m_size; }

    // the total size of the menu
    wxSize m_size;

    // where the labels and the accelerators start
    wxCoord m_ofsLabel,
            m_ofsAccel;

    // the height of a normal (not separator) item
    wxCoord m_heightItem;

    // the margin above the text of an item
    wxCoord m_marginTop;
};

namespace
{

// BMenu: fPad = (2.3, 1/3, 10/3, 0) * label spacing
struct MenuPadding
{
    MenuPadding()
    {
        const double spacing = wxHaikuLook::LabelSpacing();
        left = (int)ceil(spacing * 2.3);
        top = (int)ceil(spacing / 3.0);
        right = (int)ceil(spacing / 3.0 * 10.0);
    }

    int left, top, right;
};

} // anonymous namespace

wxSize wxHaikuRenderer::GetMenuBarItemSize(const wxSize& sizeText) const
{
    // BMenuBar: margins of 2/3 of the font size at the left and right of
    // an item, of 1/6 above and below
    const double fontSize = be_plain_font->Size();

    wxSize size = sizeText;
    size.x += 2*(int)ceil(fontSize * 2.0 / 3.0);
    size.y += 2*(int)ceil(fontSize / 6.0) + 1;

    return size;
}

bool wxHaikuRenderer::DrawMenuBarBackground(wxDC& dc, const wxRect& rect)
{
    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
        return false;

    const rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
    p.SetBackground(base);

    // as BMenuBar::Draw(): the surface, and the line under it over it
    BRect r = p.Rect(rect);
    be_control_look->DrawBorder(p.View(), r, r, base, B_PLAIN_BORDER, 0,
                                BControlLook::B_BOTTOM_BORDER);
    be_control_look->DrawMenuBarBackground(p.View(), r, r, base, 0,
                                           BControlLook::B_ALL_BORDERS);
    return true;
}

void wxHaikuRenderer::DrawMenuSelection(wxDC& dc, const wxRect& rect)
{
    // BMenuItem::Draw(): the surface of an activated item
    const rgb_color base = ui_color(B_MENU_SELECTED_BACKGROUND_COLOR);

    wxHaikuLookPainter p(dc);
    if ( !p.IsOk() )
    {
        wxHaikuLook::Fill(dc, rect, wxFromHaikuColour(base));
        return;
    }

    p.SetBackground(base);

    BRect r = p.Rect(rect);
    be_control_look->DrawMenuItemBackground(p.View(), r, r, base,
                                            BControlLook::B_ACTIVATED);
}

void wxHaikuRenderer::DrawMenuBarItem(wxDC& dc,
                                      const wxRect& rectOrig,
                                      const wxString& label,
                                      int flags,
                                      int WXUNUSED(indexAccel))
{
    // the item, above the line under the menu bar
    wxRect rect = rectOrig;
    rect.height--;

    const wxColour bg = UIColour(B_MENU_BACKGROUND_COLOR);

    wxColour colText;
    if ( flags & wxCONTROL_DISABLED )
    {
        colText = wxHaikuLook::DisabledText(UIColour(B_MENU_ITEM_TEXT_COLOR),
                                            bg);
    }
    else if ( flags & wxCONTROL_SELECTED )
    {
        DrawMenuSelection(dc, rect);
        colText = UIColour(B_MENU_SELECTED_ITEM_TEXT_COLOR);
    }
    else
    {
        colText = UIColour(B_MENU_ITEM_TEXT_COLOR);
    }

    DoDrawLabel(dc, label, wxNullBitmap, rect, wxALIGN_CENTRE, NULL, colText);
}

void wxHaikuRenderer::DrawMenuItem(wxDC& dc,
                                   wxCoord y,
                                   const wxMenuGeometryInfo& gi,
                                   const wxString& label,
                                   const wxString& accel,
                                   const wxBitmap& bitmap,
                                   int flags,
                                   int WXUNUSED(indexAccel))
{
    const wxHaikuMenuGeometryInfo& geometryInfo =
        static_cast<const wxHaikuMenuGeometryInfo&>(gi);

    const wxRect rect(0, y, geometryInfo.m_size.x, geometryInfo.m_heightItem);

    const bool disabled = (flags & wxCONTROL_DISABLED) != 0,
               selected = (flags & wxCONTROL_SELECTED) != 0 && !disabled;

    // the colours of BMenuItem
    const wxColour bg = UIColour(B_MENU_BACKGROUND_COLOR);
    wxColour colText;
    if ( disabled )
    {
        const rgb_color c = wxHaikuColour(bg);
        colText = wxFromHaikuColour(tint_color(c,
                    wxHaikuLook::IsDark(c) ? B_LIGHTEN_2_TINT
                                           : B_DISABLED_LABEL_TINT));
    }
    else if ( selected )
    {
        DrawMenuSelection(dc, rect);
        colText = UIColour(B_MENU_SELECTED_ITEM_TEXT_COLOR);
    }
    else
    {
        colText = UIColour(B_MENU_ITEM_TEXT_COLOR);
    }

    // marks and arrows are a little lighter than the text (kMarkTint)
    const wxColour colMark = wxHaikuLook::Tint(colText, 0.75f);

    // the mark, or the bitmap of the item, in the left margin
    wxRect rectMargin = rect;
    rectMargin.width = geometryInfo.m_ofsLabel;
    if ( bitmap.IsOk() )
    {
        wxRect rectBmp(0, 0, bitmap.GetWidth(), bitmap.GetHeight());
        rectBmp = rectBmp.CentreIn(rectMargin);
        dc.DrawBitmap(bitmap, rectBmp.x, rectBmp.y, true /* use mask */);
    }
    else if ( flags & wxCONTROL_CHECKED )
    {
        // BMenuItem::_DrawMarkSymbol()
        const int side = wxMin(rect.height - 2, MenuPadding().left);
        wxRect rectMark(0, 0, side, side);
        rectMark = rectMark.CentreIn(rectMargin);

        wxDCTextColourChanger changer(dc, colMark);
        DrawCheckMark(NULL, dc, rectMark, 0);
    }

    // the label and the accelerator
    wxRect rectLabel = rect;
    rectLabel.x = geometryInfo.m_ofsLabel;
    rectLabel.SetRight(geometryInfo.m_ofsAccel - 1);
    rectLabel.y += geometryInfo.m_marginTop;
    rectLabel.height -= geometryInfo.m_marginTop;
    DoDrawLabel(dc, label, wxNullBitmap, rectLabel,
                wxALIGN_LEFT | wxALIGN_TOP, NULL, colText);

    if ( !accel.empty() )
    {
        wxRect rectAccel = rectLabel;
        rectAccel.x = geometryInfo.m_ofsAccel;
        rectAccel.SetRight(rect.GetRight() - wxHaikuLook::LabelSpacing());
        DoDrawLabel(dc, accel, wxNullBitmap, rectAccel,
                    wxALIGN_LEFT | wxALIGN_TOP, NULL, colText);
    }

    // BMenuItem::_DrawSubmenuSymbol(): an arrow in a square of 2/3 of the
    // height of the item at its end
    if ( flags & wxCONTROL_ISSUBMENU )
    {
        const int side = (rect.height * 2 + 1) / 3;
        wxRect rectArrow(rect.GetRight() - side, rect.y,
                         side + 1, rect.height);
        rectArrow = wxRect(0, 0, side + 1, side + 1).CentreIn(rectArrow);
        wxHaikuLook::DrawArrow(dc, rectArrow, wxRIGHT, colMark);
    }
}

void wxHaikuRenderer::DrawMenuSeparator(wxDC& dc,
                                        wxCoord y,
                                        const wxMenuGeometryInfo& geomInfo)
{
    // BSeparatorItem::Draw(): a dark and a light line in the middle
    const wxHaikuMenuGeometryInfo& gi =
        static_cast<const wxHaikuMenuGeometryInfo&>(geomInfo);

    const int height = wxMax(4, (int)floor(be_plain_font->Size() * 0.8 / 2) * 2)
                        + gi.m_marginTop + 1;
    const wxCoord yLine = y + (height - 1) / 2;
    const wxCoord x2 = gi.m_size.x - 1;

    const wxColour base = UIColour(B_MENU_BACKGROUND_COLOR);
    dc.SetPen(LookTint(base, B_DARKEN_1_TINT));
    dc.DrawLine(1, yLine, x2, yLine);
    dc.SetPen(LookTint(base, B_LIGHTEN_2_TINT));
    dc.DrawLine(1, yLine + 1, x2, yLine + 1);
}

wxMenuGeometryInfo *wxHaikuRenderer::GetMenuGeometry(wxWindow *WXUNUSED(win),
                                                     const wxMenu& menu) const
{
    // the items are drawn in the standard font of the system
    wxScreenDC dc;
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    const MenuPadding pad;
    const wxCoord heightText = dc.GetCharHeight();

    // BMenu::_ComputeColumnLayout(): an item is as high as the font and the
    // padding above it, a separator 0.8 of the font size (rounded down to
    // an even number)
    const wxCoord heightItem = heightText + pad.top + 1,
                  heightSep = wxMax(4, (int)floor(be_plain_font->Size()
                                                    * 0.8 / 2) * 2)
                                + pad.top + 1;

    wxCoord height = 0,
            widthLabelMax = 0,
            widthAccelMax = 0,
            widthBmpMax = 0;
    bool hasSubmenus = false;

    for ( wxMenuItemList::compatibility_iterator
            node = menu.GetMenuItems().GetFirst();
          node;
          node = node->GetNext() )
    {
        wxMenuItem * const item = node->GetData();

        wxCoord h;
        if ( item->IsSeparator() )
        {
            h = heightSep;
        }
        else
        {
            h = heightItem;

            wxCoord width;
            dc.GetTextExtent(item->GetItemLabelText(), &width, NULL);
            if ( width > widthLabelMax )
                widthLabelMax = width;

            const wxString accel = item->GetAccelString();
            if ( !accel.empty() )
            {
                dc.GetTextExtent(accel, &width, NULL);
                if ( width > widthAccelMax )
                    widthAccelMax = width;
            }

            const wxBitmap& bmp = item->GetBitmap();
            if ( bmp.IsOk() )
            {
                if ( bmp.GetWidth() > widthBmpMax )
                    widthBmpMax = bmp.GetWidth();
                if ( bmp.GetHeight() + 2 > h )
                    h = bmp.GetHeight() + 2;
            }

            if ( item->IsSubMenu() )
                hasSubmenus = true;
        }

        // remember the item position and height
        item->SetGeometry(height, h);

        height += h;
    }

    wxHaikuMenuGeometryInfo * const gi = new wxHaikuMenuGeometryInfo;

    gi->m_heightItem = heightItem;
    gi->m_marginTop = pad.top;

    // the left margin has the marks, or the bitmaps if they are wider
    gi->m_ofsLabel = wxMax(pad.left, widthBmpMax + 2*Scaled(3));

    // the accelerators are aligned in a column after the widest label
    gi->m_ofsAccel = gi->m_ofsLabel + widthLabelMax;
    wxCoord width = gi->m_ofsAccel;
    if ( widthAccelMax > 0 )
    {
        gi->m_ofsAccel += pad.right;
        width = gi->m_ofsAccel + widthAccelMax + wxHaikuLook::LabelSpacing();
    }
    else
    {
        width += pad.right;
    }

    // and the arrows of the submenus after everything
    if ( hasSubmenus )
        width += heightItem / 2;

    gi->m_size.x = width;
    gi->m_size.y = height;

    return gi;
}

#endif // wxUSE_MENUS

// ----------------------------------------------------------------------------
// status bar
// ----------------------------------------------------------------------------

#if wxUSE_STATUSBAR

void wxHaikuRenderer::DrawStatusField(wxDC& dc,
                                      const wxRect& rect,
                                      const wxString& label,
                                      int flags,
                                      int style)
{
    // The system has no status bar: its applications have plain text at the
    // bottom of their windows, and so are the fields unless they ask for a
    // border.
    wxRect rectIn = rect;
    if ( style == wxSB_RAISED )
        DrawBorder(dc, wxBORDER_RAISED, rect, 0, &rectIn);
    else if ( style == wxSB_SUNKEN )
        DrawBorder(dc, wxBORDER_STATIC, rect, 0, &rectIn);

    rectIn.Deflate(GetStatusBarFieldMargins());

    // the resize corner of the window is drawn by the decorator of the
    // system, over the end of the last field
    if ( flags & wxCONTROL_SIZEGRIP )
        rectIn.width -= wxHaikuLook::ScrollBarWidth();

    if ( rectIn.width <= 0 || label.empty() )
        return;

    wxDCClipper clipper(dc, rectIn);
    DrawLabel(dc, label, rectIn, flags & wxCONTROL_DISABLED,
              wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
}

#endif // wxUSE_STATUSBAR

// ----------------------------------------------------------------------------
// size adjustments
// ----------------------------------------------------------------------------

void wxHaikuRenderer::AdjustSize(wxSize *size, const wxWindow *window)
{
#if wxUSE_SCROLLBAR
    // the best size of a scroll bar is already right
    if ( wxDynamicCast(window, wxScrollBar) )
        return;
#endif // wxUSE_SCROLLBAR

    const wxAnyButton * const button = AsButton(window);
    if ( button )
    {
        const int spacing = wxHaikuLook::LabelSpacing();

        float left = 3, top = 3, right = 3, bottom = 3;
        if ( be_control_look )
        {
            be_control_look->GetInsets(BControlLook::B_BUTTON_FRAME,
                                       BControlLook::B_BUTTON_BACKGROUND,
                                       0, left, top, right, bottom);
        }

        if ( IsFlatButton(window) )
        {
            // a flat button: its contents and a small margin
            size->x += 2*Scaled(2);
            size->y += 2*Scaled(2);
            return;
        }

#if wxUSE_BMPBUTTON
        if ( wxDynamicCast(window, wxBitmapButton)
                || button->GetLabel().empty() )
        {
            // the bitmap inside the frame, with a margin
            size->x += (int)(left + right) + spacing;
            size->y += (int)(top + bottom) + spacing;
            return;
        }
#endif // wxUSE_BMPBUTTON

        // BButton::_ValidatePreferredSize(): the label with 3.3 label
        // spacings around it, 12.5 of them at least; 1.8 times as high as
        // its text
        const int heightLine = window->GetCharHeight();
        const int margins = (int)(top + bottom) + spacing;

        if ( window->HasFlag(wxBU_EXACTFIT) )
        {
            size->x += (int)(left + right) + spacing;
            size->y += margins;
        }
        else
        {
            size->x += (int)(left + right) + (int)ceil(spacing * 3.3);
            size->x = wxMax(size->x, (int)ceil(spacing * 12.5));

            size->y += wxMax(margins, (int)ceil(heightLine * 0.8));
        }

        return;
    }

    wxStdRenderer::AdjustSize(size, window);
}

// ============================================================================
// input handlers
// ============================================================================

bool wxHaikuInputHandler::HandleMouse(wxInputConsumer *control,
                                      const wxMouseEvent& event)
{
    // clicking on a control gives it the focus
    if ( event.ButtonDown() )
    {
        wxWindow * const win = control->GetInputWindow();

        if ( win->CanAcceptFocus() && wxWindow::FindFocus() != win )
        {
            win->SetFocus();

            return true;
        }
    }

    return false;
}

#if wxUSE_CHECKBOX

bool wxHaikuCheckboxInputHandler::HandleKey(wxInputConsumer *control,
                                            const wxKeyEvent& event,
                                            bool pressed)
{
    // space toggles (enter is the default button of the dialog)
    if ( pressed )
    {
        if ( event.GetKeyCode() == WXK_SPACE )
        {
            control->PerformAction(wxACTION_CHECKBOX_TOGGLE);

            return true;
        }
    }

    return false;
}

#endif // wxUSE_CHECKBOX

#endif // wxUSE_THEME_HAIKU
