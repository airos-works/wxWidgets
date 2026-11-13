/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/graphics.cpp
// Purpose:     wxGraphicsRenderer, wxGraphicsContext and friends on BView:
//              paths as BShape, anti-aliased by app_server
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// How it maps:
//
// - Paths are kept as lists of segments (lines and cubic Béziers, arcs are
//   converted) and sent as BShapes. app_server rasterizes shapes with AGG
//   without any rounding: coordinates are pixel edges, as in the other
//   wxGraphicsContext implementations, and ShouldOffset() moves strokes of
//   odd width by half a pixel when the offset is enabled (wxGCDC does it).
// - The transformation is the view's BAffineTransform; pens (width, caps,
//   joins) and fonts are scaled by it. Dashes are cut on the client side.
// - Colours with alpha are blended (B_OP_ALPHA). On bitmaps with an alpha
//   channel B_ALPHA_COMPOSITE maintains the alpha channel of the target.
// - Gradients are BGradients, layers are BView layers.
// - Clipping is done with regions in device space: a clip rectangle under
//   a rotation is replaced by its bounding box.
//
// A context draws on the view of the wxDC it was created from: the paint
// buffer of the window being painted, the window itself or the view of the
// bitmap of a wxMemoryDC. The view may be shared with wxDCs: who drew last
// is recorded in the view (wxHaikuDrawView::m_stateOwner) and the context
// sets its transformation and clipping again when it is not the owner.

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_GRAPHICS_CONTEXT

#include "wx/graphics.h"

#ifndef WX_PRECOMP
    #include "wx/bitmap.h"
    #include "wx/dcclient.h"
    #include "wx/dcmemory.h"
    #include "wx/dcprint.h"
    #include "wx/icon.h"
    #include "wx/image.h"
    #include "wx/log.h"
    #include "wx/window.h"
#endif

#include "wx/private/graphics.h"
#include "wx/vector.h"

#include "wx/haiku/dc.h"
#include "wx/haiku/private.h"

#include <AffineTransform.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <Shape.h>

#include <float.h>
#include <math.h>

// implemented in bitmap.cpp
wxBitmap wxHaikuBitmapFromNative(const BBitmap *bitmap);

namespace
{

// ----------------------------------------------------------------------------
// a 2D affine matrix: x' = a*x + c*y + tx, y' = b*x + d*y + ty
// ----------------------------------------------------------------------------

struct Matrix
{
    Matrix() : a(1), b(0), c(0), d(1), tx(0), ty(0) { }
    Matrix(double a_, double b_, double c_, double d_, double tx_, double ty_)
        : a(a_), b(b_), c(c_), d(d_), tx(tx_), ty(ty_) { }

    bool IsIdentity() const
    {
        return a == 1 && b == 0 && c == 0 && d == 1 && tx == 0 && ty == 0;
    }

    bool operator==(const Matrix& m) const
    {
        return a == m.a && b == m.b && c == m.c && d == m.d
                && tx == m.tx && ty == m.ty;
    }

    // true if rectangles stay axis-aligned rectangles
    bool IsRectilinear() const
    {
        return (b == 0 && c == 0) || (a == 0 && d == 0);
    }

    void Apply(double *x, double *y) const
    {
        const double px = *x, py = *y;
        *x = a * px + c * py + tx;
        *y = b * px + d * py + ty;
    }

    void ApplyDistance(double *dx, double *dy) const
    {
        const double px = *dx, py = *dy;
        *dx = a * px + c * py;
        *dy = b * px + d * py;
    }

    // this = m applied first, then this
    void PreMultiply(const Matrix& m)
    {
        const Matrix r(a * m.a + c * m.b,
                       b * m.a + d * m.b,
                       a * m.c + c * m.d,
                       b * m.c + d * m.d,
                       a * m.tx + c * m.ty + tx,
                       b * m.tx + d * m.ty + ty);
        *this = r;
    }

    bool Invert()
    {
        const double det = a * d - b * c;
        if ( det == 0 )
            return false;

        const Matrix r(d / det, -b / det, -c / det, a / det,
                       (c * ty - d * tx) / det,
                       (b * tx - a * ty) / det);
        *this = r;
        return true;
    }

    // the factor lengths are scaled by, for the tolerance of flattening
    double ScaleFactor() const
    {
        return sqrt(fabs(a * d - b * c));
    }

    double a, b, c, d, tx, ty;
};

inline BPoint PT(double x, double y)
{
    return BPoint((float)x, (float)y);
}

// ----------------------------------------------------------------------------
// wxHaikuMatrixData
// ----------------------------------------------------------------------------

class wxHaikuMatrixData : public wxGraphicsMatrixData
{
public:
    wxHaikuMatrixData(wxGraphicsRenderer *renderer, const Matrix& m = Matrix())
        : wxGraphicsMatrixData(renderer), m_matrix(m)
    {
    }

    virtual wxGraphicsObjectRefData *Clone() const wxOVERRIDE
    {
        return new wxHaikuMatrixData(GetRenderer(), m_matrix);
    }

    virtual void Concat(const wxGraphicsMatrixData *t) wxOVERRIDE
    {
        m_matrix.PreMultiply(static_cast<const wxHaikuMatrixData *>(t)->m_matrix);
    }

    virtual void Set(wxDouble a = 1.0, wxDouble b = 0.0,
                     wxDouble c = 0.0, wxDouble d = 1.0,
                     wxDouble tx = 0.0, wxDouble ty = 0.0) wxOVERRIDE
    {
        m_matrix = Matrix(a, b, c, d, tx, ty);
    }

    virtual void Get(wxDouble *a = NULL, wxDouble *b = NULL,
                     wxDouble *c = NULL, wxDouble *d = NULL,
                     wxDouble *tx = NULL, wxDouble *ty = NULL) const wxOVERRIDE
    {
        if ( a ) *a = m_matrix.a;
        if ( b ) *b = m_matrix.b;
        if ( c ) *c = m_matrix.c;
        if ( d ) *d = m_matrix.d;
        if ( tx ) *tx = m_matrix.tx;
        if ( ty ) *ty = m_matrix.ty;
    }

    virtual void Invert() wxOVERRIDE
    {
        m_matrix.Invert();
    }

    virtual bool IsEqual(const wxGraphicsMatrixData *t) const wxOVERRIDE
    {
        return m_matrix == static_cast<const wxHaikuMatrixData *>(t)->m_matrix;
    }

    virtual bool IsIdentity() const wxOVERRIDE
    {
        return m_matrix.IsIdentity();
    }

    virtual void Translate(wxDouble dx, wxDouble dy) wxOVERRIDE
    {
        m_matrix.PreMultiply(Matrix(1, 0, 0, 1, dx, dy));
    }

    virtual void Scale(wxDouble xScale, wxDouble yScale) wxOVERRIDE
    {
        m_matrix.PreMultiply(Matrix(xScale, 0, 0, yScale, 0, 0));
    }

    virtual void Rotate(wxDouble angle) wxOVERRIDE
    {
        const double co = cos(angle), si = sin(angle);
        m_matrix.PreMultiply(Matrix(co, si, -si, co, 0, 0));
    }

    virtual void TransformPoint(wxDouble *x, wxDouble *y) const wxOVERRIDE
    {
        m_matrix.Apply(x, y);
    }

    virtual void TransformDistance(wxDouble *dx, wxDouble *dy) const wxOVERRIDE
    {
        m_matrix.ApplyDistance(dx, dy);
    }

    virtual void *GetNativeMatrix() const wxOVERRIDE
    {
        return const_cast<Matrix *>(&m_matrix);
    }

    const Matrix& GetMatrix() const { return m_matrix; }

private:
    Matrix m_matrix;
};

// ----------------------------------------------------------------------------
// wxHaikuPathData
// ----------------------------------------------------------------------------

struct PathSegment
{
    enum Type { Move, Line, Curve, Close };

    Type type;
    // Move, Line: the point is (x[0], y[0]); Curve: two control points and
    // the end point
    double x[3], y[3];
};

typedef std::vector<PathSegment> PathSegments;

// calls line(x, y) for the points of the flattened cubic after its start
template <typename F>
void FlattenCurve(double x0, double y0, const PathSegment& s, double scale,
                  F line)
{
    // the number of segments from the length of the control polygon
    const double len = hypot(s.x[0] - x0, s.y[0] - y0)
                        + hypot(s.x[1] - s.x[0], s.y[1] - s.y[0])
                        + hypot(s.x[2] - s.x[1], s.y[2] - s.y[1]);
    int n = (int)(len * scale / 4) + 2;
    if ( n > 200 )
        n = 200;

    for ( int i = 1; i <= n; i++ )
    {
        const double t = (double)i / n, mt = 1 - t;
        const double b0 = mt * mt * mt, b1 = 3 * mt * mt * t,
                     b2 = 3 * mt * t * t, b3 = t * t * t;
        line(b0 * x0 + b1 * s.x[0] + b2 * s.x[1] + b3 * s.x[2],
             b0 * y0 + b1 * s.y[0] + b2 * s.y[1] + b3 * s.y[2]);
    }
}

class wxHaikuPathData : public wxGraphicsPathData
{
public:
    wxHaikuPathData(wxGraphicsRenderer *renderer)
        : wxGraphicsPathData(renderer),
          m_hasCurrent(false), m_curX(0), m_curY(0),
          m_startX(0), m_startY(0),
          m_shapeValid(false)
    {
    }

    wxHaikuPathData(const wxHaikuPathData& other)
        : wxGraphicsPathData(other.GetRenderer()),
          m_segments(other.m_segments),
          m_hasCurrent(other.m_hasCurrent),
          m_curX(other.m_curX), m_curY(other.m_curY),
          m_startX(other.m_startX), m_startY(other.m_startY),
          m_shapeValid(false)
    {
    }

