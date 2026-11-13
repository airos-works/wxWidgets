// Paint speed of the port: a window draws a scene made of what BambuStudio's
// custom widgets use (rounded rectangles, text, alpha bitmaps) a number of
// times with wxDC and with wxGCDC and prints the time per frame. Exits by
// itself.
#include "wx/wx.h"
#include "wx/dcgraph.h"
#include "wx/graphics.h"
#include "wx/stopwatch.h"

class Canvas : public wxWindow
{
public:
    Canvas(wxWindow *parent)
        : wxWindow(parent, wxID_ANY), m_useGC(false), m_measure(false)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &Canvas::OnPaint, this);

        wxImage img(24, 24);
        img.InitAlpha();
        for ( int y = 0; y < 24; y++ )
            for ( int x = 0; x < 24; x++ )
            {
                img.SetRGB(x, y, 30, 120 + x * 4, 220);
                img.SetAlpha(x, y, (unsigned char)(x * 10));
            }
        m_icon = wxBitmap(img);
    }

    bool m_useGC;
    bool m_measure;

private:
    template <class DC>
    void Scene(DC& dc)
    {
        const wxSize size = GetClientSize();
        dc.SetBrush(*wxWHITE_BRUSH);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, size.x, size.y);

        dc.SetFont(GetFont());
        int n = 0;
        for ( int y = 4; y + 30 < size.y; y += 34 )
        {
            for ( int x = 4; x + 150 < size.x; x += 156, n++ )
            {
                dc.SetPen(wxPen(wxColour(0, 174, 66), 1));
                dc.SetBrush(wxBrush(n % 2 ? wxColour(238, 238, 238)
                                          : wxColour(255, 255, 255)));
                dc.DrawRoundedRectangle(x, y, 150, 30, 6);
                dc.DrawBitmap(m_icon, x + 4, y + 3, true);
                dc.SetTextForeground(wxColour(38, 46, 48));
                dc.DrawText(m_measure ? wxString::Format("Item %d", n)
                                      : wxString("Print setting"), x + 34, y + 7);
                if ( m_measure )
                    dc.GetTextExtent(wxString::Format("measure %d", n % 17));
            }
        }
    }

    void OnPaint(wxPaintEvent&)
    {
        wxPaintDC pdc(this);
        if ( m_useGC )
        {
            wxGCDC dc(pdc);
            Scene(dc);
        }
        else
        {
            Scene(pdc);
        }
    }

    wxBitmap m_icon;
};

class Frame : public wxFrame
{
public:
    Frame() : wxFrame(NULL, wxID_ANY, "paint test", wxPoint(60, 60), wxSize(1000, 700))
    {
        m_canvas = new Canvas(this);
        m_timer.Bind(wxEVT_TIMER, &Frame::OnTimer, this);
        m_timer.StartOnce(1500);
    }

private:
    void Run(const char *what, bool gc, bool measure)
    {
        m_canvas->m_useGC = gc;
        m_canvas->m_measure = measure;

        const int frames = 60;
        wxStopWatch sw;
        for ( int i = 0; i < frames; i++ )
        {
            m_canvas->Refresh();
            m_canvas->Update();
        }
        const wxSize size = m_canvas->GetClientSize();
        wxPrintf("%-28s %6.2f ms/frame (%dx%d, %d items)\n", what,
                 sw.Time() / (double)frames, size.x, size.y,
                 (size.x / 156) * (size.y / 34));
        fflush(stdout);
    }

    void OnTimer(wxTimerEvent&)
    {
        Run("wxDC", false, false);
        Run("wxGCDC", true, false);
        Run("wxDC, changing text", false, true);
        Run("wxGCDC, changing text", true, true);
        Run("wxGCDC, changing text again", true, true);
        Close();
    }

    Canvas *m_canvas;
    wxTimer m_timer;
};

class App : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        (new Frame)->Show();
        return true;
    }
};

wxIMPLEMENT_APP(App);
