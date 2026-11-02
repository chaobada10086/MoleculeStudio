#ifndef STUDIO_CONFORMER3D_HPP
#define STUDIO_CONFORMER3D_HPP
void App::conformer3D(){
    auto refs=chemicalScope();std::vector<ObjectId> atoms,bondIds;std::map<ObjectId,int> index;Box bounds;
    for(auto r:refs)if(r.kind==ObjectKind::Atom){
        auto a=s.doc.atoms().get(r.id);
        if(a->semantics.nodeType!=uint8_t(NodeType::Element)){status="请先展开缩写或移除查询通配符，再进行三维整理";return;}
        index[r.id]=int(atoms.size());atoms.push_back(r.id);bounds.add(a->position);
    }
    if(atoms.empty()||atoms.size()>1000){status="三维整理需要选中 1–1000 个原子";return;}
    for(auto r:refs)if(r.kind==ObjectKind::Bond)bondIds.push_back(r.id);
    std::ostringstream input;input.precision(15);input<<atoms.size()<<' '<<bondIds.size()<<'\n'<<s.doc.defaultBondLength()<<'\n';
    for(auto id:atoms){auto a=s.doc.atoms().get(id);input<<a->element<<' '<<int(a->semantics.charge)<<' '<<a->semantics.isotope<<' '<<int(a->semantics.implicitHydrogens)<<' '<<a->position.x<<' '<<a->position.y<<'\n';}
    for(auto id:bondIds){
        auto b=s.doc.bonds().get(id);
        if(b->order!=BondOrder::Single&&b->order!=BondOrder::Double&&b->order!=BondOrder::Triple&&b->order!=BondOrder::Aromatic){status="三维整理不支持未指定键级的查询键";return;}
        int order=b->order==BondOrder::Double?2:b->order==BondOrder::Triple?3:b->order==BondOrder::Aromatic?4:1;
        int wedge=b->display==BondDisplay::Wedge?1:b->display==BondDisplay::WedgeHash?2:0;
        input<<index[b->beginAtom]<<' '<<index[b->endAtom]<<' '<<order<<' '<<wedge<<'\n';
    }
    std::string output;status="正在生成三维构象…（RDKit）";invalidate(false);UpdateWindow(h);
    if(!chemservice::run(h,input.str(),"text/plain","text/plain","conformer3d",&output)){status=output;return;}
    std::istringstream result(output);std::string ok;size_t n;result>>ok>>n;
    if(ok!="OK"||n!=atoms.size()){status="三维整理返回了无效的原子映射";return;}
    Vec2 center=(bounds.min+bounds.max)*.5;std::vector<Vec2> positions;std::vector<double> depths;
    for(size_t i=0;i<n;i++){double x,y,z;if(!(result>>x>>y>>z)||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)){status="三维整理返回了无效坐标";return;}positions.push_back(center+Vec2(x,y));depths.push_back(z);}
    struct BondUpdate{ObjectId id,a,b;int display;};std::vector<BondUpdate> updates;size_t count=0;
    if(!(result>>ok>>count)||ok!="BONDS"||count!=bondIds.size()){status="三维整理返回了无效的立体键映射";return;}
    for(size_t i=0;i<count;i++){
        size_t row,a,b;int code;if(!(result>>row>>a>>b>>code)||row!=i||a>=atoms.size()||b>=atoms.size()||code< -1||code>2){status="三维整理返回了无效的立体键";return;}
        auto old=s.doc.bonds().get(bondIds[i]);if(!((old->beginAtom==atoms[a]&&old->endAtom==atoms[b])||(old->beginAtom==atoms[b]&&old->endAtom==atoms[a]))){status="三维整理改变了连接关系，已保留原结构";return;}
        if(code>=0)updates.push_back({bondIds[i],atoms[a],atoms[b],code});
    }
    error(s.submit(Command("三维构象整理",kInvalidateGeometry|kInvalidateStructure,[atoms,positions,depths,updates](CommandContext& c){
        for(auto id:atoms){auto& bag=c.document.extensions(ObjectRef(ObjectKind::Atom,id));bag.erase("studio.atom.z");bag.erase("pst.attr.xyz");}
        for(size_t i=0;i<atoms.size();i++){auto e=c.document.setAtomPosition(atoms[i],positions[i]);if(!e.ok())return CommandResult::fail(e.code,e.message);c.document.extensions(ObjectRef(ObjectKind::Atom,atoms[i])).set("studio.atom.z",std::to_string(depths[i]));}
        for(auto b:updates){auto e=c.document.setBondEndpoints(b.id,b.a,b.b);if(e.ok())e=c.document.setBondDisplay(b.id,b.display==1?BondDisplay::Wedge:b.display==2?BondDisplay::WedgeHash:BondDisplay::Normal);if(e.ok())e=c.document.setBondStereo(b.id,b.display==1?BondStereo::Up:b.display==2?BondStereo::Down:BondStereo::None);if(!e.ok())return CommandResult::fail(e.code,e.message);}
        return CommandResult::success();
    }),"三维构象整理"));
    status="已生成三维构象（RDKit），并按绝对构型调整楔键";
}
#endif
