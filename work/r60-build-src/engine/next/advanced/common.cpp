#include "next/advanced/common.hpp"

#include "next/chemistry/element_data.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace r62 {
namespace advanced {

const char* const kDepthKey   = "advanced.perspective.z";
const char* const kCameraKey  = "advanced.perspective.camera";
const char* const kLockKey    = "advanced.locked";
const char* const kAnchorKey  = "advanced.anchor";

const char* provenanceName(Provenance p) {
    switch (p) {
        case Provenance::NativeBacked:   return "Native-backed";
        case Provenance::OwnImplemented: return "Own-implemented";
        case Provenance::Unsupported:    return "Unsupported";
    }
    return "Unsupported";
}

// ---------------------------------------------------------------------------
// Hashing helpers. FNV-1a with a final avalanche; stable across runs and
// platforms because every input is an integer or a byte string.
// ---------------------------------------------------------------------------
namespace {

uint64_t mix64(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
    h ^= (h >> 29);
    return h;
}

uint64_t hashBytes(uint64_t h, const void* data, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) h = mix64(h, static_cast<uint64_t>(p[i]));
    return h;
}

uint64_t hashString(uint64_t h, const std::string& s) {
    h = mix64(h, static_cast<uint64_t>(s.size()) + 0x9E3779B97F4A7C15ULL);
    return hashBytes(h, s.data(), s.size());
}

uint64_t hashI64(uint64_t h, int64_t v) { return mix64(h, static_cast<uint64_t>(v)); }

uint64_t hashDouble(uint64_t h, double v) {
    // Bit pattern, so -0.0 and 0.0 differ; callers normalise before calling
    // when that matters. Here it is used only for non-coordinate attributes
    // whose exact value must round trip.
    uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return mix64(h, bits);
}

uint64_t hashBag(uint64_t h, const core::ExtensionBag& bag) {
    // Order-independent: the bag keeps insertion order, which is not part of
    // the logical state.
    std::vector<uint64_t> parts;
    const std::vector<core::ExtensionEntry>& entries = bag.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        uint64_t p = 1469598103934665603ULL;
        p = hashString(p, entries[i].key);
        p = hashString(p, entries[i].value);
        p = hashI64(p, entries[i].sourceId);
        parts.push_back(p);
    }
    std::sort(parts.begin(), parts.end());
    h = mix64(h, static_cast<uint64_t>(parts.size()));
    for (size_t i = 0; i < parts.size(); ++i) h = mix64(h, parts[i]);
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------
core::Error captureLayoutInput(const core::Document& doc, LayoutInput* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null LayoutInput");
    out->atoms.clear();
    out->bonds.clear();
    out->texts.clear();
    out->defaultBondLength = doc.defaultBondLength();
    out->defaultTextSize   = doc.defaultTextSize();
    out->documentId        = doc.id();
    out->revision          = doc.revision();
    out->structureSerial   = doc.structureSerial();

    const std::vector<core::AtomRecord>& atoms = doc.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        const core::AtomRecord& a = atoms[i];
        if (!a.isAlive()) continue;
        LayoutAtom la;
        la.id       = a.id;
        la.position = a.position;
        la.element  = a.element;
        la.locked   = isLocked(doc, core::ObjectRef(core::ObjectKind::Atom, a.id));
        la.anchor   = isAnchored(doc, a.id);
        const core::ExtensionBag& bag =
            doc.extensions(core::ObjectRef(core::ObjectKind::Atom, a.id));
        const std::string* z = bag.findValue(kDepthKey);
        if (z) {
            la.hasDepth = true;
            la.depth = std::strtod(z->c_str(), 0);
        }
        out->atoms.push_back(la);
    }
    // Rising id order is guaranteed by the slab layout, but state it explicitly:
    // a deterministic module must not rely on that by accident.
    std::sort(out->atoms.begin(), out->atoms.end(),
              [](const LayoutAtom& x, const LayoutAtom& y) { return x.id < y.id; });

