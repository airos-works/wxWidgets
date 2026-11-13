/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/filepanel.cpp
// Purpose:     the BFilePanel behind wxFileDialog and wxDirDialog
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_FILEDLG || wxUSE_DIRDLG

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/utils.h"
    #include "wx/window.h"
    #include "wx/toplevel.h"
#endif

#include "wx/evtloop.h"
#include "wx/tokenzr.h"

#include "wx/haiku/private/filepanel.h"

#include <Button.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Handler.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Node.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <TextControl.h>

#include <fnmatch.h>

#include <string>
#include <vector>

// ============================================================================
// the native side: everything here runs in the panel's window thread unless
// noted otherwise and does not call wx
// ============================================================================

namespace
{

enum
{
    // values of the callback records
    kPanelChosen = 1,           // strings: the paths
    kPanelHidden                // the panel was closed (also after kPanelChosen)
};

const uint32 kMsgFilterChosen = 'wxFC';

// The file name patterns of the filters and the filter in use. Filled by the
// wx thread before the panel exists; afterwards only the current filter
// changes, in the window thread.
struct PanelFilters
{
    PanelFilters() : current(0), dirsOnly(false) { }

    // of filter n (all of them if there are none: everything matches)
    bool Matches(int32 n, const char *name) const
    {
        if ( n < 0 || n >= (int32)patterns.size() )
            return true;

        const std::vector<std::string>& list = patterns[n];
        if ( list.empty() )
            return true;

        for ( size_t i = 0; i < list.size(); i++ )
        {
            if ( fnmatch(list[i].c_str(), name, FNM_CASEFOLD) == 0 )
                return true;
        }

        return false;
    }

    // "txt" for a filter starting with "*.txt"
    std::string DefaultExtension(int32 n) const
    {
        if ( n < 0 || n >= (int32)patterns.size() )
            return std::string();

        const std::vector<std::string>& list = patterns[n];
        for ( size_t i = 0; i < list.size(); i++ )
        {
            const std::string& pattern = list[i];
            if ( pattern.compare(0, 2, "*.") == 0 && pattern.length() > 2
                    && pattern.find_first_of("*?[", 2) == std::string::npos )
                return pattern.substr(2);
        }

        return std::string();
    }

    std::vector<std::vector<std::string> > patterns;
    std::vector<std::string> labels;
    int32 current;              // read with atomic_get()
    bool dirsOnly;
};

class PanelRefFilter : public BRefFilter
{
public:
    explicit PanelRefFilter(const PanelFilters *filters) : m_filters(filters) { }

    virtual bool Filter(const entry_ref *ref, BNode *node,
                        struct stat_beos *WXUNUSED(st),
                        const char *WXUNUSED(mimeType))
    {
        // directories (and what leads to one) are always shown: the user
        // navigates through them
        if ( node && node->InitCheck() == B_OK )
        {
            if ( node->IsDirectory() )
                return true;

            if ( node->IsSymLink() )
            {
                BEntry entry(ref, true);
                if ( entry.InitCheck() == B_OK && entry.IsDirectory() )
                    return true;
            }
        }
        else
        {
            BEntry entry(ref, true);
            if ( entry.InitCheck() == B_OK && entry.IsDirectory() )
                return true;
        }

        if ( m_filters->dirsOnly )
            return false;

        return m_filters->Matches(atomic_get((int32 *)&m_filters->current),
                                  ref->name);
    }

private:
    const PanelFilters *m_filters;
};

class NativePanel;

// Gets the messages of the filter menu and the panel's B_CANCEL.
class PanelHandler : public BHandler
{
public:
    PanelHandler(NativePanel *panel, PanelFilters *filters)
        : BHandler("wx file panel"),
          m_panel(panel),
          m_filters(filters)
    {
    }

