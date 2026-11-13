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
//  - A wxGLContext owns a BGLView (wxHaikuGLView): its context and the
//    buffers it renders into. The view has the size of the canvas the
//    context is current for (the renderers allocate their buffers again when
//    the context is made current after a change of size).
//  - A wxGLCanvas is an ordinary wx window with an ordinary view.
//  - The direct path: the view of the context is a child of the view of the
//    canvas it renders for, covering it, and wxGLCanvas::SwapBuffers() is
//    BGLView::SwapBuffers(): the kit shows the frame as it does for any
//    program. Making the context current for another canvas moves the view
//    there.
//  - The read back path: the view of the context is in no window and
//    SwapBuffers() reads the back buffer into a BBitmap of the canvas
//    (glReadPixels(), rows inverted by Mesa) and draws it. This is what
//    happens while the context renders for several canvases which are on
//    screen at the same time: the view cannot be in two places, and moving
//    it would leave the other canvas empty. The bitmap stays, an exposed
//    canvas shows its last frame at once. WXHAIKU_GL_READBACK=1 in the
//    environment makes all contexts work like this.
//  - Contexts share their objects if asked to (BGL_SHARE_CONTEXT, which
//    shares with the context current when the view is created).
//
// Threads and locks
// =================
//
// What the two implementations of the kit do (Mesa's src/hgl with BGLRenderer
// add-ons in the x86_64 images, libglvnd on EGL in the arm64 ones):
//
//  - BGLView::LockGL() takes fDisplayLock, a BLocker of the view, and makes
//    the context current for the calling thread; UnlockGL() leaves the
//    thread without context. No looper is locked by either.
//  - SwapBuffers() of the classic kit shows the frame from the calling
//    thread: it locks the looper of the view (with a timeout of one second,
//    the frame is dropped if it expires) and draws the bitmap of the frame.
//    Its BGLView::Draw() draws the front buffer the same way but without any
//    GL lock: called by a looper thread it races with the rendering thread.
//  - SwapBuffers() of the libglvnd kit hands the frame to the view under a
//    mutex nothing else is done under and posts B_INVALIDATE to it:
//    BGLView::Draw() on the looper thread is what shows the frame.
//  - AttachedToWindow() and FrameResized() store the size of the view in the
//    renderer; AttachedToWindow() also makes the context current to set the
//    viewport and leaves the thread without context.
//
// The rules of this file:
//
//  1. OpenGL belongs to the wx (main) thread. It is the only thread which
//     calls LockGL(), UnlockGL(), SwapBuffers() or any gl function, and it
//     keeps the GL lock of the view whose context is current for as long as
//     it is current, that is between frames too: wx knows no "release".
//     MakeCurrent() is the only place which locks and unlocks.
//  2. The looper threads never take a GL lock and never touch a renderer in
//     a way which could wait for the main thread. The hooks of wxHaikuGLView
//     (it has a looper only while it is the child of a canvas) post event
//     records for the canvas as those of wxHaikuView do, and:
//       - Draw() posts wxHAIKU_EVT_DRAW: wx renders the canvas again. It
//         does not call BGLView::Draw() in the classic kit (the race above;
//         the main thread shows the front buffer again when it gets the
//         record). In the libglvnd kit it must, this being how frames get
//         on the screen there, and can: only the frame mutex is taken.
//       - FrameResized() does nothing: see rule 5.
//       - AttachedToWindow() runs in the thread which calls AddChild(), the
//         main thread.
//     So a looper thread holding its looper lock never waits for a lock the
//     main thread holds. The main thread may wait for a looper lock while it
//     holds a GL lock (SwapBuffers() of the classic kit, drawing the frame
//     bitmap, moving the view): there is no cycle, and the timeout of the
//     kit cannot expire because the looper lock is only ever held for the
//     time of a hook which does not block.
//  3. The main thread does not hold a looper lock when it calls
//     BGLView::SwapBuffers(): the libglvnd kit sends a message to the looper
//     there, which blocks while the port of the looper is full, and a looper
//     waiting for its own lock does not empty its port.
//  4. Moving the view of a context between canvases (RemoveSelf(),
//     AddChild()) is done by the main thread with the context current, under
//     the looper lock of the window concerned; the handle its hooks post
//     records with changes while the view is in no window.
//  5. Resizing: only wx resizes views. The main thread resizes the view of
//     the context with the canvas and calls BGLView::FrameResized() itself,
//     which only stores the size in the renderer; making the context current
//     again then gives the buffers their size before the next frame. The
//     B_VIEW_RESIZED the looper gets later is ignored: it could only bring
//     an older size back.
//  6. Creating a BGLView and attaching it make its context current behind
//     the back of wx: CreateView() and AttachView() restore what was.

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_GLCANVAS

