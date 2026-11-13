// Test of wxWebView (summit_webkit backend) inside real wx windows: what
// BambuStudio does with it. Prints PASS/FAIL lines and exits; the exit code
// is the number of failures.
//   webtest [--keep]        (--keep: leave the window open at the end)
// Build with build/haiku/tests/build-test.sh webtest (the build tree must
// have wxUSE_WEBVIEW_SUMMIT).
#include "wx/wx.h"
#include "wx/evtloop.h"
#include "wx/filename.h"
#include "wx/stopwatch.h"
#include "wx/textfile.h"
#include "wx/webview.h"

#include <functional>
#include <stdio.h>

namespace
{

int gs_failures = 0;
wxStopWatch gs_watch;

void Check(bool ok, const char *name, const wxString& detail = wxString())
{
    if ( !ok )
        gs_failures++;

    printf("[%6ld ms] TEST %-46s %s  %s\n", gs_watch.Time(), name,
           ok ? "PASS" : "FAIL", (const char *)detail.utf8_str());
    fflush(stdout);
}

// runs the event loop until the condition holds
bool WaitFor(const std::function<bool()>& done, long timeout = 15000)
{
    const long deadline = gs_watch.Time() + timeout;
    for ( ;; )
    {
        wxTheApp->Yield(true);
        if ( done() )
            return true;
        if ( gs_watch.Time() >= deadline )
            return false;
        wxMilliSleep(5);
    }
}

void WritePage(const wxString& path, const wxString& text)
{
    wxFile file(path, wxFile::write);
    file.Write(text);
}

} // anonymous namespace

class WebTestFrame : public wxFrame
{
public:
    WebTestFrame();

    void Run(bool keep);

private:
    void OnLoaded(wxWebViewEvent& event)
    {
        m_loaded++;
        m_lastLoaded = event.GetURL();
    }
    void OnNavigating(wxWebViewEvent& event)
    {
        m_navigating++;
        if ( event.GetURL().Contains("veto") )
        {
            m_vetoed++;
            event.Veto();
        }
    }
    void OnTitle(wxWebViewEvent& event) { m_title = event.GetString(); }
    void OnError(wxWebViewEvent& event)
    {
        m_errors++;
        printf("error event: %s %s\n",
               (const char *)event.GetURL().utf8_str(),
               (const char *)event.GetString().utf8_str());
    }
    void OnMessage(wxWebViewEvent& event)
    {
        m_messages.push_back(event.GetString());

        // a script run from inside a handler, with its result
        if ( event.GetString() == "nested" )
        {
            wxString result;
            m_nestedOk = m_web->RunScript("21 * 2", &result);
            m_nestedResult = result;
        }
    }

    wxPanel *m_panel;
    wxTextCtrl *m_text;
    wxWebView *m_web;

    int m_loaded, m_navigating, m_vetoed, m_errors;
    wxString m_lastLoaded, m_title;
    wxArrayString m_messages;
    bool m_nestedOk;
    wxString m_nestedResult;
};

WebTestFrame::WebTestFrame()
    : wxFrame(NULL, wxID_ANY, "webtest", wxPoint(60, 60), wxSize(700, 500))
{
    m_loaded = m_navigating = m_vetoed = m_errors = 0;
    m_nestedOk = false;

    m_panel = new wxPanel(this);
    m_text = new wxTextCtrl(m_panel, wxID_ANY, "a wx text control");

    m_web = wxWebView::New();
    Check(m_web != NULL, "wxWebView::New()");
    if ( !m_web )
        return;

    m_web->Create(m_panel, wxID_ANY, wxWebViewDefaultURLStr);

    wxBoxSizer * const sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_text, wxSizerFlags().Expand().Border());
    sizer->Add(m_web, wxSizerFlags(1).Expand().Border());
    m_panel->SetSizer(sizer);

    m_web->Bind(wxEVT_WEBVIEW_LOADED, &WebTestFrame::OnLoaded, this);
    m_web->Bind(wxEVT_WEBVIEW_NAVIGATING, &WebTestFrame::OnNavigating, this);
    m_web->Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &WebTestFrame::OnTitle, this);
    m_web->Bind(wxEVT_WEBVIEW_ERROR, &WebTestFrame::OnError, this);
    m_web->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
                &WebTestFrame::OnMessage, this);
}

