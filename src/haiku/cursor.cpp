/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/cursor.cpp
// Purpose:     wxCursor on BCursor, busy cursor
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/image.h"
    #include "wx/utils.h"
    #include "wx/window.h"
#endif

#include "wx/cursor.h"
#include "wx/bitmap.h"

#include "wx/haiku/private.h"

#include <Cursor.h>

// ----------------------------------------------------------------------------
// wxCursorRefData
// ----------------------------------------------------------------------------

class wxCursorRefData : public wxGDIRefData
{
public:
    wxCursorRefData() : m_cursor(NULL) { }

    wxCursorRefData(const wxCursorRefData& data)
        : wxGDIRefData(),
          m_cursor(data.m_cursor ? new BCursor(*data.m_cursor) : NULL),
          m_hotSpot(data.m_hotSpot)
    {
    }

    virtual ~wxCursorRefData()
    {
        // (a cursor of a static object, destroyed after the BApplication,
        // cannot tell app_server any more and is left alone)
        if ( be_app )
            delete m_cursor;
    }

    virtual bool IsOk() const wxOVERRIDE { return m_cursor != NULL; }

    BCursor *m_cursor;
    wxPoint m_hotSpot;
};

#define M_CURSOR (static_cast<wxCursorRefData *>(m_refData))

wxIMPLEMENT_DYNAMIC_CLASS(wxCursor, wxGDIObject);

// ----------------------------------------------------------------------------
// wxCursor
// ----------------------------------------------------------------------------

void wxCursor::InitFromStock(wxStockCursor cursorId)
{
    BCursorID id;
    switch ( cursorId )
    {
        case wxCURSOR_ARROW:
        case wxCURSOR_RIGHT_ARROW:
            id = B_CURSOR_ID_SYSTEM_DEFAULT;
            break;

        case wxCURSOR_BLANK:
            id = B_CURSOR_ID_NO_CURSOR;
            break;

        case wxCURSOR_BULLSEYE:
        case wxCURSOR_CROSS:
        case wxCURSOR_PENCIL:
        case wxCURSOR_PAINT_BRUSH:
        case wxCURSOR_SPRAYCAN:
            id = B_CURSOR_ID_CROSS_HAIR;
            break;

        case wxCURSOR_HAND:
            id = B_CURSOR_ID_FOLLOW_LINK;
            break;

        case wxCURSOR_IBEAM:
        case wxCURSOR_CHAR:
            id = B_CURSOR_ID_I_BEAM;
            break;

        case wxCURSOR_MAGNIFIER:
            id = B_CURSOR_ID_ZOOM_IN;
            break;

        case wxCURSOR_NO_ENTRY:
            id = B_CURSOR_ID_NOT_ALLOWED;
            break;

        case wxCURSOR_QUESTION_ARROW:
            id = B_CURSOR_ID_HELP;
            break;

        case wxCURSOR_SIZENESW:
            id = B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST;
            break;

        case wxCURSOR_SIZENS:
            id = B_CURSOR_ID_RESIZE_NORTH_SOUTH;
            break;

        case wxCURSOR_SIZENWSE:
            id = B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST;
            break;

        case wxCURSOR_SIZEWE:
            id = B_CURSOR_ID_RESIZE_EAST_WEST;
            break;

        case wxCURSOR_SIZING:
            id = B_CURSOR_ID_MOVE;
            break;

        case wxCURSOR_WAIT:
        case wxCURSOR_WATCH:
        case wxCURSOR_ARROWWAIT:
            id = B_CURSOR_ID_PROGRESS;
            break;

        case wxCURSOR_POINT_LEFT:
        case wxCURSOR_POINT_RIGHT:
        case wxCURSOR_LEFT_BUTTON:
        case wxCURSOR_MIDDLE_BUTTON:
        case wxCURSOR_RIGHT_BUTTON:
        default:
            id = B_CURSOR_ID_SYSTEM_DEFAULT;
    }

    wxCursorRefData * const data = new wxCursorRefData;
    data->m_cursor = new BCursor(id);
    m_refData = data;
}

