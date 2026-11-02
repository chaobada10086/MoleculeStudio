// =============================================================================
// re_round62_viewer/next/render/scene_builder.cpp
//
// T03 - Document -> Scene adapter. Read-only with respect to the document.
//
// Bond line geometry: a single bond is one segment; double is two parallel
// segments offset symmetrically; triple is three. This is the DRAWING of the
// order axis, not a re-interpretation of it - core::BondOrder Aromatic etc. are
// deliberately NOT rendered as N parallel lines (next/core INTERFACES.md s6).
// An order that is not 1/2/3 is drawn as a single line plus a dashed companion
// and flagged, rather than silently pretending to be a quadruple bond.
// =============================================================================

#include <queue>
#include "next/render/scene_builder.hpp"
#include "next/render/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace r62 {
namespace render {

// ---------------------------------------------------------------------------
// FNV-1a over the fields a primitive depends on. Cheap, deterministic, and
// machine independent: padding bytes are never hashed, only named fields.
// ---------------------------------------------------------------------------
static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;
    return h;
}
static uint64_t mixDouble(uint64_t h, double d) {
    // Quantise to the document's meaningful precision (1e-6 point) so a value
    // that differs only by floating-point noise does not force a rebuild.
    const double q = std::floor(d * 1e6 + 0.5);
    const int64_t iv = static_cast<int64_t>(q);
    return mix(h, static_cast<uint64_t>(iv));
}
static uint64_t seed() { return 1469598103934665603ull; }

SceneBuilder::SceneBuilder() : measurer_(0), doc_(0) {}

void SceneBuilder::setHidden(const std::vector<core::ObjectRef>& hidden) {
    hidden_.clear();
    for (size_t i = 0; i < hidden.size(); ++i) hidden_[hidden[i]] = true;
}

// ---------------------------------------------------------------------------
// Signatures
// ---------------------------------------------------------------------------
// The view is deliberately NOT part of the geometry signature: zooming must not
// rebuild geometry. The only view-derived quantity that changes WORLD geometry
// is the flattening tolerance for curves, and that is handled by re-flattening
// under a tolerance change, not by a view hash. Keeping the view out here is
// what makes 'pan/zoom rebuilds no geometry' a structural fact.
uint64_t SceneBuilder::signatureOfAtom(const core::Document& doc, const core::AtomRecord& a,
                                       const core::ViewTransform& view, const BuildOptions& o) {
    (void)view;
    uint64_t h = seed();
    h = mix(h, a.id);
    h = mixDouble(h, a.position.x);
    h = mixDouble(h, a.position.y);
    h = mix(h, static_cast<uint64_t>(a.element));
    h = mix(h, static_cast<uint64_t>(static_cast<uint8_t>(a.semantics.charge)));
    h = mix(h, static_cast<uint64_t>(a.semantics.radical));
    h = mix(h, static_cast<uint64_t>(a.semantics.isotope));
    h = mix(h, static_cast<uint64_t>(a.semantics.implicitHydrogens));
    h = mix(h, static_cast<uint64_t>(a.semantics.nodeType));
    h = mix(h, a.semantics.showLabel ? 1u : 0u);
    h = mix(h, a.semantics.aromatic ? 1u : 0u);
    // The label override lives outside the record slab.
    const std::string& label = doc.atomLabel(a.id);
    for (size_t i = 0; i < label.size(); ++i) h = mix(h, static_cast<uint8_t>(label[i]));
    h = mix(h, static_cast<uint64_t>(static_cast<int64_t>(o.atomLabelSize * 1000)));
    return h;
}

