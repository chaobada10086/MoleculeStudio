// =============================================================================
// re_round62_viewer/next/rings_chains/fuse.cpp
//
// T08 - fusion legality, the cluster representation, and the ONE creation
//       command that every ring / chain / sprout reduces to.
//
// WHY ONE COMMAND FOR EVERYTHING
// A ring, a chain and a T06 sprout are all the same shape of edit: create some
// atoms, create some bonds, and REUSE some existing atoms and bonds. Writing
// three creation paths is how three subtly different duplicate-edge bugs appear.
// They are all lowered to a Cluster and committed by createClusterCommand(), so:
//
//   * duplicate edges are checked in exactly one place,
//   * self bonds are checked in exactly one place,
//   * the whole edit is one core::Transaction and therefore one Undo step,
//   * a failure inside the cluster leaves the document EXACTLY as it was
//     (core::Transaction rolls back from its own captured baseline, so it does
//      not depend on this command computing a correct inverse).
//
// ID REUSE IS THE POINT
// -----------------------------------------------------------------------------
// A fused ring reuses the shared bond's two atom ids and the shared bond id. It
// does NOT delete them and recreate them, and it does NOT create a second edge
// between the same pair. The tests assert the exact id sets.
//
// VALENCE IS T06's, NOT A SECOND TABLE HERE
// -----------------------------------------------------------------------------
// valenceAfterJoin() builds a chemistry::AtomEnvironment from the document and
// calls chemistry::implicitHydrogens(). This module contains NO valence numbers.
// Where T06 says the rule does not apply (a metal, a noble gas, an arbitrary
// label) the answer is treated as VALID, because T06 explicitly does not
// predict those elements - refusing on a rule that does not apply would be a
// fabricated chemistry error.
// =============================================================================

#include "next/rings_chains/internal.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace r62 {
namespace rings_chains {

const char* fuseRejectName(FuseReject r) {
    switch (r) {
        case FuseReject::None:             return "none";
        case FuseReject::AnchorMissing:    return "anchor-missing";
        case FuseReject::OverValence:      return "over-valence";
        case FuseReject::DuplicateEdge:    return "duplicate-edge";
        case FuseReject::SelfBond:         return "self-bond";
        case FuseReject::NoGapForSpiro:    return "no-gap-for-spiro";
        case FuseReject::DocumentMismatch: return "document-mismatch";
        case FuseReject::Degenerate:       return "degenerate";
        case FuseReject::PlacementInvalid: return "placement-invalid";
        case FuseReject::NotPlaced:        return "not-placed";
    }
    return "none";
}

int incidentBondOrderSum(const core::Document& document, core::ObjectId atom) {
    int sum = 0;
    const std::vector<core::ObjectId>& incident = document.incidentBonds(atom);
    for (size_t i = 0; i < incident.size(); ++i) {
        const core::BondRecord* b = document.bonds().alive(incident[i]);
        if (!b) continue;
        // Aromatic and query orders count as 1 here, exactly as T06's
        // AtomEnvironment documents. Deriving this differently in two places is
        // how a hydrogen count and a valence check start disagreeing.
        int o = 1;
        switch (b->order) {
            case core::BondOrder::Single:   o = 1; break;
            case core::BondOrder::Double:   o = 2; break;
            case core::BondOrder::Triple:   o = 3; break;
            case core::BondOrder::Quadruple:o = 4; break;
            default: o = 1; break;
        }
        sum += o;
    }
    return sum;
}

chemistry::HydrogenResult valenceAfterJoin(const core::Document& document,
                                           core::ObjectId atom,
                                           int extraOrder) {
    chemistry::AtomEnvironment env;
    const core::AtomRecord* a = document.atoms().alive(atom);
    if (!a) {
        chemistry::HydrogenResult bad;
        bad.valid = false;
        bad.note = "atom is not alive";
        return bad;
    }
    env.element = a->element;
    env.charge = a->semantics.charge;
    env.radical = a->semantics.radical;
    env.isotope = a->semantics.isotope;
    env.explicitHydrogens = a->semantics.implicitHydrogens;
    env.hasLabelOverride = a->semantics.nodeType != static_cast<uint8_t>(core::NodeType::Element);
    env.bondOrderSum = incidentBondOrderSum(document, atom) + extraOrder;
    const std::vector<core::ObjectId>& incident = document.incidentBonds(atom);
    int count = 0;
    int aromatic = 0;
    for (size_t i = 0; i < incident.size(); ++i) {
        const core::BondRecord* b = document.bonds().alive(incident[i]);
        if (!b) continue;
        ++count;
        if (b->semantics == core::BondSemantics::Aromatic ||
            b->order == core::BondOrder::Aromatic) ++aromatic;
    }
    env.bondCount = count + (extraOrder > 0 ? 1 : 0);
    env.aromaticBondCount = aromatic;
    return chemistry::implicitHydrogens(env);
}

namespace detail {

// A cheap fingerprint so a placement built for one document shape cannot be
// committed onto a different one. Exact coordinates, not a tolerance: an exact
// comparison cannot be fooled by a warm-up rounding difference.
bool samePositionSet(const std::vector<core::Vec2>& a, const std::vector<core::Vec2>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
    return true;
}

} // namespace detail

namespace commands {

// -----------------------------------------------------------------------------
// validateCluster - pure, no mutation
// -----------------------------------------------------------------------------
FuseReject validateCluster(const core::Document& document, const commands::Cluster& cluster,
                           std::string* detail) {
    if (detail) detail->clear();

    if (cluster.documentId != core::kInvalidDocumentId &&
        cluster.documentId != document.id()) {
        if (detail) *detail = "cluster was resolved against a different document";
        return FuseReject::DocumentMismatch;
    }

    // FRESHNESS. A cluster records the document REVISION it was resolved at, and
    // a commit refuses when the document has moved on. This is the "preview ==
    // commit" contract applied to placement: a placement that survived an undo,
    // a redo or any other edit describes a document shape that no longer exists,
    // and committing it would silently overwrite the user's work. A cluster built
    // with revision 0 is treated as "no freshness claim" (used by callers that
    // build a cluster by hand for a single immediate commit).
    if (cluster.documentRevision != 0 &&
        cluster.documentRevision != document.revision()) {
        if (detail) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "the placement was resolved at revision %llu but the "
                          "document is at %llu",
                          static_cast<unsigned long long>(cluster.documentRevision),
                          static_cast<unsigned long long>(document.revision()));
            *detail = buf;
        }
        return FuseReject::NotPlaced;
    }

