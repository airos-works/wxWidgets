/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/looper.cpp
// Purpose:     the BView and BWindow subclasses of the port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// Everything in this file runs on BWindow looper threads. It must not call
// into wx: the hooks only translate what happens into wxHaikuEvent records
// and post them to the wx thread (see evtloop.cpp). They never wait for the
// wx thread either, which may hold the lock of this very looper.

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/haiku/private.h"

#include <Entry.h>
#include <Input.h>
#include <Path.h>

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

namespace
{

// fills the fields common to all input records from the message being
// dispatched by the looper
void FillFromCurrentMessage(wxHaikuEvent *event, BLooper *looper)
{
    BMessage * const message = looper ? looper->CurrentMessage() : NULL;
    if ( message )
    {
        message->FindInt32("buttons", &event->buttons);
        if ( message->FindInt32("modifiers", &event->modifiers) != B_OK )
            event->modifiers = modifiers();
        message->FindInt32("clicks", &event->clicks);
        message->FindInt64("when", &event->when);
    }
    else
    {
        event->modifiers = modifiers();
    }

    if ( !event->when )
        event->when = system_time();
}

} // anonymous namespace

void wxHaikuPostMouseEvent(BView *view, wxUint32 handle,
                           wxHaikuEventType type, BPoint where,
                           int32 transit, const BMessage *drag)
{
    wxHaikuEvent *event = new wxHaikuEvent(type, handle);
    FillFromCurrentMessage(event, view->Looper());

    event->where = where;
    event->transit = transit;
    event->flag = drag != NULL;

    BMessage * const message = view->Looper()->CurrentMessage();
    if ( !message || message->FindPoint("screen_where", &event->screenWhere) != B_OK )
        event->screenWhere = view->ConvertToScreen(where);

    wxHaikuPostEvent(event);
}

bool wxHaikuHandleViewMessage(BView *view, wxUint32 handle, BMessage *message)
{
    if ( message->WasDropped() )
    {
        wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_DROP, handle);
        FillFromCurrentMessage(event, view->Looper());

        event->screenWhere = message->DropPoint();
        event->where = view->ConvertFromScreen(event->screenWhere);

        entry_ref ref;
        for ( int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++ )
        {
            BPath path(&ref);
            if ( path.InitCheck() == B_OK )
                event->strings.push_back(path.Path());
        }

        if ( !event->strings.empty() )
        {
            event->value = wxHAIKU_DROP_FILES;
        }
        else
        {
            event->value = wxHAIKU_DROP_TEXT;

            const void *data;
            ssize_t size;
            if ( message->FindData("text/plain", B_MIME_TYPE, &data, &size) == B_OK
                    && size > 0 )
            {
                event->strings.push_back(std::string((const char *)data, size));
            }
        }

        // the data in other formats stays available to the drop target
        event->message = new BMessage(*message);

        wxHaikuPostEvent(event);
        return true;
    }

    switch ( message->what )
    {
        case B_MOUSE_WHEEL_CHANGED:
            {
                wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_MOUSE_WHEEL,
                                                       handle);
                FillFromCurrentMessage(event, view->Looper());
                message->FindFloat("be:wheel_delta_x", &event->deltaX);
                message->FindFloat("be:wheel_delta_y", &event->deltaY);
                wxHaikuPostEvent(event);
            }
            return true;

        case B_INPUT_METHOD_EVENT:
            {
                int32 opcode;
                if ( message->FindInt32("be:opcode", &opcode) != B_OK )
                    return true;

                wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_IME, handle);
                event->value = opcode;

                switch ( opcode )
                {
                    case B_INPUT_METHOD_CHANGED:
                        {
                            const char *string = NULL;
                            message->FindString("be:string", &string);
                            event->strings.push_back(string ? string : "");
                            message->FindBool("be:confirmed", &event->flag);
                        }
                        break;

                    case B_INPUT_METHOD_LOCATION_REQUEST:
                        // the wx thread answers: it knows where the caret is
                        event->message = new BMessage(*message);
                        break;

                    default:
                        break;
                }

                wxHaikuPostEvent(event);
            }
            return true;
    }

    return false;
}

