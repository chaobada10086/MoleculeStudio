#pragma once
void App::performanceAiCheck(const std::string& file,const std::string& dir){
    auto require=[](bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);};
    try{
        require(s.open(file).ok(),"open large fixture");ObjectId id=29553;require(s.doc.atoms().alive(id)!=nullptr,"fixture atom");
        std::vector<ObjectId> todo(1,id);std::set<ObjectId> atoms;atoms.insert(id);
        for(size_t i=0;i<todo.size();++i)for(auto other:s.doc.neighbours(todo[i]))if(atoms.insert(other).second)todo.push_back(other);
        transformOwned.clear();for(auto atom:atoms)transformOwned.insert(ObjectRef(ObjectKind::Atom,atom));for(const auto& bond:s.doc.bonds().records())if(bond.isAlive()&&atoms.count(bond.beginAtom)&&atoms.count(bond.endAtom))transformOwned.insert(ObjectRef(ObjectKind::Bond,bond.id));
        auto view=s.view();view.scale=.8;view.dpiScale=1;view.center=s.doc.atoms().get(id)->position;view.viewportCenter=Vec2(320,240);s.setView(view);s.renderView.widthPx=640;s.renderView.heightPx=480;
#ifdef MOLECULE_STUDIO_DX11
        dx11::Renderer fast,legacy;std::string why;require(fast.initialize(&why)&&legacy.initialize(&why),"initialize renderers");render::RenderStats st;
        require(fast.draw(s.gpuScene,nullptr,s.renderView,&st).ok()&&legacy.draw(s.gpuScene,nullptr,s.renderView,&st).ok(),"warm base scene");
        std::ofstream log(dir+"/performance.txt");transformDragging=true;transformPreviewDocument=0;
        for(int frame=1;frame<=3;++frame){transformRevision++;transformMatrix=frame==3?selection::Affine2::rotation(view.center,17):selection::Affine2::scaling(view.center,1+frame*.1,1+frame*.1);
            double a=now();const auto& preview=selectionTransformScene();render::RenderStats inc;require(fast.draw(preview,nullptr,s.renderView,&inc).ok(),"incremental transform");double changedMs=now()-a;
            std::vector<uint8_t> actual;fast.readPixels(0,0,640,480,&actual);
            a=now();auto old=preview;uint64_t ordinal=0;for(auto& p:old.mutablePrimitives())if(p.layer.value!=0)p.version=(s.serial*1000003+transformRevision*1000033+(++ordinal))|1ull;
            require(legacy.draw(old,nullptr,s.renderView,&st).ok(),"legacy transform");double oldMs=now()-a;std::vector<uint8_t> expected;legacy.readPixels(0,0,640,480,&expected);require(actual==expected,"transform pixel equality");
            size_t untouched=0;for(size_t i=0;i<preview.size();++i)if(preview.primitives()[i].version==s.gpuScene.primitives()[i].version)++untouched;
            require(untouched>preview.size()/2,"untouched primitives retain cache identity");
            log<<"transform"<<frame<<" legacy_ms="<<oldMs<<" optimized_ms="<<changedMs<<" unchanged="<<untouched<<" total="<<preview.size()<<" pixel_equal=1\n";log.flush();
        }
        transformDragging=false;transformPreview.clear();transformPreviewDocument=0;
        view.center=(s.scene.primitives().front().bounds.min+s.scene.primitives().front().bounds.max)*.5;s.setView(view);
        render::Scene copy;for(size_t i=0;i<std::min(size_t(2000),s.scene.size());++i)copy.add(s.gpuScene.primitives()[i+2]);
        for(int frame=0;frame<4;++frame){Vec2 delta(frame*10,frame*6);fast.setCopyPreview(&copy,delta,s.doc.id());double a=now();require(fast.draw(s.gpuScene,nullptr,s.renderView,&st).ok(),"cached copy draw");double ms=now()-a;size_t copyMeshes=st.meshRebuilds;require(frame==0||copyMeshes==0,"copy mesh cache reused on movement");
            std::vector<uint8_t> actual;fast.readPixels(0,0,640,480,&actual);render::Scene moved;
            for(auto p:copy.primitives()){for(auto& pt:p.points)pt=pt+delta;for(auto& sub:p.subpaths)for(auto& pt:sub.points)pt=pt+delta;p.bounds.min=p.bounds.min+delta;p.bounds.max=p.bounds.max+delta;p.source=ObjectRef();p.version=0;p.cacheKey=0;moved.add(p);}
            double oldStart=now();require(legacy.draw(s.gpuScene,&moved,s.renderView,&st).ok(),"reference copy draw");double oldMs=now()-oldStart;std::vector<uint8_t> expected;legacy.readPixels(0,0,640,480,&expected);require(actual==expected,"copy pixel equality");log<<"copy"<<frame<<" optimized_ms="<<ms<<" reference_ms="<<oldMs<<" mesh_rebuilds="<<copyMeshes<<" pixel_equal=1\n";log.flush();
        }fast.setCopyPreview(nullptr,Vec2(),0);
        // Switching documents/tabs must not reuse the previous copy's geometry.
        copy.clear();copy.add(s.gpuScene.primitives().back());fast.setCopyPreview(&copy,Vec2(),s.doc.id()+1);require(fast.draw(s.gpuScene,nullptr,s.renderView,&st).ok(),"copy invalidates on new content token");fast.setCopyPreview(nullptr,Vec2(),0);
#endif
        {auto initial=s.view();initial.center=Vec2(30,40);initial.scale=.8;s.setView(initial);POINT wheelPoint={100,100};ClientToScreen(canvas,&wheelPoint);auto anchor=initial.toDocument(Vec2(100,100));
        for(int i=0;i<6;++i)SendMessageW(canvas,WM_MOUSEWHEEL,MAKEWPARAM(0,20),MAKELPARAM(wheelPoint.x,wheelPoint.y));
        require(std::abs(s.view().scale-initial.scale*1.15)<1e-10,"high resolution wheel accumulates to one notch");require(core::length(s.view().toDocument(Vec2(100,100))-anchor)<1e-8,"zoom preserves pointer anchor");}
        ObjectId last=0;for(const auto& atom:s.doc.atoms().records())if(atom.isAlive())last=atom.id;s.selected={ObjectRef(ObjectKind::Atom,last)};s.doc.setDefaultTextSize(14);
        tinyxml2::XMLDocument context;auto bytes=aiContext();require(context.Parse(bytes.c_str())==tinyxml2::XML_SUCCESS,"parse AI snapshot");auto scene=context.RootElement()->FirstChildElement("scene");require(scene&&scene->BoolAttribute("truncated"),"large snapshot reports truncation");auto first=scene->FirstChildElement("object");require(first&&first->BoolAttribute("selected")&&std::string(first->Attribute("ref"))=="atom:"+std::to_string(last),"selected object beyond first 1500 is included first");require(context.RootElement()->FirstChildElement("context")->DoubleAttribute("text_size")==14,"AI current text default");
        std::ofstream(dir+"/audit-checks.json")<<"{\"transform_pixels\":true,\"unchanged_versions\":true,\"copy_pixels\":true,\"copy_token\":true,\"selection_snapshot\":true,\"text_default\":true,\"high_resolution_zoom\":true}";s.dirty=false;
    }catch(const std::exception& e){std::ofstream(dir+"/audit-error.txt")<<e.what();s.dirty=false;}
}
