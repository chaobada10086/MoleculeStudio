#include "cdxml_adapter.hpp"

#include "third_party/tinyxml2/tinyxml2.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace r62 {
namespace core {
namespace {

using tinyxml2::XMLElement;

Error fail(const std::string& message) {
    return Error(ErrorCode::Unsupported, "CDXML: " + message);
}
Error parseFailure(const std::string& message) {
    return Error(ErrorCode::ParseError, "CDXML: " + message);
}

const char* const kPredefinedEntities[] = { "amp", "lt", "gt", "quot", "apos" };

// TinyXML2 does not resolve named entities and must never be pointed at an
// external DTD. Reject unknown named entities up front instead of turning a DTD
// reference into literal text.
Error checkEntities(const std::string& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] != 38) continue;
        const size_t end = bytes.find(59, i + 1);
        if (end == std::string::npos || end - i > 32)
            return fail("unterminated or over-long XML entity");
        const std::string name = bytes.substr(i + 1, end - i - 1);
        bool predefined = false;
        for (size_t k = 0; k < 5; ++k)
            if (name == kPredefinedEntities[k]) { predefined = true; break; }
        if (predefined) { i = end; continue; }
        if (name.empty() || name[0] != 35)
            return fail("unknown named XML entity (external DTDs are never resolved)");
        unsigned base = 10;
        size_t pos = 1;
        if (pos < name.size() && name[pos] == 120) { base = 16; ++pos; }
        if (pos == name.size()) return fail("empty numeric XML entity");
        unsigned cp = 0;
        for (; pos < name.size(); ++pos) {
            const char c = name[pos];
            unsigned digit;
            if (c >= 48 && c <= 57) digit = static_cast<unsigned>(c - 48);
            else if (base == 16 && c >= 97 && c <= 102) digit = static_cast<unsigned>(c - 97 + 10);
            else if (base == 16 && c >= 65 && c <= 70) digit = static_cast<unsigned>(c - 65 + 10);
            else return fail("invalid numeric XML entity");
            if (digit >= base || cp > (0x10ffffu - digit) / base)
                return fail("invalid numeric XML entity");
            cp = cp * base + digit;
        }
        if (!(cp == 9 || cp == 10 || cp == 13 || (cp >= 0x20 && cp <= 0xd7ff) ||
              (cp >= 0xe000 && cp <= 0xfffd) || (cp >= 0x10000 && cp <= 0x10ffffu)))
            return fail("numeric entity is not an XML character");
        i = end;
    }
    return Error::success();
}

bool allowedAttribute(const std::string& list, const char* name) {
    return list.find(std::string(" ") + name + " ") != std::string::npos;
}

Error parseId(const char* input, uint32_t* out) {
    if (!input || !*input) return parseFailure("missing object ID/reference");
    for (const char* p = input; *p; ++p)
        if (*p < 48 || *p > 57) return parseFailure("ID must be a decimal integer");
    unsigned long long value = 0;
    std::istringstream in(input);
    in.imbue(std::locale::classic());
    if (!(in >> value) || value == 0 || value > 0xFFFFFFFFull)
        return parseFailure("invalid object ID");
    *out = static_cast<uint32_t>(value);
    return Error::success();
}

Error parseNumber(const char* input, const char* name, double* out) {
    if (!input) return parseFailure(std::string("missing ") + name);
    std::istringstream in(input);
    in.imbue(std::locale::classic());
    double value = 0.0;
    if (!(in >> value) || !std::isfinite(value))
        return parseFailure(std::string("invalid ") + name);
    in >> std::ws;
    if (!in.eof()) return parseFailure(std::string("invalid ") + name);
    *out = value;
    return Error::success();
}

Error parseInteger(const char* input, const char* name, long long minimum,
                   long long maximum, long long* out) {
    double value = 0.0;
    Error r = parseNumber(input, name, &value);
    if (!r.ok()) return r;
    if (value < static_cast<double>(minimum) || value > static_cast<double>(maximum) ||
        value != std::floor(value))
        return fail(std::string("unsupported ") + name);
    *out = static_cast<long long>(value);
    return Error::success();
}