// the functions of OpenGL 1.5 and later
#define GL_GLEXT_PROTOTYPES

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/cursor.h"
    #include "wx/intl.h"
    #include "wx/log.h"
#endif

#include "wx/glcanvas.h"

#include "wx/haiku/private.h"

#include <Cursor.h>
#include <GLView.h>
#include <GL/glext.h>

#include <dlfcn.h>

#ifndef GL_PACK_INVERT_MESA
    #define GL_PACK_INVERT_MESA 0x8758
#endif

// ============================================================================
// wxHaikuGLView: the view of a context
// ============================================================================

// Its hooks run on the looper thread of the window of the canvas the view is
// the child of, except for AttachedToWindow(): see the rules above.
class wxHaikuGLView : public BGLView
{
public:
    wxHaikuGLView(BRect frame, unsigned long options);

    virtual void AttachedToWindow();
    virtual void Draw(BRect updateRect);
    virtual void FrameResized(float newWidth, float newHeight);
    virtual void MouseDown(BPoint where);
    virtual void MouseUp(BPoint where);
    virtual void MouseMoved(BPoint where, uint32 transit,
                            const BMessage *dragMessage);
    virtual void MessageReceived(BMessage *message);

    // the handle of the canvas the view is the child of: set by the main
    // thread while the view is in no window
    wxUint32 m_handle;

    // Set by the main thread before it swaps the buffers in the libglvnd
    // kit: the Draw() this results in must not make wx render again.
    int32 m_swapped;
};

// ============================================================================
// contexts
// ============================================================================

// What a wxGLContext is. The canvases refer to this, not to the wxGLContext.
struct wxHaikuGLContextData
{
    wxHaikuGLContextData()
        : view(NULL), options(0), canvas(NULL), previous(NULL),
          attached(false), frameShown(false), initialized(false)
    {
    }

    wxHaikuGLView *view;
    unsigned long options;

    // the size of the canvas the context renders for
    wxSize size;

    // the size of the view, which the renderer was told: see BufferSize()
    wxSize bufferSize;

    // the canvas the context renders for
    const wxGLCanvas *canvas;

    // the canvas it rendered for before, which may still be on screen
    const wxGLCanvas *previous;

    // the view is a child of the view of the canvas: the direct path
    bool attached;

