/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/notifmsg.cpp
// Purpose:     wxNotificationMessage on BNotification
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_NOTIFICATION_MESSAGE

#include "wx/notifmsg.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/icon.h"
#endif

#include "wx/private/notifmsg.h"

#include "wx/haiku/private.h"

#include <Notification.h>

// ----------------------------------------------------------------------------
// wxHaikuNotificationMsgImpl
// ----------------------------------------------------------------------------

// The notification is handed to the system's notification_server, which
// shows it with the other applications' notifications and hides it itself.
// What the server does not offer is not available: a notification cannot be
// closed by the application, has no actions and does not tell when the user
// clicked or dismissed it (the wxEVT_NOTIFICATION_MESSAGE_XXX events are
// never sent).
class wxHaikuNotificationMsgImpl : public wxNotificationMessageImpl
{
public:
    wxHaikuNotificationMsgImpl(wxNotificationMessageBase *notification)
        : wxNotificationMessageImpl(notification),
          m_flags(wxICON_INFORMATION)
    {
        // showing the same object again replaces what it showed before
        static unsigned long s_lastId = 0;
        m_id.Printf("wx-%lu-%lu", wxGetProcessId(), ++s_lastId);
    }

    virtual bool Show(int timeout) wxOVERRIDE
    {
        notification_type type = B_INFORMATION_NOTIFICATION;
        if ( m_flags & wxICON_ERROR )
            type = B_ERROR_NOTIFICATION;
        else if ( m_flags & wxICON_WARNING )
            type = B_IMPORTANT_NOTIFICATION;

        BNotification notification(type);
        if ( notification.InitCheck() != B_OK )
            return false;

        // the group is the heading the server collects the notifications of
        // an application under
        wxString group;
        if ( wxTheApp )
            group = wxTheApp->GetAppDisplayName();
        if ( !group.empty() )
            notification.SetGroup(BString(wxHaikuStr(group)));

        notification.SetTitle(BString(wxHaikuStr(m_title)));
        notification.SetContent(BString(wxHaikuStr(m_message)));
        notification.SetMessageID(BString(wxHaikuStr(m_id)));

        // the default is the icon of the application
        if ( m_icon.IsOk() )
        {
            const BBitmap * const bitmap = m_icon.GetHaikuDrawBitmap(true);
            if ( bitmap )
                notification.SetIcon(bitmap);
        }

        bigtime_t usecs = -1;           // the server's default
        if ( timeout == wxNotificationMessage::Timeout_Never )
            usecs = 24 * 3600 * 1000000LL;  // there is no "never"
        else if ( timeout > 0 )
            usecs = timeout * 1000000LL;

        return notification.Send(usecs) == B_OK;
    }

    virtual bool Close() wxOVERRIDE
    {
        // the server has no way to take a notification back
        return false;
    }

    virtual void SetTitle(const wxString& title) wxOVERRIDE
    {
        m_title = title;
    }

    virtual void SetMessage(const wxString& message) wxOVERRIDE
    {
        m_message = message;
    }

    virtual void SetParent(wxWindow *WXUNUSED(parent)) wxOVERRIDE
    {
    }

    virtual void SetFlags(int flags) wxOVERRIDE
    {
        m_flags = flags;
    }

    virtual void SetIcon(const wxIcon& icon) wxOVERRIDE
    {
        m_icon = icon;
    }

    virtual bool AddAction(wxWindowID WXUNUSED(actionid),
                           const wxString& WXUNUSED(label)) wxOVERRIDE
    {
        return false;
    }

private:
    wxString m_title, m_message, m_id;
    wxIcon m_icon;
    int m_flags;
};

// ----------------------------------------------------------------------------
// wxNotificationMessage
// ----------------------------------------------------------------------------

void wxNotificationMessage::Init()
{
    m_impl = new wxHaikuNotificationMsgImpl(this);
}

#endif // wxUSE_NOTIFICATION_MESSAGE
