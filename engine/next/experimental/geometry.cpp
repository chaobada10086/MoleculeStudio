#include "next/experimental/geometry.hpp"

#include <algorithm>
#include <cmath>

namespace r62 {
namespace experimental {

namespace {
const double kPi = 3.14159265358979323846;
}

// ---------------------------------------------------------------------------
// PlateFrame
// ---------------------------------------------------------------------------
PlateFrame::PlateFrame()
    : centre(0, 0), axisU(60.0, 0.0), axisV(0.0, 40.0) {}

PlateFrame::PlateFrame(const core::Vec2& c, const core::Vec2& u, const core::Vec2& v)
    : centre(c), axisU(u), axisV(v) {}

core::Vec2 PlateFrame::pointAt(double u, double v) const {
    return core::Vec2(centre.x + u * axisU.x + v * axisV.x,
                      centre.y + u * axisU.y + v * axisV.y);
}

void PlateFrame::corners(core::Vec2 out[4]) const {
    out[0] = pointAt(-1.0, -1.0);
    out[1] = pointAt( 1.0, -1.0);
    out[2] = pointAt( 1.0,  1.0);
    out[3] = pointAt(-1.0,  1.0);
}

core::Box PlateFrame::aabb() const {
    core::Vec2 c[4];
    corners(c);
    core::Box box(c[0], c[0]);
    for (int i = 1; i < 4; ++i) box.add(c[i]);
    return box;
}

double PlateFrame::halfWidth() const { return core::length(axisU); }
double PlateFrame::halfHeight() const { return core::length(axisV); }

bool PlateFrame::degenerate() const {
    return halfWidth() < 1e-9 || halfHeight() < 1e-9;
}

bool PlateFrame::relativeOf(const core::Vec2& point, double* u, double* v) const {
    const double det = axisU.x * axisV.y - axisU.y * axisV.x;
    if (std::fabs(det) < 1e-12) return false;
    const double dx = point.x - centre.x;
    const double dy = point.y - centre.y;
    if (u) *u = (dx * axisV.y - dy * axisV.x) / det;
    if (v) *v = (axisU.x * dy - axisU.y * dx) / det;
    return true;
}

PlateFrame frameFromBox(const core::Box& box) {
    const core::Vec2 c((box.min.x + box.max.x) * 0.5, (box.min.y + box.max.y) * 0.5);
    return PlateFrame(c, core::Vec2((box.max.x - box.min.x) * 0.5, 0.0),
                         core::Vec2(0.0, (box.max.y - box.min.y) * 0.5));
}

PlateFrame frameFromDrag(const core::Vec2& begin, const core::Vec2& current) {
    const core::Vec2 c((begin.x + current.x) * 0.5, (begin.y + current.y) * 0.5);
    return PlateFrame(c, core::Vec2((current.x - begin.x) * 0.5, 0.0),
                         core::Vec2(0.0, (current.y - begin.y) * 0.5));
}

PlateFrame frameScaled(const PlateFrame& frame, double factor) {
    return PlateFrame(frame.centre, frame.axisU * factor, frame.axisV * factor);
}

namespace {
core::Vec2 rotateVec(const core::Vec2& v, double degrees) {
    const double r = degrees * kPi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    // Matches core::direction(): +90 maps (1,0) to (0,-1), i.e. up on screen.
    return core::Vec2(v.x * c + v.y * s, -v.x * s + v.y * c);
}
} // namespace

PlateFrame rotateFrame(const PlateFrame& frame, const core::Vec2& pivot, double degrees) {
    const core::Vec2 rel(frame.centre.x - pivot.x, frame.centre.y - pivot.y);
    const core::Vec2 rotated = rotateVec(rel, degrees);
    return PlateFrame(core::Vec2(pivot.x + rotated.x, pivot.y + rotated.y),
                      rotateVec(frame.axisU, degrees), rotateVec(frame.axisV, degrees));
}

double relativeTolerance(const PlateFrame& frame, double documentTolerance) {
    const double smaller = frame.halfWidth() < frame.halfHeight() ? frame.halfWidth()
                                                                  : frame.halfHeight();
    if (smaller < 1e-9) return documentTolerance;
    return documentTolerance / smaller;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
core::Error buildBoardGeometry(const PlateFigure& figure, const PlateFrame& frame,
                               BoardGeometry* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    if (frame.degenerate())
        return core::Error(core::ErrorCode::InvalidGeometry, "the board frame is degenerate");
    std::string why;
    core::Error e = validateFigure(figure, &why);
    if (!e.ok()) return e;

    out->figure = figure;
    out->frame = frame;
    const core::Vec2 c0 = frame.pointAt(-1, -1);
    const core::Vec2 c1 = frame.pointAt(1, -1);
    const core::Vec2 c2 = frame.pointAt(1, 1);
    const core::Vec2 c3 = frame.pointAt(-1, 1);
    out->boardBox = core::Box(c0, c0);
    out->boardBox.add(c1);
    out->boardBox.add(c2);
    out->boardBox.add(c3);
    out->bounds = out->boardBox;
    const double halfStroke = figure.border.width > 0.0 ? figure.border.width * 0.5 : 0.3;
    out->bounds.min.x -= halfStroke;
    out->bounds.min.y -= halfStroke;
    out->bounds.max.x += halfStroke;
    out->bounds.max.y += halfStroke;
    out->rfDefined = rfMappingDefined(figure, &out->rfWhy);
    return core::Error::success();
}

void boardCornerPolygon(const PlateFrame& frame, core::Vec2 out[4]) {
    out[0] = frame.pointAt(-1.0, -1.0);
    out[1] = frame.pointAt( 1.0, -1.0);
    out[2] = frame.pointAt( 1.0,  1.0);
    out[3] = frame.pointAt(-1.0,  1.0);
}

std::vector<core::Vec2> ellipsePolygon(const PlateFrame& frame, double cu, double cv,
                                       double ru, double rv, int segments) {
    std::vector<core::Vec2> out;
    if (segments < 8) segments = 8;
    if (segments > 128) segments = 128;
    out.reserve(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(segments);
        out.push_back(frame.pointAt(cu + ru * std::cos(a), cv + rv * std::sin(a)));
    }
    return out;
}

std::vector<core::Vec2> bandPolygon(const PlateFrame& frame, const Lane& lane, const Band& band) {
    const double half = (lane.width * 0.5) * clampDouble(band.widthFraction, 0.05, 1.0);
    const double halfT = band.thicknessV * 0.5;
    std::vector<core::Vec2> out;
    out.push_back(frame.pointAt(lane.u - half, band.v - halfT));
    out.push_back(frame.pointAt(lane.u + half, band.v - halfT));
    out.push_back(frame.pointAt(lane.u + half, band.v + halfT));
    out.push_back(frame.pointAt(lane.u - half, band.v + halfT));
    return out;
}

std::vector<core::Vec2> wellPolygon(const PlateFrame& frame, const Lane& lane, double topV,
                                    double depth) {
    const double half = lane.width * 0.5;
    std::vector<core::Vec2> out;
    out.push_back(frame.pointAt(lane.u - half, topV));
    out.push_back(frame.pointAt(lane.u + half, topV));
    out.push_back(frame.pointAt(lane.u + half, topV + depth));
    out.push_back(frame.pointAt(lane.u - half, topV + depth));
    return out;
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------
bool boardHitTest(const PlateFigure& figure, const PlateFrame& frame,
                  const core::Vec2& point, double documentTolerance) {
    double u = 0.0, v = 0.0;
    if (!frame.relativeOf(point, &u, &v)) return false;
    const double tol = relativeTolerance(frame, documentTolerance);
    (void)figure;
    return u >= -1.0 - tol && u <= 1.0 + tol && v >= -1.0 - tol && v <= 1.0 + tol;
}

const char* elementKindName(ElementKind kind) {
    switch (kind) {
    case ElementKind::None:      return "None";
    case ElementKind::Body:      return "Body";
    case ElementKind::Lane:      return "Lane";
    case ElementKind::Spot:      return "Spot";
    case ElementKind::Band:      return "Band";
    case ElementKind::OriginLine:return "OriginLine";
    case ElementKind::FrontLine: return "FrontLine";
    }
    return "None";
}

ElementRef pickElement(const PlateFigure& figure, const PlateFrame& frame,
                       const core::Vec2& point, double documentTolerance) {
    ElementRef result;
    double u = 0.0, v = 0.0;
    if (!frame.relativeOf(point, &u, &v)) return result;
    const double tol = relativeTolerance(frame, documentTolerance);

    // Spots first (smallest elements), most recently added first so it matches
    // paint order on a tie.
    for (size_t i = figure.spots.size(); i > 0; --i) {
        const Spot& spot = figure.spots[i - 1];
        const double du = (u - spot.u) / (spot.radiusU + tol);
        const double dv = (v - spot.v) / (spot.radiusV + tol);
        if (du * du + dv * dv <= 1.0) {
            result.kind = ElementKind::Spot;
            result.id = spot.id;
            result.u = u;
            result.v = v;
            return result;
        }
    }

    for (size_t i = figure.bands.size(); i > 0; --i) {
        const Band& band = figure.bands[i - 1];
        const Lane* lane = findLane(figure, band.laneId);
        if (!lane) continue;
        const double half = (lane->width * 0.5) * clampDouble(band.widthFraction, 0.05, 1.0) + tol;
        const double halfT = band.thicknessV * 0.5 + tol;
        if (std::fabs(u - lane->u) <= half && std::fabs(v - band.v) <= halfT) {
            result.kind = ElementKind::Band;
            result.id = band.id;
            result.u = u;
            result.v = v;
            return result;
        }
    }

    for (size_t i = 0; i < figure.lanes.size(); ++i) {
        const Lane& lane = figure.lanes[i];
        if (std::fabs(u - lane.u) <= lane.width * 0.5 + tol &&
            std::fabs(v) <= figure.laneMarginV + tol) {
            result.kind = ElementKind::Lane;
            result.id = lane.id;
            result.u = u;
            result.v = v;
            return result;
        }
    }

    if (figure.originDefined && figure.kind == FigureKind::TlcPlate &&
        std::fabs(v - figure.originV) <= tol) {
        result.kind = ElementKind::OriginLine;
        result.u = u; result.v = v;
        return result;
    }
    if (figure.frontDefined && figure.kind == FigureKind::TlcPlate &&
        std::fabs(v - figure.frontV) <= tol) {
        result.kind = ElementKind::FrontLine;
        result.u = u; result.v = v;
        return result;
    }

    if (u >= -1.0 - tol && u <= 1.0 + tol && v >= -1.0 - tol && v <= 1.0 + tol) {
        result.kind = ElementKind::Body;
        result.u = u;
        result.v = v;
        return result;
    }
    return result;
}

core::Vec2 elementDocumentCentre(const PlateFigure& figure, const PlateFrame& frame,
                                 const ElementRef& element) {
    switch (element.kind) {
    case ElementKind::Spot: {
        const Spot* spot = findSpot(figure, element.id);
        if (spot) return frame.pointAt(spot->u, spot->v);
        break;
    }
    case ElementKind::Band: {
        const Band* band = findBand(figure, element.id);
        if (band) {
            const Lane* lane = findLane(figure, band->laneId);
            if (lane) return frame.pointAt(lane->u, band->v);
        }
        break;
    }
    case ElementKind::Lane: {
        const Lane* lane = findLane(figure, element.id);
        if (lane) return frame.pointAt(lane->u, 0.0);
        break;
    }
    case ElementKind::OriginLine:
        return frame.pointAt(0.0, figure.originV);
    case ElementKind::FrontLine:
        return frame.pointAt(0.0, figure.frontV);
    default:
        break;
    }
    return frame.centre;
}

// ---------------------------------------------------------------------------
// Resize handles
// ---------------------------------------------------------------------------
const char* boardHandleName(BoardHandle handle) {
    switch (handle) {
    case BoardHandle::None:       return "None";
    case BoardHandle::Left:       return "Left";
    case BoardHandle::Right:      return "Right";
    case BoardHandle::Top:        return "Top";
    case BoardHandle::Bottom:     return "Bottom";
    case BoardHandle::TopLeft:    return "TopLeft";
    case BoardHandle::TopRight:   return "TopRight";
    case BoardHandle::BottomLeft: return "BottomLeft";
    case BoardHandle::BottomRight:return "BottomRight";
    }
    return "None";
}

void boardHandleRelative(BoardHandle handle, double* u, double* v) {
    double hu = 0.0, hv = 0.0;
    switch (handle) {
    case BoardHandle::Left:        hu = -1.0; break;
    case BoardHandle::Right:       hu =  1.0; break;
    case BoardHandle::Top:         hv = -1.0; break;
    case BoardHandle::Bottom:      hv =  1.0; break;
    case BoardHandle::TopLeft:     hu = -1.0; hv = -1.0; break;
    case BoardHandle::TopRight:    hu =  1.0; hv = -1.0; break;
    case BoardHandle::BottomLeft:  hu = -1.0; hv =  1.0; break;
    case BoardHandle::BottomRight: hu =  1.0; hv =  1.0; break;
    default: break;
    }
    if (u) *u = hu;
    if (v) *v = hv;
}

BoardHandle pickBoardHandle(const PlateFrame& frame, const core::Vec2& point,
                            double documentTolerance) {
    if (frame.degenerate()) return BoardHandle::None;
    double u = 0.0, v = 0.0;
    if (!frame.relativeOf(point, &u, &v)) return BoardHandle::None;
    const double tol = relativeTolerance(frame, documentTolerance);
    const bool nearLeft = std::fabs(u + 1.0) <= tol;
    const bool nearRight = std::fabs(u - 1.0) <= tol;
    const bool nearTop = std::fabs(v + 1.0) <= tol;
    const bool nearBottom = std::fabs(v - 1.0) <= tol;
    const bool spanU = u >= -1.0 - tol && u <= 1.0 + tol;
    const bool spanV = v >= -1.0 - tol && v <= 1.0 + tol;

    if (nearLeft && nearTop) return BoardHandle::TopLeft;
    if (nearRight && nearTop) return BoardHandle::TopRight;
    if (nearLeft && nearBottom) return BoardHandle::BottomLeft;
    if (nearRight && nearBottom) return BoardHandle::BottomRight;
    if (nearLeft && spanV) return BoardHandle::Left;
    if (nearRight && spanV) return BoardHandle::Right;
    if (nearTop && spanU) return BoardHandle::Top;
    if (nearBottom && spanU) return BoardHandle::Bottom;
    return BoardHandle::None;
}

PlateFrame resizeFrame(const PlateFrame& start, BoardHandle handle, const core::Vec2& pointer) {
    if (handle == BoardHandle::None || start.degenerate()) return start;

    const double w = start.halfWidth();
    const double h = start.halfHeight();
    const core::Vec2 eu(start.axisU.x / w, start.axisU.y / w);
    const core::Vec2 ev(start.axisV.x / h, start.axisV.y / h);
    const core::Vec2 d(pointer.x - start.centre.x, pointer.y - start.centre.y);
    const double det = eu.x * ev.y - eu.y * ev.x;
    double s = 0.0, t = 0.0;
    if (std::fabs(det) > 1e-12) {
        s = (d.x * ev.y - d.y * ev.x) / det;
        t = (eu.x * d.y - eu.y * d.x) / det;
    } else {
        s = core::dot(d, eu);
        t = core::dot(d, ev);
    }

    double minS = -w, maxS = w, minT = -h, maxT = h;
    switch (handle) {
    case BoardHandle::Left:        minS = s; break;
    case BoardHandle::Right:       maxS = s; break;
    case BoardHandle::Top:         minT = t; break;
    case BoardHandle::Bottom:      maxT = t; break;
    case BoardHandle::TopLeft:     minS = s; minT = t; break;
    case BoardHandle::TopRight:    maxS = s; minT = t; break;
    case BoardHandle::BottomLeft:  minS = s; maxT = t; break;
    case BoardHandle::BottomRight: maxS = s; maxT = t; break;
    default: return start;
    }

    const double newW = (maxS - minS) * 0.5;
    const double newH = (maxT - minT) * 0.5;
    if (!(newW > 1e-6) || !(newH > 1e-6)) {
        // A drag that would collapse the board is refused by returning the start
        // frame; the caller commit then validates and writes either way.
        return start;
    }
    const double cs = (minS + maxS) * 0.5;
    const double ct = (minT + maxT) * 0.5;
    const core::Vec2 centre(start.centre.x + cs * eu.x + ct * ev.x,
                            start.centre.y + cs * eu.y + ct * ev.y);
    return PlateFrame(centre, core::Vec2(eu.x * newW, eu.y * newW),
                              core::Vec2(ev.x * newH, ev.y * newH));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
bool pointInPolygon(const std::vector<core::Vec2>& polygon, const core::Vec2& point) {
    bool inside = false;
    for (size_t i = 0, j = polygon.size(); i < polygon.size(); j = i++) {
        const core::Vec2& a = polygon[i];
        const core::Vec2& b = polygon[j];
        if (((a.y > point.y) != (b.y > point.y)) &&
            (point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

double distancePointSegment(const core::Vec2& p, const core::Vec2& a, const core::Vec2& b) {
    const core::Vec2 ab(b.x - a.x, b.y - a.y);
    const double lenSq = core::lengthSq(ab);
    if (lenSq <= 1e-18) return core::length(core::Vec2(p.x - a.x, p.y - a.y));
    double t = core::dot(core::Vec2(p.x - a.x, p.y - a.y), ab) / lenSq;
    t = clampDouble(t, 0.0, 1.0);
    const core::Vec2 proj(a.x + t * ab.x, a.y + t * ab.y);
    return core::length(core::Vec2(p.x - proj.x, p.y - proj.y));
}

double clampDouble(double value, double lo, double hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

} // namespace experimental
} // namespace r62