    // and the kit has shown a frame since the buffers were allocated
    bool frameShown;

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

// all the contexts with a view
std::vector<wxHaikuGLContextData *> gs_contexts;

// the context whose GL lock the main thread holds, which is current
wxHaikuGLContextData *gs_current = NULL;

thread_id gs_mainThread = -1;

// The view the kit was probed with if no context was created yet: it becomes
// the view of the first context it suits.
wxHaikuGLView *gs_spareView = NULL;
unsigned long gs_spareOptions = 0;

const unsigned long DEFAULT_OPTIONS = BGL_RGB | BGL_DOUBLE | BGL_DEPTH;

// The classic kit has BGLRenderer, the class of its renderer add-ons, the
// libglvnd one has not. This is what tells who shows the frames, see above.
bool KitPresentsInDraw()
{
    static int s_presentsInDraw = -1;
    if ( s_presentsInDraw == -1 )
    {
        s_presentsInDraw
            = dlsym(RTLD_DEFAULT, "_ZN11BGLRenderer7ReleaseEv") == NULL;
    }

    return s_presentsInDraw == 1;
}

// WXHAIKU_GL_READBACK: never the direct path
bool AlwaysReadBack()
{
    static int s_readBack = -1;
    if ( s_readBack == -1 )
    {
        const char * const value = getenv("WXHAIKU_GL_READBACK");
        s_readBack = value && *value && strcmp(value, "0") != 0;
    }

    return s_readBack == 1;
}

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
wxHaikuGLView *CreateView(unsigned long options, const wxSize& size,
                          wxHaikuGLContextData *share)
{
    wxCHECK_MSG( be_app, NULL, "OpenGL needs the application object" );

    gs_mainThread = find_thread(NULL);

    wxHaikuGLContextData * const current = gs_current;

    // BGL_SHARE_CONTEXT shares with the context which is current when the
    // view is created
    MakeCurrent(share);
    if ( share )
        options |= BGL_SHARE_CONTEXT;

    wxHaikuGLView * const
        view = new wxHaikuGLView(BRect(0, 0, size.x - 1, size.y - 1), options);

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

void DeleteView(wxHaikuGLView *view)
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

// The size of the buffers for a canvas of the given size. Rows of whole
// cache lines: this is what llvmpipe wants and what it tells the window
// system layer of the classic kit, which (Mesa 22.0.5) creates the bitmap of
// the colour buffer with rows of just the width nevertheless. With any other
// width the frame is sheared and the renderer writes past the end of the
// bitmap. The surplus columns are at the right: where nothing looks, or
// clipped by the view of the canvas.
wxSize BufferSize(const wxSize& size)
{
    return wxSize((size.x + 15) & ~15, size.y);
}

// The context renders for a canvas of this size from now on.
void SetContextSize(wxHaikuGLContextData *context, const wxSize& size)
{
    context->size = size;

    const wxSize bufferSize = BufferSize(size);
    if ( bufferSize == context->bufferSize )
        return;

    context->bufferSize = bufferSize;
    context->frameShown = false;

    wxHaikuGLView * const view = context->view;
    {
        // in a window only while it is the child of a canvas
        wxHaikuViewLock lock(view);
        if ( !lock.CanTouch() )
            return;

        view->ResizeTo(bufferSize.x - 1, bufferSize.y - 1);

        // Not the hook of wxHaikuGLView, which ignores it. This stores the
        // size in the renderer, nothing else.
        view->BGLView::FrameResized(bufferSize.x - 1, bufferSize.y - 1);
    }

    // the buffers are allocated when the context is made current
    if ( gs_current == context )
    {
        view->UnlockGL();
        view->LockGL();
    }
}

// The view of the context becomes the child of the view of the canvas, below
// the children the canvas may have: the direct path.
void AttachView(wxHaikuGLContextData *context, const wxGLCanvas *canvas)
{
    BView * const parent = canvas->GetHaikuView();
    wxHaikuGLView * const view = context->view;
    if ( context->attached || !parent )
        return;

    // BGLView::AttachedToWindow() locks GL, sets the viewport and unlocks:
    // with the context current this changes nothing but the viewport, which
    // the program does not expect to change.
    wxHaikuGLContextData * const current = gs_current;
    MakeCurrent(context);

    GLint viewport[4] = { 0, 0, context->size.x, context->size.y };
    glGetIntegerv(GL_VIEWPORT, viewport);

    view->m_handle = canvas->GetHaikuHandle();
    view->MoveTo(0, 0);

    {
        wxHaikuViewLock lock(parent);
        if ( lock.CanTouch() )
        {
            parent->AddChild(view, parent->ChildAt(0));
            context->attached = true;
        }
    }

    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    MakeCurrent(current);

    context->frameShown = false;

    if ( context->attached )
        canvas->HaikuGLViewAttached(view);
}

// The view of the context is in no window any more: the read back path.
void DetachView(wxHaikuGLContextData *context)
{
    if ( !context->attached )
        return;

    wxHaikuGLView * const view = context->view;
    {
        wxHaikuViewLock lock(view);
        if ( lock.CanTouch() )
            view->RemoveSelf();
    }

    view->m_handle = 0;
    context->attached = false;
    context->frameShown = false;
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
    DetachView(context);

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
// wxHaikuGLView
// ============================================================================

// The name starts with "wx" as those of all the views of wx windows do (the
// keys go to wx while such a view is the focus view; this one never is).
wxHaikuGLView::wxHaikuGLView(BRect frame, unsigned long options)
    : BGLView(frame, "wxGLContext", B_FOLLOW_NONE,
              B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE, options),
      m_handle(0),
      m_swapped(0)
{
    // BGLView insists on following all sides of its parent, wx moves and
    // resizes all views itself
    SetResizingMode(B_FOLLOW_NONE);
    SetViewColor(B_TRANSPARENT_COLOR);
    SetLowColor(0, 0, 0);
}

void wxHaikuGLView::AttachedToWindow()
{
    BGLView::AttachedToWindow();

    // BGLView wants a black background, which app_server would then fill
    // every exposed part with before wx renders it again
    SetViewColor(B_TRANSPARENT_COLOR);

    // AttachView() has the context of this view current, which then still
    // is. When a parent of the canvas is put into a window it is not and no
    // context is current any more: restore the one which was.
    if ( find_thread(NULL) == gs_mainThread
            && gs_current && gs_current->view != this )
    {
        gs_current->view->UnlockGL();
        gs_current->view->LockGL();
    }
}

void wxHaikuGLView::Draw(BRect updateRect)
{
    if ( KitPresentsInDraw() )
    {
        // shows the last frame
        BGLView::Draw(updateRect);

        // which is all there is to do if this is the update a swap asked for
        if ( atomic_get_and_set(&m_swapped, 0) )
            return;
    }

    // wx renders the canvas again when it gets to it
    wxHaikuEvent *event = new wxHaikuEvent(wxHAIKU_EVT_DRAW, m_handle);
    event->rect = updateRect;
    wxHaikuPostEvent(event);
}

void wxHaikuGLView::FrameResized(float WXUNUSED(newWidth),
                                 float WXUNUSED(newHeight))
{
    // the main thread told the renderer when it resized the view
}

void wxHaikuGLView::MouseDown(BPoint where)
{
    // keep getting the moves and the release when the pointer leaves the view
    // with the button down
    SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);

    wxHaikuPostMouseEvent(this, m_handle, wxHAIKU_EVT_MOUSE_DOWN, where);
}

void wxHaikuGLView::MouseUp(BPoint where)
{
    wxHaikuPostMouseEvent(this, m_handle, wxHAIKU_EVT_MOUSE_UP, where);
}

void wxHaikuGLView::MouseMoved(BPoint where, uint32 transit,
                               const BMessage *dragMessage)
{
    wxHaikuPostMouseEvent(this, m_handle, wxHAIKU_EVT_MOUSE_MOVED, where,
                          transit, dragMessage);
}

void wxHaikuGLView::MessageReceived(BMessage *message)
{
    if ( !wxHaikuHandleViewMessage(this, m_handle, message) )
        BGLView::MessageReceived(message);
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

                case WX_GL_ES2:
                    // the kit only creates desktop OpenGL contexts
                    return;

                default:
                    break;
            }
        }
    }

    // All contexts are what the kit makes them: the compatibility profile of
    // the highest version of the renderer, which has everything the core
    // profile of the same version has. So the profile asked for does not
    // matter and the version decides (without one, as in the attribute list
    // of BambuStudio, a core profile request means nothing elsewhere either).
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
        m_data->bufferSize = wxSize(16, 16);
        gs_spareView = NULL;

        SetContextSize(m_data, size);
    }
    else
    {
        m_data->view = CreateView(options, BufferSize(size), share);
        m_data->size = size;
        m_data->bufferSize = BufferSize(size);

        if ( gs_spareView )
        {
            // only now: the renderers shut down with their last context
            DeleteView(gs_spareView);
            gs_spareView = NULL;
        }
    }

    m_data->options = options;

    if ( m_data->view )
    {
        gs_contexts.push_back(m_data);
        m_isOk = true;
    }
}

