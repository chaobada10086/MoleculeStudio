#pragma once
#include "engine/mechanism_electrons.hpp"

inline Command atomElectronStateCommand(const std::vector<ObjectId>& ids,int state,bool circled){
    if(state==0)return mechanism::setLonePairCommand(ids);
    const bool radical=state==2;
    const std::string name=radical?"插入自由基":state>0?"插入正离子":"插入负离子";
    return Command(name,kInvalidateStructure|kInvalidateSemantics|kInvalidateAppearance,[ids,state,radical,circled](CommandContext& c){
        using namespace integration::molecular;
        auto& doc=c.document;
        for(auto id:ids){
            const auto* atom=doc.atoms().alive(id);
            if(!atom)return CommandResult::fail(ErrorCode::InvalidArgument,"请选择氢所连接的中心原子。");
            auto sem=atom->semantics;
            const auto label=doc.atomLabel(id);
            if(!radical)if(const auto* ion=studioions::find(label)){
                auto e=studioions::apply(doc,id,*ion,state);if(!e.ok())return CommandResult::fail(e.code,e.message);
                if(circled)doc.extensions(ObjectRef(ObjectKind::Atom,id)).set("pst.attr.MoleculeStudioChargeCircle","1");
                continue;
            }
            const std::string element=elementSymbol(atom->element);
            bool hydrogenFirst=false;size_t elementChars=0;
            const bool hydride=hydrideLabel(atom->element,label,&hydrogenFirst,&elementChars);
            auto ref=ObjectRef(ObjectKind::Atom,id);
            const auto& attributes=static_cast<const Document&>(doc).extensions(ref);
            if((!label.empty()&&label!=element&&!hydride)||attributes.findValue("studio.nickname.fragment")||
               (sem.nodeType!=uint8_t(NodeType::Element)&&!(atom->element==6&&label.empty()&&sem.nodeType==uint8_t(NodeType::Query)))||sem.aromatic)
                return CommandResult::fail(ErrorCode::InvalidArgument,"请选择明确的非芳香原子。");
            const int nextCharge=radical?0:state;
            const int nextRadical=radical?int(Radical::Doublet):0;
            if(sem.charge!=nextCharge||sem.radical!=nextRadical){
                if(!sem.charge&&!sem.radical){
                    chemistry::AtomEnvironment env;env.element=atom->element;env.explicitHydrogens=sem.implicitHydrogens;
                    ObjectId explicitH=kInvalidObjectId;
                    for(auto bid:doc.incidentBonds(id)){
                        auto b=doc.bonds().alive(bid);
                        if(!b||b->semantics!=BondSemantics::Normal||int(b->order)<1||int(b->order)>3)
                            return CommandResult::fail(ErrorCode::InvalidArgument,"请先明确该原子的键级。");
                        env.bondOrderSum+=int(b->order);++env.bondCount;
                        auto h=doc.atoms().alive(b->beginAtom==id?b->endAtom:b->beginAtom);
                        if(h&&b->order==BondOrder::Single&&h->element==1&&!h->semantics.isotope&&!h->semantics.charge&&
                           !h->semantics.radical&&h->semantics.nodeType==uint8_t(NodeType::Element)&&
                           (doc.atomLabel(h->id).empty()||doc.atomLabel(h->id)=="H")&&doc.incidentBonds(h->id).size()==1)
                            explicitH=h->id;
                    }
                    auto hydrogen=chemistry::implicitHydrogens(env);
                    if(!hydrogen.valid||hydrogen.overCoordinated||atom->element==1||
                       (hydrogen.count<=0&&explicitH==kInvalidObjectId))
                        return CommandResult::fail(ErrorCode::InvalidArgument,"该原子没有可移除的氢。");
                    if(hydrogen.count==0){
                        auto result=doc.removeObject(ObjectRef(ObjectKind::Atom,explicitH));
                        if(!result.ok())return CommandResult::fail(result.code,result.message);
                    }
                    sem.implicitHydrogens=static_cast<int8_t>(std::max(0,hydrogen.count-1));
                }
                sem.nodeType=uint8_t(NodeType::Element);
                sem.charge=static_cast<int8_t>(nextCharge);
                sem.radical=static_cast<uint8_t>(nextRadical);
                auto result=doc.setAtomSemantics(id,sem);
                if(result.ok()&&hydride){
                    result=doc.setAtomLabel(id,element);
                    studioatom::set(doc,id,{});
                }
                if(!result.ok())return CommandResult::fail(result.code,result.message);
            }
            auto& bag=doc.extensions(ref);
            bag.erase(electronMarkerKey(!radical));
            if(!bag.findValue(electronMarkerKey(radical)))bag.set(electronMarkerKey(radical),"auto");
            if(!radical&&circled)bag.set("pst.attr.MoleculeStudioChargeCircle","1");
            else bag.erase("pst.attr.MoleculeStudioChargeCircle");
        }
        return CommandResult::success();
    });
}

inline CommandResult partialCharge(Document& doc,int charge,Vec2 point,ObjectId& id){
    TextRun delta;delta.utf8="δ";delta.size=doc.defaultTextSize();delta.fontName=doc.defaultFont();
    TextRun sign=delta;sign.utf8=charge<0?"−":"+";sign.flags=kSuperscript;
    auto e=doc.addText(point,delta.size,"",&id);
    if(e.ok())e=doc.setTextRuns(id,{delta,sign});
    return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
}

inline CommandResult equivalence(Document& doc,Vec2 point,double width,uint32_t color,ObjectId& id){
    const double length=doc.defaultBondLength();
    const auto* preset=graphics::shapePresetByCommand("chooseDrawingTool_Line");
    GroupRecord group;group.label="等同符号";
    for(int row=-1;row<=1;++row){
        const auto center=point+Vec2(0,row*length*.14);
        auto params=graphics::resolveShapePreset(preset->presetId,center-Vec2(length*.45,0),center+Vec2(length*.45,0),width);
        params.style.colorRGBA=color;
        ObjectId line;
        auto e=graphics::addShape(doc,params,width,&line);
        if(!e.ok())return CommandResult::fail(e.code,e.message);
        group.members.emplace_back(ObjectKind::Arrow,line);
    }
    auto e=doc.addGroup(group,&id);
    return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
}

inline CommandResult transitionBracket(Document& doc,Box bounds,ObjectId* id){
    graphics::BracketParams p;
    p.side=graphics::BracketSide::Pair;
    p.family=graphics::BracketFamily::Square;
    p.frame.centre=(bounds.min+bounds.max)*.5;
    p.frame.axisU=Vec2(bounds.width()*.5,0);
    p.frame.axisV=Vec2(0,bounds.height()*.5);
    graphics::BracketLabel label;
    label.text="‡";
    label.size=doc.defaultTextSize();
    label.anchor=graphics::BracketLabelAnchor(1.12,-1.18);
    p.labels.push_back(label);
    auto e=graphics::addBracket(doc,p,doc.defaultLineWidth(),id);
    return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
}
