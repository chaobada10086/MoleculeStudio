#include "engine/atom_runs.hpp"
#include "engine/metal_labels.hpp"
#include "engine/electron_graphics.hpp"
#include "engine/mechanism_roles.hpp"
#include "engine/name_link.hpp"
#include "persistence.hpp"
#include "persistence_internal.hpp"
#include "next/arrows_curves/model.hpp"
#include "table_layout.hpp"
#include "next/experimental/render_bridge.hpp"
#include "next/experimental/model.hpp"

#include "third_party/tinyxml2/tinyxml2.h"

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <locale>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <functional>

namespace r62 {
namespace persistence {

using namespace cdxml;
using core::Error;
using core::ErrorCode;
using core::ObjectKind;
using core::ObjectRef;
using tinyxml2::XMLElement;

namespace {
// ---------------------------------------------------------------------------
// Writer state
// ---------------------------------------------------------------------------
struct Writer {
    const core::Document& document;
    CdxmlWriteOptions options;
    RefusalReport* refusals;
    CdxmlWriteFidelity* fidelity;
    std::ostringstream out;
    std::map<core::ObjectRef, int32_t> fileId;
    std::map<core::ObjectId, int32_t> atomFileId;
    SourceIdMap source;
    std::set<int32_t> usedFileIds;
    int32_t nextFileId;
    std::vector<std::string> fontNames;
    std::map<std::string, int32_t> fontIdByName;
    int32_t nextFontId;
    std::set<int32_t> sourceFontIds;

    Writer(const core::Document& d, const CdxmlWriteOptions& o, RefusalReport* r,
           CdxmlWriteFidelity* f)
        : document(d), options(o), refusals(r), fidelity(f), nextFileId(3),
          nextFontId(3) {}

    void refuse(const char* element, const std::string& reason, int32_t sourceId,
                const core::ObjectRef& ref) {
        if (!refusals) return;
        Refusal item;
        item.path = "/CDXML";
        item.element = element ? element : "?";
        item.reason = reason;
        item.sourceId = sourceId;
        item.ref = ref;
        refusals->add(item);
    }

    int32_t reserveFileId() {
        while (usedFileIds.count(nextFileId)) ++nextFileId;
        const int32_t value = nextFileId++;
        usedFileIds.insert(value);
        return value;
    }

    int32_t objectId(ObjectRef ref,int32_t sourceId){auto old=fileId.find(ref);if(old!=fileId.end())return old->second;int32_t id=sourceId>0?sourceId:reserveFileId();fileId[ref]=id;return id;}
    int32_t fontFor(const std::string& name) {
        const std::string resolved = name.empty() ? std::string("Arial") : name;
        std::map<std::string, int32_t>::const_iterator it = fontIdByName.find(resolved);
        if (it != fontIdByName.end()) return it->second;
        const int32_t id = nextFontId++;
        fontNames.push_back(resolved);
        fontIdByName[resolved] = id;
        return id;
    }
};

// ---------------------------------------------------------------------------
// Id plan
// ---------------------------------------------------------------------------
// Source slices can start with closing/opening containers and need not be an
// XML document by themselves. Read only opening-tag attributes, respecting
// quotes, to retain every container/nested-label ID and each slice's prefix.
void sourceTagIds(const std::string& text,const std::function<void(size_t,int32_t)>& visit){
    for(size_t at=0;(at=text.find('<',at))!=std::string::npos;){size_t start=at++;if(at>=text.size())break;if(text[at]=='/'||text[at]=='!'||text[at]=='?')continue;
        while(at<text.size()&&!std::isspace(static_cast<unsigned char>(text[at]))&&text[at]!='>'&&text[at]!='/')++at;
        while(at<text.size()){
            while(at<text.size()&&std::isspace(static_cast<unsigned char>(text[at])))++at;if(at>=text.size()||text[at]=='>'||text[at]=='/')break;
            size_t name=at;while(at<text.size()&&!std::isspace(static_cast<unsigned char>(text[at]))&&text[at]!='='&&text[at]!='>')++at;auto key=text.substr(name,at-name);
            while(at<text.size()&&std::isspace(static_cast<unsigned char>(text[at])))++at;if(at>=text.size()||text[at]!='=')break;++at;while(at<text.size()&&std::isspace(static_cast<unsigned char>(text[at])))++at;if(at>=text.size()||(text[at]!='\''&&text[at]!='"'))break;
            char quote=text[at++];size_t value=at,end=text.find(quote,at);if(end==std::string::npos)return;if(key=="id"){char* tail=nullptr;long long id=std::strtoll(text.substr(value,end-value).c_str(),&tail,10);if(id>0&&id<=2147483647)visit(start,int32_t(id));}at=end+1;
        }
    }
}
std::string sourceGap(const std::string& text,int32_t source){size_t first=std::string::npos;sourceTagIds(text,[&](size_t start,int32_t id){if(id==source&&first==std::string::npos)first=start;});return first==std::string::npos?std::string():text.substr(0,first);}
int32_t sourceIdOf(const core::Document& doc,ObjectRef ref){
    if(ref.kind==ObjectKind::Atom){auto a=doc.atoms().get(ref.id);return a?a->sourceId:0;}if(ref.kind==ObjectKind::Bond){auto a=doc.bonds().get(ref.id);return a?a->sourceId:0;}if(ref.kind==ObjectKind::Text){auto a=doc.texts().get(ref.id);return a?a->sourceId:0;}if(ref.kind==ObjectKind::Path){auto a=doc.paths().get(ref.id);return a?a->sourceId:0;}
    const core::ObjectSlab<core::AnnotationRecord>* slab=ref.kind==ObjectKind::Arrow?&doc.arrows():ref.kind==ObjectKind::Symbol?&doc.symbols():ref.kind==ObjectKind::Orbital?&doc.orbitals():ref.kind==ObjectKind::Bracket?&doc.brackets():ref.kind==ObjectKind::BioShape?&doc.bioShapes():nullptr;auto a=slab?slab->get(ref.id):nullptr;return a?a->sourceId:0;
}
// A patched export keeps the CDXML ids the objects originally had, because
// preserved raw regions and any external reference still point at them. An object
// with no source id (drawn in this session) gets a fresh one. A DUPLICATE source
// id - which real ChemDraw files do contain - is detected and, unless explicitly
// allowed, refused rather than silently merged.
Error planIds(Writer& writer) {
    const core::Document& document = writer.document;
    auto reserveBytes=[&](const std::string& bytes){sourceTagIds(bytes,[&](size_t,int32_t id){writer.usedFileIds.insert(id);});};
    for(const auto& e:document.documentExtensions().entries())if(e.key.compare(0,11,"pst.source.")==0||e.key.compare(0,8,"pst.raw.")==0)reserveBytes(e.value);
    auto reserveObject=[&](ObjectRef ref){for(const auto& e:document.extensions(ref).entries())if(e.key.compare(0,8,"pst.src.")==0)reserveBytes(e.value);};
    for(const auto& a:document.atoms().records())reserveObject({ObjectKind::Atom,a.id});for(const auto& a:document.bonds().records())reserveObject({ObjectKind::Bond,a.id});for(const auto& a:document.texts().records())reserveObject({ObjectKind::Text,a.id});for(const auto& a:document.paths().records())reserveObject({ObjectKind::Path,a.id});
    for(auto kind:{ObjectKind::Arrow,ObjectKind::Symbol,ObjectKind::Orbital,ObjectKind::Bracket,ObjectKind::BioShape}){const auto& slab=kind==ObjectKind::Arrow?document.arrows():kind==ObjectKind::Symbol?document.symbols():kind==ObjectKind::Orbital?document.orbitals():kind==ObjectKind::Bracket?document.brackets():document.bioShapes();for(const auto& a:slab.records())reserveObject({kind,a.id});}
    writer.source = SourceIdMap::fromDocument(document);

    const std::vector<int32_t>& collisions = writer.source.collisions();
    if (!collisions.empty() && !writer.options.allowDuplicateSourceIds) {
        std::ostringstream reason;
        reason << collisions.size()
               << " duplicate CDXML source id(s) present; renumbering them silently "
                  "would re-point every reference, so this export refuses instead";
        writer.refuse("CDXML", reason.str(), collisions[0], core::ObjectRef());
        return fail(reason.str(), ErrorCode::AlreadyExists);
    }

    // Reserve every surviving source id first, so a generated id cannot collide.
    const std::vector<core::AtomRecord>& atoms = document.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i)
        if (atoms[i].isAlive() && atoms[i].sourceId > 0)
            writer.usedFileIds.insert(atoms[i].sourceId);
    const std::vector<core::BondRecord>& bonds = document.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i)
        if (bonds[i].isAlive() && bonds[i].sourceId > 0)
            writer.usedFileIds.insert(bonds[i].sourceId);
    const std::vector<core::TextRecord>& texts = document.texts().records();
    for (size_t i = 0; i < texts.size(); ++i)
        if (texts[i].isAlive() && texts[i].sourceId > 0)
            writer.usedFileIds.insert(texts[i].sourceId);
    // Reserve every modeled object's source ID before allocating IDs for new
    // objects and containers. Imported arrows used to be omitted, so adding a
    // new arrow could reuse an existing arrow's ID and make the saved file fail
    // to reopen.
    for(const auto& r:document.paths().records())if(r.isAlive()&&r.sourceId>0)writer.usedFileIds.insert(r.sourceId);
    for(const auto& r:document.tables().records())if(r.isAlive()&&r.sourceId>0)writer.usedFileIds.insert(r.sourceId);
    for(const auto& r:document.groups().records())if(r.isAlive()&&r.sourceId>0)writer.usedFileIds.insert(r.sourceId);
    for(const auto* slab:{&document.arrows(),&document.brackets(),&document.orbitals(),&document.symbols(),&document.bioShapes()})
        for(const auto& r:slab->records())if(r.isAlive()&&r.sourceId>0)writer.usedFileIds.insert(r.sourceId);
    // Unknown preserved graphics also own IDs even though they have no record.
    for(const auto& entry:document.documentExtensions().entries())if(entry.key.compare(0,8,"pst.raw.")==0){
        tinyxml2::XMLDocument raw;if(raw.Parse(entry.value.c_str())!=tinyxml2::XML_SUCCESS)continue;
        std::function<void(XMLElement*)> reserve=[&](XMLElement* node){if(!node)return;int id=node->IntAttribute("id");if(id>0)writer.usedFileIds.insert(id);for(auto c=node->FirstChildElement();c;c=c->NextSiblingElement())reserve(c);};reserve(raw.RootElement());
    }
    return Error::success();
}

void assignAtomIds(Writer& writer) {
    const std::vector<core::AtomRecord>& atoms = writer.document.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        if (atoms[i].sourceId > 0) writer.atomFileId[atoms[i].id] = atoms[i].sourceId;
        else writer.atomFileId[atoms[i].id] = writer.reserveFileId();
    }
}

// ---------------------------------------------------------------------------
// Escaping and attribute helpers
// ---------------------------------------------------------------------------
void attribute(std::ostringstream& out, const char* name, const std::string& value) {
    std::string escaped;
    escapeAttribute(value, &escaped);
    out << " " << name << "=\"" << escaped << "\"";
}

void attributeNumber(std::ostringstream& out, const char* name, double value) {
    out << " " << name << "=\"" << numberToText(value) << "\"";
}
void attributeInteger(std::ostringstream& out, const char* name, long long value) {
    out << " " << name << "=\"" << value << "\"";
}

// CDXML display spellings, the INVERSE of the reader table in cdxml_util.cpp.
// Both directions live in this module and the test suite asserts them against
// each other, because a one-sided table is how a round trip changes meaning.
const char* displayToCdxml(core::BondDisplay display) {
    switch (display) {
    case core::BondDisplay::Normal: return "Solid";
    case core::BondDisplay::Wedge: return "WedgeBegin";
    case core::BondDisplay::Hash: return "Hash";
    case core::BondDisplay::WedgeHash: return "WedgedHashBegin";
    case core::BondDisplay::HollowWedge: return "HollowWedgeBegin";
    case core::BondDisplay::Bold: return "Bold";
    case core::BondDisplay::Dashed: return "Dash";
    case core::BondDisplay::Dotted: return "Dot";
    case core::BondDisplay::Wavy: return "Wavy";
    }
    return "Solid";
}

