/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/glcanvas.cpp
// Purpose:     wxGLCanvas and wxGLContext on the GL kit (BGLView)
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// Design
// ======
//
// The GL kit has no context object: a BGLView is a view, an OpenGL context
// and the frame buffer of this context in one, and nothing lets a context
// draw into the buffers of another view. wx needs contexts which are made
// current for one canvas or another, keeping their state (BambuStudio renders
// all its canvases with a single wxGLContext). So:
//
//  - A wxGLContext owns a BGLView which is never attached to a window. Its
//    context is the wxGLContext, its buffers are what the context renders
//    into. The view is resized to the canvas the context is made current for
//    (the renderers allocate their buffers again when the context is made
//    current after that; nothing happens while the size stays the same).
//  - A wxGLCanvas is an ordinary wx window with an ordinary view. Its
//    SwapBuffers() reads the back buffer of the context which rendered for
//    it into a BBitmap (glReadPixels(), rows inverted by Mesa) and draws the
//    bitmap on the view: one copy more than BGLView::SwapBuffers() needs with
//    the software renderers, the same as it does itself with the others. The
//    bitmap stays: an exposed canvas shows its last frame at once, before the
//    program has rendered the next one.
//  - Contexts share their objects if asked to (BGL_SHARE_CONTEXT, which
//    shares with the context current when the view is created).
//
// Threads and locks
// =================
//
// What the two implementations of the kit do (Mesa's src/hgl with BGLRenderer
// add-ons in the x86_64 images, libglvnd on EGL in the arm64 ones):
// BGLView::LockGL() takes fDisplayLock, a BLocker of the view, and makes the
// context current for the calling thread; UnlockGL() leaves the thread
// without context. No looper is locked by either. SwapBuffers(), Draw(),
// AttachedToWindow() and FrameResized() are where they deal with the window
// (locking its looper with a timeout from the rendering thread, drawing the
// front buffer from the looper thread without any GL lock, posting
// B_INVALIDATE to the view...) and none of them is ever used here:
//
//  1. OpenGL belongs to the wx (main) thread. It is the only thread which
//     calls LockGL(), UnlockGL() or any gl function, and it keeps the GL lock
//     of the view whose context is current for as long as it is current,
//     that is between frames too: wx knows no "release". MakeCurrent() is
//     the only place which locks and unlocks.
//  2. The views of the contexts have no window, hence no looper: no looper
//     thread ever runs one of their hooks and nothing of the kit runs on a
//     looper thread. The looper threads cannot want a GL lock.
//  3. The view of a canvas is a wxHaikuView: its hooks post event records
//     and never wait for the main thread. The main thread shows a frame as
//     it paints any window: it locks the looper of the canvas and draws the
//     bitmap, synchronously. It may hold a GL lock while it waits for the
//     looper lock; the looper thread holding that lock never waits for the
//     main thread, so there is no cycle.
//  4. Resizing: wx resizes the view of the canvas. The view of the context
//     which is current for it is resized by the main thread as well (it is
//     not in a window: no lock) and BGLView::FrameResized() is called
//     directly, which only stores the size in the renderer; making the
//     context current again then gives the buffers their size before the
//     next frame. No looper thread is involved.
//  5. Creating a BGLView makes its context current behind the back of wx in
//     the renderers of the classic kit: CreateView() restores what was.

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_GLCANVAS

// the functions of OpenGL 1.5 and later
#define GL_GLEXT_PROTOTYPES

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/intl.h"
    #include "wx/log.h"
#endif

#include "wx/glcanvas.h"

#include "wx/haiku/private.h"

#include <GLView.h>
#include <GL/glext.h>

#ifndef GL_PACK_INVERT_MESA
    #define GL_PACK_INVERT_MESA 0x8758
#endif

// ============================================================================
// contexts
// ============================================================================

// What a wxGLContext is. The canvases refer to this, not to the wxGLContext.
struct wxHaikuGLContextData
{
    wxHaikuGLContextData()
        : view(NULL), options(0), canvas(NULL), initialized(false)
    {
    }

    BGLView *view;
    unsigned long options;

    // the size of the view: the renderer was told
    wxSize size;

    // the canvas the context renders for
    const wxGLCanvas *canvas;

    // the context was made current once
    bool initialized;
};

