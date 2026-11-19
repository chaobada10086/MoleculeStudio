#pragma once
#include "compound_catalog.hpp"
#include "ion_labels.hpp"
namespace studio { namespace alcohol_labels {
inline r62::core::Error upgrade(r62::core::Document& d,bool& repaired){
    using namespace r62::core;
    for(const auto& a:d.atoms().records())if(a.isAlive()&&a.element==6&&a.semantics.nodeType==uint8_t(NodeType::Query)&&!a.semantics.charge&&!a.semantics.radical&&!a.semantics.isotope){
        const auto ref=ObjectRef(ObjectKind::Atom,a.id);
        if(!d.extensions(ref).findValue("pst.attr.MoleculeStudioLiteralLabel"))continue;
        const auto links=d.incidentBonds(a.id);if(links.size()!=1)continue;
        auto b=d.bonds().alive(links.front());if(!b||b->order!=BondOrder::Single||b->semantics!=BondSemantics::Normal)continue;
        auto other=d.atoms().alive(b->beginAtom==a.id?b->endAtom:b->beginAtom);if(!other||other->element!=1)continue;
        const auto* label=studioions::find(d.atomLabel(a.id));if(!label||label->element!=8)continue;
        for(const auto& h:compounddata::hydrides)if(h.element==8&&studioions::find(h.base)==label){
            studioions::Entry entry={h.base,"",8,0,1};auto e=studioions::apply(d,a.id,entry,0);if(!e.ok())return e;
            auto sem=d.atoms().alive(a.id)->semantics;sem.nodeType=uint8_t(NodeType::Nickname);
            e=d.setAtomSemantics(a.id,sem);if(!e.ok())return e;
            d.extensions(ref).set("studio.nickname.fragment",h.fragment);repaired=true;break;
        }
    }return Error::success();
}
} }
