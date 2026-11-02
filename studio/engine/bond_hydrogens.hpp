#pragma once
// Editing-only reconciliation. Never run in Document::addBond: import/paste
// creates bonds after loading atom H counts and must preserve that source data.
namespace bond_hydrogens {
inline int valence(const Document& d,ObjectId id){
    int sum=0;for(auto bid:d.incidentBonds(id)){auto b=d.bonds().alive(bid);if(b&&b->semantics==BondSemantics::Normal&&b->order>=BondOrder::Single&&b->order<=BondOrder::Triple)sum+=int(b->order);}return sum;
}
struct Entry {ObjectId id;int element,h,valence;std::string label;};
struct Before {
    std::vector<Entry> atoms;
    explicit Before(const Document& d){
        for(const auto& a:d.atoms().records())if(a.isAlive()){
            if(a.semantics.nodeType!=0)continue; // Nicknames/query nodes are opaque.
            int h=a.semantics.implicitHydrogens;auto label=d.atomLabel(a.id);std::string symbol=elementSymbol(a.element);
            // Only exact elemental H labels are editable; COOH/OMe/Ns etc.
            // must remain structural abbreviations, not lose arbitrary text.
            int textH=-1;if(label==symbol+"H")textH=1;
            for(int n=2;n<=8;++n)if(label==symbol+"H"+std::to_string(n))textH=n;
            if(!label.empty()&&label!=symbol&&textH<0)continue;
            if(h<0)h=textH;if(h<=0)continue;
            atoms.push_back(Entry{a.id,a.element,h,valence(d,a.id),label});
        }
    }
    Error apply(Document& d)const{
        for(const auto& old:atoms){auto a=d.atoms().alive(old.id);if(!a||a->element!=old.element)continue;
            int used=valence(d,old.id)-old.valence;if(used<=0)continue;
            auto sem=a->semantics;sem.implicitHydrogens=std::max(0,old.h-used);
            auto e=d.setAtomSemantics(old.id,sem);if(!e.ok())return e;
            if(!old.label.empty()){e=d.setAtomLabel(old.id,elementSymbol(old.element));if(!e.ok())return e;}
            auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,old.id));bag.erase("studio.atom.runs");bag.erase("studio.label.text");
        }return Error::success();
    }
};
inline bool editing(const std::string& tag){
    return tag=="resolved bond"||tag=="bond.tool"||tag=="sprout bond"||tag=="click bond"||tag=="apply bond preset"||tag=="化学键级"||tag=="键显示"||tag=="rings_chains.ring.create"||tag=="rings_chains.chain.create"||tag=="稠合环"||tag=="添加结构片段";
}
}