Error parsePosition(const char* input, Vec2* out) {
    if (!input) return parseFailure("missing position");
    std::istringstream in(input);
    in.imbue(std::locale::classic());
    double x = 0.0, y = 0.0;
    if (!(in >> x >> y) || !std::isfinite(x) || !std::isfinite(y))
        return parseFailure("invalid position");
    in >> std::ws;
    if (!in.eof()) return fail("only two-dimensional positions are supported");
    out->x = x; out->y = y;
    return Error::success();
}

Error checkContent(const XMLElement* e, bool allowText) {
    for (const tinyxml2::XMLNode* n = e->FirstChild(); n; n = n->NextSibling()) {
        if (n->ToComment() || n->ToElement()) continue;
        if (n->ToText()) {
            if (allowText) continue;
            bool whitespace = true;
            for (const char* p = n->Value(); *p; ++p)
                if (!std::isspace(static_cast<unsigned char>(*p))) whitespace = false;
            if (whitespace) continue;
        }
        return fail(std::string("unexpected content in ") + e->Name());
    }
    return Error::success();
}

// Where an unmodelled attribute is preserved.
//
// A bare ObjectRef() means "this element is not an object yet" (an empty
// fragment, a fonttable), NOT "the document bag". Those two cases must be
// distinct: routing a fragment's attributes into the document bag would
// attribute them to the wrong scope, and silently dropping them is worse.
enum class ExtensionScope { Document, Object, Discard };

Error checkAttributes(const XMLElement* e, const std::string& list,
                      Document* document, const ObjectRef& ref,
                      CdxmlImportReport* report, bool recordUnknowns,
                      ExtensionScope scope) {
    for (const tinyxml2::XMLAttribute* a = e->FirstAttribute(); a; a = a->Next()) {
        if (allowedAttribute(list, a->Name())) continue;
        if (!recordUnknowns || !document || scope == ExtensionScope::Discard)
            return fail(std::string("unsupported attribute ") + e->Name() + "." + a->Name());
        const std::string key = std::string("cdxml.attr.") + a->Name();
        const std::string value = a->Value() ? a->Value() : "";
        if (scope == ExtensionScope::Document) document->documentExtensions().set(key, value);
        else document->extensions(ref).set(key, value);
        if (report) ++report->unknownAttributesRecorded;
    }
    return Error::success();
}

// ---------------------------------------------------------------------------
// Import context
// ---------------------------------------------------------------------------
struct ImportContext {
    Document&          document;
    CdxmlImportOptions options;
    CdxmlImportReport* report;
    CdxmlImportOptions unusedPadding;
    std::map<uint32_t, ObjectId> nodes;    // CDXML id -> internal atom id
    std::set<uint32_t>           usedIds;
    std::set<uint32_t>           fontIds;
    std::vector<const XMLElement*> bonds;
    std::vector< std::pair<ObjectId, int> > hydrogens;
    double labelSize;
    double captionSize;
    bool   fontTableSeen;

    ImportContext(Document& d, const CdxmlImportOptions& o, CdxmlImportReport* r)
        : document(d), options(o), report(r), unusedPadding(),
          labelSize(10.0), captionSize(10.0), fontTableSeen(false) {}

    Error registerId(const XMLElement* e, bool mandatory) {
        const char* raw = e->Attribute("id");
        if (!raw && !mandatory) return Error::success();
        uint32_t value = 0;
        Error r = parseId(raw, &value);
        if (!r.ok()) return r;
        if (!usedIds.insert(value).second) return parseFailure("duplicate object ID");
        return Error::success();
    }

    ObjectId internalId(uint32_t sourceId) const {
        std::map<uint32_t, ObjectId>::const_iterator it = nodes.find(sourceId);
        return it == nodes.end() ? kInvalidObjectId : it->second;
    }

    void recordUnknownElement(const ObjectRef& ref, const XMLElement* e) {
        if (!options.recordUnknowns) return;
        std::string text;
        for (const tinyxml2::XMLAttribute* a = e->FirstAttribute(); a; a = a->Next()) {
            if (!text.empty()) text += " ";
            text += a->Name();
            text += "=";
            text += a->Value() ? a->Value() : "";
        }
        document.extensions(ref).set(std::string("cdxml.node.") + e->Name(), text);
        if (report) ++report->unknownElementsRecorded;
    }