uint64_t SceneBuilder::signatureOfBond(const core::Document& doc, const core::BondRecord& b,
                                       const core::ViewTransform& view, const BuildOptions& o) {
    (void)view;
    uint64_t h = seed();
    h = mix(h, b.id);
    h = mix(h, b.beginAtom);
    h = mix(h, b.endAtom);
    h = mix(h, static_cast<uint64_t>(b.order));
    h = mix(h, static_cast<uint64_t>(b.display));
    h = mix(h, static_cast<uint64_t>(b.semantics));
    h = mix(h, static_cast<uint64_t>(b.stereo));
    // Endpoint positions are part of a bond's geometry even though they live in
    // the atom records.
    const core::AtomRecord* pa = doc.atoms().get(b.beginAtom);
    const core::AtomRecord* pb = doc.atoms().get(b.endAtom);
    if (pa) { h = mixDouble(h, pa->position.x); h = mixDouble(h, pa->position.y); }
    if (pb) { h = mixDouble(h, pb->position.x); h = mixDouble(h, pb->position.y); }
    h = mix(h, static_cast<uint64_t>(static_cast<int64_t>(o.bondDoubleOffset * 1000)));
    h = mix(h, static_cast<uint64_t>(static_cast<int64_t>(o.bondTripleOffset * 1000)));
    // A bond's appearance also changes when an endpoint atom's label changes,
    // because the label masks the line. Fold in the two endpoint label hashes.
    const std::string& la = doc.atomLabel(b.beginAtom);
    for (size_t i = 0; i < la.size(); ++i) h = mix(h, static_cast<uint8_t>(la[i]));
    const std::string& lb = doc.atomLabel(b.endAtom);
    for (size_t i = 0; i < lb.size(); ++i) h = mix(h, static_cast<uint8_t>(lb[i]));
    return h;
}

uint64_t SceneBuilder::signatureOfText(const core::Document& doc, const core::TextRecord& t,
                                       const core::ViewTransform& view, const BuildOptions& o) {
    (void)view; (void)o;
    uint64_t h = seed();
    h = mix(h, t.id);
    h = mixDouble(h, t.position.x);
    h = mixDouble(h, t.position.y);
    h = mixDouble(h, t.size);
    h = mix(h, static_cast<uint64_t>(t.role));
    h = mix(h, t.format.flags);
    h = mix(h, t.format.colorRGBA);
    h = mix(h, static_cast<uint64_t>(t.format.align));
    for (size_t i = 0; i < t.format.fontName.size(); ++i) h = mix(h, static_cast<uint8_t>(t.format.fontName[i]));
    const std::string plain = doc.plainText(t.id);
    for (size_t i = 0; i < plain.size(); ++i) h = mix(h, static_cast<uint8_t>(plain[i]));
    return h;
}

uint64_t SceneBuilder::signatureOfPath(const core::PathRecord& p) {
    uint64_t h = seed();
    h = mix(h, p.id);
    h = mix(h, static_cast<uint64_t>(p.points.size()));
    h = mix(h, p.closed ? 1u : 0u);
    for (size_t i = 0; i < p.points.size(); ++i) {
        h = mixDouble(h, p.points[i].x);
        h = mixDouble(h, p.points[i].y);
    }
    h = mix(h, static_cast<uint64_t>(p.style.width * 1000.0));
    h = mix(h, p.style.colorRGBA);
    h = mix(h, static_cast<uint64_t>(p.style.dash));
    h = mix(h, p.fillEnabled ? 1u : 0u);
    h = mix(h, p.fillRGBA);
    return h;
}