// ============================================================================
// wxHaikuView
// ============================================================================

wxHaikuView::wxHaikuView(wxUint32 handle, BRect frame, uint32 resizingMode)
    : wxHaikuDrawView(frame, "wx", resizingMode,
                      B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
      m_handle(handle)
{
    // Not B_INPUT_METHOD_AWARE: input_server then shows the composition of
    // input methods in its own window at the bottom of the screen and sends
    // the confirmed text as key messages, which become wxEVT_CHAR like any
    // other typed character. In-line composition would need the text
    // controls to draw the unconfirmed text.
    // wx paints everything itself: app_server must not erase behind its back
    SetViewColor(B_TRANSPARENT_COLOR);
}

void wxHaikuView::Draw(BRect updateRect)
{
    // nothing is drawn here: the wx thread paints the rectangle through its
    // off-screen buffer when it gets to it
    wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_DRAW, m_handle);
    event->rect = updateRect;
    wxHaikuPostEvent(event);
}

void wxHaikuView::MouseDown(BPoint where)
{
    // keep getting the moves and the release when the pointer leaves the view
    // with the button down
    SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);

    // A window which accepts the first click (ours do: the click which
    // activates a window also works in it) is not activated by app_server
    // when its contents are clicked: that is left to the views.
    BWindow * const window = Window();
    if ( window && !window->IsActive() && !(window->Flags() & B_AVOID_FOCUS) )
        window->Activate(true);

    wxHaikuPostMouseEvent(this, m_handle, wxHAIKU_EVT_MOUSE_DOWN, where);
}

void wxHaikuView::MouseUp(BPoint where)
{
    wxHaikuPostMouseEvent(this, m_handle, wxHAIKU_EVT_MOUSE_UP, where);
}

void wxHaikuView::MouseMoved(BPoint where, uint32 transit,
                             const BMessage *dragMessage)
{
    wxHaikuPostMouseEvent(this, m_handle, wxHAIKU_EVT_MOUSE_MOVED, where,
                          transit, dragMessage);
}

void wxHaikuView::MessageReceived(BMessage *message)
{
    if ( message->what == wxHAIKU_MSG_MAKE_FOCUS )
    {
        if ( !IsFocus() )
            MakeFocus(true);
        return;
    }

    if ( !wxHaikuHandleViewMessage(this, m_handle, message) )
        wxHaikuDrawView::MessageReceived(message);
}

// ============================================================================
// wxHaikuWindow
// ============================================================================

wxHaikuWindow::wxHaikuWindow(wxUint32 handle, BRect frame, const char *title,
                             window_look look, window_feel feel, uint32 flags,
                             int32 firstSerial)
    : BWindow(frame, title, look, feel, flags),
      m_handle(handle),
      m_frameSerial(firstSerial)
{
}

void wxHaikuWindow::DispatchMessage(BMessage *message, BHandler *target)
{
    switch ( message->what )
    {
        case B_KEY_DOWN:
        case B_KEY_UP:
        case B_UNMAPPED_KEY_DOWN:
        case B_UNMAPPED_KEY_UP:
            {
                // A native view which is not one of ours (a web view...)
                // has the focus: the keyboard is its business.
                BView * const focus = CurrentFocus();
                if ( focus && strncmp(focus->Name(), "wx", 2) != 0 )
                    break;

                // All keys go to wx, including the ones BWindow would treat
                // as shortcuts itself (Command-W, Command-Q...): wx has its
                // own accelerators and menus.
                const bool down = message->what == B_KEY_DOWN
                                    || message->what == B_UNMAPPED_KEY_DOWN;

                wxHaikuEvent *event = new wxHaikuEvent(
                    down ? wxHAIKU_EVT_KEY_DOWN : wxHAIKU_EVT_KEY_UP, m_handle);

                message->FindInt32("key", &event->key);
                message->FindInt32("raw_char", &event->rawChar);
                if ( message->FindInt32("modifiers", &event->modifiers) != B_OK )
                    event->modifiers = modifiers();
                message->FindInt64("when", &event->when);

                int32 repeat = 0;
                if ( message->FindInt32("be:key_repeat", &repeat) == B_OK )
                    event->repeat = repeat > 0;

                const char *bytes = NULL;
                if ( message->FindString("bytes", &bytes) == B_OK && bytes )
                {
                    const size_t len = strlen(bytes);
                    if ( len < sizeof(event->bytes) )
                    {
                        memcpy(event->bytes, bytes, len + 1);
                        event->numBytes = (uint8)len;
                    }
                }
                else
                {
                    int8 byte;
                    if ( message->FindInt8("byte", &byte) == B_OK && byte )
                    {
                        event->bytes[0] = byte;
                        event->bytes[1] = '\0';
                        event->numBytes = 1;
                    }
                }

                wxHaikuPostEvent(event);
            }
            return;

        case B_MODIFIERS_CHANGED:
            {
                wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_MODIFIERS,
                                                       m_handle);
                message->FindInt32("modifiers", &event->modifiers);
                message->FindInt32("be:old_modifiers", &event->value);
                message->FindInt64("when", &event->when);
                wxHaikuPostEvent(event);
            }
            break;
    }

    BWindow::DispatchMessage(message, target);
}