    // Attributes of a not-yet-created element are DISCARDED (they belong to a
    // container we do not model); document-level and object-level attributes are
    // preserved in their own bags.
    Error attributes(const XMLElement* e, const std::string& list, const ObjectRef& ref) {
        const ExtensionScope scope = ref.valid() ? ExtensionScope::Object
                                                 : ExtensionScope::Discard;
        return checkAttributes(e, list, &document, ref, report, options.recordUnknowns, scope);
    }
    Error documentAttributes(const XMLElement* e, const std::string& list) {
        return checkAttributes(e, list, &document, ObjectRef(), report,
                               options.recordUnknowns, ExtensionScope::Document);
    }

    Error text(const XMLElement* e, double defaultSize, bool label,
               const ObjectRef& ref, std::string* out, double* sizeOut) {
        Error r = attributes(e, " id  p  BoundingBox  Justification ", ref);
        if (!r.ok()) return r;
        r = checkContent(e, false);
        if (!r.ok()) return r;
        if (const char* justification = e->Attribute("Justification"))
            if (std::string(justification) != "Left" && std::string(justification) != "0")
                return fail("unsupported text justification");
        std::string value;
        double size = defaultSize;
        bool hasRun = false;
        for (const XMLElement* run = e->FirstChildElement(); run;
             run = run->NextSiblingElement()) {
            if (std::string(run->Name()) != "s")
                return fail("only plain text s runs are supported");
            r = attributes(run, " font  size  face ", ref);
            if (!r.ok()) return r;
            r = checkContent(run, true);
            if (!r.ok()) return r;
            if (run->FirstChildElement()) return fail("nested text formatting is unsupported");
            if (const char* face = run->Attribute("face")) {
                long long faceValue = 0;
                r = parseInteger(face, "text face", 0, 0, &faceValue);
                if (!r.ok()) return fail("unsupported text face");
            }
            double runSize = defaultSize;
            if (run->Attribute("size")) {
                r = parseNumber(run->Attribute("size"), "text size", &runSize);
                if (!r.ok()) return r;
            }
            if (runSize < 0.0) return fail("negative text size");
            if (hasRun && size != runSize) return fail("mixed-size text is unsupported");
            size = runSize;
            hasRun = true;
            for (const tinyxml2::XMLNode* child = run->FirstChild(); child;
                 child = child->NextSibling()) {
                if (child->ToText()) value += child->Value();
                else if (!child->ToComment()) return fail("unsupported text run content");
            }
        }
        if (!hasRun) return fail("text needs an s run");
        Error valid = validateUtf8(value);
        if (!valid.ok()) return valid;
        if (label && size != labelSize) return fail("custom element label size is unsupported");
        *out = value;
        *sizeOut = size;
        return Error::success();
    }

    Error freeText(const XMLElement* e) {
        Error r = registerId(e, true);
        if (!r.ok()) return r;
        ObjectRef ref;
        Vec2 position;
        r = parsePosition(e->Attribute("p"), &position);
        if (!r.ok()) return r;
        std::string value;
        double size = 0.0;
        r = text(e, captionSize, false, ref, &value, &size);
        if (!r.ok()) return r;
        ObjectId id = kInvalidObjectId;
        r = document.addText(position, size, value, &id);
        if (!r.ok()) return r;
        uint32_t sourceId = 0;
        r = parseId(e->Attribute("id"), &sourceId);
        if (!r.ok()) return r;
        TextRecord record;
        document.readText(id, &record);
        record.sourceId = static_cast<int32_t>(sourceId);
        document.writeText(record);
        if (report) ++report->textsImported;
        return Error::success();
    }