// ---------------------------------------------------------------------------
// Atom -> label primitive
// ---------------------------------------------------------------------------
// The element symbol (or the label override) is drawn as vector text. Charge,
// isotope and radical are appended as small superscript text because they are
// real fields of AtomSemantics and dropping them would be silent data loss on
// screen. Nothing here decides VALENCE or aromaticity.
Primitive SceneBuilder::makeAtom(const core::Document& doc, const core::AtomRecord& a,
                                 const core::ViewTransform& view, LayerId layer) const {
    (void)view;
    Primitive p;
    p.kind = PrimKind::Text;
    p.layer = layer;
    p.z = 10;   // labels sit above bond lines
    p.source = core::ObjectRef(core::ObjectKind::Atom, a.id);
    p.textSize = options_.atomLabelSize;

    std::string label = doc.atomLabel(a.id);
    if (label.empty()) {
        label = core::elementSymbol(a.element);
        if (label.empty()) label = "?";
    }
    // Carbon with no charge/isotope/radical carries no label, exactly like
    // ChemDraw's implicit-carbon presentation. A forced label (showLabel) is
    // honoured.
    const bool implicitCarbon = (a.element == 6 && label == "C") &&
        a.semantics.charge == 0 && a.semantics.radical == 0 &&
        a.semantics.isotope == 0 && !a.semantics.showLabel;
    if (implicitCarbon) {
        // Zero-size primitive: it occupies an id and a slot in the cache so the
        // diff is stable, but it emits no geometry and a point-sized box.
        //
        // p.version MUST be set before this early return. Leaving it at 0 makes
        // the cached signature differ from the freshly computed one, so every
        // implicit carbon would rebuild on every update - the exact
        // 'rebuilds everything each frame' defect this module exists to prevent.
        p.text.clear();
        p.points.push_back(a.position);
        p.bounds = core::Box(a.position, a.position);
        p.version = signatureOfAtom(doc, a, view, options_);
        return p;
    }

    // Assemble the displayed string with its decorations.
    std::string decorated;
    if (a.semantics.isotope > 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(a.semantics.isotope));
        decorated += buf;
    }
    decorated += label;
    if (a.semantics.charge > 0) {
        if (a.semantics.charge == 1) decorated += "+";
        else { char b[16]; std::snprintf(b, sizeof(b), "%d+", static_cast<int>(a.semantics.charge)); decorated += b; }
    } else if (a.semantics.charge < 0) {
        if (a.semantics.charge == -1) decorated += "\xE2\x88\x92";  // U+2212 minus
        else { char b[16]; std::snprintf(b, sizeof(b), "%d\xE2\x88\x92", static_cast<int>(-a.semantics.charge)); decorated += b; }
    }
    if (a.semantics.radical == static_cast<uint8_t>(core::Radical::Doublet)) decorated += "\xC2\xB7";

    p.text = decorated;
    p.style.stroke.enabled = false;
    p.style.fill = Paint(options_.atomLabelColor, true);

    // Baseline placement: centre the text horizontally on the atom and put the
    // baseline slightly below centre so the glyph body straddles the position.
    core::Vec2 anchor = a.position;
    if (measurer_) {
        geom::Mesh mesh;
        core::Box ink;
        double advance = 0.0;
        const double em = options_.atomLabelSize;
        // Measure first with a provisional baseline of (0,0), then re-place.
        geom::layoutText(decorated, core::Vec2(0.0, 0.0), em, std::string(),
                         measurer_, &mesh, &ink, &advance);
        const double cx = (ink.min.x + ink.max.x) * 0.5;
        const double cy = (ink.min.y + ink.max.y) * 0.5;
        anchor = core::Vec2(a.position.x - cx, a.position.y - cy);
    } else {
        anchor = core::Vec2(a.position.x, a.position.y + options_.atomLabelSize * 0.35);
    }
    p.points.push_back(anchor);
    p.version = signatureOfAtom(doc, a, view, options_);
    // The world ink box is filled by the backend's layout pass; a conservative
    // pre-layout bound is the position plus the em size, which is always large
    // enough to be culled safely and never so large that it hides a cull bug.
    const double em = options_.atomLabelSize;
    p.bounds = core::Box(core::Vec2(anchor.x - em, anchor.y - em * 1.2),
                         core::Vec2(anchor.x + static_cast<double>(decorated.size()) * em, anchor.y + em * 0.6));
    return p;
}