namespace
{

// what the contexts of the kit are, found out once
struct KitInfo
{
    KitInfo()
        : probed(false), ok(false), major(0), minor(0),
          hasReadFramebuffer(false), hasPackBuffer(false),
          hasPackInvert(false)
    {
    }

    bool probed;
    bool ok;            // there is a renderer
    int major, minor;
    std::string extensions;

    // what reading a frame has to deal with
    bool hasReadFramebuffer;
    bool hasPackBuffer;
    bool hasPackInvert;
};

KitInfo gs_kit;

// the context whose GL lock the main thread holds, which is current
wxHaikuGLContextData *gs_current = NULL;

// The view the kit was probed with if no context was created yet: it becomes
// the view of the first context it suits.
BGLView *gs_spareView = NULL;
unsigned long gs_spareOptions = 0;

const unsigned long DEFAULT_OPTIONS = BGL_RGB | BGL_DOUBLE | BGL_DEPTH;

bool HasExtension(const char *name)
{
    const size_t len = strlen(name);
    const std::string& all = gs_kit.extensions;

    for ( size_t pos = all.find(name);
          pos != std::string::npos;
          pos = all.find(name, pos + 1) )
    {
        if ( (pos == 0 || all[pos - 1] == ' ')
                && (pos + len == all.size() || all[pos + len] == ' ') )
        {
            return true;
        }
    }

    return false;
}

// the only function which locks and unlocks GL
void MakeCurrent(wxHaikuGLContextData *context)
{
    if ( context == gs_current )
        return;

    if ( gs_current )
        gs_current->view->UnlockGL();

    gs_current = context;

    if ( context )
        context->view->LockGL();
}

// Creates the view of a context, which shares its objects with the given
// one. The current context stays what it is.
BGLView *CreateView(unsigned long options, const wxSize& size,
                    wxHaikuGLContextData *share)
{
    wxCHECK_MSG( be_app, NULL, "OpenGL needs the application object" );

    wxHaikuGLContextData * const current = gs_current;

    // BGL_SHARE_CONTEXT shares with the context which is current when the
    // view is created
    MakeCurrent(share);
    if ( share )
        options |= BGL_SHARE_CONTEXT;

    BGLView * const
        view = new BGLView(BRect(0, 0, size.x - 1, size.y - 1), "wxGLContext",
                           B_FOLLOW_NONE, 0, options);

    // The renderers of the classic kit leave the new context current
    // without the lock. Unlocking any view of this kit leaves the thread
    // without context: a known state.
    MakeCurrent(NULL);
    view->LockGL();

    if ( !gs_kit.probed )
    {
        gs_kit.probed = true;

        const char * const version = (const char *)glGetString(GL_VERSION);
        if ( version )
        {
            gs_kit.ok = true;
            sscanf(version, "%d.%d", &gs_kit.major, &gs_kit.minor);

            const char * const
                extensions = (const char *)glGetString(GL_EXTENSIONS);
            if ( extensions )
                gs_kit.extensions = extensions;

            gs_kit.hasReadFramebuffer = gs_kit.major >= 3
                || HasExtension("GL_ARB_framebuffer_object")
                || HasExtension("GL_EXT_framebuffer_blit");
            gs_kit.hasPackBuffer = gs_kit.major > 2
                || (gs_kit.major == 2 && gs_kit.minor >= 1)
                || HasExtension("GL_ARB_pixel_buffer_object");
            gs_kit.hasPackInvert = HasExtension("GL_MESA_pack_invert");
        }
    }

    view->UnlockGL();

    MakeCurrent(current);

    return view;
}

void DeleteView(BGLView *view)
{
    // Without the application (a wxGLContext deleted after wxApp::CleanUp())
    // the renderer cannot free its bitmaps: the process ends anyway.
    if ( be_app )
        delete view;
}

// Makes sure the kit was looked at; returns true if it can render. The
// options are those of the context which is likely to be created next.
bool KitIsOk(unsigned long options)
{
    if ( !gs_kit.probed )
    {
        // Asked before the application object exists (InitGLVisual() in the
        // constructor of the wxApp): no view can be created yet. The kit
        // renders in software where there is nothing better.
        if ( !be_app )
            return true;

        gs_spareOptions = options;
        gs_spareView = CreateView(options, wxSize(16, 16), NULL);
    }

    return gs_kit.ok;
}

// The context renders for a canvas of this size from now on.
void SetContextSize(wxHaikuGLContextData *context, const wxSize& size)
{
    if ( size == context->size )
        return;

    context->size = size;

    // the view is in no window: there is nothing to lock
    BGLView * const view = context->view;
    view->ResizeTo(size.x - 1, size.y - 1);

    // stores the size in the renderer, nothing else
    view->BGLView::FrameResized(size.x - 1, size.y - 1);

    // the buffers are allocated when the context is made current
    if ( gs_current == context )
    {
        view->UnlockGL();
        view->LockGL();
    }
}

// Reads the back buffer of the context, which is current, into the bitmap
// (B_RGB32, of the size of the context), first row at the top.
void ReadFrame(wxHaikuGLContextData *context, BBitmap *bitmap)
{
    const int width = context->size.x,
              height = context->size.y;

    // The program may be in the middle of anything: whatever is changed is
    // put back.
    GLint readFramebuffer = 0;
    if ( gs_kit.hasReadFramebuffer )
    {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        if ( readFramebuffer )
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }

    GLint readBuffer = GL_BACK;
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    if ( readBuffer != GL_BACK )
        glReadBuffer(GL_BACK);

    GLint packBuffer = 0;
    if ( gs_kit.hasPackBuffer )
    {
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
        if ( packBuffer )
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    static const GLenum packNames[] =
    {
        GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS,
        GL_PACK_SKIP_PIXELS, GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST,
        GL_PACK_INVERT_MESA
    };
    const GLint packWanted[WXSIZEOF(packNames)] =
    {
        4, bitmap->BytesPerRow() / 4, 0, 0, GL_FALSE, GL_FALSE, GL_TRUE
    };
    const size_t packCount = gs_kit.hasPackInvert ? WXSIZEOF(packNames)
                                                  : WXSIZEOF(packNames) - 1;
    GLint packSaved[WXSIZEOF(packNames)];
    for ( size_t n = 0; n < packCount; n++ )
    {
        packSaved[n] = packWanted[n];
        glGetIntegerv(packNames[n], &packSaved[n]);
        if ( packSaved[n] != packWanted[n] )
            glPixelStorei(packNames[n], packWanted[n]);
    }

    // B_RGB32 is blue, green, red and an unused byte in memory
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE,
                 bitmap->Bits());

    for ( size_t n = 0; n < packCount; n++ )
    {
        if ( packSaved[n] != packWanted[n] )
            glPixelStorei(packNames[n], packSaved[n]);
    }

    if ( packBuffer )
        glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
    if ( readBuffer != GL_BACK )
        glReadBuffer(readBuffer);
    if ( readFramebuffer )
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);

