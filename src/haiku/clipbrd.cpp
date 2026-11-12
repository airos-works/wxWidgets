/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/clipbrd.cpp
// Purpose:     wxClipboard on be_clipboard
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CLIPBOARD

#include "wx/clipbrd.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/dataobj.h"
#endif

#include "wx/haiku/private.h"

#include <Clipboard.h>

wxIMPLEMENT_DYNAMIC_CLASS(wxClipboard, wxObject);

wxClipboard::wxClipboard()
    : m_open(false)
{
}

wxClipboard::~wxClipboard()
{
}

bool wxClipboard::Open()
{
    wxCHECK_MSG( !m_open, false, wxS("clipboard already open") );

    // be_clipboard is locked for every operation, not while the wx clipboard
    // is "open": nothing else of the system waits for wx code then
    m_open = true;
    return true;
}

void wxClipboard::Close()
{
    m_open = false;
}

bool wxClipboard::IsOpened() const
{
    return m_open;
}

void wxClipboard::Clear()
{
    if ( !be_clipboard || !be_clipboard->Lock() )
        return;

    be_clipboard->Clear();
    be_clipboard->Commit();
    be_clipboard->Unlock();
}

bool wxClipboard::SetData(wxDataObject *data)
{
    wxCHECK_MSG( data, false, wxS("data is invalid") );

    if ( be_clipboard && be_clipboard->Lock() )
    {
        be_clipboard->Clear();
        be_clipboard->Unlock();
    }

    return AddData(data);
}

bool wxClipboard::AddData(wxDataObject *data)
{
    wxCHECK_MSG( data, false, wxS("data is invalid") );

    // the data is copied to the system clipboard right away: the object is
    // not needed any more
    bool ok = false;
    if ( be_clipboard && be_clipboard->Lock() )
    {
        BMessage * const clip = be_clipboard->Data();
        if ( clip )
        {
            ok = data->HaikuAddToMessage(clip);
            be_clipboard->Commit();
        }

        be_clipboard->Unlock();
    }

    delete data;

    return ok;
}

bool wxClipboard::IsSupported(const wxDataFormat& format)
{
    bool supported = false;
    if ( be_clipboard && be_clipboard->Lock() )
    {
        supported = wxDataObject::HaikuMessageHasFormat(be_clipboard->Data(),
                                                        format);
        be_clipboard->Unlock();
    }

    return supported;
}

bool wxClipboard::GetData(wxDataObject& data)
{
    bool ok = false;
    if ( be_clipboard && be_clipboard->Lock() )
    {
        ok = data.HaikuSetFromMessage(be_clipboard->Data());
        be_clipboard->Unlock();
    }

    return ok;
}

#endif // wxUSE_CLIPBOARD