    const std::vector<core::BondRecord>& bonds = doc.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i) {
        const core::BondRecord& b = bonds[i];
        if (!b.isAlive()) continue;
        if (!doc.atoms().alive(b.beginAtom) || !doc.atoms().alive(b.endAtom)) continue;
        LayoutBond lb;
        lb.id        = b.id;
        lb.a         = b.beginAtom;
        lb.b         = b.endAtom;
        lb.order     = static_cast<uint8_t>(b.order);
        lb.display   = static_cast<uint8_t>(b.display);
        lb.semantics = static_cast<uint8_t>(b.semantics);
        lb.stereo    = static_cast<uint8_t>(b.stereo);
        lb.aromatic  = b.semantics == core::BondSemantics::Aromatic ||
                       b.order == core::BondOrder::Aromatic ||
                       b.order == core::BondOrder::SingleOrAromatic ||
                       b.order == core::BondOrder::DoubleOrAromatic;
        out->bonds.push_back(lb);
    }
    std::sort(out->bonds.begin(), out->bonds.end(),
              [](const LayoutBond& x, const LayoutBond& y) { return x.id < y.id; });

    const std::vector<core::TextRecord>& texts = doc.texts().records();
    for (size_t i = 0; i < texts.size(); ++i) {
        const core::TextRecord& t = texts[i];
        if (!t.isAlive()) continue;
        LayoutText lt;
        lt.id       = t.id;
        lt.position = t.position;
        lt.size     = t.size;
        lt.text     = doc.plainText(t.id);
        lt.locked   = isLocked(doc, core::ObjectRef(core::ObjectKind::Text, t.id));
        out->texts.push_back(lt);
    }
    std::sort(out->texts.begin(), out->texts.end(),
              [](const LayoutText& x, const LayoutText& y) { return x.id < y.id; });

    const std::vector<core::AnnotationRecord>& arrows = doc.arrows().records();
    for (size_t i = 0; i < arrows.size(); ++i) {
        if (!arrows[i].isAlive()) continue;
        LayoutArrow la;
        la.id = arrows[i].id;
        la.a  = arrows[i].geometry[0];
        la.b  = arrows[i].geometry[1];
        out->arrows.push_back(la);
    }
    std::sort(out->arrows.begin(), out->arrows.end(),
              [](const LayoutArrow& x, const LayoutArrow& y) { return x.id < y.id; });
    return core::Error::success();
}

const LayoutAtom* findAtom(const LayoutInput& in, core::ObjectId id) {
    for (size_t i = 0; i < in.atoms.size(); ++i)
        if (in.atoms[i].id == id) return &in.atoms[i];
    return 0;
}

// ---------------------------------------------------------------------------
// Lock / anchor / depth
// ---------------------------------------------------------------------------
bool isLocked(const core::Document& doc, const core::ObjectRef& ref) {
    const std::string* v = doc.extensions(ref).findValue(kLockKey);
    return v && *v == "1";
}

void setLockedQuiet(core::Document& doc, const core::ObjectRef& ref, bool locked) {
    if (locked) {
        doc.extensions(ref).set(kLockKey, "1");
        noteExtensionWrite(doc, ref, false);
        return;
    }
    // Nothing to erase means nothing to record. Calling the NON-CONST
    // extensions() would CREATE an empty bag, and T01's digest hashes the
    // presence of the bag, so an unnecessary call would make an undo unable to
    // restore the exact digest. The const overload must be selected explicitly:
    // on a Document& the compiler picks the non-const one otherwise. (Measured;
    // see RESULTS.md.)
    const core::Document& view = doc;
    if (!view.extensions(ref).findValue(kLockKey)) return;
    doc.extensions(ref).erase(kLockKey);
    noteExtensionWrite(doc, ref, false);
}

bool isAnchored(const core::Document& doc, core::ObjectId atom) {
    const std::string* v =
        doc.extensions(core::ObjectRef(core::ObjectKind::Atom, atom)).findValue(kAnchorKey);
    return v && *v == "1";
}

double readDepth(const core::Document& doc, const core::ObjectRef& ref) {
    const std::string* v = doc.extensions(ref).findValue(kDepthKey);
    if (!v) return 0.0;
    return std::strtod(v->c_str(), 0);
}

void writeDepthQuiet(core::Document& doc, const core::ObjectRef& ref, double depth) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", depth);
    doc.extensions(ref).set(kDepthKey, std::string(buf));
    noteExtensionWrite(doc, ref, false);
}