void wxHaikuWindow::MessageReceived(BMessage *message)
{
    if ( message->what == wxHAIKU_MSG_SET_FRAME )
    {
        BRect frame;
        if ( message->FindRect("frame", &frame) == B_OK )
        {
            const BRect current = Frame();
            if ( frame.LeftTop() != current.LeftTop() )
                MoveTo(frame.LeftTop());
            if ( frame.Width() != current.Width()
                    || frame.Height() != current.Height() )
            {
                ResizeTo(frame.Width(), frame.Height());
            }
        }

        m_frameSerial++;

        BMessage reply(B_REPLY);
        reply.AddRect("frame", Frame());
        reply.AddInt32("serial", m_frameSerial);
        message->SendReply(&reply);
        return;
    }

    BWindow::MessageReceived(message);
}

void wxHaikuWindow::PostFrame()
{
    wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_FRAME, m_handle);
    event->rect = Frame();
    event->value = m_frameSerial;
    wxHaikuPostEvent(event);
}

void wxHaikuWindow::FrameMoved(BPoint WXUNUSED(newPosition))
{
    PostFrame();
}

void wxHaikuWindow::FrameResized(float WXUNUSED(newWidth),
                                 float WXUNUSED(newHeight))
{
    PostFrame();
}

void wxHaikuWindow::WindowActivated(bool active)
{
    wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_ACTIVATE, m_handle);
    event->flag = active;
    wxHaikuPostEvent(event);
}

bool wxHaikuWindow::QuitRequested()
{
    // the window is only ever destroyed by the wx thread: ask it to close
    wxHaikuPostEvent(new wxHaikuEvent(wxHAIKU_EVT_CLOSE, m_handle));
    return false;
}

void wxHaikuWindow::Minimize(bool minimize)
{
    BWindow::Minimize(minimize);

    // also called directly by the wx thread, which knows what it did
    if ( find_thread(NULL) == Thread() )
    {
        wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_MINIMIZE, m_handle);
        event->flag = minimize;
        wxHaikuPostEvent(event);
    }
}

void wxHaikuWindow::Zoom(BPoint origin, float width, float height)
{
    if ( find_thread(NULL) == Thread() )
    {
        // the zoom button: wx maximizes or restores the window itself
        wxHaikuPostEvent(new wxHaikuEvent(wxHAIKU_EVT_ZOOM, m_handle));
        return;
    }

    BWindow::Zoom(origin, width, height);
}

void wxHaikuWindow::ScreenChanged(BRect frame, color_space WXUNUSED(mode))
{
    wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_SCREEN_CHANGED, m_handle);
    event->rect = frame;
    wxHaikuPostEvent(event);
}

void wxHaikuWindow::WorkspaceActivated(int32 workspace, bool active)
{
    wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_WORKSPACE, m_handle);
    event->value = workspace;
    event->flag = active;
    wxHaikuPostEvent(event);
}