    virtual void MessageReceived(BMessage *message);

private:
    NativePanel *m_panel;
    PanelFilters *m_filters;
};

class NativePanel : public BFilePanel
{
public:
    NativePanel(wxUint32 handle, file_panel_mode mode, uint32 flavors,
                bool multiple, BRefFilter *filter)
        : BFilePanel(mode, NULL, NULL, flavors, multiple, NULL, filter,
                     true /* modal */, true /* hide when done */),
          m_handle(handle)
    {
    }

    // called instead of sending the message with the chosen entries to a
    // target
    virtual void SendMessage(const BMessenger *WXUNUSED(target),
                             BMessage *message)
    {
        wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_CALLBACK, m_handle);
        event->value = kPanelChosen;

        if ( PanelMode() == B_SAVE_PANEL )
        {
            entry_ref dir;
            const char *name;
            if ( message->FindRef("directory", &dir) == B_OK
                    && message->FindString("name", &name) == B_OK )
            {
                BPath path(&dir);
                if ( path.InitCheck() == B_OK && path.Append(name) == B_OK )
                    event->strings.push_back(path.Path());
            }
        }
        else
        {
            entry_ref ref;
            for ( int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++ )
            {
                BPath path(&ref);
                if ( path.InitCheck() == B_OK )
                    event->strings.push_back(path.Path());
            }
        }

        wxHaikuPostEvent(event);
    }

    virtual void WasHidden()
    {
        wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_CALLBACK, m_handle);
        event->value = kPanelHidden;
        wxHaikuPostEvent(event);
    }

private:
    wxUint32 m_handle;
};

void PanelHandler::MessageReceived(BMessage *message)
{
    switch ( message->what )
    {
        case kMsgFilterChosen:
            {
                int32 index;
                if ( message->FindInt32("index", &index) != B_OK
                        || index == m_filters->current )
                    break;

                const int32 old = m_filters->current;
                atomic_set(&m_filters->current, index);

                // a name to save under follows the type chosen if it had the
                // extension of the previous one (or none)
                BTextControl *text = NULL;
                if ( m_panel->PanelMode() == B_SAVE_PANEL && Looper() )
                {
                    text = dynamic_cast<BTextControl *>(
                            static_cast<BWindow *>(Looper())->FindView("text view"));
                }

                const std::string ext = m_filters->DefaultExtension(index);
                if ( text && !ext.empty() )
                {
                    std::string name = text->Text();
                    const std::string oldExt = m_filters->DefaultExtension(old);
                    const size_t dot = name.rfind('.');

                    if ( dot == std::string::npos || dot == 0 )
                    {
                        if ( !name.empty() )
                            name += "." + ext;
                    }
                    else if ( !oldExt.empty()
                                && strcasecmp(name.c_str() + dot + 1,
                                              oldExt.c_str()) == 0 )
                    {
                        name.replace(dot + 1, std::string::npos, ext);
                    }

                    if ( name != text->Text() )
                        m_panel->SetSaveText(name.c_str());
                }

                m_panel->Refresh();
            }
            break;

        case B_CANCEL:
            // reported by NativePanel::WasHidden() already
            break;

        default:
            BHandler::MessageReceived(message);
    }
}

