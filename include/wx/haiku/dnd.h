/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dnd.h
// Purpose:     wxDropTarget and wxDropSource on BMessage drag and drop
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DND_H_
#define _WX_HAIKU_DND_H_

#if wxUSE_DRAG_AND_DROP

#include "wx/icon.h"

#define wxDROP_ICON(name)   wxICON(name)

class WXDLLIMPEXP_FWD_CORE wxWindow;

class WXDLLIMPEXP_CORE wxDropTarget : public wxDropTargetBase
{
public:
    wxDropTarget(wxDataObject *dataObject = NULL);

    virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) wxOVERRIDE;
    virtual bool OnDrop(wxCoord x, wxCoord y) wxOVERRIDE;
    virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) wxOVERRIDE;
    virtual bool GetData() wxOVERRIDE;

    // Can only be called during OnXXX methods.
    wxDataFormat GetMatchingPair();
};

class WXDLLIMPEXP_CORE wxDropSource : public wxDropSourceBase
{
public:
    // constructor. set data later with SetData()
    wxDropSource(wxWindow *win = NULL,
                 const wxIcon& copy = wxNullIcon,
                 const wxIcon& move = wxNullIcon,
                 const wxIcon& none = wxNullIcon);

    // constructor for setting one data object
    wxDropSource(wxDataObject& data,
                 wxWindow *win,
                 const wxIcon& copy = wxNullIcon,
                 const wxIcon& move = wxNullIcon,
                 const wxIcon& none = wxNullIcon);

    virtual ~wxDropSource();

    // set the icon corresponding to given drag result
    void SetIcon(wxDragResult res, const wxIcon& icon)
    {
        if ( res == wxDragCopy )
            m_iconCopy = icon;
        else if ( res == wxDragMove )
            m_iconMove = icon;
        else
            m_iconNone = icon;
    }

    // Starts the drag and returns when the mouse buttons are released. The
    // result is wxDragCopy if a drop target of this application took the
    // data and otherwise what was asked for (the other applications don't
    // tell what they did with it).
    virtual wxDragResult DoDragDrop(int flags = wxDrag_CopyOnly) wxOVERRIDE;

private:
    wxWindow *m_window;
    wxIcon m_iconCopy,
           m_iconMove,
           m_iconNone;
};

#endif // wxUSE_DRAG_AND_DROP

#endif // _WX_HAIKU_DND_H_