const char* orderToCdxml(core::BondOrder order, core::BondSemantics semantics) {
    // A Kekule ring has explicit alternating 1/2 orders; its aromatic flag
    // must not replace the DOUBLE edges with 1.5 in a mixed-order ring.
    (void)semantics;
    switch (order) {
    case core::BondOrder::Single: return "1";
    case core::BondOrder::Double: return "2";
    case core::BondOrder::Triple: return "3";
    case core::BondOrder::Quadruple: return "4";
    case core::BondOrder::Aromatic: return "1.5";
    case core::BondOrder::SingleOrDouble: return "SingleOrDouble";
    case core::BondOrder::SingleOrAromatic: return "SingleOrAromatic";
    case core::BondOrder::DoubleOrAromatic: return "DoubleOrAromatic";
    case core::BondOrder::Any: return "Any";
    case core::BondOrder::Unspecified: return "Unspecified";
    }
    return "1";
}


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

std::string sourceKey(ObjectKind kind, core::ObjectId id) {
    std::ostringstream key;
    key << kExtensionPrefix << "src." << kindKeyName(kind) << "." << id;
    return key.str();
}

// ---------------------------------------------------------------------------
// Signature agreement
// ---------------------------------------------------------------------------
// An object may only be re-emitted from its preserved source bytes if the
// document has not changed since it was read. Otherwise the stale bytes would
// OVERWRITE the user edit - the exact opposite of the goal. The signature is
// recomputed from the current model state and compared with what the reader
// recorded. Any mismatch means "regenerate from the model".
//
// The signature covers every field the writer can emit, in the SAME order and
// formatting the writer uses, so a signature that matches implies the generated
// bytes equal the preserved bytes for the promoted part.
// Signatures live in cdxml_encoders.cpp so the reader and the writer cannot drift apart.

} // namespace


