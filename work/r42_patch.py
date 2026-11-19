from pathlib import Path
root=Path.cwd(); names=['engine/catalytic.hpp','catalytic_dialog.hpp','catalytic_ui.hpp','selection_transform.hpp','ribbon_tabs.hpp','studio.cpp','catalytic_v2_checks.hpp']
backup=root/'work/r42-backup'; backup.mkdir(exist_ok=True)
for n in names:(backup/Path(n).name).write_bytes((root/'studio'/n).read_bytes())
p=root/'studio/engine/catalytic.hpp';s=p.read_text()
a=s.index('    if(*mode=="cycle"&&p.route.size()>2)');b=s.index('    auto delta=to-from;',a)
s=s[:a]+'''    if(*mode=="cycle"&&p.route.size()>2){
        // Keep an unclipped path as the source of every layout. Never clip the
        // last displayed path again: refresh and whole-cycle drags must be idempotent.
        auto ref=ObjectRef(ObjectKind::Arrow,a.id);auto stored=d.extensions(ref).findValue("pst.attr.MoleculeStudioCatalyticPath");
        arrows::ArrowParams full=p;bool valid=false;if(stored)full=arrows::decodeArrowParams(*stored,&valid);
        if(!valid){auto ds=from-world(p,p.mechanismDeparture),dt=to-world(p,p.mechanismArrival);if(length(ds)<1e-6&&length(dt)<1e-6)continue;
            auto pts=arrows::routedPoints(p);for(size_t i=0;i<pts.size();++i){double t=double(i)/(pts.size()-1);pts[i]=pts[i]+ds*(1-t)+dt*t;}p.begin=pts.front();p.end=pts.back();p.route.clear();for(auto q:pts)p.route.push_back(local(p,q));
        }else{
            auto pts=arrows::routedPoints(full);auto ds=from-full.begin,dt=to-full.end;
            for(size_t i=0;i<pts.size();++i){double t=double(i)/(pts.size()-1);pts[i]=pts[i]+ds*(1-t)+dt*t;}
            full.begin=pts.front();full.end=pts.back();full.route.clear();for(auto q:pts)full.route.push_back(local(full,q));
            auto encoded=arrows::encodeArrowParams(full);if(!stored||*stored!=encoded){d.extensions(ref).set("pst.attr.MoleculeStudioCatalyticPath",encoded);d.noteTouchedExtensions(ref,false);}
            size_t first=0,last=pts.size()-1,cap=size_t((pts.size()-1)*.38);double ga=extent(d,p.electronSource.id),gb=extent(d,p.electronTarget.id);
            while(first<cap&&length(pts[first]-from)<ga)++first;while(last>pts.size()-1-cap&&length(pts[last]-to)<gb)--last;
            p.begin=pts[first];p.end=pts[last];p.route.clear();for(size_t i=first;i<=last;++i)p.route.push_back(local(p,pts[i]));
        }
        p.mechanismDeparture=local(p,from);p.mechanismArrival=local(p,to);
        arrows::ArrowParams old;bool used=false;arrows::readArrow(d,a.id,&old,&used);if(arrows::encodeArrowParams(old)==arrows::encodeArrowParams(p))continue;
        auto e=arrows::writeArrow(d,a.id,p,d.defaultLineWidth());if(!e.ok())return e;continue;}
''' +s[b:]
a=s.index('        double ga=std::min(step*.38');b=s.index('        auto pt=',a)
s=s[:a]+s[b:]
s=s.replace('e=arrows::writeArrow(d,id,p,d.defaultLineWidth());if(!e.ok())return e;if(made)', 'd.extensions(ObjectRef(ObjectKind::Arrow,id)).set("pst.attr.MoleculeStudioCatalyticPath",arrows::encodeArrowParams(p));d.noteTouchedExtensions(ObjectRef(ObjectKind::Arrow,id),false);e=arrows::writeArrow(d,id,p,d.defaultLineWidth());if(!e.ok())return e;if(made)')
s=s.replace('    return Error();\n}\n} }','    return update(d);\n}\n} }');p.write_text(s)
p=root/'studio/selection_transform.hpp';s=p.read_text();s=s.replace('if(s.selected.size()==1&&(s.selected[0].kind==ObjectKind::Arrow||s.selected[0].kind==ObjectKind::Table))return {};','''if(s.selected.size()==1){auto ref=s.selected[0];if(ref.kind==ObjectKind::Table)return {};if(ref.kind==ObjectKind::Arrow){graphics::ShapeParams shape;bool valid=false;graphics::readShape(s.doc,ref.id,&shape,&valid);if(!valid)return {};}}''')
s=s.replace('transformMatrix=selection::Affine2::scaling(transformPivot,factor,factor);','''bool graphicOnly=true;for(auto ref:transformRefs)if(ref.kind!=ObjectKind::Arrow&&ref.kind!=ObjectKind::Bracket)graphicOnly=false;
        double sx=factor,sy=factor;if(graphicOnly){if(transformHandle==1||transformHandle==6)sx=1;else if(transformHandle==3||transformHandle==4)sy=1;}
        transformMatrix=selection::Affine2::scaling(transformPivot,sx,sy);''');p.write_text(s)
# Native paired bracket with an attached double-dagger label: one selectable object.
p=root/'studio/ribbon_tabs.hpp';s=p.read_text().replace('Button("mechanism.virtualBond","待成键","virtualBond","@virtualBond")','Button("mechanism.virtualBond","待成键","virtualBond","@virtualBond"),Button("mechanism.transitionState","过渡态 [ ]‡","bracket","@transitionState")');p.write_text(s)
p=root/'studio/studio.cpp';s=p.read_text();at=s.index('    if(b.cmd=="@catalytic")')
s=s[:at]+'''    if(b.cmd=="@transitionState"){
        if(textEditor.active&&!finishText(true))return;auto bounds=studioio::selectionBounds(s);auto made=std::make_shared<ObjectId>();
        if(bounds.empty())bounds=Box(s.view().center-Vec2(40,26),s.view().center+Vec2(40,26));else {bounds.min=bounds.min-Vec2(10,10);bounds.max=bounds.max+Vec2(10,10);}
        error(s.submit(Command("插入过渡态中括号",kInvalidateGeometry|kInvalidateAppearance,[bounds,made](CommandContext& c){graphics::BracketParams p;p.side=graphics::BracketSide::Pair;p.family=graphics::BracketFamily::Square;p.frame.centre=(bounds.min+bounds.max)*.5;p.frame.axisU=Vec2(bounds.width()*.5,0);p.frame.axisV=Vec2(0,bounds.height()*.5);graphics::BracketLabel label;label.text="‡";label.size=c.document.defaultTextSize();label.anchor=graphics::BracketLabelAnchor(1.12,-1.18);p.labels.push_back(label);auto e=graphics::addBracket(c.document,p,c.document.defaultLineWidth(),made.get());return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"过渡态"));
        s.selected={ObjectRef(ObjectKind::Bracket,*made)};s.activateCommand("chooseSelectionTool_Marquee");invalidate();return;
    }
''' + s[at:];p.write_text(s)