void clearDepthQuiet(core::Document& doc, const core::ObjectRef& ref) {
    const core::Document& view = doc;
    if (!view.extensions(ref).findValue(kDepthKey)) return;   // see setLockedQuiet
    doc.extensions(ref).erase(kDepthKey);
    noteExtensionWrite(doc, ref, false);
}

void noteExtensionWrite(core::Document& doc, const core::ObjectRef& ref,
                        bool documentLevel) {
    core::DocumentSession* session = core::documentSession(&doc);
    if (session) session->noteExtensions(ref, documentLevel);
}

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------
namespace {

class UnionFind {
public:
    explicit UnionFind(size_t n) : parent_(n) {
        for (size_t i = 0; i < n; ++i) parent_[i] = i;
    }
    size_t find(size_t x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];
            x = parent_[x];
        }
        return x;
    }
    void unite(size_t a, size_t b) {
        a = find(a); b = find(b);
        if (a != b) parent_[a] = b;
    }
private:
    std::vector<size_t> parent_;
};

} // namespace

std::vector<Component> atomComponents(const LayoutInput& in) {
    std::vector<Component> result;
    if (in.atoms.empty()) return result;

    std::map<core::ObjectId, size_t> index;
    for (size_t i = 0; i < in.atoms.size(); ++i) index[in.atoms[i].id] = i;

    UnionFind uf(in.atoms.size());
    for (size_t i = 0; i < in.bonds.size(); ++i) {
        std::map<core::ObjectId, size_t>::const_iterator ia = index.find(in.bonds[i].a);
        std::map<core::ObjectId, size_t>::const_iterator ib = index.find(in.bonds[i].b);
        if (ia == index.end() || ib == index.end()) continue;
        uf.unite(ia->second, ib->second);
    }

    std::map<size_t, Component> byRoot;
    for (size_t i = 0; i < in.atoms.size(); ++i)
        byRoot[uf.find(i)].atoms.push_back(in.atoms[i].id);
    for (size_t i = 0; i < in.bonds.size(); ++i) {
        std::map<core::ObjectId, size_t>::const_iterator ia = index.find(in.bonds[i].a);
        if (ia == index.end()) continue;
        byRoot[uf.find(ia->second)].bonds.push_back(in.bonds[i].id);
    }

    for (std::map<size_t, Component>::iterator it = byRoot.begin(); it != byRoot.end(); ++it) {
        std::sort(it->second.atoms.begin(), it->second.atoms.end());
        std::sort(it->second.bonds.begin(), it->second.bonds.end());
        result.push_back(it->second);
    }
    std::sort(result.begin(), result.end(),
              [](const Component& x, const Component& y) {
                  return x.atoms.front() < y.atoms.front();
              });
    return result;
}

std::vector<Component> atomComponents(const core::Document& doc) {
    LayoutInput in;
    if (!captureLayoutInput(doc, &in).ok()) return std::vector<Component>();
    return atomComponents(in);
}

core::Vec2 componentCentroid(const LayoutInput& in, const Component& c) {
    core::Vec2 sum(0.0, 0.0);
    size_t n = 0;
    for (size_t i = 0; i < c.atoms.size(); ++i) {
        const LayoutAtom* a = findAtom(in, c.atoms[i]);
        if (!a) continue;
        sum = sum + a->position;
        ++n;
    }
    if (n == 0) return core::Vec2(0.0, 0.0);
    return core::Vec2(sum.x / static_cast<double>(n), sum.y / static_cast<double>(n));
}

core::Box componentBounds(const LayoutInput& in, const Component& c) {
    core::Box box;
    bool first = true;
    for (size_t i = 0; i < c.atoms.size(); ++i) {
        const LayoutAtom* a = findAtom(in, c.atoms[i]);
        if (!a) continue;
        if (first) { box = core::Box(a->position, a->position); first = false; }
        else box.add(a->position);
    }
    return box;
}

