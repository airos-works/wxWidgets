/////////////////////////////////////////////////////////////////////////////
// Name:        build/haiku/tests/gltest.cpp
// Purpose:     wxGLCanvas / wxGLContext test: what BambuStudio does with them
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// build/haiku/tests/build-test.sh gltest; build/haiku/vm/run.sh gltest -- ...
//
//   --size=WxH      client size of the canvas (400x400)
//   --bambu         the attribute list of BambuStudio: alpha, 24 bits of
//                   depth, stencil, WX_GL_CORE_PROFILE
//   --two           a second frame whose canvas is rendered with the same
//                   wxGLContext: the state and the objects OpenGL never
//                   shares (vertex array, frame buffer object) must follow
//   --nodc          the paint handler creates no wxPaintDC and renders
//                   nothing: the canvases are rendered in idle time
//   --bench=N       N frames as fast as they get on the screen, then fps
//   --sizes=AxB,... the same for each of these sizes of the (first) canvas
//   --events        prints the input events of the canvases
//   --spin          keeps rotating (a 16 ms timer)
//   --quit          closes the windows when the script is done
//
// Prints lines starting with GL (information), OK/FAIL (checks), BENCH and
// EV on stdout.

#define GL_GLEXT_PROTOTYPES

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#include "wx/glcanvas.h"
#include "wx/stopwatch.h"
#include "wx/tokenzr.h"

#include <GL/glext.h>

#include <stdio.h>
#include <vector>

namespace
{

struct Options
{
    Options()
        : size(400, 400), bambu(false), two(false), nodc(false), bench(0),
          events(false), spin(false), quit(false)
    {
    }

    wxSize size;
    bool bambu, two, nodc;
    int bench;
    std::vector<wxSize> sizes;
    bool events, spin, quit;
} gs_options;

void Check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "OK  " : "FAIL", what);
    fflush(stdout);
}

bool ParseSize(const wxString& text, wxSize *size)
{
    long w, h;
    if ( !text.BeforeFirst('x').ToLong(&w) || !text.AfterFirst('x').ToLong(&h) )
        return false;

    *size = wxSize(w, h);
    return true;
}

const char *kVertexShader =
    "#version 130\n"
    "in vec2 pos;\n"
    "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";

const char *kFragmentShader =
    "#version 130\n"
    "out vec4 colour;\n"
    "void main() { colour = vec4(1.0, 0.8, 0.0, 1.0); }\n";

} // anonymous namespace

// ----------------------------------------------------------------------------
// the context: all the state is set up once, for whatever canvas comes first
// ----------------------------------------------------------------------------

class TestContext : public wxGLContext
{
public:
    explicit TestContext(wxGLCanvas *canvas);

    void Draw(const wxSize& size, float angle, int index);

    bool m_modern;
    GLuint m_vao, m_vbo, m_program, m_fbo, m_fboTexture, m_texture;
};

