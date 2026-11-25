#pragma once
#include "interop.hpp"
#include "engine/name_link.hpp"
#include "next/selection/grouping.hpp"

namespace studio_chemistry {
using namespace r62::core;

inline std::vector<ObjectRef> scope(const Document& document, const std::vector<ObjectRef>& selection) {
    std::set<ObjectId> atoms;
    std::vector<ObjectId> pending;
    auto add = [&](ObjectId atom) {
        if (atoms.insert(atom).second) pending.push_back(atom);
    };
    if (selection.empty()) {
        for (const auto& atom : document.atoms().records())
            if (atom.isAlive()) add(atom.id);
    } else {
        for (auto ref : studioio::expanded(document, selection))
            if (ref.kind == ObjectKind::Atom && document.atoms().alive(ref.id)) add(ref.id);
        for (size_t i = 0; i < pending.size(); ++i)
            for (auto neighbor : document.neighbours(pending[i])) add(neighbor);
    }
    std::vector<ObjectRef> refs;
    for (auto atom : atoms) refs.emplace_back(ObjectKind::Atom, atom);
    for (const auto& bond : document.bonds().records())
        if (bond.isAlive() && atoms.count(bond.beginAtom) && atoms.count(bond.endAtom))
            refs.emplace_back(ObjectKind::Bond, bond.id);
    return refs;
}
}

// ---------------------------------------------------------------------------
// Structure-to-name caption
// ---------------------------------------------------------------------------
// The caption is bound to the structure it names: the pair becomes one
// GroupRecord, and the caption carries a digest of the linked structure so a
// later edit can raise the red "structure changed" frame. One Command = one
// Ctrl+Z, including the group and the link. Running it again REPLACES its own
// previous caption instead of stacking a second one.
namespace studioNaming {
inline r62::core::Error createCaption(r62::core::Document& document,const std::string& caption,const r62::core::Vec2& position,
                           const std::vector<r62::core::ObjectRef>& linkedRefs,r62::core::ObjectId* textId){
    using namespace r62::core;
    // Captions this command owns (found by their structure link) and the naming
    // groups they live in. A group the user made themselves is left alone.
    const std::vector<ObjectId> previous=namelink::linkedTextsForStructure(document,linkedRefs);
    std::vector<ObjectId> previousGroups;
    for(size_t k=0;k<previous.size();++k){
        const ObjectId owner=namelink::owningGroup(document,ObjectRef(ObjectKind::Text,previous[k]));
        if(owner==kInvalidObjectId)continue;
        const GroupRecord* group=document.groups().alive(owner);
        if(!group||group->label!=namelink::groupLabel())continue;
        if(std::find(previousGroups.begin(),previousGroups.end(),owner)==previousGroups.end())previousGroups.push_back(owner);
    }
    for(size_t k=0;k<previousGroups.size();++k){r62::selection::GroupReport report;r62::selection::ungroup(document,std::vector<ObjectId>(1,previousGroups[k]),false,&report);}
    for(size_t k=0;k<previous.size();++k)if(document.texts().alive(previous[k]))document.removeObject(ObjectRef(ObjectKind::Text,previous[k]));
    ObjectId fallbackId=kInvalidObjectId;
    ObjectId* created=textId?textId:&fallbackId;
    Error e=document.addText(position,12,caption,created);
    if(!e.ok())return e;
    auto format=document.texts().get(*created)->format;format.fontName="Arial";
    e=document.setTextFormat(*created,format);
    if(!e.ok())return e;
    // Bubble existing user groups instead of stealing their members: this group
    // nests the user group and puts the caption beside it.
    std::vector<ObjectRef> groupRefs=namelink::topLevelMembers(document,linkedRefs);
    groupRefs.push_back(ObjectRef(ObjectKind::Text,*created));
    r62::selection::createGroup(document,groupRefs,namelink::groupLabel(),0);
    namelink::setLink(document,*created,linkedRefs,namelink::structureDigestText(document,linkedRefs));
    return Error();
}
}
