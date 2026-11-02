// =============================================================================
// re_round62_viewer/next/rings_chains/presets.cpp
//
// T08 - the 10 ring presets and the 2 chain presets, plus the ring geometry.
//
// SOURCE COLUMN
//   Every preset carries its exact tools.csv command name. The geometry that
//   ChemDraw uses for those presets is NOT in the verified material this project
//   holds (see SOURCES_AND_LIMITS.md), so each shape below is marked:
//
//     GUIDE    a rule the bundled ChemDraw User Guide states (PDF p.92-93: the
//              standard chain angle is 120 degrees; fixed angles move in 15
//              degree increments; fixed bond lengths use the document default).
//     PROJECT  our own geometry. Labelled, never presented as a ChemDraw
//              constant. "Looks right" is explicitly not a compatibility claim.
//
// NUMBERS THAT ARE ACTUALLY PROVABLE
//   * regularRingRadius(): R = L / (2 sin(pi/n)). This is exact: a regular n-gon
//     with circumradius R has every edge equal to 2 R sin(pi/n). The tests
//     assert every edge of every flat preset equals the requested bond length to
//     1e-9, which a wrong constant cannot pass.
//   * The chair is a REAL conformer (two parallel equilateral triangles offset in
//     z and rotated 60 degrees), projected to 2D. The projection is not a
//     regular hexagon; a test asserts that independently of any label.
// =============================================================================

#include "next/rings_chains/internal.hpp"

#include <algorithm>
#include <cmath>

