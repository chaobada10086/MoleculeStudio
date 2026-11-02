// =============================================================================
// re_round62_viewer/next/rings_chains/place.cpp
//
// T08 - the FIVE PLACEMENT RULES, implemented as a pure read-only resolver.
//
// NOTHING in this file mutates a document. resolveRingPlacement() only reads
// topology (incident bonds, bondBetween) and produces a RingPlacement value.
// That is what makes "a cancelled preview does not change the document" a
// structural property: the preview path and the commit path call the SAME
// resolver, and only the commit path hands the result to a core::Command.
//
// -----------------------------------------------------------------------------
// RULE 1  EMPTY CANVAS
// -----------------------------------------------------------------------------
// The pointer is the ring CENTROID. Every vertex is new, and the default
// rotation puts ring vertex 0 to the RIGHT of the centroid.
//
// -----------------------------------------------------------------------------
// RULE 2  GROW FROM AN ATOM
// -----------------------------------------------------------------------------
// The ring shares EXACTLY ONE atom: the anchor, which IS ring vertex 0. The
// anchor's existing neighbours are measured and, when the caller gave no
// rotation, the ring body is grown into the WIDEST free angular gap around the
// anchor.
//
// -----------------------------------------------------------------------------
// RULE 3  FUSION ON A BOND
// -----------------------------------------------------------------------------
// The ring shares EXACTLY ONE EDGE. The shared edge is ring edge 0, so ring
// vertices 0 and 1 ARE the bond's begin and end atoms and the hot bond id is
// REUSED. The ring is laid out on the side of the bond AWAY from the existing
// ring body. Every other ring edge whose endpoints already have a bond is also
// reused, so no edge is ever duplicated.
//
// -----------------------------------------------------------------------------
// RULE 4  SPIRO
// -----------------------------------------------------------------------------
// Shares one atom like rule 2, but the rotation is forced into the widest gap of
// the anchor's existing bond directions. An anchor with 4+ in-plane neighbours
// and no usable gap is REFUSED: drawing a ring on top of four bonds produces a
// picture that is chemically wrong and the user cannot tell from the image.
//
// -----------------------------------------------------------------------------
// RULE 5  PREVIEW == COMMIT
// -----------------------------------------------------------------------------
// RingPlacement records the document id and revision it was resolved against.
// createRingCommand() re-validates both, so a placement that survived an
// undo/open cannot be committed onto a different document shape.
// =============================================================================

#include "next/rings_chains/internal.hpp"

#include <algorithm>
#include <cmath>