TestContext::TestContext(wxGLCanvas *canvas)
    : wxGLContext(canvas),
      m_modern(false), m_vao(0), m_vbo(0), m_program(0), m_fbo(0),
      m_fboTexture(0), m_texture(0)
{
    Check(IsOK(), "wxGLContext::IsOK()");
    if ( !IsOK() )
        return;

    SetCurrent(*canvas);

    GLint v[4] = { 0, 0, 0, 0 };
    printf("GL vendor: %s\n", (const char *)glGetString(GL_VENDOR));
    printf("GL renderer: %s\n", (const char *)glGetString(GL_RENDERER));
    printf("GL version: %s\n", (const char *)glGetString(GL_VERSION));
    printf("GL shading language: %s\n",
           (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));

    int major = 0, minor = 0;
    wxGLContext::HaikuGetVersion(&major, &minor);
    printf("GL HaikuGetVersion: %d.%d\n", major, minor);
    m_modern = major >= 3;

    if ( m_modern )
    {
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, v);
        printf("GL profile mask: %d (%s)\n", v[0],
               v[0] & GL_CONTEXT_CORE_PROFILE_BIT ? "core" : "compatibility");
        glGetIntegerv(GL_MAX_SAMPLES, v);
        printf("GL max samples (frame buffer objects): %d\n", v[0]);
    }

    GLint red, alpha, depth, stencil, samples, dbl;
    glGetIntegerv(GL_RED_BITS, &red);
    glGetIntegerv(GL_ALPHA_BITS, &alpha);
    glGetIntegerv(GL_DEPTH_BITS, &depth);
    glGetIntegerv(GL_STENCIL_BITS, &stencil);
    glGetIntegerv(GL_SAMPLES, &samples);
    glGetIntegerv(GL_DOUBLEBUFFER, &dbl);
    printf("GL buffers: red %d alpha %d depth %d stencil %d samples %d "
           "double %d\n", red, alpha, depth, stencil, samples, dbl);
    glGetIntegerv(GL_VIEWPORT, v);
    printf("GL initial viewport: %d %d %d %d, canvas %dx%d\n",
           v[0], v[1], v[2], v[3],
           canvas->GetClientSize().x, canvas->GetClientSize().y);
    Check(v[2] == canvas->GetClientSize().x && v[3] == canvas->GetClientSize().y,
          "initial viewport is the client size");
    printf("GL content scale factor: %g\n", canvas->GetContentScaleFactor());

    if ( gs_options.bambu )
    {
        Check(alpha == 8, "8 bits of alpha");
        Check(depth >= 24, "24 bits of depth");
        Check(stencil == 8, "8 bits of stencil");
    }

    static const char * const extensions[] =
    {
        "GL_ARB_framebuffer_object", "GL_ARB_vertex_array_object",
        "GL_EXT_texture_compression_s3tc", "GL_ARB_texture_float",
        "GL_EXT_framebuffer_multisample", "GL_KHR_debug",
        "GL_EXT_texture_filter_anisotropic", "GL_ARB_compatibility",
        "GL_MESA_pack_invert"
    };
    for ( size_t n = 0; n < WXSIZEOF(extensions); n++ )
    {
        printf("GL extension %s: %s\n", extensions[n],
               wxGLCanvas::IsExtensionSupported(extensions[n]) ? "yes" : "no");
    }

    // BambuStudio detects multisampling like this
    const int msaa[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 24,
                         WX_GL_SAMPLE_BUFFERS, 1, WX_GL_SAMPLES, 4, 0 };
    const int plain[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_MIN_RED, 8,
                          WX_GL_MIN_GREEN, 8, WX_GL_MIN_BLUE, 8,
                          WX_GL_MIN_ALPHA, 8, WX_GL_DEPTH_SIZE, 24,
                          WX_GL_STENCIL_SIZE, 8, 0 };
    const int stereo[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_STEREO, 0 };
    printf("GL IsDisplaySupported: multisampled %d, plain %d, stereo %d, "
           "NULL %d\n",
           (int)wxGLCanvas::IsDisplaySupported(msaa),
           (int)wxGLCanvas::IsDisplaySupported(plain),
           (int)wxGLCanvas::IsDisplaySupported(stereo),
           (int)wxGLCanvas::IsDisplaySupported((const int *)NULL));

    // versions the way BambuStudio and the pyramid sample ask for them
    static const int versions[][2] =
        { { 2, 0 }, { 3, 2 }, { 3, 3 }, { 4, 1 }, { 4, 6 }, { 9, 0 } };
    for ( size_t n = 0; n < WXSIZEOF(versions); n++ )
    {
        wxGLContextAttrs attrs;
        attrs.PlatformDefaults().CoreProfile()
             .OGLVersion(versions[n][0], versions[n][1]).EndList();
        wxGLContext context(canvas, NULL, &attrs);
        printf("GL core profile %d.%d context: %s\n",
               versions[n][0], versions[n][1], context.IsOK() ? "ok" : "no");
    }
    SetCurrent(*canvas);

    // the state, set once
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5, 0.5, 1, 4);
    glMatrixMode(GL_MODELVIEW);

    // a texture
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    unsigned char pixels[8 * 8 * 4];
    for ( int n = 0; n < 64; n++ )
    {
        const bool on = ((n / 8) + (n % 8)) % 2 == 0;
        pixels[4 * n] = on ? 255 : 40;
        pixels[4 * n + 1] = on ? 255 : 90;
        pixels[4 * n + 2] = on ? 255 : 200;
        pixels[4 * n + 3] = 255;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if ( m_modern )
    {
        // objects which are never shared between contexts
        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);
        glGenBuffers(1, &m_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        static const GLfloat triangle[] =
            { -0.95f, -0.95f, -0.55f, -0.95f, -0.95f, -0.55f };
        glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle,
                     GL_STATIC_DRAW);

        const GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &kVertexShader, NULL);
        glCompileShader(vs);
        const GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &kFragmentShader, NULL);
        glCompileShader(fs);
        m_program = glCreateProgram();
        glAttachShader(m_program, vs);
        glAttachShader(m_program, fs);
        glBindAttribLocation(m_program, 0, "pos");
        glLinkProgram(m_program);
        GLint linked = 0;
        glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
        Check(linked == GL_TRUE, "GLSL 1.30 program links");

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glGenFramebuffers(1, &m_fbo);
        glGenTextures(1, &m_fboTexture);
        glBindTexture(GL_TEXTURE_2D, m_fboTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_fboTexture, 0);
        Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)
                == GL_FRAMEBUFFER_COMPLETE, "frame buffer object complete");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    Check(glGetError() == GL_NO_ERROR, "no OpenGL error during the setup");
    fflush(stdout);
}

