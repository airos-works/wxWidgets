// Test of single launch applications: the second launch passes its arguments
// to the running instance and exits.
//   singletest --make-single     marks the executable B_SINGLE_LAUNCH
//   singletest [FILE...]         shows a window, prints the files it is asked
//                                to open (by this and by later launches)
// Build with build/haiku/tests/build-test.sh singletest.
#include "wx/wx.h"

#include <AppFileInfo.h>
#include <File.h>
#include <Roster.h>

#include <stdio.h>

class SingleApp : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE;
    virtual void MacOpenFiles(const wxArrayString& fileNames) wxOVERRIDE;

private:
    wxTextCtrl *m_text;
};

wxIMPLEMENT_APP(SingleApp);

bool SingleApp::OnInit()
{
    m_text = NULL;

    if ( argc > 1 && argv[1] == "--make-single" )
    {
        BFile file(argv[0].utf8_str(), B_READ_WRITE);
        BAppFileInfo info(&file);
        status_t status = info.InitCheck();
        if ( status == B_OK )
            status = info.SetSignature("application/x-vnd.wx-singletest");
        if ( status == B_OK )
            status = info.SetAppFlags(B_SINGLE_LAUNCH);
        printf("make-single: %s\n", strerror(status));
        return false;
    }

    printf("started, signature %s\n",
           (const char *)GetHaikuSignature().utf8_str());

    wxFrame * const frame = new wxFrame(NULL, wxID_ANY, "singletest",
                                        wxDefaultPosition, wxSize(500, 300));
    m_text = new wxTextCtrl(frame, wxID_ANY, "", wxDefaultPosition,
                            wxDefaultSize, wxTE_MULTILINE);
    frame->Show();

    wxArrayString files;
    for ( int n = 1; n < argc; n++ )
        files.push_back(argv[n]);
    if ( !files.empty() )
        MacOpenFiles(files);

    fflush(stdout);

    return true;
}

void SingleApp::MacOpenFiles(const wxArrayString& fileNames)
{
    for ( size_t n = 0; n < fileNames.size(); n++ )
    {
        printf("open: %s\n", (const char *)fileNames[n].utf8_str());
        if ( m_text )
            m_text->AppendText(fileNames[n] + "\n");
    }

    fflush(stdout);
}
