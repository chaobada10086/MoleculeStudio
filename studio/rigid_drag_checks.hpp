#pragma once
void App::rigidDragCheck(const std::string& dir){
 auto require=[](bool b,const char* m){if(!b)throw std::runtime_error(m);};
 s.cancel();s.doc=Document();drawingSettings.apply(s.doc);s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
 s.activateCommand("chooseRingTool_Benzene");s.gesture(PointerPhase::Down,Vec2());s.gesture(PointerPhase::Up,Vec2());s.selected=s.all();s.activateCommand("chooseSelectionTool_Marquee");
 if(auto file=std::getenv("MOLECULE_STUDIO_RIGID_DRAG_FILE")){require(s.open(file).ok(),"large fixture opens");s.selected=s.all();s.activateCommand("chooseSelectionTool_Marquee");s.fit();}
 auto v=s.view();if(!std::getenv("MOLECULE_STUDIO_RIGID_DRAG_FILE")){v.scale=2;v.center=Vec2();}s.setView(v);auto before=s.doc.digest();auto box=studioio::selectionBounds(s);auto start=s.view().toDevice((box.min+box.max)*.5);for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()){auto q=s.view().toDevice(atom.position);if(q.x>20&&q.y>20&&q.x<s.renderView.widthPx-20&&q.y<s.renderView.heightPx-20&&selectionHandleAt(q)<0){start=q;break;}}auto at=[](Vec2 p){return MAKELPARAM(int(p.x),int(p.y));};
 SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,at(start));require(s.objectDragActive(),"real drag begins");
 size_t bad=0,total=0;
#ifdef MOLECULE_STUDIO_DX11
 dx11::Renderer reference;std::string why;require(reference.initialize(&why),"reference initializes");
 for(int i=1;i<=4;++i){SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,at(start+Vec2(i*9,i*5)));render();require(dx11Renderer->rotationPreviewApplied(),"GPU rigid preview applied");std::vector<uint8_t> gpu,cpu;gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&gpu);auto o=s.overlay();render::RenderStats st;require(reference.draw(s.displayScene(),&o.scene(),s.renderView,&st).ok(),"reference draws CPU geometry");reference.readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&cpu);require(gpu.size()==cpu.size(),"matching viewport");for(size_t j=0;j<gpu.size();++j){bad+=std::abs(int(gpu[j])-int(cpu[j]))>8;++total;}}
 require(bad<total/100,"GPU and CPU images agree within rasterization tolerance");
#endif
 require(s.doc.digest()==before,"preview preserves document");SendMessageW(canvas,WM_LBUTTONUP,0,at(start+Vec2(36,20)));auto after=s.doc.digest();require(after!=before,"drag commits");require(s.undo().ok()&&s.doc.digest()==before,"drag undo");require(s.undo(true).ok()&&s.doc.digest()==after,"drag redo");
 std::ofstream(dir+"/rigid-drag-checks.json")<<"{\"passed\":true,\"preview_preserves_document\":true,\"undo_redo\":true,\"pixel_channels_over_tolerance\":"<<bad<<",\"channels\":"<<total<<"}";
 s.dirty=false;
}