// Adds the menu of the filters to the panel's window; wx thread, with the
// window locked. The panel has a row with its buttons at the bottom, an open
// panel nothing left of them, a save panel the name field: that one moves up
// and the file list gets shorter.
void AddFilterMenu(BFilePanel *panel, PanelHandler *handler,
                   const PanelFilters& filters)
{
    BWindow * const window = panel->Window();
    BView * const background = window->ChildAt(0);
    if ( !background )
        return;

    BView * const cancel = background->FindView("cancel button");
    BView * const list = background->FindView("PoseView");
    BView * const text = background->FindView("text view");
    if ( !cancel || !list || list->Parent() != background
            || (panel->PanelMode() == B_SAVE_PANEL
                    && (!text || text->Parent() != background)) )
    {
        // not the panel we know: do without the menu
        return;
    }

    BPopUpMenu * const menu = new BPopUpMenu("filter");
    for ( size_t n = 0; n < filters.labels.size(); n++ )
    {
        BMessage * const message = new BMessage(kMsgFilterChosen);
        message->AddInt32("index", (int32)n);

        BMenuItem * const item = new BMenuItem(filters.labels[n].c_str(), message);
        item->SetTarget(handler);
        if ( (int32)n == filters.current )
            item->SetMarked(true);
        menu->AddItem(item);
    }

    const float spacing = list->Frame().left;
    const BRect cancelFrame = cancel->Frame();

    BRect rect(spacing, cancelFrame.top,
               cancelFrame.left - spacing, cancelFrame.bottom);

    BMenuField * const field = new BMenuField(rect, "filter menu", NULL, menu,
                                              B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
    field->SetDivider(0);

    // wide enough for the longest of the labels, which the field shows one
    // at a time: ask it with each of them marked
    float width = 0, height = 0;
    for ( int32 n = 0; n < menu->CountItems(); n++ )
    {
        menu->ItemAt(n)->SetMarked(true);

        float w, h;
        field->GetPreferredSize(&w, &h);
        if ( w > width )
            width = w;
        if ( h > height )
            height = h;
    }

    if ( BMenuItem * const current = menu->ItemAt(filters.current) )
        current->SetMarked(true);

    if ( width > rect.Width() )
        width = rect.Width();
    field->ResizeTo(width, height);
    field->MoveTo(rect.left,
                  rect.top + floorf((rect.Height() - height) / 2));

    if ( text )
    {
        const float shift = text->Frame().Height() + spacing;
        list->ResizeBy(0, -shift);
        text->MoveBy(0, -shift);
    }

    background->AddChild(field);

    // the buttons follow the right edge of the window: keep them off the menu
    float minWidth, maxWidth, minHeight, maxHeight;
    window->GetSizeLimits(&minWidth, &maxWidth, &minHeight, &maxHeight);
    const float needed = field->Frame().right + spacing
                            + (window->Bounds().Width() - cancelFrame.left);
    if ( needed > minWidth )
        window->SetSizeLimits(needed, maxWidth, minHeight, maxHeight);
}

} // anonymous namespace

// ============================================================================
// the wx side
// ============================================================================

wxHaikuFilePanel::wxHaikuFilePanel(Mode mode)
    : m_mode(mode),
      m_multiple(false),
      m_filterIndex(0),
      m_chosen(false),
      m_loop(NULL)
{
}

wxHaikuFilePanel::~wxHaikuFilePanel()
{
}

void wxHaikuFilePanel::SetFilters(const wxArrayString& descriptions,
                                  const wxArrayString& patterns,
                                  int index)
{
    m_filterDescriptions = descriptions;
    m_filterPatterns = patterns;
    m_filterIndex = index >= 0 && index < (int)patterns.size() ? index : 0;
}

wxString wxHaikuFilePanel::GetDefaultExtension(int filter) const
{
    if ( filter < 0 || filter >= (int)m_filterPatterns.size() )
        return wxString();

    wxStringTokenizer tokens(m_filterPatterns[filter], ";");
    while ( tokens.HasMoreTokens() )
    {
        const wxString pattern = tokens.GetNextToken();
        wxString ext;
        if ( pattern.StartsWith("*.", &ext) && !ext.empty()
                && ext.find_first_of("*?[") == wxString::npos )
            return ext;
    }

    return wxString();
}

void wxHaikuFilePanel::HaikuCallback(const wxHaikuEvent& event)
{
    switch ( event.value )
    {
        case kPanelChosen:
            m_paths.clear();
            for ( size_t n = 0; n < event.strings.size(); n++ )
                m_paths.push_back(wxString::FromUTF8(event.strings[n].c_str()));
            m_chosen = !m_paths.empty();
            break;

        case kPanelHidden:
            if ( m_loop )
                m_loop->Exit();
            break;
    }
}

