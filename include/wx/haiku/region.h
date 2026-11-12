/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/region.h
// Purpose:     wxRegion on BRegion
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_REGION_H_
#define _WX_HAIKU_REGION_H_

class BRegion;

class WXDLLIMPEXP_CORE wxRegion : public wxRegionBase
{
public:
    wxRegion();
    wxRegion(wxCoord x, wxCoord y, wxCoord w, wxCoord h);
    wxRegion(const wxPoint& topLeft, const wxPoint& bottomRight);
    wxRegion(const wxRect& rect);
    wxRegion(size_t n, const wxPoint *points,
             wxPolygonFillMode fillStyle = wxODDEVEN_RULE);
    wxRegion(const wxBitmap& bmp)
    {
        Union(bmp);
    }
    wxRegion(const wxBitmap& bmp,
             const wxColour& transColour, int tolerance = 0)
    {
        Union(bmp, transColour, tolerance);
    }

    virtual ~wxRegion();

    // wxRegionBase methods
    virtual void Clear() wxOVERRIDE;
    virtual bool IsEmpty() const wxOVERRIDE;

    // the native region (NULL for an invalid region); BRegion is purely
    // client side, there is no server object behind it
    const BRegion *GetHaikuRegion() const;

    // number of rectangles the region is made of and the n-th one
    int HaikuCountRects() const;
    wxRect HaikuRectAt(int n) const;

protected:
    virtual wxGDIRefData *CreateGDIRefData() const wxOVERRIDE;
    virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const wxOVERRIDE;

    // wxRegionBase pure virtuals
    virtual bool DoIsEqual(const wxRegion& region) const wxOVERRIDE;
    virtual bool DoGetBox(wxCoord& x, wxCoord& y, wxCoord& w, wxCoord& h) const wxOVERRIDE;
    virtual wxRegionContain DoContainsPoint(wxCoord x, wxCoord y) const wxOVERRIDE;
    virtual wxRegionContain DoContainsRect(const wxRect& rect) const wxOVERRIDE;

    virtual bool DoOffset(wxCoord x, wxCoord y) wxOVERRIDE;
    virtual bool DoUnionWithRect(const wxRect& rect) wxOVERRIDE;
    virtual bool DoUnionWithRegion(const wxRegion& region) wxOVERRIDE;
    virtual bool DoIntersect(const wxRegion& region) wxOVERRIDE;
    virtual bool DoSubtract(const wxRegion& region) wxOVERRIDE;
    virtual bool DoXor(const wxRegion& region) wxOVERRIDE;

    friend class WXDLLIMPEXP_FWD_CORE wxRegionIterator;

    wxDECLARE_DYNAMIC_CLASS(wxRegion);
};

class WXDLLIMPEXP_CORE wxRegionIterator : public wxObject
{
public:
    wxRegionIterator() : m_current(0) { }
    wxRegionIterator(const wxRegion& region) { Reset(region); }

    void Reset() { m_current = 0; }
    void Reset(const wxRegion& region);

    bool HaveRects() const { return m_current < m_region.HaikuCountRects(); }
    operator bool() const { return HaveRects(); }

    wxRegionIterator& operator++();
    wxRegionIterator operator++(int);

    wxCoord GetX() const { return GetRect().x; }
    wxCoord GetY() const { return GetRect().y; }
    wxCoord GetW() const { return GetRect().width; }
    wxCoord GetWidth() const { return GetW(); }
    wxCoord GetH() const { return GetRect().height; }
    wxCoord GetHeight() const { return GetH(); }
    wxRect GetRect() const { return m_region.HaikuRectAt(m_current); }

private:
    wxRegion m_region;
    int m_current;

    wxDECLARE_DYNAMIC_CLASS(wxRegionIterator);
};

#endif // _WX_HAIKU_REGION_H_