void TestContext::Draw(const wxSize& size, float angle, int index)
{
    glViewport(0, 0, size.x, size.y);

    if ( m_modern )
    {
        // something rendered off screen with a frame buffer object bound
        // for reading too when the frame is swapped later
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, 64, 64);
        glClearColor(0.9f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, size.x, size.y);
    }

    // the canvases have different backgrounds, with alpha below 1
    if ( index == 0 )
        glClearColor(0.10f, 0.15f, 0.30f, 0.25f);
    else
        glClearColor(0.10f, 0.30f, 0.15f, 0.25f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // a diamond in the stencil buffer: the cube is drawn inside only
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(GL_QUADS);
        glVertex2f(0, -0.97f);
        glVertex2f(0.97f, 0);
        glVertex2f(0, 0.97f);
        glVertex2f(-0.97f, 0);
    glEnd();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    glLoadIdentity();
    glTranslatef(0, 0, -2.2f);
    glRotatef(angle, 0.3f, 1, 0.2f);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    static const GLfloat faces[6][4][3] =
    {
        { { -.5f, -.5f, .5f }, { .5f, -.5f, .5f }, { .5f, .5f, .5f }, { -.5f, .5f, .5f } },
        { { .5f, -.5f, -.5f }, { -.5f, -.5f, -.5f }, { -.5f, .5f, -.5f }, { .5f, .5f, -.5f } },
        { { .5f, -.5f, .5f }, { .5f, -.5f, -.5f }, { .5f, .5f, -.5f }, { .5f, .5f, .5f } },
        { { -.5f, -.5f, -.5f }, { -.5f, -.5f, .5f }, { -.5f, .5f, .5f }, { -.5f, .5f, -.5f } },
        { { -.5f, .5f, .5f }, { .5f, .5f, .5f }, { .5f, .5f, -.5f }, { -.5f, .5f, -.5f } },
        { { -.5f, -.5f, -.5f }, { .5f, -.5f, -.5f }, { .5f, -.5f, .5f }, { -.5f, -.5f, .5f } },
    };
    static const GLfloat colours[6][3] =
    {
        { 1, .4f, .4f }, { .4f, 1, .4f }, { .4f, .4f, 1 },
        { 1, 1, .4f }, { 1, .4f, 1 }, { .4f, 1, 1 }
    };
    static const GLfloat coords[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

    glBegin(GL_QUADS);
    for ( int f = 0; f < 6; f++ )
    {
        glColor3fv(colours[f]);
        for ( int n = 0; n < 4; n++ )
        {
            glTexCoord2fv(coords[n]);
            glVertex3fv(faces[f][n]);
        }
    }
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_STENCIL_TEST);

    if ( m_modern )
    {
        // the yellow triangle in the bottom left corner
        glDisable(GL_DEPTH_TEST);
        glUseProgram(m_program);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
        glEnable(GL_DEPTH_TEST);
    }
}