    virtual wxGraphicsObjectRefData *Clone() const wxOVERRIDE
    {
        return new wxHaikuPathData(*this);
    }

    virtual void MoveToPoint(wxDouble x, wxDouble y) wxOVERRIDE
    {
        PathSegment s;
        s.type = PathSegment::Move;
        s.x[0] = x;
        s.y[0] = y;
        Add(s);

        m_startX = x;
        m_startY = y;
        SetCurrent(x, y);
    }

    virtual void AddLineToPoint(wxDouble x, wxDouble y) wxOVERRIDE
    {
        if ( !m_hasCurrent )
        {
            MoveToPoint(x, y);
            return;
        }

        PathSegment s;
        s.type = PathSegment::Line;
        s.x[0] = x;
        s.y[0] = y;
        Add(s);

        SetCurrent(x, y);
    }

    virtual void AddCurveToPoint(wxDouble cx1, wxDouble cy1,
                                 wxDouble cx2, wxDouble cy2,
                                 wxDouble x, wxDouble y) wxOVERRIDE
    {
        if ( !m_hasCurrent )
            MoveToPoint(cx1, cy1);

        PathSegment s;
        s.type = PathSegment::Curve;
        s.x[0] = cx1;
        s.y[0] = cy1;
        s.x[1] = cx2;
        s.y[1] = cy2;
        s.x[2] = x;
        s.y[2] = y;
        Add(s);

        SetCurrent(x, y);
    }

    virtual void AddPath(const wxGraphicsPathData *path) wxOVERRIDE
    {
        const wxHaikuPathData * const
            other = static_cast<const wxHaikuPathData *>(path);

        m_segments.insert(m_segments.end(),
                          other->m_segments.begin(), other->m_segments.end());
        if ( other->m_hasCurrent )
        {
            m_startX = other->m_startX;
            m_startY = other->m_startY;
            SetCurrent(other->m_curX, other->m_curY);
        }

        m_shapeValid = false;
    }

    virtual void CloseSubpath() wxOVERRIDE
    {
        if ( m_segments.empty() )
            return;

        PathSegment s;
        s.type = PathSegment::Close;
        s.x[0] = m_startX;
        s.y[0] = m_startY;
        Add(s);

        SetCurrent(m_startX, m_startY);
    }

    virtual void GetCurrentPoint(wxDouble *x, wxDouble *y) const wxOVERRIDE
    {
        if ( x )
            *x = m_curX;
        if ( y )
            *y = m_curY;
    }

    virtual void AddArc(wxDouble x, wxDouble y, wxDouble r,
                        wxDouble startAngle, wxDouble endAngle,
                        bool clockwise) wxOVERRIDE
    {
        // Angles grow clockwise on screen (the y axis points down), as in
        // the other implementations.
        double sweep;
        if ( clockwise )
        {
            sweep = endAngle - startAngle;
            if ( sweep < 0 )
                sweep = fmod(sweep, 2 * M_PI) + 2 * M_PI;
            else if ( sweep > 2 * M_PI )
                sweep = 2 * M_PI;
        }
        else
        {
            sweep = endAngle - startAngle;
            if ( sweep > 0 )
                sweep = fmod(sweep, 2 * M_PI) - 2 * M_PI;
            else if ( sweep < -2 * M_PI )
                sweep = -2 * M_PI;
        }

        const double sx = x + r * cos(startAngle),
                     sy = y + r * sin(startAngle);

        // a line from the current point to the start of the arc
        if ( m_hasCurrent )
            AddLineToPoint(sx, sy);
        else
            MoveToPoint(sx, sy);

        if ( sweep == 0 || r == 0 )
            return;

        // cubic Béziers for at most a quarter of the circle each
        const int n = (int)ceil(fabs(sweep) / (M_PI / 2) - 1e-9);
        const double step = sweep / n;
        const double k = 4.0 / 3.0 * tan(step / 4);

        double a0 = startAngle;
        for ( int i = 0; i < n; i++ )
        {
            const double a1 = a0 + step;
            const double c0 = cos(a0), s0 = sin(a0),
                         c1 = cos(a1), s1 = sin(a1);

            AddCurveToPoint(x + r * (c0 - k * s0), y + r * (s0 + k * c0),
                            x + r * (c1 + k * s1), y + r * (s1 - k * c1),
                            x + r * c1, y + r * s1);
            a0 = a1;
        }
    }

    virtual void *GetNativePath() const wxOVERRIDE
    {
        return GetShape();
    }

    virtual void UnGetNativePath(void *WXUNUSED(p)) const wxOVERRIDE
    {
    }

    virtual void Transform(const wxGraphicsMatrixData *matrix) wxOVERRIDE
    {
        const Matrix& m = static_cast<const wxHaikuMatrixData *>(matrix)->GetMatrix();

        for ( size_t n = 0; n < m_segments.size(); n++ )
        {
            PathSegment& s = m_segments[n];
            const int count = s.type == PathSegment::Curve ? 3 : 1;
            for ( int i = 0; i < count; i++ )
                m.Apply(&s.x[i], &s.y[i]);
        }

        m.Apply(&m_curX, &m_curY);
        m.Apply(&m_startX, &m_startY);

        m_shapeValid = false;
    }

    virtual void GetBox(wxDouble *x, wxDouble *y,
                        wxDouble *w, wxDouble *h) const wxOVERRIDE
    {
        double minX = DBL_MAX, minY = DBL_MAX, maxX = -DBL_MAX, maxY = -DBL_MAX;

        struct Box
        {
            Box(double& x0, double& y0, double& x1, double& y1)
                : minX(x0), minY(y0), maxX(x1), maxY(y1) { }

            void operator()(double px, double py)
            {
                if ( px < minX ) minX = px;
                if ( px > maxX ) maxX = px;
                if ( py < minY ) minY = py;
                if ( py > maxY ) maxY = py;
            }

            double &minX, &minY, &maxX, &maxY;
        } box(minX, minY, maxX, maxY);

        double cx = 0, cy = 0;
        for ( size_t n = 0; n < m_segments.size(); n++ )
        {
            const PathSegment& s = m_segments[n];
            switch ( s.type )
            {
                case PathSegment::Move:
                case PathSegment::Line:
                case PathSegment::Close:
                    box(s.x[0], s.y[0]);
                    cx = s.x[0];
                    cy = s.y[0];
                    break;

                case PathSegment::Curve:
                    // the extrema of the cubic in each coordinate
                    box(s.x[2], s.y[2]);
                    for ( int axis = 0; axis < 2; axis++ )
                    {
                        const double p0 = axis ? cy : cx,
                                     p1 = axis ? s.y[0] : s.x[0],
                                     p2 = axis ? s.y[1] : s.x[1],
                                     p3 = axis ? s.y[2] : s.x[2];

                        // derivative: at^2 + bt + c
                        const double a = -p0 + 3 * p1 - 3 * p2 + p3,
                                     b = 2 * (p0 - 2 * p1 + p2),
                                     c = p1 - p0;

                        double roots[2];
                        int count = 0;
                        if ( fabs(a) < 1e-12 )
                        {
                            if ( fabs(b) > 1e-12 )
                                roots[count++] = -c / b;
                        }
                        else
                        {
                            const double disc = b * b - 4 * a * c;
                            if ( disc >= 0 )
                            {
                                const double sq = sqrt(disc);
                                roots[count++] = (-b + sq) / (2 * a);
                                roots[count++] = (-b - sq) / (2 * a);
                            }
                        }

                        for ( int i = 0; i < count; i++ )
                        {
                            const double t = roots[i];
                            if ( t <= 0 || t >= 1 )
                                continue;

                            const double mt = 1 - t;
                            const double v = mt * mt * mt * p0
                                                + 3 * mt * mt * t * p1
                                                + 3 * mt * t * t * p2
                                                + t * t * t * p3;
                            if ( axis )
                                box(cx, v), box(s.x[2], v);
                            else
                                box(v, cy), box(v, s.y[2]);
                        }
                    }

                    cx = s.x[2];
                    cy = s.y[2];
                    break;
            }
        }

        if ( minX > maxX )
            minX = minY = maxX = maxY = 0;

        if ( x ) *x = minX;
        if ( y ) *y = minY;
        if ( w ) *w = maxX - minX;
        if ( h ) *h = maxY - minY;
    }

    virtual bool Contains(wxDouble x, wxDouble y,
                          wxPolygonFillMode fillStyle) const wxOVERRIDE
    {
        // crossings of the ray going right from the point
        int winding = 0, crossings = 0;

        struct Edge
        {
            Edge(double px_, double py_, int& winding_, int& crossings_)
                : px(px_), py(py_), winding(winding_), crossings(crossings_) { }

            void operator()(double x0, double y0, double x1, double y1)
            {
                if ( (y0 <= py && y1 > py) || (y1 <= py && y0 > py) )
                {
                    const double ix = x0 + (py - y0) * (x1 - x0) / (y1 - y0);
                    if ( ix > px )
                    {
                        crossings++;
                        winding += y1 > y0 ? 1 : -1;
                    }
                }
            }

            double px, py;
            int& winding;
            int& crossings;
        } edge(x, y, winding, crossings);

        ForEachEdge(edge, 1.0, true);

        return fillStyle == wxWINDING_RULE ? winding != 0 : (crossings & 1) != 0;
    }

    // implementation

    const PathSegments& GetSegments() const { return m_segments; }

    bool IsEmpty() const { return m_segments.empty(); }

    // the path as a shape (owned by the path)
    BShape *GetShape() const
    {
        if ( !m_shapeValid )
        {
            m_shape.Clear();
            BuildShape(m_shape, m_segments);
            m_shapeValid = true;
        }

        return &m_shape;
    }

