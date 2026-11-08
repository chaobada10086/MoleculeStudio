#include "engine/cdxml_id_repair.hpp"
#include "engine/atom_runs.hpp"
#include "engine/name_link.hpp"
#include "persistence.hpp"
#include "persistence_internal.hpp"

#include "next/core/commands.hpp"
#include "table_layout.hpp"
#include "next/experimental/model.hpp"
#include "third_party/tinyxml2/tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <set>
#include <sstream>

namespace r62 {
namespace persistence {

using namespace cdxml;
using core::Error;
using core::ErrorCode;
using core::ObjectKind;
using core::ObjectRef;
using tinyxml2::XMLElement;

namespace {

const char* kindKeyName(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Atom: return "Atom";
    case ObjectKind::Bond: return "Bond";
    case ObjectKind::Text: return "Text";
    case ObjectKind::Path: return "Path";
    case ObjectKind::Arrow: return "Arrow";
    case ObjectKind::Bracket: return "Bracket";
    case ObjectKind::Table: return "Table";
    case ObjectKind::Orbital: return "Orbital";
    case ObjectKind::Symbol: return "Symbol";
    case ObjectKind::BioShape: return "BioShape";
    case ObjectKind::Group: return "Group";
    default: return "Unknown";
    }
}

std::string rawKey(int32_t sourceId) {
    std::ostringstream key;
    key << kExtensionPrefix << "raw." << sourceId;
    return key.str();
}
std::string sourceKey(ObjectKind kind, core::ObjectId id) {
    std::ostringstream key;
    key << kExtensionPrefix << "src." << kindKeyName(kind) << "." << id;
    return key.str();
}
// The additional attributes are all copied into the object bag, and this key
// records the ORDER the reader saw their names in. Order matters because a
// reordered attribute list is a different byte stream and the acceptance test
// asserts byte identity for the no-edit case.
std::string attrOrderKey(ObjectKind kind, core::ObjectId id) {
    std::ostringstream key;
    key << kExtensionPrefix << "attr.order." << kindKeyName(kind) << "." << id;
    return key.str();
}

// One preserved source region: the verbatim bytes of a whole element plus the
// decoded values of its id-ish attributes, so a paste can rewrite them.
struct RawRegion {
    int32_t sourceId;          // CDXML id of the region root, 0 when anonymous
    std::vector<std::pair<std::string, int32_t> > refs;   // attr name -> id
    std::string bytes;
    ObjectRef ref;             // filled when this region IS one editable object
    int  order;                // position in the source's own object order
    bool orderAssigned;        // set when order was fixed at push time
    RawRegion() : sourceId(0), order(0), orderAssigned(false) {}
};

// Configuration for the ONE configurable attribute gate.
enum GateMode { kGateStrict, kGatePreserve };

struct ReaderContext {
    core::Document& document;
    CdxmlReadOptions options;
    CdxmlReadReport* report;
    RefusalReport* refusals;
    GateMode gate;

    SourceIdMap sourceIds;
    std::vector<const XMLElement*> pendingBonds;
    std::set<int32_t> usedIds;          // document-wide, for reporting
    std::set<int32_t> containerIds;     // reset at each container boundary
    std::map<uint32_t, std::string> fontNames;
    std::map<int,uint32_t> palette;
    bool color(const char* value,uint32_t* out) const {
        if(value){char* end=0;long id=std::strtol(value,&end,10);auto it=palette.find(int(id));if(end&&!*end&&it!=palette.end()){*out=it->second;return true;}}
        return colorFromCdxml(value,out);
    }
    double labelSize;
    double captionSize;
    std::string defaultFont;
    bool   fontTableSeen;
    uint64_t objectCount;
    std::vector<RawRegion> regions;   // document order; re-emitted in this order
    std::string path;                 // current structural path for diagnostics
    // Raw source bytes and a monotonic cursor into them. TinyXML2 does not expose byte
    // offsets, but the DOM preserves document order, so slicing the source at a cursor
    // that only moves forward recovers each element's EXACT bytes, including the
    // source's own indentation and attribute spacing. Regenerating those bytes from the
    // DOM would normalise whitespace and make a no-edit round trip non-identical.
    const std::string* source;
    size_t sourceCursor;
    // Lower bound for the next element search. Bounded to the ENCLOSING container so a
    // child is found inside its parent; a monotonic cursor alone would jump past it and
    // both mis-slice and scan forward quadratically.
    size_t sliceFloor;
    // Monotonic counter giving every captured region a unique position in the source.
    uint64_t nextSourceOrder;
    // Number of objects whose source bytes had to be REGENERATED from the DOM instead
    // of sliced from the source. Non-zero means a no-edit round trip will not be byte
    // identical, and the acceptance test reports it rather than hiding it.
    mutable size_t fidelityRegenerated;

    ReaderContext(core::Document& d, const CdxmlReadOptions& o,
                  CdxmlReadReport* r, RefusalReport* f, GateMode g)
        : document(d), options(o), report(r), refusals(f), gate(g),
          labelSize(10.0), captionSize(10.0), fontTableSeen(false), objectCount(0),
          source(0), sourceCursor(0), sliceFloor(0), nextSourceOrder(0),
          fidelityRegenerated(0) {}

