#pragma once
static Command ringTemplateCommand(int index,Vec2 center,double bondLength,std::shared_ptr<std::vector<ObjectRef>> created){
    return Command("插入环母体",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance|kInvalidateSemantics,[index,center,bondLength,created](CommandContext& context){
        if(index<0||index>=ring_library::variantCount||!std::isfinite(bondLength)||bondLength<=0)return CommandResult::fail(ErrorCode::InvalidArgument,"环模板或键长无效");
        auto& doc=context.document;const auto& v=ring_library::variants[index];std::vector<ObjectId> ids;created->clear();
        for(int i=0;i<v.atomCount;++i){const auto& a=ring_library::atoms[v.firstAtom+i];ObjectId id;auto e=doc.addAtom(center+Vec2(a.x,a.y)*bondLength,a.element,&id);if(!e.ok())return CommandResult::fail(e.code,e.message);auto sem=doc.atoms().get(id)->semantics;sem.charge=int8_t(a.charge);sem.implicitHydrogens=a.element==16?int8_t(a.hydrogens):-1;e=doc.setAtomSemantics(id,sem);if(!e.ok())return CommandResult::fail(e.code,e.message);ids.push_back(id);created->push_back(ObjectRef(ObjectKind::Atom,id));}
        for(int i=0;i<v.bondCount;++i){const auto& b=ring_library::bonds[v.firstBond+i];BondRecord record;record.beginAtom=ids[b.a];record.endAtom=ids[b.b];record.order=b.order==2?BondOrder::Double:b.order==3?BondOrder::Triple:BondOrder::Single;record.display=b.wedge==1?BondDisplay::Wedge:b.wedge==2?BondDisplay::WedgeHash:BondDisplay::Normal;ObjectId id;auto e=doc.addBondRecord(record,&id);if(!e.ok())return CommandResult::fail(e.code,e.message);created->push_back(ObjectRef(ObjectKind::Bond,id));}
        return CommandResult::success();
    });
}
