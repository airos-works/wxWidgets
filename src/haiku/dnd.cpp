/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/dnd.cpp
// Purpose:     wxDropTarget and wxDropSource
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_DRAG_AND_DROP

#include "wx/dnd.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/window.h"
#endif

#include "wx/evtloop.h"
#include "wx/scopedarray.h"

#include "wx/haiku/private.h"

namespace
{

// the drop being delivered to a drop target
const wxHaikuEvent *gs_currentDrop = NULL;

// set when a drop target of this application took the data of a drag
bool gs_dropDelivered = false;
wxDragResult gs_dropResult = wxDragNone;

} // anonymous namespace

// ----------------------------------------------------------------------------
// wxDropTarget
// ----------------------------------------------------------------------------

wxDropTarget::wxDropTarget(wxDataObject *dataObject)
    : wxDropTargetBase(dataObject)
{
}

wxDragResult wxDropTarget::OnDragOver(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y),
                                      wxDragResult def)
{
    return def;
}

bool wxDropTarget::OnDrop(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y))
{
    return true;
}

wxDragResult wxDropTarget::OnData(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y),
                                  wxDragResult def)
{
    return GetData() ? def : wxDragNone;
}

bool wxDropTarget::GetData()
{
    if ( !gs_currentDrop || !gs_currentDrop->message || !m_dataObject )
        return false;

    return m_dataObject->HaikuSetFromMessage(gs_currentDrop->message);
}

wxDataFormat wxDropTarget::GetMatchingPair()
{
    if ( !gs_currentDrop || !gs_currentDrop->message || !m_dataObject )
        return wxFormatInvalid;

    const size_t count = m_dataObject->GetFormatCount(wxDataObject::Set);
    wxScopedArray<wxDataFormat> formats(count);
    m_dataObject->GetAllFormats(formats.get(), wxDataObject::Set);

    for ( size_t n = 0; n < count; n++ )
    {
        if ( wxDataObject::HaikuMessageHasFormat(gs_currentDrop->message,
                                                 formats[n]) )
        {
            return formats[n];
        }
    }

    return wxFormatInvalid;
}

// called by the window code when something was dropped on a window with a
// drop target; returns false if the target cannot take the data
bool wxHaikuDeliverDrop(wxDropTarget *target, const wxHaikuEvent& event,
                        const wxPoint& pos)
{
    if ( !event.message || !target->GetDataObject() )
        return false;

    gs_currentDrop = &event;

    bool taken = false;
    if ( target->GetMatchingPair() != wxFormatInvalid )
    {
        taken = true;

        // Command (Control in wx terms) asks for a copy where a move is the
        // default and the other way round
        wxDragResult result = wxDragCopy;

        result = target->OnEnter(pos.x, pos.y, result);
        if ( result != wxDragNone )
            result = target->OnDragOver(pos.x, pos.y, result);

        if ( result != wxDragNone && target->OnDrop(pos.x, pos.y) )
            result = target->OnData(pos.x, pos.y, result);
        else
            result = wxDragNone;

        gs_dropDelivered = true;
        gs_dropResult = result;
    }

    gs_currentDrop = NULL;

    return taken;
}

// ----------------------------------------------------------------------------
// wxDropSource
// ----------------------------------------------------------------------------

wxDropSource::wxDropSource(wxWindow *win,
                           const wxIcon& copy,
                           const wxIcon& move,
                           const wxIcon& none)
    : m_window(win),
      m_iconCopy(copy),
      m_iconMove(move),
      m_iconNone(none)
{
}

wxDropSource::wxDropSource(wxDataObject& data,
                           wxWindow *win,
                           const wxIcon& copy,
                           const wxIcon& move,
                           const wxIcon& none)
    : m_window(win),
      m_iconCopy(copy),
      m_iconMove(move),
      m_iconNone(none)
{
    SetData(data);
}

wxDropSource::~wxDropSource()
{
}

wxDragResult wxDropSource::DoDragDrop(int flags)
{
    wxCHECK_MSG( m_data, wxDragNone, wxS("no data to drag") );

    wxWindow *win = m_window;
    if ( !win )
        win = wxTheApp ? wxTheApp->GetTopWindow() : NULL;

    BView * const view = win ? win->GetHaikuView() : NULL;
    if ( !view )
        return wxDragError;

    BMessage message(B_SIMPLE_DATA);
    if ( !m_data->HaikuAddToMessage(&message) )
        return wxDragError;

    message.AddInt32("be:actions",
                     (flags & wxDrag_AllowMove) ? B_MOVE_TARGET : B_COPY_TARGET);

    gs_dropDelivered = false;
    gs_dropResult = wxDragNone;

    {
        wxHaikuViewLock lock(view);
        if ( !lock.IsLocked() )
            return wxDragError;

        // app_server takes over from here: the message travels with the
        // pointer until the buttons are released
        const wxPoint pos = win->ScreenToClient(wxGetMousePosition())
                                + win->GetClientAreaOrigin();
        view->DragMessage(&message,
                          BRect(pos.x - 16, pos.y - 8, pos.x + 16, pos.y + 8));
    }

    wxEventLoopBase * const loop = wxEventLoopBase::GetActive();
    if ( !loop )
        return wxDragError;

    // keep the application alive until the buttons are released...
    for ( ;; )
    {
        BPoint where;
        uint32 buttons = 0;
        if ( get_mouse(&where, &buttons) != B_OK || !buttons )
            break;

        loop->DispatchTimeout(30);
        wxHaikuPaintPendingWindows();
    }

    // ...and a little longer for the drop to arrive if it was on one of our
    // own windows
    const bool overOurs = wxFindWindowAtPoint(wxGetMousePosition()) != NULL;
    if ( overOurs )
    {
        for ( int n = 0; n < 25 && !gs_dropDelivered; n++ )
            loop->DispatchTimeout(20);

        return gs_dropDelivered ? gs_dropResult : wxDragNone;
    }

    // Other applications don't report what they did with the data.
    return (flags & wxDrag_DefaultMove) ? wxDragMove : wxDragCopy;
}

#endif // wxUSE_DRAG_AND_DROP
