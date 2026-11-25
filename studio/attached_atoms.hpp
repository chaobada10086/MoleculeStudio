#pragma once

#include "engine/mechanism_visual_anchors.hpp"
#include "nicknames.hpp"
#include <limits>

namespace studio { namespace attached_atoms {
using namespace r62;
using namespace r62::core;

inline Error exposeAttachedProton(Document& doc,ObjectId host,Vec2 position,ObjectId* hydrogen,bool front=false,int terminalElement=0){
    if(!hydrogen||!doc.atoms().alive(host))return Error(ErrorCode::NotFound,"没有可编辑的附着原子");
    if(terminalElement){auto terminal=studio_terminal_drag::find(doc.atomLabel(host));if(!terminal||terminal->element!=terminalElement)return Error(ErrorCode::InvalidArgument,"没有可展开的末端原子");
        auto sem=doc.atoms().alive(host)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);sem.implicitHydrogens=0;sem.showLabel=true;sem.charge=sem.radical=0;auto e=doc.setElement(host,terminal->host);if(e.ok())e=doc.setAtomSemantics(host,sem);if(e.ok())e=doc.setAtomLabel(host,terminal->base);studioatom::set(doc,host,{});auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,host));bag.erase("studio.compound.fragment");bag.set("studio.nickname.fragment",terminal->fragment);
        if(e.ok())e=doc.addAtom(position,terminalElement,hydrogen);if(e.ok()){auto s=doc.atoms().alive(*hydrogen)->semantics;s.implicitHydrogens=0;s.showLabel=true;e=doc.setAtomSemantics(*hydrogen,s);}if(e.ok()){ObjectId b;e=doc.addBond(host,*hydrogen,&b);}return e;
    }
    const auto* atom=doc.atoms().alive(host);
    const int count=atom?(front?studio::hydride_labels::terminal(doc.atomLabel(host),nullptr,true):arrows::attachedHydrogenCount(doc,*atom)):0;
    if(!count)
        return Error(ErrorCode::InvalidArgument,"这个标记没有可拉出的附着氢。");
    if(const auto* hydride=compounddata::hydride(doc.atomLabel(host))){
        studioions::Entry compact={hydride->base,"",hydride->element,0,hydride->element==8?1:3};
        auto e=studioions::apply(doc,host,compact,0);
        if(!e.ok())return e;
        auto compactSem=doc.atoms().alive(host)->semantics;compactSem.nodeType=uint8_t(NodeType::Nickname);
        e=doc.setAtomSemantics(host,compactSem);if(!e.ok())return e;
        auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,host));
        bag.erase("studio.compound.fragment");bag.set("studio.nickname.fragment",hydride->fragment);
        std::vector<TextRun> runs;for(const auto& part:chemcompletion::segments(hydride->base,true)){TextRun run;run.utf8=part.text;run.flags=part.script<0?kSubscript:part.script>0?kSuperscript:0;runs.push_back(run);}studioatom::set(doc,host,runs);
        e=doc.addAtom(position,1,hydrogen);
        if(e.ok()){auto h=doc.atoms().alive(*hydrogen)->semantics;h.implicitHydrogens=0;h.showLabel=true;e=doc.setAtomSemantics(*hydrogen,h);}
        if(e.ok()){ObjectId bond;e=doc.addBond(host,*hydrogen,&bond);}return e;
    }
    auto sem=atom->semantics;
    const auto label=doc.atomLabel(host);
    // A textual residue remains textual. Pulling out its authored H creates a
    // genuine bond without guessing a different compound or discarding atoms.
    std::string residue;bool first=false;
    if(studio::hydride_labels::count(atom->element,label)<0&&label!="OH"&&label!="HO"&&studio::hydride_labels::terminal(label,&residue,front,&first)>0){
        if(count>1){const std::string h="H"+(count>2?std::to_string(count-1):"");residue=first?h+residue:residue+h;}
        sem.nodeType=uint8_t(NodeType::Query);sem.implicitHydrogens=0;sem.showLabel=true;
        auto e=doc.setAtomSemantics(host,sem);if(e.ok())e=doc.setAtomLabel(host,residue);
        auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,host));for(auto key:{"studio.nickname.fragment","studio.compound.fragment","studio.mechanism.ion.base","studio.mechanism.compact.valence"})bag.erase(key);
        bag.set("pst.attr.MoleculeStudioLiteralLabel","1");
        studioatom::set(doc,host,{});
        if(e.ok())e=doc.addAtom(position,1,hydrogen);if(e.ok()){auto hs=doc.atoms().alive(*hydrogen)->semantics;hs.implicitHydrogens=0;hs.showLabel=true;e=doc.setAtomSemantics(*hydrogen,hs);}if(e.ok()){ObjectId bond;e=doc.addBond(host,*hydrogen,&bond);}return e;
    }
    const bool hydroxylLabel=!sem.charge&&(label=="OH"||label=="HO");
    const int atomicNumber=hydroxylLabel?8:atom->element;
    const std::string element=elementSymbol(atomicNumber);
    sem.implicitHydrogens=count-1;
    if(hydroxylLabel)sem.nodeType=uint8_t(NodeType::Element);
    auto error=doc.setElement(host,atomicNumber);
    if(error.ok())error=doc.setAtomSemantics(host,sem);
    if(error.ok()&&!label.empty()&&label!=element){
        error=doc.setAtomLabel(host,element);
        if(error.ok()){
            studioatom::set(doc,host,{});
            auto& attrs=doc.extensions(ObjectRef(ObjectKind::Atom,host));
            attrs.erase("studio.nickname.fragment");
            attrs.erase("studio.query.description");
        }
    }
    if(error.ok())error=doc.addAtom(position,1,hydrogen);
    if(error.ok()){
        auto h=doc.atoms().alive(*hydrogen)->semantics;
        h.implicitHydrogens=0;h.showLabel=true;
        error=doc.setAtomSemantics(*hydrogen,h);
    }
    if(error.ok()){
        ObjectId bond;
        error=doc.addBond(host,*hydrogen,&bond);
    }
    return error;
}