    Error node(const XMLElement* e) {
        Error r = registerId(e, true);
        if (!r.ok()) return r;
        ObjectRef ref;
        r = attributes(e, " id  p  Element  NumHydrogens  NodeType ", ref);
        if (!r.ok()) return r;
        r = checkContent(e, false);
        if (!r.ok()) return r;
        if (const char* type = e->Attribute("NodeType"))
            if (std::string(type) != "Element")
                return fail("only element nodes are supported (nickname/R-group/query refused)");
        Vec2 position;
        r = parsePosition(e->Attribute("p"), &position);
        if (!r.ok()) return r;
        long long element = 6;
        if (e->Attribute("Element")) {
            r = parseInteger(e->Attribute("Element"), "Element", 1, 118, &element);
            if (!r.ok()) return r;
        }
        ObjectId id = kInvalidObjectId;
        r = document.addAtom(position, static_cast<int>(element), &id);
        if (!r.ok()) return r;
        uint32_t sourceId = 0;
        r = parseId(e->Attribute("id"), &sourceId);
        if (!r.ok()) return r;
        nodes[sourceId] = id;
        {
            AtomRecord record;
            document.readAtom(id, &record);
            record.sourceId = static_cast<int32_t>(sourceId);
            document.writeAtom(record);
        }
        if (const char* h = e->Attribute("NumHydrogens")) {
            long long value = 0;
            r = parseInteger(h, "NumHydrogens", 0, 4, &value);
            if (!r.ok()) return r;
            hydrogens.push_back(std::make_pair(id, static_cast<int>(value)));
        }
        bool labelSeen = false;
        for (const XMLElement* child = e->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            if (std::string(child->Name()) != "t" || labelSeen)
                return fail("unsupported atom child (abbreviations refused)");
            labelSeen = true;
            r = registerId(child, true);
            if (!r.ok()) return r;
            if (const char* labelPosition = child->Attribute("p")) {
                Vec2 offset;
                r = parsePosition(labelPosition, &offset);
                if (!r.ok()) return r;
                if (offset.x != position.x || offset.y != position.y)
                    return fail("offset atom labels are unsupported");
            }
            std::string value;
            double size = 0.0;
            r = text(child, labelSize, true, ref, &value, &size);
            if (!r.ok()) return r;
            const char* symbol = elementSymbol(static_cast<int>(element));
            if (!symbol || value != symbol)
                return fail("atom label is not its element symbol (abbreviations refused)");
        }
        if (report) ++report->nodesImported;
        return Error::success();
    }

    Error fragment(const XMLElement* e) {
        Error r = registerId(e, false);
        if (!r.ok()) return r;
        ObjectRef ref;
        r = attributes(e, " id  BoundingBox ", ref);
        if (!r.ok()) return r;
        r = checkContent(e, false);
        if (!r.ok()) return r;
        for (const XMLElement* child = e->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            const std::string name = child->Name();
            if (name == "n") { r = node(child); if (!r.ok()) return r; }
            else if (name == "b") {
                r = registerId(child, true);
                if (!r.ok()) return r;
                r = attributes(child, " id  B  E  Order  Display ", ObjectRef());
                if (!r.ok()) return r;
                r = checkContent(child, false);
                if (!r.ok()) return r;
                if (child->FirstChildElement()) return fail("unsupported bond child");
                if (const char* display = child->Attribute("Display"))
                    if (std::string(display) != "Solid")
                        return fail("stereo/dashed/wedge bond display is unsupported");
                bonds.push_back(child);
            } else if (name == "t") { r = freeText(child); if (!r.ok()) return r; }
            else return fail("unsupported fragment object " + name);
        }
        return Error::success();
    }

    Error page(const XMLElement* e) {
        Error r = registerId(e, false);
        if (!r.ok()) return r;
        r = attributes(e,
            " id  BoundingBox  WidthPages  HeightPages  HeaderPosition  FooterPosition ",
            ObjectRef());
        if (!r.ok()) return r;
        r = checkContent(e, false);
        if (!r.ok()) return r;
        for (const XMLElement* child = e->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            const std::string name = child->Name();
            if (name == "fragment") { r = fragment(child); if (!r.ok()) return r; }
            else if (name == "t") { r = freeText(child); if (!r.ok()) return r; }
            else return fail("unsupported page object " + name);
        }
        return Error::success();
    }

    Error fonts(const XMLElement* e) {
        Error r = attributes(e, " ", ObjectRef());
        if (!r.ok()) return r;
        r = checkContent(e, false);
        if (!r.ok()) return r;
        for (const XMLElement* f = e->FirstChildElement(); f; f = f->NextSiblingElement()) {
            if (std::string(f->Name()) != "font") return fail("unsupported fonttable entry");
            r = attributes(f, " id  charset  name ", ObjectRef());
            if (!r.ok()) return r;
            r = checkContent(f, false);
            if (!r.ok()) return r;
            if (f->FirstChildElement()) return fail("unsupported font entry child");
            uint32_t id = 0;
            r = parseId(f->Attribute("id"), &id);
            if (!r.ok()) return r;
            if (!fontIds.insert(id).second) return parseFailure("duplicate font ID");
            if (const char* name = f->Attribute("name")) {
                std::ostringstream key;
                key << "cdxml.font." << id;
                document.documentExtensions().set(key.str(), name);
            }
            if (report) ++report->fontsSeen;
        }
        return Error::success();
    }

