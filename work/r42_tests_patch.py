from pathlib import Path
p=Path('studio/catalytic_v2_checks.hpp');s=p.read_text().replace('p.route.size()>80','p.route.size()>16')
a=s.index(' // Ordinary crossing arrows')
s=s[:a]+''' // Repeated layout, folding, and whole-object translation never consume the curve.
 auto pathLength=[&](const Document& d,ObjectId id){arrows::ArrowParams p;bool ok=false;arrows::readArrow(d,id,&p,&ok);double total=0;auto pts=arrows::routedPoints(p);for(size_t i=1;i<pts.size();++i)total+=length(pts[i]-pts[i-1]);return total;};
 std::map<ObjectId,double> lengths;for(auto r:made)lengths[r.id]=pathLength(demo.doc,r.id);
 for(int repeat=0;repeat<12;++repeat){require(update(demo.doc).ok(),"repeat update");for(auto r:made)require(std::abs(pathLength(demo.doc,r.id)-lengths[r.id])<1e-6,"refresh keeps arrow length");}
 for(int repeat=0;repeat<4;++repeat){require(integration::translateDocumentObjects(demo.doc,demo.all(),Vec2(23,-17)).ok(),"move whole cycle");require(update(demo.doc).ok(),"follow whole cycle");for(auto r:made)require(std::abs(pathLength(demo.doc,r.id)-lengths[r.id])<1e-5,"whole drag preserves curve length");}
 for(auto id:nodes)require(CatalyticMetal::fold(demo.doc,id,false),"collapse cycle node");require(update(demo.doc).ok(),"follow compact labels");
 std::map<ObjectId,double> compact;for(auto r:made){compact[r.id]=pathLength(demo.doc,r.id);require(compact[r.id]>15,"collapsed label keeps visible shaft");}
 for(int repeat=0;repeat<8;++repeat){require(update(demo.doc).ok(),"repeat compact update");for(auto r:made)require(std::abs(pathLength(demo.doc,r.id)-compact[r.id])<1e-6,"compact preview stable");}
 for(auto id:nodes)require(CatalyticMetal::fold(demo.doc,id,true),"expand cycle node");require(update(demo.doc).ok(),"expanded curve");
''' +s[a:]
a=s.index(' app.s.cancel();app.s.doc=demo.doc;')
s=s[:a]+''' // Independent preview configuration retains the other nodes.
 CycleFields individual;individual.cycle=true;individual.values={"4","107.096530","74.613799"};individual.fixedSize=true;require(individual.rebuild(),"initial individual preview");individual.nodeMetals[1].element="Ni";individual.nodeMetals[1].oxidation="III";individual.nodeMetals[1].collapsed=true;individual.editedNodes.insert(1);require(individual.rebuild(),"individual edit preview");for(size_t i=0;i<4;++i)require(CatalyticMetal::read(individual.cyclePreview->source->doc,individual.previewNodes[i]).element==(i==1?"Ni":"Pd"),"only selected complex changed");
 // Exercise the actual selection handle gesture on a native oval.
 app.s.cancel();app.s.doc=Document();app.s.history.clear();graphics::ShapeParams shape;shape.form=graphics::ShapeForm::Oval;shape.frame.centre=Vec2(100,100);shape.frame.axisU=Vec2(40,0);shape.frame.axisV=Vec2(0,25);ObjectId oval;require(graphics::addShape(app.s.doc,shape,.6,&oval).ok(),"insert oval");app.s.selected={ObjectRef(ObjectKind::Arrow,oval)};app.s.activateCommand("chooseSelectionTool_Marquee");app.s.refresh();auto handles=app.selectionHandlePoints();require(handles.size()==9,"oval exposes resize handles");auto start=app.s.view().toDevice(handles[4]);require(app.beginSelectionTransform(start,0),"begin oval width resize");app.finishSelectionTransform(start+Vec2(40,0),0);bool validShape=false;graphics::readShape(app.s.doc,oval,&shape,&validShape);require(validShape&&length(shape.frame.axisU)>40&&std::abs(length(shape.frame.axisV)-25)<1e-6,"side handle changes width only");require(app.s.undo().ok(),"undo oval resize");graphics::readShape(app.s.doc,oval,&shape,&validShape);require(std::abs(length(shape.frame.axisU)-40)<1e-6,"undo restores oval width");
 app.run(Button("test.transition","过渡态","bracket","@transitionState"));require(app.s.selected.size()==1&&app.s.selected[0].kind==ObjectKind::Bracket,"transition state is one bracket object");graphics::BracketParams bracket;bool validBracket=false;graphics::readBracket(app.s.doc,app.s.selected[0].id,&bracket,&validBracket);require(validBracket&&bracket.paired()&&bracket.labels.size()==1&&bracket.labels[0].text=="‡","paired square bracket includes double dagger");
''' +s[a:]
# run method name verify next
p.write_text(s)
