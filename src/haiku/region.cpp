/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/region.cpp
// Purpose:     wxRegion on BRegion
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/region.h"
#include "wx/utils.h"

#include <algorithm>
#include <vector>

#include "wx/haiku/private.h"

wxIMPLEMENT_DYNAMIC_CLASS(wxRegion, wxGDIObject);
wxIMPLEMENT_DYNAMIC_CLASS(wxRegionIterator, wxObject);

// ----------------------------------------------------------------------------
// wxRegionRefData
// ----------------------------------------------------------------------------

class wxRegionRefData : public wxGDIRefData
{
public:
    wxRegionRefData() { }
    wxRegionRefData(const BRect& rect) : m_region(rect) { }
    wxRegionRefData(const wxRegionRefData& data)
        : wxGDIRefData(), m_region(data.m_region) { }

    BRegion m_region;
};

#define M_REGION (static_cast<wxRegionRefData *>(m_refData)->m_region)
#define M_REGION_OF(r) (static_cast<wxRegionRefData *>((r).m_refData)->m_region)

// ----------------------------------------------------------------------------
// wxRegion
// ----------------------------------------------------------------------------

wxGDIRefData *wxRegion::CreateGDIRefData() const
{
    return new wxRegionRefData;
}

wxGDIRefData *wxRegion::CloneGDIRefData(const wxGDIRefData *data) const
{
    return new wxRegionRefData(*static_cast<const wxRegionRefData *>(data));
}

wxRegion::wxRegion()
{
    m_refData = NULL;
}

wxRegion::wxRegion(wxCoord x, wxCoord y, wxCoord w, wxCoord h)
{
    m_refData = w > 0 && h > 0 ? new wxRegionRefData(wxHaikuRect(x, y, w, h))
                               : new wxRegionRefData;
}

wxRegion::wxRegion(const wxPoint& topLeft, const wxPoint& bottomRight)
{
    m_refData = new wxRegionRefData(BRect(topLeft.x, topLeft.y,
                                          bottomRight.x, bottomRight.y));
}

wxRegion::wxRegion(const wxRect& r)
{
    m_refData = !r.IsEmpty() ? new wxRegionRefData(wxHaikuRect(r))
                             : new wxRegionRefData;
}

wxRegion::wxRegion(size_t n, const wxPoint *points, wxPolygonFillMode fillStyle)
{
    // A scan line conversion of the polygon: one rectangle per span.
    m_refData = new wxRegionRefData;

    if ( n < 3 )
        return;

    int minY = points[0].y, maxY = points[0].y;
    for ( size_t i = 1; i < n; i++ )
    {
        minY = wxMin(minY, points[i].y);
        maxY = wxMax(maxY, points[i].y);
    }

    std::vector<std::pair<double, int> > crossings;
    for ( int y = minY; y < maxY; y++ )
    {
        const double sy = y + 0.5;
        crossings.clear();

        for ( size_t i = 0; i < n; i++ )
        {
            const wxPoint& a = points[i];
            const wxPoint& b = points[(i + 1) % n];
            if ( a.y == b.y )
                continue;

            if ( (sy >= a.y && sy < b.y) || (sy >= b.y && sy < a.y) )
            {
                const double x = a.x + (sy - a.y) * (b.x - a.x) / (double)(b.y - a.y);
                crossings.push_back(std::make_pair(x, b.y > a.y ? 1 : -1));
            }
        }

        std::sort(crossings.begin(), crossings.end());

        int winding = 0;
        for ( size_t i = 0; i + 1 < crossings.size(); i++ )
        {
            winding += crossings[i].second;

            const bool inside = fillStyle == wxWINDING_RULE ? winding != 0
                                                            : (i % 2) == 0;
            if ( !inside )
                continue;

            const int x1 = (int)ceil(crossings[i].first - 0.5),
                      x2 = (int)ceil(crossings[i + 1].first - 0.5) - 1;
            if ( x2 >= x1 )
                M_REGION.Include(BRect(x1, y, x2, y));
        }
    }
}