    // Bonds resolve after every node exists, because a bond may reference an atom
    // defined later in the file. Order is mapped to the ORDER axis only for 1/2/3;
    // anything else is refused rather than reinterpreted as an integer.
    Error finishBonds() {
        std::vector<int> valence(document.atoms().size(), 0);
        for (size_t i = 0; i < bonds.size(); ++i) {
            const XMLElement* e = bonds[i];
            uint32_t beginSource = 0, endSource = 0;
            Error r = parseId(e->Attribute("B"), &beginSource);
            if (!r.ok()) return r;
            r = parseId(e->Attribute("E"), &endSource);
            if (!r.ok()) return r;
            const ObjectId begin = internalId(beginSource);
            const ObjectId end = internalId(endSource);
            if (begin == kInvalidObjectId || end == kInvalidObjectId)
                return parseFailure("bond references an unknown atom ID");
            long long order = 1;
            if (e->Attribute("Order")) {
                r = parseInteger(e->Attribute("Order"), "Order", 1, 3, &order);
                if (!r.ok())
                    return fail("bond Order outside 1..3 is refused rather than stored as an "
                                "integer; express aromatic/dative/query through the semantics axis");
            }
            BondRecord record;
            record.beginAtom = begin;
            record.endAtom = end;
            record.order = static_cast<BondOrder>(order);
            record.display = BondDisplay::Normal;
            record.semantics = BondSemantics::Normal;
            record.stereo = BondStereo::None;
            uint32_t sourceId = 0;
            r = parseId(e->Attribute("id"), &sourceId);
            if (!r.ok()) return r;
            record.sourceId = static_cast<int32_t>(sourceId);
            ObjectId id = kInvalidObjectId;
            r = document.addBondRecord(record, &id);
            if (!r.ok()) return r;
            valence[begin] += static_cast<int>(order);
            valence[end] += static_cast<int>(order);
            if (report) ++report->bondsImported;
        }
        for (size_t i = 0; i < hydrogens.size(); ++i) {
            const AtomRecord* atom = document.atoms().get(hydrogens[i].first);
            if (!atom) continue;
            if (atom->element != 6 || hydrogens[i].second != 4 - valence[hydrogens[i].first])
                return fail("explicit NumHydrogens is only supported for a redundant "
                            "neutral saturated carbon");
        }
        return Error::success();
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------
namespace {

// Parse into whatever document it is given. Called ONLY with a scratch document
// by importCdxml(), so a refusal can never leave a half-imported document behind:
// the public entry point parses into scratch and moves the result in on success.
Error importCdxmlInto(const std::string& bytes, Document& document,
                      CdxmlImportReport* report, const CdxmlImportOptions& options) {
    Error valid = validateUtf8(bytes);
    if (!valid.ok()) return valid;
    Error entities = checkEntities(bytes);
    if (!entities.ok()) return entities;

    tinyxml2::XMLDocument xml(true, tinyxml2::PRESERVE_WHITESPACE);
    if (xml.Parse(bytes.data(), bytes.size()) != tinyxml2::XML_SUCCESS)
        return parseFailure(std::string("XML parse failed: ") +
                            (xml.ErrorStr() ? xml.ErrorStr() : "unknown error"));
    const XMLElement* root = xml.RootElement();
    if (!root || std::string(root->Name()) != "CDXML")
        return parseFailure("expected a CDXML root element");

    for (const tinyxml2::XMLNode* n = xml.FirstChild(); n; n = n->NextSibling()) {
        if (n == root || n->ToComment()) continue;
        if (n->ToDeclaration()) {
            std::string declaration = n->Value();
            for (size_t i = 0; i < declaration.size(); ++i)
                declaration[i] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(declaration[i])));
            if (declaration.find("encoding") != std::string::npos &&
                declaration.find("utf-8") == std::string::npos)
                return fail("only UTF-8 XML documents are supported");
            continue;
        }
        if (n->ToUnknown()) {
            const std::string value = n->Value();
            if (value.compare(0, 13, "DOCTYPE CDXML") != 0 ||
                (value.size() > 13 &&
                 !std::isspace(static_cast<unsigned char>(value[13]))) ||
                value.find(91) != std::string::npos ||
                value.find("ENTITY") != std::string::npos)
                return fail("unsupported XML declaration or internal DTD");
            continue;
        }
        return fail("unexpected XML document content");
    }

    // Only an empty document may be imported into: a partial import would mix
    // half of one file with half of another.
    if (document.totalSlotCount() != 0)
        return Error(ErrorCode::AlreadyExists, "Import target document is not empty");

    ImportContext context(document, options, report);
    // Root attributes describe the DOCUMENT, so an unmodelled one belongs in the
    // document-level extension bag.
    Error r = context.documentAttributes(root,
        " Name  BoundingBox  BondLength  BondSpacing  LineWidth  BoldWidth "
        " HashSpacing  MarginWidth  LabelFont  LabelSize  CaptionFont "
        " CaptionSize  ShowTerminalCarbonLabels  ShowNonTerminalCarbonLabels ");
    if (!r.ok()) return r;
    r = checkContent(root, false);
    if (!r.ok()) return r;
    for (size_t k = 0; k < 2; ++k) {
        const char* name = k == 0 ? "ShowTerminalCarbonLabels" : "ShowNonTerminalCarbonLabels";
        if (const char* value = root->Attribute(name))
            if (std::string(value) != "no")
                return fail("explicit carbon label display is unsupported");
    }
    if (root->Attribute("LabelSize")) {
        r = parseNumber(root->Attribute("LabelSize"), "LabelSize", &context.labelSize);
        if (!r.ok()) return r;
        if (context.labelSize != 10.0) return fail("custom element LabelSize is unsupported");
    }
    if (root->Attribute("CaptionSize")) {
        r = parseNumber(root->Attribute("CaptionSize"), "CaptionSize", &context.captionSize);
        if (!r.ok()) return r;
        if (context.captionSize < 0.0) return fail("negative CaptionSize");
    }
    if (root->Attribute("LineWidth")) {
        double width = 0.0;
        r = parseNumber(root->Attribute("LineWidth"), "LineWidth", &width);
        if (!r.ok()) return r;
        document.setDefaultLineWidth(width);
    }
    if (root->Attribute("BondLength")) {
        double length = 0.0;
        r = parseNumber(root->Attribute("BondLength"), "BondLength", &length);
        if (!r.ok()) return r;
        document.setDefaultBondLength(length);
    }
    if (root->Attribute("Name")) document.setName(root->Attribute("Name"));
    document.documentExtensions().set("cdxml.source.root", "CDXML");

    for (const XMLElement* child = root->FirstChildElement(); child;
         child = child->NextSiblingElement())
        if (std::string(child->Name()) == "fonttable") {
            if (context.fontTableSeen) return parseFailure("duplicate font table");
            context.fontTableSeen = true;
            r = context.fonts(child);
            if (!r.ok()) return r;
        }
    for (const XMLElement* child = root->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        const std::string name = child->Name();
        if (name == "fonttable") continue;
        if (name == "page") { r = context.page(child); if (!r.ok()) return r; }
        else return fail("unsupported root object " + name);
    }
    r = context.finishBonds();
    if (!r.ok()) return r;
    return document.validate();
}

} // namespace

