// Bold text as BambuStudio makes it (WX-REQUESTS 27): wxFont(size, SWISS,
// NORMAL, BOLD, false, "HarmonyOS Sans SC") + SetFaceName(), as Label::sysFont
// does. Prints what the fonts resolve to and compares the width wx measures
// with the width of the ink drawn by wxDC and wxGraphicsContext. As
// BambuStudio does, the fonts are made once before wxEntry() too (there is no
// application then).
//   fonttest            prints and exits
//   fonttest --show     also shows the texts in a window
#include "wx/wx.h"
#include "wx/dcgraph.h"
#include "wx/graphics.h"

#include <Font.h>

#include <stdio.h>

namespace
{

const char *const TEXT = "General Settings";

// the right edge of what is not white, -1 if nothing is
int InkRight(const wxBitmap& bitmap)
{
    const wxImage image = bitmap.ConvertToImage();
    int right = -1;
    for ( int y = 0; y < image.GetHeight(); y++ )
        for ( int x = 0; x < image.GetWidth(); x++ )
            if ( image.GetRed(x, y) < 200 || image.GetGreen(x, y) < 200 )
                right = wxMax(right, x);
    return right;
}

wxFont BambuFont(int size, bool bold)
{
    const wxString face = wxString::FromUTF8("HarmonyOS Sans SC");
    wxFont font(size * 4 / 5, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, face);
    font.SetFaceName(face);
    if ( !font.IsOk() )
    {
        font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        if ( bold )
            font.MakeBold();
        font.SetPointSize(size * 4 / 5);
    }
    return font;
}

void Report(const char *name, const wxFont& font)
{
    const BFont *native = font.GetHaikuFont();
    font_family family;
    font_style style;
    native->GetFamilyAndStyle(&family, &style);

    wxBitmap bitmap(400, 60);
    int measured;
    {
        wxMemoryDC dc(bitmap);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        dc.SetFont(font);
        dc.SetTextForeground(*wxBLACK);
        measured = dc.GetTextExtent(TEXT).x;
        dc.DrawText(TEXT, 0, 0);
    }
    const int inkDC = InkRight(bitmap) + 1;

    wxBitmap bitmap2(400, 60);
    {
        wxMemoryDC dc(bitmap2);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        wxGCDC gcdc(dc);
        gcdc.SetFont(font);
        gcdc.SetTextForeground(*wxBLACK);
        gcdc.DrawText(TEXT, 0, 0);
    }
    const int inkGC = InkRight(bitmap2) + 1;

    printf("%-12s %s/%s face=0x%x falsebold=%.2f size=%.1f spacing=%d: "
           "measured %d, ink wxDC %d, ink wxGCDC %d\n", name, family, style,
           native->Face(), native->FalseBoldWidth(), native->Size(),
           (int)native->Spacing(), measured, inkDC, inkGC);
}

} // anonymous namespace

class App : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        const wxFont plain = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        wxFont bold = plain;
        bold.MakeBold();

        Report("gui", plain);
        Report("gui bold", bold);
        Report("bambu 14", BambuFont(14, false));
        Report("bambu 14 b", BambuFont(14, true));
        Report("bambu 18 b", BambuFont(18, true));
        fflush(stdout);

        if ( argc < 2 || argv[1] != "--show" )
            return false;

        wxFrame * const frame = new wxFrame(NULL, wxID_ANY, "fonttest",
                                            wxPoint(40, 40), wxSize(420, 260));
        wxPanel * const panel = new wxPanel(frame);
        wxBoxSizer * const sizer = new wxBoxSizer(wxVERTICAL);
        const wxFont fonts[] = { plain, bold, BambuFont(14, false),
                                 BambuFont(14, true), BambuFont(18, true) };
        for ( size_t n = 0; n < WXSIZEOF(fonts); n++ )
        {
            wxStaticText * const text = new wxStaticText(panel, wxID_ANY, TEXT);
            text->SetFont(fonts[n]);
            sizer->Add(text, wxSizerFlags().Border());
        }
        panel->SetSizer(sizer);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP_NO_MAIN(App);

int main(int argc, char **argv)
{
    // what BambuStudio's Label::initSysFont() does before wxEntry()
    const wxFont early[] = { BambuFont(14, false), BambuFont(14, true) };
    (void)early;

    return wxEntry(argc, argv);
}