    Error refuse(const std::string& element, const std::string& attribute,
                 const std::string& reason, int32_t sourceId = 0,
                 ErrorCode code = ErrorCode::Unsupported) {
        if (refusals) {
            Refusal item;
            item.path = path;
            item.element = element;
            item.attribute = attribute;
            item.reason = reason;
            item.sourceId = sourceId;
            refusals->add(item);
        }
        return fail(reason, code);
    }
};

// ---------------------------------------------------------------------------
// Effort-preserving attribute gate
// ---------------------------------------------------------------------------
// Nothing may be quietly dropped, but a reader is not required to PROMOTE
// everything. The gate is the single place that decision is made, so it is
// testable and reversible:
//
//   kGateStrict   - a non-promoted attribute aborts the read with a refusal
//                   naming the element, the attribute and the object id.
//   kGatePreserve - the attribute is kept verbatim in the extension bag under
//                   pst.attr.<Name>.
//
// Default is kGatePreserve: a factually correct refusal on every unknown
// attribute would make the reader useless on real files while still being
// lossless. The read report counts them so a caller can see what happened.
Error applyAttributeGate(ReaderContext& ctx, const XMLElement* element,
                         const AttrTable& table, const ObjectRef& ref,
                         bool scopeObject) {
    for (const tinyxml2::XMLAttribute* a = element->FirstAttribute(); a; a = a->Next()) {
        if (table.isAllowed(a->Name())) continue;
        const std::string name = a->Name() ? a->Name() : "";
        const std::string value = a->Value() ? a->Value() : "";
        const std::string elementName = element->Name() ? element->Name() : "?";
        if (ctx.gate == kGateStrict)
            return ctx.refuse(elementName, name,
                              "attribute " + elementName + "." + name +
                              " is not promoted by this reader");
        if (!ctx.options.preserveUnknowns)
            return ctx.refuse(elementName, name,
                              "attribute " + name +
                              " would be dropped and preserving unknowns is disabled");
        const std::string key = std::string(kExtensionPrefix) + "attr." + name;
        if (scopeObject && ref.valid()) ctx.document.extensions(ref).set(key, value);
        else ctx.document.documentExtensions().set(key, value);
        if (ctx.report) ++ctx.report->unknownAttributesPreserved;
    }
    return Error::success();
}

// ---------------------------------------------------------------------------
// Source ID registry
// ---------------------------------------------------------------------------
Error declareId(ReaderContext& ctx, const XMLElement* element, bool mandatory,
                int32_t* out) {
    const char* raw = element->Attribute("id");
    const std::string elementName = element->Name() ? element->Name() : "?";
    if (!raw) {
        if (mandatory)
            return ctx.refuse(elementName, "id", "object has no CDXML id");
        *out = 0;
        return Error::success();
    }
    int32_t value = 0;
    Error r = parseId(raw, &value);
    if (!r.ok()) return r;
    // ID uniqueness follows the vendor rule: unique within a CONTAINER, and merely
    // recommended document-wide. A repeat inside the same container is genuinely
    // ambiguous (a reference could mean either object) and is refused; a repeat across
    // two containers is legal and real ChemDraw files contain it, so it is counted and
    // allowed. ctx.containerIds is reset for each container the reader enters.
    if (!ctx.containerIds.insert(value).second) {
        if (ctx.report) ++ctx.report->duplicateSourceIds;
        return ctx.refuse(elementName, "id",
                          "duplicate CDXML object id inside one container", value,
                          ErrorCode::AlreadyExists);
    }
    ctx.usedIds.insert(value);
    *out = value;
    return Error::success();
}

// ---------------------------------------------------------------------------
// Text (<t> and its <s> runs)
// ---------------------------------------------------------------------------
struct TextPiece {
    std::vector<core::TextRun> runs;
    double       size;
    uint32_t     color;
    std::string  fontName;
    int          align;
    bool         hasAlign;
    double       width;
    bool         hasWidth;
    TextPiece() : size(0.0), color(0xFF000000u), align(0), hasAlign(false),
                  width(0.0), hasWidth(false) {}
};

// <s> runs. face bits follow the vendor CDXString enumeration; size and font
// are resolved through the font table so the model carries a NAME, not an index
// that would dangle after a paste into another document.
Error readTextRuns(ReaderContext& ctx, const XMLElement* element, double defaultSize,
                   const ObjectRef& ref, TextPiece* out, bool scopeObject,
                   const std::string& pathPrefix) {
    AttrTable table;
    const char* const allowed[] = { "id", "p", "BoundingBox", "Justification",
                                    "LineHeight", "LineStarts", "CaptionLineHeight",
                                    "TextStrikethrough", "MoleculeStudioTextWidth",
                                    "MoleculeStudioNameLink" };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        table.allow(allowed[i]);
    Error r = applyAttributeGate(ctx, element, table, ref, scopeObject);
    if (!r.ok()) return r;

    // Wrap width of a text box. ChemDraw has no public attribute for this, so the
    // value travels under the vendor-prefixed name this app already uses for its
    // other private attributes; other readers ignore it and keep their own box.
    if (const char* rawWidth = element->Attribute("MoleculeStudioTextWidth")) {
        double width = 0.0;
        Error widthError = parseNumber(rawWidth, "text width", &width);
        if (!widthError.ok()) return widthError;
        if (width > 0.0 && std::isfinite(width)) { out->hasWidth = true; out->width = width; }
    }

    if (const char* justification = element->Attribute("Justification")) {
        core::TextAlign align = core::TextAlign::Left;
        bool known = true;
        if (textAlignFromCdxml(justification, &align, &known)) {
            out->hasAlign = true;
            out->align = static_cast<int>(align);
        } else if (scopeObject && ref.valid()) {
            ctx.document.extensions(ref).set("pst.attr.Justification", justification);
        }
    }

    out->size = defaultSize;
    bool hasRun = false;
    for (const XMLElement* run = element->FirstChildElement(); run;
         run = run->NextSiblingElement()) {
        if (std::string(run->Name()) != "s")
            return ctx.refuse(element->Name() ? element->Name() : "?", "child",
                              std::string("unsupported text child <") + run->Name() + ">");
        AttrTable runTable;
        const char* const runAllowed[] = { "font", "size", "face", "color", "charset" };
        for (size_t i = 0; i < sizeof(runAllowed) / sizeof(runAllowed[0]); ++i)
            runTable.allow(runAllowed[i]);
        r = applyAttributeGate(ctx, run, runTable, ref, scopeObject);
        if (!r.ok()) return r;
        if (run->FirstChildElement())
            return ctx.refuse("s", "child", "nested run formatting is not promoted yet");

        core::TextRun textRun;
        textRun.flags = ref.kind == ObjectKind::Atom && element->Parent() && element->Parent()->ToElement() && std::string(element->Parent()->ToElement()->Name())=="n" ? core::kFormula : 0;
        const char* inheritedFace=element->Attribute("LabelFace");
        if(!inheritedFace && element->Parent() && element->Parent()->ToElement())inheritedFace=element->Parent()->ToElement()->Attribute("LabelFace");
        if(inheritedFace)textRun.flags=textFlagsFromFace(std::atoi(inheritedFace));
        textRun.size = defaultSize;
        if (const char* face = run->Attribute("face")) {
            long long faceValue = 0;
            r = parseInteger(face, "text face", 0, 255, &faceValue);
            if (!r.ok()) return r;
            textRun.flags = textFlagsFromFace(static_cast<int>(faceValue));
            if ((static_cast<int>(faceValue) & kFaceUnmappedMask) && scopeObject && ref.valid()) {
                std::ostringstream key;
                key << "pst.face.raw." << ref.id << "." << out->runs.size();
                ctx.document.extensions(ref).set(key.str(), face);
            }
        }
        if(ref.kind==ObjectKind::Text){const char* warning=element->Attribute("Warning");if(warning&&std::string(warning).find("Chemical Interpretation is not possible")!=std::string::npos)textRun.flags&=~core::kFormula;}
        if (const char* size = run->Attribute("size")) {
            double runSize = 0.0;
            r = parseNumber(size, "text size", &runSize);
            if (!r.ok()) return r;
            if (runSize < 0.0)
                return ctx.refuse("s", "size", "negative text size", 0,
                                  ErrorCode::InvalidText);
            textRun.size = runSize;
            out->size = runSize;
        }
        if (const char* font = run->Attribute("font")) {
            int32_t fontId = 0;
            r = parseId(font, &fontId);
            if (!r.ok()) return r;
            std::map<uint32_t, std::string>::const_iterator found =
                ctx.fontNames.find(static_cast<uint32_t>(fontId));
            if (found != ctx.fontNames.end()) {
                textRun.fontName = found->second;
                out->fontName = found->second;
            }
        }
        if (const char* color = run->Attribute("color")) {
            uint32_t rgba = 0;
            if (ctx.color(color, &rgba)) {
                out->color = rgba;
                if (ctx.report) ++ctx.report->colors;
            } else if (scopeObject && ref.valid()) {
                ctx.document.extensions(ref).set("pst.attr.color", color);
            }
        }

        std::string value;
        for (const tinyxml2::XMLNode* child = run->FirstChild(); child;
             child = child->NextSibling()) {
            if (child->ToText()) value += child->Value() ? child->Value() : "";
            else if (child->ToComment()) continue;
            else if (child->ToElement()) {
                tinyxml2::XMLPrinter printer;
                child->Accept(&printer);
                if (scopeObject && ref.valid()) {
                    std::ostringstream key;
                    key << "pst.run.markup." << ref.id << "." << out->runs.size();
                    ctx.document.extensions(ref).set(key.str(),
                                                    printer.CStr() ? printer.CStr() : "");
                }
            }
        }
        Error valid = core::validateUtf8(value);
        if (!valid.ok()) return valid;
        textRun.utf8 = value;
        out->runs.push_back(textRun);
        if (ctx.report) ++ctx.report->textRuns;
        hasRun = true;
    }
    if (!hasRun) {
        core::TextRun empty;
        empty.size = defaultSize;
        out->runs.push_back(empty);
    }
    out->size = out->runs[0].size;
    (void)pathPrefix;
    return Error::success();
}



// ---------------------------------------------------------------------------
// Deferred value substitution
// ---------------------------------------------------------------------------
// A CDXML file may reference an object defined LATER (a bond before its atoms,
// SupersededBy pointing forward). The reader therefore never resolves a
// reference while walking the tree; it records the reference and resolves it in
// a final pass when every declaration is known. That also makes a dangling
// reference detectable rather than sometimes-visible.
void recordRef(RawRegion* region, const XMLElement* element, const char* attribute) {
    int32_t value = 0;
    if (!parseId(element->Attribute(attribute), &value).ok()) return;
    region->refs.push_back(std::make_pair(std::string(attribute), value));
    if (std::strcmp(attribute, "id") == 0) region->sourceId = value;
}

// ---------------------------------------------------------------------------
// Raw region capture
// ---------------------------------------------------------------------------
// Slice one element's EXACT source bytes.
//
// Two strategies, in order of fidelity:
//   1. Exact slice. Find the element's opening tag in the source at or after a cursor
//      that only moves forward (the DOM is walked in document order, so the cursor is
//      correct and the search is linear overall). The matching close is found by a
//      depth-aware scan that skips comments, CDATA and attributes containing '>'.
//      This preserves the source's indentation, attribute spacing and quoting.
//   2. XMLPrinter fallback. Used only when the slice cannot be located (a synthetic
//      document with no source, or a pathological tag). It normalises whitespace, so
//      a no-edit round trip would NOT be byte identical - which is exactly why the
//      fallback is reported in the fidelity counters rather than hidden.
// Instrumentation used only by an off-by-default diagnostic build.

bool sliceSource(ReaderContext& ctx, const XMLElement* element,
                 size_t* begin, size_t* end) {
    if (!ctx.source) return false;
    const std::string& bytes = *ctx.source;
    std::string open = "<";
    open += element->Name() ? element->Name() : "";
    // A cheap DISAMBIGUATOR: the element's own id attribute, if it has one. Two <n>
    // elements share a tag name, so searching for "<n" alone would keep finding the same
    // occurrence and the depth scan would walk forward from there every time - which is
    // what made a 5,000-atom document take seconds. Searching for the id makes each
    // lookup start at the right element directly.
    std::string needle = open;
    if (const char* id = element->Attribute("id")) {
        needle += " id=\"";
        needle += id;
        needle += "\"";
    }

    // Match the element's actual ID even when attributes span multiple lines
    // or have a different order. A bare-tag fallback used to re-use the FIRST
    // node/bond repeatedly, producing duplicate IDs on an edited ChemDraw file.
    size_t at=bytes.find(open,ctx.sliceFloor);
    const char* wanted=element->Attribute("id");
    while(at!=std::string::npos){
        size_t after=at+open.size();bool boundary=after<bytes.size()&&(std::isspace(static_cast<unsigned char>(bytes[after]))||bytes[after]=='>'||bytes[after]=='/');
        if(!boundary){at=bytes.find(open,after);continue;}
        size_t close=after;char quote=0;for(;close<bytes.size();++close){char c=bytes[close];if(quote){if(c==quote)quote=0;}else if(c==34||c==39)quote=c;else if(c==62)break;}
        if(close>=bytes.size())return false;
        if(!wanted)break;
        std::string tag=bytes.substr(at,close-at+1);if(tag.size()>1&&tag[tag.size()-2]!='/')tag.insert(tag.size()-1,"/");
        tinyxml2::XMLDocument candidate;if(candidate.Parse(tag.c_str())==tinyxml2::XML_SUCCESS){auto node=candidate.RootElement();auto id=node?node->Attribute("id"):nullptr;if(id&&std::strcmp(id,wanted)==0)break;}
        at=bytes.find(open,close+1);
    }
    if(at==std::string::npos)return false;
    // Start the slice at the CURSOR, not at the '<'. The cursor sits just after the
    // previous element's end (or the container's '>'), so it includes the whitespace
    // and indentation that precedes this element - which is part of the file and must
    // come back unchanged.
    // The slice starts at the cursor when the cursor still points into the whitespace
    // before this element, and at the tag otherwise. The FLOOR is the enclosing
    // container's start, so a child element is always found inside its parent even
    // though the parent's own slice already advanced the outer cursor.
    const size_t sliceBegin = (ctx.sourceCursor <= at && ctx.sourceCursor >= ctx.sliceFloor)
                                  ? ctx.sourceCursor : at;

    // Depth-aware scan for the matching close tag.
    size_t depth = 0;
    size_t scan = at;
    while (scan < bytes.size()) {
        const size_t next = bytes.find('<', scan);
        if (next == std::string::npos) return false;
        if (bytes.compare(next, 4, "<!--") == 0) {
            const size_t close = bytes.find("-->", next);
            if (close == std::string::npos) return false;
            scan = close + 3;
            continue;
        }
        if (bytes.compare(next, 9, "<![CDATA[") == 0) {
            const size_t close = bytes.find("]]>", next);
            if (close == std::string::npos) return false;
            scan = close + 3;
            continue;
        }
        if (bytes.compare(next, 2, "</") == 0) {
            const size_t close = bytes.find(62, next);
            if (close == std::string::npos) return false;
            if (depth == 0) { *begin = sliceBegin; *end = close + 1; return true; }
            --depth;
            scan = close + 1;
            continue;
        }
        if (bytes.compare(next, 2, "<?") == 0 || bytes.compare(next, 2, "<!") == 0) {
            const size_t close = bytes.find(62, next);
            if (close == std::string::npos) return false;
            scan = close + 1;
            continue;
        }
        // An opening tag. Find its end, honouring quotes so a '>' inside an attribute
        // value does not terminate the tag early.
        size_t cursor = next + 1;
        char quote = 0;
        while (cursor < bytes.size()) {
            const char c = bytes[cursor];
            if (quote) { if (c == quote) quote = 0; }
            else if (c == 34 || c == 39) quote = c;
            else if (c == 62) break;
            ++cursor;
        }
        if (cursor >= bytes.size()) return false;
        const bool selfClosing = bytes[cursor - 1] == '/';
        // The TARGET's own opening tag must NOT raise the depth: the scan begins at it,
        // so counting it would make the scan consume one sibling too many.
        if (next != at && !selfClosing) ++depth;
        if (next == at && selfClosing) { *begin = sliceBegin; *end = cursor + 1; return true; }
        scan = cursor + 1;
    }
    return false;
}

void captureRegion(ReaderContext& ctx, const XMLElement* element, RawRegion* region) {
    // The source ORDER is fixed HERE, at capture time, because capture happens in
    // document order while attach and push happen later (a bond is resolved after the
    // whole file is read). Taking the order anywhere else gives several objects the
    // same number and the file comes back reordered.
    if (!region->orderAssigned) {
        // A DEDICATED monotonic counter, not ctx.regions.size(): several regions can be
        // captured before any of them is pushed (a node captures its own region and then
        // its label child's), so the push count is not a unique sequence number.
        region->order = static_cast<int>(ctx.nextSourceOrder++);
        region->orderAssigned = true;
    }
    size_t begin = 0, end = 0;
    const size_t savedFloor = ctx.sliceFloor;
    if (sliceSource(ctx, element, &begin, &end)) {
        region->bytes = ctx.source->substr(begin, end - begin);
        // Advance the outer cursor, and let any CHILD of this element search from the
        // element's own start. An element's children are visited immediately after it,
        // so leaving the cursor past the whole subtree would make a child's lookup find
        // a later sibling instead.
        ctx.sourceCursor = end;
        // A child of this element will be visited immediately after it; it must search
        // from this element's START, not from the cursor that is now past the subtree.
        ctx.sliceFloor = begin;
        return;
    }
    ctx.sliceFloor = savedFloor;
    tinyxml2::XMLPrinter printer(0, true, 0);
    element->Accept(&printer);
    region->bytes = printer.CStr() ? printer.CStr() : "";
    if (ctx.fidelityRegenerated <= (1u << 20)) ++ctx.fidelityRegenerated;
}

// Store an unmodelled element verbatim, together with the position it occupied in the
// source, so the writer can put it back between the same neighbours it had.
void storeRawRegion(ReaderContext& ctx, RawRegion* region) {
    ctx.document.documentExtensions().set(rawKey(region->sourceId), region->bytes);
    std::ostringstream orderKey;
    orderKey << kExtensionPrefix << "raw.order." << region->sourceId;
    std::ostringstream orderValue;
    orderValue << region->order;
    ctx.document.documentExtensions().set(orderKey.str(), orderValue.str());
    if (ctx.report) ++ctx.report->rawElements;
}

// Capture a CHILD element (an atom's label <t>) without disturbing the parent's cursor:
// the parent already consumed the whole subtree, and its siblings must continue from
// there.
void captureChildRegion(ReaderContext& ctx, const XMLElement* element, RawRegion* region) {
    const size_t savedCursor = ctx.sourceCursor;
    // A child is part of its parent's region, so it must NOT consume an order number:
    // doing so would leave a gap and push every following sibling's order out by one.
    region->orderAssigned = true;
    region->order = -1;
    captureRegion(ctx, element, region);
    region->orderAssigned = false;   // the region is discarded; only its bytes are kept
    ctx.sourceCursor = savedCursor;
}

// The load-time signature of an object, computed with the SAME function the
// writer uses, so "unchanged" is a fact rather than an assumption.
std::string currentSignature(const core::Document& document, const core::ObjectRef& ref) {
    const core::AtomRecord* atom = document.atoms().get(ref.id);
    if (atom && ref.kind == ObjectKind::Atom) return atomSignature(document, *atom);
    const core::BondRecord* bond = document.bonds().get(ref.id);
    if (bond && ref.kind == ObjectKind::Bond) return bondSignature(*bond);
    const core::TextRecord* text = document.texts().get(ref.id);
    if (text && ref.kind == ObjectKind::Text) return textSignature(document, *text);
    const core::PathRecord* path = document.paths().get(ref.id);
    if (path && ref.kind == ObjectKind::Path) return pathSignature(document, *path);
    const core::ObjectSlab<core::AnnotationRecord>* slab =
        ref.kind == ObjectKind::Arrow ? &document.arrows() :
        ref.kind == ObjectKind::Bracket ? &document.brackets() :
        ref.kind == ObjectKind::Orbital ? &document.orbitals() :
        ref.kind == ObjectKind::Symbol ? &document.symbols() :
        ref.kind == ObjectKind::BioShape ? &document.bioShapes() : 0;
    if (slab) {
        const core::AnnotationRecord* annotation = slab->get(ref.id);
        if (annotation) return annotationSignature(*annotation);
    }
    return std::string();
}


void sourceAppearance(ReaderContext& ctx,const XMLElement* element,ObjectRef ref){
    auto& bag=ctx.document.extensions(ref);
    if(ref.kind==ObjectKind::Atom&&element->Attribute("MoleculeStudioHydrogens")){bag.set("studio.hydrogens.manual","1");auto atom=ctx.document.atoms().get(ref.id);if(atom)ctx.document.setAtomLabel(ref.id,core::elementSymbol(atom->element));}
    uint32_t color=0x000000ff;ctx.color(element->Attribute("color"),&color);bag.set("studio.color",std::to_string(color));
    if(auto z=element->Attribute("Z"))bag.set("studio.z",z);
    if(auto v=element->Attribute("Visible"))bag.set("studio.visible",v);
    auto t=std::string(element->Name())=="t"?element:element->FirstChildElement("t");
    if(t){std::ostringstream colors;for(auto run=t->FirstChildElement("s");run;run=run->NextSiblingElement("s")){uint32_t c=color;if(run->Attribute("color"))ctx.color(run->Attribute("color"),&c);colors<<c<<' ';}bag.set("studio.text.colors",colors.str());
        for(const char* key:{"Justification","LabelJustification","LabelAlignment","RotationAngle","LineHeight","LineStarts","Visible"})if(auto v=t->Attribute(key))bag.set(std::string("studio.text.")+key,v);
    }
    if(std::string(element->Name()?element->Name():"")=="t")if(const char* link=element->Attribute("MoleculeStudioNameLink"))bag.set("studio.nameLink.raw",link);
    const std::string name=element->Name();if(!element->Attribute("MoleculeStudioPath")&&(name=="graphic"||name=="curve"||name=="embeddedobject")){tinyxml2::XMLPrinter out;element->Accept(&out);bag.set("studio.cdxml.graphic",out.CStr());}
}

// Mark the region belonging to an editable object and stash its bytes on that
// object, so a patched export can re-emit the parts this reader never modelled.
void attachSource(ReaderContext& ctx, const ObjectRef& ref, const RawRegion& region) {
    if (!ref.valid()) return;
#ifdef R62_NO_ATTACH
    (void)ctx; (void)region;
    return;
#endif
    // Source ORDER. The fixture (and real files) interleave nodes, bonds and text; the
    // slabs group them by kind. Re-emitting in slab order would reorder the file even
    // though nothing changed, so the order is recorded and used for output.
    {
        // Small integer-to-decimal conversion. An ostringstream per object allocates on
        // construction and this runs once per object per read, so it is avoided; the
        // decimal spelling is produced directly.
        char buffer[16];
        int value = region.order;
        int length = 0;
        if (value <= 0) {
            buffer[length++] = '0';
        } else {
            char reversed[16];
            int digits = 0;
            while (value > 0) { reversed[digits++] = static_cast<char>('0' + value % 10); value /= 10; }
            while (digits > 0) buffer[length++] = reversed[--digits];
        }
        ctx.document.extensions(ref).set(std::string(kExtensionPrefix) + "source.order",
                                         std::string(buffer, buffer + length));
    }
    ctx.document.extensions(ref).set(sourceKey(ref.kind, ref.id), region.bytes);
    // The signature is the load-time snapshot the writer compares against to
    // decide whether the source bytes are still valid for this object.
    // NOTE: the signature is NOT stored here. It is stored in a FINAL pass after the
    // whole document is read, because fields such as the atom label and its text runs
    // are attached after this point. A signature taken too early would differ from the
    // writer's and every object would be regenerated instead of re-emitted.
    if (ctx.report) ++ctx.report->objectsImported;
    std::ostringstream order;
    for (size_t i = 0; i < region.refs.size(); ++i) {
        if (i) order << ",";
        order << region.refs[i].first;
    }
    ctx.document.extensions(ref).set(attrOrderKey(ref.kind, ref.id), order.str());
#ifndef R62_NO_BIND
    for (size_t i = 0; i < region.refs.size(); ++i)
        ctx.sourceIds.bind(region.refs[i].second, ref);
#endif
}


// ---------------------------------------------------------------------------
// <n> node
// ---------------------------------------------------------------------------
// PROMOTED node attributes. Everything else is preserved, never dropped.
// NodeType IS promoted: nickname / R-group / query nodes keep their kind and
// their label, which is what stops an abbreviation from degenerating into a
// bare carbon on a round trip.
Error readNode(ReaderContext& ctx, const XMLElement* element) {
    RawRegion region;
    captureRegion(ctx, element, &region);
    recordRef(&region, element, "id");

    AttrTable table;
    const char* const allowed[] = {
        "id", "p", "Element", "NumHydrogens", "NodeType", "Charge", "Isotope",
        "Radical", "LabelDisplay", "LabelFont", "LabelSize", "LabelFace",
        "LabelColor", "LineWidth", "Color", "BoundingBox", "Warning",
        "ExternalConnectionType", "ExternalConnectionNum", "Z", "Geometry",
        "AS", "Alkyl", "AttachedLabels", "SupersededBy"
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        table.allow(allowed[i]);
    Error r = applyAttributeGate(ctx, element, table, ObjectRef(), false);
    if (!r.ok()) return r;

    // The id goes through declareId so that the CONTAINER-scoped uniqueness rule
    // applies to nodes exactly as it does to bonds and text. Parsing it locally would
    // make a duplicate node id invisible while duplicate bond ids were refused.
    int32_t sourceId = 0;
    r = declareId(ctx, element, true, &sourceId);
    if (!r.ok()) return r;

    core::Vec2 position;
    r = parsePosition(element->Attribute("p"), &position);
    if (!r.ok()) return r;

    long long elementNumber = 6;
    if (element->Attribute("Element")) {
        r = parseInteger(element->Attribute("Element"), "Element", 1, 118, &elementNumber);
        if (!r.ok()) return r;
    }

    core::AtomSemantics semantics;
    if (element->Attribute("NodeType")) {
        core::NodeType nodeType = core::NodeType::Element;
        if (!nodeTypeFromCdxml(element->Attribute("NodeType"), &nodeType))
            return ctx.refuse("n", "NodeType",
                              std::string("unknown NodeType \"") +
                              element->Attribute("NodeType") + "\"", sourceId);
        semantics.nodeType = static_cast<uint8_t>(nodeType);
        if(std::string(element->Attribute("NodeType"))=="GenericNickname")semantics.nodeType=static_cast<uint8_t>(core::NodeType::Query);
    }
    if (element->Attribute("Charge")) {
        long long charge = 0;
        r = parseInteger(element->Attribute("Charge"), "Charge", -15, 15, &charge);
        if (!r.ok()) return r;
        semantics.charge = static_cast<int8_t>(charge);
    }
    if (element->Attribute("Isotope")) {
        long long isotope = 0;
        r = parseInteger(element->Attribute("Isotope"), "Isotope", 0, 1000, &isotope);
        if (!r.ok()) return r;
        semantics.isotope = static_cast<int16_t>(isotope);
    }
    if (element->Attribute("NumHydrogens")) {
        long long hydrogens = 0;
        r = parseInteger(element->Attribute("NumHydrogens"), "NumHydrogens", 0, 8, &hydrogens);
        if (!r.ok()) return r;
        semantics.implicitHydrogens = static_cast<int8_t>(hydrogens);
    }
    if (element->Attribute("Radical")) {
        uint8_t radical = 0;
        if (!radicalFromCdxml(element->Attribute("Radical"), &radical))
            return ctx.refuse("n", "Radical",
                              std::string("unknown Radical \"") +
                              element->Attribute("Radical") + "\"", sourceId);
        semantics.radical = radical;
    }
    if (element->Attribute("LabelDisplay")) {
        const std::string v = lower(element->Attribute("LabelDisplay"));
        if (v == "shown" || v == "1") semantics.showLabel = true;
    }

    core::AtomRecord record;
    record.position = position;
    record.element = static_cast<int16_t>(elementNumber);
    record.semantics = semantics;
    record.sourceId = sourceId;

    core::ObjectId id = core::kInvalidObjectId;
    r = ctx.document.addAtomRecord(record, &id);
    if (!r.ok()) return r;
    ++ctx.objectCount;
    if (ctx.report) ++ctx.report->nodes;

    const ObjectRef ref(ObjectKind::Atom, id);
    {std::ostringstream value;value.precision(12);value<<position.x<<' '<<position.y;ctx.document.extensions(ref).set("studio.atom.source.xy",value.str());}
    region.ref = ref;
    if(auto xyz=element->Attribute("xyz")){std::istringstream input(xyz);double x,y,z;if(input>>x>>y>>z&&std::isfinite(z))ctx.document.extensions(ref).set("studio.atom.z",std::to_string(z));}
    if(auto number=element->Attribute("AtomNumber"))if(element->Attribute("ShowAtomNumber","yes"))ctx.document.extensions(ref).set("studio.atom.number",number);

    // Object tags are annotations on an atom, not replacement atom labels.
    // Keep their positions relative to the atom, so moving it moves the tag.
    tinyxml2::XMLDocument tags;auto tagsRoot=tags.NewElement("tags");tags.InsertEndChild(tagsRoot);
    for(auto child=element->FirstChildElement("objecttag");child;child=child->NextSiblingElement("objecttag")){
        auto text=child->FirstChildElement("t");if(!text)continue;
        TextPiece piece;r=readTextRuns(ctx,text,ctx.labelSize,ref,&piece,true,"t");if(!r.ok())return r;
        core::Vec2 anchor=position;if(text->Attribute("p")){r=parsePosition(text->Attribute("p"),&anchor);if(!r.ok())return r;}
        auto tag=tags.NewElement("tag");tag->SetAttribute("dx",anchor.x-position.x);tag->SetAttribute("dy",anchor.y-position.y);
        if(auto name=child->Attribute("Name"))tag->SetAttribute("name",name);
        uint32_t tagColor=0x000000ff;auto first=text->FirstChildElement("s");ctx.color(first?first->Attribute("color"):text->Attribute("color"),&tagColor);tag->SetAttribute("color",tagColor);
        for(size_t i=0;i<piece.runs.size();++i){auto run=tags.NewElement("s");const auto& value=piece.runs[i];
            run->SetAttribute("font",value.fontName.c_str());run->SetAttribute("size",value.size);run->SetAttribute("flags",unsigned(value.flags));run->SetText(value.utf8.c_str());tag->InsertEndChild(run);
        }
        tagsRoot->InsertEndChild(tag);
    }
    if(tagsRoot->FirstChildElement()){tinyxml2::XMLPrinter printer;tags.Print(&printer);ctx.document.extensions(ref).set("studio.atom.tags",printer.CStr());}

    // The label: an <n> may carry a <t> child holding the drawn text. For an
    // element node the text is the element symbol; for a Nickname / RGroup /
    // Query node it is the abbreviation, and THAT is what must survive.
    for (const XMLElement* child = element->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        const std::string childName = child->Name() ? child->Name() : "";
        if (childName == "fragment") {
            tinyxml2::XMLPrinter printer; child->Accept(&printer);
            ctx.document.extensions(ref).set("studio.nickname.fragment",printer.CStr());
            continue;
        }
        if(childName=="objecttag")continue;
        const XMLElement* labelElement = child;
        if(childName!="t")return ctx.refuse("n","child",std::string("unsupported node child <")+childName+">",sourceId);
        RawRegion labelRegion;
        captureChildRegion(ctx, child, &labelRegion);
        recordRef(&labelRegion, child, "id");
        TextPiece piece;
        r = readTextRuns(ctx, labelElement, ctx.labelSize, ref, &piece, true, "t");
        if (!r.ok()) return r;
        std::string label;
        for (size_t i = 0; i < piece.runs.size(); ++i) label += piece.runs[i].utf8;
        ctx.document.setAtomLabelQuiet(id, label);
        if(elementNumber==6&&label=="C"){semantics.showLabel=true;ctx.document.setAtomSemantics(id,semantics);}
        studioatom::set(ctx.document,id,piece.runs);
        if(const char* p=labelElement->Attribute("p")){
            std::istringstream input(p);double x,y;if(input>>x>>y){std::ostringstream v;v.precision(12);v<<x-position.x<<' '<<y-position.y;
                ctx.document.extensions(ref).set("studio.label.offset",v.str());
                ctx.document.extensions(ref).set("studio.label.text",label);
            }
        }
        if (!labelRegion.bytes.empty()) {
            std::ostringstream key;
            key << "pst.label.src." << id;
            ctx.document.extensions(ref).set(key.str(), labelRegion.bytes);
        }
        for (size_t i = 0; i < labelRegion.refs.size(); ++i)
            ctx.sourceIds.bind(labelRegion.refs[i].second, ref);
        break;
    }

    sourceAppearance(ctx,element,ref);
    attachSource(ctx, ref, region);
    if (!region.orderAssigned) {
        region.order = static_cast<int>(ctx.regions.size());
        region.orderAssigned = true;
    }
    ctx.regions.push_back(region);
    return Error::success();
}



// ---------------------------------------------------------------------------
// <b> bond (deferred: endpoints may be defined later in the file)
// ---------------------------------------------------------------------------
void readBondDeferred(ReaderContext& ctx, const XMLElement* element) {
    RawRegion region;
    captureRegion(ctx, element, &region);
    recordRef(&region, element, "id");
    recordRef(&region, element, "B");
    recordRef(&region, element, "E");
    // The source ORDER is fixed at push time. A bond is resolved later (its endpoints
    // may be defined further down the file), so taking the order at attachSource time
    // would give every bond the order of the LAST object read.
    region.order = static_cast<int>(ctx.regions.size());
    if (!region.orderAssigned) {
        region.order = static_cast<int>(ctx.regions.size());
        region.orderAssigned = true;
    }
    ctx.regions.push_back(region);
    ctx.pendingBonds.push_back(element);
    if (ctx.report) ++ctx.report->bonds;
}

Error resolveBond(ReaderContext& ctx, const XMLElement* element, RawRegion* region) {
    AttrTable table;
    const char* const allowed[] = {
        "id", "B", "E", "Order", "Display", "Stereo", "DoublePosition",
        "DoubleBondSpacing", "BS", "LineWidth", "BoldWidth", "HashSpacing", "Color", "BondCircularOrdering",
        "BoundingBox", "SupersededBy", "Begin", "End"
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        table.allow(allowed[i]);
    Error r = applyAttributeGate(ctx, element, table, ObjectRef(), false);
    if (!r.ok()) return r;

    int32_t sourceId = 0;
    r = declareId(ctx, element, true, &sourceId);
    if (!r.ok()) return r;

    const char* beginText = element->Attribute("B") ? element->Attribute("B")
                                                     : element->Attribute("Begin");
    const char* endText = element->Attribute("E") ? element->Attribute("E")
                                                   : element->Attribute("End");
    if (!beginText || !endText)
        return ctx.refuse("b", beginText ? "E" : "B", "bond has no endpoints", sourceId);
    int32_t beginSource = 0, endSource = 0;
    r = parseId(beginText, &beginSource);
    if (!r.ok()) return r;
    r = parseId(endText, &endSource);
    if (!r.ok()) return r;

    const ObjectRef beginRef = ctx.sourceIds.refFor(beginSource);
    const ObjectRef endRef = ctx.sourceIds.refFor(endSource);
    if (!beginRef.valid() || !endRef.valid()) {
        if (ctx.report) ++ctx.report->danglingReferences;
        return ctx.refuse("b", beginRef.valid() ? "E" : "B",
                          "bond references an unknown atom id", sourceId,
                          ErrorCode::NotFound);
    }

    core::BondRecord record;
    record.beginAtom = beginRef.id;
    record.endAtom = endRef.id;
    core::BondSemantics semantics = core::BondSemantics::Normal;
    core::BondOrder order = core::BondOrder::Single;
    if (element->Attribute("Order")) {
        if (!bondOrderFromCdxml(element->Attribute("Order"), &order, &semantics))
            return ctx.refuse("b", "Order",
                              std::string("unknown bond Order \"") +
                              element->Attribute("Order") +
                              "\"; refusing rather than guessing a multiplicity",
                              sourceId, ErrorCode::InvalidSemantics);
    }
    core::BondDisplay display = core::BondDisplay::Normal;
    if (element->Attribute("Display")) {
        if (!bondDisplayFromCdxml(element->Attribute("Display"), &display))
            return ctx.refuse("b", "Display",
                              std::string("unknown bond Display \"") +
                              element->Attribute("Display") + "\"", sourceId);
        std::string direction=lower(element->Attribute("Display"));
        if(direction=="wedgeend"||direction=="wedgedhashend"||direction=="hollowwedgeend"||direction=="wavywedgeend"||direction=="4"||direction=="7"||direction=="10"||direction=="12")std::swap(record.beginAtom,record.endAtom);
    }
    core::BondStereo stereo = core::BondStereo::None;
    if (element->Attribute("Stereo")) {
        if (!bondStereoFromCdxml(element->Attribute("Stereo"), &stereo))
            return ctx.refuse("b", "Stereo",
                              std::string("unknown bond Stereo \"") +
                              element->Attribute("Stereo") + "\"", sourceId);
    }
    // A Wedge/Hash display IS stereo information; folding it into the stereo
    // axis as well keeps the two axes consistent when a renderer reads only one.
    if (stereo == core::BondStereo::None) {
        if (display == core::BondDisplay::Wedge) stereo = core::BondStereo::Up;
        else if (display == core::BondDisplay::WedgeHash) stereo = core::BondStereo::Down;
        else if (display == core::BondDisplay::Wavy) stereo = core::BondStereo::Either;
    }
    record.order = order;
    record.display = display;
    record.semantics = semantics;
    record.stereo = stereo;
    record.sourceId = sourceId;

    if (order == core::BondOrder::Aromatic &&
        semantics != core::BondSemantics::Aromatic &&
        semantics != core::BondSemantics::Query)
        return ctx.refuse("b", "Order",
                          "aromatic order without an aromatic/query semantics axis",
                          sourceId, ErrorCode::InvalidSemantics);

    core::ObjectId id = core::kInvalidObjectId;
    r = ctx.document.addBondRecord(record, &id);
    if (!r.ok()) return r;
    ++ctx.objectCount;

    const ObjectRef ref(ObjectKind::Bond, id);
    for(const char* key:{"DoublePosition","DoubleBondSpacing","LineWidth","BoldWidth","HashSpacing"})if(const char* value=element->Attribute(key))ctx.document.extensions(ref).set(std::string("pst.attr.")+key,value);
    sourceAppearance(ctx,element,ref);
    if (region) {
        region->ref = ref;
        attachSource(ctx, ref, *region);
    }
    if (ctx.report) ++ctx.report->objectsImported;
    return Error::success();
}

// ---------------------------------------------------------------------------
// <t> free text
// ---------------------------------------------------------------------------
Error readFreeText(ReaderContext& ctx, const XMLElement* element) {
    RawRegion region;
    captureRegion(ctx, element, &region);
    recordRef(&region, element, "id");

    int32_t sourceId = 0;
    Error r = declareId(ctx, element, true, &sourceId);
    if (!r.ok()) return r;

    core::Vec2 position;
    r = parsePosition(element->Attribute("p"), &position);
    if (!r.ok()) return r;

    TextPiece piece;
    r = readTextRuns(ctx, element, ctx.captionSize, ObjectRef(), &piece, false, "t");
    if (!r.ok()) return r;

    core::TextRecord record;
    record.position = position;
    record.size = piece.size;
    record.role = core::TextRole::Caption;
    record.sourceId = sourceId;
    record.format.fontName = piece.fontName;
    record.format.size = piece.size;
    record.format.colorRGBA = piece.color;
    // Explicit source line starts are zero-based UTF-8 byte offsets. Retain run
    // boundaries (and their colours) while reproducing ChemDraw's soft wraps.
    if(const char* starts=element->Attribute("LineStarts")){
        std::set<size_t> breaks;std::istringstream in(starts);size_t n;while(in>>n)if(n>0)breaks.insert(n);
        size_t pos=0;char previous=0;
        for(auto& run:piece.runs){std::string text;for(size_t k=0;k<run.utf8.size();){unsigned char c=run.utf8[k];size_t bytes=c<128?1:c<224?2:c<240?3:4;
            if(breaks.count(pos)&&previous!='\n'&&c!='\n')text+='\n';text.append(run.utf8,k,bytes);previous=char(c);pos+=bytes;k+=bytes;}run.utf8=text;}
    }

    if (piece.hasAlign) record.format.align = static_cast<uint8_t>(piece.align);

    core::ObjectId id = core::kInvalidObjectId;
    r = ctx.document.addTextRecord(record, &id);
    if (!r.ok()) return r;
    ctx.document.setRunsQuiet(id, piece.runs);
    ++ctx.objectCount;
    if (ctx.report) ++ctx.report->texts;

    const ObjectRef ref(ObjectKind::Text, id);
    if (piece.hasWidth && piece.width > 0.0 && std::isfinite(piece.width))
        ctx.document.extensions(ref).set("studio.text.width", std::to_string(piece.width));
    region.ref = ref;
    sourceAppearance(ctx,element,ref);
    attachSource(ctx, ref, region);
    if (!region.orderAssigned) {
        region.order = static_cast<int>(ctx.regions.size());
        region.orderAssigned = true;
    }
    ctx.regions.push_back(region);
    return Error::success();
}


// ---------------------------------------------------------------------------
// Graphic family
// ---------------------------------------------------------------------------
// VERIFIED SOURCE: vendor spec, properties/Graphic_Type.htm (kCDXProp_Graphic_Type
// 0x0A00). The enumeration is only:
//   0 Undefined  1 Line  2 Arc  3 Rectangle  4 Oval  5 Orbital  6 Bracket  7 Symbol
// There is no spline/Bezier/polygon GraphicType; closed/open paths are separate
// <curve> objects with Closed="yes|no" and a bit-encoded CurveType.
//
// CRITICAL COMPATIBILITY FACT: Arrow, Bracket, Orbital and Symbol are NOT element
// names. They are <graphic GraphicType="..."> values, with ArrowType / BracketType
// / OrbitalType / SymbolType as ATTRIBUTES. A reader that only accepts
// n/b/t/fragment/page refuses every real ChemDraw document that has an arrow on
// it. Real ChemDraw 26 output also contains a genuine <arrow> element (238 uses
// across 616 shipped sample files) plus <annotation> and <bioshape>, which the
// stale public spec does not enumerate at all.
enum GraphicFamily {
    kGraphicUnknown = 0,
    kGraphicUnsupported,   // recognised vendor type this reader cannot lay out yet
    kGraphicPath,          // Line / Rectangle / Oval
    kGraphicArrow,
    kGraphicBracket,
    kGraphicOrbital,
    kGraphicSymbol
};

// GraphicType is written as a NUMBER or as a name; both spellings occur.
GraphicFamily classifyGraphicType(const char* graphicType) {
    if (!graphicType || !*graphicType) return kGraphicUnsupported;   // Undefined
    const std::string v = lower(graphicType);
    if (v == "line" || v == "1") return kGraphicPath;
    if (v == "arc" || v == "2") return kGraphicPath;          // no arc layout yet
    if (v == "rectangle" || v == "3") return kGraphicPath;
    if (v == "oval" || v == "4") return kGraphicPath;
    if (v == "orbital" || v == "5") return kGraphicOrbital;
    if (v == "bracket" || v == "6") return kGraphicBracket;
    if (v == "symbol" || v == "7") return kGraphicSymbol;
    if (v == "undefined" || v == "0") return kGraphicUnsupported;
    return kGraphicUnknown;
}

// A <graphic> is an arrow when GraphicType is absent but ArrowType is present:
// real files do write the head type without a GraphicType.
GraphicFamily classifyGraphic(const XMLElement* element) {
    const GraphicFamily byType = classifyGraphicType(element->Attribute("GraphicType"));
    if (byType != kGraphicUnknown && byType != kGraphicUnsupported) return byType;
    if (element->Attribute("ArrowType")) return kGraphicArrow;
    if (element->Attribute("BracketType")) return kGraphicBracket;
    if (element->Attribute("OrbitalType")) return kGraphicOrbital;
    if (element->Attribute("SymbolType")) return kGraphicSymbol;
    return byType;
}

core::ObjectKind objectKindForFamily(GraphicFamily family) {
    switch (family) {
    case kGraphicPath: return ObjectKind::Path;
    case kGraphicArrow: return ObjectKind::Arrow;
    case kGraphicBracket: return ObjectKind::Bracket;
    case kGraphicOrbital: return ObjectKind::Orbital;
    case kGraphicSymbol: return ObjectKind::Symbol;
    default: return ObjectKind::Unknown;
    }
}

// VERIFIED SOURCE: Graphic_Type.htm, quoted verbatim by the research pass:
//   "Graphic objects are the only objects whose BoundingBox property has a
//    special meaning, representing a pair of points rather than a rectangle."
// So for a Line/Arrow the BoundingBox is the two ENDPOINTS; for Arc/Oval/
// Rectangle it is a box. Treating a line as a box doubles the drawn geometry.
void readGraphicBox(const XMLElement* element, bool asPair, core::Box* box,
                    core::Vec2 pair[2], bool* havePair, bool* haveBox) {
    *havePair = false;
    *haveBox = false;
    const char* raw = element->Attribute("BoundingBox");
    if (!raw) return;
    std::istringstream in(raw);
    in.imbue(std::locale::classic());
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!(in >> x0 >> y0 >> x1 >> y1)) return;
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1))
        return;
    pair[0] = core::Vec2(x0, y0);
    pair[1] = core::Vec2(x1, y1);
    *havePair = true;
    if (!asPair) {
        box->min = core::Vec2(std::min(x0, x1), std::min(y0, y1));
        box->max = core::Vec2(std::max(x0, x1), std::max(y0, y1));
        *haveBox = true;
    }
}

const char* graphicColorAttribute(const XMLElement* element) {
    const char* color = element->Attribute("Color");
    return color ? color : element->Attribute("color");
}

// <graphic>, <arrow> and <curve> all arrive here. The promoted attributes are the
// ones the model can hold; every other attribute goes through the attribute gate,
// which preserves it rather than dropping it.
Error readGraphic(ReaderContext& ctx, const XMLElement* element, const char* elementName) {
    // ChemDraw writes page metadata as <annotation Keyword="Name" Content="..."/>
    // WITHOUT an id. Those tags are not drawable objects and must not make the
    // whole document unreadable, so keep them verbatim under a synthetic,
    // collision-free key (real CDXML ids are positive).
    if (elementName && std::string(elementName) == "annotation" &&
        !element->Attribute("id")) {
        // Page metadata (GUID / page name), not a drawable object. ChemDraw
        // emits it at page level without an id. It is deliberately not stored
        // as a raw region: the raw-region stream is ordered among page OBJECTS,
        // and replaying page-level metadata from that stream emits it before a
        // fragment close and corrupts the page structure. Nothing visible is
        // lost; every drawing object is still read normally.
        return Error::success();
    }

    RawRegion region;
    captureRegion(ctx, element, &region);
    recordRef(&region, element, "id");

    int32_t sourceId = 0;
    Error r = declareId(ctx, element, true, &sourceId);
    if (!r.ok()) return r;

    AttrTable table;
    const char* const allowed[] = {
        "id", "GraphicType", "ArrowType", "BracketType", "OrbitalType", "SymbolType",
        "BoundingBox", "LineWidth", "Color", "color", "FillType", "FadePercent",
        "CornerRadius", "OvalType", "RectangleType", "SupersededBy", "Z",
        "RotationAngle", "HeadSize", "HeadWidth", "HeadCenterSize",
        "ArrowheadType", "ArrowHeadType", "ArrowHeadHead", "ArrowHeadTail",
        "Tail3D", "Head3D", "Center3D", "MajorAxisEnd3D", "MinorAxisEnd3D",
        "Closed", "CurveType", "CurvePoints", "BioShapeType", "DNAWaveLength",
        "DNAWaveHeight", "DNAWaveWidth", "DNAWaveOffset", "MembraneElementSize",
        "PrintTrimMarks", "xyz", "Visible"
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        table.allow(allowed[i]);
    r = applyAttributeGate(ctx, element, table, ObjectRef(), false);
    if (!r.ok()) return r;

    const GraphicFamily family = std::string(elementName)=="embeddedobject" ? kGraphicPath : std::string(elementName)=="curve" ? (element->Attribute("CurvePoints")?kGraphicPath:kGraphicUnknown) : classifyGraphic(element);
    const ObjectKind kind = objectKindForFamily(family);

    if (kind == ObjectKind::Unknown) {
        // Either an unrecognised type, or a recognised vendor type this reader
        // cannot lay out yet (Undefined, Arc, or the undocumented <bioshape>).
        // It is KEPT VERBATIM in the document bag in document order, so a patched
        // export writes it back byte for byte. It is neither dropped nor turned
        // into a line.
        if (!ctx.options.preserveUnknowns)
            return ctx.refuse(elementName, "GraphicType",
                              std::string("graphic type [") +
                              (element->Attribute("GraphicType")
                                   ? element->Attribute("GraphicType") : "unknown") +
                              "] is not modelled and preserving unknowns is disabled",
                              sourceId);
        storeRawRegion(ctx, &region);
        ctx.regions.push_back(region);
        return Error::success();
    }

    const std::string type = lower(element->Attribute("GraphicType")
                                       ? element->Attribute("GraphicType") : "");
    const bool asPair = (kind == ObjectKind::Arrow) || type == "line" || type == "arc";
    core::Box box;
    core::Vec2 pair[2];
    bool havePair = false, haveBox = false;
    readGraphicBox(element, asPair, &box, pair, &havePair, &haveBox);

    if (kind == ObjectKind::Path) {
        core::PathRecord record;
        if (haveBox) {
            record.points.push_back(box.min);
            record.points.push_back(core::Vec2(box.max.x, box.min.y));
            record.points.push_back(box.max);
            record.points.push_back(core::Vec2(box.min.x, box.max.y));
            record.pointKind.assign(4, 0);
        } else if (havePair) {
            record.points.push_back(pair[0]);
            record.points.push_back(pair[1]);
            record.pointKind.assign(2, 0);
        }
        record.closed = (type == "rectangle" || type == "oval");
        if(const char* curve=element->Attribute("CurvePoints")){record.points.clear();std::istringstream input(curve);double x,y;while(input>>x>>y)record.points.push_back(core::Vec2(x,y));record.pointKind.assign(record.points.size(),0);record.closed=(element->IntAttribute("CurveType")&1)!=0;}
        if(const char* vertices=element->Attribute("MoleculeStudioPath")){record.points.clear();std::istringstream input(vertices);double x,y;while(input>>x>>y)record.points.push_back(core::Vec2(x,y));record.pointKind.assign(record.points.size(),0);record.fillEnabled=(element->IntAttribute("CurveType")&128)!=0;record.fillRGBA=element->UnsignedAttribute("MoleculeStudioFill");}
        record.style.colorRGBA=0x000000ff;
        record.sourceId = sourceId;
        if (element->Attribute("LineWidth")) {
            double width = 0.0;
            r = parseNumber(element->Attribute("LineWidth"), "LineWidth", &width);
            if (!r.ok()) return r;
            record.style.width = width;
        }
        if (const char* color = graphicColorAttribute(element)) {
            uint32_t rgba = 0;
            if (ctx.color(color, &rgba)) record.style.colorRGBA = rgba;
        }
        core::ObjectId id = core::kInvalidObjectId;
        r = ctx.document.addPath(record, &id);
        if (!r.ok()) return r;
        ++ctx.objectCount;
        const ObjectRef ref(ObjectKind::Path, id);
        region.ref = ref;
        sourceAppearance(ctx,element,ref);
    attachSource(ctx, ref, region);
        if (!region.orderAssigned) {
        region.order = static_cast<int>(ctx.regions.size());
        region.orderAssigned = true;
    }
    ctx.regions.push_back(region);
        if (ctx.report) ++ctx.report->graphics;
        return Error::success();
    }

    core::AnnotationRecord record;
    record.kind = kind;
    record.sourceId = sourceId;
    if (element->Attribute("LineWidth")) {
        double width = 0.0;
        r = parseNumber(element->Attribute("LineWidth"), "LineWidth", &width);
        if (!r.ok()) return r;
        record.style.width = width;
    }
    if (const char* color = graphicColorAttribute(element)) {
        uint32_t rgba = 0;
        if (ctx.color(color, &rgba)) record.style.colorRGBA = rgba;
    }
    if (element->Attribute("RotationAngle")) {
        double rotation = 0.0;
        if (parseNumber(element->Attribute("RotationAngle"), "RotationAngle", &rotation).ok())
            record.rotation = rotation;
    }
    if (havePair) {
        record.geometry[0] = pair[0];
        record.geometry[1] = pair[1];
        record.geometry[2] = pair[1];
        record.geometry[3] = pair[1];
    }
    if (haveBox) {
        record.geometry[2] = box.max;
        record.geometry[3] = core::Vec2(box.min.x, box.max.y);
    }

    core::ObjectId id = core::kInvalidObjectId;
    r = ctx.document.addAnnotation(kind, record, &id);
    if (!r.ok()) return r;
    ++ctx.objectCount;
    const ObjectRef ref(kind, id);
    region.ref = ref;
    sourceAppearance(ctx,element,ref);
    attachSource(ctx, ref, region);
    if (!region.orderAssigned) {
        region.order = static_cast<int>(ctx.regions.size());
        region.orderAssigned = true;
    }
    ctx.regions.push_back(region);
    if (ctx.report) ++ctx.report->graphics;
    return Error::success();
}

// ---------------------------------------------------------------------------
// Structural path
// ---------------------------------------------------------------------------
// Every diagnostic carries a readable path, so a refusal is locatable in the
// user file instead of being an anonymous "import failed".
struct PathScope {
    ReaderContext& ctx;
    std::string    saved;
    PathScope(ReaderContext& c, const std::string& segment) : ctx(c), saved(c.path) {
        std::ostringstream next;
        next << saved << "/" << segment;
        ctx.path = next.str();
    }
    ~PathScope() { ctx.path = saved; }
};

std::string ordinal(const char* name, int index) {
    std::ostringstream out;
    out << (name && *name ? name : "?") << "[" << (index + 1) << "]";
    return out.str();
}

// ---------------------------------------------------------------------------
// fonttable
// ---------------------------------------------------------------------------
Error readFontTable(ReaderContext& ctx, const XMLElement* element) {
    AttrTable table;
    Error r = applyAttributeGate(ctx, element, table, ObjectRef(), false);
    if (!r.ok()) return r;

    int index = 0;
    for (const XMLElement* font = element->FirstChildElement(); font;
         font = font->NextSiblingElement(), ++index) {
        PathScope fontScope(ctx, ordinal(font->Name(), index));
        if (std::string(font->Name() ? font->Name() : "") != "font")
            return ctx.refuse(font->Name(), "element", "unsupported font table entry");
        AttrTable fontTable;
        fontTable.allow("id");
        fontTable.allow("charset");
        fontTable.allow("name");
        r = applyAttributeGate(ctx, font, fontTable, ObjectRef(), false);
        if (!r.ok()) return r;
        if (const XMLElement* child = font->FirstChildElement())
            return ctx.refuse("font", "child",
                              std::string("unsupported font entry child <") +
                              (child->Name() ? child->Name() : "?") + ">");
        int32_t id = 0;
        r = parseId(font->Attribute("id"), &id);
        if (!r.ok()) return r;
        if (ctx.fontNames.find(static_cast<uint32_t>(id)) != ctx.fontNames.end())
            return ctx.refuse("font", "id", "duplicate font id", id, ErrorCode::AlreadyExists);
        const char* name = font->Attribute("name");
        if (!name) return ctx.refuse("font", "name", "font entry has no name", id);
        Error valid = core::validateUtf8(std::string(name));
        if (!valid.ok()) return valid;
        ctx.fontNames[static_cast<uint32_t>(id)] = name;
        if (ctx.report) ++ctx.report->fonts;
    }

    // Font NAME -> index, exactly as sorted by std::map over the font id, is the
    // numbering the writer will reuse. Keeping it here means a reloaded document
    // reproduces the same font table without guessing.
    int position = 0;
    for (std::map<uint32_t, std::string>::const_iterator it = ctx.fontNames.begin();
         it != ctx.fontNames.end(); ++it, ++position) {
        std::ostringstream key;
        key << kExtensionPrefix << "font." << position;
        std::ostringstream value;
        value << it->first << "\x1f" << it->second;
        ctx.document.documentExtensions().set(key.str(), value.str());
    }
    return Error::success();
}

// ---------------------------------------------------------------------------
// Object dispatch shared by fragment / page / root
// ---------------------------------------------------------------------------
bool isGraphicLike(const std::string& name) {
    return name == "embeddedobject" || name == "graphic" || name == "arrow" || name == "curve" ||
           name == "bioshape" || name == "objecttag" || name == "represent" ||
           name == "annotation" || name == "group" || name == "table" ||
           name == "tlcplate" || name == "tlclane" || name == "tlcspot" ||
           name == "spectrum" || name == "scheme" || name == "step" ||
           name == "bracketedgroup" || name == "chemicalproperty";
}

// Read one child object. Returns false (with *handled=false) when the element is
// none of the object types this reader understands; the caller then preserves it.
Error readFragment(ReaderContext& ctx, const XMLElement* element);
Error readObjectChild(ReaderContext& ctx, const XMLElement* child,
                      const std::string& name, bool* handled) {
    *handled = true;
    if(name=="group"||name=="fragment")return readFragment(ctx,child);
    if (name == "n") return readNode(ctx, child);
    if (name == "b") { readBondDeferred(ctx, child); return Error::success(); }
    if (name == "t") return readFreeText(ctx, child);
    if (isGraphicLike(name)) return readGraphic(ctx, child, name.c_str());
    *handled = false;
    return Error::success();
}

// Keep an unrecognised element verbatim, in document order.
Error preserveUnknownChild(ReaderContext& ctx, const XMLElement* child,
                           const std::string& name) {
    if (!ctx.options.preserveUnknowns)
        return ctx.refuse(name, "element", "unsupported object " + name);
    RawRegion region;
    captureRegion(ctx, child, &region);
    recordRef(&region, child, "id");
    storeRawRegion(ctx, &region);
    ctx.regions.push_back(region);
    return Error::success();
}

// ---------------------------------------------------------------------------
// fragment
// ---------------------------------------------------------------------------
Error readFragment(ReaderContext& ctx, const XMLElement* element) {
    // A container boundary: sibling uniqueness restarts here.
    ctx.containerIds.clear();
    AttrTable table;
    const char* const allowed[] = { "id", "BoundingBox", "ConnectionTable",
                                    "Name", "SupersededBy", "Z", "ConnectionOrder" };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        table.allow(allowed[i]);
    Error r = applyAttributeGate(ctx, element, table, ObjectRef(), false);
    if (!r.ok()) return r;
    if (ctx.report) ++ctx.report->fragments;

    int index = 0;
    for (const XMLElement* child = element->FirstChildElement(); child;
         child = child->NextSiblingElement(), ++index) {
        const std::string name = child->Name() ? child->Name() : "?";
        PathScope childScope(ctx, ordinal(name.c_str(), index));
        bool handled = false;
        r = readObjectChild(ctx, child, name, &handled);
        if (!r.ok()) return r;
        if (handled) continue;
        r = preserveUnknownChild(ctx, child, name);
        if (!r.ok()) return r;
    }
    return Error::success();
}

// ---------------------------------------------------------------------------
// page
// ---------------------------------------------------------------------------
Error readPage(ReaderContext& ctx, const XMLElement* element, int pageIndex) {
    ctx.containerIds.clear();
    AttrTable table;
    const char* const allowed[] = {
        "id", "BoundingBox", "WidthPages", "HeightPages", "HeaderPosition",
        "FooterPosition", "PageHeight", "PageWidth", "PageOverlap", "Name",
        "SupersededBy", "PageDefinition", "PrintTrimMarks", "Z",
        // Poster canvas geometry (CDXML DTD) plus the per-page size this
        // application records so a page grid survives ChemDraw's rewriting.
        "DrawingSpace", "Width", "Height",
        "MoleculeStudioPageWidth", "MoleculeStudioPageHeight"
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        table.allow(allowed[i]);
    Error r = applyAttributeGate(ctx, element, table, ObjectRef(), false);
    if (!r.ok()) return r;
    if (ctx.report) ++ctx.report->pages;

    int32_t pageSourceId = 0;
    if (element->Attribute("id")) {
        long long value = 0;
        r = parseInteger(element->Attribute("id"), "id", 1, 0x7FFFFFFF, &value);
        if (!r.ok()) return r;
        pageSourceId = static_cast<int32_t>(value);
    }

    // Page membership is recorded per object, so a multi-page document does not
    // silently collapse into one page. The value is the ORIGINAL page element id.
    const size_t before = ctx.regions.size();
    int index = 0;
    for (const XMLElement* child = element->FirstChildElement(); child;
         child = child->NextSiblingElement(), ++index) {
        const std::string name = child->Name() ? child->Name() : "?";
        PathScope childScope(ctx, ordinal(name.c_str(), index));
        if (name == "fragment") { r = readFragment(ctx, child); if (!r.ok()) return r; continue; }
        bool handled = false;
        r = readObjectChild(ctx, child, name, &handled);
        if (!r.ok()) return r;
        if (handled) continue;
        r = preserveUnknownChild(ctx, child, name);
        if (!r.ok()) return r;
    }

    std::ostringstream pageValue;
    pageValue << pageSourceId;
    int fragmentOrdinal = 0;
    for (size_t i = before; i < ctx.regions.size(); ++i) {
        const ObjectRef& ref = ctx.regions[i].ref;
        if (!ref.valid()) continue;
        ctx.document.extensions(ref).set(std::string(kExtensionPrefix) + "page.index",
                                         pageValue.str());
        ctx.document.extensions(ref).set(std::string(kExtensionPrefix) + "page.ordinal",
                                         ordinal("fragment", fragmentOrdinal));
    }
    std::ostringstream test;
    test << kExtensionPrefix << "page." << (pageIndex + 1);
    ctx.document.documentExtensions().set(test.str(), pageValue.str());
    return Error::success();
}


// ---------------------------------------------------------------------------
// Root walk
// ---------------------------------------------------------------------------
// Capture everything before the first object element: the XML declaration, the
// DOCTYPE, the root open tag with ALL its attributes, the font table, and whatever
// whitespace the source used. This is the part the writer can reproduce exactly
// instead of regenerating, which is what makes a no-edit round trip byte exact.
// It is also the part that holds every root attribute this reader does not model.
Error capturePrologue(const std::string& bytes, const tinyxml2::XMLElement* root,
                      std::string* prologue, std::string* opening, size_t* end) {
    *prologue = std::string();
    *opening = std::string();
    // Locate the root element's opening tag in the SOURCE bytes, not in the DOM, so
    // the spelling, attribute order and quoting are the source file's.
    // "<CDXML" also appears inside "<!DOCTYPE CDXML SYSTEM ...", so the needle alone
    // is not enough. The root OPEN TAG is the one followed by whitespace, '>' or '/'.
    const std::string needle = "<CDXML";
    size_t at = std::string::npos;
    for (size_t probe = bytes.find(needle); probe != std::string::npos;
         probe = bytes.find(needle, probe + 1)) {
        const size_t after = probe + needle.size();
        if (after < bytes.size() && (bytes[after] == 32 || bytes[after] == 9 || bytes[after] == 10 || bytes[after] == 13 ||
                                     bytes[after] == 62 || bytes[after] == 47)) {
            at = probe;
            break;
        }
    }
    if (at == std::string::npos) return Error::success();
    const size_t tagEnd = bytes.find(62, at);
    if (tagEnd == std::string::npos)
        return Error(ErrorCode::Truncated, "root element is not terminated");
    *opening = bytes.substr(at, tagEnd - at + 1);
    // Extend through the font table too: the font table is a document-level table the
    // writer would otherwise have to regenerate, and regenerating it cannot
    // reproduce the source's charset spellings or ordering. Stop at the first <page,
    // <fragment or object element, whichever comes first.
    size_t stop = bytes.size();
    // Walk forward through the container open tags (<page ...>, <fragment ...>), which
    // are containers this model does not make objects for. Preserving them verbatim is
    // what keeps a multi-page file's structure and its own id numbering intact.
    size_t scan = tagEnd + 1;
    for (;;) {
        while (scan < bytes.size() && (bytes[scan] == 10 || bytes[scan] == 13 ||
                                       bytes[scan] == 32 || bytes[scan] == 9))
            ++scan;
        bool advanced = false;
        // Container tags to absorb, and complete non-container ELEMENTS to absorb
        // whole (their text included) - the font table is the important one, because
        // regenerating it cannot reproduce the source's charset spellings. Children of
        // an absorbed element are absorbed with it.
        const char* const containers[] = { "<page", "<fragment" };
        for (int i = 0; i < 2; ++i) {
            const size_t length = strlen(containers[i]);
            if (bytes.compare(scan, length, containers[i]) == 0) {
                const size_t close = bytes.find(62, scan);
                if (close == std::string::npos)
                    return Error(ErrorCode::Truncated, "container tag is not terminated");
                scan = close + 1;
                advanced = true;
                break;
            }
        }
        if (!advanced) {
            const char* const elements[] = { "fonttable", "colortable" };
            for (int i = 0; i < 2; ++i) {
                const std::string open = std::string("<") + elements[i];
                const std::string close = std::string("</") + elements[i] + ">";
                if (bytes.compare(scan, open.size(), open) == 0) {
                    const size_t startEnd=bytes.find('>',scan);
                    if(startEnd!=std::string::npos && startEnd>scan && bytes[startEnd-1]=='/') {
                        scan=startEnd+1;advanced=true;break;
                    }
                    const size_t end = bytes.find(close, scan);
                    if (end == std::string::npos)
                        return Error(ErrorCode::Truncated,
                                     std::string(elements[i]) + " is not terminated");
                    scan = end + close.size();
                    advanced = true;
                    break;
                }
            }
        }
        if (!advanced) break;
    }
    stop = scan;
    *prologue = bytes.substr(0, stop);
    *end = stop;
    (void)root;
    return Error::success();
}

Error readInto(const std::string& bytes, core::Document& document,
               CdxmlReadReport* report, RefusalReport* refusals,
               const CdxmlReadOptions& options) {
    Error valid = core::validateUtf8(bytes);
    if (!valid.ok()) return valid;
    Error entities = checkEntities(bytes);
    if (!entities.ok()) return entities;

    tinyxml2::XMLDocument xml(true, tinyxml2::PRESERVE_WHITESPACE);
    if (xml.Parse(bytes.data(), bytes.size()) != tinyxml2::XML_SUCCESS)
        return fail(std::string("XML parse failed: ") +
                    (xml.ErrorStr() ? xml.ErrorStr() : "unknown error"),
                    ErrorCode::ParseError);
    const XMLElement* root = xml.RootElement();
    if (!root || std::string(root->Name() ? root->Name() : "") != "CDXML")
        return fail("expected a CDXML root element", ErrorCode::ParseError);

    for (const tinyxml2::XMLNode* n = xml.FirstChild(); n; n = n->NextSibling()) {
        if (n == root || n->ToComment()) continue;
        if (n->ToDeclaration()) {
            std::string declaration = n->Value() ? n->Value() : "";
            for (size_t i = 0; i < declaration.size(); ++i)
                declaration[i] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(declaration[i])));
            if (declaration.find("encoding") != std::string::npos &&
                declaration.find("utf-8") == std::string::npos)
                return fail("only UTF-8 XML documents are supported",
                            ErrorCode::Unsupported);
            continue;
        }
        if (n->ToUnknown()) {
            const std::string value = n->Value() ? n->Value() : "";
            // An external DTD reference is accepted and IGNORED (TinyXML2 never
            // fetches it). An internal subset is refused outright: that is the
            // shape an entity-expansion or XXE attack takes.
            if (value.compare(0, 13, "DOCTYPE CDXML") != 0 ||
                (value.size() > 13 &&
                 !std::isspace(static_cast<unsigned char>(value[13]))) ||
                value.find(91) != std::string::npos ||
                value.find("ENTITY") != std::string::npos)
                return fail("unsupported XML declaration or internal DTD",
                            ErrorCode::Unsupported);
            continue;
        }
        return fail("unexpected XML document content", ErrorCode::ParseError);
    }

    // The default is kGatePreserve: an attribute this reader does not promote is KEPT
    // verbatim, which is still lossless, and refusing on every unknown attribute
    // would make the reader useless on real ChemDraw output while protecting
    // nothing. kGateStrict is the opt-in mode for callers that want a hard failure
    // and know their input stays inside the promoted vocabulary.
    GateMode gate = options.refuseUnmappedSemantics ? kGateStrict : kGatePreserve;
    ReaderContext ctx(document, options, report, refusals, gate);
    ctx.path = "/CDXML";
    ctx.source = &bytes;


    AttrTable rootTable;
    const char* const allowed[] = {
        "Name", "BoundingBox", "BondLength", "BondSpacing", "LineWidth",
        "BoldWidth", "HashSpacing", "MarginWidth", "LabelFont", "LabelSize",
        "LabelFace", "CaptionFont", "CaptionSize", "CaptionFace",
        "ShowTerminalCarbonLabels", "ShowNonTerminalCarbonLabels", "Color",
        "CreationProgram", "CreationDate", "ModificationProgram", "ModificationDate",
        "Fragment", "WindowPosition", "WindowSize", "SupersededBy", "MacPrintInfo",
        "WinPrintInfo", "PrintOptions", "RepaintList", "Cartridge", "Z"
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        rootTable.allow(allowed[i]);
    Error r = applyAttributeGate(ctx, root, rootTable, ObjectRef(), false);
    if (!r.ok()) return r;
    if (!onlyWhitespace(root))
        return ctx.refuse("CDXML", "content",
                          "unexpected character data directly under the root");

    std::string prologue, opening;
    size_t prologueEnd = 0;
    Error captured = capturePrologue(bytes, root, &prologue, &opening, &prologueEnd);
    if (!captured.ok()) return captured;
    document.documentExtensions().set(std::string(kExtensionPrefix) + "source.prologue", prologue);
    ctx.source = &bytes;
    ctx.sourceCursor = prologueEnd;

    document.documentExtensions().set(std::string(kExtensionPrefix) + "source.magic", "CDXML");
    document.documentExtensions().set(std::string(kExtensionPrefix) + "source.encoding",
                                      "UTF-8");

    if (const char* name = root->Attribute("Name")) document.setName(name);
    for(const char* key:{"BondSpacing","MarginWidth","LabelSize","LabelFace","BoldWidth","HashSpacing"})if(const char* value=root->Attribute(key))
        document.documentExtensions().set(std::string("studio.style.")+key,value);
    if (root->Attribute("BondLength")) {
        double value = 0.0;
        r = parseNumber(root->Attribute("BondLength"), "BondLength", &value);
        if (!r.ok()) return r;
        document.setDefaultBondLength(value);
    }
    if (root->Attribute("LineWidth")) {
        double value = 0.0;
        r = parseNumber(root->Attribute("LineWidth"), "LineWidth", &value);
        if (!r.ok()) return r;
        document.setDefaultLineWidth(value);
    }
    if (root->Attribute("CaptionSize")) {
        double value = 0.0;
        r = parseNumber(root->Attribute("CaptionSize"), "CaptionSize", &value);
        if (!r.ok()) return r;
        if (value < 0.0)
            return ctx.refuse("CDXML", "CaptionSize", "negative CaptionSize");
        ctx.captionSize = value;
        document.setDefaultTextSize(value);
    }
    if (root->Attribute("LabelSize")) {
        double value = 0.0;
        r = parseNumber(root->Attribute("LabelSize"), "LabelSize", &value);
        if (!r.ok()) return r;
        if (value <= 0.0)
            return ctx.refuse("CDXML", "LabelSize", "non-positive LabelSize");
        ctx.labelSize = value;
    }
    if (const char* font = root->Attribute("CaptionFont")) ctx.defaultFont = font;
    if (const char* font = root->Attribute("LabelFont")) ctx.defaultFont = font;
    

    ctx.palette[0]=0x000000ff;ctx.palette[1]=0xffffffff;
    if(auto table=root->FirstChildElement("colortable")){int index=2;for(auto c=table->FirstChildElement("color");c;c=c->NextSiblingElement("color"),++index){
        auto channel=[&](const char* key){return uint32_t(std::max(0.,std::min(255.,std::floor(c->DoubleAttribute(key)*255.+.5))));};
        ctx.palette[index]=(channel("r")<<24)|(channel("g")<<16)|(channel("b")<<8)|255;
    }}
    for(auto c:ctx.palette)document.documentExtensions().set("studio.palette."+std::to_string(c.first),std::to_string(c.second));
    // Two passes over the root children, exactly like the T01 adapter: fonts
    // must be known before any run references them, and a fragment may appear
    // before the fonttable in a hand-edited file.
    for (const XMLElement* child = root->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        if (std::string(child->Name() ? child->Name() : "") != "fonttable") continue;
        if (ctx.fontTableSeen)
            return ctx.refuse("fonttable", "element", "duplicate font table", 0,
                              ErrorCode::AlreadyExists);
        ctx.fontTableSeen = true;
        PathScope scope(ctx, "fonttable");
        r = readFontTable(ctx, child);
        if (!r.ok()) return r;
    }

        int pageIndex = 0;
    int childIndex = 0;
    if (!ctx.defaultFont.empty()) {
        char* end=0; unsigned long id=std::strtoul(ctx.defaultFont.c_str(),&end,10);
        if(end && !*end){auto it=ctx.fontNames.find(static_cast<uint32_t>(id));ctx.defaultFont=it==ctx.fontNames.end()?"Arial":it->second;}
        document.setDefaultFont(ctx.defaultFont);
    } else {
        // ChemDraw's default caption/label face is Arial. A file that omits
        // CaptionFont/LabelFont must not inherit this build's engine default
        // (Times New Roman), or every imported/pasted caption would be typeset
        // in a face the source document never asked for.
        document.setDefaultFont("Arial");
    }
    for (const XMLElement* child = root->FirstChildElement(); child;
         child = child->NextSiblingElement(), ++childIndex) {
        const std::string name = child->Name() ? child->Name() : "?";
        if (name == "fonttable" || name == "colortable") continue;
        PathScope scope(ctx, ordinal(name.c_str(), childIndex));
        if (name == "page") {
            r = readPage(ctx, child, pageIndex);
            if (!r.ok()) return r;
            ++pageIndex;
        } else if (name == "fragment") {
            // A fragment directly under the root (no page) is legal but rare.
            r = readFragment(ctx, child);
            if (!r.ok()) return r;
        } else if (name == "t") {
            r = readFreeText(ctx, child);
            if (!r.ok()) return r;
        } else if (isGraphicLike(name)) {
            r = readGraphic(ctx, child, name.c_str());
            if (!r.ok()) return r;
        } else if (name == "n") {
            r = readNode(ctx, child);
            if (!r.ok()) return r;
        } else if (name == "b") {
            readBondDeferred(ctx, child);
        } else {
            r = preserveUnknownChild(ctx, child, name);
            if (!r.ok()) return r;
        }
        if (ctx.objectCount > options.maxObjects)
            return fail("object count exceeds the configured limit",
                        ErrorCode::LimitExceeded);
    }

    // Deferred bond resolution: every node exists by now, so a reference is
    // either resolvable or provably dangling.
        // Bond resolution walks every pending bond once. The region for a bond is found
    // through an INDEX, not by scanning ctx.regions: a nested scan is O(bonds x regions),
    // which for a 5,000-atom chain is 50 million iterations and measured as seconds of
    // pure spin. The index is built once, from the same region list the reader filled.
    std::map<int32_t, RawRegion*> regionBySourceId;
    for (size_t k = 0; k < ctx.regions.size(); ++k) {
        if (ctx.regions[k].sourceId == 0) continue;
        std::map<int32_t, RawRegion*>::iterator existing =
            regionBySourceId.find(ctx.regions[k].sourceId);
        if (existing == regionBySourceId.end())
            regionBySourceId[ctx.regions[k].sourceId] = &ctx.regions[k];
    }
    for (size_t i = 0; i < ctx.pendingBonds.size(); ++i) {
        int32_t bondSourceId = 0;
        if (!parseId(ctx.pendingBonds[i]->Attribute("id"), &bondSourceId).ok())
            bondSourceId = 0;
        std::map<int32_t, RawRegion*>::iterator found = regionBySourceId.find(bondSourceId);
        RawRegion* region = found == regionBySourceId.end() ? 0 : found->second;
        r = resolveBond(ctx, ctx.pendingBonds[i], region);
        if (!r.ok()) return r;
    }
    // Final pass: record the load-time signature of every object the reader produced.
    // The writer compares against these to decide whether the preserved source bytes
    // are still valid for that object.
    for (size_t i = 0; i < ctx.regions.size(); ++i) {
        const ObjectRef& ref = ctx.regions[i].ref;
        if (!ref.valid()) continue;
        document.extensions(ref).set(signatureKey(ref.kind, ref.id),
                                     currentSignature(document, ref));
    }
    // The epilogue is everything after the last object the reader consumed: the
    // container close tags and the file's trailing whitespace. Preserving it is what
    // makes the no-edit round trip byte identical instead of "identical except for two
    // bytes of trailing newline".
    if (ctx.source && ctx.sourceCursor < ctx.source->size()) {
        const std::string epilogue = ctx.source->substr(ctx.sourceCursor);
        // Only keep it when the tail contains nothing but container close tags and
        // whitespace: a tail with real content is content the reader did not model, and
        // pretending to have preserved it would be worse than regenerating the close.
        // Scan TAG BY TAG, not character by character: the tail must consist only of
        // whitespace, container close tags and comments. Anything else is content the
        // reader did not model, and claiming to have preserved it would be a lie.
        bool structural = false;
        size_t scan = 0;
        while (scan < epilogue.size() && !structural) {
            const char c = epilogue[scan];
            if (c == 32 || c == 9 || c == 10 || c == 13) { ++scan; continue; }
            if (c != 60) { structural = true; break; }
            const size_t close = epilogue.find(62, scan);
            if (close == std::string::npos) { structural = true; break; }
            const std::string tag = epilogue.substr(scan, close - scan + 1);
            const bool allowed = tag.compare(0, 10, "</fragment") == 0 ||
                                 tag.compare(0, 6, "</page") == 0 ||
                                 tag.compare(0, 7, "</CDXML") == 0 ||
                                 tag.compare(0, 4, "<!--") == 0 ||
                                 // ChemDraw page metadata (GUID / page name) after
                                 // the last drawable object belongs to the tail and
                                 // must survive verbatim, exactly like a close tag.
                                 tag.compare(0, 11, "<annotation") == 0;
            if (!allowed) { structural = true; break; }
            scan = close + 1;
        }
        if (!structural)
            document.documentExtensions().set(std::string(kExtensionPrefix) + "source.epilogue",
                                              epilogue);
    }
    if (report) report->objectsImported = ctx.objectCount;
    return document.validate();
}

} // namespace


// ---------------------------------------------------------------------------
// Structure-to-name links
// ---------------------------------------------------------------------------
// The writer stores "digest;a1,b2" as MoleculeStudioNameLink. Members are CDXML
// file ids, so they are translated back to document ObjectIds here (after every
// object and its source id exists) and the caption's group is rebuilt. A file
// whose members no longer resolve keeps no link rather than a dangling one.
static void resolveNameLinks(core::Document& document) {
    std::map<int, core::ObjectRef> bySourceId;
    for (const auto& atom : document.atoms().records())
        if (atom.isAlive() && atom.sourceId > 0) bySourceId[atom.sourceId] = core::ObjectRef(core::ObjectKind::Atom, atom.id);
    for (const auto& bond : document.bonds().records())
        if (bond.isAlive() && bond.sourceId > 0) bySourceId[bond.sourceId] = core::ObjectRef(core::ObjectKind::Bond, bond.id);
    for (const auto& text : document.texts().records()) {
        if (!text.isAlive()) continue;
        const core::ObjectRef textRef(core::ObjectKind::Text, text.id);
        const core::ExtensionBag& bag = document.extensions(textRef);
        const std::string* raw = bag.findValue("studio.nameLink.raw");
        if (!raw) continue;
        const size_t separator = raw->find(';');
        std::string digest = separator == std::string::npos ? *raw : raw->substr(0, separator);
        std::vector<core::ObjectRef> resolved;
        if (separator != std::string::npos) {
            const std::vector<core::ObjectRef> fileRefs = namelink::decodeRefs(raw->substr(separator + 1));
            for (size_t i = 0; i < fileRefs.size(); ++i) {
                std::map<int, core::ObjectRef>::const_iterator found = bySourceId.find(int(fileRefs[i].id));
                if (found != bySourceId.end() && found->second.kind == fileRefs[i].kind) resolved.push_back(found->second);
            }
        }
        core::ExtensionBag& mutableBag = document.extensions(textRef);
        mutableBag.erase("studio.nameLink.raw");
        if (digest.empty() || resolved.empty()) continue;
        mutableBag.set("studio.nameLink.digest", digest);
        mutableBag.set("studio.nameLink.refs", namelink::encodeRefs(resolved));
        bool grouped = false;
        for (const auto& group : document.groups().records()) {
            if (!group.isAlive()) continue;
            for (size_t i = 0; i < group.members.size(); ++i) if (group.members[i] == textRef) { grouped = true; break; }
            if (grouped) break;
        }
        if (grouped) continue;
        std::vector<core::ObjectRef> members = namelink::topLevelMembers(document, resolved);
        members.push_back(textRef);
        if (members.size() < 2) continue;
        core::GroupRecord group;
        group.members = members;
        group.label = namelink::groupLabel();
        core::ObjectId groupId = core::kInvalidObjectId;
        document.addGroup(group, &groupId);
    }
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------
// Declared in persistence.hpp; everything above this point is internal.
core::Error readCdxml(const std::string& bytes, core::Document& document,
                      CdxmlReadReport* report, RefusalReport* refusals,
                      const CdxmlReadOptions& options) {
    if (document.totalSlotCount() != 0)
        return Error(ErrorCode::AlreadyExists, "Read target document is not empty");
    CdxmlReadReport local;
    RefusalReport localRefusals;
    // Parse into a scratch document: a refusal must leave the caller untouched.
    core::Document scratch;
    std::string input=bytes;repairUnreferencedArrowIds(input);std::vector<std::pair<graphics::TableData,std::string>> tables;
    std::vector<std::pair<experimental::PlateFigure,experimental::PlateFrame>> figures;std::vector<double> figureDiameters;std::vector<bool> figureRf;
    tinyxml2::XMLDocument tableXml;
    if(tableXml.Parse(input.c_str())==tinyxml2::XML_SUCCESS&&tableXml.RootElement()){
        std::function<Error(tinyxml2::XMLElement*)> scan=[&](tinyxml2::XMLElement* parent)->Error{
            for(auto node=parent->FirstChildElement();node;){auto next=node->NextSiblingElement();
                if(auto params=node->Attribute("MoleculeStudioTLC")){bool ok=false;auto figure=experimental::decodeFigure(params,&ok);if(!ok||!experimental::validateFigure(figure).ok())return Error(ErrorCode::ParseError,"Invalid TLC metadata");
                    experimental::PlateFrame frame;std::istringstream geometry(node->Attribute("MoleculeStudioFrame")?node->Attribute("MoleculeStudioFrame"):"");
                    if(!(geometry>>frame.centre.x>>frame.centre.y>>frame.axisU.x>>frame.axisU.y>>frame.axisV.x>>frame.axisV.y)||frame.degenerate()||!std::isfinite(frame.centre.x)||!std::isfinite(frame.centre.y)||!std::isfinite(frame.axisU.x)||!std::isfinite(frame.axisU.y)||!std::isfinite(frame.axisV.x)||!std::isfinite(frame.axisV.y))return Error(ErrorCode::ParseError,"Invalid TLC frame");
                    if(auto value=node->Attribute("BoundingBox")){core::Vec2 a,b;std::istringstream bounds(value);if(!(bounds>>a.x>>a.y>>b.x>>b.y)||!a.finite()||!b.finite())return Error(ErrorCode::ParseError,"Invalid TLC bounds");frame.centre=(a+b)*.5;}
                    double diameter=node->DoubleAttribute("MoleculeStudioDotSize",4.);if(!std::isfinite(diameter)||diameter<1||diameter>16)return Error(ErrorCode::ParseError,"Invalid TLC spot size");figureDiameters.push_back(diameter);figureRf.push_back(node->IntAttribute("MoleculeStudioShowRf",0)!=0);
                    figures.push_back({figure,frame});parent->DeleteChild(node);
                }else if(auto params=node->Attribute("MoleculeStudioTable")){bool ok=false;auto data=graphics::decodeTableParams(params,&ok);if(!ok)return Error(ErrorCode::ParseError,"Invalid table metadata");
                    if(auto bb=node->Attribute("BoundingBox")){std::istringstream b(bb);if(!(b>>data.grid.frame.min.x>>data.grid.frame.min.y>>data.grid.frame.max.x>>data.grid.frame.max.y))return Error(ErrorCode::ParseError,"Invalid table frame");}
                    tables.push_back({data,node->Attribute("MoleculeStudioStyle")?node->Attribute("MoleculeStudioStyle"):"grid"});parent->DeleteChild(node);
                }else{auto e=scan(node);if(!e.ok())return e;}node=next;
            }return Error();};
        auto e=scan(tableXml.RootElement());if(!e.ok())return e;
        if(!tables.empty()||!figures.empty()){tinyxml2::XMLPrinter out;tableXml.Print(&out);input=out.CStr();}
    }
    Error parsed = readInto(input, scratch, &local, &localRefusals, options);
    if (!parsed.ok()) {
        if (refusals) *refusals = localRefusals;
        return parsed;
    }
    if(!tables.empty()){
        std::map<int,core::ObjectId> textIds;for(const auto& text:scratch.texts().records())if(text.isAlive())textIds[text.sourceId]=text.id;
        for(auto& table:tables){auto& data=table.first;
            for(auto& cell:data.cells)if(cell.kind==graphics::CellKind::Text){auto found=textIds.find(int(cell.text));if(found==textIds.end())return Error(ErrorCode::ParseError,"Table cell text reference is missing");cell.text=found->second;scratch.texts().mutableGet(cell.text)->role=core::TextRole::TableCell;}
            core::ObjectId id;auto e=graphics::createTable(scratch,data.grid,&id);if(!e.ok())return e;e=graphics::writeTable(scratch,id,data);if(!e.ok())return e;
            scratch.extensions(core::ObjectRef(core::ObjectKind::Table,id)).set("studio.table.style",table.second);
        }
    }
    if(!figures.empty()){
        std::map<int,core::ObjectId> textIds;for(const auto& text:scratch.texts().records())if(text.isAlive())textIds[text.sourceId]=text.id;
        size_t figureIndex=0;for(auto& item:figures){for(auto& anchor:item.first.annotations)if(anchor.textId!=core::kInvalidObjectId){auto found=textIds.find(int(anchor.textId));if(found==textIds.end())return Error(ErrorCode::ParseError,"TLC annotation reference is missing");anchor.textId=found->second;}
            core::ObjectId id;auto e=experimental::addFigure(scratch,item.first,item.second,scratch.defaultLineWidth(),&id);if(!e.ok())return e;scratch.extensions(experimental::figureRef(id)).set("studio.tlc.spotDiameter",std::to_string(figureDiameters[figureIndex]));scratch.extensions(experimental::figureRef(id)).set("studio.tlc.showRf",figureRf[figureIndex++]?"1":"0");
        }
    }
    resolveNameLinks(scratch);
    core::Document::RawState state;
    scratch.captureAll(&state);
    Error adopted = document.restoreRaw(state);
    if (!adopted.ok()) return adopted;
    document.setName(scratch.name());
    if (report) *report = local;
    if (refusals) *refusals = localRefusals;
    return Error::success();
}

core::Error readCdxmlFile(const std::string& path, core::Document& document,
                          CdxmlReadReport* report, RefusalReport* refusals,
                          const CdxmlReadOptions& options) {
    std::string bytes;
    Error read = readFileBytes(path, &bytes);
    if (!read.ok()) return read;
    std::string localBytes = bytes;
    return readCdxml(localBytes, document, report, refusals, options);
}



} // namespace persistence
} // namespace r62