namespace r62 {
namespace rings_chains {
namespace detail {

double normalizeDeg(double d) {
    if (!std::isfinite(d)) return 0.0;
    double r = std::fmod(d, 360.0);
    if (r < 0.0) r += 360.0;
    return r;
}

double angleGapDeg(double a, double b) {
    double d = std::fabs(normalizeDeg(a) - normalizeDeg(b));
    if (d > 180.0) d = 360.0 - d;
    return d;
}

bool placementIsFresh(const core::Document& document,
                      core::DocumentId placementDocumentId,
                      uint64_t placementRevision) {
    if (placementDocumentId == core::kInvalidDocumentId) return false;
    if (document.id() != placementDocumentId) return false;
    return document.revision() == placementRevision;
}

double resolvedBondLength(const core::Document& document, double requested) {
    if (requested > 0.0 && std::isfinite(requested)) return requested;
    const double d = document.defaultBondLength();
    return (d > 0.0 && std::isfinite(d)) ? d : 14.4;
}

namespace {

const double kPi = 3.14159265358979323846;

struct Neighbour {
    core::ObjectId atom;
    double         directionDeg;
};

std::vector<Neighbour> neighboursOf(const core::Document& doc,
                                    core::ObjectId atom,
                                    const core::Vec2& position) {
    std::vector<Neighbour> out;
    if (!doc.atoms().alive(atom)) return out;
    const std::vector<core::ObjectId>& incident = doc.incidentBonds(atom);
    for (size_t i = 0; i < incident.size(); ++i) {
        const core::BondRecord* b = doc.bonds().alive(incident[i]);
        if (!b) continue;
        const core::ObjectId other = (b->beginAtom == atom) ? b->endAtom : b->beginAtom;
        if (other == atom) continue;
        const core::AtomRecord* n = doc.atoms().alive(other);
        if (!n) continue;
        Neighbour nb;
        nb.atom = other;
        nb.directionDeg = core::angleOf(n->position - position);
        out.push_back(nb);
    }
    return out;
}

void sortDirections(std::vector<double>* dirs) {
    for (size_t i = 0; i < dirs->size(); ++i) (*dirs)[i] = normalizeDeg((*dirs)[i]);
    std::sort(dirs->begin(), dirs->end());
}

double widestGapCentre(const std::vector<double>& sortedDirs,
                       double fallbackDeg, double* gapDeg) {
    if (sortedDirs.empty()) {
        if (gapDeg) *gapDeg = 360.0;
        return normalizeDeg(fallbackDeg);
    }
    if (sortedDirs.size() == 1) {
        if (gapDeg) *gapDeg = 360.0;
        return normalizeDeg(sortedDirs[0] + 180.0);
    }
    double bestGap = -1.0;
    double bestCentre = normalizeDeg(fallbackDeg);
    for (size_t i = 0; i < sortedDirs.size(); ++i) {
        const double a = sortedDirs[i];
        const double b = sortedDirs[(i + 1) % sortedDirs.size()];
        double gap = normalizeDeg(b - a);
        if (gap <= 0.0) gap += 360.0;
        if (gap > bestGap) {
            bestGap = gap;
            bestCentre = normalizeDeg(a + gap * 0.5);
        }
    }
    if (gapDeg) *gapDeg = bestGap;
    return bestCentre;
}

// Given the centre G of the widest free angular gap around the anchor, return the
// LOCAL-GEOMETRY rotation that grows the ring body into that gap.
//
// The local geometry places vertex 0 at angle 0 on the circumcircle and the
// centroid at the local origin. After rotating by rot, vertex 0 sits at
// anchor + R * direction(rot). resolveRingPlacement then TRANSLATES the shape so
// that vertex 0 coincides with the anchor, which puts the centroid at
// anchor - R * direction(rot) and the BODY on the far side of the centroid from
// vertex 0, i.e. along direction(rot + 180).
//
// So the body bisector is direction(rot + 180), and aiming it at the gap centre
// G requires rot = G + 180.
double growRotation(const std::vector<double>& neighbourDirs, int size,
                    double fallbackDeg, double* gapDeg) {
    (void)size;
    const double centre = widestGapCentre(neighbourDirs, fallbackDeg, gapDeg);
    return normalizeDeg(centre + 180.0);
}

} // namespace

} // namespace detail

namespace detail {

core::Error resolveRingPlacementImpl(const core::Document& document,
                                     const RingRequest& request,
                                     RingPlacement* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");

    const RingPresetSpec* spec = ringPresetById(request.preset);
    if (!spec) {
        out->valid = false;
        out->rejectReason = "unknown ring preset";
        return core::Error(core::ErrorCode::OutOfRange, "unknown ring preset");
    }

    const double bondLength = resolvedBondLength(document, request.bondLength);
    RingGeometry geometry;
    core::Error built = buildRingGeometry(request.preset, bondLength, &geometry);
    if (!built.ok()) { out->valid = false; out->rejectReason = built.message; return built; }

    RingPlacement p;
    p.preset = request.preset;
    p.geometry = geometry;
    p.documentId = document.id();
    p.documentRevision = document.revision();
    p.reuseAtom.assign(static_cast<size_t>(geometry.size), core::kInvalidObjectId);
    p.reuseBond.assign(geometry.bonds.size(), core::kInvalidObjectId);

    double rotation = request.rotationGiven ? request.rotationDeg : 0.0;
    bool mirroredLocal_ = false;

    if (request.mode == RingAttachMode::GrowFromAtom ||
        request.mode == RingAttachMode::Spiro) {
        const core::AtomRecord* anchor = document.atoms().alive(request.anchorAtom);
        if (!anchor) {
            p.valid = false;
            p.rejectReason = "anchor atom does not exist or is deleted";
            *out = p;
            return core::Error(core::ErrorCode::NotFound, p.rejectReason);
        }
        const std::vector<Neighbour> nbs =
            neighboursOf(document, request.anchorAtom, anchor->position);
        std::vector<double> dirs;
        dirs.reserve(nbs.size());
        for (size_t i = 0; i < nbs.size(); ++i) dirs.push_back(nbs[i].directionDeg);
        sortDirections(&dirs);

        double gap = 0.0;
        if (request.rotationGiven) {
            rotation = request.rotationDeg;
        } else {
            const double fallback = core::angleOf(request.pointer - anchor->position);
            rotation = growRotation(dirs, geometry.size, fallback, &gap);
        }
        if (request.mode == RingAttachMode::Spiro) {
            // A spiro ring occupies an angular APERTURE at the anchor equal to the
            // angle between its two edges there, which for a regular n-gon is
            // 180 - 360/n degrees (the interior angle at vertex 0). The ring can
            // only be placed honestly if some free gap between the anchor's
            // existing bonds is at least that aperture.
            //
            // This is a geometric requirement with a derivation, not a tuned
            // constant: a 6-ring needs 120 degrees of free space, a 5-ring 108,
            // a 3-ring 60. A test asserts the boundary by building an atom whose
            // free gap is just under and just over the aperture.
            const double aperture =
                180.0 - 360.0 / static_cast<double>(geometry.size);
            double gapForRing = gap;
            if (dirs.empty()) gapForRing = 360.0;
            else if (dirs.size() == 1) gapForRing = 360.0;
            if (dirs.size() < 4 && gapForRing < aperture) {
                // With fewer than four neighbours the widest gap is measured the
                // long way round; only a genuinely crowded atom can fail here.
                p.valid = false;
                p.rejectReason = "no angular gap wide enough for a spiro ring at "
                                 "this atom";
                *out = p;
                return core::Error(core::ErrorCode::InvalidGeometry, p.rejectReason);
            }
            if (dirs.size() >= 4 && gapForRing < aperture) {
                p.valid = false;
                p.rejectReason = "no angular gap wide enough for a spiro ring at "
                                 "this atom (the largest free gap is smaller than "
                                 "the ring interior angle)";
                *out = p;
                return core::Error(core::ErrorCode::InvalidGeometry, p.rejectReason);
            }
        }
        // VERTEX 0 MUST LAND ON THE ANCHOR. The local geometry puts vertex 0 at
        // radius R along angle 0, so the placement origin (the ring CENTROID) is
        // the anchor MINUS that rotated offset. Getting this wrong places the
        // ring one radius away from the atom it is supposed to grow from, which
        // is why the earlier version produced ring bonds of the wrong length.
        p.rotationDeg = normalizeDeg(rotation);
        p.origin = anchor->position -
                   detail::rotateLocal(geometry.vertices[0].local, p.rotationDeg);
        p.reuseAtom[0] = request.anchorAtom;
    } else if (request.mode == RingAttachMode::FuseOnPath) {
        // Peri/bay fusion: reuse the path A-B-C (two adjacent edges).  B is the
        // central fusion carbon; A and C are its two neighbours on the rim.  A
        // regular hexagon through A,B,C adds D,E,F. The shared bridgehead B
        // stays saturated; the twelve perimeter atoms receive six double bonds.
        const core::AtomRecord* b = document.atoms().alive(request.anchorAtom);
        const core::BondRecord* ab = document.bonds().alive(request.anchorBond);
        const core::BondRecord* cb = document.bonds().alive(request.anchorBond2);
        if (!b || !ab || !cb) {
            p.valid = false; p.rejectReason = "peri fusion path is missing";
            *out = p; return core::Error(core::ErrorCode::NotFound, p.rejectReason);
        }
        core::ObjectId aid = ab->beginAtom == b->id ? ab->endAtom : ab->beginAtom;
        core::ObjectId cid = cb->beginAtom == b->id ? cb->endAtom : cb->beginAtom;
        const core::AtomRecord* a = document.atoms().alive(aid);
        const core::AtomRecord* c = document.atoms().alive(cid);
        if (!a || !c || aid == cid) {
            p.valid = false; p.rejectReason = "peri fusion path endpoints are invalid";
            *out = p; return core::Error(core::ErrorCode::InvalidGeometry, p.rejectReason);
        }
        const double la = core::length(a->position - b->position);
        const double lc = core::length(c->position - b->position);
        if (!(la > 1e-6) || !(lc > 1e-6)) {
            p.valid = false; p.rejectReason = "peri fusion path has a zero edge";
            *out = p; return core::Error(core::ErrorCode::InvalidGeometry, p.rejectReason);
        }
        const core::Vec2 o = a->position + c->position - b->position;
        const core::Vec2 v = a->position - o;
        auto rotate = [](core::Vec2 q, double degrees) {
            const double r = degrees * 3.14159265358979323846 / 180.0;
            const double cs = std::cos(r), sn = std::sin(r);
            return core::Vec2(q.x * cs - q.y * sn, q.x * sn + q.y * cs);
        };
        const core::Vec2 vb=b->position-o;
        const double turn=(v.x*vb.y-v.y*vb.x)>0 ? -60.0 : 60.0;
        const core::Vec2 d = o + rotate(v, turn);
        const core::Vec2 e = o + rotate(v, 2*turn);
        const core::Vec2 f = o + rotate(v, 3*turn);
        RingGeometry g;
        g.preset = request.preset; g.topology = RingTopologyClass::Aromatic; g.size = 6;
        g.circumRadius = core::length(v); g.bondLength = la; g.planar = true;
        g.vertices.resize(6);
        g.vertices[0].local = a->position - o;
        g.vertices[1].local = b->position - o;
        g.vertices[2].local = c->position - o;
        g.vertices[3].local = d - o;
        g.vertices[4].local = e - o;
        g.vertices[5].local = f - o;
        // Hexagon order is C-B-A-D-E-F-C: the two shared edges are A-B and
        // B-C, so the perimeter must close through C-F and D-A rather than a
        // naive 0-1-2-3-4-5 walk.
        const int ringEdges[6][2] = {{0,1},{1,2},{2,5},{5,4},{4,3},{3,0}};
        for (int k = 0; k < 6; ++k) {
            RingBondSpec bond;
            bond.from = ringEdges[k][0]; bond.to = ringEdges[k][1];
            bond.order = core::BondOrder::Single; bond.display = core::BondDisplay::Normal;
            bond.cyclicRow = k & 1;
            g.bonds.push_back(bond);
        }
        geometry = g; // The common placement loop must use this path geometry.
        p.geometry = g;
        p.origin = o; p.rotationDeg = 0.0;
        p.reuseAtom.assign(6, core::kInvalidObjectId);
        p.reuseBond.assign(6, core::kInvalidObjectId);
        p.noDouble.assign(6, 0);
        p.reuseAtom[0] = aid; p.reuseAtom[1] = b->id; p.reuseAtom[2] = cid;
        p.reuseBond[0] = request.anchorBond; p.reuseBond[1] = request.anchorBond2;
        p.noDouble[1] = 1;
        p.worldPosition.assign(6, core::Vec2(0, 0));
        p.worldPosition[0] = a->position; p.worldPosition[1] = b->position; p.worldPosition[2] = c->position;
        p.worldPosition[3] = d; p.worldPosition[4] = e; p.worldPosition[5] = f;
    } else if (request.mode == RingAttachMode::FuseOnBond) {
        const core::BondRecord* hot = document.bonds().alive(request.anchorBond);
        if (!hot) {
            p.valid = false;
            p.rejectReason = "fusion anchor bond does not exist or is deleted";
            *out = p;
            return core::Error(core::ErrorCode::NotFound, p.rejectReason);
        }
        const core::AtomRecord* a0 = document.atoms().alive(hot->beginAtom);
        const core::AtomRecord* a1 = document.atoms().alive(hot->endAtom);
        if (!a0 || !a1) {
            p.valid = false;
            p.rejectReason = "fusion anchor bond references a dead atom";
            *out = p;
            return core::Error(core::ErrorCode::NotAlive, p.rejectReason);
        }

        const core::Vec2 mid = (a0->position + a1->position) * 0.5;
        // The shared edge has its OWN stored length, which need not equal the
        // document bond length. A fused ring whose edges are all the requested
        // length while its shared edge is a different one is not a regular
        // polygon, so the ring is rebuilt for the SHARED length. That keeps every
        // edge equal, honours the existing bond exactly, and never moves the two
        // shared atoms.
        const double sharedLength = core::length(a1->position - a0->position);
        const double effectiveLength =
            (sharedLength > 0.0 && std::isfinite(sharedLength)) ? sharedLength : bondLength;
        {
            RingGeometry scaled;
            if (buildRingGeometry(request.preset, effectiveLength, &scaled).ok()) {
                geometry = scaled;
                p.geometry = scaled;
            }
        }
        const double axisDeg = core::angleOf(a1->position - a0->position);
        const double perpDeg = normalizeDeg(axisDeg + 90.0);
        const core::Vec2 perp = core::direction(perpDeg);
        const core::Vec2 mid2 = (a0->position + a1->position) * 0.5;

        double sideSum = 0.0;
        int    sideCount = 0;
        {
            const core::ObjectId shared[2] = { hot->beginAtom, hot->endAtom };
            for (int s = 0; s < 2; ++s) {
                const core::Vec2 base = (s == 0) ? a0->position : a1->position;
                const std::vector<Neighbour> nbs =
                    neighboursOf(document, shared[s], base);
                for (size_t i = 0; i < nbs.size(); ++i) {
                    if (nbs[i].atom == shared[0] || nbs[i].atom == shared[1]) continue;
                    const core::AtomRecord* n = document.atoms().alive(nbs[i].atom);
                    if (!n) continue;
                    sideSum += core::dot(n->position - base, perp);
                    ++sideCount;
                }
            }
        }
        double sideSign = 1.0;
        if (sideCount > 0) {
            sideSign = (sideSum >= 0.0) ? -1.0 : 1.0;
        } else {
            if (core::dot(request.pointer - mid, perp) < 0.0) sideSign = -1.0;
        }

        // The rotation must map the LOCAL edge 0->1 exactly onto the STORED
        // a0->a1 vector. The local ring places vertex i at angle i*360/size, so
        // the local edge direction is angleOf(local1 - local0); solving
        //     rotate(local1 - local0) == a1 - a0
        // gives the rotation below. Both vectors have the bond length, so a pure
        // rotation always exists and the two shared atoms keep their EXACT
        // coordinates. Using the vertex-0-to-centroid direction instead (which is
        // what the first version did) rotates the pattern by one half-step, which
        // is why the fused ring folded back over the shared bond.
        // THE FUSION FRAME IS FULLY DETERMINED by two constraints:
        //   W_0 == a0  and  W_1 == a1  (the stored coordinates, untouched).
        // With local vertices L_i = R*(cos(i*360/n), -sin(i*360/n)):
        //   rotate(L_1 - L_0, rot) == a1 - a0   ->  rot follows directly
        //   origin = a0 - rotate(L_0, rot)
        // There is NO free parameter left, so the SIDE cannot be chosen by
        // tweaking the rotation. The side is chosen by reflecting the polygon:
        // using L'_i = (L_i.x, -L_i.y) makes the reflected edge angle the
        // negative of the original, i.e. rot' = 2*axis - rot. The two candidate
        // frames are built and the one whose centroid lies on the required side
        // wins. When the requested side is unreachable for a given ring size the
        // original frame is kept, because a fused ring on the wrong side is still
        // a correct ring, whereas moving the shared atoms would not be.
        {
            const core::Vec2 localEdge =
                geometry.vertices[1].local - geometry.vertices[0].local;
            const core::Vec2 worldEdge = a1->position - a0->position;
            const double axisOfBond = core::angleOf(worldEdge);
            const double rotA = normalizeDeg(axisOfBond - core::angleOf(localEdge));
            const double rotB = normalizeDeg(axisOfBond + core::angleOf(localEdge));

            // Candidate A: the polygon as generated.
            const core::Vec2 originA =
                a0->position - detail::rotateLocal(geometry.vertices[0].local, rotA);
            // Candidate B: the polygon reflected in x (same ring, other side).
            core::Vec2 mirror0(geometry.vertices[0].local.x, -geometry.vertices[0].local.y);
            const core::Vec2 originB = a0->position - detail::rotateLocal(mirror0, rotB);

            const double sideA = core::dot(originA - mid2, perp) * sideSign;
            const double sideB = core::dot(originB - mid2, perp) * sideSign;

            if (sideA >= sideB) {
                rotation = rotA;
                p.rotationDeg = rotation;
                p.origin = originA;
            } else {
                rotation = rotB;
                p.rotationDeg = rotation;
                p.origin = originB;
                mirroredLocal_ = true;
            }
        }

        p.reuseAtom[0] = hot->beginAtom;
        p.reuseAtom[1] = hot->endAtom;
        p.reuseBond[0] = request.anchorBond;

        p.worldPosition.assign(static_cast<size_t>(geometry.size), core::Vec2(0, 0));
        p.worldPosition[0] = a0->position;
        p.worldPosition[1] = a1->position;
    } else {
        // Empty-canvas placement: unless the caller supplied an explicit
        // rotation (Alt-drag), a chair gets its preset's built-in orientation so
        // the two chair presets read differently. Other rings stay at 0, except
        // benzene: its default is turned by 30 degrees so one vertex points
        // straight up. The sign is -30 (330): the hexagon is identical either
        // way, but this sign puts the Kekule double bonds on the top-right,
        // bottom-right and left-vertical edges, which is the layout produced by
        // rotating a default ring by 30 degrees in the editor. Attachment modes
        // still derive their rotation from the anchor/bond, so growing or fusing
        // a ring is unchanged.
        if (spec->topology == RingTopologyClass::Chair && !request.rotationGiven)
            rotation = chairOrientationDegrees(request.preset);
        else if (!request.rotationGiven && spec->id == RingPresetId::Benzene)
            rotation = -30.0;
        p.origin = request.pointer;
        p.rotationDeg = normalizeDeg(rotation);
    }

    if (p.worldPosition.size() != static_cast<size_t>(geometry.size))
        p.worldPosition.assign(static_cast<size_t>(geometry.size), core::Vec2(0, 0));
    for (int i = 0; i < geometry.size; ++i) {
        if (p.reuseAtom[static_cast<size_t>(i)] != core::kInvalidObjectId) continue;
        core::Vec2 local = geometry.vertices[static_cast<size_t>(i)].local;
        if (mirroredLocal_) local = core::Vec2(local.x, -local.y);
        p.worldPosition[static_cast<size_t>(i)] =
            detail::placeLocal(local, p.rotationDeg, p.origin);
    }

    // Extend bay attachment along existing boundary vertices, retaining their
    // exact coordinates. The common edge pass also reuses their bonds.
    if (request.mode == RingAttachMode::FuseOnPath) {
        for (size_t i = 0; i < p.reuseAtom.size(); ++i) {
            if (p.reuseAtom[i] != core::kInvalidObjectId) continue;
            for (const auto& atom : document.atoms().records()) {
                if (!atom.isAlive() || core::length(atom.position-p.worldPosition[i]) >= geometry.bondLength*.08) continue;
                if (atom.element != 6 || std::find(p.reuseAtom.begin(),p.reuseAtom.end(),atom.id)!=p.reuseAtom.end()) {
                    p.valid=false; p.rejectReason="incompatible bay vertex"; *out=p;
                    return core::Error(core::ErrorCode::InvalidGeometry,p.rejectReason);
                }
                p.reuseAtom[i]=atom.id; p.worldPosition[i]=atom.position; break;
            }
        }
    }

    for (size_t e = 0; e < geometry.bonds.size(); ++e) {
        if (p.reuseBond[e] != core::kInvalidObjectId) continue;
        const size_t i0 = static_cast<size_t>(geometry.bonds[e].from);
        const size_t i1 = static_cast<size_t>(geometry.bonds[e].to);
        const core::ObjectId a = p.reuseAtom[i0];
        const core::ObjectId b = p.reuseAtom[i1];
        if (a != core::kInvalidObjectId && b != core::kInvalidObjectId) {
            if (a == b) {
                p.valid = false;
                p.rejectReason = "ring edge would be a self bond";
                *out = p;
                return core::Error(core::ErrorCode::SelfBond, p.rejectReason);
            }
            const core::ObjectId existing = document.bondBetween(a, b);
            if (existing != core::kInvalidObjectId) p.reuseBond[e] = existing;
        }
    }

    int createdAtoms = 0, reusedAtoms = 0, createdBonds = 0, reusedBonds = 0;
    for (size_t i = 0; i < p.reuseAtom.size(); ++i) {
        if (p.reuseAtom[i] == core::kInvalidObjectId) ++createdAtoms; else ++reusedAtoms;
    }
    for (size_t e = 0; e < p.reuseBond.size(); ++e) {
        if (p.reuseBond[e] == core::kInvalidObjectId) ++createdBonds; else ++reusedBonds;
    }

    int overlaps = 0;
    {
        const size_t slots = document.atoms().size();
        for (int i = 0; i < geometry.size; ++i) {
            if (p.reuseAtom[static_cast<size_t>(i)] != core::kInvalidObjectId) continue;
            const core::Vec2& wp = p.worldPosition[static_cast<size_t>(i)];
            for (size_t s = 0; s < slots; ++s) {
                const core::AtomRecord* r =
                    document.atoms().alive(static_cast<core::ObjectId>(s));
                if (!r) continue;
                if (r->position == wp) { ++overlaps; break; }
            }
        }
    }

    p.createdAtomCount = createdAtoms;
    p.createdBondCount = createdBonds;
    p.reusedAtomCount = reusedAtoms;
    p.reusedBondCount = reusedBonds;
    p.existingAtomOverlapCount = overlaps;
    p.valid = true;
    *out = p;
    return core::Error::success();
}

} // namespace detail

core::Error resolveRingPlacement(const core::Document& document,
                                 const RingRequest& request,
                                 RingPlacement* out) {
    return detail::resolveRingPlacementImpl(document, request, out);
}

RingRequest ringRequestFor(const core::Document& document,
                           RingPresetId preset,
                           const core::Vec2& pointer,
                           const PlacementPolicy& policy) {
    RingRequest r;
    r.preset = preset;
    r.pointer = pointer;
    r.bondLength = policy.bondLength;
    r.rotationDeg = policy.rotationDeg;
    r.rotationGiven = policy.rotationGiven;
    r.strictAttachment = true;

    if (policy.anchorBond != core::kInvalidObjectId &&
        policy.anchorBond2 != core::kInvalidObjectId &&
        document.bonds().alive(policy.anchorBond) &&
        document.bonds().alive(policy.anchorBond2)) {
        r.mode = RingAttachMode::FuseOnPath;
        r.anchorAtom = policy.anchorAtom;
        r.anchorBond = policy.anchorBond;
        r.anchorBond2 = policy.anchorBond2;
    } else if (policy.anchorBond != core::kInvalidObjectId &&
        document.bonds().alive(policy.anchorBond)) {
        r.mode = RingAttachMode::FuseOnBond;
        r.anchorBond = policy.anchorBond;
    } else if (policy.anchorAtom != core::kInvalidObjectId &&
               document.atoms().alive(policy.anchorAtom)) {
        r.mode = policy.spiroRequested ? RingAttachMode::Spiro
                                       : RingAttachMode::GrowFromAtom;
        r.anchorAtom = policy.anchorAtom;
    } else {
        r.mode = RingAttachMode::EmptyCanvas;
    }
    return r;
}

} // namespace rings_chains
} // namespace r62
