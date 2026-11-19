#pragma once
void App::insertReactiveCenter(bool allene){
    if(textEditor.active&&!finishText(true))return;
    s.cancel();
    auto at=s.view().center;
    ObjectId selected=kInvalidObjectId;
    if(s.selected.size()==1&&s.selected.front().kind==ObjectKind::Atom&&s.doc.atoms().alive(s.selected.front().id))selected=s.selected.front().id;
    if(selected!=kInvalidObjectId)at=s.doc.atoms().alive(selected)->position;
    auto made=std::make_shared<std::vector<ObjectRef>>();
    auto result=s.submit(Command(allene?"插入连烯":"插入卡宾",kInvalidateStructure|kInvalidateGeometry|kInvalidateSemantics,
        [at,selected,allene,made](CommandContext& c){
            auto& doc=c.document;ObjectId center=selected;auto e=Error();
            if(center==kInvalidObjectId)e=doc.addAtom(at,6,&center);
            if(!e.ok())return CommandResult::fail(e.code,e.message);
            auto sem=doc.atoms().alive(center)->semantics;
            if(doc.atoms().alive(center)->element!=6||sem.nodeType!=uint8_t(NodeType::Element))
                return CommandResult::fail(ErrorCode::InvalidArgument,"请选择一个碳原子。");
            int degree=0;for(auto id:doc.incidentBonds(center))degree+=int(doc.bonds().alive(id)->order);
            if(!allene&&degree>2)return CommandResult::fail(ErrorCode::InvalidArgument,"卡宾碳最多有两个成键价。");
            if(allene&&selected!=kInvalidObjectId&&degree>2)return CommandResult::fail(ErrorCode::InvalidArgument,"连烯端碳需要两个空闲成键价。");
            Vec2 axis(1,0);
            if(allene&&selected!=kInvalidObjectId){
                auto direction=sprout_layout::resolve(doc,selected,0);if(!direction.ok)return CommandResult::fail(ErrorCode::InvalidArgument,direction.rejectReason);
                const auto environment=rings_chains::buildSproutEnvironment(doc,selected,25.);
                const double unit=doc.defaultBondLength();bool found=false;
                for(int step=0;step<25&&!found;++step){double angle=direction.directionDeg+(step%2?1.:-1.)*((step+1)/2)*15.;Vec2 ray=core::direction(angle);bool clear=true;
                    for(auto neighbour:environment.neighbourDirectionsDeg)if(sprout_layout::gap(angle,neighbour)<25.)clear=false;
                    for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&atom.id!=selected&&sprout_layout::pointSegmentSq(atom.position,at,at+ray*(2*unit))<unit*unit*.08)clear=false;
                    for(const auto& bond:doc.bonds().records())if(bond.isAlive()&&bond.beginAtom!=selected&&bond.endAtom!=selected&&sprout_layout::segmentSq(at,at+ray*(2*unit),doc.atoms().alive(bond.beginAtom)->position,doc.atoms().alive(bond.endAtom)->position)<unit*unit*.025)clear=false;
                    if(clear){axis=ray;found=true;}
                }
                if(!found)return CommandResult::fail(ErrorCode::InvalidGeometry,"该原子周围没有足够空间插入连烯。");
            }
            sem.charge=0;sem.radical=allene?0:uint8_t(Radical::Singlet);sem.implicitHydrogens=allene?0:2-degree;
            e=doc.setAtomSemantics(center,sem);made->push_back(ObjectRef(ObjectKind::Atom,center));
            ObjectId previous=center;
            if(allene)for(int part=0;part<2;++part){
                // On a selected carbon, that atom is the terminal carbon of
                // C=C=C. Never put a four-valent middle carbon on a ring vertex.
                const auto point=selected==kInvalidObjectId?at+axis*((part?1.:-1.)*doc.defaultBondLength()):at+axis*((part+1)*doc.defaultBondLength());
                ObjectId atom,bond;e=doc.addAtom(point,6,&atom);
                if(e.ok())e=doc.addBond(selected==kInvalidObjectId?center:previous,atom,&bond);
                if(e.ok())e=doc.setBondOrder(bond,BondOrder::Double);
                if(!e.ok())break;
                made->push_back(ObjectRef(ObjectKind::Atom,atom));made->push_back(ObjectRef(ObjectKind::Bond,bond));
                previous=atom;
            }
            return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
        }),allene?"插入连烯":"插入卡宾");
    error(result);
    if(result.ok())s.selected=*made;s.activateCommand("chooseSelectionTool_Marquee");updateInfo();invalidate();
}