// ---------------------------------------------------------------------------
// Structure signature
// ---------------------------------------------------------------------------
namespace {

uint64_t atomLabelHash(const core::Document& doc, const core::AtomRecord& a) {
    uint64_t h = 1469598103934665603ULL;
    h = hashI64(h, a.element);
    h = hashI64(h, a.semantics.charge);
    h = hashI64(h, a.semantics.radical);
    h = hashI64(h, a.semantics.isotope);
    h = hashI64(h, a.semantics.implicitHydrogens);
    h = hashI64(h, a.semantics.nodeType);
    h = hashI64(h, a.semantics.showLabel ? 1 : 0);
    h = hashI64(h, a.semantics.aromatic ? 1 : 0);
    h = hashString(h, doc.atomLabel(a.id));
    return h;
}

uint64_t bondAxesHash(const core::BondRecord& b) {
    uint64_t h = 1469598103934665603ULL;
    h = hashI64(h, static_cast<int64_t>(b.order));
    h = hashI64(h, static_cast<int64_t>(b.display));
    h = hashI64(h, static_cast<int64_t>(b.semantics));
    h = hashI64(h, static_cast<int64_t>(b.stereo));
    return h;
}

uint64_t textAttributeHash(const core::Document& doc, const core::TextRecord& t) {
    uint64_t h = 1469598103934665603ULL;
    h = hashI64(h, static_cast<int64_t>(t.role));
    h = hashDouble(h, t.size);
    h = hashString(h, t.format.fontName);
    h = hashDouble(h, t.format.size);
    h = hashI64(h, t.format.flags);
    h = hashI64(h, t.format.align);
    h = hashI64(h, t.format.colorRGBA);
    const std::vector<core::TextRun>& runs = doc.runs(t.id);
    h = hashI64(h, static_cast<int64_t>(runs.size()));
    for (size_t i = 0; i < runs.size(); ++i) {
        h = hashString(h, runs[i].utf8);
        h = hashI64(h, runs[i].flags);
        h = hashString(h, runs[i].fontName);
        h = hashDouble(h, runs[i].size);
    }
    return h;
}

uint64_t annotationAttributeHash(const core::AnnotationRecord& a) {
    uint64_t h = 1469598103934665603ULL;
    h = hashI64(h, static_cast<int64_t>(a.kind));
    h = hashI64(h, a.presetId);
    h = hashDouble(h, a.scale);
    h = hashDouble(h, a.rotation);
    h = hashDouble(h, a.style.width);
    h = hashI64(h, a.style.colorRGBA);
    h = hashI64(h, a.style.dash);
    h = hashI64(h, a.style.lineCap);
    h = hashI64(h, a.fillRGBA);
    // anchors[] are ObjectIds; deliberately excluded so the invariant does not
    // depend on slot numbering.
    return h;
}

uint64_t pathAttributeHash(const core::PathRecord& p) {
    uint64_t h = 1469598103934665603ULL;
    h = hashI64(h, p.closed ? 1 : 0);
    h = hashDouble(h, p.style.width);
    h = hashI64(h, p.style.colorRGBA);
    h = hashI64(h, p.style.dash);
    h = hashI64(h, p.style.lineCap);
    h = hashI64(h, p.fillRGBA);
    h = hashI64(h, p.fillEnabled ? 1 : 0);
    h = hashI64(h, static_cast<int64_t>(p.points.size()));
    return h;
}

uint64_t tableAttributeHash(const core::TableRecord& t) {
    uint64_t h = 1469598103934665603ULL;
    h = hashI64(h, t.rows);
    h = hashI64(h, t.columns);
    h = hashI64(h, static_cast<int64_t>(t.cells.size()));
    h = hashDouble(h, t.rowHeight);
    return h;
}

uint64_t groupAttributeHash(const core::GroupRecord& g) {
    uint64_t h = 1469598103934665603ULL;
    h = hashI64(h, static_cast<int64_t>(g.members.size()));
    h = hashI64(h, g.zIndex);
    h = hashString(h, g.label);
    return h;
}

} // namespace