namespace {

// ---------------------------------------------------------------------------
// Preserved-attribute replay
// ---------------------------------------------------------------------------
// When an object is REGENERATED from the model, every attribute the reader could
// not promote must still be written. They live in the extension bag under
// pst.attr.<Name>; this replays them. The order the reader saw them in is kept so
// a second read sees the same file, which is what makes the byte comparison in
// the round-trip test meaningful.
void replayPreservedAttributes(std::ostringstream& out, const core::Document& document,
                               const ObjectRef& ref) {
    if (!ref.valid()) return;
    const core::ExtensionBag& bag = document.extensions(ref);
    std::set<std::string> written;written.insert("MoleculeStudioPerspectiveFront");written.insert("MoleculeStudioPerspectiveZ");
    if(ref.kind==ObjectKind::Atom){written.insert("xyz");written.insert("MoleculeStudioHydrogens");
        if(!mechanism::genericRole(document,ref.id).empty()){written.insert("IgnoreWarnings");written.insert("GenericNickname");written.insert("Warning");}}
    auto custom=[&](const char* name,const std::string& value){attribute(out,name,value);written.insert(name);};
    if(auto z=bag.findValue("studio.z"))custom("Z",*z);
    if(ref.kind==ObjectKind::Text)if(auto angle=bag.findValue("studio.text.RotationAngle"))custom("RotationAngle",*angle);
    if(ref.kind==ObjectKind::Atom){auto atom=document.atoms().alive(ref.id);if(atom){auto z=bag.findValue("studio.atom.z");if(!z)z=bag.findValue("pst.attr.MoleculeStudioPerspectiveZ");if(z)custom("xyz",numberToText(atom->position.x)+" "+numberToText(atom->position.y)+" "+*z);if(auto n=bag.findValue("studio.atom.number")){custom("AtomNumber",*n);custom("ShowAtomNumber","yes");}}}
    const std::vector<core::ExtensionEntry>& entries = bag.entries();
    // Replay in the order recorded by the reader when it is available.
    std::ostringstream orderKeyText;
    orderKeyText << kExtensionPrefix << "attr.order." << kindKeyName(ref.kind) << "." << ref.id;
    const std::string orderKeyValue = orderKeyText.str();
    const std::string* order = bag.findValue(orderKeyValue);
    std::vector<std::string> names;
    if (order && !order->empty()) {
        size_t start = 0;
        for (;;) {
            const size_t comma = order->find(44, start);
            names.push_back(order->substr(start, comma == std::string::npos
                                                  ? std::string::npos : comma - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    for (size_t i = 0; i < names.size(); ++i) {
        if(written.count(names[i]))continue;
        const std::string key = std::string(kExtensionPrefix) + "attr." + names[i];
        const std::string* value = bag.findValue(key);
        if (!value) continue;
        attribute(out, names[i].c_str(), *value);
        written.insert(names[i]);
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& key = entries[i].key;
        if (key.compare(0, 9, "pst.attr.") != 0) continue;
        // The ORDER BOOKKEEPING key lives in the same namespace and must never be
        // written back as if it were an attribute of the element.
        if (key.compare(0, 10, "pst.attr.o") == 0 && key.find("attr.order.") != std::string::npos)
            continue;
        const std::string name = key.substr(9);
        if (written.count(name)) continue;
        attribute(out, name.c_str(), entries[i].value);
        written.insert(name);
    }
}

// ---------------------------------------------------------------------------
// Atom emission
// ---------------------------------------------------------------------------
// The atom label is written as a <t> CHILD whose text is the label, which is how
// real CDXML stores an abbreviation. A plain element node with no label override
// stays self-closing, which is what the simple fixtures look like and what keeps
// the byte comparison meaningful.
void emitAtom(Writer& writer, const core::AtomRecord& atom) {
    const core::Document& document = writer.document;
    const std::string role=mechanism::genericRole(document,atom.id);
    const std::map<core::ObjectId, int32_t>::const_iterator idIt = writer.atomFileId.find(atom.id);
    if (idIt == writer.atomFileId.end()) return;
    std::ostringstream& out = writer.out;
    out << "<n";
    attributeInteger(out, "id", idIt->second);
    out << " p=\"" << numberToText(atom.x()) << ' ' << numberToText(atom.y()) << "\"";
    if (atom.element > 0 && atom.element != 6)
        attributeInteger(out, "Element", atom.element);
    if (atom.semantics.charge != 0)
        attributeInteger(out, "Charge", atom.semantics.charge);
    if (atom.semantics.isotope != 0)
        attributeInteger(out, "Isotope", atom.semantics.isotope);
    if (atom.semantics.implicitHydrogens >= 0)
        attributeInteger(out, "NumHydrogens", atom.semantics.implicitHydrogens);
    if (atom.semantics.radical != 0)
        attribute(out, "Radical", atom.semantics.radical==1?"Singlet":atom.semantics.radical==2?"Doublet":"Triplet");
    std::string sourceNodeType;
    if(role.empty())if(auto raw=document.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("studio.cdxml.NodeType")){
        core::NodeType mapped=core::NodeType::Element;
        if(nodeTypeFromCdxml(raw->c_str(),&mapped)&&
           (uint8_t(mapped)==atom.semantics.nodeType||(*raw=="GenericNickname"&&atom.semantics.nodeType==uint8_t(core::NodeType::Query))))sourceNodeType=*raw;
    }
    if(!sourceNodeType.empty())attribute(out,"NodeType",sourceNodeType);
    else if (atom.semantics.nodeType != static_cast<uint8_t>(core::NodeType::Element)) {
        static const char* const kNodeNames[] = { "Element", "Nickname", "Fragment",
                                                  "GenericNickname", "GenericNickname", "GenericNickname",
                                                  "MultiAttachment", "ExternalConnectionPoint" };
        const size_t index = atom.semantics.nodeType < 8 ? atom.semantics.nodeType : 0;
        attribute(out, "NodeType", kNodeNames[index]);
    }
    if (atom.semantics.showLabel) attribute(out, "LabelDisplay", "Shown");
    if(!role.empty()){
        // B/Nu here denote a user-selected generic reagent, not an element or
        // a chemically expandable abbreviation. Suppress only this placeholder's
        // interpretation warning; real atoms retain ChemDraw's validation.
        attribute(out,"IgnoreWarnings","yes");
        attribute(out,"GenericNickname",document.atomLabel(atom.id));
    }
    replayPreservedAttributes(out, document, ObjectRef(ObjectKind::Atom, atom.id));

    std::string label = document.atomLabel(atom.id);
    // ChemDraw derives the valence of an unlabelled heteroatom on load and
    // can discard Charge (notably an isolated Br- with no <t>). Emit the
    // chemical label and its superscript charge together for labelled ions.
    if(label.empty()&&atom.semantics.charge&&(atom.element!=6||document.incidentBonds(atom.id).empty())){
        label=core::elementSymbol(atom.element);int h=integration::molecular::labelHydrogenCount(document,atom);
        if(h>0)label+="H"+(h>1?std::to_string(h):std::string());
    }
    if(document.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("studio.hydrogens.manual")&&atom.semantics.implicitHydrogens>=0){int h=atom.semantics.implicitHydrogens;label=std::string(core::elementSymbol(atom.element))+(h?"H"+(h>1?std::to_string(h):std::string()):std::string());attribute(out,"MoleculeStudioHydrogens","1");}
    std::vector<core::TextRun> runs = studioatom::runs(document,atom.id);
    std::string labelFont=document.defaultFont();if(auto f=document.documentExtensions().findValue("studio.style.LabelFontName"))labelFont=*f;
    double labelSize=10;if(auto v=document.documentExtensions().findValue("studio.style.LabelSize")){double n=std::atof(v->c_str());if(std::isfinite(n)&&n>0)labelSize=n;}
    std::string drawn;for(const auto& run:runs)drawn+=run.utf8;if(drawn!=label)runs.clear();
    if (label.empty() || (atom.element==6&&!atom.semantics.showLabel&&(atom.semantics.radical||atom.semantics.charge)&&document.incidentBonds(atom.id).size()>=2&&label=="C")) { out << "/>"; return; }
    out << ">";
    if(!role.empty()){
        out<<"<objecttag";attributeInteger(out,"id",writer.reserveFileId());
        attribute(out,"Name","MoleculeStudio.GenericRole."+role);
        attribute(out,"Persistent","yes");out<<"/>";
    }
    auto chargeLabel=[&](){
        // With an explicit <t>, ChemDraw uses the label verbatim rather than
        // synthesizing Charge. Include its superscript as well as the semantic
        // property. The reader removes this display run from the editable label.
        if(!atom.semantics.charge||studio_electron_graphics::separateCharge(document,atom))return;
        int q=atom.semantics.charge;
        std::string text=(std::abs(q)>1?std::to_string(std::abs(q)):std::string())+(q>0?"+":"-");
        out<<"<s font=\""<<writer.fontFor(labelFont)<<"\" size=\""<<numberToText(labelSize)<<"\" face=\"64\">"<<text<<"</s>";
    };
    if(atom.semantics.nodeType==static_cast<uint8_t>(core::NodeType::Nickname)){
        auto raw=document.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("studio.nickname.fragment");
        if(!raw)raw=document.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("studio.compound.fragment");
        if(raw){tinyxml2::XMLDocument nested;if(nested.Parse(raw->c_str())==tinyxml2::XML_SUCCESS){
            std::map<int,int> ids;std::function<void(XMLElement*)> allocate=[&](XMLElement* n){if(n->Attribute("id")){int old=n->IntAttribute("id");ids[old]=writer.reserveFileId();}for(auto c=n->FirstChildElement();c;c=c->NextSiblingElement())allocate(c);};
            std::function<void(XMLElement*)> remap=[&](XMLElement* n){for(const char* key:{"id","B","E"})if(n->Attribute(key)){auto it=ids.find(n->IntAttribute(key));if(it!=ids.end())n->SetAttribute(key,it->second);}for(const char* key:{"ConnectionOrder","BondOrdering","BondCircularOrdering","CrossingBonds","object"})if(auto raw=n->Attribute(key)){std::istringstream input(raw);std::ostringstream output;int old;bool first=true;while(input>>old){if(!first)output<<char(32);auto it=ids.find(old);output<<(it==ids.end()?old:it->second);first=false;}n->SetAttribute(key,output.str().c_str());}n->DeleteAttribute("Z");for(auto c=n->FirstChildElement();c;c=c->NextSiblingElement())remap(c);};
            auto root=nested.RootElement();if(root){allocate(root);remap(root);tinyxml2::XMLPrinter p;root->Accept(&p);out<<p.CStr();}
        }}
    }
    const bool metalAttached=studio::metal_labels::active(document,atom.id);
    if(metalAttached){if(runs.empty()){core::TextRun run;run.utf8=label;run.fontName=labelFont;run.size=labelSize;run.flags=core::kFormula;runs.push_back(run);}runs=studio::metal_labels::canonical(label,runs);}
    out << "<t";
    if(metalAttached)attribute(out,"LabelAlignment",studio::metal_labels::bondOnRight(document,atom.id)?"Right":"Left");
    out << ">";
    if (runs.empty()) {
        // A label override with no explicit run still has to be written, or the
        // abbreviation would silently disappear from the exported file.
        std::string escaped;
        escapeContent(label, &escaped);
        out << "<s font=\"" << writer.fontFor(labelFont) << "\" size=\""
            << numberToText(labelSize) << "\" face=\"96\">"
            << escaped << "</s>";
        chargeLabel();out << "</t></n>";
        return;
    }
    for (size_t i = 0; i < runs.size(); ++i) {
        std::string escaped;
        escapeContent(runs[i].utf8, &escaped);
        out << "<s font=\"" << writer.fontFor(runs[i].fontName) << "\" size=\""
            << numberToText(runs[i].size > 0.0 ? runs[i].size : labelSize)
            << "\"";
        const int face = faceFromTextFlags(runs[i].flags);
        if (face != 0) out << " face=\"" << face << "\"";
        out << ">" << escaped << "</s>";
    }
    chargeLabel();out << "</t></n>";
}

void emitBond(Writer& writer, const core::BondRecord& bond) {
    const std::map<core::ObjectId, int32_t>::const_iterator beginIt =
        writer.atomFileId.find(bond.beginAtom);
    const std::map<core::ObjectId, int32_t>::const_iterator endIt =
        writer.atomFileId.find(bond.endAtom);
    if (beginIt == writer.atomFileId.end() || endIt == writer.atomFileId.end()) {
        writer.refuse("b", "bond endpoint does not resolve to an emitted atom",
                      bond.sourceId, ObjectRef(ObjectKind::Bond, bond.id));
        return;
    }
    std::ostringstream& out = writer.out;
    out << "<b id=\"" << writer.objectId(ObjectRef(ObjectKind::Bond,bond.id),bond.sourceId)
        << "\" B=\"" << beginIt->second << "\" E=\"" << endIt->second << "\"";
    out << " Order=\"" << orderToCdxml(bond.order, bond.semantics) << "\"";
    auto display=bond.display;
    const auto& bondBag=writer.document.extensions(ObjectRef(ObjectKind::Bond,bond.id));
    if(display==core::BondDisplay::Normal&&bondBag.findValue("pst.attr.MoleculeStudioPerspectiveFront")){
        display=core::BondDisplay::Bold;
        if(!bondBag.findValue("pst.attr.BoldWidth"))attributeNumber(out,"BoldWidth",writer.document.defaultLineWidth()*2.22);
    }
    if (display != core::BondDisplay::Normal)attribute(out, "Display", displayToCdxml(display));
    replayPreservedAttributes(out, writer.document, ObjectRef(ObjectKind::Bond, bond.id));
    out << "/>";
}

namespace {
// Translate a caption's stored structure members (document ids) into the CDXML
// file ids this export assigns to the same objects. Bonds may not have been
// emitted yet, so objectId() is used; atom ids were pre-allocated by
// assignAtomIds() before any object is written.
std::vector<core::ObjectRef> linkRefsToFileIds(Writer& writer, const std::vector<core::ObjectRef>& refs) {
    std::vector<core::ObjectRef> out;
    for (size_t i = 0; i < refs.size(); ++i) {
        const core::ObjectRef& ref = refs[i];
        if (ref.kind == core::ObjectKind::Atom) {
            std::map<core::ObjectId, int32_t>::const_iterator it = writer.atomFileId.find(ref.id);
            if (it != writer.atomFileId.end()) out.push_back(core::ObjectRef(core::ObjectKind::Atom, core::ObjectId(it->second)));
        } else if (ref.kind == core::ObjectKind::Bond) {
            const core::BondRecord* bond = writer.document.bonds().alive(ref.id);
            if (bond) out.push_back(core::ObjectRef(core::ObjectKind::Bond, core::ObjectId(writer.objectId(ref, bond->sourceId))));
        }
    }
    return out;
}
} // namespace

void emitText(Writer& writer, const core::TextRecord& text) {
    const core::Document& document = writer.document;
    const std::vector<core::TextRun>& runs = document.runs(text.id);
    std::ostringstream& out = writer.out;
    out << "<t id=\"" << writer.objectId(ObjectRef(ObjectKind::Text,text.id),text.sourceId)
        << "\" p=\"" << numberToText(text.x()) << ' ' << numberToText(text.y()) << "\"";
    if (text.format.align == static_cast<uint8_t>(core::TextAlign::Center))
        attribute(out, "Justification", "Center");
    else if (text.format.align == static_cast<uint8_t>(core::TextAlign::Right))
        attribute(out, "Justification", "Right");
    if (const std::string* width = document.extensions(ObjectRef(ObjectKind::Text, text.id)).findValue("studio.text.width")) {
        const double value = std::atof(width->c_str());
        if (value > 0.0) attributeNumber(out, "MoleculeStudioTextWidth", value);
    }
    // The structure link travels as ONE attribute: "digest;a1,b2,...". Member
    // ids are translated to CDXML file ids here and resolved back on read, so the
    // association survives a CDXML round trip.
    {
        const core::ExtensionBag& linkBag = document.extensions(ObjectRef(ObjectKind::Text, text.id));
        const std::string* digest = linkBag.findValue("studio.nameLink.digest");
        if (digest && !digest->empty()) {
            std::string value = *digest;
            const std::string* storedRefs = linkBag.findValue("studio.nameLink.refs");
            if (storedRefs) {
                std::vector<core::ObjectRef> members = linkRefsToFileIds(writer, namelink::decodeRefs(*storedRefs));
                if (!members.empty()) value += ";" + namelink::encodeRefs(members);
            }
            attribute(out, "MoleculeStudioNameLink", value);
        }
    }
    replayPreservedAttributes(out, document, ObjectRef(ObjectKind::Text, text.id));
    out << ">";
    const double defaultSize = text.size > 0.0 ? text.size : document.defaultTextSize();
    if (runs.empty()) {
        out << "<s font=\"" << writer.fontFor(text.format.fontName) << "\" size=\""
            << numberToText(defaultSize) << "\"/>";
    } else {
        for (size_t i = 0; i < runs.size(); ++i) {
            std::string escaped;
            escapeContent(runs[i].utf8, &escaped);
            out << "<s font=\"" << writer.fontFor(runs[i].fontName) << "\" size=\""
                << numberToText(runs[i].size > 0.0 ? runs[i].size : defaultSize) << "\"";
            const int face = faceFromTextFlags(runs[i].flags);
            if (face != 0) out << " face=\"" << face << "\"";
            out << ">" << escaped << "</s>";
        }
    }
    out << "</t>";
}

bool emitImportedGraphic(Writer& writer,ObjectRef ref,core::Vec2 current,int sourceId){
    auto raw=writer.document.extensions(ref).findValue("studio.cdxml.graphic");if(!raw)return false;
    tinyxml2::XMLDocument xml;if(xml.Parse(raw->c_str())!=tinyxml2::XML_SUCCESS)return false;auto root=xml.RootElement();if(!root)return false;
    std::string name=root->Name(),type=root->Attribute("GraphicType")?root->Attribute("GraphicType"):"";
    const char* anchor=root->Attribute(name=="curve"?"CurvePoints":name=="arrow"?"Tail3D":"BoundingBox");if(!anchor)return false;
    core::Vec2 original;std::istringstream in(anchor);if(!(in>>original.x>>original.y))return false;
    if(ref.kind==ObjectKind::Path&&name!="curve"&&type!="Line"&&type!="Arc"){double x,y;if(in>>x>>y){original.x=std::min(original.x,x);original.y=std::min(original.y,y);}}
    auto delta=current-original;root->SetAttribute("id",writer.objectId(ref,sourceId));
    for(const char* key:{"BoundingBox","CurvePoints","Head3D","Tail3D","Center3D","MajorAxisEnd3D","MinorAxisEnd3D"})if(auto value=root->Attribute(key)){
        std::istringstream input(value);std::vector<double> values;double n;while(input>>n)values.push_back(n);bool pairs=std::string(key)=="BoundingBox"||std::string(key)=="CurvePoints";
        for(size_t i=0;i+1<values.size();i+=pairs?2:3){values[i]+=delta.x;values[i+1]+=delta.y;}std::ostringstream output;output.precision(12);for(size_t i=0;i<values.size();++i){if(i)output<<' ';output<<values[i];}root->SetAttribute(key,output.str().c_str());
    }
    if(name=="embeddedobject"&&root->Attribute("MoleculeStudioRaster")){
        auto path=writer.document.paths().alive(ref.id);
        if(path&&path->points.size()==4){
            const auto& p=path->points;auto center=(p[0]+p[2])*.5,u=p[1]-p[0],v=p[3]-p[0];
            const double width=core::length(u),height=core::length(v);
            std::ostringstream box,corners;box.precision(12);corners.precision(12);
            box<<center.x-width*.5<<' '<<center.y-height*.5<<' '<<center.x+width*.5<<' '<<center.y+height*.5;
            for(auto point:p)corners<<point.x<<' '<<point.y<<' ';
            root->SetAttribute("BoundingBox",box.str().c_str());root->SetAttribute("MoleculeStudioImageCorners",corners.str().c_str());
            root->SetAttribute("RotationAngle",std::atan2(u.y,u.x)*180./3.141592653589793*65536.);
        }
    }
    tinyxml2::XMLPrinter out;root->Accept(&out);writer.out<<out.CStr();return true;
}

void emitPath(Writer& writer, const core::PathRecord& path) {
    if(!path.points.empty()&&emitImportedGraphic(writer,ObjectRef(ObjectKind::Path,path.id),path.points[0],path.sourceId))return;
    std::ostringstream& out = writer.out;
    out << "<curve";
    attributeInteger(out, "id", writer.objectId(ObjectRef(ObjectKind::Path,path.id),path.sourceId));
    attribute(out,"Closed",path.closed?"yes":"no");
    attributeInteger(out,"CurveType",(path.closed?1:0)|(path.fillEnabled?128:0));
    std::ostringstream points,vertices;for(const auto& point:path.points){vertices<<numberToText(point.x)<<' '<<numberToText(point.y)<<' ';for(int i=0;i<3;++i)points<<numberToText(point.x)<<' '<<numberToText(point.y)<<' ';}
    attribute(out,"CurvePoints",points.str());attribute(out,"MoleculeStudioPath",vertices.str());
    attribute(out,"MoleculeStudioFill",std::to_string(path.fillRGBA));attribute(out,"FillType",path.fillEnabled?"Solid":"None");
    if (path.style.width > 0.0) attributeNumber(out, "LineWidth", path.style.width);
    replayPreservedAttributes(out, writer.document, ObjectRef(ObjectKind::Path, path.id));
    out << "/>";
}

#include "tlc_cdxml_writer.inc"

// Fit the sampled routing polyline to a few genuine Bezier segments. Exporting
// one knot per renderer sample makes ChemDraw clamp the arrowhead to a tiny
// final segment; it also produces hundreds of unhelpful editing handles.
std::vector<core::Vec2> routeBezierPoints(const std::vector<core::Vec2>& pts,double tolerance,double headLength){
    struct Cubic{core::Vec2 a,b,c,d;};std::vector<Cubic> segments;
    std::vector<double> lengths(pts.size(),0);for(size_t i=1;i<pts.size();++i)lengths[i]=lengths[i-1]+core::length(pts[i]-pts[i-1]);
    auto tangent=[&](size_t i){auto v=pts[std::min(i+1,pts.size()-1)]-pts[i?i-1:i];double l=core::length(v);return l>1e-9?v*(1/l):core::Vec2(1,0);};
    auto dot=[](core::Vec2 a,core::Vec2 b){return a.x*b.x+a.y*b.y;};
    std::function<void(size_t,size_t,int)> fit=[&](size_t first,size_t last,int depth){
        const auto a=pts[first],d=pts[last],u=tangent(first),v=tangent(last);
        const double span=lengths[last]-lengths[first];double aa=0,ab=0,bb=0,ax=0,bx=0;
        for(size_t i=first+1;i<last;++i){double t=span>1e-9?(lengths[i]-lengths[first])/span:0,s=1-t,b0=s*s*s,b1=3*s*s*t,b2=3*s*t*t,b3=t*t*t;
            auto x=u*b1,y=v*(-b2),r=pts[i]-a*(b0+b1)-d*(b2+b3);aa+=dot(x,x);ab+=dot(x,y);bb+=dot(y,y);ax+=dot(x,r);bx+=dot(y,r);}
        double alpha=span/3,beta=span/3,det=aa*bb-ab*ab;
        if(det>1e-12){alpha=(ax*bb-bx*ab)/det;beta=(bx*aa-ax*ab)/det;}
        if(alpha<=0||beta<=0||alpha>span*1.5||beta>span*1.5)alpha=beta=span/3;
        Cubic q{a,a+u*alpha,d-v*beta,d};double error=0;size_t split=(first+last)/2;
        for(size_t i=first+1;i<last;++i){double t=span>1e-9?(lengths[i]-lengths[first])/span:0,s=1-t;auto p=q.a*(s*s*s)+q.b*(3*s*s*t)+q.c*(3*s*t*t)+q.d*(t*t*t);double e=core::length(p-pts[i]);if(e>error){error=e;split=i;}}
        const double minSpan=std::max(tolerance*8,headLength*2.5);
        if(error>tolerance&&depth<5&&last>first+2&&lengths[split]-lengths[first]>=minSpan&&lengths[last]-lengths[split]>=minSpan){fit(first,split,depth+1);fit(split,last,depth+1);}
        else segments.push_back(q);
    };
    if(pts.size()<2)return {};fit(0,pts.size()-1,0);
    std::vector<core::Vec2> controls;
    controls.push_back(segments.front().a);controls.push_back(segments.front().a);controls.push_back(segments.front().b);
    for(size_t i=0;i<segments.size();++i){controls.push_back(segments[i].c);controls.push_back(segments[i].d);controls.push_back(i+1<segments.size()?segments[i+1].b:segments[i].d);}
    return controls;
}

void emitAnnotation(Writer& writer, const core::AnnotationRecord& annotation) {
    if(emitImportedGraphic(writer,ObjectRef(annotation.kind,annotation.id),annotation.geometry[0],annotation.sourceId))return;
    std::ostringstream& out = writer.out;
    if(annotation.kind==ObjectKind::Arrow&&writer.document.extensions(ObjectRef(ObjectKind::Arrow,annotation.id)).find(arrows::kArrowParamsKey)){
        arrows::ArrowParams p;bool ok=false;if(arrows::readArrow(writer.document,annotation.id,&p,&ok).ok()&&ok){
            double width=arrows::arrowStrokeWidth(p,writer.document.defaultLineWidth());arrows::ArrowGeometry geometry;arrows::buildArrowGeometry(p,writer.document.defaultLineWidth(),arrows::Tessellation(),&geometry);
            auto route=arrows::routedPoints(p);
            const bool doubleArc=p.family==arrows::ArrowFamily::Arc&&p.shafts==arrows::ShaftCount::Double;
            const bool routed=doubleArc||(route.size()>2&&p.shafts==arrows::ShaftCount::One);
            const bool vectorHead=routed&&(p.mechanism||doubleArc);
            if(vectorHead&&!geometry.shafts.empty())route=geometry.shafts.front();
            bool retro=p.head.shape==arrows::ArrowHeadShape::HalfArrow;out<<(routed?"<curve":"<arrow");attributeInteger(out,"id",writer.objectId(ObjectRef(annotation.kind,annotation.id),annotation.sourceId));
            if(routed){
                // Native Bezier splines retain the actual gesture/routing shape.
                // Each knot has incoming handle, anchor, outgoing handle in CDXML.
                std::ostringstream points;
                for(auto q:routeBezierPoints(route,std::max(.04,width*.16),p.head.length))points<<numberToText(q.x)<<' '<<numberToText(q.y)<<' ';
                attribute(out,"CurvePoints",points.str());
                const bool half=p.head.sides!=arrows::HeadSides::Both;
                attributeInteger(out,"CurveType",(vectorHead?0:((p.head.atEnd?(half?32:8):0)|(p.head.atBegin?(half?64:16):0)))|(p.line==arrows::ArrowLine::Dashed?2:0));
            }
            auto point=[&](const char* name,core::Vec2 value){attribute(out,name,numberToText(value.x)+" "+numberToText(value.y)+" 0");};
            attribute(out,"BoundingBox",numberToText(geometry.bounds.min.x)+" "+numberToText(geometry.bounds.min.y)+" "+numberToText(geometry.bounds.max.x)+" "+numberToText(geometry.bounds.max.y));
            point("Tail3D",p.begin);point("Head3D",p.end);attributeNumber(out,"LineWidth",width);attribute(out,"FillType","None");
            attribute(out,"ArrowheadType",(retro||p.shafts==arrows::ShaftCount::Two)?"Angle":p.head.shape==arrows::ArrowHeadShape::Hollow?"Hollow":p.head.shape==arrows::ArrowHeadShape::Angled?"Angle":"Solid");
            const char* head=p.head.sides==arrows::HeadSides::Left?"HalfLeft":p.head.sides==arrows::HeadSides::Right?"HalfRight":"Full";
            attribute(out,"ArrowheadHead",vectorHead?"None":p.shafts==arrows::ShaftCount::Two?"HalfLeft":p.head.atEnd?head:"None");attribute(out,"ArrowheadTail",vectorHead?"None":p.shafts==arrows::ShaftCount::Two?"HalfLeft":p.head.atBegin?head:"None");
            attributeInteger(out,"HeadSize",static_cast<long long>(std::lround(p.head.length*100/width)));attributeInteger(out,"ArrowheadCenterSize",static_cast<long long>(std::lround(p.head.length*(retro?1:.875)*100/width)));attributeInteger(out,"ArrowheadWidth",static_cast<long long>(std::lround((retro?p.head.length*.25:p.head.width*.5)*100/width)));
            if(p.line==arrows::ArrowLine::Dashed)attribute(out,"LineType","Dashed");
            if(!doubleArc&&p.shafts!=arrows::ShaftCount::One){attributeInteger(out,"ArrowShaftSpacing",static_cast<long long>(std::lround(p.separation*(retro?.5:1)*100/width)));attributeInteger(out,"ArrowEquilibriumRatio",static_cast<long long>(std::lround((1-p.splitRatio)/p.splitRatio*100)));}
            if(p.head.shape==arrows::ArrowHeadShape::Cross||p.head.shape==arrows::ArrowHeadShape::Hash)attribute(out,"NoGo",p.head.shape==arrows::ArrowHeadShape::Cross?"Cross":"Hash");
            if(!routed&&p.family==arrows::ArrowFamily::Arc&&std::abs(p.arcSagitta)>1e-8){auto d=p.end-p.begin;double chord=core::length(d);if(chord>1e-8){core::Vec2 u=d*(1/chord),n(-u.y,u.x);double cy=(p.arcSagitta*p.arcSagitta-chord*chord*.25)/(2*p.arcSagitta);auto center=(p.begin+p.end)*.5+n*cy;double radius=std::sqrt(chord*chord*.25+cy*cy);point("Center3D",center);point("MajorAxisEnd3D",center+u*radius);point("MinorAxisEnd3D",center+n*radius);attributeNumber(out,"AngularSize",4*std::atan(2*p.arcSagitta/chord)*180/3.141592653589793);}}
            auto electronRef=[&](const char* key,core::ObjectRef ref){
                int source=0;bool alive=false;
                if(ref.kind==ObjectKind::Atom){auto a=writer.document.atoms().alive(ref.id);if(a){source=a->sourceId;alive=true;}}
                if(ref.kind==ObjectKind::Bond){auto b=writer.document.bonds().alive(ref.id);if(b){source=b->sourceId;alive=true;}}
                if(ref.kind==ObjectKind::Arrow){auto a=writer.document.arrows().alive(ref.id);if(a){source=a->sourceId;alive=true;}}
                if(alive){int file=ref.kind==ObjectKind::Atom?writer.atomFileId.at(ref.id):writer.objectId(ref,source);attribute(out,key,std::string(ref.kind==ObjectKind::Atom?"atom:":ref.kind==ObjectKind::Bond?"bond:":"arrow:")+std::to_string(file));}
            };
            if(p.mechanism||p.virtualBond){electronRef("MoleculeStudioElectronSource",p.electronSource);electronRef("MoleculeStudioElectronTarget",p.electronTarget);}
            p.electronSource=core::ObjectRef();p.electronTarget=core::ObjectRef();
            for(const auto& group:writer.document.groups().records())if(group.isAlive()&&group.label=="Joined arrows")for(auto ref:group.members)if(ref==ObjectRef(ObjectKind::Arrow,annotation.id))attributeInteger(out,"MoleculeStudioJoinedGroup",group.id+1);
            attribute(out,"MoleculeStudioArrow",arrows::encodeArrowParams(p));out<<"/>";
            if(vectorHead){
                // ChemDraw 26 retains custom spline-head dimensions in XML but
                // renders a fixed tiny head, and flattens hollow/angled styles.
                // Emit the same filled outline/open barb used by our renderer
                // as ordinary editable vector curves instead of losing it.
                const int owner=writer.objectId(ObjectRef(annotation.kind,annotation.id),annotation.sourceId);
                const auto& bag=writer.document.extensions(ObjectRef(annotation.kind,annotation.id));
                auto ink=bag.findValue("studio.color.override");if(!ink)ink=bag.findValue("studio.color");
                auto part=[&](const std::vector<core::Vec2>& points,bool fill,bool closed){if(points.size()<2)return;
                    out<<"<curve";attributeInteger(out,"id",writer.reserveFileId());attributeInteger(out,"MoleculeStudioArrowPart",owner);
                    attributeInteger(out,"CurveType",(closed?1:0)|(fill?128:0));attribute(out,"Closed",closed?"yes":"no");
                    attribute(out,"FillType",fill?"Solid":"None");attributeNumber(out,"LineWidth",fill?0:width);
                    attribute(out,"MoleculeStudioInk",ink?*ink:"255");
                    std::ostringstream controls;for(auto q:points)for(int i=0;i<3;++i)controls<<numberToText(q.x)<<' '<<numberToText(q.y)<<' ';
                    attribute(out,"CurvePoints",controls.str());out<<"/>";
                };
                if(doubleArc&&geometry.shafts.size()>1)part(geometry.shafts[1],false,false);
                for(const auto& h:geometry.heads){if(!h.outline.empty())part(h.outline,h.filled,true);
                    for(size_t i=0;i+1<h.strokes.size();i+=2)part({h.strokes[i],h.strokes[i+1]},false,false);}
            }
            return;
        }
    }
    out << "<graphic";
    attributeInteger(out, "id",
                     writer.objectId(ObjectRef(annotation.kind,annotation.id),annotation.sourceId));
    switch (annotation.kind) {
    case ObjectKind::Arrow: attribute(out, "GraphicType", "Line"); attribute(out, "ArrowType", "FullHead"); break;
    case ObjectKind::Bracket: attribute(out, "GraphicType", "Bracket"); break;
    case ObjectKind::Orbital: attribute(out, "GraphicType", "Orbital"); break;
    case ObjectKind::Symbol: attribute(out, "GraphicType", "Symbol"); break;
    case ObjectKind::BioShape: attribute(out, "GraphicType", "Undefined"); break;
    default: attribute(out, "GraphicType", "Undefined"); break;
    }
    const core::Vec2 a = annotation.geometry[0];
    const core::Vec2 b = annotation.geometry[2];
    out << " BoundingBox=\"" << numberToText(a.x) << ' ' << numberToText(a.y) << ' '
        << numberToText(b.x) << ' ' << numberToText(b.y) << "\"";
    if (annotation.style.width > 0.0) attributeNumber(out, "LineWidth", annotation.style.width);
    replayPreservedAttributes(out, writer.document,
                              ObjectRef(annotation.kind, annotation.id));
    out << "/>";
}

// Raw regions are source fragments no object models (an unrecognised <graphic>, an
// unknown sibling element). They are written back VERBATIM, in the source's own
// ORDER relative to the modelled objects, because the bytes came from the source
// file: re-emitting them cannot invent or lose anything, and moving them would
// reorder the user's document.
struct RawRegionEntry {
    long long order;
    std::string bytes;
    bool operator<(const RawRegionEntry& other) const { return order < other.order; }
};
void collectRawRegions(const core::Document& document,
                       std::vector<RawRegionEntry>* out) {
    const core::ExtensionBag& bag = document.documentExtensions();
    const std::vector<core::ExtensionEntry>& entries = bag.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& key = entries[i].key;
        if (key.compare(0, 8, "pst.raw.") != 0) continue;
        if (key.compare(0, 18, "pst.raw.order.") == 0) continue;   // bookkeeping, not a region
        RawRegionEntry region;
        const std::string sourceId = key.substr(8);
        const std::string* recorded =
            bag.findValue(std::string(kExtensionPrefix) + "raw.order." + sourceId);
        region.order = 1000000000LL;
        if (recorded && !recorded->empty()) {
            std::istringstream in(*recorded);
            in.imbue(std::locale::classic());
            in >> region.order;
        }
        region.bytes = entries[i].value;
        out->push_back(region);
    }
}

void emitRawRegionsInRange(Writer& writer, long long from, long long to) {
    std::vector<RawRegionEntry> regions;
    collectRawRegions(writer.document, &regions);
    // Promoting a standard electron graphic into its owner atom removes its
    // drawing record, but its leading </fragment>/<group> bytes still belong
    // to the document. Emit those at the original source position.
    if(writer.options.mode==CdxmlExportMode::Lossless)for(auto kind:{ObjectKind::Arrow,ObjectKind::Symbol,ObjectKind::Orbital,ObjectKind::Bracket,ObjectKind::BioShape}){const auto& slab=kind==ObjectKind::Arrow?writer.document.arrows():kind==ObjectKind::Symbol?writer.document.symbols():kind==ObjectKind::Orbital?writer.document.orbitals():kind==ObjectKind::Bracket?writer.document.brackets():writer.document.bioShapes();for(const auto& a:slab.records())if(!a.isAlive()){
        ObjectRef ref(kind,a.id);const auto& bag=writer.document.extensions(ref);auto order=bag.findValue("pst.source.order"),raw=bag.findValue(sourceKey(kind,a.id));if(!order||!raw)continue;auto gap=sourceGap(*raw,a.sourceId);if(gap.empty())continue;RawRegionEntry entry;entry.order=std::strtoll(order->c_str(),nullptr,10);entry.bytes=gap;regions.push_back(entry);
    }}
    // Deleted ordinary objects also own leading container boundaries.
    if(writer.options.mode==CdxmlExportMode::Lossless){
        auto gap=[&](ObjectRef ref,int source){const auto& bag=writer.document.extensions(ref);auto order=bag.findValue("pst.source.order"),raw=bag.findValue(sourceKey(ref.kind,ref.id));if(!order||!raw)return;auto bytes=sourceGap(*raw,source);if(bytes.empty())return;RawRegionEntry entry;entry.order=std::strtoll(order->c_str(),nullptr,10);entry.bytes=bytes;regions.push_back(entry);};
        for(const auto& a:writer.document.atoms().records())if(!a.isAlive())gap({ObjectKind::Atom,a.id},a.sourceId);
        for(const auto& a:writer.document.bonds().records())if(!a.isAlive())gap({ObjectKind::Bond,a.id},a.sourceId);
        for(const auto& a:writer.document.texts().records())if(!a.isAlive())gap({ObjectKind::Text,a.id},a.sourceId);
        for(const auto& a:writer.document.paths().records())if(!a.isAlive())gap({ObjectKind::Path,a.id},a.sourceId);
    }
    std::stable_sort(regions.begin(), regions.end());
    for (size_t i = 0; i < regions.size(); ++i) {
        if (regions[i].order < from || regions[i].order >= to) continue;
        writer.out << regions[i].bytes;
        if (writer.fidelity) ++writer.fidelity->rawFragmentsEmitted;
    }
}

} // namespace


namespace {


// ---------------------------------------------------------------------------
// Edit detection
// ---------------------------------------------------------------------------
// "Was this object edited since it was read?" is answered by comparing a signature
// computed from the CURRENT model state with the one the reader stored. Comparing a
// signature rather than trusting a dirty flag is deliberate: a dirty flag can be
// set by a transaction that changed nothing, and cleared by a rollback, while a
// signature cannot disagree with the data.
std::string storedSignature(const core::Document& document, const ObjectRef& ref) {
    std::ostringstream key;
    key << kExtensionPrefix << "sign." << kindKeyName(ref.kind) << "." << ref.id;
    const std::string* value = document.extensions(ref).findValue(key.str());
    return value ? *value : std::string();
}

bool objectUnchanged(const core::Document& document, const ObjectRef& ref,
                     const std::string& current) {
    const std::string stored = storedSignature(document, ref);
    return !stored.empty() && stored == current;
}

// Source bytes the reader preserved for one object.
const std::string* preservedSource(const core::Document& document, const ObjectRef& ref) {
    return document.extensions(ref).findValue(sourceKey(ref.kind, ref.id));
}

// ---------------------------------------------------------------------------
// Document assembly
// ---------------------------------------------------------------------------
// Re-emit an object from its preserved source bytes when, and only when, the
// model still matches what was read. Returns true when the object was written
// from its source. The SOURCE bytes carry the object's original id and every
// attribute, including the ones this writer cannot promote, which is exactly how
// the unknown-attribute preservation survives into the output.
bool emitPreservedIfUnchanged(Writer& writer, const ObjectRef& ref,
                              const std::string& currentSignature) {
    if (writer.options.mode != CdxmlExportMode::Lossless) return false;
    const std::string* bytes = preservedSource(writer.document, ref);
    auto regenerate=[&](){if(bytes)writer.out<<sourceGap(*bytes,sourceIdOf(writer.document,ref));return false;};
    if(ref.kind==ObjectKind::Atom){
        const auto atom=writer.document.atoms().alive(ref.id);
        // Old saves contain the faulty generic encoding / bare charged label.
        // Regenerate these even when untouched so Save repairs existing files.
        if(!mechanism::genericRole(writer.document,ref.id).empty()||
           (atom&&atom->semantics.charge))return regenerate();
    }
    if(writer.document.extensions(ref).findValue("studio.text.rotation.edited"))return regenerate();
    if (!bytes || bytes->empty()) return false;
    if (!objectUnchanged(writer.document, ref, currentSignature)) return regenerate();
    const auto& appearance=writer.document.extensions(ref);if(appearance.findValue("studio.color.override")||appearance.findValue("studio.text.colors.edited")||appearance.findValue("studio.graphics.width")){tinyxml2::XMLDocument raw;if(raw.Parse(bytes->c_str())==tinyxml2::XML_SUCCESS&&raw.RootElement()&&raw.RootElement()->Attribute("id"))writer.fileId[ref]=raw.RootElement()->IntAttribute("id");}
    writer.out << *bytes;
    if (writer.fidelity) ++writer.fidelity->objectsFromSource;
    return true;
}

// ---------------------------------------------------------------------------
// Document colour table
// ---------------------------------------------------------------------------
// ChemDraw resolves an object's color/FillColor against the DOCUMENT table with a
// two-slot offset: attribute 0 is the default ink, 1 is the paper, and attribute
// 2+n is colortable entry n. The page itself is painted with entry 0. That makes
// two saved shapes unreadable in ChemDraw 26: a prologue with no table at all
// (nothing defines the page, and every explicit index is undefined) and a table
// whose first entry is not white (the page is painted with that entry, so a lone
// black entry paints the whole page black). Both were reproduced with ChemDraw 26
// on files this tool wrote. Every save therefore normalises the table to
// ChemDraw's own eight entries and rewrites object indices through the OLD table,
// so a file that used entry 0 as black ink keeps black ink instead of silently
// turning white.
struct ColorEntry { unsigned r, g, b; };

// ChemDraw 26's stock palette, in the order ChemDraw writes it.
const unsigned kChemDrawPalette[8][3] = {
    {255, 255, 255}, {0, 0, 0}, {255, 0, 0}, {255, 255, 0},
    {0, 255, 0}, {0, 255, 255}, {0, 0, 255}, {255, 0, 255}
};

// The same eight entries as CDXML, spelled the way ChemDraw spells them.
const char kChemDrawPaletteXml[] =
    "<colortable>\n"
    "<color r=\"1\" g=\"1\" b=\"1\"/>\n"
    "<color r=\"0\" g=\"0\" b=\"0\"/>\n"
    "<color r=\"1\" g=\"0\" b=\"0\"/>\n"
    "<color r=\"1\" g=\"1\" b=\"0\"/>\n"
    "<color r=\"0\" g=\"1\" b=\"0\"/>\n"
    "<color r=\"0\" g=\"1\" b=\"1\"/>\n"
    "<color r=\"0\" g=\"0\" b=\"1\"/>\n"
    "<color r=\"1\" g=\"0\" b=\"1\"/>\n"
    "</colortable>\n";

unsigned paletteChannel(const XMLElement* color, const char* name) {
    double value = color->DoubleAttribute(name, 0.0);
    if (!(value > 0.0)) return 0;                 // NaN, negative and zero channels
    if (value > 1.0) value = 1.0;                 // some writers store 0..255 by mistake
    return static_cast<unsigned>(std::floor(value * 255.0 + 0.5));
}

bool sameEntry(const ColorEntry& a, const ColorEntry& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

bool canonicalEntry(size_t index, const ColorEntry& entry) {
    return index < 8 && entry.r == kChemDrawPalette[index][0] &&
           entry.g == kChemDrawPalette[index][1] && entry.b == kChemDrawPalette[index][2];
}

// Every attribute name CDXML uses for a colour slot, on any element.
bool colorSlotAttribute(const char* name) {
    return name && (!std::strcmp(name, "color") || !std::strcmp(name, "Color") ||
                    !std::strcmp(name, "FillColor") || !std::strcmp(name, "bgcolor"));
}

bool parseColorSlot(const char* text, int* value) {
    if (!text || !*text) return false;
    char* end = 0;
    long parsed = std::strtol(text, &end, 10);
    if (!end || *end || parsed < 0 || parsed > 100000) return false;
    *value = static_cast<int>(parsed);
    return true;
}

void visitElements(XMLElement* element, const std::function<void(XMLElement*)>& visit) {
    visit(element);
    for (XMLElement* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
        visitElements(child, visit);
}

void appendPaletteEntry(tinyxml2::XMLDocument& xml, XMLElement* table, const ColorEntry& entry) {
    XMLElement* color = xml.NewElement("color");
    color->SetAttribute("r", double(entry.r) / 255.0);
    color->SetAttribute("g", double(entry.g) / 255.0);
    color->SetAttribute("b", double(entry.b) / 255.0);
    table->InsertEndChild(color);
}

std::string printXml(tinyxml2::XMLDocument& xml) {
    tinyxml2::XMLPrinter printer;
    xml.Print(&printer);
    return printer.CStr();
}

Error writeDocument(Writer& writer) {
    const core::Document& document = writer.document;
    std::ostringstream& out = writer.out;

    // Font table. Fonts are numbered by first use, and the mapping is written into
    // the document bag as pst.font.<n> = "<id>\x1f<name>" so a re-read assigns the
    // SAME numbers and byte identity is reproducible rather than accidental.
    std::vector<core::ObjectRef> objectOrder;

    assignAtomIds(writer);

    const std::vector<core::AtomRecord>& atoms = document.atoms().records();
    const std::vector<core::BondRecord>& bonds = document.bonds().records();
    const std::vector<core::TextRecord>& texts = document.texts().records();
    const std::vector<core::PathRecord>& paths = document.paths().records();

    for (size_t i = 0; i < atoms.size(); ++i) if (atoms[i].isAlive())
        objectOrder.push_back(ObjectRef(ObjectKind::Atom, atoms[i].id));
    for (size_t i = 0; i < bonds.size(); ++i) if (bonds[i].isAlive())
        objectOrder.push_back(ObjectRef(ObjectKind::Bond, bonds[i].id));
    for (size_t i = 0; i < texts.size(); ++i) if (texts[i].isAlive())
        objectOrder.push_back(ObjectRef(ObjectKind::Text, texts[i].id));
    for (size_t i = 0; i < paths.size(); ++i) if (paths[i].isAlive())
        objectOrder.push_back(ObjectRef(ObjectKind::Path, paths[i].id));
    for(const auto& table:document.tables().records())if(table.isAlive())objectOrder.push_back(ObjectRef(ObjectKind::Table,table.id));

    const core::ObjectSlab<core::AnnotationRecord>* const slabs[5] = {
        &document.arrows(), &document.brackets(), &document.orbitals(),
        &document.symbols(), &document.bioShapes()
    };
    const ObjectKind slabKinds[5] = { ObjectKind::Arrow, ObjectKind::Bracket,
                                      ObjectKind::Orbital, ObjectKind::Symbol,
                                      ObjectKind::BioShape };
    for (int s = 0; s < 5; ++s) {
        const std::vector<core::AnnotationRecord>& records = slabs[s]->records();
        for (size_t i = 0; i < records.size(); ++i)
            if (records[i].isAlive())
                objectOrder.push_back(ObjectRef(slabKinds[s], records[i].id));
    }

    for(const auto& entry:document.documentExtensions().entries())if(entry.key.compare(0,9,"pst.font.")==0){auto sep=entry.value.find(char(31));if(sep==std::string::npos)continue;int id=std::atoi(entry.value.substr(0,sep).c_str());auto name=entry.value.substr(sep+1);if(id>0&&!name.empty()){writer.fontNames.push_back(name);writer.fontIdByName[name]=id;writer.sourceFontIds.insert(id);writer.nextFontId=std::max(writer.nextFontId,id+1);}}
    // Pre-register every font so the table is complete before the header is
    // printed, and so a run never references a font that has no entry.
    writer.fontFor(document.defaultFont());
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) continue;
        writer.fontFor(texts[i].format.fontName);
        const std::vector<core::TextRun>& runs = document.runs(texts[i].id);
        for (size_t k = 0; k < runs.size(); ++k) writer.fontFor(runs[k].fontName);
    }
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        const std::vector<core::TextRun>& runs = studioatom::runs(document,atoms[i].id);
        for (size_t k = 0; k < runs.size(); ++k) writer.fontFor(runs[k].fontName);
    }

    if(auto font=document.documentExtensions().findValue("studio.style.LabelFontName"))writer.fontFor(*font);
    // ---- header ---------------------------------------------------------
    // In Lossless mode the ENTIRE prologue (XML declaration, DOCTYPE, root open tag
    // with every attribute, font table, and the source's own whitespace) is written
    // back verbatim. Regenerating it cannot reproduce attribute spelling such as
    // LineWidth="0.6" vs "0.59999999999999998", the choice of which defaults the
    // source bothered to write, or the font table layout - all of which are part of
    // the file the user has. Byte identity is only claimed in this mode.
    const std::string* prologue =
        document.documentExtensions().findValue(std::string(kExtensionPrefix) + "source.prologue");
    if (writer.options.mode == CdxmlExportMode::Lossless && prologue && !prologue->empty()) {
        // The prologue is emitted VERBATIM, with no added separator: its final bytes
        // are the container open tag, and the first object's preserved slice already
        // begins with the whitespace that followed it.
        std::string header=*prologue;std::ostringstream extra;
        for(const auto& name:writer.fontNames){int id=writer.fontIdByName[name];if(writer.sourceFontIds.count(id))continue;std::string escaped;escapeAttribute(name,&escaped);extra<<"<font id=\""<<id<<"\" charset=\"iso-8859-1\" name=\""<<escaped<<"\"/>";}
        auto end=header.find("</fonttable>");if(end!=std::string::npos)header.insert(end,extra.str());else if(!extra.str().empty()){auto page=header.find("<page");header.insert(page==std::string::npos?header.size():page,"<fonttable>"+extra.str()+"</fonttable>");}
        out << header;
    } else {
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << "<CDXML";
        if (!document.name().empty()) attribute(out, "Name", document.name());
        attributeNumber(out, "BondLength", document.defaultBondLength());
        if (document.defaultLineWidth() > 0.0)
            attributeNumber(out, "LineWidth", document.defaultLineWidth());
        if (document.defaultTextSize() > 0.0)
            attributeNumber(out, "CaptionSize", document.defaultTextSize());
        {auto label=document.documentExtensions().findValue("studio.style.LabelSize");double points=label?std::atof(label->c_str()):10.;attributeNumber(out,"LabelSize",points>0?points:10.);}
        if (!document.defaultFont().empty())
            attributeInteger(out, "CaptionFont", writer.fontFor(document.defaultFont()));
        {std::string font=document.defaultFont();if(auto f=document.documentExtensions().findValue("studio.style.LabelFontName"))font=*f;attributeInteger(out,"LabelFont",writer.fontFor(font));}
        {
            const core::ExtensionBag& bag = document.documentExtensions();
            const std::vector<core::ExtensionEntry>& entries = bag.entries();
            std::set<std::string> written;
            for (size_t i = 0; i < entries.size(); ++i) {
                const std::string& key = entries[i].key;
                if (key.compare(0, 9, "pst.attr.") != 0) continue;
                const std::string name = key.substr(9);
                if (name == "Name" || name == "BondLength" || name == "LineWidth" ||
                    name == "CaptionSize" || name == "CaptionFont" || name == "LabelSize" ||
                    name == "LabelFont") continue;
                if (written.count(name)) continue;
                attribute(out, name.c_str(), entries[i].value);
                if (writer.fidelity) ++writer.fidelity->unknownAttributesEmitted;
                written.insert(name);
            }
        }
        out << ">\n";
        // ChemDraw paints the page with colortable entry 0 and resolves an object's
        // color attribute through the same table: 0 is the default ink, 1 is the
        // paper, and attribute 2+n is entry n. A regenerated prologue that writes no
        // table therefore leaves the page to the importer's default and makes every
        // explicit index undefined - which is one of the two shapes of the reported
        // black page. Emit ChemDraw's own eight entries, byte for byte.
        out << kChemDrawPaletteXml;
        if (!writer.fontNames.empty()) {
            out << "<fonttable>";
            for (size_t i = 0; i < writer.fontNames.size(); ++i) {
                std::string escaped;
                escapeAttribute(writer.fontNames[i], &escaped);
                out << "<font id=\"" << writer.fontIdByName[writer.fontNames[i]]
                    << "\" charset=\"iso-8859-1\" name=\"" << escaped << "\"/>";
            }
            out << "</fonttable>\n";
        }
    }

    // Re-emit in the SOURCE's order when the reader recorded one, so a file whose
    // nodes and bonds are interleaved comes back interleaved. Objects drawn in this
    // session have no recorded order and sort after every loaded object, in id order.
    {
        std::vector<std::pair<long long, core::ObjectRef> > keyed;
        keyed.reserve(objectOrder.size());
        for (size_t i = 0; i < objectOrder.size(); ++i) {
            const std::string* recorded =
                document.extensions(objectOrder[i]).findValue(
                    std::string(kExtensionPrefix) + "source.order");
            long long position = 0;
            if (recorded && !recorded->empty()) {
                std::istringstream in(*recorded);
                in.imbue(std::locale::classic());
                if (!(in >> position)) position = 0;
            } else {
                position = 1000000000LL + static_cast<long long>(objectOrder[i].id);
            }
            keyed.push_back(std::make_pair(position, objectOrder[i]));
        }
        std::stable_sort(keyed.begin(), keyed.end());
        objectOrder.clear();
        for (size_t i = 0; i < keyed.size(); ++i) objectOrder.push_back(keyed[i].second);
    }

    // ---- pages and fragments --------------------------------------------
    // A container is opened only when the document actually has more than one
    // page or more than one fragment; otherwise the output keeps the flat, single
    // page/fragment shape the simple fixtures use, which is what makes the
    // no-edit byte comparison exact for the common case.
    // Containers come from the source when available; otherwise a fresh page and
    // fragment are opened. The container ids are NOT taken from the object id pool,
    // because they are container identity, not object identity.
    if (!(writer.options.mode == CdxmlExportMode::Lossless && prologue && !prologue->empty())) {
        out << "<page id=\"" << writer.reserveFileId() << "\">\n";
        out << "<fragment id=\"" << writer.reserveFileId() << "\">\n";
    }
    long long rawCursor = 0;
    for (size_t i = 0; i < objectOrder.size(); ++i) {
        const ObjectRef& ref = objectOrder[i];
        bool preservedEmitted = false;
        // Emit every preserved raw region that comes BEFORE this object in the source,
        // so an unknown <graphic> stays between the same neighbours it had.
        {
            const std::string* recorded =
                document.extensions(ref).findValue(std::string(kExtensionPrefix) + "source.order");
            long long position = 1000000000LL;
            if (recorded && !recorded->empty()) {
                std::istringstream in(*recorded);
                in.imbue(std::locale::classic());
                in >> position;
            }
            emitRawRegionsInRange(writer, rawCursor, position);
            rawCursor = position;
        }
        switch (ref.kind) {
        case ObjectKind::Atom: {
            const core::AtomRecord* atom = document.atoms().get(ref.id);
            if (!atom) break;
            if (emitPreservedIfUnchanged(writer, ref, atomSignature(document, *atom))) { preservedEmitted = true; break; }
            emitAtom(writer, *atom);
            break;
        }
        case ObjectKind::Bond: {
            const core::BondRecord* bond = document.bonds().get(ref.id);
            if (!bond) break;
            if (emitPreservedIfUnchanged(writer, ref, bondSignature(*bond))) { preservedEmitted = true; break; }
            emitBond(writer, *bond);
            break;
        }
        case ObjectKind::Text: {
            const core::TextRecord* text = document.texts().get(ref.id);
            if (!text) break;
            if (emitPreservedIfUnchanged(writer, ref, textSignature(document, *text))) { preservedEmitted = true; break; }
            emitText(writer, *text);
            break;
        }
        case ObjectKind::Path: {
            const core::PathRecord* path = document.paths().get(ref.id);
            if (!path) break;
            if (emitPreservedIfUnchanged(writer, ref, pathSignature(document, *path))) {
                preservedEmitted = true;
                break;
            }
            emitPath(writer, *path);
            break;
        }
        case ObjectKind::Arrow: {
            const core::AnnotationRecord* a = document.arrows().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        case ObjectKind::Bracket: {
            const core::AnnotationRecord* a = document.brackets().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        case ObjectKind::Orbital: {
            const core::AnnotationRecord* a = document.orbitals().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        case ObjectKind::Symbol: {
            if(experimental::isFigureObject(document,ref)){auto e=emitExperimentalFigure(writer,ref.id);if(!e.ok())return e;break;}
            const core::AnnotationRecord* a = document.symbols().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        case ObjectKind::BioShape: {
            const core::AnnotationRecord* a = document.bioShapes().get(ref.id);
            if (!a) break;
            if (emitPreservedIfUnchanged(writer, ref, annotationSignature(*a))) {
                preservedEmitted = true;
                break;
            }
            emitAnnotation(writer, *a);
            break;
        }
        case ObjectKind::Table: {
            graphics::TableData data;bool used=false;auto e=graphics::readTable(document,ref.id,&data,&used);if(!e.ok())return e;
            const auto lines=graphics::tableSegments(document,ref.id,data);
            for(auto& cell:data.cells)if(cell.kind==graphics::CellKind::Text){auto text=document.texts().alive(cell.text);if(!text)return fail("table text is missing",ErrorCode::NotFound);cell.text=writer.objectId(ObjectRef(ObjectKind::Text,text->id),text->sourceId);}
            out<<"<group";attributeInteger(out,"id",writer.objectId(ref,document.tables().get(ref.id)->sourceId));
            attribute(out,"MoleculeStudioTable",graphics::encodeTableParams(data));attribute(out,"MoleculeStudioStyle",graphics::tableStyle(document,ref.id));
            std::ostringstream box;box.precision(17);box<<data.grid.frame.min.x<<" "<<data.grid.frame.min.y<<" "<<data.grid.frame.max.x<<" "<<data.grid.frame.max.y;attribute(out,"BoundingBox",box.str());out<<">";
            for(const auto& line:lines){out<<"<graphic";attributeInteger(out,"id",writer.reserveFileId());attribute(out,"GraphicType",std::string("Line"));
                std::ostringstream b;b.precision(17);b<<line.a.x<<" "<<line.a.y<<" "<<line.b.x<<" "<<line.b.y;attribute(out,"BoundingBox",b.str());attributeNumber(out,"LineWidth",line.width);if(line.dashed)attribute(out,"LineType",std::string("Dashed"));out<<"/>";}
            out<<"</group>";break;
        }
        default:
            // A slab the model can hold but this writer cannot lay out. That is a
            // LOSS, and it is refused rather than silently omitted.
            if (writer.options.failOnLossyEntities)
                return fail(std::string("cannot write object of kind ") +
                            core::kindName(ref.kind), ErrorCode::NotImplemented);
            if (writer.refusals) writer.refuse(core::kindName(ref.kind),
                                               "object kind is not writable", 0, ref);
            break;
        }
        // A PRESERVED region already contains the whitespace that followed the
        // previous object, so no separator is added; a REGENERATED object gets one
        // because its generator does not emit trailing whitespace.
        if (!preservedEmitted) out << "\n";
    }
    // Any raw region recorded AFTER the last modelled object (for example an unknown
    // element that came last in the source) still has to be written. Regions that sit
    // between modelled objects were already emitted by the interleaving above, so this
    // final flush only covers the tail - running it twice would duplicate them.
    emitRawRegionsInRange(writer, rawCursor, 1000000000LL);
    // The epilogue is the source's own closing bytes when available, so the trailing
    // whitespace is the file's, not the generator's. Regenerating it is what turns a
    // 2-byte difference into a "not identical" verdict.
    const std::string* epilogue =
        document.documentExtensions().findValue(std::string(kExtensionPrefix) + "source.epilogue");
    if (writer.options.mode == CdxmlExportMode::Lossless && epilogue && !epilogue->empty())
        out << *epilogue;
    else
        out << "\n</fragment>\n</page>\n</CDXML>\n";
    return Error::success();
}

// Normalise the document colour table after the prologue/objects are assembled.
// A source that already carries ChemDraw's table is returned UNCHANGED (its bytes
// are the user's file), so the lossless round trip keeps working; a source with a
// missing, short or non-canonical table is rewritten, and every colour slot is
// re-pointed so each object resolves to the colour it resolved to before.
std::string normalizeColorTable(const std::string& source) {
    tinyxml2::XMLDocument xml;
    if (xml.Parse(source.c_str()) != tinyxml2::XML_SUCCESS) return source;
    XMLElement* root = xml.RootElement();
    if (!root || !root->Name() || std::strcmp(root->Name(), "CDXML") != 0) return source;

    std::vector<XMLElement*> tables;
    for (XMLElement* child = root->FirstChildElement("colortable"); child;
         child = child->NextSiblingElement("colortable"))
        tables.push_back(child);
    XMLElement* table = tables.empty() ? 0 : tables[0];

    std::vector<ColorEntry> entries;
    if (table) {
        for (XMLElement* color = table->FirstChildElement("color"); color;
             color = color->NextSiblingElement("color"))
            entries.push_back(ColorEntry{paletteChannel(color, "r"),
                                         paletteChannel(color, "g"),
                                         paletteChannel(color, "b")});
    }

    bool canonical = entries.size() >= 8;
    for (size_t i = 0; canonical && i < 8; ++i) canonical = canonicalEntry(i, entries[i]);

    int highestSlot = -1;
    visitElements(root, [&](XMLElement* element) {
        for (const tinyxml2::XMLAttribute* a = element->FirstAttribute(); a; a = a->Next()) {
            int slot = 0;
            if (colorSlotAttribute(a->Name()) && parseColorSlot(a->Value(), &slot) && slot > highestSlot)
                highestSlot = slot;
        }
    });

    if (canonical) {
        // Slot 2+n addresses entry n, so the table only has to grow when an object
        // addresses a slot the source table never defined. Those slots mean black.
        size_t needed = highestSlot >= 2 ? size_t(highestSlot - 1) : 0;
        bool dropDuplicate = tables.size() > 1;
        if (!dropDuplicate && entries.size() >= needed) return source;
        for (size_t i = 1; i < tables.size(); ++i) root->DeleteChild(tables[i]);
        while (entries.size() < needed) {
            ColorEntry black = {0, 0, 0};
            appendPaletteEntry(xml, table, black);
            entries.push_back(black);
        }
        return printXml(xml);
    }

    // Rebuild from ChemDraw's eight entries and keep any colour the source actually
    // used after them. The map is memoised so repeated ink costs one lookup.
    std::vector<ColorEntry> rebuilt;
    for (int i = 0; i < 8; ++i)
        rebuilt.push_back(ColorEntry{kChemDrawPalette[i][0], kChemDrawPalette[i][1],
                                     kChemDrawPalette[i][2]});
    std::map<int, int> rewritten;
    std::function<int(int)> slotFor = [&](int slot) -> int {
        std::map<int, int>::const_iterator known = rewritten.find(slot);
        if (known != rewritten.end()) return known->second;
        int result = slot;
        if (slot >= 2) {
            // What the OLD table made this slot mean; an undefined slot was black.
            ColorEntry wanted = size_t(slot - 2) < entries.size()
                                    ? entries[slot - 2]
                                    : ColorEntry{0, 0, 0};
            result = -1;
            for (size_t i = 0; i < rebuilt.size(); ++i)
                if (sameEntry(rebuilt[i], wanted)) { result = int(i) + 2; break; }
            if (result < 0) {
                rebuilt.push_back(wanted);
                result = int(rebuilt.size()) + 1;   // slot 2+n for the appended entry
            }
        }
        rewritten[slot] = result;
        return result;
    };

    visitElements(root, [&](XMLElement* element) {
        std::vector<std::pair<std::string, std::string> > updates;
        for (const tinyxml2::XMLAttribute* a = element->FirstAttribute(); a; a = a->Next()) {
            int slot = 0;
            if (!colorSlotAttribute(a->Name()) || !parseColorSlot(a->Value(), &slot)) continue;
            int mapped = slotFor(slot);
            if (mapped != slot) updates.push_back(std::make_pair(std::string(a->Name()), std::to_string(mapped)));
        }
        for (size_t i = 0; i < updates.size(); ++i)
            element->SetAttribute(updates[i].first.c_str(), updates[i].second.c_str());
    });

    if (!table) {
        table = xml.NewElement("colortable");
        if (root->FirstChildElement()) root->InsertFirstChild(table); else root->InsertEndChild(table);
    } else {
        table->DeleteChildren();
    }
    for (size_t i = 0; i < rebuilt.size(); ++i) appendPaletteEntry(xml, table, rebuilt[i]);
    return printXml(xml);
}

// Apply explicit editor appearance and attached NMR tags after lossless source
// regions have been emitted. Unedited files retain their original byte stream.
std::string applyStudioAppearance(Writer& writer,const std::string& source){
    std::map<ObjectRef,int32_t> objects=writer.fileId;for(const auto& atom:writer.atomFileId)objects[ObjectRef(ObjectKind::Atom,atom.first)]=atom.second;
    bool needed=false;
    for(const auto& entry:objects){const auto& bag=writer.document.extensions(entry.first);
        if(entry.first.kind==ObjectKind::Path||experimental::isFigureObject(writer.document,entry.first)||bag.findValue("studio.color.override")||bag.findValue("studio.text.colors.edited")||bag.findValue("studio.graphics.width"))needed=true;
        if(auto tags=bag.findValue("studio.atom.tags"))if(tags->find("MoleculeStudio.NMR.")!=std::string::npos)needed=true;
    }
    if(!needed)return source;
    tinyxml2::XMLDocument xml;if(xml.Parse(source.c_str())!=tinyxml2::XML_SUCCESS)return source;
    auto root=xml.RootElement();std::map<int,XMLElement*> nodes;
    std::function<void(XMLElement*)> visit=[&](XMLElement* e){if(e->Attribute("id"))nodes[e->IntAttribute("id")]=e;for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())visit(c);};visit(root);
    auto table=root->FirstChildElement("colortable");if(!table){
        table=xml.NewElement("colortable");root->InsertFirstChild(table);
        // ChemDraw's own table: entry 0 is the white page, 1 the black default ink.
        // Slots 2+n address entry n, so appended colours start at slot 2+entries.
        for(int i=0;i<8;++i)appendPaletteEntry(xml,table,ColorEntry{kChemDrawPalette[i][0],kChemDrawPalette[i][1],kChemDrawPalette[i][2]});
    }
    int count=2;for(auto c=table->FirstChildElement("color");c;c=c->NextSiblingElement("color"))++count;
    std::map<uint32_t,int> palette;
    // Reuse the slot a colour already occupies instead of appending a duplicate, and
    // give black/white their ChemDraw defaults (0 = ink, 1 = paper) so a recoloured
    // structure does not grow a table it does not need.
    auto colorFor=[&](uint32_t rgba){
        auto old=palette.find(rgba);if(old!=palette.end())return old->second;
        unsigned red=(rgba>>24)&255,green=(rgba>>16)&255,blue=(rgba>>8)&255;int id=-1;
        if(!red&&!green&&!blue)id=0;else if(red==255&&green==255&&blue==255)id=1;
        else{int slot=0;for(auto c=table->FirstChildElement("color");c;c=c->NextSiblingElement("color"),++slot)if(paletteChannel(c,"r")==red&&paletteChannel(c,"g")==green&&paletteChannel(c,"b")==blue){id=2+slot;break;}}
        if(id<0){id=count++;auto c=xml.NewElement("color");c->SetAttribute("r",double(red)/255);c->SetAttribute("g",double(green)/255);c->SetAttribute("b",double(blue)/255);table->InsertEndChild(c);}
        palette[rgba]=id;return id;};
    std::function<void(XMLElement*)> colorFigures=[&](XMLElement* e){
        if(auto value=e->Attribute("MoleculeStudioInk")){e->SetAttribute("color",colorFor(uint32_t(std::strtoull(value,0,10))));e->DeleteAttribute("MoleculeStudioInk");}
        if(auto value=e->Attribute("MoleculeStudioFillInk")){e->SetAttribute("FillColor",colorFor(uint32_t(std::strtoull(value,0,10))));e->DeleteAttribute("MoleculeStudioFillInk");}
        for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())colorFigures(c);
    };colorFigures(root);
    for(const auto& entry:objects){auto found=nodes.find(entry.second);if(found==nodes.end())continue;auto e=found->second;auto ref=entry.first;const auto& bag=writer.document.extensions(ref);
        if(experimental::isFigureObject(writer.document,ref))continue;
        if(ref.kind==ObjectKind::Path&&e->Attribute("MoleculeStudioPath")){auto path=writer.document.paths().get(ref.id);if(path){e->SetAttribute("color",colorFor(path->style.colorRGBA));if(path->fillEnabled)e->SetAttribute("FillColor",colorFor(path->fillRGBA));}}
        if(ref.kind!=ObjectKind::Bond&&ref.kind!=ObjectKind::Atom&&ref.kind!=ObjectKind::Text)if(auto width=bag.findValue("studio.graphics.width")){
            double value=std::atof(width->c_str());e->SetAttribute("LineWidth",value);
        }
        if(auto value=bag.findValue("studio.color.override")){
            int color=colorFor(uint32_t(std::strtoull(value->c_str(),0,10)));e->SetAttribute("color",color);
            auto t=ref.kind==ObjectKind::Text?e:e->FirstChildElement("t");if(t){t->SetAttribute("color",color);for(auto run=t->FirstChildElement("s");run;run=run->NextSiblingElement("s"))run->SetAttribute("color",color);}
        }
        if(bag.findValue("studio.text.colors.edited")){auto t=ref.kind==ObjectKind::Text?e:e->FirstChildElement("t");auto value=bag.findValue("studio.text.colors");if(t&&value){std::istringstream colors(*value);uint32_t rgba;for(auto run=t->FirstChildElement("s");run&&colors>>rgba;run=run->NextSiblingElement("s"))run->SetAttribute("color",colorFor(rgba));}}
        if(ref.kind!=ObjectKind::Atom)continue;auto raw=bag.findValue("studio.atom.tags");if(!raw||raw->find("MoleculeStudio.NMR.")==std::string::npos)continue;
        tinyxml2::XMLDocument tags;if(tags.Parse(raw->c_str())!=tinyxml2::XML_SUCCESS)continue;
        for(auto c=e->FirstChildElement("objecttag");c;){auto next=c->NextSiblingElement("objecttag");auto name=c->Attribute("Name");if(name&&std::string(name).find("MoleculeStudio.NMR.")==0)e->DeleteChild(c);c=next;}
        auto atom=writer.document.atoms().get(ref.id);
        for(auto tag=tags.RootElement()->FirstChildElement("tag");tag;tag=tag->NextSiblingElement("tag")){
            auto name=tag->Attribute("name");if(!name||std::string(name).find("MoleculeStudio.NMR.")!=0)continue;
            auto object=xml.NewElement("objecttag");object->SetAttribute("Name",name);object->SetAttribute("TagType","String");auto text=xml.NewElement("t");
            std::ostringstream point;point.precision(12);point<<atom->position.x+tag->DoubleAttribute("dx")<<' '<<atom->position.y+tag->DoubleAttribute("dy");text->SetAttribute("p",point.str().c_str());
            int color=colorFor(tag->UnsignedAttribute("color",0x0000ffff));
            for(auto run=tag->FirstChildElement("s");run;run=run->NextSiblingElement("s")){auto r=xml.NewElement("s");r->SetAttribute("font",writer.fontFor(run->Attribute("font")?run->Attribute("font"):"Arial"));r->SetAttribute("size",run->DoubleAttribute("size",8));r->SetAttribute("face",faceFromTextFlags(run->UnsignedAttribute("flags")));r->SetAttribute("color",color);r->SetText(run->GetText()?run->GetText():"");text->InsertEndChild(r);}
            object->InsertEndChild(text);e->InsertEndChild(object);
        }
    }
    tinyxml2::XMLPrinter printer;xml.Print(&printer);return printer.CStr();
}

// Recompose native projection groups from current membership. Flattening their
// old envelopes first also makes ungroup/delete/save behave correctly, without
// touching any other source group or fragment.
std::string wrapProjectionGroups(Writer& writer,const std::string& bytes){
    auto projection=[](const std::string& n){return n=="Fischer projection"||n=="Sawhorse projection"||n=="Newman projection";};
    bool needed=bytes.find(" projection\"")!=std::string::npos;
    for(const auto& g:writer.document.groups().records())if(g.isAlive()&&projection(g.label))needed=true;
    if(!needed)return bytes;
    tinyxml2::XMLDocument xml;if(xml.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS)return bytes;
    std::function<void(XMLElement*)> flatten=[&](XMLElement* parent){
        for(auto e=parent->FirstChildElement();e;){auto next=e->NextSiblingElement();flatten(e);
            if(std::string(e->Name())=="group"&&projection(e->Attribute("Name")?e->Attribute("Name"):"")){
                tinyxml2::XMLNode* previous=e;
                while(auto child=e->FirstChild()){parent->InsertAfterChild(previous,child);previous=child;}
                parent->DeleteChild(e);
            }e=next;
        }
    };flatten(xml.RootElement());
    std::map<int,XMLElement*> nodes;
    std::function<void(XMLElement*)> scan=[&](XMLElement* e){if(e->Attribute("id"))nodes[e->IntAttribute("id")]=e;for(auto c=e->FirstChildElement();c;c=c->NextSiblingElement())scan(c);};scan(xml.RootElement());
    for(const auto& g:writer.document.groups().records())if(g.isAlive()&&projection(g.label)){
        std::vector<XMLElement*> members;for(auto ref:g.members){auto id=writer.fileId.find(ref);int file=id==writer.fileId.end()?sourceIdOf(writer.document,ref):id->second;auto node=nodes.find(file);if(node!=nodes.end())members.push_back(node->second);}
        if(members.size()<2)continue;auto parent=members.front()->Parent();bool same=true;for(auto e:members)same&=e->Parent()==parent;if(!same)continue;
        auto group=xml.NewElement("group");group->SetAttribute("id",writer.objectId(ObjectRef(ObjectKind::Group,g.id),g.sourceId));group->SetAttribute("Name",g.label.c_str());
        parent->InsertAfterChild(members.front(),group);for(auto e:members)group->InsertEndChild(e);
    }
    tinyxml2::XMLPrinter out;xml.Print(&out);return out.CStr();
}

std::string wrapJoinedArrows(const std::string& bytes){
    tinyxml2::XMLDocument xml;if(xml.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS)return bytes;std::map<int,std::vector<tinyxml2::XMLElement*>> groups;int next=1;
    std::function<void(tinyxml2::XMLElement*)> scan=[&](tinyxml2::XMLElement* e){for(;e;e=e->NextSiblingElement()){next=std::max(next,e->IntAttribute("id",0)+1);int group=e->IntAttribute("MoleculeStudioJoinedGroup",0);if(group)groups[group].push_back(e);scan(e->FirstChildElement());}};scan(xml.RootElement());if(groups.empty())return bytes;
    for(const auto& row:groups){if(row.second.size()<2)continue;auto parent=row.second.front()->Parent();bool same=true;for(auto node:row.second)same&=node->Parent()==parent;if(!same)continue;auto group=xml.NewElement("group");group->SetAttribute("id",next++);group->SetAttribute("MoleculeStudioArrowGroup",1);parent->InsertEndChild(group);for(auto node:row.second)group->InsertEndChild(node);}
    tinyxml2::XMLPrinter out;xml.Print(&out);return out.CStr();
}
} // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------
std::string normalizePartialChargeCaptions(const std::string& source){
    if(source.find("δ")==std::string::npos)return source;
    tinyxml2::XMLDocument xml;if(xml.Parse(source.c_str())!=tinyxml2::XML_SUCCESS)return source;
    bool changed=false;
    std::function<void(tinyxml2::XMLElement*)> visit=[&](tinyxml2::XMLElement* e){
        if(std::string(e->Name())=="t"&&(!e->Parent()->ToElement()||std::string(e->Parent()->ToElement()->Name())!="n")){
            std::string label;for(auto run=e->FirstChildElement("s");run;run=run->NextSiblingElement("s"))if(run->GetText())label+=run->GetText();
            if(label=="δ−"||label=="δ-"||label=="δ+"){
                // A partial-charge caption is not a standalone chemical formula.
                e->SetAttribute("InterpretChemically","no");e->DeleteAttribute("Warning");changed=true;
            }
        }
        for(auto child=e->FirstChildElement();child;child=child->NextSiblingElement())visit(child);
    };if(xml.RootElement())visit(xml.RootElement());
    if(!changed)return source;tinyxml2::XMLPrinter printer;xml.Print(&printer);return printer.CStr();
}
std::string normalizeElectronEnums(std::string source){
    // Older Studio exports wrote the binary enum value into the XML enum.
    // Repair only node attributes; literal captions and valid source bytes stay.
    size_t cursor=0;const char* names[]={"None","Singlet","Doublet","Triplet"};
    while((cursor=source.find("<n ",cursor))!=std::string::npos){auto end=source.find('>',cursor);if(end==std::string::npos)break;auto key=source.find("Radical=",cursor);if(key<end){auto q=key+8;if(q+2<end&&(source[q]=='\"'||source[q]=='\'')){char quote=source[q];char number=source[q+1];if(number>='0'&&number<='3'&&source[q+2]==quote){source.replace(q+1,1,names[number-'0']);end=source.find('>',cursor);}}}cursor=end+1;
    }return source;
}
core::Error writeCdxml(const core::Document& document, std::ostream& output,
                       RefusalReport* refusals, CdxmlWriteFidelity* fidelity,
                       const CdxmlWriteOptions& options) {
    RefusalReport localRefusals;
    CdxmlWriteFidelity localFidelity;
    Writer writer(document, options, refusals ? refusals : &localRefusals,
                  fidelity ? fidelity : &localFidelity);

    Error valid = document.validate();
    if (!valid.ok()) return valid;
    Error planned = planIds(writer);
    if (!planned.ok()) return planned;
    Error written = writeDocument(writer);
    if (!written.ok()) return written;

    // The table is normalised BEFORE appearance is applied: normalizeColorTable can
    // rewrite the whole prologue (ChemDraw's table has to live at the root), and the
    // appearance pass must then append its own colours to the final table.
    const std::string text = normalizePartialChargeCaptions(normalizeElectronEnums(studio_electron_graphics::exportSymbols(document,writer.atomFileId,wrapProjectionGroups(writer,wrapJoinedArrows(applyStudioAppearance(writer,normalizeColorTable(writer.out.str())))))));
    tinyxml2::XMLDocument verified;
    if(verified.Parse(text.c_str())!=tinyxml2::XML_SUCCESS)
        return Error(ErrorCode::ParseError,std::string("Invalid generated CDXML: ")+verified.ErrorStr());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) return Error(ErrorCode::IoError, "cannot write CDXML output");
    return Error::success();
}

std::string writeCdxmlToString(const core::Document& document, RefusalReport* refusals,
                               CdxmlWriteFidelity* fidelity,
                               const CdxmlWriteOptions& options) {
    std::ostringstream out;
    Error r = writeCdxml(document, out, refusals, fidelity, options);
    if (!r.ok()) return std::string();
    return out.str();
}

core::Error writeCdxml(const core::Document& document, const std::string& path,
                       RefusalReport* refusals, CdxmlWriteFidelity* fidelity,
                       const CdxmlWriteOptions& options) {
    // The destination is never the file the content came from, and the replace is
    // atomic: a failure at any point leaves the previous file intact.
    std::string bytes = writeCdxmlToString(document, refusals, fidelity, options);
    if (bytes.empty()) return Error(ErrorCode::IoError, "CDXML serialization produced no bytes");
    AtomicWriteOptions atomic;
    atomic.sourcePath = document.sourcePath();
    atomic.verifyAfterWrite = true;
    return writeFileAtomic(path, bytes, atomic, 0);
}

core::Error planCdxmlWrite(const core::Document& document, CdxmlFidelityPlan* plan,
                           const CdxmlWriteOptions& options) {
    if (!plan) return Error(ErrorCode::InvalidArgument, "null fidelity plan");
    *plan = CdxmlFidelityPlan();
    CdxmlWriteFidelity fidelity;
    RefusalReport refusals;
    std::ostringstream sink;
    Error r = writeCdxml(document, sink, &refusals, &fidelity, options);
    if (!r.ok()) {
        plan->objectsRefused = refusals.size() ? refusals.size() : 1;
        for (size_t i = 0; i < refusals.items.size(); ++i)
            plan->losses.push_back(refusals.items[i].summaryLine());
        if (refusals.items.empty()) plan->losses.push_back(r.message);
        return r;
    }
    plan->objectsPreserved = fidelity.objectsFromSource;
    plan->objectsRegenerated = fidelity.objectsRegenerated;
    plan->objectsRefused = refusals.size();
    for (size_t i = 0; i < refusals.items.size(); ++i)
        plan->losses.push_back(refusals.items[i].summaryLine());
    return Error::success();
}

} // namespace persistence
} // namespace r62

