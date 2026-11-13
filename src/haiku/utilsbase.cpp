/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/utilsbase.cpp
// Purpose:     wxBase utility functions which Haiku answers itself instead
//              of the generic Unix code (which runs uname(1) for them)
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// This file is part of wxBase: nothing here may need a BApplication.

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/utils.h"

#ifndef WX_PRECOMP
    #include "wx/string.h"
#endif

#include "wx/platinfo.h"

#include <OS.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

// ----------------------------------------------------------------------------
// operating system version
// ----------------------------------------------------------------------------

// The version is <major>.<minor>.<hrev>: uname() reports the release as
// "R1~beta6+development" (R1, no minor version) and the revision of the
// source tree in the version field ("hrev60207+2611 Nov 12 2026 ..."); the
// revision is what distinguishes two builds of a release.
wxOperatingSystemId wxGetOsVersion(int *verMaj, int *verMin, int *verMicro)
{
    int major = -1, minor = -1, micro = -1;

    struct utsname uts;
    if ( uname(&uts) == 0 )
    {
        const char *p = uts.release;
        while ( *p && !isdigit((unsigned char)*p) )
            p++;

        if ( *p )
        {
            char *end;
            major = (int)strtol(p, &end, 10);
            minor = 0;
            if ( *end == '.' && isdigit((unsigned char)end[1]) )
                minor = (int)strtol(end + 1, NULL, 10);

            micro = 0;
            p = strstr(uts.version, "hrev");
            if ( p )
                micro = (int)strtol(p + 4, NULL, 10);
        }
    }

    if ( verMaj )
        *verMaj = major;
    if ( verMin )
        *verMin = minor;
    if ( verMicro )
        *verMicro = micro;

    return wxOS_UNIX_HAIKU;
}

wxString wxGetOsDescription()
{
    wxString desc;

    struct utsname uts;
    if ( uname(&uts) != 0 )
        return "Haiku";

    desc << uts.sysname << ' ' << uts.release;

    // the revision without the build date following it
    const char * const rev = strstr(uts.version, "hrev");
    if ( rev )
    {
        desc << ' ';
        for ( const char *p = rev; *p && *p != ' '; p++ )
            desc << *p;
    }

    desc << ' ' << uts.machine;

    // air/OS, the distribution this port is made for, describes itself in a
    // small JSON file: mention its version if this is one.
    FILE * const fp = fopen("/boot/system/data/airos/release", "r");
    if ( fp )
    {
        wxString version;

        char buf[256];
        while ( fgets(buf, sizeof(buf), fp) )
        {
            const char *p = strstr(buf, "\"version\"");
            if ( !p )
                continue;

            p = strchr(p + 9, ':');
            if ( p )
                p = strchr(p, '"');
            if ( p )
            {
                const char * const end = strchr(++p, '"');
                if ( end )
                    version = wxString::FromUTF8(p, end - p);
            }
            break;
        }

        fclose(fp);

        desc = "air/OS " + version + (version.empty() ? "(" : " (") + desc + ")";
    }

    return desc;
}

bool wxCheckOsVersion(int majorVsn, int minorVsn, int microVsn)
{
    int majorCur, minorCur, microCur;
    wxGetOsVersion(&majorCur, &minorCur, &microCur);

    return majorCur > majorVsn
        || (majorCur == majorVsn && minorCur > minorVsn)
        || (majorCur == majorVsn && minorCur == minorVsn && microCur >= microVsn);
}