StructureSignature structureSignature(const core::Document& doc) {
    StructureSignature sig;

    // --- collect alive atoms + index ---------------------------------------
    std::vector<core::ObjectId> atomIds;
    std::map<core::ObjectId, size_t> atomIndex;
    const std::vector<core::AtomRecord>& atoms = doc.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        atomIndex[atoms[i].id] = atomIds.size();
        atomIds.push_back(atoms[i].id);
    }
    const size_t n = atomIds.size();
    sig.atomCount = n;

    // --- adjacency with bond codes ------------------------------------------
    struct Edge { size_t a; size_t b; uint64_t code; };
    std::vector<Edge> edges;
    const std::vector<core::BondRecord>& bonds = doc.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i) {
        const core::BondRecord& b = bonds[i];
        if (!b.isAlive()) continue;
        std::map<core::ObjectId, size_t>::const_iterator ia = atomIndex.find(b.beginAtom);
        std::map<core::ObjectId, size_t>::const_iterator ib = atomIndex.find(b.endAtom);
        if (ia == atomIndex.end() || ib == atomIndex.end()) continue;
        Edge e;
        e.a = ia->second;
        e.b = ib->second;
        e.code = bondAxesHash(b);
        edges.push_back(e);
    }
    sig.bondCount = edges.size();

    // --- initial labels ------------------------------------------------------
    std::vector<uint64_t> label(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const core::AtomRecord* a = doc.atoms().alive(atomIds[i]);
        label[i] = a ? atomLabelHash(doc, *a) : 0u;
    }

    // --- Weisfeiler-Lehman refinement ---------------------------------------
    // 4 rounds is enough for every graph this editor produces and keeps the
    // cost bounded on 50k atoms (O(rounds * bonds * log degree)).
    for (int round = 0; round < 4; ++round) {
        std::vector<std::vector<uint64_t> > incoming(n);
        for (size_t e = 0; e < edges.size(); ++e) {
            uint64_t ab = mix64(0x9E3779B97F4A7C15ULL, edges[e].a);
            ab = mix64(ab, edges[e].b);
            uint64_t toB = mix64(ab, label[edges[e].a]);
            toB = mix64(toB, edges[e].code);
            incoming[edges[e].b].push_back(toB);

            uint64_t ab2 = mix64(0x9E3779B97F4A7C15ULL, edges[e].b);
            ab2 = mix64(ab2, edges[e].a);
            uint64_t toA = mix64(ab2, label[edges[e].b]);
            toA = mix64(toA, edges[e].code);
            incoming[edges[e].a].push_back(toA);
        }
        std::vector<uint64_t> next(n, 0);
        for (size_t i = 0; i < n; ++i) {
            std::vector<uint64_t>& v = incoming[i];
            std::sort(v.begin(), v.end());
            uint64_t h = label[i];
            for (size_t k = 0; k < v.size(); ++k) h = mix64(h, v[k]);
            next[i] = h;
        }
        label.swap(next);
    }

    // --- graph hash ----------------------------------------------------------
    {
        std::vector<uint64_t> parts(label.begin(), label.end());
        std::sort(parts.begin(), parts.end());
        uint64_t h = 1469598103934665603ULL;
        h = hashI64(h, static_cast<int64_t>(n));
        for (size_t i = 0; i < parts.size(); ++i) h = mix64(h, parts[i]);
        std::vector<uint64_t> edgeCodes;
        for (size_t e = 0; e < edges.size(); ++e) {
            uint64_t lo = edges[e].a < edges[e].b ? label[edges[e].a] : label[edges[e].b];
            uint64_t hi = edges[e].a < edges[e].b ? label[edges[e].b] : label[edges[e].a];
            uint64_t c = mix64(lo, hi);
            c = mix64(c, edges[e].code);
            edgeCodes.push_back(c);
        }
        std::sort(edgeCodes.begin(), edgeCodes.end());
        h = hashI64(h, static_cast<int64_t>(edgeCodes.size()));
        for (size_t i = 0; i < edgeCodes.size(); ++i) h = mix64(h, edgeCodes[i]);
        sig.graphHash = h;
        sig.hash = h;
    }

    // --- attribute hash ------------------------------------------------------
    {
        uint64_t h = 0xCBF29CE484222325ULL;
        h = hashI64(h, static_cast<int64_t>(sig.atomCount));
        h = hashI64(h, static_cast<int64_t>(sig.bondCount));
        const std::vector<core::TextRecord>& texts = doc.texts().records();
        size_t textCount = 0;
        for (size_t i = 0; i < texts.size(); ++i) {
            if (!texts[i].isAlive()) continue;
            ++textCount;
            h = mix64(h, textAttributeHash(doc, texts[i]));
        }
        sig.textCount = textCount;
        h = hashI64(h, static_cast<int64_t>(textCount));

        const std::vector<core::PathRecord>& paths = doc.paths().records();
        for (size_t i = 0; i < paths.size(); ++i)
            if (paths[i].isAlive()) h = mix64(h, pathAttributeHash(paths[i]));

        const core::ObjectSlab<core::AnnotationRecord>* ann[5] = {
            &doc.arrows(), &doc.brackets(), &doc.orbitals(), &doc.symbols(), &doc.bioShapes()
        };
        for (int s = 0; s < 5; ++s) {
            size_t count = 0;
            const std::vector<core::AnnotationRecord>& recs = ann[s]->records();
            for (size_t i = 0; i < recs.size(); ++i) {
                if (!recs[i].isAlive()) continue;
                ++count;
                h = mix64(h, annotationAttributeHash(recs[i]));
            }
            h = hashI64(h, static_cast<int64_t>(count));
        }

        const std::vector<core::TableRecord>& tables = doc.tables().records();
        for (size_t i = 0; i < tables.size(); ++i)
            if (tables[i].isAlive()) h = mix64(h, tableAttributeHash(tables[i]));

        const std::vector<core::GroupRecord>& groups = doc.groups().records();
        for (size_t i = 0; i < groups.size(); ++i)
            if (groups[i].isAlive()) h = mix64(h, groupAttributeHash(groups[i]));

        h = hashBag(h, doc.documentExtensions());
        std::map<core::ObjectRef, core::ExtensionBag>::const_iterator it;
        for (it = doc.allObjectExtensions().begin(); it != doc.allObjectExtensions().end(); ++it) {
            if (it->second.empty()) continue;
            h = mix64(h, hashBag(0x84222325CBF29CE4ULL, it->second));
        }
        sig.attributeHash = h;
        sig.hash = mix64(sig.graphHash, h);
    }

    // --- per component -------------------------------------------------------
    {
        std::vector<Component> comps = atomComponents(doc);
        sig.componentCount = comps.size();
        for (size_t c = 0; c < comps.size(); ++c) {
            uint64_t h = 1469598103934665603ULL;
            std::vector<uint64_t> labels;
            std::set<size_t> members;
            for (size_t i = 0; i < comps[c].atoms.size(); ++i) {
                std::map<core::ObjectId, size_t>::const_iterator f =
                    atomIndex.find(comps[c].atoms[i]);
                if (f == atomIndex.end()) continue;
                members.insert(f->second);
                labels.push_back(label[f->second]);
            }
            std::sort(labels.begin(), labels.end());
            for (size_t i = 0; i < labels.size(); ++i) h = mix64(h, labels[i]);
            std::vector<uint64_t> codes;
            for (size_t e = 0; e < edges.size(); ++e)
                if (members.count(edges[e].a)) codes.push_back(edges[e].code);
            std::sort(codes.begin(), codes.end());
            for (size_t i = 0; i < codes.size(); ++i) h = mix64(h, codes[i]);
            sig.componentHashes.push_back(h);
        }
        std::sort(sig.componentHashes.begin(), sig.componentHashes.end());
    }
    return sig;
}

