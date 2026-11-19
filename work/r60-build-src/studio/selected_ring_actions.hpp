#pragma once
namespace selected_ring {
// Resolve against the current transaction, then check the complete new ring
// against the old graph. The anchor's deliberate common endpoint is excluded.
inline Error resolve(const Document& doc,rings_chains::RingRequest request,rings_chains::RingPlacement* out){
    auto result=rings_chains::resolveRingPlacement(doc,request,out);
    if(!result.ok()||request.mode!=rings_chains::RingAttachMode::GrowFromAtom)return result;
    const auto anchor=request.anchorAtom;const double unit=doc.defaultBondLength();
    auto clear=[&](const rings_chains::RingPlacement& candidate){
        for(const auto& old:doc.atoms().records())if(old.isAlive()&&old.id!=anchor){
            if(sprout_layout::inside(old.position,candidate.worldPosition))return false;
            for(auto point:candidate.worldPosition)if(length(point-old.position)<unit*.18)return false;
        }
        for(const auto& edge:candidate.geometry.bonds)for(const auto& old:doc.bonds().records())if(old.isAlive()){
            auto a=candidate.worldPosition[edge.from],b=candidate.worldPosition[edge.to];
            auto c=doc.atoms().alive(old.beginAtom)->position,d=doc.atoms().alive(old.endAtom)->position;
            if(old.beginAtom==anchor||old.endAtom==anchor){const auto origin=doc.atoms().alive(anchor)->position;
                if(length(a-origin)<unit*.01)a=a+(b-a)*.3;
                if(length(b-origin)<unit*.01)b=b+(a-b)*.3;
                if(old.beginAtom==anchor)c=c+(d-c)*.3;else d=d+(c-d)*.3;
            }
            if(sprout_layout::segmentSq(a,b,c,d)<unit*unit*.012)return false;
        }return true;
    };
    const double rotation=out->rotationDeg;
    if(clear(*out))return Error();
    for(int step=1;step<=24;++step){request.rotationGiven=true;request.rotationDeg=rotation+(step%2?1.:-1.)*((step+1)/2)*15.;
        rings_chains::RingPlacement candidate;
        if(rings_chains::resolveRingPlacement(doc,request,&candidate).ok()&&clear(candidate)){*out=std::move(candidate);return Error();}
    }
    return Error(ErrorCode::InvalidGeometry,"该原子周围没有足够空间接环，请拖动指定位置。");
}
}
bool App::insertSelectedRing(const std::string& command){
    const auto* preset=rings_chains::ringPresetForCommand(command);
    if(!preset||s.selected.size()!=1)return false;
    const auto anchor=s.selected.front();
    if((anchor.kind!=ObjectKind::Atom||!s.doc.atoms().alive(anchor.id))&&
       (anchor.kind!=ObjectKind::Bond||!s.doc.bonds().alive(anchor.id)))return false;
    s.cancel();down=pan=cloneDragging=selectionArrowEdit=textPlacing=false;cloneSource.reset();
    if(GetCapture()==canvas)ReleaseCapture();
    bool connector=false;
    if(anchor.kind==ObjectKind::Atom){const auto atom=s.doc.atoms().alive(anchor.id);int valence=0;
        for(auto id:s.doc.incidentBonds(anchor.id))valence+=int(s.doc.bonds().alive(id)->order);
        if(atom->element==6){
            if(valence>=4){status="该碳原子没有空闲成键价接环。";updateCanvasCursor();invalidate();return true;}
            rings_chains::RingGeometry geometry;rings_chains::buildRingGeometry(preset->id,s.doc.defaultBondLength(),&geometry);
            int ringValence=0;for(const auto& edge:geometry.bonds)if(!edge.from||!edge.to)ringValence+=int(edge.order);
            connector=valence+ringValence>4;
        }
    }
    const auto id=preset->id;auto made=std::make_shared<std::vector<ObjectRef>>();
    auto result=s.submit(Command("在选中位置插入环",kInvalidateStructure|kInvalidateGeometry|kInvalidateSemantics,
        [anchor,id,connector,made](CommandContext& context){auto& doc=context.document;
            rings_chains::RingRequest request;request.preset=id;request.bondLength=doc.defaultBondLength();
            if(anchor.kind==ObjectKind::Atom){
                request.mode=rings_chains::RingAttachMode::GrowFromAtom;request.anchorAtom=anchor.id;
                if(connector){
                    auto direction=sprout_layout::resolve(doc,anchor.id,-30.);if(!direction.ok)return CommandResult::fail(ErrorCode::InvalidGeometry,direction.rejectReason);
                    auto point=doc.atoms().alive(anchor.id)->position+core::direction(direction.directionDeg)*doc.defaultBondLength();ObjectId atom,bond;
                    auto error=doc.addAtom(point,6,&atom);if(error.ok())error=doc.addBond(anchor.id,atom,&bond);
                    if(!error.ok())return CommandResult::fail(error.code,error.message);
                    request.anchorAtom=atom;made->emplace_back(ObjectKind::Bond,bond);
                }
                request.pointer=doc.atoms().alive(request.anchorAtom)->position;
            }else {request.mode=rings_chains::RingAttachMode::FuseOnBond;request.anchorBond=anchor.id;auto bond=doc.bonds().alive(anchor.id);request.pointer=(doc.atoms().alive(bond->beginAtom)->position+doc.atoms().alive(bond->endAtom)->position)*.5;}
            rings_chains::RingPlacement plan;auto error=selected_ring::resolve(doc,request,&plan);
            if(!error.ok())return CommandResult::fail(error.code,error.message);
            std::vector<ObjectId> atoms,bonds;auto result=rings_chains::commands::createRingCommand(plan,&atoms,&bonds).execute(context);
            if(result.ok()){for(auto atom:atoms)made->emplace_back(ObjectKind::Atom,atom);for(auto bond:bonds)made->emplace_back(ObjectKind::Bond,bond);}
            return result;
        }),"rings_chains.ring.create");
    error(result);if(result.ok())s.selected=*made;
    s.activateCommand("chooseSelectionTool_Marquee");selectionCursorReset=true;SetFocus(canvas);updateInfo();updateCanvasCursor();invalidate();return true;
}