    const size_t n = cluster.atoms.size();
    if (n == 0) {
        if (detail) *detail = "cluster has no atoms";
        return FuseReject::Degenerate;
    }

    // 1. Every atom is either reusable (an alive atom of this document) or a
    //    new atom with a finite coordinate.
    for (size_t i = 0; i < n; ++i) {
        const commands::ClusterAtom& ca = cluster.atoms[i];
        if (ca.reuse != core::kInvalidObjectId) {
            if (!document.atoms().alive(ca.reuse)) {
                if (detail) *detail = "reused atom id is not alive in this document";
                return FuseReject::AnchorMissing;
            }
            continue;
        }
        if (!ca.position.finite()) {
            if (detail) *detail = "new atom has a non-finite position";
            return FuseReject::Degenerate;
        }
        if (!core::validElement(ca.element)) {
            if (detail) *detail = "new atom has an out-of-range element";
            return FuseReject::Degenerate;
        }
    }

    // 2. Two NEW atoms at exactly the same position would be indistinguishable
    //    in the drawing and both would exist. Refuse rather than create them.
    {
        std::map<std::pair<double, double>, int> seen;
        for (size_t i = 0; i < n; ++i) {
            if (cluster.atoms[i].reuse != core::kInvalidObjectId) continue;
            std::pair<double, double> key(cluster.atoms[i].position.x,
                                          cluster.atoms[i].position.y);
            std::map<std::pair<double, double>, int>::iterator it = seen.find(key);
            if (it != seen.end()) {
                if (detail) *detail = "two new atoms would occupy the same point";
                return FuseReject::PlacementInvalid;
            }
            seen[key] = static_cast<int>(i);
        }
    }

