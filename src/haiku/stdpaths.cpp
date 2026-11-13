/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/stdpaths.cpp
// Purpose:     wxStandardPaths for Haiku
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// This file is part of wxBase: it must not use anything that needs a
// BApplication (find_directory(), find_path() and the image functions are
// in libroot).

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_STDPATHS

#include "wx/stdpaths.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/utils.h"
#endif //WX_PRECOMP

#include "wx/filename.h"

#include <FindDirectory.h>
#include <image.h>

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

namespace
{

wxString FindDirectory(directory_which which, bool create = false)
{
    char path[PATH_MAX];
    if ( find_directory(which, -1, create, path, sizeof(path)) != B_OK )
        return wxString();

    return wxString::FromUTF8(path);
}

// a directory of the installation location the executable is in
wxString FindAppPath(path_base_directory base, const wxString& subPath = wxString())
{
    char path[PATH_MAX];
    const wxScopedCharBuffer sub = subPath.utf8_str();
    if ( find_path(B_APP_IMAGE_SYMBOL, base,
                   subPath.empty() ? NULL : sub.data(),
                   path, sizeof(path)) != B_OK )
        return wxString();

    return wxString::FromUTF8(path);
}

wxString GetAppName()
{
    wxString name;
    if ( wxTheApp )
        name = wxTheApp->GetAppName();

    if ( name.empty() )
        name = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetName();

    return name;
}

// <home>/<name> if it exists
wxString HomeSubDir(const char *name)
{
    const wxString dir = FindDirectory(B_USER_DIRECTORY) + "/" + name;
    return wxFileName::DirExists(dir) ? dir : wxString();
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// wxStandardPaths
// ----------------------------------------------------------------------------

wxString wxStandardPaths::GetExecutablePath() const
{
    wxString path;

    image_info info;
    int32 cookie = 0;
    while ( get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK )
    {
        if ( info.type == B_APP_IMAGE )
        {
            path = wxString::FromUTF8(info.name);
            break;
        }
    }

    if ( path.empty() )
        return wxStandardPathsBase::GetExecutablePath();

    // the name is the path the program was started with
    if ( !path.StartsWith("/") )
    {
        char real[PATH_MAX];
        if ( realpath(path.utf8_str(), real) )
            path = wxString::FromUTF8(real);
    }

    return path;
}

wxString wxStandardPaths::GetInstallPrefix() const
{
    return FindAppPath(B_FIND_PATH_INSTALLATION_LOCATION_DIRECTORY);
}

wxString
wxStandardPaths::GetAppDirWith(const char *subdir1, const char *subdir2) const
{
    wxString dir = wxFileName(GetExecutablePath()).GetPath();

    // the executable's directory, then its parent if the executable is in
    // "bin" (the layout of applications ported from other systems)
    for ( int n = 0; n < 2 && !dir.empty(); n++ )
    {
        if ( wxFileName::DirExists(dir + "/" + subdir1)
                || (subdir2 && wxFileName::DirExists(dir + "/" + subdir2)) )
            return dir;

        if ( !dir.EndsWith("/bin") )
            break;

        dir.erase(dir.length() - 4);
    }

    return wxString();
}

wxString wxStandardPaths::GetConfigDir() const
{
    return FindDirectory(B_SYSTEM_SETTINGS_DIRECTORY);
}

wxString wxStandardPaths::GetUserConfigDir() const
{
    return FindDirectory(B_USER_SETTINGS_DIRECTORY, true);
}

wxString wxStandardPaths::GetDataDir() const
{
    // as on the other Unix systems the location can be overridden with
    // WX_APPNAME_DATA_DIR, which is handy for running a program which is
    // not installed
    wxString dir;
    if ( wxGetEnv("WX_" + GetAppName().Upper() + "_DATA_DIR", &dir)
            && !dir.empty() )
        return dir;

    dir = GetAppDirWith("data", "resources");
    if ( !dir.empty() )
        return dir;

    // The package layout. Data directories are usually named like the
    // package, in lower case: prefer an existing directory with the exact
    // application name to that.
    const wxString name = GetAppName();
    dir = FindAppPath(B_FIND_PATH_DATA_DIRECTORY);
    if ( dir.empty() )
    {
        // not in an installation location and no data next to the
        // executable: there is nothing better than its directory
        return wxFileName(GetExecutablePath()).GetPath();
    }

    if ( wxFileName::DirExists(dir + "/" + name) )
        return dir + "/" + name;

    return dir + "/" + name.Lower();
}

wxString wxStandardPaths::GetLocalDataDir() const
{
    return AppendAppInfo(FindDirectory(B_SYSTEM_NONPACKAGED_DATA_DIRECTORY));
}

wxString wxStandardPaths::GetUserDataDir() const
{
    return AppendAppInfo(FindDirectory(B_USER_SETTINGS_DIRECTORY, true));
}

wxString wxStandardPaths::GetUserLocalDataDir() const
{
    return AppendAppInfo(FindDirectory(B_USER_CACHE_DIRECTORY, true));
}

wxString wxStandardPaths::GetPluginsDir() const
{
    wxString dir = GetAppDirWith("add-ons", "plugins");
    if ( !dir.empty() )
    {
        return wxFileName::DirExists(dir + "/add-ons") ? dir + "/add-ons"
                                                        : dir + "/plugins";
    }

    const wxString name = GetAppName();
    dir = FindAppPath(B_FIND_PATH_ADD_ONS_DIRECTORY);
    if ( dir.empty() )
        return wxFileName(GetExecutablePath()).GetPath();

    if ( wxFileName::DirExists(dir + "/" + name) )
        return dir + "/" + name;

    return dir + "/" + name.Lower();
}

wxString
wxStandardPaths::GetLocalizedResourcesDir(const wxString& lang,
                                          ResourceCat category) const
{
    if ( category != ResourceCat_Messages )
        return wxStandardPathsBase::GetLocalizedResourcesDir(lang, category);

    // message catalogs: <dir>/locale/<lang>/LC_MESSAGES, where <dir> is the
    // application's own directory or the data directory of its package
    // (<prefix>/data/locale is where packages install them)
    wxString dir = GetAppDirWith("locale", NULL);
    if ( dir.empty() )
        dir = GetAppDirWith("data", "resources");
    if ( dir.empty() )
        dir = FindAppPath(B_FIND_PATH_DATA_DIRECTORY);
    if ( dir.empty() )
        dir = wxFileName(GetExecutablePath()).GetPath();

    return dir + "/locale/" + lang + "/LC_MESSAGES";
}

wxString wxStandardPaths::GetDocumentsDir() const
{
    return GetUserDir(Dir_Documents);
}

wxString wxStandardPaths::GetTempDir() const
{
    wxString dir;
    if ( wxGetEnv("TMPDIR", &dir) && wxFileName::DirExists(dir) )
        return dir;

    dir = FindDirectory(B_SYSTEM_TEMP_DIRECTORY, true);
    if ( dir.empty() )
        dir = wxFileName::GetTempDir();

    return dir;
}

wxString wxStandardPaths::GetUserDir(Dir userDir) const
{
    wxString dir;

    switch ( userDir )
    {
        case Dir_Cache:
            dir = FindDirectory(B_USER_CACHE_DIRECTORY, true);
            break;

        case Dir_Desktop:
            dir = FindDirectory(B_DESKTOP_DIRECTORY, true);
            break;

        // Haiku has no fixed directories for these: use the conventional
        // ones in the home directory if the user has them.
        case Dir_Documents:
            dir = HomeSubDir("Documents");
            break;

        case Dir_Downloads:
            dir = HomeSubDir("Downloads");
            if ( dir.empty() )
            {
                // where the system's browser puts downloads by default
                dir = FindDirectory(B_DESKTOP_DIRECTORY, true);
            }
            break;

        case Dir_Music:
            dir = HomeSubDir("Music");
            break;

        case Dir_Pictures:
            dir = HomeSubDir("Pictures");
            break;

        case Dir_Videos:
            dir = HomeSubDir("Videos");
            break;
    }

    if ( dir.empty() )
        dir = FindDirectory(B_USER_DIRECTORY);
    if ( dir.empty() )
        dir = wxFileName::GetHomeDir();

    return dir;
}

wxString
wxStandardPaths::MakeConfigFileName(const wxString& basename,
                                    ConfigFileConv WXUNUSED(conv)) const
{
    // Neither a leading dot (files in the settings directory are not
    // hidden) nor the bare name, which is the name of the directory
    // GetUserDataDir() returns.
    return basename + wxS(".conf");
}

#endif // wxUSE_STDPATHS