wxRegion::~wxRegion()
{
    // m_refData unrefed in ~wxObject
}

const BRegion *wxRegion::GetHaikuRegion() const
{
    return m_refData ? &M_REGION : NULL;
}

int wxRegion::HaikuCountRects() const
{
    return m_refData ? M_REGION.CountRects() : 0;
}

wxRect wxRegion::HaikuRectAt(int n) const
{
    if ( !m_refData || n < 0 || n >= M_REGION.CountRects() )
        return wxRect();

    return wxFromHaikuRect(M_REGION.RectAt(n));
}

void wxRegion::Clear()
{
    UnRef();
}

bool wxRegion::IsEmpty() const
{
    return !m_refData || M_REGION.CountRects() == 0;
}

bool wxRegion::DoIsEqual(const wxRegion& region) const
{
    return M_REGION == M_REGION_OF(region);
}

bool wxRegion::DoGetBox(wxCoord& x, wxCoord& y, wxCoord& w, wxCoord& h) const
{
    if ( IsEmpty() )
    {
        x = y = w = h = 0;
        return false;
    }

    const wxRect r = wxFromHaikuRect(M_REGION.Frame());
    x = r.x;
    y = r.y;
    w = r.width;
    h = r.height;

    return true;
}

wxRegionContain wxRegion::DoContainsPoint(wxCoord x, wxCoord y) const
{
    if ( !m_refData )
        return wxOutRegion;

    return M_REGION.Contains((int32)x, (int32)y) ? wxInRegion : wxOutRegion;
}

wxRegionContain wxRegion::DoContainsRect(const wxRect& rect) const
{
    if ( !m_refData || rect.IsEmpty() )
        return wxOutRegion;

    const BRect r = wxHaikuRect(rect);
    if ( !M_REGION.Intersects(r) )
        return wxOutRegion;

    // entirely inside if nothing of the rectangle is left outside
    BRegion rest(r);
    rest.Exclude(&M_REGION);

    return rest.CountRects() == 0 ? wxInRegion : wxPartRegion;
}

bool wxRegion::DoOffset(wxCoord x, wxCoord y)
{
    if ( !m_refData )
        return true;

    AllocExclusive();
    M_REGION.OffsetBy(x, y);

    return true;
}

bool wxRegion::DoUnionWithRect(const wxRect& rect)
{
    if ( rect.IsEmpty() )
        return true;

    AllocExclusive();
    M_REGION.Include(wxHaikuRect(rect));

    return true;
}

bool wxRegion::DoUnionWithRegion(const wxRegion& region)
{
    if ( region.IsEmpty() )
        return true;

    AllocExclusive();
    M_REGION.Include(&M_REGION_OF(region));

    return true;
}

bool wxRegion::DoIntersect(const wxRegion& region)
{
    if ( !m_refData )
        return true;

    AllocExclusive();

    if ( region.IsEmpty() )
        M_REGION.MakeEmpty();
    else
        M_REGION.IntersectWith(&M_REGION_OF(region));

    return true;
}

bool wxRegion::DoSubtract(const wxRegion& region)
{
    if ( !m_refData || region.IsEmpty() )
        return true;

    AllocExclusive();
    M_REGION.Exclude(&M_REGION_OF(region));

    return true;
}

bool wxRegion::DoXor(const wxRegion& region)
{
    if ( region.IsEmpty() )
        return true;

    AllocExclusive();
    M_REGION.ExclusiveInclude(&M_REGION_OF(region));

    return true;
}

// ----------------------------------------------------------------------------
// wxRegionIterator
// ----------------------------------------------------------------------------

void wxRegionIterator::Reset(const wxRegion& region)
{
    m_region = region;
    m_current = 0;
}

wxRegionIterator& wxRegionIterator::operator++()
{
    if ( HaveRects() )
        ++m_current;

    return *this;
}

wxRegionIterator wxRegionIterator::operator++(int)
{
    wxRegionIterator tmp = *this;

    if ( HaveRects() )
        ++m_current;

    return tmp;
}
