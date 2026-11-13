// The message box icons of the art provider as BambuStudio's own message
// dialog shows them (WX-REQUESTS 15, 19): wxStaticBitmap of
// wxArtProvider::GetBitmap(id, wxART_MESSAGE_BOX[, size]), next to the icon
// as wxArtProvider::GetIcon() gives it. Prints what the bitmaps are.
#include "wx/wx.h"
#include "wx/artprov.h"

#include <stdio.h>

class App : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        wxFrame * const frame = new wxFrame(NULL, wxID_ANY, "arttest",
                                            wxPoint(40, 40), wxSize(520, 300));
        wxPanel * const panel = new wxPanel(frame);
        wxFlexGridSizer * const sizer = new wxFlexGridSizer(4, 8, 8);

        const wxArtID ids[] = { wxART_INFORMATION, wxART_WARNING,
                                wxART_ERROR, wxART_QUESTION };
        for ( size_t n = 0; n < WXSIZEOF(ids); n++ )
        {
            const wxBitmap plain = wxArtProvider::GetBitmap(ids[n],
                                                            wxART_MESSAGE_BOX);
            const wxBitmap sized = wxArtProvider::GetBitmap(ids[n],
                                        wxART_MESSAGE_BOX, wxSize(48, 48));
            const wxIcon icon = wxArtProvider::GetIcon(ids[n],
                                                       wxART_MESSAGE_BOX);

            printf("%-20s default %dx%d alpha=%d, 48: %dx%d alpha=%d, "
                   "icon %dx%d\n", (const char *)ids[n].utf8_str(),
                   plain.GetWidth(), plain.GetHeight(), (int)plain.HasAlpha(),
                   sized.GetWidth(), sized.GetHeight(), (int)sized.HasAlpha(),
                   icon.GetWidth(), icon.GetHeight());

            sizer->Add(new wxStaticText(panel, wxID_ANY, ids[n]),
                       wxSizerFlags().Center());
            sizer->Add(new wxStaticBitmap(panel, wxID_ANY, plain));
            sizer->Add(new wxStaticBitmap(panel, wxID_ANY, sized));
            sizer->Add(new wxStaticBitmap(panel, wxID_ANY, icon));
        }

        // created without a bitmap and given one later, as BambuStudio's
        // message dialog does: must be as large as the bitmap
        wxStaticBitmap * const later = new wxStaticBitmap(panel, wxID_ANY,
                                                          wxNullBitmap);
        sizer->Add(new wxStaticText(panel, wxID_ANY, "set later"),
                   wxSizerFlags().Center());
        sizer->Add(later);
        later->SetBitmap(wxArtProvider::GetBitmap(wxART_ERROR,
                                                  wxART_MESSAGE_BOX,
                                                  wxSize(64, 64)));
        printf("set later: control %dx%d\n",
               later->GetSize().x, later->GetSize().y);
        fflush(stdout);

        panel->SetSizer(sizer);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(App);