wxGLContext::~wxGLContext()
{
    if ( m_data->view )
    {
        Detach(m_data);

        if ( gs_current == m_data )
            MakeCurrent(NULL);

        for ( size_t n = 0; n < gs_contexts.size(); n++ )
        {
            if ( gs_contexts[n] == m_data )
            {
                gs_contexts.erase(gs_contexts.begin() + n);
                break;
            }
        }

        DeleteView(m_data->view);
    }

    delete m_data;
}

bool wxGLContext::SetCurrent(const wxGLCanvas& win) const
{
    wxHaikuGLContextData * const context = m_data;
    if ( !context->view )
        return false;

    wxSize size = win.HaikuGetViewSize();
    size.IncTo(wxSize(1, 1));

    if ( context->canvas != &win || !context->attached )
    {
        if ( context->canvas != &win )
        {
            // The view leaves the canvas it was in, which gets its own view
            // exposed and renders again if it is on screen.
            const wxGLCanvas * const old = context->canvas;
            Detach(context);
            if ( old )
                context->previous = old;

            // another context rendered for this canvas
            if ( win.HaikuGetContextData() )
                Detach(win.HaikuGetContextData());

            context->canvas = &win;
            win.HaikuSetContextData(context);
        }

        SetContextSize(context, size);

        // The view of the context goes into the canvas unless the canvas
        // the context rendered for before is on screen too: the context
        // probably goes on rendering for both then, and its view cannot be
        // in both.
        const bool direct = !AlwaysReadBack()
            && (!context->previous || context->previous == &win
                    || !context->previous->IsShownOnScreen());
        if ( direct )
            AttachView(context, &win);
    }
    else
    {
        SetContextSize(context, size);
    }

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
    for ( size_t n = 0; n < gs_contexts.size(); n++ )
    {
        if ( gs_contexts[n]->previous == this )
            gs_contexts[n]->previous = NULL;
    }

    // takes the view of the context out of ours, which the base class deletes
    if ( m_context )
        Detach(m_context);

    delete m_frame;
}