uint64_t structureHash(const core::Document& doc) {
    return structureSignature(doc).hash;
}

std::string describeSignature(const StructureSignature& sig) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "sig=%016llx graph=%016llx attr=%016llx atoms=%llu bonds=%llu "
                  "texts=%llu components=%llu",
                  static_cast<unsigned long long>(sig.hash),
                  static_cast<unsigned long long>(sig.graphHash),
                  static_cast<unsigned long long>(sig.attributeHash),
                  static_cast<unsigned long long>(sig.atomCount),
                  static_cast<unsigned long long>(sig.bondCount),
                  static_cast<unsigned long long>(sig.textCount),
                  static_cast<unsigned long long>(sig.componentCount));
    return std::string(buf);
}

size_t aliveAtomCount(const core::Document& doc) {
    size_t n = 0;
    const std::vector<core::AtomRecord>& recs = doc.atoms().records();
    for (size_t i = 0; i < recs.size(); ++i) if (recs[i].isAlive()) ++n;
    return n;
}

size_t aliveBondCount(const core::Document& doc) {
    size_t n = 0;
    const std::vector<core::BondRecord>& recs = doc.bonds().records();
    for (size_t i = 0; i < recs.size(); ++i) if (recs[i].isAlive()) ++n;
    return n;
}

} // namespace advanced
} // namespace r62
