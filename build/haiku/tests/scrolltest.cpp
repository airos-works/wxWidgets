// A scrolled window with a sizer of rows, taller than the window: once the
// vertical scroll bar is there the rows must fit in the client width
// (WX-REQUESTS 26). Prints the widths, takes a look with --show.
//   scrolltest            rows of expanding text controls
//   scrolltest --late     the rows are added once the window is shown, as an
//                         application fills a page when it is selected
//   scrolltest --bambu    BambuStudio's parameter page: wxVSCROLL only,
//                         SetScrollbars(1, 20, 1, 2), rows of a label and a
//                         self-drawn field of fixed width with its unit
//                         inside, added late between Freeze() and Thaw();
//                         the text control is made narrower than its best
//                         (= minimal) size as BambuStudio's TextInput does
#include "wx/wx.h"

#include <stdio.h>

namespace
{

// BambuStudio's TextInput, reduced: a box drawn from the full bounds with a
// borderless text control in it and the unit at the right
class Field : public wxWindow
{
public:
    Field(wxWindow *parent, const wxString& value, const wxString& unit)
        : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(120, -1),
                   wxBORDER_NONE),
          m_unit(unit)
    {
        SetBackgroundColour(*wxWHITE);
        m_text = new wxTextCtrl(this, wxID_ANY, value, wxPoint(5, 4),
                                wxDefaultSize, wxBORDER_NONE);
        // as TextInput: the best size becomes the minimal size, then the
        // control is made narrower by hand
        m_text->SetInitialSize(m_text->GetBestSize());
        const int h = m_text->GetBestSize().y + 8;
        SetMinSize(wxSize(120, h));
        SetSize(wxSize(120, h));
        Bind(wxEVT_PAINT, &Field::OnPaint, this);
    }

    virtual void DoSetSize(int x, int y, int width, int height,
                           int sizeFlags) wxOVERRIDE
    {
        const wxSize old = GetSize();
        wxWindow::DoSetSize(x, y, width, height, sizeFlags);
        const wxSize size = GetSize();
        if ( size != old )
            Refresh();
        if ( m_text )
        {
            wxClientDC dc(this);
            const int unit = dc.GetTextExtent(m_unit).x;
            m_asked = wxMax(size.x - unit - 20, 10);
            m_text->SetSize(wxSize(m_asked, -1));
            m_text->SetPosition(wxPoint(5, (size.y - m_text->GetSize().y) / 2));
        }
    }

    wxTextCtrl *m_text = NULL;
    int m_asked = 0;

private:
    void OnPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        const wxSize size = GetSize();
        dc.SetPen(wxPen(wxColour(0, 174, 66)));
        dc.SetBrush(*wxWHITE_BRUSH);
        dc.DrawRectangle(0, 0, size.x, size.y);
        dc.SetTextForeground(wxColour(144, 144, 144));
        // after the text control, as TextInput draws it
        const wxSize unit = dc.GetTextExtent(m_unit);
        dc.DrawText(m_unit, m_text->GetRect().GetRight() + 5,
                    (size.y - unit.y) / 2);
    }

    wxString m_unit;
};

} // anonymous namespace

class App : public wxApp
{
public:
    virtual bool OnInit() wxOVERRIDE
    {
        const wxString mode = argc > 1 ? wxString(argv[1]) : wxString();
        const bool bambu = mode == "--bambu";
        const bool late = bambu || mode == "--late";

        wxFrame * const frame = new wxFrame(NULL, wxID_ANY, "scrolltest",
                                            wxPoint(40, 40), wxSize(420, 320));
        wxScrolledWindow * const scrolled
            = new wxScrolledWindow(frame, wxID_ANY, wxDefaultPosition,
                                   wxDefaultSize,
                                   bambu ? wxVSCROLL : wxHSCROLL | wxVSCROLL);
        if ( bambu )
        {
            scrolled->SetBackgroundColour(*wxWHITE);
            scrolled->SetScrollbars(1, 20, 1, 2);
        }
        else
        {
            scrolled->SetScrollRate(0, 10);
        }

        wxBoxSizer * const sizer = new wxBoxSizer(wxVERTICAL);
        scrolled->SetSizer(sizer);
        static wxWindow *last = NULL;
        const auto addRows = [scrolled, sizer, bambu]()
        {
            if ( bambu )
                scrolled->Freeze();
            for ( int n = 0; n < 25; n++ )
            {
                wxBoxSizer * const row = new wxBoxSizer(wxHORIZONTAL);
                row->Add(new wxStaticText(scrolled, wxID_ANY,
                                          wxString::Format("Setting %d", n)),
                         wxSizerFlags().Center());
                if ( bambu )
                {
                    row->AddStretchSpacer();
                    last = new Field(scrolled, "0.20", "mm");
                    row->Add(last, wxSizerFlags().Border(wxLEFT));
                }
                else
                {
                    last = new wxTextCtrl(scrolled, wxID_ANY, "0.20");
                    row->Add(last, wxSizerFlags(1).Border(wxLEFT));
                    row->Add(new wxStaticText(scrolled, wxID_ANY, "mm"),
                             wxSizerFlags().Center().Border(wxLEFT));
                }
                sizer->Add(row, wxSizerFlags().Expand().Border());
            }
            scrolled->Layout();
            scrolled->FitInside();
            if ( bambu )
                scrolled->Thaw();
        };

        if ( !late )
            addRows();
        frame->Show();
        if ( late )
            frame->CallAfter(addRows);

        frame->CallAfter([frame, scrolled]
        {
            wxTimer * const timer = new wxTimer;
            timer->Bind(wxEVT_TIMER, [frame, scrolled, timer](wxTimerEvent&)
            {
                const wxSize client = scrolled->GetClientSize();
                const wxSize virt = scrolled->GetVirtualSize();
                const wxRect rect = last->GetRect();
                printf("client %dx%d virtual %dx%d, right edge of the last "
                       "field %d -> %s\n",
                       client.x, client.y, virt.x, virt.y, rect.GetRight(),
                       rect.GetRight() < client.x ? "fits" : "CUT");
                if ( Field * const field = dynamic_cast<Field *>(last) )
                {
                    // the unit is drawn 5 pixels after the text control
                    wxClientDC dc(field);
                    const int unitRight = field->m_text->GetRect().GetRight()
                                          + 5 + dc.GetTextExtent("mm").x;
                    printf("text control of the field: asked %d wide, is %d; "
                           "unit ends at %d of %d -> %s\n", field->m_asked,
                           field->m_text->GetSize().x, unitRight,
                           field->GetSize().x,
                           unitRight < field->GetSize().x ? "ok" : "CUT");
                }
                fflush(stdout);
                delete timer;
            });
            timer->StartOnce(1500);
        });

        return true;
    }
};

wxIMPLEMENT_APP(App);