    if ( !gs_kit.hasPackInvert )
    {
        // OpenGL starts with the bottom row
        const size_t rowSize = bitmap->BytesPerRow();
        wxUint8 * const bits = (wxUint8 *)bitmap->Bits();
        std::vector<wxUint8> row(rowSize);
        for ( int top = 0, bottom = height - 1; top < bottom; top++, bottom-- )
        {
            memcpy(&row[0], bits + top * rowSize, rowSize);
            memcpy(bits + top * rowSize, bits + bottom * rowSize, rowSize);
            memcpy(bits + bottom * rowSize, &row[0], rowSize);
        }
    }
}

// the context does not render for its canvas any more
void Detach(wxHaikuGLContextData *context)
{
    if ( context->canvas )
    {
        context->canvas->HaikuSetContextData(NULL);
        context->canvas = NULL;
    }
}

// ----------------------------------------------------------------------------
// attributes
// ----------------------------------------------------------------------------

// Both lists are stored as pairs of a WX_GL_XXX constant and a value (1 for
// those without one), ended by 0.

// Translates the attributes to the options of BGLView. Returns false if they
// ask for something the kit does not have; multisampling and sRGB, which a
// program can do without, are only refused if strict.
bool TranslateAttributes(const wxGLAttributes& attrs, unsigned long *options,
                         bool strict)
{
    // Always double buffered: the frames get on the screen in SwapBuffers()
    // only.
    unsigned long opts = BGL_RGB | BGL_DOUBLE;

    int sampleBuffers = -1,
        samples = -1;

    const int * const values = attrs.GetGLAttrs();
    const int count = attrs.GetSize();
    for ( int n = 0; values && n + 1 < count && values[n]; n += 2 )
    {
        const int value = values[n + 1];

        switch ( values[n] )
        {
            case WX_GL_RGBA:
            case WX_GL_DOUBLEBUFFER:
                break;

            case WX_GL_BUFFER_SIZE:
                if ( value > 32 )
                    return false;
                break;

            case WX_GL_LEVEL:
                // no overlay or underlay planes
                if ( value != 0 )
                    return false;
                break;

            case WX_GL_STEREO:
                return false;

            case WX_GL_AUX_BUFFERS:
                if ( value > 0 )
                    return false;
                break;

            case WX_GL_MIN_RED:
            case WX_GL_MIN_GREEN:
            case WX_GL_MIN_BLUE:
                if ( value > 8 )
                    return false;
                break;

            case WX_GL_MIN_ALPHA:
                if ( value > 8 )
                    return false;
                if ( value > 0 )
                    opts |= BGL_ALPHA;
                break;

            case WX_GL_DEPTH_SIZE:
                // the renderers have 24 bits: what a program asking for the
                // traditional 32 gets everywhere else too
                if ( value > 32 )
                    return false;
                if ( value > 0 )
                    opts |= BGL_DEPTH;
                break;

            case WX_GL_STENCIL_SIZE:
                if ( value > 8 )
                    return false;
                // the stencil buffer comes with the depth buffer
                if ( value > 0 )
                    opts |= BGL_DEPTH | BGL_STENCIL;
                break;

            case WX_GL_MIN_ACCUM_RED:
            case WX_GL_MIN_ACCUM_GREEN:
            case WX_GL_MIN_ACCUM_BLUE:
            case WX_GL_MIN_ACCUM_ALPHA:
                if ( value > 16 )
                    return false;
                if ( value > 0 )
                    opts |= BGL_ACCUM;
                break;

            case WX_GL_SAMPLE_BUFFERS:
                sampleBuffers = value;
                break;

            case WX_GL_SAMPLES:
                samples = value;
                break;

            case WX_GL_FRAMEBUFFER_SRGB:
                if ( strict )
                    return false;
                break;

            default:
                return false;
        }
    }

    // BGLView has no option for multisampled buffers and the renderers
    // create none.
    if ( strict && (sampleBuffers > 0 || samples > 1) )
        return false;

    if ( options )
        *options = opts;

    return true;
}

} // anonymous namespace