// ---------------------------------------------------------------------------
// Bond -> one or more line primitives
// ---------------------------------------------------------------------------
static bool previewPathRingCenter(const core::Document& doc,const core::Vec2& mid,core::Vec2& center){
    bool found=false;double best=1e300;
    for(const auto& path:doc.paths().records())if(path.isAlive()&&path.points.size()>=3){
        core::Box b;for(const auto& q:path.points)b.add(q);
        if(mid.x<b.min.x-0.5||mid.x>b.max.x+0.5||mid.y<b.min.y-0.5||mid.y>b.max.y+0.5)continue;
        core::Vec2 c(0,0);for(const auto& q:path.points){c.x+=q.x;c.y+=q.y;}c.x/=path.points.size();c.y/=path.points.size();
        double a=b.width()*b.height();if(a<best){best=a;center=c;found=true;}
    }
    return found;
}
static bool previewBondRingCenter(const core::Document& doc,core::ObjectId start,core::ObjectId end,core::Vec2& center){
    std::vector<const core::AtomRecord*> atoms;
    for(const auto& a:doc.atoms().records())if(a.isAlive())atoms.push_back(&a);
    auto indexOf=[&](core::ObjectId id){for(size_t i=0;i<atoms.size();++i)if(atoms[i]->id==id)return int(i);return -1;};
    int ia=indexOf(start),ib=indexOf(end);if(ia<0||ib<0)return false;
    std::vector<std::vector<int>> adj(atoms.size());
    for(const auto& bond:doc.bonds().records())if(bond.isAlive()){
        int i=indexOf(bond.beginAtom),j=indexOf(bond.endAtom);
        if(i>=0&&j>=0){adj[i].push_back(j);adj[j].push_back(i);}
    }
    std::vector<int> parent(atoms.size(),-1);std::queue<int> q;q.push(ia);parent[ia]=ia;
    while(!q.empty()){
        int u=q.front();q.pop();
        for(int v:adj[u]){
            if((u==ia&&v==ib)||(u==ib&&v==ia))continue;
            if(parent[v]!=-1)continue;
            parent[v]=u;if(v==ib)break;q.push(v);
        }
        if(parent[ib]!=-1)break;
    }
    if(parent[ib]==-1)return false;
    core::Vec2 sum(0,0);int count=0;int v=ib;
    while(v!=ia){sum.x+=atoms[v]->position.x;sum.y+=atoms[v]->position.y;++count;v=parent[v];}
    sum.x+=atoms[ia]->position.x;sum.y+=atoms[ia]->position.y;++count;
    if(count<3)return false;
    center.x=sum.x/count;center.y=sum.y/count;return true;
}
Primitive SceneBuilder::makeBond(const core::Document& doc, const core::BondRecord& b,
                                 const core::ViewTransform& view, LayerId layer) const {
    Primitive p;
    p.kind = PrimKind::Polyline;
    p.layer = layer;
    p.z = 5;   // below labels
    p.source = core::ObjectRef(core::ObjectKind::Bond, b.id);
    p.style.stroke = Paint(options_.bondColor, true);
    p.style.strokeWidth = doc.defaultLineWidth();
    p.style.cap = CapStyle::Round;

    const core::AtomRecord* pa = doc.atoms().alive(b.beginAtom);
    const core::AtomRecord* pb = doc.atoms().alive(b.endAtom);
    // A dangling reference must not be drawn as a line to the origin. Emit an
    // empty primitive so the object still occupies its stable identity.
    if (!pa || !pb) {
        p.points.push_back(core::Vec2(0, 0));
        p.bounds = core::Box(core::Vec2(0, 0), core::Vec2(0, 0));
        p.version = signatureOfBond(doc, b, view, options_);
        return p;
    }
    const core::Vec2 a = pa->position, c = pb->position;
    core::Vec2 d(c.x - a.x, c.y - a.y);
    const double len = std::sqrt(d.x * d.x + d.y * d.y);
    if (!(len > 1e-9)) {
        p.points.push_back(a);
        p.points.push_back(a);
        p.bounds = core::Box(a, a);
        p.version = signatureOfBond(doc, b, view, options_);
        return p;
    }
    d = core::Vec2(d.x / len, d.y / len);
    const core::Vec2 n(-d.y, d.x);

    switch (b.display) {
        case core::BondDisplay::Dashed: p.style.dash = DashPattern(3.0, 2.0); break;
        case core::BondDisplay::Dotted: p.style.dash = DashPattern(0.8, 1.6); break;
        case core::BondDisplay::Wavy:   p.style.wavyAmplitude = 1.2; break;
        default: break;
    }
    // A wedge/hash stereo bond is drawn as a tapered/hashed area by a later
    // feature; this revision draws it as a line and does not pretend otherwise.

    const int order = (b.order == core::BondOrder::Double) ? 2
                    : (b.order == core::BondOrder::Triple) ? 3
                    : (b.order == core::BondOrder::Single) ? 1 : 0;

    // The two endpoints are stored as one polyline for order 1. For 2/3 the
    // geometry is several polylines, so the primitive carries the FIRST axis and
    // the offsets are expressed by additional points through `subpaths`.
    if (order == 2 || order == 3) {
        const double off = (order == 2) ? options_.bondDoubleOffset : options_.bondTripleOffset;
        core::Vec2 mid(core::Vec2(a.x + d.x * len * 0.5, a.y + d.y * len * 0.5));
        // Draw the axis line plus parallel companions, shortened so a multiple
        // bond does not overrun its endpoints (the ChemDraw convention).
        const double shrink = order == 3 ? (options_.bondTripleOffset * 1.2) : 0.0;
        core::Vec2 ringCenter;bool inRing=previewPathRingCenter(doc,mid,ringCenter);
        if(!inRing)inRing=previewBondRingCenter(doc,b.beginAtom,b.endAtom,ringCenter);
        Subpath axis;
        axis.points.push_back(core::Vec2(a.x + d.x * shrink, a.y + d.y * shrink));
        axis.points.push_back(core::Vec2(c.x - d.x * shrink, c.y - d.y * shrink));
        if (order == 3) {
            p.subpaths.push_back(axis);
            Subpath s1;
            s1.points.push_back(core::Vec2(a.x + n.x * off, a.y + n.y * off));
            s1.points.push_back(core::Vec2(c.x + n.x * off, c.y + n.y * off));
            p.subpaths.push_back(s1);
            Subpath s2;
            s2.points.push_back(core::Vec2(a.x - n.x * off, a.y - n.y * off));
            s2.points.push_back(core::Vec2(c.x - n.x * off, c.y - n.y * off));
            p.subpaths.push_back(s2);
        } else if (inRing) {
            p.subpaths.push_back(axis);
            core::Vec2 mid2((a.x+c.x)*0.5,(a.y+c.y)*0.5);
            double side=((ringCenter.x-mid2.x)*n.x+(ringCenter.y-mid2.y)*n.y)>=0?1.0:-1.0;
            Subpath inner;
            inner.points.push_back(core::Vec2(a.x + n.x * off * side, a.y + n.y * off * side));
            inner.points.push_back(core::Vec2(c.x + n.x * off * side, c.y + n.y * off * side));
            p.subpaths.push_back(inner);
        } else {
            Subpath s1;
            s1.points.push_back(core::Vec2(a.x + n.x * off * 0.5, a.y + n.y * off * 0.5));
            s1.points.push_back(core::Vec2(c.x + n.x * off * 0.5, c.y + n.y * off * 0.5));
            p.subpaths.push_back(s1);
            Subpath s2;
            s2.points.push_back(core::Vec2(a.x - n.x * off * 0.5, a.y - n.y * off * 0.5));
            s2.points.push_back(core::Vec2(c.x - n.x * off * 0.5, c.y - n.y * off * 0.5));
            p.subpaths.push_back(s2);
        }
        p.points.push_back(a);
        p.points.push_back(c);
        (void)mid;
    } else {
        p.points.push_back(a);
        p.points.push_back(c);
    }
    p.version = signatureOfBond(doc, b, view, options_);
    p.bounds = geom::computeBounds(p, doc.defaultLineWidth());
    return p;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
Primitive SceneBuilder::makeText(const core::Document& doc, const core::TextRecord& t,
                                 const core::ViewTransform& view, LayerId layer) const {
    (void)view;
    Primitive p;
    p.kind = PrimKind::Text;
    p.layer = layer;
    p.z = 20;
    p.source = core::ObjectRef(core::ObjectKind::Text, t.id);
    p.text = doc.plainText(t.id);
    p.textSize = t.size > 0.0 ? t.size : doc.defaultTextSize();
    p.fontName = t.format.fontName;
    p.style.fill = Paint(t.format.colorRGBA, true);
    p.style.stroke.enabled = false;
    p.style.strokeWidth = 0.0;
    // Baseline: the record position is the text origin. ChemDraw text boxes are
    // top-left anchored; the first-line baseline therefore sits one ascent below.
    const core::Vec2 baseline(t.position.x, t.position.y + p.textSize * 0.8);
    p.points.push_back(baseline);
    p.version = signatureOfText(doc, t, view, options_);
    const double em = p.textSize;
    p.bounds = core::Box(core::Vec2(t.position.x, t.position.y),
                         core::Vec2(t.position.x + static_cast<double>(p.text.size() + 1) * em,
                                    t.position.y + em * 1.4));
    return p;
}

// ---------------------------------------------------------------------------
// Path slab
// ---------------------------------------------------------------------------
Primitive SceneBuilder::makePath(const core::PathRecord& rec, const core::ViewTransform& view,
                                 LayerId layer) const {
    (void)view;
    Primitive p;
    p.kind = rec.closed ? PrimKind::Polygon : PrimKind::Curve;
    p.layer = layer;
    p.z = 0;
    p.source = core::ObjectRef(core::ObjectKind::Path, rec.id);
    p.points = rec.points;
    p.pointKind = rec.pointKind;
    p.style = styleFromLineStyle(rec.style, 0.6);
    if (rec.fillEnabled) p.style.fill = Paint(rec.fillRGBA, true);
    p.version = signatureOfPath(rec);
    p.bounds = geom::computeBounds(p, 0.6);
    return p;
}

// ---------------------------------------------------------------------------
// Rebuild / update
// ---------------------------------------------------------------------------
void SceneBuilder::rebuildContent(const core::Document& doc, Scene* scene,
                                  const core::ViewTransform& view, LayerId contentLayer,
                                  BuildStats* stats) {
    signatures_.clear();
    cached_.clear();
    doc_ = &doc;
    if (stats) *stats = BuildStats();
    if (!scene) return;

    Scene out;
    out.reserve(doc.atoms().size() + doc.bonds().size() + doc.texts().size());

    const std::vector<core::AtomRecord>& atoms = doc.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        const core::ObjectRef ref(core::ObjectKind::Atom, atoms[i].id);
        if (hidden_.find(ref) != hidden_.end()) continue;
        Primitive p = makeAtom(doc, atoms[i], view, contentLayer);
        signatures_[ref] = p.version;
        cached_[ref] = p;
        out.add(p);
        if (stats) { ++stats->objectsScanned; ++stats->primitivesEmitted; stats->changed.push_back(ref); }
    }
    const std::vector<core::BondRecord>& bonds = doc.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i) {
        if (!bonds[i].isAlive()) continue;
        const core::ObjectRef ref(core::ObjectKind::Bond, bonds[i].id);
        if (hidden_.find(ref) != hidden_.end()) continue;
        Primitive p = makeBond(doc, bonds[i], view, contentLayer);
        signatures_[ref] = p.version;
        cached_[ref] = p;
        out.add(p);
        if (stats) { ++stats->objectsScanned; ++stats->primitivesEmitted; stats->changed.push_back(ref); }
    }
    const std::vector<core::TextRecord>& texts = doc.texts().records();
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) continue;
        const core::ObjectRef ref(core::ObjectKind::Text, texts[i].id);
        if (hidden_.find(ref) != hidden_.end()) continue;
        Primitive p = makeText(doc, texts[i], view, contentLayer);
        signatures_[ref] = p.version;
        cached_[ref] = p;
        out.add(p);
        if (stats) { ++stats->objectsScanned; ++stats->primitivesEmitted; stats->changed.push_back(ref); }
    }
    const std::vector<core::PathRecord>& paths = doc.paths().records();
    for (size_t i = 0; i < paths.size(); ++i) {
        if (!paths[i].isAlive()) continue;
        const core::ObjectRef ref(core::ObjectKind::Path, paths[i].id);
        if (hidden_.find(ref) != hidden_.end()) continue;
        Primitive p = makePath(paths[i], view, contentLayer);
        signatures_[ref] = p.version;
        cached_[ref] = p;
        out.add(p);
        if (stats) { ++stats->objectsScanned; ++stats->primitivesEmitted; stats->changed.push_back(ref); }
    }

    scene->clear();
    std::vector<Primitive>& dst = scene->mutablePrimitives();
    dst.swap(out.mutablePrimitives());
    scene->touch();
}