// Public entry point. The caller's document is not touched unless the whole
// import succeeded, which is the same all-or-nothing guarantee the legacy
// loader provided and what the acceptance test asserts.
Error importCdxml(const std::string& bytes, Document& document,
                  CdxmlImportReport* report, const CdxmlImportOptions& options) {
    // Only an empty document may be imported into. The empty-target check runs
    // before anything else so a populated document is refused untouched.
    if (document.totalSlotCount() != 0)
        return Error(ErrorCode::AlreadyExists, "Import target document is not empty");
    // Keep the caller's identity and container settings; only contents move.
    Document scratch;
    scratch.setDefaultBondLength(document.defaultBondLength());
    scratch.setDefaultLineWidth(document.defaultLineWidth());
    scratch.setDefaultTextSize(document.defaultTextSize());
    scratch.setDefaultFont(document.defaultFont());

    CdxmlImportReport local;
    Error parsed = importCdxmlInto(bytes, scratch, &local, options);
    if (!parsed.ok()) return parsed;

    // Success: hand the parsed state to the caller's document, preserving the
    // caller's DocumentId so anything already holding a reference stays valid.
    Document::RawState state;
    scratch.captureAll(&state);
    Error adopted = document.restoreRaw(state);
    if (!adopted.ok()) return adopted;
    document.setName(scratch.name());
    if (report) *report = local;
    return Error::success();
}

