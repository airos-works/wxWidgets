// Checks that alpha survives the trips BambuStudio makes it take:
// wxImage -> wxBitmap -> (wxMemoryDC / wxGraphicsContext drawing on a bitmap
// with alpha) -> wxImage. Prints one line per check and "ALL PASSED" or the
// number of failures; needs no interaction.
#include "wx/wx.h"
#include "wx/dcmemory.h"
#include "wx/graphics.h"
#include "wx/dcgraph.h"
#include "wx/rawbmp.h"

static int gs_failures = 0;

static void Check(bool ok, const wxString& what)
{
    wxPrintf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if ( !ok )
        gs_failures++;
    fflush(stdout);
}

struct RGBA
{
    int r, g, b, a;
};

static RGBA PixelOf(const wxImage& img, int x, int y)
{
    RGBA p;
    p.r = img.GetRed(x, y);
    p.g = img.GetGreen(x, y);
    p.b = img.GetBlue(x, y);
    p.a = img.HasAlpha() ? img.GetAlpha(x, y) : 255;
    return p;
}

static bool Near(const RGBA& p, int r, int g, int b, int a, int tol = 3)
{
    return abs(p.r - r) <= tol && abs(p.g - g) <= tol
        && abs(p.b - b) <= tol && abs(p.a - a) <= tol;
}

static wxString Str(const RGBA& p)
{
    return wxString::Format("(%d,%d,%d,%d)", p.r, p.g, p.b, p.a);
}

// an image with a red square of the given alpha in the middle of nothing
static wxImage MakeIcon(int size, unsigned char alpha)
{
    wxImage img(size, size);
    img.InitAlpha();
    for ( int y = 0; y < size; y++ )
    {
        for ( int x = 0; x < size; x++ )
        {
            const bool in = x >= size / 4 && x < 3 * size / 4
                                && y >= size / 4 && y < 3 * size / 4;
            img.SetRGB(x, y, in ? 255 : 0, 0, 0);
            img.SetAlpha(x, y, in ? alpha : 0);
        }
    }
    return img;
}

