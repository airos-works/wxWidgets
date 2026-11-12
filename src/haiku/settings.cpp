/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/settings.cpp
// Purpose:     wxSystemSettingsNative: colours from ui_color(), system fonts
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/settings.h"

#ifndef WX_PRECOMP
    #include "wx/colour.h"
    #include "wx/font.h"
    #include "wx/gdicmn.h"
#endif

#include "wx/display.h"

#include "wx/haiku/private.h"

#include <ControlLook.h>
#include <InterfaceDefs.h>

// ui_color() reads the colours from memory shared with app_server: it is
// cheap and always current (dark mode included), nothing is cached here.
wxColour wxSystemSettingsNative::GetColour(wxSystemColour index)
{
    const rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);

    rgb_color c;
    switch ( index )
    {
        case wxSYS_COLOUR_SCROLLBAR:
            c = tint_color(panel, B_DARKEN_1_TINT);
            break;

        case wxSYS_COLOUR_DESKTOP:
            c = ui_color(B_DESKTOP_COLOR);
            break;

        case wxSYS_COLOUR_ACTIVECAPTION:
        case wxSYS_COLOUR_GRADIENTACTIVECAPTION:
            c = ui_color(B_WINDOW_TAB_COLOR);
            break;

        case wxSYS_COLOUR_INACTIVECAPTION:
        case wxSYS_COLOUR_GRADIENTINACTIVECAPTION:
            c = ui_color(B_WINDOW_INACTIVE_TAB_COLOR);
            break;

        case wxSYS_COLOUR_MENU:
        case wxSYS_COLOUR_MENUBAR:
            c = ui_color(B_MENU_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_WINDOW:
            c = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_LISTBOX:
            c = ui_color(B_LIST_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_WINDOWFRAME:
        case wxSYS_COLOUR_ACTIVEBORDER:
            c = ui_color(B_WINDOW_BORDER_COLOR);
            break;

        case wxSYS_COLOUR_INACTIVEBORDER:
            c = ui_color(B_WINDOW_INACTIVE_BORDER_COLOR);
            break;

        case wxSYS_COLOUR_MENUTEXT:
            c = ui_color(B_MENU_ITEM_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_WINDOWTEXT:
            c = ui_color(B_DOCUMENT_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_LISTBOXTEXT:
            c = ui_color(B_LIST_ITEM_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_CAPTIONTEXT:
            c = ui_color(B_WINDOW_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_INACTIVECAPTIONTEXT:
            c = ui_color(B_WINDOW_INACTIVE_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_APPWORKSPACE:
            c = tint_color(panel, B_DARKEN_2_TINT);
            break;

        case wxSYS_COLOUR_HIGHLIGHT:
            c = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_MENUHILIGHT:
            c = ui_color(B_MENU_SELECTED_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_HIGHLIGHTTEXT:
        case wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT:
            c = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_BTNFACE:
            c = panel;
            break;

        case wxSYS_COLOUR_BTNSHADOW:
            c = tint_color(panel, B_DARKEN_2_TINT);
            break;

        case wxSYS_COLOUR_3DDKSHADOW:
            c = tint_color(panel, B_DARKEN_3_TINT);
            break;

        case wxSYS_COLOUR_BTNHIGHLIGHT:
            c = tint_color(panel, B_LIGHTEN_MAX_TINT);
            break;

        case wxSYS_COLOUR_3DLIGHT:
            c = tint_color(panel, B_LIGHTEN_1_TINT);
            break;

        case wxSYS_COLOUR_GRAYTEXT:
            c = disable_color(ui_color(B_PANEL_TEXT_COLOR), panel);
            break;

        case wxSYS_COLOUR_BTNTEXT:
            c = ui_color(B_PANEL_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_INFOTEXT:
            c = ui_color(B_TOOL_TIP_TEXT_COLOR);
            break;

        case wxSYS_COLOUR_INFOBK:
            c = ui_color(B_TOOL_TIP_BACKGROUND_COLOR);
            break;

        case wxSYS_COLOUR_HOTLIGHT:
            c = ui_color(B_LINK_TEXT_COLOR);
            break;

        default:
            c = panel;
    }

    return wxColour(c.red, c.green, c.blue);
}

wxFont wxSystemSettingsNative::GetFont(wxSystemFont index)
{
    const double scale = wxHaikuScaleFactor();

    switch ( index )
    {
        case wxSYS_OEM_FIXED_FONT:
        case wxSYS_ANSI_FIXED_FONT:
        case wxSYS_SYSTEM_FIXED_FONT:
            {
                wxFont font(wxFontInfo(be_fixed_font->Size() * 72.0 / 96.0 / scale)
                                .Family(wxFONTFAMILY_TELETYPE));
                return font;
            }

        case wxSYS_ANSI_VAR_FONT:
        case wxSYS_SYSTEM_FONT:
        case wxSYS_DEVICE_DEFAULT_FONT:
        case wxSYS_DEFAULT_GUI_FONT:
            {
                // The plain font at the size it has: 9 points at 96 DPI
                // times the scale factor are its 12 (scaled) pixels.
                wxFont font(wxFontInfo(be_plain_font->Size() * 72.0 / 96.0 / scale)
                                .Family(wxFONTFAMILY_DEFAULT));
                return font;
            }

        default:
            wxFAIL_MSG( wxS("unknown font type") );
            return wxNullFont;
    }
}

int wxSystemSettingsNative::GetMetric(wxSystemMetric index,
                                      const wxWindow* WXUNUSED(win))
{
    const double scale = wxHaikuScaleFactor();

    switch ( index )
    {
        case wxSYS_SCREEN_X:
            return wxDisplay().GetGeometry().width;

        case wxSYS_SCREEN_Y:
            return wxDisplay().GetGeometry().height;

        case wxSYS_BORDER_X:
        case wxSYS_BORDER_Y:
            return 1;

        case wxSYS_EDGE_X:
        case wxSYS_EDGE_Y:
            return 2;

        case wxSYS_CURSOR_X:
        case wxSYS_CURSOR_Y:
            return 32;

        case wxSYS_DCLICK_X:
        case wxSYS_DCLICK_Y:
        case wxSYS_DRAG_X:
        case wxSYS_DRAG_Y:
            return wxRound(4 * scale);

        case wxSYS_ICON_X:
        case wxSYS_ICON_Y:
            return wxRound(32 * scale);

        case wxSYS_SMALLICON_X:
        case wxSYS_SMALLICON_Y:
            return wxRound(16 * scale);

        case wxSYS_HSCROLL_Y:
        case wxSYS_VSCROLL_X:
        case wxSYS_HSCROLL_ARROW_X:
        case wxSYS_HSCROLL_ARROW_Y:
        case wxSYS_VSCROLL_ARROW_X:
        case wxSYS_VSCROLL_ARROW_Y:
        case wxSYS_HTHUMB_X:
        case wxSYS_VTHUMB_Y:
            return be_control_look
                    ? (int)be_control_look->GetScrollBarWidth(B_VERTICAL) + 1
                    : wxRound(15 * scale);

        case wxSYS_CAPTION_Y:
        case wxSYS_MENU_Y:
            return wxRound(21 * scale);

        case wxSYS_FRAMESIZE_X:
        case wxSYS_FRAMESIZE_Y:
            return 5;

        case wxSYS_DCLICK_MSEC:
            {
                bigtime_t speed = 500000;
                get_click_speed(&speed);
                return (int)(speed / 1000);
            }

        case wxSYS_CARET_ON_MSEC:
        case wxSYS_CARET_OFF_MSEC:
            return 500;

        case wxSYS_CARET_TIMEOUT_MSEC:
            return -1;

        case wxSYS_SWAP_BUTTONS:
        case wxSYS_PENWINDOWS_PRESENT:
        case wxSYS_SHOW_SOUNDS:
            return 0;

        case wxSYS_NETWORK_PRESENT:
            return 1;

        default:
            return -1;
    }
}

bool wxSystemSettingsNative::HasFeature(wxSystemFeature index)
{
    switch ( index )
    {
        case wxSYS_CAN_ICONIZE_FRAME:
            return true;

        // the windows have native decorations: wxUniv must not draw its own
        case wxSYS_CAN_DRAW_FRAME_DECORATIONS:
            return true;

        default:
            return false;
    }
}