    // 3. Edges: no self bond, no duplicate edge (whether the duplicate is
    //    inside the cluster or already in the document), and a reuse id that is
    //    genuinely the edge between the two resolved atoms.
    std::set<std::pair<uint32_t, uint32_t> > edges;
    for (size_t e = 0; e < cluster.bonds.size(); ++e) {
        const commands::ClusterBond& cb = cluster.bonds[e];
        if (cb.fromAtom < 0 || cb.toAtom < 0 ||
            static_cast<size_t>(cb.fromAtom) >= n ||
            static_cast<size_t>(cb.toAtom) >= n) {
            if (detail) *detail = "bond references an atom index outside the cluster";
            return FuseReject::Degenerate;
        }
        if (cb.fromAtom == cb.toAtom) {
            if (detail) *detail = "cluster bond is a self bond";
            return FuseReject::SelfBond;
        }
        // Resolve the endpoint identities. A new atom is identified by its
        // cluster index NEGATED, so it cannot collide with a real document id.
        const uint32_t kA = (cluster.atoms[static_cast<size_t>(cb.fromAtom)].reuse !=
                             core::kInvalidObjectId)
            ? cluster.atoms[static_cast<size_t>(cb.fromAtom)].reuse
            : (0x80000000u | static_cast<uint32_t>(cb.fromAtom));
        const uint32_t kB = (cluster.atoms[static_cast<size_t>(cb.toAtom)].reuse !=
                             core::kInvalidObjectId)
            ? cluster.atoms[static_cast<size_t>(cb.toAtom)].reuse
            : (0x80000000u | static_cast<uint32_t>(cb.toAtom));
        if (kA == kB) {
            if (detail) *detail = "cluster bond resolves to a self bond";
            return FuseReject::SelfBond;
        }
        const std::pair<uint32_t, uint32_t> key = (kA < kB) ? std::make_pair(kA, kB)
                                                            : std::make_pair(kB, kA);
        if (edges.find(key) != edges.end()) {
            if (detail) *detail = "the cluster contains two copies of one edge";
            return FuseReject::DuplicateEdge;
        }
        edges.insert(key);

        // If BOTH endpoints are existing atoms, the edge must not already exist
        // in the document - unless this cluster bond IS that existing bond.
        const core::ObjectId ra = cluster.atoms[static_cast<size_t>(cb.fromAtom)].reuse;
        const core::ObjectId rb = cluster.atoms[static_cast<size_t>(cb.toAtom)].reuse;
        if (ra != core::kInvalidObjectId && rb != core::kInvalidObjectId) {
            const core::ObjectId existing = document.bondBetween(ra, rb);
            if (existing != core::kInvalidObjectId) {
                if (cb.reuse != existing) {
                    if (detail)
                        *detail = "an edge between these two atoms already exists and "
                                  "this cluster bond does not reuse it";
                    return FuseReject::DuplicateEdge;
                }
            } else if (cb.reuse != core::kInvalidObjectId) {
                if (detail) *detail = "reused bond id does not exist between these atoms";
                return FuseReject::DuplicateEdge;
            }
        } else if (cb.reuse != core::kInvalidObjectId) {
            if (!document.bonds().alive(cb.reuse)) {
                if (detail) *detail = "reused bond id is not alive in this document";
                return FuseReject::DuplicateEdge;
            }
        }
    }

    // 4. Valence, answered by T06 and by nothing else here.
    //    Count how much extra bond order each existing atom is taking from the
    //    cluster bonds that are going to be CREATED (a reused bond adds
    //    nothing: it already exists and is already counted).
    std::map<core::ObjectId, int> extraOrder;
    for (size_t e = 0; e < cluster.bonds.size(); ++e) {
        const commands::ClusterBond& cb = cluster.bonds[e];
        if (cb.reuse != core::kInvalidObjectId) continue;
        int o = 1;
        switch (cb.order) {
            case core::BondOrder::Single:    o = 1; break;
            case core::BondOrder::Double:    o = 2; break;
            case core::BondOrder::Triple:    o = 3; break;
            case core::BondOrder::Quadruple: o = 4; break;
            default:                         o = 1; break;
        }
        const core::ObjectId ra = cluster.atoms[static_cast<size_t>(cb.fromAtom)].reuse;
        const core::ObjectId rb = cluster.atoms[static_cast<size_t>(cb.toAtom)].reuse;
        if (ra != core::kInvalidObjectId) extraOrder[ra] += o;
        if (rb != core::kInvalidObjectId) extraOrder[rb] += o;
    }
    // Also account for bonds being created between two NEW atoms only: the new
    // atoms have no valence history, so they are written with the conventional
    // count and are not checked against a table this module does not own.
    for (std::map<core::ObjectId, int>::const_iterator it = extraOrder.begin();
         it != extraOrder.end(); ++it) {
        const chemistry::HydrogenResult hr =
            valenceAfterJoin(document, it->first, it->second);
        if (hr.valid && hr.overCoordinated) {
            if (detail) {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "atom %u would exceed its conventional valence "
                              "(T06 reports over-coordination; degree=%u orderSum=%d extra=%d)",
                              static_cast<unsigned>(it->first),
                              static_cast<unsigned>(document.incidentBonds(it->first).size()),
                              incidentBondOrderSum(document, it->first), it->second);
                *detail = buf;
            }
            return FuseReject::OverValence;
        }
    }

    return FuseReject::None;
}