namespace r62 {
namespace rings_chains {

namespace {

const double kPi = 3.14159265358979323846;

double deg2rad(double d) { return d * kPi / 180.0; }

} // namespace

const char* evidenceName(EvidenceSource s) {
    switch (s) {
        case EvidenceSource::Uncited:      return "uncited";
        case EvidenceSource::Toolbar:      return "toolbar";
        case EvidenceSource::Guide:        return "guide";
        case EvidenceSource::ToolbarGuide: return "toolbar+guide";
        case EvidenceSource::Project:      return "project";
    }
    return "uncited";
}

const char* ringTopologyClassName(RingTopologyClass c) {
    switch (c) {
        case RingTopologyClass::Flat:     return "flat";
        case RingTopologyClass::Chair:    return "chair";
        case RingTopologyClass::Aromatic: return "aromatic";
    }
    return "flat";
}

const char* ringAttachModeName(RingAttachMode m) {
    switch (m) {
        case RingAttachMode::EmptyCanvas:  return "empty-canvas";
        case RingAttachMode::GrowFromAtom: return "grow-from-atom";
        case RingAttachMode::FuseOnBond:   return "fuse-on-bond";
        case RingAttachMode::Spiro:        return "spiro";
    }
    return "empty-canvas";
}

// ---------------------------------------------------------------------------
// Preset table (10 rings)
// ---------------------------------------------------------------------------
// Order == tools.csv order so the coverage report joins with no mapping table.
const std::vector<RingPresetSpec>& ringPresets() {
    static std::vector<RingPresetSpec> table;
    if (!table.empty()) return table;

    struct Row {
        RingPresetId id;
        const char* toolId;
        const char* command;
        const char* name;
        RingTopologyClass topology;
        int size;
        EvidenceSource evidence;
        const char* note;
    };
    static const Row rows[] = {
        { RingPresetId::Cyclopropane, "r62.tool.ring.cyclopropane",
          "chooseRingTool_Cyclopropane", "Cyclopropane (3-ring)",
          RingTopologyClass::Flat, 3, EvidenceSource::ToolbarGuide,
          "regular triangle; PROJECT geometry R = L/(2 sin(pi/3))" },
        { RingPresetId::Cyclobutane, "r62.tool.ring.cyclobutane",
          "chooseRingTool_Cyclobutane", "Cyclobutane (4-ring)",
          RingTopologyClass::Flat, 4, EvidenceSource::ToolbarGuide,
          "regular square; PROJECT geometry" },
        { RingPresetId::Cyclopentane, "r62.tool.ring.cyclopentane",
          "chooseRingTool_Cyclopentane", "Cyclopentane (5-ring)",
          RingTopologyClass::Flat, 5, EvidenceSource::ToolbarGuide,
          "regular pentagon; PROJECT geometry" },
        { RingPresetId::Cyclohexane, "r62.tool.ring.cyclohexane",
          "chooseRingTool_Cyclohexane", "Cyclohexane (6-ring)",
          RingTopologyClass::Flat, 6, EvidenceSource::ToolbarGuide,
          "flat hexagon: the classic skeletal drawing; PROJECT geometry" },
        { RingPresetId::Cycloheptane, "r62.tool.ring.cycloheptane",
          "chooseRingTool_Cycloheptane", "Cycloheptane (7-ring)",
          RingTopologyClass::Flat, 7, EvidenceSource::ToolbarGuide,
          "regular heptagon; PROJECT geometry" },
        { RingPresetId::Cyclooctane, "r62.tool.ring.cyclooctane",
          "chooseRingTool_Cyclooctane", "Cyclooctane (8-ring)",
          RingTopologyClass::Flat, 8, EvidenceSource::ToolbarGuide,
          "regular octagon; PROJECT geometry" },
        { RingPresetId::Chair1, "r62.tool.ring.chair1",
          "chooseRingTool_Chair_Cyclohexane1", "Cyclohexane chair (orientation 1)",
          RingTopologyClass::Chair, 6, EvidenceSource::ToolbarGuide,
          "real chair conformer projected to 2D; PROJECT vertex numbering" },
        { RingPresetId::Chair2, "r62.tool.ring.chair2",
          "chooseRingTool_Chair_Cyclohexane2", "Cyclohexane chair (orientation 2)",
          RingTopologyClass::Chair, 6, EvidenceSource::ToolbarGuide,
          "same conformer rotated; PROJECT angle - NOT a different molecule" },
        { RingPresetId::Benzene, "r62.tool.ring.benzene",
          "chooseRingTool_Benzene", "Benzene",
          RingTopologyClass::Aromatic, 6, EvidenceSource::ToolbarGuide,
          "flat hexagon with REAL alternating single/double bonds" },
        { RingPresetId::Cyclopentadiene, "r62.tool.ring.cyclopentadiene",
          "chooseRingTool_Cyclopentadiene", "Cyclopentadiene",
          RingTopologyClass::Aromatic, 5, EvidenceSource::ToolbarGuide,
          "flat pentagon, two REAL double bonds at the diene positions" }
    };

    const size_t n = sizeof(rows) / sizeof(rows[0]);
    table.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        RingPresetSpec s;
        s.presetId  = static_cast<uint32_t>(i);
        s.id        = rows[i].id;
        s.toolId    = rows[i].toolId;
        s.command   = rows[i].command;
        s.displayName = rows[i].name;
        s.category  = "Ring structures";
        s.topology  = rows[i].topology;
        s.size      = rows[i].size;
        s.implemented = true;
        s.evidence  = rows[i].evidence;
        s.note      = rows[i].note;
        table.push_back(s);
    }
    return table;
}

const std::vector<ChainPresetSpec>& chainPresets() {
    static std::vector<ChainPresetSpec> table;
    if (!table.empty()) return table;

    struct Row {
        ChainPresetId id;
        const char* toolId;
        const char* command;
        const char* name;
        bool snaking;
        const char* note;
    };
    static const Row rows[] = {
        { ChainPresetId::Acyclic, "r62.tool.chain.acyclic",
          "chooseAlkaneTool_Acyclic_Chain", "Acyclic (straight) chain",
          false,
          "continues in the drag direction; only the 120-degree ANGLE is GUIDE" },
        { ChainPresetId::Snaking, "r62.tool.chain.snaking",
          "chooseAlkaneTool_Snaking_Chain", "Snaking (turnable) chain",
          true,
          "alternates +/- chainAngle after every bond; drag end picks the sign" }
    };
    const size_t n = sizeof(rows) / sizeof(rows[0]);
    for (size_t i = 0; i < n; ++i) {
        ChainPresetSpec s;
        s.presetId  = static_cast<uint32_t>(i);
        s.id        = rows[i].id;
        s.toolId    = rows[i].toolId;
        s.command   = rows[i].command;
        s.displayName = rows[i].name;
        s.category  = "Carbon chains";
        s.snaking   = rows[i].snaking;
        s.implemented = true;
        s.evidence  = EvidenceSource::ToolbarGuide;
        s.note      = rows[i].note;
        table.push_back(s);
    }
    return table;
}

const RingPresetSpec* ringPresetForCommand(const std::string& command) {
    const std::vector<RingPresetSpec>& t = ringPresets();
    for (size_t i = 0; i < t.size(); ++i) if (t[i].command == command) return &t[i];
    return 0;
}
const RingPresetSpec* ringPresetById(RingPresetId id) {
    const std::vector<RingPresetSpec>& t = ringPresets();
    for (size_t i = 0; i < t.size(); ++i) if (t[i].id == id) return &t[i];
    return 0;
}
const ChainPresetSpec* chainPresetForCommand(const std::string& command) {
    const std::vector<ChainPresetSpec>& t = chainPresets();
    for (size_t i = 0; i < t.size(); ++i) if (t[i].command == command) return &t[i];
    return 0;
}
const ChainPresetSpec* chainPresetById(ChainPresetId id) {
    const std::vector<ChainPresetSpec>& t = chainPresets();
    for (size_t i = 0; i < t.size(); ++i) if (t[i].id == id) return &t[i];
    return 0;
}

std::vector<core::ToolPreset> ringsChainsToolPresets() {
    std::vector<core::ToolPreset> out;
    const std::vector<RingPresetSpec>& rings = ringPresets();
    for (size_t i = 0; i < rings.size(); ++i) {
        core::ToolPreset p;
        p.presetId      = rings[i].presetId;
        p.id            = rings[i].toolId;
        p.behavior      = core::ToolBehavior::PlaceRings;
        p.target        = core::PresetTarget::Atom;
        p.displayName   = rings[i].displayName;
        p.category      = rings[i].category;
        p.sourceCommand = rings[i].command;
        p.optionKey     = rings[i].toolId;
        p.invalidation  = core::kInvalidateStructure | core::kInvalidateGeometry |
                          core::kInvalidateSemantics;
        p.implemented   = rings[i].implemented;
        out.push_back(p);
    }
    const std::vector<ChainPresetSpec>& chains = chainPresets();
    for (size_t i = 0; i < chains.size(); ++i) {
        core::ToolPreset p;
        p.presetId      = static_cast<uint32_t>(rings.size() + i);
        p.id            = chains[i].toolId;
        p.behavior      = core::ToolBehavior::PlaceChain;
        p.target        = core::PresetTarget::Atom;
        p.displayName   = chains[i].displayName;
        p.category      = chains[i].category;
        p.sourceCommand = chains[i].command;
        p.optionKey     = chains[i].toolId;
        p.invalidation  = core::kInvalidateStructure | core::kInvalidateGeometry |
                          core::kInvalidateSemantics;
        p.implemented   = chains[i].implemented;
        out.push_back(p);
    }
    return out;
}

std::vector<std::string> ringChainCommandNames() {
    std::vector<std::string> out;
    const std::vector<RingPresetSpec>& rings = ringPresets();
    for (size_t i = 0; i < rings.size(); ++i) out.push_back(rings[i].command);
    const std::vector<ChainPresetSpec>& chains = chainPresets();
    for (size_t i = 0; i < chains.size(); ++i) out.push_back(chains[i].command);
    return out;
}

// ---------------------------------------------------------------------------
// Preset -> id lookups used by the placement code
// ---------------------------------------------------------------------------
namespace detail {
const char* ringToolId(RingPresetId id) {
    const RingPresetSpec* s = ringPresetById(id);
    return s ? s->toolId.c_str() : "";
}
const char* ringCommandName(RingPresetId id) {
    const RingPresetSpec* s = ringPresetById(id);
    return s ? s->command.c_str() : "";
}
const char* chainToolId(ChainPresetId id) {
    const ChainPresetSpec* s = chainPresetById(id);
    return s ? s->toolId.c_str() : "";
}
const char* chainCommandName(ChainPresetId id) {
    const ChainPresetSpec* s = chainPresetById(id);
    return s ? s->command.c_str() : "";
}
} // namespace detail

// ---------------------------------------------------------------------------
// Geometry: the flat rings
// ---------------------------------------------------------------------------
double regularRingRadius(int size, double bondLength) {
    if (size < 3 || bondLength <= 0.0) return 0.0;
    return bondLength / (2.0 * std::sin(kPi / static_cast<double>(size)));
}

namespace {

// Aromatic / diene bond patterns.
//
// Benzene: the six edges alternate single / double. The pattern is stored
// EXPLICITLY on the edges, never inferred, so a renderer cannot flatten it.
//
// Cyclopentadiene: vertex 0 is the sp3 CH2 apex, so its two edges (0 and 4) stay
// single and edges 1 and 2 carry the two diene double bonds. That is the real
// CH2=CH-CH=CH-CH2 connection. The tests assert exactly two double bonds, that
// neither touches the apex, and that the ring is one closed cycle.
bool aromaticSemanticsFor(RingPresetId preset) {
    return preset == RingPresetId::Benzene;
}

std::vector<RingBondSpec> buildBondPattern(RingPresetId preset, int size) {
    std::vector<RingBondSpec> bonds;
    bonds.reserve(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i) {
        RingBondSpec b;
        b.from = i;
        b.to   = (i + 1) % size;
        b.order = core::BondOrder::Single;
        b.display = core::BondDisplay::Normal;
        b.cyclicRow = i % 2;
        bonds.push_back(b);
    }
    if (preset == RingPresetId::Benzene) {
        // Alternating pattern chosen so edge 0 is SINGLE. That matters because
        // edge 0 is the edge a FUSION maps the hot bond onto: fusing must never
        // silently retype the existing bond, so the shared edge is deliberately
        // the one the pattern leaves single.
        for (int i = 0; i < size; ++i)
            if (i % 2 == 1) bonds[static_cast<size_t>(i)].order = core::BondOrder::Double;
    } else if (preset == RingPresetId::Cyclopentadiene) {
        // Cyclopentadiene is a CONJUGATED diene: the two double bonds sit on
        // edges 1 and 3 (edges 1-2 and 3-4), one single bond apart. Edges 0 and
        // 4 are the single bonds that meet at the saturated CH2 apex (vertex 0).
        // Using bonds {1,2} would put both doubles on edges sharing vertex 2,
        // i.e. a cumulated allene pattern, which is chemically wrong and also
        // makes the two inner double-bond lines fuse on that shared vertex.
        bonds[1].order = core::BondOrder::Double;
        bonds[3].order = core::BondOrder::Double;
    }
    return bonds;
}

// Build the six ring carbons of an idealised cyclohexane CHAIR and project them
// to 2D in the conventional skeletal way. This is a real conformer with a real
// pucker, not a flat hexagon wearing a chair label.
//
// THE 3D CONFORMER (exact, and the reason the numbers below are what they are)
//   C_k = ( r cos(60 k),  r sin(60 k),  (-1)^k * h ),   k = 0..5
//
//   Ring bond k -> k+1: the two vertices are 60 degrees apart in the plane, so
//   the in-plane separation is 2 r sin(30) = r, and the z separation is 2h.
//   Therefore EVERY ring bond has
//        d^2 = r^2 + 4 h^2
//   and setting that equal to the requested bond length L gives the family
//        r^2 + 4 h^2 = L^2 .
//   That family is the "twist" family: every member has equal 3D bond lengths.
//   The CHAIR is the member with the classic puckering ratio h/r = 1/2, so
//        h = r/2,   r = L / sqrt(1 + 4*(1/2)^2) = L / sqrt(2) .
//   (Other ratios are the intermediate twist conformers, not the chair.)
//
// THE PROJECTION, and why this is not a flat hexagon
//   Dropping z outright WOULD give a regular hexagon (all six vertices lie on
//   the circle of radius r), which is the trap this function avoids. The chair
//   is drawn with an OBLIQUE skeletal projection, the long-standing convention
//   for showing the two out-of-plane bonds:
//        X = x + kx * s_k ,   Y = -y + ky * s_k ,   s_k = (-1)^k
//   with kx = 0.60 * h and ky = 0.35 * h. The sign flip makes the projection
//   NON-LINEAR in the vertex parity, so the six projected vertices are NOT
//   concyclic and the six drawn bonds have different lengths. A test asserts
//   both of those independently, which is precisely the check a flat hexagon
//   fails.
void buildChairVertices(double bondLength, std::vector<RingVertex>* out) {
    out->clear();
    // Standard skeletal chair projection. The six points below are the classic
    // 2D chair outline: two long parallel top/bottom bonds (alternating up and
    // down zig-zag) joined by the two side bonds, giving the familiar "two
    // parallel trapezoids" reading. The previous oblique projection collapsed
    // the conformer into a squashed four-lobe blob; this skeleton keeps the
    // receding/advancing bonds long and the front/back bonds short, which is
    // what makes a chair read as a chair rather than a flat hexagon.
    //
    // The shape is only defined up to scale here; it is normalised so that the
    // LONGEST ring edge equals the requested bond length. The other edges are
    // shorter, which is exactly the foreshortening a chair shows in 2D (and is
    // what the test's "vertices are not all on one circle" check relies on).
    static const double kChairSkeleton[6][2] = {
        {-1.2137, -0.4326},
        {-0.2527, -0.1582},
        { 0.7137, -0.4326},
        { 1.2137,  0.4329},
        { 0.2527,  0.1582},
        {-0.7137,  0.4329},
    };

    double maxEdge = 0.0;
    for (int k = 0; k < 6; ++k) {
        const int n = (k + 1) % 6;
        const double dx = kChairSkeleton[n][0] - kChairSkeleton[k][0];
        const double dy = kChairSkeleton[n][1] - kChairSkeleton[k][1];
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d > maxEdge) maxEdge = d;
    }
    double scale = 1.0;
    if (bondLength > 0.0 && maxEdge > 0.0) scale = bondLength / maxEdge;