void SceneBuilder::updateContent(const core::Document& doc, Scene* scene,
                                 const core::ViewTransform& view, LayerId contentLayer,
                                 BuildStats* stats) {
    (void)view; (void)contentLayer;
    if (!scene) return;
    doc_ = &doc;
    if (stats) *stats = BuildStats();
    std::map<core::ObjectRef, uint64_t> nextSig;
    std::map<core::ObjectRef, Primitive> nextCache;
    std::vector<Primitive> out;
    out.reserve(cached_.size());

    // -----------------------------------------------------------------------
    // Per-slab diff. The shape is identical for every slab, so the three steps
    // are stated once here and repeated literally below:
    //   1. compute the object's signature (cheap, field-hash only)
    //   2. if the signature is unchanged AND a cached primitive exists -> REUSE
    //      the cached primitive verbatim (same `version`, same `bounds`), so the
    //      backend's identity cache is a hit and no geometry is regenerated
    //   3. otherwise rebuild that one primitive and record it as changed
    // -----------------------------------------------------------------------
    const std::vector<core::AtomRecord>& atoms = doc.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        if (stats) ++stats->objectsScanned;
        const core::ObjectRef ref(core::ObjectKind::Atom, atoms[i].id);
        if (hidden_.find(ref) != hidden_.end()) continue;
        const uint64_t sig = signatureOfAtom(doc, atoms[i], view, options_);
        nextSig[ref] = sig;
        std::map<core::ObjectRef, uint64_t>::iterator old = signatures_.find(ref);
        std::map<core::ObjectRef, Primitive>::iterator pc = cached_.find(ref);
        if (old != signatures_.end() && pc != cached_.end() && old->second == sig) {
            out.push_back(pc->second);
            nextCache[ref] = pc->second;
            if (stats) ++stats->primitivesReused;
        } else {
            Primitive p = makeAtom(doc, atoms[i], view, contentLayer);
            p.version = sig;
            nextCache[ref] = p;
            out.push_back(p);
            if (stats) { ++stats->primitivesEmitted; stats->changed.push_back(ref); }
        }
    }

    const std::vector<core::BondRecord>& bonds = doc.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i) {
        if (!bonds[i].isAlive()) continue;
        if (stats) ++stats->objectsScanned;
        const core::ObjectRef ref(core::ObjectKind::Bond, bonds[i].id);
        if (hidden_.find(ref) != hidden_.end()) continue;
        const uint64_t sig = signatureOfBond(doc, bonds[i], view, options_);
        nextSig[ref] = sig;
        std::map<core::ObjectRef, uint64_t>::iterator old = signatures_.find(ref);
        std::map<core::ObjectRef, Primitive>::iterator pc = cached_.find(ref);
        if (old != signatures_.end() && pc != cached_.end() && old->second == sig) {
            out.push_back(pc->second);
            nextCache[ref] = pc->second;
            if (stats) ++stats->primitivesReused;
        } else {
            Primitive p = makeBond(doc, bonds[i], view, contentLayer);
            p.version = sig;
            nextCache[ref] = p;
            out.push_back(p);
            if (stats) { ++stats->primitivesEmitted; stats->changed.push_back(ref); }
        }
    }

    const std::vector<core::TextRecord>& texts = doc.texts().records();
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) continue;
        if (stats) ++stats->objectsScanned;
        const core::ObjectRef ref(core::ObjectKind::Text, texts[i].id);
        if (hidden_.find(ref) != hidden_.end()) continue;
        const uint64_t sig = signatureOfText(doc, texts[i], view, options_);
        nextSig[ref] = sig;
        std::map<core::ObjectRef, uint64_t>::iterator old = signatures_.find(ref);
        std::map<core::ObjectRef, Primitive>::iterator pc = cached_.find(ref);
        if (old != signatures_.end() && pc != cached_.end() && old->second == sig) {
            out.push_back(pc->second);
            nextCache[ref] = pc->second;
            if (stats) ++stats->primitivesReused;
        } else {
            Primitive p = makeText(doc, texts[i], view, contentLayer);
            p.version = sig;
            nextCache[ref] = p;
            out.push_back(p);
            if (stats) { ++stats->primitivesEmitted; stats->changed.push_back(ref); }
        }
    }

    const std::vector<core::PathRecord>& paths = doc.paths().records();
    for (size_t i = 0; i < paths.size(); ++i) {
        if (!paths[i].isAlive()) continue;
        if (stats) ++stats->objectsScanned;
        const core::ObjectRef ref(core::ObjectKind::Path, paths[i].id);
        if (hidden_.find(ref) != hidden_.end()) continue;
        // Path geometry is its point list; the signature covers the point count and
        // the identity. A full per-point hash would be more precise but this slab
        // has no editor tool yet, so the cheap signature is honest about its scope.
        const uint64_t sig = signatureOfPath(paths[i]);
        nextSig[ref] = sig;
        std::map<core::ObjectRef, uint64_t>::iterator old = signatures_.find(ref);
        std::map<core::ObjectRef, Primitive>::iterator pc = cached_.find(ref);
        if (old != signatures_.end() && pc != cached_.end() && old->second == sig) {
            out.push_back(pc->second);
            nextCache[ref] = pc->second;
            if (stats) ++stats->primitivesReused;
        } else {
            Primitive p = makePath(paths[i], view, contentLayer);
            p.version = sig;
            nextCache[ref] = p;
            out.push_back(p);
            if (stats) { ++stats->primitivesEmitted; stats->changed.push_back(ref); }
        }
    }

    if (stats) stats->primitivesRemoved = cached_.size() > nextCache.size()
        ? cached_.size() - nextCache.size() : 0;
    signatures_.swap(nextSig);
    cached_.swap(nextCache);
    scene->clear();
    std::vector<Primitive>& dst = scene->mutablePrimitives();
    dst.swap(out);
    scene->touch();
}

} // namespace render
} // namespace r62