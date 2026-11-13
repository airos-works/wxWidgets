/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/launch.cpp
// Purpose:     wxLaunchDefaultApplication() on BRoster
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/utils.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/intl.h"
#endif

#include <Entry.h>
#include <Roster.h>
#include <Url.h>

// Opens a file with the preferred application of its type (a directory in
// Tracker) or a URL with the handler of its scheme, as open(1) does.
bool wxLaunchDefaultApplication(const wxString& document, int WXUNUSED(flags))
{
    const wxScopedCharBuffer path = document.utf8_str();

    status_t status = B_ENTRY_NOT_FOUND;

    BEntry entry(path.data());
    if ( entry.InitCheck() == B_OK && entry.Exists() )
    {
        entry_ref ref;
        status = entry.GetRef(&ref);
        if ( status == B_OK )
            status = be_roster->Launch(&ref);
    }
    else if ( document.find(':') != wxString::npos )
    {
        BUrl url(path.data(), true);
        if ( url.IsValid() )
            status = url.OpenWithPreferredApplication();
    }

    // an application which runs already got the document as a message
    if ( status == B_OK || status == B_ALREADY_RUNNING )
        return true;

    wxLogDebug("wxLaunchDefaultApplication(\"%s\") failed: %s",
               document, wxString::FromUTF8(strerror(status)));

    return false;
}