inline Error materializeHydrogenCaption(Document& doc,ObjectId text,Vec2 position,ObjectId* host){
    const auto label=doc.plainText(text);int element=6;for(int z=2;z<=118;++z)if(studio::hydride_labels::count(z,label)>0){element=z;break;}
    auto e=doc.addAtom(position,element,host);if(e.ok())e=doc.setAtomLabel(*host,label);
    if(e.ok()&&compounddata::hydride(label))e=nicknames::apply(doc,*host,label);
    if(e.ok())studioatom::set(doc,*host,doc.runs(text));
    if(e.ok())e=doc.removeObject(ObjectRef(ObjectKind::Text,text));return e;
}

struct Hit {
    ObjectRef target;
    Vec2 center, host;
    bool front = false;
    int terminal = 0;
};

inline void glyphInk(Box& bounds, const text::GlyphPlacement& glyph, Vec2 offset) {
    for (const auto& contour : glyph.contours)
        for (const auto point : contour) bounds.add(point + offset);
}

inline Hit hitTest(const Document& doc, text::GlyphCache& cache, Vec2 point, double tolerance, bool nearAtomArtwork = true) {
    Hit hit;
    double best = tolerance, nearestCenter = std::numeric_limits<double>::infinity();
    const auto consider = [&](ObjectRef ref, const Box& box, Vec2 host, bool front, int terminal) {
        if (box.empty()) return;
        const auto center = (box.min + box.max) * .5;
        const auto gap = arrows::mechanismBoxDistance(box, point);
        const auto centerDistance = length(point - center);
        if (gap > best || (gap == best && centerDistance >= nearestCenter)) return;
        best = gap;
        nearestCenter = centerDistance;
        hit.target = ref;
        hit.center = center;
        hit.host = host;
        hit.front = front;
        hit.terminal = terminal;
    };
    if (nearAtomArtwork) for (const auto& entry : arrows::mechanismVisualBoxes(doc)) {
        const auto* atom = doc.atoms().alive(entry.first);
        if (!atom) continue;
        const ObjectRef ref(ObjectKind::Atom, entry.first);
        const auto& boxes = entry.second;
        if (boxes.hasAttachedProton) consider(ref, boxes.attachedProton, atom->position, false, 0);
        if (boxes.hasAttachedProtonFirst) consider(ref, boxes.attachedProtonFirst, atom->position, true, 0);
        if (boxes.hasTerminal) consider(ref, boxes.terminal, atom->position, false, boxes.terminalElement);
    }
    if (hit.target.valid()) return hit;
    for (const auto& caption : doc.texts().records()) {
        if (!caption.isAlive()) continue;
        const ObjectRef ref(ObjectKind::Text, caption.id);
        const auto& extensions = doc.extensions(ref);
        const auto* interpreted = extensions.findValue("pst.attr.InterpretChemically");
        if (interpreted && *interpreted == "no") continue;
        const auto label = studioions::ascii(doc.plainText(caption.id));
        const auto* terminal = studio_terminal_drag::find(label);
        const bool leading = studio::hydride_labels::terminal(label, nullptr, true) > 0;
        size_t end = label.size();
        while (end && label[end - 1] >= '0' && label[end - 1] <= '9') --end;
        const bool trailing = end > 1 && label[end - 1] == 'H' && studio::hydride_labels::terminal(label) > 0;
        if (!terminal && !leading && !trailing) continue;
        text::LayoutOptions options;
        options.triangulateGlyphs = false;
        options.defaultSize = caption.size > 0 ? caption.size : doc.defaultTextSize();
        options.defaultFont = caption.format.fontName.empty() ? doc.defaultFont() : caption.format.fontName;
        options.defaultFlags = caption.format.flags;
        text::RichLayout layout;
        if (!text::layoutRichText(doc.runs(caption.id), Vec2(), options, &cache, &layout).ok()) continue;
        auto offset = caption.position - Vec2(0, layout.metrics.firstBaseline);
        if (caption.format.align == 1) offset.x -= layout.metrics.width * .5;
        else if (caption.format.align == 2) offset.x -= layout.metrics.width;
        for (size_t index = 0; index < layout.glyphs.size(); ++index) {
            const auto& glyph = layout.glyphs[index];
            const bool isTerminal = terminal && index == terminal->offset;
            const bool isFront = leading && index == 0 && glyph.codepoint == 'H';
            if (!isTerminal && !isFront && !(trailing && index == end - 1 && glyph.codepoint == 'H')) continue;
            const auto last = std::min(layout.glyphs.size(), index + (isTerminal ? terminal->token.size() : 1));
            const double infinity = std::numeric_limits<double>::infinity();
            Box grip(Vec2(infinity, infinity), Vec2(-infinity, -infinity));
            auto body = grip;
            for (size_t i = 0; i < layout.glyphs.size(); ++i) {
                const auto& item = layout.glyphs[i];
                if (i >= index && i < last) glyphInk(grip, item, offset);
                else if ((isTerminal || item.codepoint != 'H') && !(item.codepoint >= '0' && item.codepoint <= '9'))
                    glyphInk(body, item, offset);
            }
            const auto host = body.empty() ? caption.position : (body.min + body.max) * .5;
            consider(ref, grip, host, isFront, isTerminal ? terminal->element : 0);
        }
    }
    return hit;
}

} }