// -----------------------------------------------------------------------------
// Lowering: RingPlacement / ChainPlan -> Cluster
// -----------------------------------------------------------------------------
commands::Cluster clusterFromRingPlacement(const RingPlacement& p) {
    commands::Cluster c;
    c.documentId = p.documentId;
    c.documentRevision = p.documentRevision;
    c.tag = "ring";
    const size_t n = p.geometry.vertices.size();
    c.atoms.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        commands::ClusterAtom ca;
        ca.position = p.worldPosition[i];
        ca.element = p.geometry.vertices[i].element;
        ca.reuse = p.reuseAtom[i];
        ca.noDouble = i < p.noDouble.size() && p.noDouble[i] != 0;
        c.atoms.push_back(ca);
    }
    for (size_t e = 0; e < p.geometry.bonds.size(); ++e) {
        commands::ClusterBond cb;
        cb.fromAtom = p.geometry.bonds[e].from;
        cb.toAtom = p.geometry.bonds[e].to;
        cb.reuse = p.reuseBond[e];
        cb.order = p.geometry.bonds[e].order;
        cb.display = p.geometry.bonds[e].display;
        // A fused benzene shares edge 0 with the existing ring. Copying the
        // preset's alternating pattern verbatim puts a double bond on edge 1
        // or 5, which lands on an already three-connected fusion atom and is
        // correctly rejected as over-valence. Naphthalene needs a Kekule
        // pattern across the fused system: keep both bonds adjacent to the
        // shared edge single, then alternate doubles on edges 2/4. The shared
        // edge itself is reused and keeps its existing order.
        const bool fused = !p.reuseBond.empty() &&
                           p.reuseBond[0] != core::kInvalidObjectId;
        if (fused && p.preset == RingPresetId::Benzene) {
            if (e == 1 || e == 5 || e == 3) cb.order = core::BondOrder::Single;
            else if (e == 2 || e == 4) cb.order = core::BondOrder::Double;
        }
        c.bonds.push_back(cb);
    }
    return c;
}

commands::Cluster clusterFromChainPlan(const ChainPlan& plan) {
    commands::Cluster c;
    c.documentId = plan.documentId;
    c.documentRevision = plan.documentRevision;
    c.tag = "chain";
    // Atom 0 is the ORIGIN. When the origin is an existing atom it is a reused
    // cluster atom, so bonds[0] has a real endpoint on both sides and the
    // cluster stays uniform.
    {
        commands::ClusterAtom origin;
        origin.position = plan.origin;
        origin.element = 6;
        origin.reuse = plan.originAtom;
        c.atoms.push_back(origin);
    }
    for (size_t i = 0; i < plan.atoms.size(); ++i) {
        commands::ClusterAtom ca;
        ca.position = plan.atoms[i].position;
        ca.element = plan.atoms[i].element;
        c.atoms.push_back(ca);
    }
    for (size_t i = 0; i < plan.bonds.size(); ++i) {
        commands::ClusterBond cb;
        cb.fromAtom = (plan.bonds[i].fromNewIndex < 0)
            ? 0
            : (plan.bonds[i].fromNewIndex + 1);
        cb.toAtom = plan.bonds[i].toNewIndex + 1;
        cb.reuse = core::kInvalidObjectId;
        cb.order = plan.bonds[i].order;
        cb.display = core::BondDisplay::Normal;
        c.bonds.push_back(cb);
    }
    return c;
}

} // namespace commands
} // namespace rings_chains
} // namespace r62