Error importCdxmlFile(const std::string& path, Document& document,
                      CdxmlImportReport* report, const CdxmlImportOptions& options) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) return Error(ErrorCode::IoError, "Cannot open the CDXML file: " + path);
    std::string bytes;
    char buffer[65536];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize got = input.gcount();
        if (got <= 0) break;
        bytes.append(buffer, static_cast<size_t>(got));
    }
    if (input.bad()) return Error(ErrorCode::IoError, "CDXML file read failed");
    return importCdxml(bytes, document, report, options);
}

// ---------------------------------------------------------------------------
// Export (documented as LOSSY, not a round trip)
// ---------------------------------------------------------------------------
Error exportCdxml(const Document& document, std::ostream& output) {
    output.imbue(std::locale::classic());
    output << std::setprecision(17);
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
    output << "<CDXML Name=\"" << document.name() << "\" LineWidth=\""
           << document.defaultLineWidth() << "\" BondLength=\""
           << document.defaultBondLength() << "\">" << std::endl;
    output << "<fonttable><font id=\"3\" charset=\"utf-8\" name=\""
           << (document.defaultFont().empty() ? "Arial" : document.defaultFont())
           << "\"/></fonttable>" << std::endl;
    output << "<page id=\"1\">" << std::endl << "<fragment id=\"2\">" << std::endl;

    const std::vector<AtomRecord>& atoms = document.atoms().records();
    const std::vector<BondRecord>& bonds = document.bonds().records();
    const std::vector<TextRecord>& texts = document.texts().records();

    // CDXML ids must be positive and unique, so export renumbers them. This is
    // why export is a NEW document and not a round trip. The internal id stays
    // on the record; it is what undo/redo and host bindings keep using.
    std::vector<uint32_t> fileId(atoms.size(), 0);
    uint32_t nextFileId = 3;
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        fileId[i] = nextFileId++;
        output << "<n id=\"" << fileId[i] << "\" p=\"" << atoms[i].x() << ' '
               << atoms[i].y() << "\" Element=\""
               << atoms[i].element << "\"";
        if (atoms[i].semantics.charge != 0)
            output << " Charge=\"" << static_cast<int>(atoms[i].semantics.charge) << "\"";
        const std::string& label = document.atomLabel(atoms[i].id);
        if (label.empty()) output << "/>" << std::endl;
        else output << "><t><s font=\"3\" size=\"10\">" << label
                    << "</s></t></n>" << std::endl;
    }
    for (size_t i = 0; i < bonds.size(); ++i) {
        if (!bonds[i].isAlive()) continue;
        if (bonds[i].beginAtom >= fileId.size() || bonds[i].endAtom >= fileId.size())
            continue;
        if (!fileId[bonds[i].beginAtom] || !fileId[bonds[i].endAtom]) continue;
        output << "<b id=\"" << nextFileId++ << "\" B=\""
               << fileId[bonds[i].beginAtom] << "\" E=\"" << fileId[bonds[i].endAtom]
               << "\" Order=\"" << static_cast<int>(bonds[i].order) << "\"";
        if (bonds[i].semantics == BondSemantics::Aromatic)
            output << " Display=\"Aromatic\"";
        output << "/>" << std::endl;
    }
    output << "</fragment>" << std::endl;
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) continue;
        output << "<t id=\"" << nextFileId++ << "\" p=\"" << texts[i].x() << ' '
               << texts[i].y() << "\"><s font=\"3\" size=\""
               << texts[i].size << "\">";
        const std::vector<TextRun>& runs = document.runs(texts[i].id);
        if (!runs.empty()) output << runs[0].utf8;
        output << "</s></t>" << std::endl;
    }
    output << "</page>" << std::endl << "</CDXML>" << std::endl;
    if (!output) return Error(ErrorCode::IoError, "Cannot write CDXML output");
    return Error::success();
}

Error exportCdxml(const Document& document, const std::string& path) {
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output) return Error(ErrorCode::IoError, "Cannot open CDXML output file: " + path);
    return exportCdxml(document, output);
}

} // namespace core
} // namespace r62