// ----------------------------------------------------------------------------
// the canvas
// ----------------------------------------------------------------------------

class TestCanvas : public wxGLCanvas
{
public:
    TestCanvas(wxWindow *parent, const int *attribs, int index);

    void Render();

    int m_index;
    float m_angle;
    bool m_dirty;
    int m_paints, m_frames;

private:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouse(wxMouseEvent& event);
    void OnKey(wxKeyEvent& event);
    void OnChar(wxKeyEvent& event);
    void OnFocus(wxFocusEvent& event);

    wxPoint m_last;
    bool m_checked;

    wxDECLARE_EVENT_TABLE();
};

class TestApp : public wxApp
{
public:
    TestApp() : m_context(NULL), m_step(0), m_firstIdle(true), m_started(false) { }

    virtual bool OnInit() wxOVERRIDE;
    virtual int OnExit() wxOVERRIDE;

    TestContext& GetContext(wxGLCanvas *canvas);

    void OnIdle(wxIdleEvent& event);
    void OnTimer(wxTimerEvent& event);
    void OnSpin(wxTimerEvent& event);

    void Bench(const char *what);
    void CheckShared();

    TestContext *m_context;
    std::vector<TestCanvas *> m_canvases;
    std::vector<wxFrame *> m_frames;
    wxTimer m_timer, m_spinTimer;
    size_t m_step;
    bool m_firstIdle, m_started;
};

wxDECLARE_APP(TestApp);
wxIMPLEMENT_APP(TestApp);

wxBEGIN_EVENT_TABLE(TestCanvas, wxGLCanvas)
    EVT_PAINT(TestCanvas::OnPaint)
    EVT_SIZE(TestCanvas::OnSize)
    EVT_MOUSE_EVENTS(TestCanvas::OnMouse)
    EVT_KEY_DOWN(TestCanvas::OnKey)
    EVT_KEY_UP(TestCanvas::OnKey)
    EVT_CHAR(TestCanvas::OnChar)
    EVT_SET_FOCUS(TestCanvas::OnFocus)
    EVT_KILL_FOCUS(TestCanvas::OnFocus)
wxEND_EVENT_TABLE()

TestCanvas::TestCanvas(wxWindow *parent, const int *attribs, int index)
    : wxGLCanvas(parent, wxID_ANY, attribs, wxDefaultPosition, wxDefaultSize,
                 wxWANTS_CHARS),
      m_index(index), m_angle(30 + 40 * index), m_dirty(true),
      m_paints(0), m_frames(0), m_checked(false)
{
}

void TestCanvas::Render()
{
    if ( !IsShownOnScreen() )
        return;

    TestContext& context = wxGetApp().GetContext(this);
    if ( !context.IsOK() )
        return;

    context.SetCurrent(*this);
    context.Draw(GetClientSize(), m_angle, m_index);

    if ( !m_checked )
    {
        m_checked = true;

        // the alpha of the background, in the middle of the left edge
        // (Draw() leaves its frame buffer object bound for reading)
        unsigned char pixel[4] = { 0, 0, 0, 0 };
        GLint readFramebuffer = 0;
        if ( context.m_modern )
        {
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        }
        glReadPixels(0, GetClientSize().y / 2, 1, 1, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixel);
        if ( context.m_modern )
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        printf("GL canvas %d: background pixel read back %d %d %d %d\n",
               m_index, pixel[0], pixel[1], pixel[2], pixel[3]);
        if ( gs_options.bambu )
            Check(pixel[3] > 50 && pixel[3] < 80, "alpha is read back");

        Check(glGetError() == GL_NO_ERROR, "no OpenGL error in the frame");
    }

    SwapBuffers();

    m_dirty = false;
    m_frames++;
}

void TestCanvas::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    m_paints++;

    if ( gs_options.nodc )
    {
        // as the canvases of BambuStudio: rendered by the next idle event
        m_dirty = true;
        return;
    }

    wxPaintDC dc(this);
    Render();
}

void TestCanvas::OnSize(wxSizeEvent& event)
{
    m_dirty = true;
    event.Skip();
}

