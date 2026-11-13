// Console test of the wxBase pieces of the Haiku port: prints the standard
// paths and the system information wx reports. Links wxBase only and creates
// no BApplication. Run it in the VM over ssh:
//   build/haiku/tests/build-config-test.sh basetest && \
//       build/haiku/vm/haiku.sh /Data/wx/basetest   (after copying it there)
#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/crt.h"
#include "wx/dynlib.h"
#include "wx/filename.h"
#include "wx/init.h"
#include "wx/mimetype.h"
#include "wx/platinfo.h"
#include "wx/power.h"
#include "wx/snglinst.h"
#include "wx/stdpaths.h"
#include "wx/stopwatch.h"
#include "wx/utils.h"

static void P(const char *what, const wxString& value)
{
    wxPrintf("%-36s %s\n", what, value);
}

int main(int argc, char **argv)
{
    wxInitializer init(argc, argv);
    if ( !init.IsOk() )
        return 1;

    if ( argc > 1 )
        wxTheApp->SetAppName(argv[1]);

    wxStandardPaths& sp = wxStandardPaths::Get();
    wxPrintf("--- wxStandardPaths (app name \"%s\")\n", wxTheApp->GetAppName());
    P("GetExecutablePath", sp.GetExecutablePath());
    P("GetInstallPrefix", sp.GetInstallPrefix());
    P("GetConfigDir", sp.GetConfigDir());
    P("GetUserConfigDir", sp.GetUserConfigDir());
    P("GetDataDir", sp.GetDataDir());
    P("GetLocalDataDir", sp.GetLocalDataDir());
    P("GetUserDataDir", sp.GetUserDataDir());
    P("GetUserLocalDataDir", sp.GetUserLocalDataDir());
    P("GetPluginsDir", sp.GetPluginsDir());
    P("GetResourcesDir", sp.GetResourcesDir());
    P("GetLocalizedResourcesDir(fr)", sp.GetLocalizedResourcesDir("fr"));
    P("GetLocalizedResourcesDir(fr, Messages)",
      sp.GetLocalizedResourcesDir("fr", wxStandardPaths::ResourceCat_Messages));
    P("GetDocumentsDir", sp.GetDocumentsDir());
    P("GetAppDocumentsDir", sp.GetAppDocumentsDir());
    P("GetTempDir", sp.GetTempDir());
    P("GetUserDir(Dir_Cache)", sp.GetUserDir(wxStandardPaths::Dir_Cache));
    P("GetUserDir(Dir_Documents)", sp.GetUserDir(wxStandardPaths::Dir_Documents));
    P("GetUserDir(Dir_Desktop)", sp.GetUserDir(wxStandardPaths::Dir_Desktop));
    P("GetUserDir(Dir_Downloads)", sp.GetUserDir(wxStandardPaths::Dir_Downloads));
    P("GetUserDir(Dir_Music)", sp.GetUserDir(wxStandardPaths::Dir_Music));
    P("GetUserDir(Dir_Pictures)", sp.GetUserDir(wxStandardPaths::Dir_Pictures));
    P("GetUserDir(Dir_Videos)", sp.GetUserDir(wxStandardPaths::Dir_Videos));
    P("MakeConfigFileName(app, Dot)",
      sp.MakeConfigFileName("app", wxStandardPaths::ConfigFileConv_Dot));
    P("MakeConfigFileName(app, Ext)",
      sp.MakeConfigFileName("app", wxStandardPaths::ConfigFileConv_Ext));
    P("wxFileName::GetTempDir", wxFileName::GetTempDir());
    P("wxGetHomeDir", wxGetHomeDir());
    P("wxGetCwd", wxGetCwd());

    wxPrintf("--- system\n");
    int major = -2, minor = -2, micro = -2;
    const wxOperatingSystemId os = wxGetOsVersion(&major, &minor, &micro);
    P("wxGetOsVersion", wxString::Format("id 0x%x (%s, family %s) %d.%d.%d",
        (unsigned)os,
        wxPlatformInfo::GetOperatingSystemIdName(os),
        wxPlatformInfo::GetOperatingSystemFamilyName(os),
        major, minor, micro));
    P("wxGetOsDescription", wxGetOsDescription());
    P("wxCheckOsVersion(1,0) / (2,0)", wxString::Format("%d / %d",
        wxCheckOsVersion(1, 0), wxCheckOsVersion(2, 0)));
    P("wxGetCpuArchitectureName", wxGetCpuArchitectureName());
    P("wxIsPlatform64Bit", wxIsPlatform64Bit() ? "yes" : "no");
    const wxPlatformInfo& pi = wxPlatformInfo::Get();
    P("wxPlatformInfo", wxString::Format("os \"%s\" (%s) %d.%d.%d, port %s%s, %s, %s",
        pi.GetOperatingSystemIdName(), pi.GetOperatingSystemDescription(),
        pi.GetOSMajorVersion(), pi.GetOSMinorVersion(), pi.GetOSMicroVersion(),
        pi.GetPortIdName(), pi.IsUsingUniversalWidgets() ? "/univ" : "",
        pi.GetBitnessName(), pi.GetEndiannessName()));
    P("wxGetFreeMemory", wxString::Format("%lld MiB",
        wxGetFreeMemory().GetValue() / (1024 * 1024)));
    wxDiskspaceSize_t total = 0, free = 0;
    const bool disk = wxGetDiskSpace("/boot/home", &total, &free);
    P("wxGetDiskSpace(/boot/home)", wxString::Format("%d: total %lld MiB, free %lld MiB",
        disk, total.GetValue() / (1024 * 1024), free.GetValue() / (1024 * 1024)));
    P("wxGetHostName / wxGetUserId", wxGetHostName() + " / " + wxGetUserId());
    P("wxGetPowerType / wxGetBatteryState", wxString::Format("%d / %d",
        (int)wxGetPowerType(), (int)wxGetBatteryState()));
    {
        wxPowerResourceBlocker blocker(wxPOWER_RESOURCE_SYSTEM, "test");
        P("wxPowerResourceBlocker", blocker.IsInEffect() ? "in effect" : "not in effect (stub)");
    }

    wxPrintf("--- wxDynamicLibrary\n");
    P("CanonicalizeName(foo)", wxDynamicLibrary::CanonicalizeName("foo"));
    P("CanonicalizeName(foo, wxDL_MODULE)",
      wxDynamicLibrary::CanonicalizeName("foo", wxDL_MODULE));
    P("GetDllExt", wxDynamicLibrary::GetDllExt());
    {
        const wxDynamicLibraryDetailsArray loaded = wxDynamicLibrary::ListLoaded();
        P("ListLoaded", wxString::Format("%u images", (unsigned)loaded.size()));
        for ( size_t n = 0; n < loaded.size() && n < 3; n++ )
        {
            void *addr = NULL; size_t len = 0;
            loaded[n].GetAddress(&addr, &len);
            wxPrintf("    %s (%s) at %p, %lu bytes, version \"%s\"\n",
                     loaded[n].GetName(), loaded[n].GetPath(), addr,
                     (unsigned long)len, loaded[n].GetVersion());
        }
        wxDynamicLibrary lib;
        wxLogNull noLog;
        // (the name a program is linked against: libz.so exists in develop/lib only)
        P("Load(libz.so.1)", lib.Load("libz.so.1", wxDL_VERBATIM | wxDL_NOW) ? "ok" : "FAILED");
        if ( lib.IsLoaded() )
            P("  HasSymbol(zlibVersion)", lib.HasSymbol("zlibVersion") ? "yes" : "no");
    }

    wxPrintf("--- wxMimeTypesManager\n");
    {
        wxStopWatch sw;
        static const char * const exts[] = { "txt", "png", "html", "3mf", "stl", "nonexistent" };
        for ( size_t n = 0; n < WXSIZEOF(exts); n++ )
        {
            wxString mime("-"), desc("-"), cmd("-");
            wxFileType * const ft = wxTheMimeTypesManager->GetFileTypeFromExtension(exts[n]);
            if ( ft )
            {
                ft->GetMimeType(&mime);
                ft->GetDescription(&desc);
                ft->GetOpenCommand(&cmd, wxFileType::MessageParameters("/tmp/x." + wxString(exts[n])));
                delete ft;
            }
            wxPrintf("    .%-12s %s: %s | %s | %s\n", exts[n], ft ? "found" : "none ", mime, desc, cmd);
        }
        wxFileType * const ft = wxTheMimeTypesManager->GetFileTypeFromMimeType("image/jpeg");
        wxArrayString extsFound;
        if ( ft )
            ft->GetExtensions(extsFound);
        P("GetFileTypeFromMimeType(image/jpeg)",
          ft ? "found, extensions: " + wxJoin(extsFound, ' ') : wxString("none"));
        delete ft;
        wxArrayString all;
        const size_t count = wxTheMimeTypesManager->EnumAllFileTypes(all);
        P("EnumAllFileTypes", wxString::Format("%u types", (unsigned)count));
        P("time for all of the above", wxString::Format("%ld ms", sw.Time()));
    }

    wxPrintf("--- wxSingleInstanceChecker\n");
    {
        wxSingleInstanceChecker first;
        P("first: IsAnotherRunning", first.IsAnotherRunning() ? "yes (WRONG)" : "no");
        wxSingleInstanceChecker second;
        // same process: the lock is ours, so this only shows that creating
        // the checker twice works; "--hold" lets a second process check
        P("second (same process)", second.IsAnotherRunning() ? "yes" : "no");

        if ( argc > 2 && strcmp(argv[2], "--hold") == 0 )
        {
            wxPrintf("holding the lock for 6 seconds\n");
            fflush(stdout);
            wxSleep(6);
        }
    }

    return 0;
}
