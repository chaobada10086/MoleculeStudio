from pathlib import Path
p=Path('studio/engine/projection_molecule.hpp');s=p.read_text(encoding='utf-8');s=s.replace('} }\n',r'''
struct V3{double x,y,z;V3(double a=0,double b=0,double c=0):x(a),y(b),z(c){}};
inline V3 sub(V3 a,V3 b){return V3(a.x-b.x,a.y-b.y,a.z-b.z);}
inline double parity(const std::vector<V3>& v){if(v.size()!=4)return 0;auto a=sub(v[0],v[3]),b=sub(v[1],v[3]),c=sub(v[2],v[3]);return a.x*(b.y*c.z-b.z*c.y)-a.y*(b.x*c.z-b.z*c.x)+a.z*(b.x*c.y-b.y*c.x);}
inline const char* vectorKey(const BondRecord& b,ObjectId core){return b.beginAtom==core?"pst.attr.MoleculeStudioProjectionBegin":"pst.attr.MoleculeStudioProjectionEnd";}
inline V3 vector(const Document& d,const BondRecord& b,ObjectId c){auto v=d.extensions(ObjectRef(ObjectKind::Bond,b.id)).findValue(vectorKey(b,c));V3 result;if(v){std::istringstream in(*v);in>>result.x>>result.y>>result.z;}return result;}
inline void seed(Document& d){for(const auto& a:d.atoms().records())if(a.isAlive()&&marked(d,a.id))for(auto bid:d.incidentBonds(a.id)){auto b=d.bonds().alive(bid);auto n=b->beginAtom==a.id?b->endAtom:b->beginAtom;auto p=d.atoms().alive(n)->position-a.position;double span=length(p);if(span>1e-8)p=p*(1/span);int k=kind(d,a.id),plane=role(d,a.id)%10;V3 v;
    if(k==0)v=V3(p.x,-p.y,std::abs(p.x)>.5?1:-1);else if(marked(d,n))v=V3(0,0,plane?1:-1);else v=V3(k==1?p.x-.35*p.y:p.x,-p.y,plane?-.6:.6);
    std::ostringstream out;out.precision(17);out<<v.x<<' '<<v.y<<' '<<v.z;d.extensions(ObjectRef(ObjectKind::Bond,bid)).set(vectorKey(*b,a.id),out.str());}}
inline Error convert(Document& d,ObjectId axisId,int k,int variant){
    auto axisBond=d.bonds().alive(axisId);if(!axisBond)return Error(ErrorCode::NotFound,"请选择投影中的碳碳单键");auto a=axisBond->beginAtom,b=axisBond->endAtom;
    if(d.atoms().alive(a)->element!=6||d.atoms().alive(b)->element!=6||axisBond->order!=BondOrder::Single)return Error(ErrorCode::InvalidArgument,"投影转换需要碳碳单键");
    if(d.incidentBonds(a).size()!=4||d.incidentBonds(b).size()!=4)return Error(ErrorCode::InvalidArgument,"请选择两个四配位碳之间的键；先补齐其取代基");
    std::vector<ObjectId> roots[2];std::vector<std::vector<ObjectId>> branches[2];std::set<ObjectId> used;
    for(int plane=0;plane<2;++plane){auto c=plane?b:a,other=plane?a:b;for(auto bid:d.incidentBonds(c)){auto edge=d.bonds().alive(bid);auto n=edge->beginAtom==c?edge->endAtom:edge->beginAtom;if(n==other)continue;auto branch=component(d,n,c);for(auto id:branch)if(id==other||!used.insert(id).second)return Error(ErrorCode::InvalidArgument,"环连接不能直接展开为此投影");roots[plane].push_back(n);branches[plane].push_back(branch);}}
    const double L=d.defaultBondLength();auto origin=(d.atoms().alive(a)->position+d.atoms().alive(b)->position)*.5;
    Vec2 centres[2]={origin,origin};if(k==0){centres[0]=origin+Vec2(0,-.75*L);centres[1]=origin+Vec2(0,.75*L);}if(k==1){centres[0]=origin+Vec2(-1.4*L,.65*L);centres[1]=origin+Vec2(1.4*L,-.65*L);}
    for(int plane=0;plane<2;++plane){auto c=plane?b:a;std::vector<Vec2> rays;std::vector<V3> oldV={vector(d,*axisBond,c)},newV;
        if(k==0){rays={Vec2(0,plane?1:-1),Vec2(-1,0),Vec2(1,0)};newV={V3(0,plane?1:-1,-1),V3(0,plane?-1:1,-1),V3(-1,0,1),V3(1,0,1)};}
        else{newV.push_back(V3(0,0,plane?1:-1));for(int i=0;i<3;++i){double angle=k==1?i*120.+(plane&&variant==0?60.:0.):-90.+i*120.+(variant==0?(plane?60.:0.):(plane?8.:-8.));double t=angle*3.141592653589793/180.;Vec2 ray(cos(t),sin(t));newV.push_back(V3(ray.x,-ray.y,plane?-.6:.6));if(k==1){ray.x+=.35*ray.y;ray=ray*(1/length(ray));}rays.push_back(ray);}}
        for(auto n:roots[plane])oldV.push_back(vector(d,*d.bonds().alive(d.bondBetween(c,n)),c));if(parity(oldV)*parity(newV)<0)std::swap(rays[1],rays[2]);
        for(size_t i=0;i<roots[plane].size();++i){auto target=centres[plane]+rays[i]*(L*(k==0?1:k==1?(plane?.82:.9):1.12));auto delta=target-d.atoms().alive(roots[plane][i])->position;for(auto id:branches[plane][i])d.setAtomPosition(id,d.atoms().alive(id)->position+delta);}
        d.setAtomPosition(c,centres[plane]);mark(d,c,k,plane);
    }return Error();
}
} }
''');p.write_text(s,encoding='utf-8')
p=Path('studio/projection_templates.hpp');s=p.read_text(encoding='utf-8').replace('std::shared_ptr<Session> done(){s->refresh();','std::shared_ptr<Session> done(){projection_molecule::seed(s->doc);s->refresh();');p.write_text(s,encoding='utf-8')
p=Path('studio/projection_actions.hpp');s=p.read_text(encoding='utf-8');needle='bool App::editProjectionEndpoint(Vec2 point,bool probe,Vec2* capture){';s=s.replace(needle,needle+'''\n    auto atom=s.nearestAtom(point,s.view().deviceRadiusToDocument(12)/s.view().docPerDevice());
    if(atom!=kInvalidObjectId&&projection_molecule::coreFor(s.doc,atom)!=kInvalidObjectId){if(capture)*capture=s.doc.atoms().alive(atom)->position;if(!probe)openText(kInvalidObjectId,atom);return true;}
''',1);s+=r'''
ObjectId App::projectionAxis(Vec2 point){
    auto pick=s.pick(point);if(pick.kind==ObjectKind::Bond){auto b=s.doc.bonds().alive(pick.id);if(b&&projection_molecule::axis(s.doc,*b))return b->id;}
    auto refs=studioio::expanded(s.doc,s.selected);for(auto ref:refs)if(ref.kind==ObjectKind::Bond){auto b=s.doc.bonds().alive(ref.id);if(b&&projection_molecule::axis(s.doc,*b))return b->id;}return kInvalidObjectId;
}
void App::convertProjection(ObjectId axis,int kind,int variant){
    if(textEditor.active&&!finishText(true))return;auto e=s.submit(Command("转换立体投影",kInvalidateGeometry|kInvalidateAppearance,[axis,kind,variant](CommandContext& c){auto e=projection_molecule::convert(c.document,axis,kind,variant);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"转换立体投影");error(e);if(e.ok())status=std::string("已转换为")+stereo_projection::name(kind)+" · 分子连接关系保持不变";invalidate();
}
''';p.write_text(s,encoding='utf-8')
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8').replace('void projectionMenu(int kind);','ObjectId projectionAxis(Vec2 point);void convertProjection(ObjectId axis,int kind,int variant);void projectionMenu(int kind);',1)
s=s.replace('            appendSelectionContextMenu(menu,structure,!s.selected.empty());',r'''            appendSelectionContextMenu(menu,structure,!s.selected.empty());
            const auto projectionBond=a->projectionAxis(point);
            if(projectionBond!=kInvalidObjectId){HMENU forms=CreatePopupMenu();for(int i=0;i<5;++i)studio_i18n::appendMenu(forms,MF_STRING,70+i,i==0?L"费歇尔投影":i==1?L"锯架式 · 交叉":i==2?L"锯架式 · 重叠":i==3?L"纽曼投影 · 交叉":L"纽曼投影 · 重叠");studio_i18n::appendMenu(menu,MF_POPUP,UINT_PTR(forms),L"转换立体投影");}
''',1).replace('            if(choice>=40&&choice<=47)','            if(choice>=70&&choice<=74)a->convertProjection(projectionBond,choice==70?0:choice<73?1:2,choice==72||choice==74?1:0);\n            else if(choice>=40&&choice<=47)',1);p.write_text(s,encoding='utf-8')
p=Path('studio/chemistry_open.py');s=p.read_text(encoding='utf-8');needle='        mols=list(Chem.MolsFromCDXML';i=s.index(needle);s=s[:i]+s[i:];needle='    if not mols or any(m is None for m in mols):';s=s.replace(needle,'''        from rdkit.Geometry import Point3D
        nodes={int(n.get('id')):n for n in root.iter('n') if n.get('id')}
        edges=list(root.iter('b'))
        for parsed in mols:
            if parsed is None: continue
            ids={a.GetIntProp('_STUDIO_CDX_ID'):a.GetIdx() for a in parsed.GetAtoms() if a.HasProp('_STUDIO_CDX_ID')}
            for ident,node in nodes.items():
                if 'MoleculeStudioProjection' not in node.attrib or ident not in ids: continue
                vectors={}
                for edge in edges:
                    begin,end=int(edge.get('B','0')),int(edge.get('E','0'))
                    key='MoleculeStudioProjectionBegin' if begin==ident else 'MoleculeStudioProjectionEnd' if end==ident else ''
                    other=end if begin==ident else begin
                    if key and edge.get(key) and other in ids:vectors[ids[other]]=tuple(map(float,edge.get(key).split()))
                if len(vectors)!=4: continue
                trial=Chem.Mol(parsed);conf=Chem.Conformer(trial.GetNumAtoms());conf.Set3D(True)
                for j in range(trial.GetNumAtoms()):conf.SetAtomPosition(j,Point3D(0,0,0))
                for j,v in vectors.items():conf.SetAtomPosition(j,Point3D(*v))
                trial.RemoveAllConformers();trial.AddConformer(conf);Chem.AssignAtomChiralTagsFromStructure(trial,replaceExistingTags=True)
                parsed.GetAtomWithIdx(ids[ident]).SetChiralTag(trial.GetAtomWithIdx(ids[ident]).GetChiralTag())
'''+needle,1);p.write_text(s,encoding='utf-8')