void TestCanvas::OnMouse(wxMouseEvent& event)
{
    if ( event.Entering() )
        SetFocus();

    if ( event.Dragging() )
    {
        m_angle += event.GetX() - m_last.x;
        m_dirty = true;
        if ( !gs_options.nodc )
            Refresh(false);
    }
    m_last = event.GetPosition();

    if ( event.GetWheelRotation() )
    {
        m_angle += event.GetWheelRotation() > 0 ? 15 : -15;
        Refresh(false);
        m_dirty = true;
    }

    if ( gs_options.events && (!event.Moving() || event.Dragging()) )
    {
        const char *name = "?";
        const wxEventType type = event.GetEventType();
        if ( type == wxEVT_LEFT_DOWN ) name = "left down";
        else if ( type == wxEVT_LEFT_UP ) name = "left up";
        else if ( type == wxEVT_LEFT_DCLICK ) name = "left dclick";
        else if ( type == wxEVT_RIGHT_DOWN ) name = "right down";
        else if ( type == wxEVT_RIGHT_UP ) name = "right up";
        else if ( type == wxEVT_MIDDLE_DOWN ) name = "middle down";
        else if ( type == wxEVT_MIDDLE_UP ) name = "middle up";
        else if ( type == wxEVT_MOTION ) name = "drag";
        else if ( type == wxEVT_ENTER_WINDOW ) name = "enter";
        else if ( type == wxEVT_LEAVE_WINDOW ) name = "leave";
        else if ( type == wxEVT_MOUSEWHEEL ) name = "wheel";

        printf("EV canvas %d mouse %s at %d,%d%s%s%s%s wheel %d\n", m_index,
               name, event.GetX(), event.GetY(),
               event.LeftIsDown() ? " left" : "",
               event.ControlDown() ? " ctrl" : "",
               event.ShiftDown() ? " shift" : "",
               event.AltDown() ? " alt" : "",
               event.GetWheelRotation());
        fflush(stdout);
    }

    event.Skip();
}

void TestCanvas::OnKey(wxKeyEvent& event)
{
    if ( event.GetEventType() == wxEVT_KEY_DOWN )
    {
        switch ( event.GetKeyCode() )
        {
            case WXK_LEFT:
                m_angle -= 10;
                break;

            case WXK_RIGHT:
                m_angle += 10;
                break;
        }

        m_dirty = true;
        if ( !gs_options.nodc )
            Refresh(false);
    }

    if ( gs_options.events )
    {
        printf("EV canvas %d key %s code %d unicode %d%s%s%s\n", m_index,
               event.GetEventType() == wxEVT_KEY_DOWN ? "down" : "up",
               event.GetKeyCode(), (int)event.GetUnicodeKey(),
               event.ControlDown() ? " ctrl" : "",
               event.ShiftDown() ? " shift" : "",
               event.AltDown() ? " alt" : "");
        fflush(stdout);
    }

    event.Skip();
}

void TestCanvas::OnChar(wxKeyEvent& event)
{
    if ( gs_options.events )
    {
        printf("EV canvas %d char code %d unicode U+%04X\n", m_index,
               event.GetKeyCode(), (unsigned)event.GetUnicodeKey());
        fflush(stdout);
    }

    event.Skip();
}

void TestCanvas::OnFocus(wxFocusEvent& event)
{
    if ( gs_options.events )
    {
        printf("EV canvas %d focus %s\n", m_index,
               event.GetEventType() == wxEVT_SET_FOCUS ? "set" : "killed");
        fflush(stdout);
    }

    event.Skip();
}

// ----------------------------------------------------------------------------
// the application
// ----------------------------------------------------------------------------

