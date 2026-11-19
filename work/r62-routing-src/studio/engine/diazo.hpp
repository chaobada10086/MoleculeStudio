#pragma once
#include "next/core/document.hpp"
#include <cmath>
#include <algorithm>
#include <cstdlib>
namespace studio { namespace diazo {
using namespace r62::core;
inline bool requested(const std::string& s){return s=="n2"||s=="N2"||s=="N₂"||s=="n₂";}
inline int externalValence(const Document& d,ObjectId id){int sum=0;for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);if(!b||b->semantics!=BondSemantics::Normal||int(b->order)>2)return 99;sum+=int(b->order);}return sum;}
inline bool eligible(const Document& d,ObjectId id){auto a=d.atoms().alive(id);int sum=externalValence(d,id);return a&&a->element==6&&!a->semantics.charge&&!a->semantics.radical&&sum>0&&sum<=2;}
inline Vec2 axis(const Document& d,ObjectId id){Vec2 out;auto p=d.atoms().alive(id)->position;for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);auto q=d.atoms().alive(b->beginAtom==id?b->endAtom:b->beginAtom)->position;auto v=p-q;if(length(v)>1e-8)out=out+v*(1/length(v));}return length(out)>1e-8?out*(1/length(out)):Vec2(1,0);}
inline double nitrogenLength(const Document& d){double em=d.defaultTextSize();if(auto v=d.documentExtensions().findValue("studio.style.LabelSize"))em=std::max(em,std::atof(v->c_str()));return std::max(d.defaultBondLength(),em*2.2);}
// Only cap oversized free N2 or terminal diazonium bonds. Short/standard
// bonds and their electronic states remain as authored.
inline Error compactNitrogenBond(Document& d,ObjectId first,ObjectId last,bool& changed){
    auto a=d.atoms().alive(first),b=d.atoms().alive(last);if(!a||!b||a->element!=7||b->element!=7)return Error::success();
    auto link=d.bonds().alive(d.bondBetween(first,last));if(!link||link->semantics!=BondSemantics::Normal)return Error::success();
    const auto da=d.incidentBonds(first).size(),db=d.incidentBonds(last).size();
    const bool free=da==1&&db==1&&(link->order==BondOrder::Double||link->order==BondOrder::Triple);
    const bool terminal=link->order==BondOrder::Triple&&((da==2&&db==1)||(da==1&&db==2));
    if(!free&&!terminal)return Error::success();
    const auto pa=a->position,pb=b->position,v=pb-pa;const double old=length(v),desired=nitrogenLength(d);if(old<=desired+1e-7)return Error::success();
    Vec2 nextA=pa,nextB=pb;
    if(free){const auto mid=(pa+pb)*.5;nextA=mid-v*(desired/(2*old));nextB=mid+v*(desired/(2*old));}
    else if(da==2)nextB=pa+v*(desired/old);else nextA=pb-v*(desired/old);
    auto e=d.setAtomPosition(first,nextA);if(e.ok())e=d.setAtomPosition(last,nextB);if(e.ok())changed=true;return e;
}
inline Error terminal(Document& d,ObjectId root,Vec2 direction){
    ObjectId end,bond;auto e=d.addAtom(d.atoms().alive(root)->position+direction*nitrogenLength(d),7,&end);if(!e.ok())return e;
    e=d.setElement(root,7);if(!e.ok())return e;
    for(auto id:{root,end}){auto sem=d.atoms().alive(id)->semantics;sem.nodeType=uint8_t(NodeType::Element);sem.charge=id==root?1:0;sem.radical=0;sem.implicitHydrogens=0;sem.showLabel=true;e=d.setAtomSemantics(id,sem);if(!e.ok())return e;}
    e=d.addBond(root,end,&bond);if(e.ok())e=d.setBondOrder(bond,BondOrder::Triple);if(!e.ok())return e;
    if(!d.atomLabel(root).empty())e=d.setAtomLabel(root,"");d.setAtomLabelQuiet(root,"");auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,root));for(auto key:{"studio.atom.runs","studio.compound.fragment","studio.nickname.fragment","studio.label.text","studio.label.offset","cdxml.attr.r62ChemLabel","pst.attr.MoleculeStudioLiteralLabel"})bag.erase(key);return e;
}
inline Error append(Document& d,ObjectId root,Vec2 direction,int external){
    // A drawn single-bond endpoint represents the first diazonium N. The
    // divalent CH2 route below keeps its carbon and its existing behaviour.
    if(external==1&&d.incidentBonds(root).size()==1)return terminal(d,root,direction);
    auto p=d.atoms().alive(root)->position;ObjectId middle,end,bond;Error e=d.addAtom(p+direction*d.defaultBondLength(),7,&middle);if(!e.ok())return e;
    e=d.addAtom(p+direction*(d.defaultBondLength()+nitrogenLength(d)),7,&end);if(!e.ok())return e;
    for(auto id:{middle,end}){auto sem=d.atoms().alive(id)->semantics;sem.charge=id==middle?1:-1;sem.implicitHydrogens=0;sem.showLabel=true;e=d.setAtomSemantics(id,sem);if(!e.ok())return e;}
    for(auto edge:{std::make_pair(root,middle),std::make_pair(middle,end)}){e=d.addBond(edge.first,edge.second,&bond);if(e.ok())e=d.setBondOrder(bond,BondOrder::Double);if(!e.ok())return e;}
    auto sem=d.atoms().alive(root)->semantics;sem.nodeType=uint8_t(NodeType::Element);sem.implicitHydrogens=std::max(0,2-external);sem.showLabel=false;e=d.setAtomSemantics(root,sem);if(!e.ok())return e;
    if(!d.atomLabel(root).empty())e=d.setAtomLabel(root,"");d.setAtomLabelQuiet(root,"");auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,root));for(auto key:{"studio.atom.runs","studio.compound.fragment","studio.nickname.fragment","studio.label.text","studio.label.offset","cdxml.attr.r62ChemLabel","pst.attr.MoleculeStudioLiteralLabel"})bag.erase(key);return e;
}
inline Error normalize(Document& d,bool& changed){
    for(const auto& a:d.atoms().records())if(a.isAlive()&&a.element==7&&a.semantics.nodeType==uint8_t(NodeType::Element)&&!a.semantics.radical){
        auto links=d.incidentBonds(a.id);if(links.size()!=2)continue;ObjectId carbon=kInvalidObjectId,end=kInvalidObjectId;
        for(auto bid:links){auto b=d.bonds().alive(bid);if(b->order!=BondOrder::Double||b->semantics!=BondSemantics::Normal)continue;auto id=b->beginAtom==a.id?b->endAtom:b->beginAtom;auto n=d.atoms().alive(id);if(n->element==6)carbon=id;if(n->element==7&&d.incidentBonds(id).size()==1)end=id;}
        if(carbon==kInvalidObjectId||end==kInvalidObjectId)continue;const auto* n=d.atoms().alive(end);
        if(n->semantics.radical||n->semantics.isotope||n->semantics.charge>0||a.semantics.charge<0)continue;
        for(auto id:{a.id,end}){auto sem=d.atoms().alive(id)->semantics;sem.charge=id==a.id?1:-1;sem.implicitHydrogens=0;if(!(sem==d.atoms().alive(id)->semantics)){auto e=d.setAtomSemantics(id,sem);if(!e.ok())return e;changed=true;}auto label=d.atomLabel(id);if(!label.empty()&&label!="N"){auto e=d.setAtomLabel(id,"N");if(!e.ok())return e;d.extensions(ObjectRef(ObjectKind::Atom,id)).erase("studio.atom.runs");changed=true;}}
        auto v=a.position-d.atoms().alive(carbon)->position;if(length(v)<1e-8)continue;auto p=a.position+v*(nitrogenLength(d)/length(v));
        if(length(p-n->position)>1e-7){auto e=d.setAtomPosition(end,p);if(!e.ok())return e;changed=true;}
    }
    for(const auto& b:d.bonds().records())if(b.isAlive()){auto e=compactNitrogenBond(d,b.beginAtom,b.endAtom,changed);if(!e.ok())return e;}
    return Error::success();
}
} }