#if wxUSE_IMAGE

wxCursor::wxCursor(const wxImage& image)
{
    InitFromImage(image);
}

wxCursor::wxCursor(const char* const* xpmData)
{
    InitFromImage(wxImage(xpmData));
}

void wxCursor::InitFromImage(const wxImage& imageOrig)
{
    if ( !imageOrig.IsOk() )
        return;

    wxImage image(imageOrig);
    if ( !image.HasAlpha() )
        image.InitAlpha(); // turns the mask, if any, into alpha

    const int hotX = image.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_X)
                        ? image.GetOptionInt(wxIMAGE_OPTION_CUR_HOTSPOT_X) : 0,
              hotY = image.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y)
                        ? image.GetOptionInt(wxIMAGE_OPTION_CUR_HOTSPOT_Y) : 0;

    const wxBitmap bitmap(image);
    if ( !bitmap.IsOk() )
        return;

    wxCursorRefData * const data = new wxCursorRefData;
    data->m_cursor = new BCursor(bitmap.GetHaikuBitmap(), BPoint(hotX, hotY));
    data->m_hotSpot = wxPoint(hotX, hotY);
    m_refData = data;
}

#endif // wxUSE_IMAGE

wxCursor::wxCursor(const wxString& name, wxBitmapType type,
                   int hotSpotX, int hotSpotY)
{
#if wxUSE_IMAGE
    wxImage image;
    if ( !image.LoadFile(name, type) )
        return;

    if ( !image.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_X) )
        image.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, hotSpotX);
    if ( !image.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y) )
        image.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, hotSpotY);

    InitFromImage(image);
#else
    wxUnusedVar(name);
    wxUnusedVar(type);
    wxUnusedVar(hotSpotX);
    wxUnusedVar(hotSpotY);
#endif
}

wxPoint wxCursor::GetHotSpot() const
{
    wxCHECK_MSG( IsOk(), wxDefaultPosition, wxS("invalid cursor") );

    return M_CURSOR->m_hotSpot;
}

const BCursor *wxCursor::GetHaikuCursor() const
{
    return IsOk() ? M_CURSOR->m_cursor : NULL;
}

wxGDIRefData *wxCursor::CreateGDIRefData() const
{
    return new wxCursorRefData;
}

wxGDIRefData *wxCursor::CloneGDIRefData(const wxGDIRefData *data) const
{
    return new wxCursorRefData(*static_cast<const wxCursorRefData *>(data));
}

// ----------------------------------------------------------------------------
// global cursor functions
// ----------------------------------------------------------------------------

namespace
{

int gs_busyCount = 0;
wxCursor gs_globalCursor;

void ApplyApplicationCursor(const wxCursor& cursor)
{
    if ( !be_app )
        return;

    // The application cursor is the one of all views which did not set a
    // cursor of their own.
    const BCursor *native = cursor.GetHaikuCursor();
    if ( native )
    {
        be_app->SetCursor(native, true);
    }
    else
    {
        BCursor standard(B_CURSOR_ID_SYSTEM_DEFAULT);
        be_app->SetCursor(&standard, true);
    }
}

} // anonymous namespace

void wxSetCursor(const wxCursor& cursor)
{
    gs_globalCursor = cursor;

    if ( !gs_busyCount )
        ApplyApplicationCursor(cursor);
}

void wxBeginBusyCursor(const wxCursor *cursor)
{
    if ( gs_busyCount++ == 0 )
        ApplyApplicationCursor(cursor ? *cursor : *wxHOURGLASS_CURSOR);
}

void wxEndBusyCursor()
{
    wxCHECK_RET( gs_busyCount > 0,
                 wxS("no matching wxBeginBusyCursor() for wxEndBusyCursor()") );

    if ( --gs_busyCount == 0 )
        ApplyApplicationCursor(gs_globalCursor);
}

bool wxIsBusy()
{
    return gs_busyCount > 0;
}