bool TestApp::OnInit()
{
    for ( int n = 1; n < argc; n++ )
    {
        const wxString arg = argv[n];
        long value;
        if ( arg == "--bambu" ) gs_options.bambu = true;
        else if ( arg == "--two" ) gs_options.two = true;
        else if ( arg == "--nodc" ) gs_options.nodc = true;
        else if ( arg == "--events" ) gs_options.events = true;
        else if ( arg == "--spin" ) gs_options.spin = true;
        else if ( arg == "--quit" ) gs_options.quit = true;
        else if ( arg.StartsWith("--size=") )
            ParseSize(arg.AfterFirst('='), &gs_options.size);
        else if ( arg.StartsWith("--bench=") && arg.AfterFirst('=').ToLong(&value) )
            gs_options.bench = value;
        else if ( arg.StartsWith("--sizes=") )
        {
            wxStringTokenizer tokens(arg.AfterFirst('='), ",");
            while ( tokens.HasMoreTokens() )
            {
                wxSize size;
                if ( ParseSize(tokens.GetNextToken(), &size) )
                    gs_options.sizes.push_back(size);
            }
        }
        else
        {
            printf("unknown option %s\n", (const char *)arg.utf8_str());
            return false;
        }
    }

    static const int bambu[] =
    {
        WX_GL_RGBA, WX_GL_DOUBLEBUFFER,
        WX_GL_MIN_RED, 8, WX_GL_MIN_GREEN, 8, WX_GL_MIN_BLUE, 8,
        WX_GL_MIN_ALPHA, 8, WX_GL_DEPTH_SIZE, 24, WX_GL_STENCIL_SIZE, 8,
        WX_GL_SAMPLE_BUFFERS, 1, WX_GL_SAMPLES, 4,
        WX_GL_CORE_PROFILE, 0
    };
    static const int plain[] =
    {
        WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 16,
        WX_GL_STENCIL_SIZE, 8, 0
    };

    const int count = gs_options.two ? 2 : 1;
    for ( int n = 0; n < count; n++ )
    {
        wxFrame * const frame = new wxFrame(NULL, wxID_ANY,
            wxString::Format("gltest %d", n + 1),
            wxPoint(60 + n * (gs_options.size.x + 40), 80));
        TestCanvas * const canvas
            = new TestCanvas(frame, gs_options.bambu ? bambu : plain, n);
        Check(canvas->GetHandle() != NULL, "canvas created");

        // the second canvas is smaller: the context changes its size with
        // the canvas
        frame->SetClientSize(n == 0 ? gs_options.size
                                    : gs_options.size * 3 / 4);
        frame->Show();

        m_frames.push_back(frame);
        m_canvases.push_back(canvas);
    }

    Bind(wxEVT_IDLE, &TestApp::OnIdle, this);
    m_timer.Bind(wxEVT_TIMER, &TestApp::OnTimer, this);
    m_spinTimer.Bind(wxEVT_TIMER, &TestApp::OnSpin, this);

    // the script starts when the windows had the time to appear
    m_timer.StartOnce(1500);
    if ( gs_options.spin )
        m_spinTimer.Start(16);

    return true;
}

int TestApp::OnExit()
{
    delete m_context;
    printf("GL exit\n");
    fflush(stdout);

    return wxApp::OnExit();
}

TestContext& TestApp::GetContext(wxGLCanvas *canvas)
{
    if ( !m_context )
        m_context = new TestContext(canvas);

    return *m_context;
}

void TestApp::OnIdle(wxIdleEvent& event)
{
    // BambuStudio renders its dirty canvases here
    for ( size_t n = 0; n < m_canvases.size(); n++ )
    {
        if ( m_canvases[n]->m_dirty && (gs_options.nodc || m_firstIdle) )
            m_canvases[n]->Render();
    }

    m_firstIdle = false;

    event.Skip();
}

void TestApp::OnSpin(wxTimerEvent& WXUNUSED(event))
{
    for ( size_t n = 0; n < m_canvases.size(); n++ )
    {
        m_canvases[n]->m_angle += 2;
        m_canvases[n]->m_dirty = true;
        if ( !gs_options.nodc )
            m_canvases[n]->Refresh(false);
    }
}

