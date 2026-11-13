/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/stdpaths.h
// Purpose:     wxStandardPaths for Haiku
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_STDPATHS_H_
#define _WX_HAIKU_STDPATHS_H_

// ----------------------------------------------------------------------------
// wxStandardPaths
// ----------------------------------------------------------------------------

// The directories come from find_directory() and find_path(): nothing here
// needs a BApplication, the class is part of wxBase.
//
// An application is either installed from a package: the executable is in
// <prefix>/apps/<Name> (or <prefix>/apps/<Name>/..., <prefix>/bin) and its
// data in <prefix>/data/<name>, where <prefix> is an installation location
// (/boot/system, /boot/home/config and their non-packaged directories); or
// it is run from a directory of its own (a build tree, an application
// directory copied somewhere) which holds its data in "data" or "resources"
// next to the executable (or next to the "bin" directory with it).
class WXDLLIMPEXP_BASE wxStandardPaths : public wxStandardPathsBase
{
public:
    // implement base class pure virtuals
    virtual wxString GetExecutablePath() const wxOVERRIDE;

    // /boot/system/settings
    virtual wxString GetConfigDir() const wxOVERRIDE;

    // ~/config/settings
    virtual wxString GetUserConfigDir() const wxOVERRIDE;

    // the directory of the executable if it has a "data" or "resources"
    // directory, <prefix>/data/<appname> otherwise
    virtual wxString GetDataDir() const wxOVERRIDE;
    virtual wxString GetResourcesDir() const wxOVERRIDE { return GetDataDir(); }

    // /boot/system/non-packaged/data/<appname>
    virtual wxString GetLocalDataDir() const wxOVERRIDE;

    // ~/config/settings/<appname>: where Haiku applications keep what they
    // write for the user
    virtual wxString GetUserDataDir() const wxOVERRIDE;

    // ~/config/cache/<appname>
    virtual wxString GetUserLocalDataDir() const wxOVERRIDE;

    // "add-ons" (or "plugins") next to the executable if it exists,
    // <prefix>/add-ons/<appname> otherwise
    virtual wxString GetPluginsDir() const wxOVERRIDE;

    virtual wxString GetLocalizedResourcesDir(const wxString& lang,
                                              ResourceCat category) const wxOVERRIDE;

    // ~/Documents if it exists, the home directory otherwise
    virtual wxString GetDocumentsDir() const wxOVERRIDE;

    // TMPDIR if set, /boot/system/cache/tmp otherwise
    virtual wxString GetTempDir() const wxOVERRIDE;

    virtual wxString GetUserDir(Dir userDir) const wxOVERRIDE;

    // settings files have no leading dot on Haiku: "<basename>.conf"
    virtual wxString MakeConfigFileName(const wxString& basename,
                                        ConfigFileConv conv = ConfigFileConv_Ext
                                        ) const wxOVERRIDE;

    // The installation location (<prefix> above) the executable is in or an
    // empty string if it is not in one.
    wxString GetInstallPrefix() const;

protected:
    // Ctor is protected, use wxStandardPaths::Get() instead of instantiating
    // objects of this class directly.
    wxStandardPaths() { }

private:
    // the directory holding "data"/"resources" (or the given subdirectory)
    // of an application run from its own directory, empty if there is none
    wxString GetAppDirWith(const char *subdir1, const char *subdir2) const;
};

#endif // _WX_HAIKU_STDPATHS_H_
