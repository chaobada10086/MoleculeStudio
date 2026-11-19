#pragma once
#include "bond_hydrogens.hpp"
#include "next/chemistry/valence.hpp"

namespace studio { namespace electron_valence {
using namespace r62::core;
struct State { ObjectId id; int element,sum; AtomSemantics sem; };
// Apply inside the originating transaction, so charge, label and H undo together.
struct Before {
    std::vector<State> states;
    explicit Before(const Document& d){
        for(const auto& a:d.atoms().records())if(a.isAlive()&&(a.element==7||a.element==8))
            states.push_back({a.id,a.element,bond_hydrogens::valence(d,a.id),a.semantics});
    }
    Error apply(Document& d,bool infer)const{
        for(const auto& a:d.atoms().records())if(a.isAlive()&&(a.element==7||a.element==8)){
            if(a.semantics.nodeType!=uint8_t(NodeType::Element)||a.semantics.aromatic)continue;
            const auto label=d.atomLabel(a.id);
            if(!label.empty()&&label!=elementSymbol(a.element)&&hydride_labels::count(a.element,label)<0)continue;
            bool ordinary=true;
            for(auto id:d.incidentBonds(a.id)){auto b=d.bonds().alive(id);if(b&&(b->semantics!=BondSemantics::Normal||int(b->order)<1||int(b->order)>3))ordinary=false;}
            if(!ordinary||a.semantics.charge < -1||a.semantics.charge > 1)continue;
            auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,a.id));
            if(bag.findValue("studio.nickname.fragment"))continue;
            const State* old=nullptr;
            for(const auto& state:states)if(state.id==a.id){old=&state;break;}
            auto sem=a.semantics;
            const int sum=bond_hydrogens::valence(d,a.id),limit=a.element==7?4:3;
            const bool changed=!old||old->sum!=sum||old->element!=a.element;
            bool electronChanged=old&&(old->sem.charge!=sem.charge||old->sem.radical!=sem.radical);
            if(electronChanged)bag.erase("pst.attr.MoleculeStudioAutoPositive");
            if(infer&&changed&&!sem.radical){
                if(!sem.charge&&sum==limit){sem.charge=1;bag.set("pst.attr.MoleculeStudioAutoPositive","1");electronChanged=true;}
                else if(sem.charge==1&&sum<limit&&bag.findValue("pst.attr.MoleculeStudioAutoPositive")){
                    sem.charge=0;bag.erase("pst.attr.MoleculeStudioAutoPositive");electronChanged=true;
                }
            }
            if(!electronChanged)continue;
            r62::chemistry::AtomEnvironment env;env.element=a.element;env.charge=sem.charge;
            env.radical=sem.radical;env.bondOrderSum=sum;
            auto h=r62::chemistry::implicitHydrogens(env);
            if(h.valid)sem.implicitHydrogens=static_cast<int8_t>(h.count);
            auto e=d.setAtomSemantics(a.id,sem);if(!e.ok())return e;
            if(!label.empty()&&label!=elementSymbol(a.element)){
                e=d.setAtomLabel(a.id,elementSymbol(a.element));if(!e.ok())return e;
                bag.erase("studio.atom.runs");bag.erase("studio.label.text");bag.erase("cdxml.attr.r62ChemLabel");
            }
            bag.erase("studio.mechanism.ion.base");bag.erase("studio.mechanism.compact.valence");
        }
        return Error::success();
    }
};
} }