void TestApp::CheckShared()
{
    if ( m_canvases.size() < 2 || !m_context || !m_context->IsOK() )
        return;

    // with the second canvas current: everything made for the first one
    m_context->SetCurrent(*m_canvases[1]);

    Check(glIsEnabled(GL_DEPTH_TEST) == GL_TRUE,
          "second canvas: the state of the context (depth test)");
    GLfloat projection[16];
    glGetFloatv(GL_PROJECTION_MATRIX, projection);
    Check(projection[0] == 2.0f, "second canvas: the projection matrix");
    Check(glIsTexture(m_context->m_texture) == GL_TRUE,
          "second canvas: the texture");

    if ( m_context->m_modern )
    {
        Check(glIsVertexArray(m_context->m_vao) == GL_TRUE,
              "second canvas: the vertex array object");
        Check(glIsFramebuffer(m_context->m_fbo) == GL_TRUE,
              "second canvas: the frame buffer object");
        Check(glIsProgram(m_context->m_program) == GL_TRUE,
              "second canvas: the program");
    }

    // a second context sharing with the first, and one which does not
    {
        wxGLContext shared(m_canvases[1], m_context);
        shared.SetCurrent(*m_canvases[1]);
        Check(shared.IsOK() && glIsTexture(m_context->m_texture) == GL_TRUE,
              "a context sharing with the first one has its texture");
        Check(glIsEnabled(GL_DEPTH_TEST) == GL_FALSE,
              "and its own state");

        wxGLContext separate(m_canvases[1]);
        separate.SetCurrent(*m_canvases[1]);
        Check(separate.IsOK() && glIsTexture(m_context->m_texture) == GL_FALSE,
              "a context not sharing has not");
    }

    // the contexts above rendered nothing: the canvas keeps its frame
    m_context->SetCurrent(*m_canvases[1]);
    m_canvases[1]->Render();
    m_canvases[0]->Render();
}

void TestApp::Bench(const char *what)
{
    if ( !m_context || !m_context->IsOK() || gs_options.bench <= 0 )
        return;

    const int frames = gs_options.bench;

    wxStopWatch sw;
    for ( int n = 0; n < frames; n++ )
    {
        for ( size_t c = 0; c < m_canvases.size(); c++ )
        {
            m_canvases[c]->m_angle += 1.5f;
            m_canvases[c]->Render();
        }
    }
    glFinish();
    const long ms = wxMax(sw.Time(), 1);

    const wxSize size = m_canvases[0]->GetClientSize();
    printf("BENCH %s %dx%d%s: %d frames in %ld ms, %.1f fps (%.2f ms/frame)\n",
           what, size.x, size.y,
           m_canvases.size() > 1 ? " and the second canvas, alternating" : "",
           frames, ms, frames * 1000.0 / ms, (double)ms / frames);

    // how much of a frame is getting it on the screen
    sw.Start();
    for ( int n = 0; n < frames; n++ )
        m_canvases[0]->SwapBuffers();
    const long msSwap = sw.Time();
    printf("BENCH SwapBuffers alone %dx%d: %.2f ms\n", size.x, size.y,
           (double)msSwap / frames);
    fflush(stdout);
}

void TestApp::OnTimer(wxTimerEvent& WXUNUSED(event))
{
    if ( !m_started )
    {
        m_started = true;

        for ( size_t n = 0; n < m_canvases.size(); n++ )
        {
            printf("GL canvas %d: %d paint events, %d frames before the "
                   "script\n", (int)n, m_canvases[n]->m_paints,
                   m_canvases[n]->m_frames);
        }

        CheckShared();
        Bench("initial size");
    }

    if ( m_step < gs_options.sizes.size() )
    {
        // every other call resizes, the next one measures
        static bool s_resized = false;
        if ( !s_resized )
        {
            m_frames[0]->SetClientSize(gs_options.sizes[m_step]);
            s_resized = true;
            m_timer.StartOnce(400);
            return;
        }

        s_resized = false;
        const wxSize size = m_canvases[0]->GetClientSize();
        Check(size == gs_options.sizes[m_step], "canvas resized");
        m_canvases[0]->Render();
        if ( gs_options.bench > 0 )
            Bench("resized");
        else
            printf("GL rendered at %dx%d\n", size.x, size.y);
        fflush(stdout);

        m_step++;
        m_timer.StartOnce(200);
        return;
    }

    for ( size_t n = 0; n < m_canvases.size(); n++ )
    {
        printf("GL canvas %d: %d paint events, %d frames after the script\n",
               (int)n, m_canvases[n]->m_paints, m_canvases[n]->m_frames);
    }
    printf("GL script done\n");
    fflush(stdout);

    if ( gs_options.quit )
    {
        for ( size_t n = 0; n < m_frames.size(); n++ )
            m_frames[n]->Close();
    }
}