    static void BuildShape(BShape& shape, const PathSegments& segments)
    {
        for ( size_t n = 0; n < segments.size(); n++ )
        {
            const PathSegment& s = segments[n];
            switch ( s.type )
            {
                case PathSegment::Move:
                    shape.MoveTo(PT(s.x[0], s.y[0]));
                    break;

                case PathSegment::Line:
                    shape.LineTo(PT(s.x[0], s.y[0]));
                    break;

                case PathSegment::Curve:
                    {
                        BPoint pts[3] =
                        {
                            PT(s.x[0], s.y[0]), PT(s.x[1], s.y[1]),
                            PT(s.x[2], s.y[2])
                        };
                        shape.BezierTo(pts);
                    }
                    break;

                case PathSegment::Close:
                    shape.Close();
                    break;
            }
        }
    }

    // Calls edge(x0, y0, x1, y1) for every straight piece of the flattened
    // path; with closeAll the open subpaths are closed (for filling).
    template <typename F>
    void ForEachEdge(F& edge, double scale, bool closeAll) const
    {
        double cx = 0, cy = 0, sx = 0, sy = 0;
        bool open = false;

        for ( size_t n = 0; n < m_segments.size(); n++ )
        {
            const PathSegment& s = m_segments[n];
            switch ( s.type )
            {
                case PathSegment::Move:
                    if ( open && closeAll && (cx != sx || cy != sy) )
                        edge(cx, cy, sx, sy);
                    cx = sx = s.x[0];
                    cy = sy = s.y[0];
                    open = true;
                    break;

                case PathSegment::Line:
                    edge(cx, cy, s.x[0], s.y[0]);
                    cx = s.x[0];
                    cy = s.y[0];
                    break;

                case PathSegment::Curve:
                    {
                        struct Line
                        {
                            Line(F& e, double& x, double& y)
                                : edge(e), cx(x), cy(y) { }

                            void operator()(double px, double py)
                            {
                                edge(cx, cy, px, py);
                                cx = px;
                                cy = py;
                            }

                            F& edge;
                            double &cx, &cy;
                        } line(edge, cx, cy);

                        const double x0 = cx, y0 = cy;
                        FlattenCurve(x0, y0, s, scale, line);
                    }
                    break;

                case PathSegment::Close:
                    if ( cx != sx || cy != sy )
                        edge(cx, cy, sx, sy);
                    cx = sx;
                    cy = sy;
                    open = false;
                    break;
            }
        }

        if ( open && closeAll && (cx != sx || cy != sy) )
            edge(cx, cy, sx, sy);
    }

    // true if the path is one axis-aligned rectangle; returns it
    bool IsRectangle(double *x, double *y, double *w, double *h) const
    {
        if ( m_segments.size() != 5
                || m_segments[0].type != PathSegment::Move
                || m_segments[1].type != PathSegment::Line
                || m_segments[2].type != PathSegment::Line
                || m_segments[3].type != PathSegment::Line
                || m_segments[4].type != PathSegment::Close )
        {
            return false;
        }

        const PathSegment *s = &m_segments[0];
        const bool hv = s[0].y[0] == s[1].y[0] && s[1].x[0] == s[2].x[0]
                            && s[2].y[0] == s[3].y[0] && s[3].x[0] == s[0].x[0];
        const bool vh = s[0].x[0] == s[1].x[0] && s[1].y[0] == s[2].y[0]
                            && s[2].x[0] == s[3].x[0] && s[3].y[0] == s[0].y[0];
        if ( !hv && !vh )
            return false;

        const double x0 = wxMin(s[0].x[0], s[2].x[0]),
                     x1 = wxMax(s[0].x[0], s[2].x[0]),
                     y0 = wxMin(s[0].y[0], s[2].y[0]),
                     y1 = wxMax(s[0].y[0], s[2].y[0]);
        *x = x0;
        *y = y0;
        *w = x1 - x0;
        *h = y1 - y0;
        return true;
    }

private:
    void Add(const PathSegment& s)
    {
        m_segments.push_back(s);
        m_shapeValid = false;
    }

    void SetCurrent(double x, double y)
    {
        m_curX = x;
        m_curY = y;
        m_hasCurrent = true;
    }

    PathSegments m_segments;
    bool m_hasCurrent;
    double m_curX, m_curY;
    double m_startX, m_startY;

    mutable BShape m_shape;
    mutable bool m_shapeValid;
};

// ----------------------------------------------------------------------------
// pens, brushes, fonts, bitmaps
// ----------------------------------------------------------------------------

// builds the native gradient for wx gradient parameters
struct GradientInfo
{
    GradientInfo() : type(wxGRADIENT_NONE), x1(0), y1(0), x2(0), y2(0),
                     radius(0), hasAlpha(false) { }

    void Set(wxGradientType type_, double x1_, double y1_,
             double x2_, double y2_, double radius_,
             const wxGraphicsGradientStops& stops_,
             const wxGraphicsMatrix& matrix)
    {
        type = type_;
        x1 = x1_;
        y1 = y1_;
        x2 = x2_;
        y2 = y2_;
        radius = radius_;

        if ( !matrix.IsNull() )
        {
            // the gradient matrix maps gradient space to user space
            double a, b, c, d, tx, ty;
            matrix.Get(&a, &b, &c, &d, &tx, &ty);
            const Matrix m(a, b, c, d, tx, ty);
            m.Apply(&x1, &y1);
            m.Apply(&x2, &y2);
            radius *= m.ScaleFactor();
        }

        stops.clear();
        hasAlpha = false;
        for ( size_t n = 0; n < stops_.GetCount(); n++ )
        {
            const wxGraphicsGradientStop stop = stops_.Item(n);
            stops.push_back(std::make_pair((double)stop.GetPosition(),
                                           stop.GetColour()));
            if ( stop.GetColour().Alpha() != 255 )
                hasAlpha = true;
        }
    }

    // the caller deletes the gradient
    BGradient *Create() const
    {
        BGradient *gradient;
        if ( type == wxGRADIENT_LINEAR )
        {
            gradient = new BGradientLinear(PT(x1, y1), PT(x2, y2));
        }
        else
        {
            // from the start (focal) point to the circle around the end
            if ( x1 == x2 && y1 == y2 )
                gradient = new BGradientRadial(PT(x2, y2), (float)radius);
            else
                gradient = new BGradientRadialFocus(PT(x2, y2), (float)radius,
                                                    PT(x1, y1));
        }

        for ( size_t n = 0; n < stops.size(); n++ )
        {
            gradient->AddColor(wxHaikuColour(stops[n].second),
                               (float)(stops[n].first * 255));
        }

        return gradient;
    }

    wxGradientType type;
    double x1, y1, x2, y2, radius;
    std::vector<std::pair<double, wxColour> > stops;
    bool hasAlpha;
};

class wxHaikuPenData : public wxGraphicsObjectRefData
{
public:
    wxHaikuPenData(wxGraphicsRenderer *renderer, const wxGraphicsPenInfo& info)
        : wxGraphicsObjectRefData(renderer)
    {
        m_colour = info.GetColour();
        m_width = info.GetWidth();
        if ( m_width <= 0 )
            m_width = 1;
        m_transparent = info.GetStyle() == wxPENSTYLE_TRANSPARENT
                            || !m_colour.IsOk();

        switch ( info.GetCap() )
        {
            case wxCAP_PROJECTING: m_cap = B_SQUARE_CAP; break;
            case wxCAP_BUTT:       m_cap = B_BUTT_CAP; break;
            default:               m_cap = B_ROUND_CAP;
        }

        switch ( info.GetJoin() )
        {
            case wxJOIN_BEVEL: m_join = B_BEVEL_JOIN; break;
            case wxJOIN_MITER: m_join = B_MITER_JOIN; break;
            default:           m_join = B_ROUND_JOIN;
        }

        // the dash patterns of the other implementations
        const double unit = m_width < 1.0 ? 1.0 : m_width;
        switch ( info.GetStyle() )
        {
            case wxPENSTYLE_DOT:
                m_dashes.push_back(unit);
                m_dashes.push_back(unit + 2.0);
                m_cap = B_BUTT_CAP;
                break;

            case wxPENSTYLE_LONG_DASH:
                m_dashes.push_back(19.0);
                m_dashes.push_back(9.0);
                break;

            case wxPENSTYLE_SHORT_DASH:
                m_dashes.push_back(9.0);
                m_dashes.push_back(6.0);
                break;

            case wxPENSTYLE_DOT_DASH:
                m_dashes.push_back(9.0);
                m_dashes.push_back(6.0);
                m_dashes.push_back(3.0);
                m_dashes.push_back(3.0);
                break;

            case wxPENSTYLE_USER_DASH:
                {
                    wxDash *dashes;
                    const int count = info.GetDashes(&dashes);
                    for ( int n = 0; n < count; n++ )
                    {
                        // the lengths are in units of the pen width; a zero
                        // length dash is a dot
                        double len = dashes[n] * unit;
                        if ( len == 0 )
                            len = 0.1;
                        m_dashes.push_back(len);
                    }
                }
                break;

            default:
                break;
        }

        if ( info.GetGradientType() != wxGRADIENT_NONE )
        {
            m_gradient.Set(info.GetGradientType(),
                           info.GetX1(), info.GetY1(),
                           info.GetX2(), info.GetY2(),
                           info.GetRadius(), info.GetStops(),
                           info.GetMatrix());
        }
    }

    wxColour m_colour;
    double m_width;
    bool m_transparent;
    cap_mode m_cap;
    join_mode m_join;
    std::vector<double> m_dashes;
    GradientInfo m_gradient;
};

class wxHaikuBrushData : public wxGraphicsObjectRefData
{
public:
    wxHaikuBrushData(wxGraphicsRenderer *renderer)
        : wxGraphicsObjectRefData(renderer),
          m_transparent(true)
    {
        m_pattern = B_SOLID_HIGH;
    }