// ============================================================================
// wxGLContextAttrs
// ============================================================================

wxGLContextAttrs& wxGLContextAttrs::CoreProfile()
{
    AddAttribute(WX_GL_CORE_PROFILE);
    AddAttribute(1);
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::MajorVersion(int val)
{
    if ( val > 0 )
    {
        AddAttribute(WX_GL_MAJOR_VERSION);
        AddAttribute(val);
    }
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::MinorVersion(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_MINOR_VERSION);
        // 0 ends the list
        AddAttribute(val + 1);
    }
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::CompatibilityProfile()
{
    AddAttribute(wx_GL_COMPAT_PROFILE);
    AddAttribute(1);
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ForwardCompatible()
{
    AddAttribute(WX_GL_FORWARD_COMPAT);
    AddAttribute(1);
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ES2()
{
    AddAttribute(WX_GL_ES2);
    AddAttribute(1);
    return *this;
}

// The kit creates its contexts without asking: a debug or robust context
// cannot be requested, the following are accepted and have no effect.

wxGLContextAttrs& wxGLContextAttrs::DebugCtx()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::Robust()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::NoResetNotify()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::LoseOnReset()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ResetIsolation()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ReleaseFlush(int WXUNUSED(val))
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::PlatformDefaults()
{
    return *this;
}

void wxGLContextAttrs::EndList()
{
    AddAttribute(0);
}

// ============================================================================
// wxGLAttributes
// ============================================================================

wxGLAttributes& wxGLAttributes::RGBA()
{
    AddAttribute(WX_GL_RGBA);
    AddAttribute(1);
    return *this;
}

wxGLAttributes& wxGLAttributes::BufferSize(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_BUFFER_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Level(int val)
{
    AddAttribute(WX_GL_LEVEL);
    AddAttribute(val);
    return *this;
}

wxGLAttributes& wxGLAttributes::DoubleBuffer()
{
    AddAttribute(WX_GL_DOUBLEBUFFER);
    AddAttribute(1);
    return *this;
}

wxGLAttributes& wxGLAttributes::Stereo()
{
    AddAttribute(WX_GL_STEREO);
    AddAttribute(1);
    return *this;
}

wxGLAttributes& wxGLAttributes::AuxBuffers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_AUX_BUFFERS);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::MinRGBA(int mRed, int mGreen, int mBlue,
                                        int mAlpha)
{
    if ( mRed >= 0 )
    {
        AddAttribute(WX_GL_MIN_RED);
        AddAttribute(mRed);
    }
    if ( mGreen >= 0 )
    {
        AddAttribute(WX_GL_MIN_GREEN);
        AddAttribute(mGreen);
    }
    if ( mBlue >= 0 )
    {
        AddAttribute(WX_GL_MIN_BLUE);
        AddAttribute(mBlue);
    }
    if ( mAlpha >= 0 )
    {
        AddAttribute(WX_GL_MIN_ALPHA);
        AddAttribute(mAlpha);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Depth(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_DEPTH_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Stencil(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_STENCIL_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::MinAcumRGBA(int mRed, int mGreen, int mBlue,
                                            int mAlpha)
{
    if ( mRed >= 0 )
    {
        AddAttribute(WX_GL_MIN_ACCUM_RED);
        AddAttribute(mRed);
    }
    if ( mGreen >= 0 )
    {
        AddAttribute(WX_GL_MIN_ACCUM_GREEN);
        AddAttribute(mGreen);
    }
    if ( mBlue >= 0 )
    {
        AddAttribute(WX_GL_MIN_ACCUM_BLUE);
        AddAttribute(mBlue);
    }
    if ( mAlpha >= 0 )
    {
        AddAttribute(WX_GL_MIN_ACCUM_ALPHA);
        AddAttribute(mAlpha);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::SampleBuffers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_SAMPLE_BUFFERS);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Samplers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_SAMPLES);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::FrameBuffersRGB()
{
    AddAttribute(WX_GL_FRAMEBUFFER_SRGB);
    AddAttribute(1);
    return *this;
}

void wxGLAttributes::EndList()
{
    AddAttribute(0);
}

wxGLAttributes& wxGLAttributes::PlatformDefaults()
{
    return *this;
}

wxGLAttributes& wxGLAttributes::Defaults()
{
    // no multisampling, unlike in the other ports: there is none here and
    // the defaults should be something IsDisplaySupported() agrees to
    RGBA().DoubleBuffer().Depth(16);
    return *this;
}

void wxGLAttributes::AddDefaultsForWXBefore31()
{
    // ParseAttribList() will add EndList(), don't do it now
    RGBA().DoubleBuffer().Depth(16);
}

// ============================================================================
// wxGLContext
// ============================================================================

wxIMPLEMENT_CLASS(wxGLContext, wxObject);

wxGLContext::wxGLContext(wxGLCanvas *win,
                         const wxGLContext *other,
                         const wxGLContextAttrs *ctxAttrs)
    : m_data(new wxHaikuGLContextData)
{
    m_isOk = false;

    if ( !be_app )
        return;

    const unsigned long
        options = win ? win->GetHaikuGLOptions() : DEFAULT_OPTIONS;

    if ( !KitIsOk(options) )
        return;

    // what was asked for: explicitly or in the attribute list of the canvas
    const wxGLContextAttrs *attrs = ctxAttrs;
    if ( !attrs && win && win->GetGLCTXAttrs().GetGLAttrs() )
        attrs = &win->GetGLCTXAttrs();

    int major = 0,
        minor = 0;
    bool core = false;

    if ( attrs )
    {
        const int * const values = attrs->GetGLAttrs();
        const int count = attrs->GetSize();
        for ( int n = 0; values && n + 1 < count && values[n]; n += 2 )
        {
            switch ( values[n] )
            {
                case WX_GL_MAJOR_VERSION:
                    major = values[n + 1];
                    break;

                case WX_GL_MINOR_VERSION:
                    minor = values[n + 1] - 1;
                    break;

                case WX_GL_CORE_PROFILE:
                    core = true;
                    break;

                case WX_GL_ES2:
                    // the kit only creates desktop OpenGL contexts
                    return;

                default:
                    break;
            }
        }
    }

    // core profiles start with 3.2
    if ( core && (major < 3 || (major == 3 && minor < 2)) )
    {
        major = 3;
        minor = 2;
    }

    // All contexts are what the kit makes them: the compatibility profile of
    // the highest version of the renderer, which has everything the core
    // profile of the same version has.
    if ( gs_kit.major < major
            || (gs_kit.major == major && gs_kit.minor < minor) )
    {
        return;
    }

    wxSize size(16, 16);
    if ( win )
    {
        size = win->HaikuGetViewSize();
        size.IncTo(wxSize(1, 1));
    }

    wxHaikuGLContextData * const
        share = other && other->m_data->view ? other->m_data : NULL;

    if ( gs_spareView && !share && gs_spareOptions == options )
    {
        m_data->view = gs_spareView;
        m_data->size = wxSize(16, 16);
        gs_spareView = NULL;

        SetContextSize(m_data, size);
    }
    else
    {
        m_data->view = CreateView(options, size, share);
        m_data->size = size;

        if ( gs_spareView )
        {
            // only now: the renderers shut down with their last context
            DeleteView(gs_spareView);
            gs_spareView = NULL;
        }
    }

    m_data->options = options;

    m_isOk = m_data->view != NULL;
}

wxGLContext::~wxGLContext()
{
    if ( m_data->view )
    {
        if ( gs_current == m_data )
            MakeCurrent(NULL);

        Detach(m_data);
        DeleteView(m_data->view);
    }

    delete m_data;
}

bool wxGLContext::SetCurrent(const wxGLCanvas& win) const
{
    wxHaikuGLContextData * const context = m_data;
    if ( !context->view )
        return false;

    if ( context->canvas != &win )
    {
        Detach(context);

        // another context rendered for this canvas
        if ( win.HaikuGetContextData() )
            Detach(win.HaikuGetContextData());

        context->canvas = &win;
        win.HaikuSetContextData(context);
    }

    wxSize size = win.HaikuGetViewSize();
    size.IncTo(wxSize(1, 1));
    SetContextSize(context, size);

    MakeCurrent(context);

    if ( !context->initialized )
    {
        // as for any context made current for the first time, whatever the
        // size of its view was meanwhile
        context->initialized = true;

        glViewport(0, 0, size.x, size.y);
        glScissor(0, 0, size.x, size.y);
    }

    return true;
}

/* static */
void wxGLContext::ClearCurrent()
{
    MakeCurrent(NULL);
}

/* static */
void wxGLContext::HaikuGetVersion(int *major, int *minor)
{
    KitIsOk(DEFAULT_OPTIONS);

    if ( major )
        *major = gs_kit.major;
    if ( minor )
        *minor = gs_kit.minor;
}

// ============================================================================
// wxGLCanvas
// ============================================================================

wxIMPLEMENT_CLASS(wxGLCanvas, wxWindow);

void wxGLCanvas::Init()
{
    m_glOptions = DEFAULT_OPTIONS;
    m_context = NULL;
    m_frame = NULL;
}

wxGLCanvas::wxGLCanvas(wxWindow *parent,
                       const wxGLAttributes& dispAttrs,
                       wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name)
{
    Init();

    Create(parent, dispAttrs, id, pos, size, style, name);
}

wxGLCanvas::wxGLCanvas(wxWindow *parent,
                       wxWindowID id,
                       const int *attribList,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name)
{
    Init();

    Create(parent, id, pos, size, style, name, attribList);
}

bool wxGLCanvas::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name,
                        const int *attribList)
{
    // separate the display and the context attributes: the latter are used
    // by the wxGLContext created for this canvas without attributes
    wxGLAttributes dispAttrs;
    if ( !ParseAttribList(attribList, dispAttrs, &m_GLCTXAttrs) )
        return false;

    return Create(parent, dispAttrs, id, pos, size, style, name);
}

bool wxGLCanvas::Create(wxWindow *parent,
                        const wxGLAttributes& dispAttrs,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name)
{
    if ( !TranslateAttributes(dispAttrs, &m_glOptions, false) )
    {
        wxLogError(_("The OpenGL display attributes are not supported."));
        return false;
    }

    if ( !KitIsOk(m_glOptions) )
    {
        wxLogError(_("OpenGL is not available: no renderer was found."));
        return false;
    }

    // The view is all OpenGL: wxUniversal cannot draw a border in it.
    style &= ~wxBORDER_MASK;
    style |= wxBORDER_NONE;

    return wxWindow::Create(parent, id, pos, size, style, name);
}

wxGLCanvas::~wxGLCanvas()
{
    if ( m_context )
        Detach(m_context);

    delete m_frame;
}

void wxGLCanvas::DoMoveWindow(int x, int y, int width, int height)
{
    wxGLCanvasBase::DoMoveWindow(x, y, width, height);

    // the buffers of the context rendering for this canvas follow, also for
    // a program which makes its context current only once
    if ( m_context )
    {
        wxSize size = HaikuGetViewSize();
        size.IncTo(wxSize(1, 1));
        SetContextSize(m_context, size);
    }
}

BBitmap *wxGLCanvas::HaikuGetFrameBitmap(int width, int height) const
{
    if ( m_frame )
    {
        const BRect bounds = m_frame->Bounds();
        if ( bounds.IntegerWidth() + 1 == width
                && bounds.IntegerHeight() + 1 == height )
        {
            return m_frame;
        }

        // nothing draws it any more: it is always drawn synchronously
        delete m_frame;
        m_frame = NULL;
    }

    BBitmap * const
        bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
    if ( bitmap->InitCheck() != B_OK )
    {
        delete bitmap;
        return NULL;
    }

    m_frame = bitmap;

    return m_frame;
}

void wxGLCanvas::HaikuShowFrame(const wxRect *rect)
{
    if ( !m_frame || !m_view )
        return;

    wxHaikuViewLock lock(m_view);
    if ( !lock.IsLocked() )
        return;

    // a wxDC may have drawn on the canvas and left its state in the view
    wxHaikuDrawView * const view = dynamic_cast<wxHaikuDrawView *>(m_view);
    if ( !view || view->m_stateOwner )
    {
        if ( view )
            view->m_stateOwner = NULL;

        m_view->ConstrainClippingRegion(NULL);
        m_view->SetDrawingMode(B_OP_COPY);
    }

    // The synchronous calls: app_server has copied the pixels when they
    // return, the next frame may be read into the bitmap.
    if ( rect )
    {
        const BRect part = wxHaikuRect(*rect) & m_frame->Bounds();
        if ( part.IsValid() )
            m_view->DrawBitmap(m_frame, part, part);
    }
    else
    {
        m_view->DrawBitmap(m_frame, B_ORIGIN);
    }
}

void wxGLCanvas::HaikuHandleEvent(const wxHaikuEvent& event)
{
    if ( event.type == wxHAIKU_EVT_DRAW && m_frame )
    {
        // Something of the canvas was exposed: show the last frame at once
        // if it still fits. The program renders the next one when it gets
        // the paint event, which may take it a while.
        const BRect bounds = m_frame->Bounds();
        if ( wxSize(bounds.IntegerWidth() + 1, bounds.IntegerHeight() + 1)
                == HaikuGetViewSize() )
        {
            const wxRect rect = wxFromHaikuRect(event.rect);
            HaikuShowFrame(&rect);
        }
    }

    wxGLCanvasBase::HaikuHandleEvent(event);
}

bool wxGLCanvas::SwapBuffers()
{
    // the frame is in the back buffer of the context which was made current
    // for this canvas
    wxHaikuGLContextData * const context = m_context;
    if ( !context || !m_view )
        return false;

    BBitmap * const
        bitmap = HaikuGetFrameBitmap(context->size.x, context->size.y);
    if ( !bitmap )
        return false;

    wxHaikuGLContextData * const current = gs_current;
    MakeCurrent(context);
    ReadFrame(context, bitmap);
    MakeCurrent(current);

    HaikuShowFrame();

    return true;
}

/* static */
bool wxGLCanvasBase::IsDisplaySupported(const wxGLAttributes& dispAttrs)
{
    unsigned long options;
    return TranslateAttributes(dispAttrs, &options, true) && KitIsOk(options);
}

/* static */
bool wxGLCanvasBase::IsDisplaySupported(const int *attribList)
{
    wxGLAttributes dispAttrs;
    ParseAttribList(attribList, dispAttrs);

    return IsDisplaySupported(dispAttrs);
}

/* static */
bool wxGLCanvasBase::IsExtensionSupported(const char *extension)
{
    if ( !KitIsOk(DEFAULT_OPTIONS) )
        return false;

    return IsExtensionInList(gs_kit.extensions.c_str(), extension);
}

// ============================================================================
// wxGLApp
// ============================================================================

// its class information is in glcmn.cpp

bool wxGLApp::InitGLVisual(const int *attribList)
{
    // there are no visuals to choose from
    return wxGLCanvas::IsDisplaySupported(attribList);
}

#endif // wxUSE_GLCANVAS