bool wxGLCanvas::SetCursor(const wxCursor& cursor)
{
    if ( !wxGLCanvasBase::SetCursor(cursor) )
        return false;

    if ( m_context && m_context->attached )
        HaikuGLViewAttached(m_context->view);

    return true;
}

void wxGLCanvas::HaikuGLViewAttached(BView *view) const
{
    // the pointer is over the view of the context, not over ours: it shows
    // the cursor of the canvas or the one the canvas inherits
    const wxCursor *cursor = NULL;
    for ( const wxWindowHaiku *win = this; win; win = win->GetParent() )
    {
        if ( win->GetCursor().IsOk() )
        {
            cursor = &win->GetCursor();
            break;
        }

        if ( win->IsTopLevel() )
            break;
    }

    const BCursor * const native = cursor ? cursor->GetHaikuCursor() : NULL;

    wxHaikuViewLock lock(view);
    if ( !lock.IsLocked() )
        return;

    if ( native )
    {
        view->SetViewCursor(native, false);
    }
    else
    {
        BCursor standard(B_CURSOR_ID_SYSTEM_DEFAULT);
        view->SetViewCursor(&standard, false);
    }
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

    // a wxDC or a wxGraphicsContext may have drawn on the canvas and left
    // its state in the view
    wxHaikuDrawView * const view = dynamic_cast<wxHaikuDrawView *>(m_view);
    if ( !view || view->m_stateOwner || view->m_transformed )
    {
        if ( view )
        {
            view->m_stateOwner = NULL;

            if ( view->m_transformed )
            {
                m_view->SetTransform(BAffineTransform());
                view->m_transformed = false;
            }
        }

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
    if ( event.type == wxHAIKU_EVT_DRAW )
    {
        // Something of the canvas was exposed: show the last frame at once.
        // The program renders the next one when it gets the paint event,
        // which may take it a while.
        if ( m_context && m_context->attached )
        {
            // The libglvnd kit has done it in the Draw() hook. The classic
            // one draws its front buffer, which only this thread may ask
            // for.
            if ( !KitPresentsInDraw() && m_context->frameShown )
            {
                wxHaikuViewLock lock(m_context->view);
                if ( lock.IsLocked() )
                    m_context->view->BGLView::Draw(event.rect);
            }
        }
        else if ( m_frame )
        {
            const BRect bounds = m_frame->Bounds();
            if ( wxSize(bounds.IntegerWidth() + 1, bounds.IntegerHeight() + 1)
                    == HaikuGetViewSize() )
            {
                const wxRect rect = wxFromHaikuRect(event.rect);
                HaikuShowFrame(&rect);
            }
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

    if ( context->attached )
    {
        // the kit shows the frame
        delete m_frame;
        m_frame = NULL;

        wxHaikuGLContextData * const current = gs_current;
        MakeCurrent(context);

        if ( KitPresentsInDraw() )
            atomic_set(&context->view->m_swapped, 1);

        // The looper must not be locked here, see the top of this file.
        context->view->SwapBuffers();
        context->frameShown = true;

        MakeCurrent(current);

        return true;
    }

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