bool wxHaikuFilePanel::ShowModal(wxWindow *parent)
{
    wxCHECK_MSG( be_app, false, "wxApp must be initialized first" );
    wxCHECK_MSG( !m_loop, false, "the panel is shown already" );

    m_paths.clear();
    m_chosen = false;

    // the patterns in the form the window thread uses them
    PanelFilters filters;
    filters.dirsOnly = m_mode == Mode_Dir;
    filters.current = m_filterIndex;

    for ( size_t n = 0; n < m_filterPatterns.size(); n++ )
    {
        std::vector<std::string> list;

        wxStringTokenizer tokens(m_filterPatterns[n], ";");
        while ( tokens.HasMoreTokens() )
        {
            const wxString pattern = tokens.GetNextToken().Trim().Trim(false);
            if ( pattern.empty() )
                continue;

            // "everything" in the two ways of writing it: no list at all
            if ( pattern == "*" || pattern == "*.*" )
            {
                list.clear();
                break;
            }

            list.push_back(std::string(pattern.utf8_str()));
        }

        filters.patterns.push_back(list);
        filters.labels.push_back(std::string(
            (n < m_filterDescriptions.size() ? m_filterDescriptions[n]
                                             : m_filterPatterns[n]).utf8_str()));
    }

    PanelRefFilter refFilter(&filters);

    NativePanel * const panel = new NativePanel(
        GetHaikuCallbackHandle(),
        m_mode == Mode_Save ? B_SAVE_PANEL : B_OPEN_PANEL,
        m_mode == Mode_Dir ? B_DIRECTORY_NODE : B_FILE_NODE,
        m_multiple,
        m_mode == Mode_Save && filters.patterns.empty() ? NULL : &refFilter);

    BWindow * const window = panel->Window();
    PanelHandler * const handler = new PanelHandler(panel, &filters);

    if ( !window || !window->Lock() )
    {
        delete panel;
        delete handler;
        return false;
    }

    window->AddHandler(handler);
    panel->SetTarget(BMessenger(handler, window));

    if ( filters.patterns.size() > 1 )
        AddFilterMenu(panel, handler, filters);

    if ( !m_title.empty() )
        window->SetTitle(wxHaikuStr(m_title));

    window->Unlock();

    // these lock the window themselves
    if ( m_mode == Mode_Dir )
        panel->SetButtonLabel(B_DEFAULT_BUTTON, wxHaikuStr(_("Select")));

    if ( !m_dir.empty() )
    {
        BEntry entry(wxHaikuStr(m_dir), true);
        if ( entry.InitCheck() == B_OK && entry.IsDirectory() )
            panel->SetPanelDirectory(&entry);
    }

    if ( m_mode == Mode_Save )
        panel->SetSaveText(wxHaikuStr(m_name));

    {
        // The panel's window has the application modal feel, which keeps it
        // in front of our windows and the input away from them; for wx they
        // are disabled meanwhile, as for any modal dialog.
        wxWindowDisabler disableOthers;

        wxEventLoop loop;
        m_loop = &loop;

        // BFilePanel::Show() places the panel like an alert on the screen
        // (or over the window whose thread calls it, which this thread is
        // not): centre it on the window it belongs to instead.
        //
        // NB: not wxDynamicCast(), wxNonOwnedWindow is not in the wx RTTI
        //     chain of wxTopLevelWindow
        wxNonOwnedWindow * const tlw = parent
            ? dynamic_cast<wxNonOwnedWindow *>(wxGetTopLevelParent(parent))
            : NULL;

        panel->Show();

        if ( tlw && tlw->GetHaikuWindow() && tlw->IsShown() && window->Lock() )
        {
            // (also keeps it on the screen)
            window->CenterIn(wxHaikuRect(tlw->GetScreenRect()));
            window->Unlock();
        }

        loop.Run();

        m_loop = NULL;
    }

    m_filterIndex = filters.current;

    // The destructor of the panel waits for its window thread to end, which
    // never waits for this thread. The handler does not belong to the window.
    if ( window->Lock() )
    {
        window->RemoveHandler(handler);
        window->Unlock();
    }

    delete panel;
    delete handler;

    return m_chosen;
}

#endif // wxUSE_FILEDLG || wxUSE_DIRDLG
