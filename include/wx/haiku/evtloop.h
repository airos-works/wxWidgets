/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/evtloop.h
// Purpose:     wxGUIEventLoop for the native Haiku port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_EVTLOOP_H_
#define _WX_HAIKU_EVTLOOP_H_

#include "wx/unix/evtloop.h"

// The loop waits in the wxFDIODispatcher shared with timers and sockets: the
// looper threads wake it through a pipe when they queue an event record.
class WXDLLIMPEXP_CORE wxGUIEventLoop : public wxConsoleEventLoop
{
public:
    wxGUIEventLoop();

    virtual bool Pending() const wxOVERRIDE;
    virtual bool Dispatch() wxOVERRIDE;
    virtual int DispatchTimeout(unsigned long timeout) wxOVERRIDE;

protected:
    virtual void DoYieldFor(long eventsToProcess) wxOVERRIDE;

private:
    wxDECLARE_NO_COPY_CLASS(wxGUIEventLoop);
};

#endif // _WX_HAIKU_EVTLOOP_H_