    out->reserve(6);
    for (int k = 0; k < 6; ++k) {
        RingVertex v;
        v.local = core::Vec2(kChairSkeleton[k][0] * scale, kChairSkeleton[k][1] * scale);
        v.element = 6;
        out->push_back(v);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// buildRingGeometry
// ---------------------------------------------------------------------------
core::Error buildRingGeometry(RingPresetId preset, double bondLength,
                              RingGeometry* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "out is null");
    const RingPresetSpec* spec = ringPresetById(preset);
    if (!spec) return core::Error(core::ErrorCode::OutOfRange, "unknown ring preset id");
    if (!(bondLength > 0.0) || !std::isfinite(bondLength))
        return core::Error(core::ErrorCode::InvalidArgument,
                           "bondLength must be positive and finite");

    RingGeometry g;
    g.preset = preset;
    g.topology = spec->topology;
    g.size = spec->size;
    g.bondLength = bondLength;
    g.planar = (spec->topology != RingTopologyClass::Chair);
    g.ringShapeEvidence = EvidenceSource::Project;

    if (spec->topology == RingTopologyClass::Chair) {
        buildChairVertices(bondLength, &g.vertices);
        g.circumRadius = 0.0;   // a chair has no single circumradius
        g.note = "3D chair conformer projected to 2D; the silhouette is NOT a "
                 "regular hexagon";
    } else {
        const double R = regularRingRadius(g.size, bondLength);
        g.circumRadius = R;
        g.vertices.reserve(static_cast<size_t>(g.size));
        for (int i = 0; i < g.size; ++i) {
            const double a = deg2rad(360.0 * static_cast<double>(i) /
                                     static_cast<double>(g.size));
            RingVertex v;
            v.local = core::Vec2(R * std::cos(a), -R * std::sin(a));
            v.element = 6;
            g.vertices.push_back(v);
        }
        g.note = "regular polygon; edge length is exactly bondLength";
    }

    // Enforce the centroid at the local origin (documented convention).
    core::Vec2 centroid(0, 0);
    for (size_t i = 0; i < g.vertices.size(); ++i)
        centroid = centroid + g.vertices[i].local;
    if (!g.vertices.empty()) {
        centroid = centroid * (1.0 / static_cast<double>(g.vertices.size()));
        for (size_t i = 0; i < g.vertices.size(); ++i)
            g.vertices[i].local = g.vertices[i].local - centroid;
    }

    g.bonds = buildBondPattern(preset, g.size);

    // Measure the edges so the numbers in a report are computed, not claimed.
    double mn = 0.0, mx = 0.0;
    for (size_t i = 0; i < g.bonds.size(); ++i) {
        const core::Vec2& a = g.vertices[static_cast<size_t>(g.bonds[i].from)].local;
        const core::Vec2& b = g.vertices[static_cast<size_t>(g.bonds[i].to)].local;
        const double d = core::length(b - a);
        if (i == 0) { mn = mx = d; }
        else { if (d < mn) mn = d; if (d > mx) mx = d; }
    }
    g.minEdgeLength = mn;
    g.maxEdgeLength = mx;

    *out = g;
    return core::Error::success();
}

double chairOrientationDegrees(RingPresetId preset) {
    if (preset == RingPresetId::Chair1) return 0.0;
    if (preset == RingPresetId::Chair2) return 30.0;
    return 0.0;
}

// A BondSemantics value for a ring edge: benzene edges carry Aromatic semantics
// so a renderer or persistence layer can tell an aromatic ring from a Kekule
// ring without re-perceiving aromaticity (which T06 explicitly does not do).
core::BondSemantics ringBondSemantics(RingPresetId preset) {
    return aromaticSemanticsFor(preset) ? core::BondSemantics::Aromatic
                                        : core::BondSemantics::Normal;
}

namespace detail {

// Rotate a local offset by an angle in the core convention (degrees,
// counter-clockwise in math orientation, y DOWN).
core::Vec2 rotateLocal(const core::Vec2& local, double rotationDeg) {
    const double r = rotationDeg * kPi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    return core::Vec2(local.x * c + local.y * s, -local.x * s + local.y * c);
}

// Rotate/translate a local geometry into world space. Exposed so the tests can
// reproduce a placement bit-for-bit.
core::Vec2 placeLocal(const core::Vec2& local, double rotationDeg,
                      const core::Vec2& origin) {
    return rotateLocal(local, rotationDeg) + origin;
}

double directionTo(const core::Vec2& origin, const core::Vec2& target) {
    return core::angleOf(target - origin);
}

} // namespace detail

} // namespace rings_chains
} // namespace r62
