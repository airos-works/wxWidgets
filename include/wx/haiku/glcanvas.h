/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/glcanvas.h
// Purpose:     wxGLCanvas and wxGLContext on the GL kit (BGLView)
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_GLCANVAS_H_
#define _WX_HAIKU_GLCANVAS_H_

#include <GL/gl.h>

class BBitmap;
struct wxHaikuGLContextData;

// The GL kit ties an OpenGL context and its frame buffer to a BGLView. A
// wxGLContext must be usable with any canvas, so it owns a BGLView of its own
// which is never shown: the context renders into the buffers of this view,
// which have the size of the canvas the context is current for, and
// wxGLCanvas::SwapBuffers() copies the frame to the canvas. See the top of
// src/haiku/glcanvas.cpp.
//
// What follows from this:
//  - The back buffer belongs to the context, not to the canvas: swap the
//    buffers of a canvas before making its context current for another one,
//    what was rendered and not swapped is lost then.
//  - The kit decides what context it creates: a compatibility profile of the
//    highest version the renderer has. A wxGLContextAttrs request for a
//    version is checked against it; a core profile request is satisfied by
//    the compatibility profile of at least this version, which contains it.
//  - There are no multisampled, stereo or single buffered canvases.
class WXDLLIMPEXP_GL wxGLContext : public wxGLContextBase
{
public:
    // The frame buffer of the context has the attributes of win. With other
    // the two contexts share their objects, otherwise they share nothing.
    wxGLContext(wxGLCanvas *win,
                const wxGLContext *other = NULL,
                const wxGLContextAttrs *ctxAttrs = NULL);
    virtual ~wxGLContext();

    virtual bool SetCurrent(const wxGLCanvas& win) const wxOVERRIDE;

    // Nothing is current for the calling (main) thread any more: the same as
    // the wxGLContext::ClearCurrent() of later wx versions.
    static void ClearCurrent();

    // The version of the contexts the GL kit creates (0 if there is no
    // renderer), which does not depend on the canvas.
    static void HaikuGetVersion(int *major, int *minor);

    // implementation
    wxHaikuGLContextData *HaikuGetData() const { return m_data; }

private:
    wxHaikuGLContextData *m_data;

    wxDECLARE_CLASS(wxGLContext);
    wxDECLARE_NO_COPY_CLASS(wxGLContext);
};

class WXDLLIMPEXP_GL wxGLCanvas : public wxGLCanvasBase
{
public:
    wxGLCanvas() { Init(); }

    // There are no palettes in this port (wxUSE_PALETTE is 0): the palette
    // argument the other ports have at the end is missing.

    wxGLCanvas(wxWindow *parent,
               const wxGLAttributes& dispAttrs,
               wxWindowID id = wxID_ANY,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = 0,
               const wxString& name = wxGLCanvasName);

    wxGLCanvas(wxWindow *parent,
               wxWindowID id = wxID_ANY,
               const int *attribList = NULL,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = 0,
               const wxString& name = wxGLCanvasName);

    bool Create(wxWindow *parent,
                const wxGLAttributes& dispAttrs,
                wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxGLCanvasName);

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxGLCanvasName,
                const int *attribList = NULL);

    virtual ~wxGLCanvas();

    // implement wxGLCanvasBase methods
    virtual bool SwapBuffers() wxOVERRIDE;

    // Haiku-specific functions
    // ------------------------

    // the BGL_XXX options the display attributes were translated to
    unsigned long GetHaikuGLOptions() const { return m_glOptions; }

    // implementation-only from now on
    virtual bool HaikuHasNativeContent() const wxOVERRIDE { return true; }
    virtual void HaikuHandleEvent(const wxHaikuEvent& event) wxOVERRIDE;

    // the context which renders for this canvas at the moment
    wxHaikuGLContextData *HaikuGetContextData() const { return m_context; }
    void HaikuSetContextData(wxHaikuGLContextData *context) const
        { m_context = context; }

    // the bitmap a frame of the given size is read into
    BBitmap *HaikuGetFrameBitmap(int width, int height) const;

protected:
    virtual void DoMoveWindow(int x, int y, int width, int height) wxOVERRIDE;

private:
    void Init();

    // draws the frame bitmap, or the part of it, on the canvas
    void HaikuShowFrame(const wxRect *rect = NULL);

    unsigned long m_glOptions;

    mutable wxHaikuGLContextData *m_context;

    // the last frame shown
    mutable BBitmap *m_frame;

    wxDECLARE_CLASS(wxGLCanvas);
    wxDECLARE_NO_COPY_CLASS(wxGLCanvas);
};

#endif // _WX_HAIKU_GLCANVAS_H_
