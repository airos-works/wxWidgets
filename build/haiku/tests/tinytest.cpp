// Windows laid out smaller than their minimal size, down to 0 pixels, and
// scrolled windows emptied with DestroyChildren(): what BambuStudio's
// MonitorPanel does at start-up (its HMSPanel's wxScrolledWindow, laid out
// 0x20 with a minimal size of 0x30, gets a vertical scroll bar and is then
// emptied). wxUniv's scroll bars are children of the window: they must not
// be left dangling. Must not crash; prints "done".
//   tinytest [W H]     the size the scrolled windows get (default 0 20)
#include "wx/wx.h"

#include <stdio.h>

class App : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        long w = 0, h = 20;
        if ( argc > 2 )
        {
            wxString(argv[1]).ToLong(&w);
            wxString(argv[2]).ToLong(&h);
        }

        wxFrame * const frame = new wxFrame(NULL, wxID_ANY, "tinytest",
                                            wxPoint(40, 40), wxSize(400, 300));
        wxPanel * const panel = new wxPanel(frame);

        const long styles[] = { wxHSCROLL | wxVSCROLL, wxVSCROLL, wxHSCROLL };
        for ( size_t n = 0; n < WXSIZEOF(styles); n++ )
        {
            wxScrolledWindow * const scrolled
                = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition,
                                       wxDefaultSize, styles[n]);
            scrolled->SetScrollRate(5, 5);
            scrolled->SetMinSize(wxSize(0, 30));
            wxBoxSizer * const sizer = new wxBoxSizer(wxVERTICAL);
            for ( int i = 0; i < 10; i++ )
                sizer->Add(new wxButton(scrolled, wxID_ANY,
                                        wxString::Format("Button %d", i)));
            scrolled->SetSizer(sizer);
            scrolled->FitInside();

            printf("style %ld: %ldx%ld\n", styles[n], w, h);
            fflush(stdout);
            scrolled->SetSize(0, 0, w, h);
            scrolled->Layout();
            scrolled->SetSize(0, 0, w, h + 1);
            scrolled->SetSize(0, 0, 0, 0);
            scrolled->SetSize(0, 0, 200, 100);
            scrolled->SetSize(0, 0, w, h);

            // as HMSPanel::delete_hms_panels(), then more layout
            const bool hadBar = scrolled->GetScrollbar(wxVERTICAL) != NULL
                                || scrolled->GetScrollbar(wxHORIZONTAL) != NULL;
            scrolled->DestroyChildren();
            printf("  DestroyChildren() with a scroll bar: %d, bars now %p %p\n",
                   (int)hadBar, (void *)scrolled->GetScrollbar(wxVERTICAL),
                   (void *)scrolled->GetScrollbar(wxHORIZONTAL));
            fflush(stdout);
            scrolled->SetSize(0, 0, w, h + 2);
            scrolled->SetSize(0, 0, 50, 10);
            scrolled->SetSize(0, 0, w, h);
            scrolled->FitInside();
            wxYield();
        }

        frame->Show();
        printf("done\n");
        fflush(stdout);
        return true;
    }
};

wxIMPLEMENT_APP(App);