    wxHaikuBrushData(wxGraphicsRenderer *renderer, const wxBrush& brush)
        : wxGraphicsObjectRefData(renderer)
    {
        m_pattern = B_SOLID_HIGH;
        m_colour = brush.GetColour();
        m_transparent = !brush.IsOk()
                            || brush.GetStyle() == wxBRUSHSTYLE_TRANSPARENT;

        static const pattern bdiag = { { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 } };
        static const pattern fdiag = { { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 } };
        static const pattern crossdiag = { { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 } };
        static const pattern horiz = { { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
        static const pattern vert = { { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 } };
        static const pattern cross = { { 0xff, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 } };

        if ( !m_transparent )
        {
            switch ( brush.GetStyle() )
            {
                case wxBRUSHSTYLE_BDIAGONAL_HATCH:  m_pattern = bdiag; break;
                case wxBRUSHSTYLE_FDIAGONAL_HATCH:  m_pattern = fdiag; break;
                case wxBRUSHSTYLE_CROSSDIAG_HATCH:  m_pattern = crossdiag; break;
                case wxBRUSHSTYLE_HORIZONTAL_HATCH: m_pattern = horiz; break;
                case wxBRUSHSTYLE_VERTICAL_HATCH:   m_pattern = vert; break;
                case wxBRUSHSTYLE_CROSS_HATCH:      m_pattern = cross; break;

                case wxBRUSHSTYLE_STIPPLE:
                case wxBRUSHSTYLE_STIPPLE_MASK:
                case wxBRUSHSTYLE_STIPPLE_MASK_OPAQUE:
                    if ( brush.GetStipple() )
                        m_stipple = *brush.GetStipple();
                    break;

                default:
                    break;
            }
        }
    }

    wxColour m_colour;
    bool m_transparent;
    pattern m_pattern;
    wxBitmap m_stipple;
    GradientInfo m_gradient;
};

class wxHaikuFontData : public wxGraphicsObjectRefData
{
public:
    wxHaikuFontData(wxGraphicsRenderer *renderer, const wxFont& font,
                    const wxColour& colour)
        : wxGraphicsObjectRefData(renderer),
          m_font(font), m_colour(colour)
    {
    }

    wxFont m_font;
    wxColour m_colour;
};

class wxHaikuBitmapData : public wxGraphicsBitmapData
{
public:
    wxHaikuBitmapData(wxGraphicsRenderer *renderer, const wxBitmap& bitmap)
        : wxGraphicsBitmapData(renderer),
          m_bitmap(bitmap)
    {
    }

    virtual void *GetNativeBitmap() const wxOVERRIDE
    {
        return m_bitmap.GetHaikuBitmap();
    }

    wxBitmap m_bitmap;
};

// ----------------------------------------------------------------------------
// wxHaikuContext
// ----------------------------------------------------------------------------

class wxHaikuContext : public wxGraphicsContext
{
public:
    // on the view behind a DC of this port
    wxHaikuContext(wxGraphicsRenderer *renderer, const wxDC& dc,
                   wxWindow *window = NULL);
    // on a bitmap created from the image, written back to it at the end
    wxHaikuContext(wxGraphicsRenderer *renderer, wxImage& image);
    // on the view of a window, outside of its painting
    wxHaikuContext(wxGraphicsRenderer *renderer, wxWindow *window);
    // only for measuring
    wxHaikuContext(wxGraphicsRenderer *renderer);

    virtual ~wxHaikuContext();

    virtual void PushState() wxOVERRIDE;
    virtual void PopState() wxOVERRIDE;

    virtual void Clip(const wxRegion& region) wxOVERRIDE;
    virtual void Clip(wxDouble x, wxDouble y, wxDouble w, wxDouble h) wxOVERRIDE;
    virtual void ResetClip() wxOVERRIDE;
    virtual void GetClipBox(wxDouble *x, wxDouble *y,
                            wxDouble *w, wxDouble *h) wxOVERRIDE;

    virtual void *GetNativeContext() wxOVERRIDE { return m_view; }

    virtual bool SetAntialiasMode(wxAntialiasMode antialias) wxOVERRIDE;
    virtual bool SetInterpolationQuality(wxInterpolationQuality interpolation) wxOVERRIDE;
    virtual bool SetCompositionMode(wxCompositionMode op) wxOVERRIDE;

    virtual void BeginLayer(wxDouble opacity) wxOVERRIDE;
    virtual void EndLayer() wxOVERRIDE;

    virtual void Translate(wxDouble dx, wxDouble dy) wxOVERRIDE;
    virtual void Scale(wxDouble xScale, wxDouble yScale) wxOVERRIDE;
    virtual void Rotate(wxDouble angle) wxOVERRIDE;
    virtual void ConcatTransform(const wxGraphicsMatrix& matrix) wxOVERRIDE;
    virtual void SetTransform(const wxGraphicsMatrix& matrix) wxOVERRIDE;
    virtual wxGraphicsMatrix GetTransform() const wxOVERRIDE;

    virtual void StrokePath(const wxGraphicsPath& path) wxOVERRIDE;
    virtual void FillPath(const wxGraphicsPath& path,
                          wxPolygonFillMode fillStyle = wxODDEVEN_RULE) wxOVERRIDE;
    virtual void ClearRectangle(wxDouble x, wxDouble y,
                                wxDouble w, wxDouble h) wxOVERRIDE;

    virtual void GetTextExtent(const wxString& text,
                               wxDouble *width, wxDouble *height,
                               wxDouble *descent,
                               wxDouble *externalLeading) const wxOVERRIDE;
    virtual void GetPartialTextExtents(const wxString& text,
                                       wxArrayDouble& widths) const wxOVERRIDE;

    virtual void DrawBitmap(const wxGraphicsBitmap& bmp,
                            wxDouble x, wxDouble y, wxDouble w, wxDouble h) wxOVERRIDE;
    virtual void DrawBitmap(const wxBitmap& bmp,
                            wxDouble x, wxDouble y, wxDouble w, wxDouble h) wxOVERRIDE;
    virtual void DrawIcon(const wxIcon& icon,
                          wxDouble x, wxDouble y, wxDouble w, wxDouble h) wxOVERRIDE;

    virtual bool ShouldOffset() const wxOVERRIDE;

    virtual void Flush() wxOVERRIDE;

    virtual void GetDPI(wxDouble *dpiX, wxDouble *dpiY) const wxOVERRIDE;

protected:
    virtual void DoDrawText(const wxString& str, wxDouble x, wxDouble y) wxOVERRIDE;

private:
    void Init();
    void InitFromDC(const wxDC& dc);

    // Locks the view and makes its transformation and clipping ours;
    // false if nothing can be drawn. End() must be called if it returned
    // true.
    bool Begin();
    void End();

    // sets drawing mode and colour for the colour (or for bitmaps/gradients
    // if hasAlpha says they need blending)
    void SetupColour(const wxColour& colour);
    void SetupBlending(bool hasAlpha);

    // the clipping region in view coordinates
    void ApplyClip();
    void ApplyTransform();

    // replaces the pixels of the device rectangle (DC coordinates) of the
    // target bitmap by the colour; false if there is no bitmap behind the
    // view
    bool FillPixels(const wxRect& rect, const wxColour& colour);

    // a stroked path cut into dashes
    void BuildDashedShape(BShape& shape, const wxHaikuPathData *path,
                          const std::vector<double>& dashes) const;

    struct State
    {
        Matrix matrix;
        wxRegion clip;      // device (DC) coordinates
        bool hasClip;
    };

    BView *m_view;
    wxHaikuDrawView *m_drawView;

    // position of the DC's device origin in the view
    wxPoint m_offset;

    // clipping of the DC the context was created from, view coordinates
    wxRegion m_baseClip;
    bool m_hasBaseClip;

    // the bitmap drawn on, if any: its cached state must be dropped when it
    // was drawn on
    wxBitmap m_targetBitmap;
    bool m_targetAlpha;

    // set for contexts created from an image
    wxImage *m_image;

    State m_state;
    std::vector<State> m_stateStack;

    bool m_clipDirty;
    bool m_transformDirty;
    int m_lockCount;
    int m_layers;

    wxDECLARE_NO_COPY_CLASS(wxHaikuContext);
};

// the renderer, defined below
wxGraphicsRenderer *GetHaikuRenderer();

void wxHaikuContext::Init()
{
    m_view = NULL;
    m_drawView = NULL;
    m_hasBaseClip = false;
    m_targetAlpha = false;
    m_image = NULL;
    m_state.hasClip = false;
    m_clipDirty = true;
    m_transformDirty = true;
    m_lockCount = 0;
    m_layers = 0;

    m_width = 0;
    m_height = 0;
}

void wxHaikuContext::InitFromDC(const wxDC& dc)
{
    wxHaikuDCImpl * const impl = wxDynamicCast(dc.GetImpl(), wxHaikuDCImpl);
    if ( !impl )
        return;

    m_view = impl->GetHaikuView();
    m_drawView = dynamic_cast<wxHaikuDrawView *>(m_view);
    m_offset = impl->HaikuGetViewOffset();
    m_targetAlpha = impl->HaikuTargetHasAlpha();

    // the clipping the DC has now stays the limit of this context
    m_hasBaseClip = impl->HaikuGetClip(m_baseClip);

    const wxBitmap * const bitmap = impl->HaikuGetSourceBitmap(NULL);
    if ( bitmap )
        m_targetBitmap = *bitmap;

    const wxSize size = dc.GetSize();
    m_width = size.x;
    m_height = size.y;

    // the origin and scale of the DC
    const double scaleX = dc.LogicalToDeviceXRel(1000) / 1000.0,
                 scaleY = dc.LogicalToDeviceYRel(1000) / 1000.0;
    m_state.matrix = Matrix(scaleX ? scaleX : 1, 0, 0, scaleY ? scaleY : 1,
                            dc.LogicalToDeviceX(0), dc.LogicalToDeviceY(0));
}

wxHaikuContext::wxHaikuContext(wxGraphicsRenderer *renderer, const wxDC& dc,
                               wxWindow *window)
    : wxGraphicsContext(renderer, window)
{
    Init();
    InitFromDC(dc);
}

wxHaikuContext::wxHaikuContext(wxGraphicsRenderer *renderer, wxWindow *window)
    : wxGraphicsContext(renderer, window)
{
    Init();

    // as a client DC: the buffer of the paint in progress or the screen
    wxClientDC dc(window);
    InitFromDC(dc);
}

wxHaikuContext::wxHaikuContext(wxGraphicsRenderer *renderer, wxImage& image)
    : wxGraphicsContext(renderer)
{
    Init();

    m_image = &image;

    if ( !image.HasAlpha() )
        image.InitAlpha();

    m_targetBitmap = wxBitmap(image);
    if ( !m_targetBitmap.IsOk() )
        return;

    m_view = m_targetBitmap.HaikuBeginDrawing();
    m_drawView = dynamic_cast<wxHaikuDrawView *>(m_view);
    m_targetAlpha = true;
    m_width = image.GetWidth();
    m_height = image.GetHeight();
}

wxHaikuContext::wxHaikuContext(wxGraphicsRenderer *renderer)
    : wxGraphicsContext(renderer)
{
    Init();
}

wxHaikuContext::~wxHaikuContext()
{
    if ( m_view && m_view->Window() && m_view->LockLooper() )
    {
        // leave the view as the DCs expect it
        while ( m_layers-- > 0 )
            m_view->EndLayer();

        if ( m_drawView && m_drawView->m_stateOwner == this )
        {
            m_view->SetTransform(BAffineTransform());
            m_drawView->m_transformed = false;
            m_drawView->m_stateOwner = NULL;
        }

        m_view->UnlockLooper();
    }

    if ( m_image )
    {
        // back to the image the context was created for
        m_targetBitmap.HaikuEndDrawing();
        *m_image = m_targetBitmap.ConvertToImage();
    }
}

// ----------------------------------------------------------------------------
// locking and state
// ----------------------------------------------------------------------------

bool wxHaikuContext::Begin()
{
    if ( !m_view || !m_view->Window() || !m_view->LockLooper() )
        return false;

    m_lockCount++;

    if ( !m_drawView || m_drawView->m_stateOwner != this )
    {
        if ( m_drawView )
            m_drawView->m_stateOwner = this;
        m_clipDirty = true;
        m_transformDirty = true;
    }

    if ( m_clipDirty )
        ApplyClip();
    if ( m_transformDirty )
        ApplyTransform();

    // app_server offsets shapes by the pen location (as BeOS did), which
    // drawing text moves: keep it at the origin (BView only sends changes)
    m_view->MovePenTo(B_ORIGIN);

    return true;
}

void wxHaikuContext::End()
{
    if ( --m_lockCount == 0 && m_targetBitmap.IsOk() )
        m_targetBitmap.HaikuTouch();

    m_view->UnlockLooper();
}

void wxHaikuContext::ApplyClip()
{
    m_clipDirty = false;

    if ( !m_state.hasClip && !m_hasBaseClip )
    {
        m_view->ConstrainClippingRegion(NULL);
        return;
    }

    wxRegion clip;
    if ( m_state.hasClip )
    {
        clip = m_state.clip;
        if ( !clip.IsEmpty() )
            clip.Offset(m_offset);
        if ( m_hasBaseClip )
            clip.Intersect(m_baseClip);
    }
    else
    {
        clip = m_baseClip;
    }

    BRegion region;
    if ( clip.GetHaikuRegion() )
        region = *clip.GetHaikuRegion();

    // Regions are not transformed by the view's affine transformation, but
    // set it aside anyway: this must not depend on that.
    m_view->ConstrainClippingRegion(&region);
}

void wxHaikuContext::ApplyTransform()
{
    m_transformDirty = false;

    const Matrix& m = m_state.matrix;
    m_view->SetTransform(BAffineTransform(m.a, m.b, m.c, m.d,
                                          m.tx + m_offset.x,
                                          m.ty + m_offset.y));
    if ( m_drawView )
        m_drawView->m_transformed = true;
}

void wxHaikuContext::SetupBlending(bool hasAlpha)
{
    switch ( m_composition )
    {
        case wxCOMPOSITION_SOURCE:
            m_view->SetDrawingMode(B_OP_COPY);
            return;

        case wxCOMPOSITION_ADD:
            m_view->SetDrawingMode(B_OP_ADD);
            return;

        default:
            break;
    }

    if ( hasAlpha || m_targetAlpha )
    {
        m_view->SetDrawingMode(B_OP_ALPHA);
        m_view->SetBlendingMode(B_PIXEL_ALPHA,
                                m_targetAlpha ? B_ALPHA_COMPOSITE
                                              : B_ALPHA_OVERLAY);
    }
    else
    {
        // blends the anti-aliased edges with what is there
        m_view->SetDrawingMode(B_OP_OVER);
    }
}

void wxHaikuContext::SetupColour(const wxColour& colour)
{
    SetupBlending(colour.Alpha() != 255);
    m_view->SetHighColor(wxHaikuColour(colour));
}

void wxHaikuContext::PushState()
{
    m_stateStack.push_back(m_state);
}

void wxHaikuContext::PopState()
{
    wxCHECK_RET( !m_stateStack.empty(), wxS("unbalanced PopState()") );

    m_state = m_stateStack.back();
    m_stateStack.pop_back();

    m_clipDirty = true;
    m_transformDirty = true;
}

void wxHaikuContext::Flush()
{
    if ( m_view && m_view->Window() && m_view->LockLooper() )
    {
        m_view->Flush();
        m_view->UnlockLooper();
    }
}

void wxHaikuContext::GetDPI(wxDouble *dpiX, wxDouble *dpiY) const
{
    const double dpi = 96 * wxHaikuScaleFactor();
    if ( dpiX )
        *dpiX = dpi;
    if ( dpiY )
        *dpiY = dpi;
}

// ----------------------------------------------------------------------------
// clipping
// ----------------------------------------------------------------------------

void wxHaikuContext::Clip(const wxRegion& region)
{
    // the rectangles of the region are in user coordinates
    wxRegion device;
    for ( wxRegionIterator it(region); it; ++it )
    {
        const wxRect r = it.GetRect();
        double xs[4] = { (double)r.x, (double)(r.x + r.width), (double)(r.x + r.width), (double)r.x };
        double ys[4] = { (double)r.y, (double)r.y, (double)(r.y + r.height), (double)(r.y + r.height) };

        double minX = DBL_MAX, minY = DBL_MAX, maxX = -DBL_MAX, maxY = -DBL_MAX;
        for ( int i = 0; i < 4; i++ )
        {
            m_state.matrix.Apply(&xs[i], &ys[i]);
            minX = wxMin(minX, xs[i]);
            maxX = wxMax(maxX, xs[i]);
            minY = wxMin(minY, ys[i]);
            maxY = wxMax(maxY, ys[i]);
        }

        const int x0 = (int)floor(minX + 0.5), y0 = (int)floor(minY + 0.5),
                  x1 = (int)floor(maxX + 0.5), y1 = (int)floor(maxY + 0.5);
        if ( x1 > x0 && y1 > y0 )
            device.Union(wxRect(x0, y0, x1 - x0, y1 - y0));
    }

    if ( m_state.hasClip )
        m_state.clip.Intersect(device);
    else
        m_state.clip = device;

    if ( !m_state.clip.GetHaikuRegion() )
        m_state.clip = wxRegion(0, 0, 0, 0);

    m_state.hasClip = true;
    m_clipDirty = true;
}

void wxHaikuContext::Clip(wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
    // the bounding box in device space of the transformed rectangle
    double xs[4] = { x, x + w, x + w, x };
    double ys[4] = { y, y, y + h, y + h };

    double minX = DBL_MAX, minY = DBL_MAX, maxX = -DBL_MAX, maxY = -DBL_MAX;
    for ( int i = 0; i < 4; i++ )
    {
        m_state.matrix.Apply(&xs[i], &ys[i]);
        minX = wxMin(minX, xs[i]);
        maxX = wxMax(maxX, xs[i]);
        minY = wxMin(minY, ys[i]);
        maxY = wxMax(maxY, ys[i]);
    }

    const int x0 = (int)floor(minX + 0.5), y0 = (int)floor(minY + 0.5),
              x1 = (int)floor(maxX + 0.5), y1 = (int)floor(maxY + 0.5);

    wxRegion device;
    if ( x1 > x0 && y1 > y0 )
        device = wxRegion(x0, y0, x1 - x0, y1 - y0);
    else
        device = wxRegion(0, 0, 0, 0);

    if ( m_state.hasClip )
        m_state.clip.Intersect(device);
    else
        m_state.clip = device;

    if ( !m_state.clip.GetHaikuRegion() )
        m_state.clip = wxRegion(0, 0, 0, 0);

    m_state.hasClip = true;
    m_clipDirty = true;
}

void wxHaikuContext::ResetClip()
{
    m_state.clip.Clear();
    m_state.hasClip = false;
    m_clipDirty = true;
}

void wxHaikuContext::GetClipBox(wxDouble *x, wxDouble *y,
                                wxDouble *w, wxDouble *h)
{
    // device rectangle
    wxRect box;
    if ( m_state.hasClip )
    {
        box = m_state.clip.GetBox();
    }
    else if ( m_hasBaseClip )
    {
        box = m_baseClip.GetBox();
        box.Offset(-m_offset);
    }
    else
    {
        box = wxRect(0, 0, (int)m_width, (int)m_height);
    }

    // to user coordinates
    Matrix inverse = m_state.matrix;
    inverse.Invert();

    double xs[4] = { (double)box.x, (double)(box.x + box.width), (double)(box.x + box.width), (double)box.x };
    double ys[4] = { (double)box.y, (double)box.y, (double)(box.y + box.height), (double)(box.y + box.height) };

    double minX = DBL_MAX, minY = DBL_MAX, maxX = -DBL_MAX, maxY = -DBL_MAX;
    for ( int i = 0; i < 4; i++ )
    {
        inverse.Apply(&xs[i], &ys[i]);
        minX = wxMin(minX, xs[i]);
        maxX = wxMax(maxX, xs[i]);
        minY = wxMin(minY, ys[i]);
        maxY = wxMax(maxY, ys[i]);
    }

    if ( x ) *x = minX;
    if ( y ) *y = minY;
    if ( w ) *w = maxX - minX;
    if ( h ) *h = maxY - minY;
}

// ----------------------------------------------------------------------------
// modes
// ----------------------------------------------------------------------------

bool wxHaikuContext::SetAntialiasMode(wxAntialiasMode antialias)
{
    // app_server always anti-aliases shapes; without anti-aliasing the
    // strokes are at least put on the pixel grid (see ShouldOffset())
    m_antialias = antialias;
    return true;
}

bool wxHaikuContext::SetInterpolationQuality(wxInterpolationQuality interpolation)
{
    m_interpolation = interpolation;
    return true;
}

bool wxHaikuContext::SetCompositionMode(wxCompositionMode op)
{
    switch ( op )
    {
        case wxCOMPOSITION_OVER:
        case wxCOMPOSITION_SOURCE:
        case wxCOMPOSITION_ADD:
        case wxCOMPOSITION_CLEAR:
            m_composition = op;
            return true;

        default:
            // the other Porter-Duff operators have no drawing mode
            return false;
    }
}

void wxHaikuContext::BeginLayer(wxDouble opacity)
{
    if ( !Begin() )
        return;

    m_view->BeginLayer((uint8)wxRound(wxMin(wxMax(opacity, 0.0), 1.0) * 255));
    m_layers++;

    End();
}

void wxHaikuContext::EndLayer()
{
    if ( m_layers <= 0 || !Begin() )
        return;

    m_view->EndLayer();
    m_layers--;

    End();
}

// ----------------------------------------------------------------------------
// transformation
// ----------------------------------------------------------------------------

void wxHaikuContext::Translate(wxDouble dx, wxDouble dy)
{
    m_state.matrix.PreMultiply(Matrix(1, 0, 0, 1, dx, dy));
    m_transformDirty = true;
}

void wxHaikuContext::Scale(wxDouble xScale, wxDouble yScale)
{
    m_state.matrix.PreMultiply(Matrix(xScale, 0, 0, yScale, 0, 0));
    m_transformDirty = true;
}

void wxHaikuContext::Rotate(wxDouble angle)
{
    const double co = cos(angle), si = sin(angle);
    m_state.matrix.PreMultiply(Matrix(co, si, -si, co, 0, 0));
    m_transformDirty = true;
}

void wxHaikuContext::ConcatTransform(const wxGraphicsMatrix& matrix)
{
    double a, b, c, d, tx, ty;
    matrix.Get(&a, &b, &c, &d, &tx, &ty);
    m_state.matrix.PreMultiply(Matrix(a, b, c, d, tx, ty));
    m_transformDirty = true;
}

void wxHaikuContext::SetTransform(const wxGraphicsMatrix& matrix)
{
    double a, b, c, d, tx, ty;
    matrix.Get(&a, &b, &c, &d, &tx, &ty);
    m_state.matrix = Matrix(a, b, c, d, tx, ty);
    m_transformDirty = true;
}

wxGraphicsMatrix wxHaikuContext::GetTransform() const
{
    wxGraphicsMatrix matrix;
    matrix.SetRefData(new wxHaikuMatrixData(GetRenderer(), m_state.matrix));
    return matrix;
}

// ----------------------------------------------------------------------------
// paths
// ----------------------------------------------------------------------------

bool wxHaikuContext::ShouldOffset() const
{
    if ( !m_enableOffset && m_antialias != wxANTIALIAS_NONE )
        return false;

    // only when the pen maps to an odd number of device pixels: lines along
    // pixel edges then cover two half pixels instead of one
    double width = 1;
    if ( !m_pen.IsNull() )
    {
        width = static_cast<wxHaikuPenData *>(m_pen.GetRefData())->m_width
                    * m_state.matrix.ScaleFactor();
    }

    const int w = wxRound(width);
    return fabs(width - w) < 0.01 && (w % 2) == 1;
}

void wxHaikuContext::BuildDashedShape(BShape& shape,
                                      const wxHaikuPathData *path,
                                      const std::vector<double>& dashes) const
{
    struct Dasher
    {
        Dasher(BShape& shape_, const std::vector<double>& dashes_)
            : shape(shape_), dashes(dashes_),
              index(0), remaining(dashes_[0]), on(true), penDown(false)
        {
        }

        void operator()(double x0, double y0, double x1, double y1)
        {
            double len = hypot(x1 - x0, y1 - y0);
            if ( len == 0 )
                return;

            const double ux = (x1 - x0) / len, uy = (y1 - y0) / len;
            double x = x0, y = y0;

            // a new subpath starts where the previous edge did not end
            if ( !penDown || x0 != lastX || y0 != lastY )
            {
                penDown = false;
            }

            while ( len > 0 )
            {
                const double step = wxMin(len, remaining);
                const double nx = x + ux * step, ny = y + uy * step;

                if ( on )
                {
                    if ( !penDown )
                    {
                        shape.MoveTo(PT(x, y));
                        penDown = true;
                    }
                    shape.LineTo(PT(nx, ny));
                }

                x = nx;
                y = ny;
                len -= step;
                remaining -= step;

                if ( remaining <= 1e-9 )
                {
                    index = (index + 1) % dashes.size();
                    remaining = dashes[index];
                    on = !on;
                    penDown = false;
                }
            }

            lastX = x1;
            lastY = y1;
        }

        BShape& shape;
        const std::vector<double>& dashes;
        size_t index;
        double remaining;
        bool on, penDown;
        double lastX, lastY;
    } dasher(shape, dashes);

    dasher.lastX = dasher.lastY = 0;

    path->ForEachEdge(dasher, m_state.matrix.ScaleFactor(), false);
}

void wxHaikuContext::StrokePath(const wxGraphicsPath& path)
{
    if ( m_pen.IsNull() || path.IsNull() )
        return;

    const wxHaikuPenData * const
        pen = static_cast<wxHaikuPenData *>(m_pen.GetRefData());
    if ( pen->m_transparent || pen->m_colour.Alpha() == 0 )
        return;

    const wxHaikuPathData * const
        data = static_cast<const wxHaikuPathData *>(path.GetPathData());
    if ( data->IsEmpty() )
        return;

    if ( !Begin() )
        return;

    const bool offset = ShouldOffset();
    if ( offset )
    {
        // half a device pixel in each direction
        Matrix m = m_state.matrix;
        m.tx += 0.5;
        m.ty += 0.5;
        m_view->SetTransform(BAffineTransform(m.a, m.b, m.c, m.d,
                                              m.tx + m_offset.x,
                                              m.ty + m_offset.y));
    }

    m_view->SetPenSize((float)pen->m_width);
    m_view->SetLineMode(pen->m_cap, pen->m_join, 10.0f);

    BShape dashed;
    BShape *shape;
    if ( !pen->m_dashes.empty() )
    {
        BuildDashedShape(dashed, data, pen->m_dashes);
        shape = &dashed;
    }
    else
    {
        shape = data->GetShape();
    }

    if ( pen->m_gradient.type != wxGRADIENT_NONE )
    {
        SetupBlending(pen->m_gradient.hasAlpha);
        BGradient * const gradient = pen->m_gradient.Create();
        m_view->StrokeShape(shape, *gradient);
        delete gradient;
    }
    else
    {
        SetupColour(pen->m_colour);
        m_view->StrokeShape(shape);
    }

    if ( offset )
        ApplyTransform();

    End();
}

void wxHaikuContext::FillPath(const wxGraphicsPath& path,
                              wxPolygonFillMode fillStyle)
{
    if ( m_brush.IsNull() || path.IsNull() )
        return;

    const wxHaikuBrushData * const
        brush = static_cast<wxHaikuBrushData *>(m_brush.GetRefData());
    if ( brush->m_transparent && brush->m_gradient.type == wxGRADIENT_NONE )
        return;

    const wxHaikuPathData * const
        data = static_cast<const wxHaikuPathData *>(path.GetPathData());
    if ( data->IsEmpty() )
        return;

    const bool isGradient = brush->m_gradient.type != wxGRADIENT_NONE;

    // Replacing (not blending) the pixels of a bitmap with an alpha channel
    // by a translucent colour, as wxGCDC::Clear() does it: app_server
    // cannot do this, the pixels are set directly.
    if ( !isGradient && m_targetAlpha
            && (m_composition == wxCOMPOSITION_SOURCE
                    || m_composition == wxCOMPOSITION_CLEAR)
            && m_state.matrix.IsRectilinear() )
    {
        double x, y, w, h;
        if ( data->IsRectangle(&x, &y, &w, &h) )
        {
            double x1 = x + w, y1 = y + h;
            m_state.matrix.Apply(&x, &y);
            m_state.matrix.Apply(&x1, &y1);

            const int dx0 = (int)floor(wxMin(x, x1) + 0.5),
                      dy0 = (int)floor(wxMin(y, y1) + 0.5),
                      dx1 = (int)floor(wxMax(x, x1) + 0.5),
                      dy1 = (int)floor(wxMax(y, y1) + 0.5);

            const wxColour colour = m_composition == wxCOMPOSITION_CLEAR
                                        ? wxColour(0, 0, 0, 0)
                                        : brush->m_colour;
            if ( FillPixels(wxRect(dx0, dy0, dx1 - dx0, dy1 - dy0), colour) )
                return;
        }
    }

    if ( !isGradient && brush->m_colour.Alpha() == 0
            && m_composition == wxCOMPOSITION_OVER )
    {
        return;
    }

    if ( !Begin() )
        return;

    m_view->SetFillRule(fillStyle == wxWINDING_RULE ? B_NONZERO : B_EVEN_ODD);

    BShape * const shape = data->GetShape();

    if ( isGradient )
    {
        SetupBlending(brush->m_gradient.hasAlpha);
        BGradient * const gradient = brush->m_gradient.Create();
        m_view->FillShape(shape, *gradient);
        delete gradient;
    }
    else if ( m_composition == wxCOMPOSITION_CLEAR )
    {
        // on an opaque target: black
        m_view->SetDrawingMode(B_OP_COPY);
        m_view->SetHighColor(0, 0, 0, 255);
        m_view->FillShape(shape);
    }
    else if ( brush->m_stipple.IsOk() )
    {
        // the bitmap tiled inside the path
        double bx, by, bw, bh;
        data->GetBox(&bx, &by, &bw, &bh);

        m_view->PushState();
        m_view->ClipToShape(shape);

        if ( brush->m_stipple.HaikuNeedsAlphaBlend(true) )
        {
            m_view->SetDrawingMode(B_OP_ALPHA);
            m_view->SetBlendingMode(B_PIXEL_ALPHA,
                                    m_targetAlpha ? B_ALPHA_COMPOSITE
                                                  : B_ALPHA_OVERLAY);
        }
        else
        {
            m_view->SetDrawingMode(B_OP_COPY);
        }

        m_view->DrawTiledBitmapAsync(brush->m_stipple.GetHaikuDrawBitmap(true),
                                     BRect((float)bx, (float)by,
                                           (float)(bx + bw), (float)(by + bh)),
                                     B_ORIGIN);
        m_view->PopState();
    }
    else
    {
        SetupColour(brush->m_colour);
        m_view->FillShape(shape, brush->m_pattern);
    }

    End();
}

bool wxHaikuContext::FillPixels(const wxRect& rectOrig, const wxColour& colour)
{
    if ( !m_targetBitmap.IsOk() )
        return false;

    // limited to the bitmap and to the bounding box of the clipping
    wxRect rect(rectOrig);
    rect.Offset(m_offset);
    rect.Intersect(wxRect(0, 0, m_targetBitmap.GetWidth(),
                          m_targetBitmap.GetHeight()));

    wxRegion clip;
    bool clipped = false;
    if ( m_state.hasClip )
    {
        clip = m_state.clip;
        if ( !clip.IsEmpty() )
            clip.Offset(m_offset);
        if ( m_hasBaseClip )
            clip.Intersect(m_baseClip);
        clipped = true;
    }
    else if ( m_hasBaseClip )
    {
        clip = m_baseClip;
        clipped = true;
    }

    if ( rect.IsEmpty() )
        return true;

    // waits for the drawing in progress
    unsigned char * const bits = m_targetBitmap.HaikuGetBits();
    const int rowBytes = m_targetBitmap.HaikuGetRowBytes();

    const unsigned char b = colour.Blue(), g = colour.Green(),
                        r = colour.Red(), a = colour.Alpha();

    wxRegion fill(rect);
    if ( clipped )
        fill.Intersect(clip);

    for ( wxRegionIterator it(fill); it; ++it )
    {
        const wxRect part = it.GetRect();
        for ( int y = part.y; y < part.y + part.height; y++ )
        {
            unsigned char *p = bits + y * rowBytes + part.x * 4;
            for ( int x = 0; x < part.width; x++, p += 4 )
            {
                p[0] = b;
                p[1] = g;
                p[2] = r;
                p[3] = a;
            }
        }
    }

    m_targetBitmap.HaikuTouch();

    return true;
}

void wxHaikuContext::ClearRectangle(wxDouble x, wxDouble y,
                                    wxDouble w, wxDouble h)
{
    if ( m_targetAlpha && m_state.matrix.IsRectilinear() )
    {
        double x1 = x + w, y1 = y + h;
        m_state.matrix.Apply(&x, &y);
        m_state.matrix.Apply(&x1, &y1);

        const int dx0 = (int)floor(wxMin(x, x1) + 0.5),
                  dy0 = (int)floor(wxMin(y, y1) + 0.5),
                  dx1 = (int)floor(wxMax(x, x1) + 0.5),
                  dy1 = (int)floor(wxMax(y, y1) + 0.5);

        if ( FillPixels(wxRect(dx0, dy0, dx1 - dx0, dy1 - dy0),
                        wxColour(0, 0, 0, 0)) )
        {
            return;
        }

        // back to user coordinates for the fallback
        Matrix inverse = m_state.matrix;
        inverse.Invert();
        inverse.Apply(&x, &y);
    }

    // an opaque target has nothing to become transparent: black, as the
    // other implementations leave it
    if ( !Begin() )
        return;

    BShape shape;
    shape.MoveTo(PT(x, y));
    shape.LineTo(PT(x + w, y));
    shape.LineTo(PT(x + w, y + h));
    shape.LineTo(PT(x, y + h));
    shape.Close();

    m_view->SetDrawingMode(B_OP_COPY);
    m_view->SetHighColor(0, 0, 0, 255);
    m_view->FillShape(&shape);

    End();
}

// ----------------------------------------------------------------------------
// text
// ----------------------------------------------------------------------------

void wxHaikuContext::DoDrawText(const wxString& str, wxDouble x, wxDouble y)
{
    wxCHECK_RET( !m_font.IsNull(),
                 wxS("wxHaikuContext::DrawText - no valid font set") );

    if ( str.empty() )
        return;

    const wxHaikuFontData * const
        font = static_cast<wxHaikuFontData *>(m_font.GetRefData());
    if ( !font->m_font.IsOk() || font->m_colour.Alpha() == 0 )
        return;

    if ( !Begin() )
        return;

    int ascent, descent;
    font->m_font.HaikuGetMetrics(&ascent, &descent, NULL);

    SetupColour(font->m_colour);
    m_view->SetFont(font->m_font.GetHaikuFont());

    const wxScopedCharBuffer utf8 = str.utf8_str();
    m_view->DrawString(utf8, (int32)utf8.length(), PT(x, y + ascent));

    if ( font->m_font.GetUnderlined() || font->m_font.GetStrikethrough() )
    {
        // drawing the string moved the pen, which offsets shapes
        m_view->MovePenTo(B_ORIGIN);

        const int width = font->m_font.HaikuGetTextWidth(str);
        const float thickness = wxMax(1.0f, (ascent + descent) / 14.0f);

        m_view->SetPenSize(thickness);
        m_view->SetLineMode(B_BUTT_CAP, B_MITER_JOIN);

        if ( font->m_font.GetUnderlined() )
        {
            const double ly = y + ascent + wxMax(1.0, descent / 3.0) + thickness / 2;
            BShape line;
            line.MoveTo(PT(x, ly));
            line.LineTo(PT(x + width, ly));
            m_view->StrokeShape(&line);
        }

        if ( font->m_font.GetStrikethrough() )
        {
            const double ly = y + ascent * 0.68;
            BShape line;
            line.MoveTo(PT(x, ly));
            line.LineTo(PT(x + width, ly));
            m_view->StrokeShape(&line);
        }
    }

    End();
}

void wxHaikuContext::GetTextExtent(const wxString& text,
                                   wxDouble *width, wxDouble *height,
                                   wxDouble *descent,
                                   wxDouble *externalLeading) const
{
    wxCHECK_RET( !m_font.IsNull(),
                 wxS("wxHaikuContext::GetTextExtent - no valid font set") );

    const wxHaikuFontData * const
        font = static_cast<wxHaikuFontData *>(m_font.GetRefData());

    int ascent = 0, fontDescent = 0, leading = 0, w = 0;
    if ( font->m_font.IsOk() )
    {
        font->m_font.HaikuGetMetrics(&ascent, &fontDescent, &leading);
        w = font->m_font.HaikuGetTextWidth(text);
    }

    if ( width )
        *width = w;
    if ( height )
        *height = ascent + fontDescent;
    if ( descent )
        *descent = fontDescent;
    if ( externalLeading )
        *externalLeading = leading;
}

void wxHaikuContext::GetPartialTextExtents(const wxString& text,
                                           wxArrayDouble& widths) const
{
    widths.Empty();

    wxCHECK_RET( !m_font.IsNull(),
                 wxS("wxHaikuContext::GetPartialTextExtents - no valid font set") );

    const wxHaikuFontData * const
        font = static_cast<wxHaikuFontData *>(m_font.GetRefData());

    wxArrayInt intWidths;
    if ( font->m_font.IsOk() )
        font->m_font.HaikuGetPartialExtents(text, intWidths);

    widths.reserve(intWidths.size());
    for ( size_t n = 0; n < intWidths.size(); n++ )
        widths.push_back(intWidths[n]);
}

// ----------------------------------------------------------------------------
// bitmaps
// ----------------------------------------------------------------------------

void wxHaikuContext::DrawBitmap(const wxGraphicsBitmap& bmp,
                                wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
    if ( bmp.IsNull() )
        return;

    DrawBitmap(static_cast<wxHaikuBitmapData *>(bmp.GetRefData())->m_bitmap,
               x, y, w, h);
}

void wxHaikuContext::DrawBitmap(const wxBitmap& bmp,
                                wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
    if ( !bmp.IsOk() || w <= 0 || h <= 0 )
        return;

    BBitmap * const native = bmp.GetHaikuDrawBitmap(true);
    if ( !native )
        return;

    if ( !Begin() )
        return;

    if ( m_composition == wxCOMPOSITION_SOURCE )
    {
        m_view->SetDrawingMode(B_OP_COPY);
    }
    else if ( bmp.HaikuNeedsAlphaBlend(true) )
    {
        m_view->SetDrawingMode(B_OP_ALPHA);
        m_view->SetBlendingMode(B_PIXEL_ALPHA,
                                m_targetAlpha ? B_ALPHA_COMPOSITE
                                              : B_ALPHA_OVERLAY);
    }
    else
    {
        m_view->SetDrawingMode(B_OP_COPY);
    }

    const BRect src(0, 0, bmp.GetWidth() - 1, bmp.GetHeight() - 1);
    const BRect dst((float)x, (float)y, (float)(x + w - 1), (float)(y + h - 1));

    const bool scaled = w != bmp.GetWidth() || h != bmp.GetHeight()
                            || !m_state.matrix.IsRectilinear()
                            || m_state.matrix.ScaleFactor() != 1;

    uint32 options = 0;
    if ( scaled && m_interpolation != wxINTERPOLATION_NONE )
        options = B_FILTER_BITMAP_BILINEAR;

    m_view->DrawBitmapAsync(native, src, dst, options);

    End();
}

void wxHaikuContext::DrawIcon(const wxIcon& icon,
                              wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
    DrawBitmap(icon, x, y, w, h);
}

// ----------------------------------------------------------------------------
// wxHaikuGraphicsRenderer
// ----------------------------------------------------------------------------

class wxHaikuGraphicsRenderer : public wxGraphicsRenderer
{
public:
    wxHaikuGraphicsRenderer() { }

    virtual wxGraphicsContext *CreateContext(const wxWindowDC& dc) wxOVERRIDE
    {
        return new wxHaikuContext(this, dc, dc.GetWindow());
    }

    virtual wxGraphicsContext *CreateContext(const wxMemoryDC& dc) wxOVERRIDE
    {
        return new wxHaikuContext(this, dc);
    }

#if wxUSE_PRINTING_ARCHITECTURE
    virtual wxGraphicsContext *CreateContext(const wxPrinterDC& WXUNUSED(dc)) wxOVERRIDE
    {
        return NULL;
    }
#endif

    virtual wxGraphicsContext *CreateContextFromNativeContext(void *WXUNUSED(context)) wxOVERRIDE
    {
        // a bare BView does not say where the device origin is or what
        // else draws on it: contexts are made from DCs and windows
        return NULL;
    }

    virtual wxGraphicsContext *CreateContextFromNativeWindow(void *WXUNUSED(window)) wxOVERRIDE
    {
        return NULL;
    }

    virtual wxGraphicsContext *CreateContext(wxWindow *window) wxOVERRIDE
    {
        return new wxHaikuContext(this, window);
    }

#if wxUSE_IMAGE
    virtual wxGraphicsContext *CreateContextFromImage(wxImage& image) wxOVERRIDE
    {
        return new wxHaikuContext(this, image);
    }
#endif // wxUSE_IMAGE

    virtual wxGraphicsContext *CreateMeasuringContext() wxOVERRIDE
    {
        return new wxHaikuContext(this);
    }

    virtual wxGraphicsPath CreatePath() wxOVERRIDE
    {
        wxGraphicsPath path;
        path.SetRefData(new wxHaikuPathData(this));
        return path;
    }

    virtual wxGraphicsMatrix CreateMatrix(wxDouble a = 1.0, wxDouble b = 0.0,
                                          wxDouble c = 0.0, wxDouble d = 1.0,
                                          wxDouble tx = 0.0, wxDouble ty = 0.0) wxOVERRIDE
    {
        wxGraphicsMatrix matrix;
        matrix.SetRefData(new wxHaikuMatrixData(this, Matrix(a, b, c, d, tx, ty)));
        return matrix;
    }

    virtual wxGraphicsPen CreatePen(const wxGraphicsPenInfo& info) wxOVERRIDE
    {
        wxGraphicsPen pen;
        if ( info.GetStyle() != wxPENSTYLE_TRANSPARENT )
            pen.SetRefData(new wxHaikuPenData(this, info));
        return pen;
    }

    virtual wxGraphicsBrush CreateBrush(const wxBrush& brush) wxOVERRIDE
    {
        wxGraphicsBrush result;
        if ( brush.IsOk() && brush.GetStyle() != wxBRUSHSTYLE_TRANSPARENT )
            result.SetRefData(new wxHaikuBrushData(this, brush));
        return result;
    }

    virtual wxGraphicsBrush
    CreateLinearGradientBrush(wxDouble x1, wxDouble y1,
                              wxDouble x2, wxDouble y2,
                              const wxGraphicsGradientStops& stops,
                              const wxGraphicsMatrix& matrix) wxOVERRIDE
    {
        wxHaikuBrushData * const data = new wxHaikuBrushData(this);
        data->m_gradient.Set(wxGRADIENT_LINEAR, x1, y1, x2, y2, 0, stops, matrix);

        wxGraphicsBrush brush;
        brush.SetRefData(data);
        return brush;
    }

    virtual wxGraphicsBrush
    CreateRadialGradientBrush(wxDouble startX, wxDouble startY,
                              wxDouble endX, wxDouble endY,
                              wxDouble radius,
                              const wxGraphicsGradientStops& stops,
                              const wxGraphicsMatrix& matrix) wxOVERRIDE
    {
        wxHaikuBrushData * const data = new wxHaikuBrushData(this);
        data->m_gradient.Set(wxGRADIENT_RADIAL, startX, startY, endX, endY,
                             radius, stops, matrix);

        wxGraphicsBrush brush;
        brush.SetRefData(data);
        return brush;
    }

    virtual wxGraphicsFont CreateFont(const wxFont& font,
                                      const wxColour& col) wxOVERRIDE
    {
        wxGraphicsFont result;
        if ( font.IsOk() )
            result.SetRefData(new wxHaikuFontData(this, font, col));
        return result;
    }

    virtual wxGraphicsFont CreateFont(double sizeInPixels,
                                      const wxString& facename,
                                      int flags,
                                      const wxColour& col) wxOVERRIDE
    {
        wxFont font(wxFontInfo(wxSize(0, wxRound(sizeInPixels)))
                        .FaceName(facename).AllFlags(flags));

        wxGraphicsFont result;
        result.SetRefData(new wxHaikuFontData(this, font, col));
        return result;
    }

    virtual wxGraphicsFont CreateFontAtDPI(const wxFont& font,
                                           const wxRealPoint& WXUNUSED(dpi),
                                           const wxColour& col) wxOVERRIDE
    {
        // fonts are sized in pixels already
        return CreateFont(font, col);
    }

    virtual wxGraphicsBitmap CreateBitmap(const wxBitmap& bitmap) wxOVERRIDE
    {
        wxGraphicsBitmap result;
        if ( bitmap.IsOk() )
            result.SetRefData(new wxHaikuBitmapData(this, bitmap));
        return result;
    }

#if wxUSE_IMAGE
    virtual wxGraphicsBitmap CreateBitmapFromImage(const wxImage& image) wxOVERRIDE
    {
        wxGraphicsBitmap result;
        if ( image.IsOk() )
            result.SetRefData(new wxHaikuBitmapData(this, wxBitmap(image)));
        return result;
    }

    virtual wxImage CreateImageFromBitmap(const wxGraphicsBitmap& bmp) wxOVERRIDE
    {
        if ( bmp.IsNull() )
            return wxNullImage;

        return static_cast<wxHaikuBitmapData *>(bmp.GetRefData())
                    ->m_bitmap.ConvertToImage();
    }
#endif // wxUSE_IMAGE

    virtual wxGraphicsBitmap CreateBitmapFromNativeBitmap(void *bitmap) wxOVERRIDE
    {
        wxGraphicsBitmap result;
        const wxBitmap copy = wxHaikuBitmapFromNative((const BBitmap *)bitmap);
        if ( copy.IsOk() )
            result.SetRefData(new wxHaikuBitmapData(this, copy));
        return result;
    }

    virtual wxGraphicsBitmap CreateSubBitmap(const wxGraphicsBitmap& bitmap,
                                             wxDouble x, wxDouble y,
                                             wxDouble w, wxDouble h) wxOVERRIDE
    {
        wxGraphicsBitmap result;
        if ( bitmap.IsNull() )
            return result;

        const wxBitmap& source
            = static_cast<wxHaikuBitmapData *>(bitmap.GetRefData())->m_bitmap;

        wxRect rect((int)x, (int)y, (int)w, (int)h);
        rect.Intersect(wxRect(0, 0, source.GetWidth(), source.GetHeight()));
        if ( !rect.IsEmpty() )
        {
            result.SetRefData(new wxHaikuBitmapData(this,
                                                    source.GetSubBitmap(rect)));
        }

        return result;
    }

    virtual wxString GetName() const wxOVERRIDE
    {
        return "haiku";
    }

    virtual void GetVersion(int *major, int *minor, int *micro) const wxOVERRIDE
    {
        if ( major )
            *major = 1;
        if ( minor )
            *minor = 0;
        if ( micro )
            *micro = 0;
    }

private:
    wxDECLARE_NO_COPY_CLASS(wxHaikuGraphicsRenderer);
};

wxHaikuGraphicsRenderer gs_haikuRenderer;

wxGraphicsRenderer *GetHaikuRenderer()
{
    return &gs_haikuRenderer;
}

} // anonymous namespace

/* static */
wxGraphicsRenderer *wxGraphicsRenderer::GetDefaultRenderer()
{
    return &gs_haikuRenderer;
}

#endif // wxUSE_GRAPHICS_CONTEXT
