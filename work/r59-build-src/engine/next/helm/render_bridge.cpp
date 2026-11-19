// =============================================================================
// re_round62_viewer/next/helm/render_bridge.cpp
//
// T15 - sequence to T03 primitive emission. The boundary is in render_bridge.hpp.
// =============================================================================

#include "next/helm/render_bridge.hpp"

#include "next/helm/layout.hpp"

#include <cstring>

namespace r62 {
namespace helm {

// The label a residue shows. A known monomer shows its symbol; an unknown one shows
// the text it was preserved with, so a reader can always tell WHICH residue this is
// even when its chemistry was not resolved.
std::string labelForMonomerToken(const MonomerInstance& instance) {
    if (!instance.definition.symbol.empty()) return instance.definition.symbol;
    if (!instance.sourceText.empty()) return instance.sourceText;
    return std::string("?");
}

// ----------------------------------------------------------------------------
// Natural-analog colours
// ----------------------------------------------------------------------------
// The guide (PDF pages 172-174) lists a palette NAME per natural analog. Those
// names are from a vendor palette; this function records the RGB this project uses
// for each name and states in RESULTS.md that the exact vendor RGB was NOT
// extracted from an installed palette file. Returning 0 means the table does not
// cover the analog, which is different from 'black'.
uint32_t naturalAnalogColor(const std::string& naturalAnalog) {
    if (naturalAnalog.size() != 1) return 0u;
    // Peptide analogs (guide PDF pages 171-173).
    struct Entry { const char* analog; uint32_t rgba; };
    static const Entry kPeptide[] = {
        { "A", 0x8FBCE6FFu },   // LondonHue     sky blue
        { "R", 0x9B7FBFu << 8 | 0xFFu },  // Wisteria muted violet
        { "N", 0x7B6B8Fu << 8 | 0xFFu },  // Affair       dusty violet
        { "D", 0xE5896Au << 8 | 0xFFu },  // Scampi       orange
        { "C", 0x3F9E8Cu << 8 | 0xFFu },  // SanMarino    teal
        { "Q", 0x4B5EA3u << 8 | 0xFFu },  // Indigo
        { "E", 0x5A88B5u << 8 | 0xFFu },  // Danube       mid blue
        { "G", 0x4A90C2u << 8 | 0xFFu },  // SteelBlue
        { "H", 0x6FA8C8u << 8 | 0xFFu },  // HippieBlue
        { "I", 0x6FA88Cu << 8 | 0xFFu },  // BreakerBay   sea green
        { "L", 0x8FAF9Fu << 8 | 0xFFu },  // SilverTree   sage
        { "K", 0x8A9A6Bu << 8 | 0xFFu },  // Xanadu       olive grey
        { "M", 0x9CBF7Au << 8 | 0xFFu },  // Olivine      yellow green
        { "F", 0xA8D08Au << 8 | 0xFFu },  // Celery       light green
        { "P", 0xC9A94Bu << 8 | 0xFFu },  // OldGold
        { "S", 0xD9A05Bu << 8 | 0xFFu },  // Anzac        ochre
        { "T", 0xE08A5Au << 8 | 0xFFu },  // FireBush     orange brown
        { "W", 0xC9B04Bu << 8 | 0xFFu },  // Zest         yellow
        { "Y", 0xE08A8Au << 8 | 0xFFu },  // FlamePea     salmon
        { "V", 0xD97A8Au << 8 | 0xFFu },  // Punch        pink
        { "O", 0x9E9E9Eu << 8 | 0xFFu },  // Grey
        { "X", 0xC8C4D8u << 8 | 0xFFu },  // Selago       pale lilac (unknown)
    };
    for (size_t i = 0; i < sizeof(kPeptide) / sizeof(kPeptide[0]); ++i)
        if (naturalAnalog == kPeptide[i].analog) return kPeptide[i].rgba;
    // DNA/RNA analogs (guide PDF pages 172-174). Note the guide separates the
    // ribose/deoxyribose and phosphate rows from the base rows.
    static const Entry kNucleic[] = {
        { "I", 0xD97A8Au << 8 | 0xFFu },  // inosine base: Punch
        { "r", 0xA8C4A0u << 8 | 0xFFu },  // Ribose: Sage
        { "p", 0xE5B96Au << 8 | 0xFFu },  // Phosphate: Feijoya
        { "A", 0x7B6B8Fu << 8 | 0xFFu },  // Adenine: Affair
        { "C", 0x4B5EA3u << 8 | 0xFFu },  // Cytosine: Indigo
        { "G", 0x8FBCE6u << 8 | 0xFFu },  // Guanine: LondonHue
        { "T", 0x8A9A6Bu << 8 | 0xFFu },  // Thymine: Xanadu
        { "U", 0xC9A94Bu << 8 | 0xFFu },  // Uracil: OldGold
        { "X", 0x9E9E9Eu << 8 | 0xFFu },  // unknown: Grey
        { "N", 0x9E9E9Eu << 8 | 0xFFu },
    };
    for (size_t i = 0; i < sizeof(kNucleic) / sizeof(kNucleic[0]); ++i)
        if (naturalAnalog == kNucleic[i].analog) return kNucleic[i].rgba;
    return 0u;
}

// ----------------------------------------------------------------------------
// Layers
// ----------------------------------------------------------------------------
const LayerRegistration* sequenceLayers(size_t* count) {
    static const LayerRegistration kLayers[] = {
        { "sequence.connections", render::layers::kContent + 10 },
        { "sequence.residues",    render::layers::kContent + 20 },
        { "sequence.labels",      render::layers::kContent + 30 },
    };
    if (count) *count = sizeof(kLayers) / sizeof(kLayers[0]);
    return kLayers;
}

namespace {

render::LayerId layerFor(int32_t z) {
    return render::LayerId(static_cast<uint32_t>(z));
}

// One marker primitive for a residue. The SHAPE comes from the cartoon table when
// the polymer is in cartoon display, and is a small square handle in structure
// display. This never invents a chemical shape for an unknown residue: it falls
// back to the marker, and the label carries the symbol.
render::Primitive markerFor(const MonomerInstance& instance, const core::Vec2& position,
                            const RenderOptions& options, SequenceDisplayMode display) {
    render::Primitive primitive;
    primitive.source = core::ObjectRef(core::ObjectKind::Atom, instance.id);
    primitive.layer = layerFor(render::layers::kContent + 20);
    primitive.z = static_cast<int32_t>(instance.id);
    primitive.size = options.markerRadius * 2.0;
    primitive.version = instance.id + 1;

    const CartoonShape shape = (display == SequenceDisplayMode::Cartoon)
                                   ? cartoonShapeFor(instance.definition)
                                   : CartoonShape::Square;
    if (shape == CartoonShape::None) {
        // An unknown residue in cartoon display: a neutral marker, because drawing
        // a confident shape for chemistry we did not resolve would be a lie.
        primitive.kind = render::PrimKind::Marker;
        primitive.marker = render::MarkerShape::Ring;
    } else if (shape == CartoonShape::Circle) {
        primitive.kind = render::PrimKind::Ellipse;
        primitive.points.push_back(position);
        primitive.points.push_back(core::Vec2(options.markerRadius, options.markerRadius));
    } else if (shape == CartoonShape::Square || shape == CartoonShape::Diamond ||
               shape == CartoonShape::Rectangle) {
        // Square, diamond and rectangle are all polygons; the diamond is the square
        // rotated 45 degrees so the guide's shape vocabulary is preserved.
        primitive.kind = render::PrimKind::Polygon;
        const double r = options.markerRadius;
        render::Subpath subpath;
        subpath.closed = true;
        if (shape == CartoonShape::Diamond) {
            subpath.points.push_back(core::Vec2(position.x, position.y - r));
            subpath.points.push_back(core::Vec2(position.x + r, position.y));
            subpath.points.push_back(core::Vec2(position.x, position.y + r));
            subpath.points.push_back(core::Vec2(position.x - r, position.y));
        } else {
            const double w = (shape == CartoonShape::Rectangle) ? r * 1.6 : r;
            const double h = (shape == CartoonShape::Rectangle) ? r * 0.8 : r;
            subpath.points.push_back(core::Vec2(position.x - w, position.y - h));
            subpath.points.push_back(core::Vec2(position.x + w, position.y - h));
            subpath.points.push_back(core::Vec2(position.x + w, position.y + h));
            subpath.points.push_back(core::Vec2(position.x - w, position.y + h));
        }
        primitive.subpaths.push_back(subpath);
    } else {
        // Octagon (sugar) and trapezoid (base). Both are regular polygons with the
        // guide's vertex count, which is what makes the shapes distinguishable.
        const int sides = (shape == CartoonShape::Octagon) ? 8 : 4;
        primitive.kind = render::PrimKind::Polygon;
        render::Subpath subpath;
        subpath.closed = true;
        for (int i = 0; i < sides; ++i) {
            // A trapezoid is a 4-gon; squashing the top pair of vertices is what
            // distinguishes it from a square, which is how the guide's table reads.
            const double theta = 2.0 * 3.14159265358979323846 * static_cast<double>(i)
                                 / static_cast<double>(sides)
                                 + 3.14159265358979323846 / static_cast<double>(sides);
            double rx = options.markerRadius;
            double ry = options.markerRadius;
            if (shape == CartoonShape::Trapezoid && std::cos(theta) > 0.0) rx *= 0.6;
            subpath.points.push_back(core::Vec2(position.x + rx * std::cos(theta),
                                                position.y + ry * std::sin(theta)));
        }
        primitive.subpaths.push_back(subpath);
    }

    // Colour: the guide derives a cartoon colour from the natural analog.
    uint32_t fill = 0xDDDDDDFFu;
    if (options.naturalAnalogColors) {
        const uint32_t analog = naturalAnalogColor(instance.definition.naturalAnalog);
        if (analog != 0u) fill = analog;
    }
    if (!instance.definition.chemistryDefined) {
        fill = instance.definition.isBlob() ? options.blobColor : options.unknownColor;
    }
    primitive.style.fill = render::Paint(fill, true);
    primitive.style.stroke = render::Paint(0x333333FFu, true);
    primitive.style.strokeWidth = 0.5;
    return primitive;
}

// One Text primitive carrying the residue label.
render::Primitive labelFor(const MonomerInstance& instance, const core::Vec2& position,
                           const RenderOptions& options) {
    render::Primitive primitive;
    primitive.kind = render::PrimKind::Text;
    primitive.source = core::ObjectRef(core::ObjectKind::Atom, instance.id);
    primitive.layer = layerFor(render::layers::kContent + 30);
    primitive.z = static_cast<int32_t>(instance.id);
    primitive.text = labelForMonomerToken(instance);
    primitive.textSize = options.labelSize;
    primitive.points.push_back(position);
    primitive.style.stroke = render::Paint(0x000000FFu, true);
    primitive.version = instance.id + 1;
    return primitive;
}

// One Segment for a connection.
render::Primitive connectionSegment(const SequenceModel& model,
                                    const SequenceConnection& connection,
                                    const SequenceLayout& layout,
                                    const RenderOptions& options) {
    render::Primitive primitive;
    primitive.kind = render::PrimKind::Segment;
    const SequencePolymer* pf = model.polymer(connection.from.polymer);
    const SequencePolymer* pt = model.polymer(connection.to.polymer);
    primitive.source = core::ObjectRef(core::ObjectKind::Bond, connection.id);
    primitive.layer = layerFor(render::layers::kContent + 10);
    primitive.z = static_cast<int32_t>(connection.id);
    if (pf && connection.from.position < pf->monomers.size())
        primitive.points.push_back(layout.get(pf->monomers[connection.from.position].id));
    if (pt && connection.to.position < pt->monomers.size())
        primitive.points.push_back(layout.get(pt->monomers[connection.to.position].id));
    if (connection.kind == ConnectionKind::Hydrogen) {
        primitive.style.stroke = render::Paint(options.hydrogenColor, true);
        primitive.style.dash = render::DashPattern(2.0, 2.0);
    } else {
        primitive.style.stroke = render::Paint(options.covalentColor, true);
    }
    primitive.style.strokeWidth = options.connectionWidth;
    primitive.version = connection.id + 1;
    return primitive;
}

} // namespace

// ----------------------------------------------------------------------------
// Emission
// ----------------------------------------------------------------------------
size_t emitPolymer(const SequenceModel& model, size_t polymerIndex,
                   const RenderOptions& options, render::Scene* scene) {
    if (!scene) return 0;
    const SequencePolymer* polymer = model.polymer(polymerIndex);
    if (!polymer) return 0;
    const SequenceLayout& layout = model.layout();
    size_t emitted = 0;
    for (size_t m = 0; m < polymer->monomers.size(); ++m) {
        const MonomerInstance& instance = polymer->monomers[m];
        if (!instance.alive) continue;
        if (!layout.has(instance.id)) continue;   // not placed: nothing to draw
        const core::Vec2 position = layout.get(instance.id);
        scene->add(markerFor(instance, position, options, polymer->display));
        scene->add(labelFor(instance, position, options));
        emitted += 2;
    }
    return emitted;
}

size_t emitSequence(const SequenceModel& model, const RenderOptions& options,
                    render::Scene* scene) {
    if (!scene) return 0;
    size_t emitted = 0;
    // Connections first so a residue marker covers the end of a bond, which is the
    // usual reading order for a sequence diagram.
    for (size_t c = 0; c < model.connectionCount(); ++c) {
        const SequenceConnection* connection = model.connection(c);
        if (!connection || !connection->alive) continue;
        if (connection->kind == ConnectionKind::Generic) continue;
        scene->add(connectionSegment(model, *connection, model.layout(), options));
        ++emitted;
    }
    for (size_t p = 0; p < model.polymerCount(); ++p)
        emitted += emitPolymer(model, p, options, scene);
    return emitted;
}

} // namespace helm
} // namespace r62