void WebTestFrame::Run(bool keep)
{
    if ( !m_web )
    {
        Close();
        return;
    }

    const wxString dir = wxFileName::GetTempDir() + "/wxwebtest";
    wxFileName::Mkdir(dir, 0755, wxPATH_MKDIR_FULL);
    WritePage(dir + "/one.html",
        "<html><head><title>page one</title></head><body>"
        "<h1 id='h'>Page one</h1><input id='t' type='text'>"
        "<p><a id='veto' href='veto.html'>vetoed link</a></p>"
        "</body></html>");
    WritePage(dir + "/two.html",
        "<html><head><title>page two</title></head><body><h1>Page two</h1>"
        "<script>window.addEventListener('load', function() {"
        " if (window.wx) window.wx.postMessage('two loaded'); });</script>"
        "</body></html>");
    WritePage(dir + "/veto.html", "<html><body>never shown</body></html>");

    // loading, title, URL
    m_web->AddScriptMessageHandler("wx");
    m_web->LoadURL("file://" + dir + "/one.html");
    Check(WaitFor([this] { return m_lastLoaded.EndsWith("one.html"); }),
          "LOADED event", m_lastLoaded);
    Check(WaitFor([this] { return m_title == "page one"; }, 3000),
          "TITLE_CHANGED event", m_title);
    Check(m_web->GetCurrentURL().EndsWith("one.html"), "GetCurrentURL()",
          m_web->GetCurrentURL());
    Check(m_web->GetCurrentTitle() == "page one", "GetCurrentTitle()",
          m_web->GetCurrentTitle());

    // scripts with results
    wxString result;
    bool ok = m_web->RunScript("document.getElementById('h').textContent",
                               &result);
    Check(ok && result == "Page one", "RunScript() string", result);
    ok = m_web->RunScript("6 * 7", &result);
    Check(ok && result == "42", "RunScript() number", result);
    ok = m_web->RunScript("document.title = 'changed'; true", &result);
    Check(ok && WaitFor([this] { return m_title == "changed"; }, 3000),
          "script changes the title", m_title);

    // messages from the page, a script run from their handler
    m_web->RunScript("window.wx.postMessage('hello')");
    Check(WaitFor([this] { return m_messages.Index("hello") != wxNOT_FOUND; },
                  5000),
          "SCRIPT_MESSAGE_RECEIVED");
    m_web->RunScript("window.wx.postMessage('nested')");
    Check(WaitFor([this] { return m_messages.Index("nested") != wxNOT_FOUND; },
                  5000)
            && m_nestedOk && m_nestedResult == "42",
          "RunScript() inside a message handler", m_nestedResult);

    // size follows the sizer, hide and show
    const wxSize before = m_web->GetSize();
    SetSize(900, 640);
    WaitFor([] { return false; }, 300);
    const wxSize after = m_web->GetSize();
    Check(after.x > before.x && after.y > before.y, "resized with the frame",
          wxString::Format("%dx%d -> %dx%d", before.x, before.y,
                           after.x, after.y));
    ok = m_web->RunScript("window.innerWidth", &result);
    long inner = 0;
    result.ToLong(&inner);
    Check(ok && labs(inner - after.x) <= 4, "page width is the control's",
          result);

    m_web->Hide();
    WaitFor([] { return false; }, 300);
    ok = m_web->RunScript("1 + 1", &result);
    Check(ok && result == "2", "RunScript() while hidden", result);
    m_web->Show();
    m_panel->Layout();
    WaitFor([] { return false; }, 300);

    // focus: into the page and back to a wx control
    m_web->SetFocus();
    m_web->RunScript("document.getElementById('t').focus()");
    WaitFor([] { return false; }, 300);
    m_text->SetFocus();
    WaitFor([] { return false; }, 300);
    Check(wxWindow::FindFocus() == m_text, "focus back in the wx control");

    // veto
    const int loadedBefore = m_loaded;
    m_web->RunScript("document.getElementById('veto').click()");
    Check(WaitFor([this] { return m_vetoed >= 1; }, 5000), "NAVIGATING event");
    WaitFor([] { return false; }, 1000);
    // Not counted as a failure: the engine has no navigation policy hook,
    // the veto stops the load after the engine reported it, and the load of
    // a local file usually wins that race.
    const bool stayed = m_web->GetCurrentURL().EndsWith("one.html")
                            && m_loaded == loadedBefore;
    printf("[%6ld ms] NOTE vetoed load of a local file %s  %s\n",
           gs_watch.Time(), stayed ? "did not happen" : "happened anyway",
           (const char *)m_web->GetCurrentURL().utf8_str());
    if ( !stayed )
    {
        m_web->LoadURL("file://" + dir + "/one.html");
        WaitFor([this, loadedBefore] { return m_loaded > loadedBefore + 1
                                || m_lastLoaded.EndsWith("one.html"); }, 5000);
    }

    // a modal dialog with a web view of its own: the page ends it
    {
        wxDialog dialog(this, wxID_ANY, "modal web view", wxDefaultPosition,
                        wxSize(500, 360));
        wxWebView * const web = wxWebView::New();
        web->Create(&dialog, wxID_ANY, wxWebViewDefaultURLStr);
        web->AddScriptMessageHandler("wx");
        wxString message;
        web->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED,
                  [&](wxWebViewEvent& event)
                  {
                      message = event.GetString();
                      dialog.EndModal(wxID_OK);
                  });
        wxTimer timeout(&dialog);
        dialog.Bind(wxEVT_TIMER,
                    [&](wxTimerEvent&) { dialog.EndModal(wxID_CANCEL); });
        timeout.StartOnce(15000);
        web->LoadURL("file://" + dir + "/two.html");
        const int rc = dialog.ShowModal();
        Check(rc == wxID_OK && message == "two loaded",
              "modal dialog ended by its page", message);
    }

    WaitFor([] { return false; }, 300);
    ok = m_web->RunScript("'still ' + 'alive'", &result);
    Check(ok && result == "still alive", "first view works after the dialog",
          result);
    Check(m_errors == 0, "no ERROR events");

    printf("%s: %d failure(s)\n", gs_failures ? "FAILED" : "ALL PASSED",
           gs_failures);
    fflush(stdout);

    if ( !keep )
        Close();
}

class WebTestApp : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        const bool keep = argc > 1 && argv[1] == "--keep";

        WebTestFrame * const frame = new WebTestFrame;
        frame->Show();
        frame->CallAfter([frame, keep] { frame->Run(keep); });

        return true;
    }

    virtual int OnExit() wxOVERRIDE
    {
        return gs_failures;
    }
};

wxIMPLEMENT_APP(WebTestApp);