class TestApp : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        // 1. image -> bitmap -> image
        {
            const wxImage src = MakeIcon(16, 128);
            const wxBitmap bmp(src);
            Check(bmp.IsOk() && bmp.HasAlpha() && bmp.GetDepth() == 32,
                  "bitmap from image with alpha has alpha");
            const wxImage back = bmp.ConvertToImage();
            Check(back.HasAlpha(), "round trip keeps the alpha channel");
            RGBA in = PixelOf(back, 8, 8), out = PixelOf(back, 1, 1);
            Check(Near(in, 255, 0, 0, 128, 0), "round trip inside " + Str(in));
            Check(out.a == 0, "round trip outside " + Str(out));
        }

        // 2. a transparent 32 bit bitmap stays transparent where nothing is
        //    drawn, opaque drawing makes opaque pixels
        {
            wxBitmap bmp(32, 32, 32);
            {
                wxMemoryDC dc(bmp);
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(*wxBLUE_BRUSH);
                dc.DrawRectangle(8, 8, 16, 16);
            }
            const wxImage img = bmp.ConvertToImage();
            RGBA in = PixelOf(img, 16, 16), out = PixelOf(img, 2, 2);
            Check(Near(in, 0, 0, 255, 255), "wxMemoryDC opaque fill " + Str(in));
            Check(out.a == 0, "wxMemoryDC untouched pixel " + Str(out));
        }

        // 3. drawing an alpha bitmap into a transparent bitmap: source over
        {
            const wxBitmap icon(MakeIcon(16, 128));
            wxBitmap bmp(32, 32, 32);
            {
                wxMemoryDC dc(bmp);
                dc.DrawBitmap(icon, 8, 8, true);
            }
            const wxImage img = bmp.ConvertToImage();
            RGBA in = PixelOf(img, 16, 16), edge = PixelOf(img, 9, 9),
                 out = PixelOf(img, 2, 2);
            Check(Near(in, 255, 0, 0, 128), "DrawBitmap alpha on transparent " + Str(in));
            Check(edge.a == 0, "DrawBitmap transparent part " + Str(edge));
            Check(out.a == 0, "DrawBitmap outside " + Str(out));
        }

        // 4. the same on an opaque white background: blended, opaque result
        {
            const wxBitmap icon(MakeIcon(16, 128));
            wxBitmap bmp(32, 32, 24);
            {
                wxMemoryDC dc(bmp);
                dc.SetBackground(*wxWHITE_BRUSH);
                dc.Clear();
                dc.DrawBitmap(icon, 8, 8, true);
            }
            const wxImage img = bmp.ConvertToImage();
            RGBA in = PixelOf(img, 16, 16), out = PixelOf(img, 2, 2);
            Check(Near(in, 255, 127, 127, 255, 4), "DrawBitmap alpha on white " + Str(in));
            Check(Near(out, 255, 255, 255, 255), "white background " + Str(out));
        }

        // 5. two icons composed side by side and over each other
        {
            const wxBitmap a(MakeIcon(16, 255)), b(MakeIcon(16, 128));
            wxBitmap bmp(48, 16, 32);
            {
                wxMemoryDC dc(bmp);
                dc.DrawBitmap(a, 0, 0, true);
                dc.DrawBitmap(b, 16, 0, true);
                dc.DrawBitmap(b, 32, 0, true);
                dc.DrawBitmap(b, 32, 0, true);
            }
            const wxImage img = bmp.ConvertToImage();
            RGBA p0 = PixelOf(img, 8, 8), p1 = PixelOf(img, 24, 8),
                 p2 = PixelOf(img, 40, 8), gap = PixelOf(img, 16, 0);
            Check(Near(p0, 255, 0, 0, 255), "composed opaque icon " + Str(p0));
            Check(Near(p1, 255, 0, 0, 128), "composed half transparent icon " + Str(p1));
            Check(Near(p2, 255, 0, 0, 192, 4), "icon twice over itself " + Str(p2));
            Check(gap.a == 0, "gap between icons " + Str(gap));
        }

        // 6. wxGCDC on a transparent bitmap: Clear() with a transparent
        //    background, anti-aliased shape with alpha
        {
            wxBitmap bmp(32, 32, 32);
            {
                wxMemoryDC mdc(bmp);
                // garbage first, which the clear must remove
                mdc.SetBackground(*wxGREEN_BRUSH);
                mdc.Clear();

                wxGCDC dc(mdc);
                dc.SetBackground(wxBrush(wxColour(0, 0, 0, 0)));
                dc.Clear();
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(wxColour(0, 0, 255, 128)));
                dc.DrawRectangle(8, 8, 16, 16);
            }
            const wxImage img = bmp.ConvertToImage();
            RGBA in = PixelOf(img, 16, 16), out = PixelOf(img, 2, 2);
            Check(out.a == 0, "wxGCDC Clear() to transparent " + Str(out));
            Check(Near(in, 0, 0, 255, 128), "wxGCDC translucent fill on transparent " + Str(in));
        }

        // 7. wxGraphicsContext from an image
        {
            wxImage img(32, 32);
            img.InitAlpha();
            memset(img.GetAlpha(), 0, 32 * 32);
            {
                wxGraphicsContext *gc = wxGraphicsContext::Create(img);
                Check(gc != NULL, "wxGraphicsContext::Create(wxImage)");
                if ( gc )
                {
                    gc->SetBrush(wxBrush(wxColour(255, 255, 0)));
                    gc->DrawEllipse(4, 4, 24, 24);
                    delete gc;
                }
            }
            RGBA in = PixelOf(img, 16, 16), out = PixelOf(img, 1, 1);
            Check(Near(in, 255, 255, 0, 255), "context on image, inside " + Str(in));
            Check(out.a == 0, "context on image, outside " + Str(out));
        }

        // 8. text on a transparent bitmap: opaque glyph pixels, transparent
        //    background
        {
            wxBitmap bmp(64, 32, 32);
            {
                wxMemoryDC dc(bmp);
                dc.SetFont(wxFont(wxFontInfo(18).Bold()));
                dc.SetTextForeground(*wxBLACK);
                dc.DrawText("MMM", 2, 2);
            }
            const wxImage img = bmp.ConvertToImage();
            int opaque = 0, transparent = 0, badColour = 0;
            for ( int y = 0; y < 32; y++ )
            {
                for ( int x = 0; x < 64; x++ )
                {
                    const RGBA p = PixelOf(img, x, y);
                    if ( p.a == 0 )
                        transparent++;
                    else if ( p.a == 255 )
                        opaque++;
                    if ( p.a > 200 && (p.r > 40 || p.g > 40 || p.b > 40) )
                        badColour++;
                }
            }
            Check(opaque > 50 && transparent > 500 && badColour == 0,
                  wxString::Format("text on transparent bitmap (opaque %d, "
                                   "transparent %d, wrong colour %d)",
                                   opaque, transparent, badColour));
        }

        // 9. raw bitmap access
        {
            wxBitmap bmp(8, 8, 32);
            {
                wxAlphaPixelData data(bmp);
                Check(!!data, "wxAlphaPixelData on a 32 bit bitmap");
                if ( data )
                {
                    wxAlphaPixelData::Iterator p(data);
                    for ( int y = 0; y < 8; y++ )
                    {
                        wxAlphaPixelData::Iterator row = p;
                        for ( int x = 0; x < 8; x++, ++p )
                        {
                            p.Red() = 10;
                            p.Green() = 20;
                            p.Blue() = 30;
                            p.Alpha() = (unsigned char)(x * 32);
                        }
                        p = row;
                        p.OffsetY(data, 1);
                    }
                }
            }
            const wxImage img = bmp.ConvertToImage();
            RGBA p = PixelOf(img, 4, 3);
            Check(Near(p, 10, 20, 30, 128, 0), "raw alpha pixel " + Str(p));
        }

        // 10. masks
        {
            wxImage img(16, 16);
            img.SetRGB(wxRect(0, 0, 16, 16), 255, 0, 255);
            img.SetRGB(wxRect(4, 4, 8, 8), 0, 255, 0);
            img.SetMaskColour(255, 0, 255);
            const wxBitmap masked(img);
            Check(masked.GetMask() != NULL, "bitmap from image with mask has a mask");

            wxBitmap bmp(16, 16, 24);
            {
                wxMemoryDC dc(bmp);
                dc.SetBackground(*wxWHITE_BRUSH);
                dc.Clear();
                dc.DrawBitmap(masked, 0, 0, true);
            }
            const wxImage out = bmp.ConvertToImage();
            RGBA in = PixelOf(out, 8, 8), bg = PixelOf(out, 1, 1);
            Check(Near(in, 0, 255, 0, 255), "masked bitmap, opaque part " + Str(in));
            Check(Near(bg, 255, 255, 255, 255), "masked bitmap, masked part " + Str(bg));
        }

        // 11. sub bitmap and scaling blit
        {
            const wxBitmap icon(MakeIcon(16, 255));
            const wxBitmap sub = icon.GetSubBitmap(wxRect(4, 4, 8, 8));
            const wxImage img = sub.ConvertToImage();
            RGBA p = PixelOf(img, 4, 4);
            Check(sub.GetWidth() == 8 && Near(p, 255, 0, 0, 255), "sub bitmap " + Str(p));

            wxBitmap big(32, 32, 32);
            {
                wxMemoryDC dst(big), src;
                src.SelectObjectAsSource(icon);
                dst.StretchBlit(0, 0, 32, 32, &src, 0, 0, 16, 16);
            }
            const wxImage scaled = big.ConvertToImage();
            RGBA c = PixelOf(scaled, 16, 16), o = PixelOf(scaled, 2, 2);
            Check(Near(c, 255, 0, 0, 255) && o.a == 0,
                  "StretchBlit of alpha bitmap " + Str(c) + " " + Str(o));
        }

        // 12. the same on an opaque target, and scaled through a context
        {
            const wxBitmap icon(MakeIcon(16, 255));
            wxBitmap big(32, 32, 24);
            {
                wxMemoryDC dst(big), src;
                dst.SetBackground(*wxWHITE_BRUSH);
                dst.Clear();
                src.SelectObjectAsSource(icon);
                dst.StretchBlit(0, 0, 32, 32, &src, 0, 0, 16, 16);
            }
            const wxImage scaled = big.ConvertToImage();
            RGBA c = PixelOf(scaled, 16, 16), o = PixelOf(scaled, 2, 2);
            Check(Near(c, 255, 0, 0, 255) && Near(o, 255, 255, 255, 255),
                  "StretchBlit of alpha bitmap on white " + Str(c) + " " + Str(o));

            wxBitmap big2(32, 32, 32);
            {
                wxMemoryDC dst(big2);
                wxGraphicsContext *gc = wxGraphicsContext::Create(dst);
                gc->DrawBitmap(icon, 0, 0, 32, 32);
                delete gc;
            }
            const wxImage scaled2 = big2.ConvertToImage();
            c = PixelOf(scaled2, 16, 16);
            o = PixelOf(scaled2, 2, 2);
            Check(Near(c, 255, 0, 0, 255) && o.a == 0,
                  "context DrawBitmap scaled on transparent " + Str(c) + " " + Str(o));
        }

        if ( gs_failures )
            wxPrintf("%d FAILED\n", gs_failures);
        else
            wxPrintf("ALL PASSED\n");
        fflush(stdout);

        return false;
    }
};

wxIMPLEMENT_APP(TestApp);
