#pragma once
void App::initializeElements(){
    elements.attach(h);
    elements.choose=[this](int z){selectElement(z);};
    elements.recolor=[this](int z){recolorElement(z);};
}
void App::selectElement(int z){
    if(textEditor.active&&!finishText(true))return;
    s.cancel();if(patternPlacing)cancelPatternPlacement();if(ringPlacing)cancelRingPlacement();if(bioPlacing)cancelBioPlacement();
    execute(Button("","","","chooseSelectionTool_Marquee"));
    elements.active=z;elements.refresh();
    std::vector<ObjectId> atoms;for(auto ref:studioio::expanded(s.doc,s.selected))if(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id))atoms.push_back(ref.id);
    if(!atoms.empty()){
        auto color=elementpalette::settings().color(z);
        error(s.submit(Command("设置元素",kInvalidateStructure|kInvalidateGeometry|kInvalidateSemantics|kInvalidateAppearance,[atoms,z,color](CommandContext& c){
            for(auto id:atoms){auto result=chemistry::setElementCommand(id,z).execute(c);if(!result.ok())return result;auto ref=ObjectRef(ObjectKind::Atom,id);auto e=graphic_style::apply(c.document,ref,0,color,false,true);if(!e.ok())return CommandResult::fail(e.code,e.message);c.document.extensions(ref).set("studio.color",std::to_string(color));}
            return CommandResult::success();}),"设置元素"));
    }
    auto message=studio_i18n::tr(L"元素 %s：点击画布添加或替换原子，Esc 退出");auto slot=message.find(L"%s");if(slot!=std::wstring::npos)message.replace(slot,2,W(elementpalette::Palette::symbol(z)));status=U(message);updateInfo();invalidate();SetFocus(canvas);
}
void App::placeElement(Vec2 point){
    const int z=elements.active;if(!z)return;const auto color=elementpalette::settings().color(z);auto target=s.nearestAtom(point,s.snapRadiusDevice());
    error(s.submit(Command("绘制元素",kInvalidateStructure|kInvalidateGeometry|kInvalidateSemantics|kInvalidateAppearance,[target,z,color,point](CommandContext& c){
        auto id=target;if(id==kInvalidObjectId){auto e=c.document.addAtom(point,z,&id);if(!e.ok())return CommandResult::fail(e.code,e.message);}else{auto result=chemistry::setElementCommand(id,z).execute(c);if(!result.ok())return result;}
        auto ref=ObjectRef(ObjectKind::Atom,id);auto e=graphic_style::apply(c.document,ref,0,color,false,true);if(!e.ok())return CommandResult::fail(e.code,e.message);c.document.extensions(ref).set("studio.color",std::to_string(color));return CommandResult::success();}),"绘制元素"));
    updateInfo();invalidate();
}
void App::recolorElement(int z){
    std::vector<ObjectId> atoms;for(const auto& a:s.doc.atoms().records())if(a.isAlive()&&a.element==z&&a.semantics.nodeType==uint8_t(NodeType::Element))atoms.push_back(a.id);
    if(!atoms.empty()){auto color=elementpalette::settings().color(z);error(s.submit(Command("元素颜色",kInvalidateAppearance,[atoms,color](CommandContext& c){for(auto id:atoms){auto ref=ObjectRef(ObjectKind::Atom,id);auto e=graphic_style::apply(c.document,ref,0,color,false,true);if(!e.ok())return CommandResult::fail(e.code,e.message);c.document.extensions(ref).set("studio.color",std::to_string(color));c.document.extensions(ref).erase("studio.text.colors");}return CommandResult::success();}),"元素颜色"));}
    invalidate();
}
void App::elementPaletteCheck(const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto saved=elementpalette::settings();struct Restore{elementpalette::Settings original;~Restore(){elementpalette::settings()=original;elementpalette::settings().save();}} restore{saved};
    std::set<std::pair<int,int>> positions;for(int z=1;z<=118;++z){auto p=elementpalette::Palette::cell(z);require(p.x>=0&&p.x<18&&p.y>=0&&p.y<10,"periodic table bounds");require(positions.insert({p.x,p.y}).second,"periodic table overlapping elements");require(elements.hit(22+p.x*42+19,76+p.y*42+19)==z,"periodic table hit testing");}
    RECT c,b,v;GetWindowRect(canvas,&c);GetWindowRect(elements.bar,&b);GetWindowRect(vbar,&v);require(c.right==b.left&&b.right==v.left,"palette between canvas and scrollbar");
    s.doc=Document();s.selected.clear();s.refresh();elementpalette::settings().colors[8]=0xD32F2FFFu;selectElement(8);placeElement(Vec2(0,0));require(s.doc.stats().aliveAtoms==1,"element placement");auto id=s.doc.atoms().records().front().id;require(s.doc.atoms().alive(id)->element==8,"oxygen placed");
    require(*s.doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.color")==std::to_string(0xD32F2FFFu),"element color stored");
    selectElement(7);placeElement(Vec2(0,0));require(s.doc.stats().aliveAtoms==1&&s.doc.atoms().alive(id)->element==7,"element replacement");require(s.undo().ok()&&s.doc.atoms().alive(id)->element==8,"element replacement undo");
    auto& pins=elementpalette::settings().pins;pins.erase(std::remove(pins.begin(),pins.end(),16),pins.end());elements.selected=16;elements.action(1);require(elementpalette::settings().pinned(16),"pin element");elementpalette::Settings reload;require(reload.pinned(16)&&reload.color(8)==0xD32F2FFFu,"palette settings persist");
    elements.action(1);require(!elementpalette::settings().pinned(16),"unpin element");elementpalette::settings()=saved;require(elementpalette::settings().save(),"restore palette settings");
    elements.active=0;elements.selected=8;elements.refresh();s.doc=Document();s.selected.clear();s.refresh();
    screenshot(dir+"/element-bar.png");elements.show();UpdateWindow(elements.table);
    RECT rc;GetClientRect(elements.table,&rc);HDC dc=GetDC(elements.table);Bitmap shot(rc.right,rc.bottom,PixelFormat32bppARGB);Graphics g(&shot);HDC target=g.GetHDC();elements.paint(elements.table,target,true);g.ReleaseHDC(target);ReleaseDC(elements.table,dc);CLSID png;CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}",&png);shot.Save(W(dir+"/periodic-table.png").c_str(),&png,nullptr);ShowWindow(elements.table,SW_HIDE);
    std::ofstream(dir+"/element-palette-checks.json")<<"{\"passed\":true,\"elements\":118,\"layout\":true,\"placement\":true,\"replacement_undo\":true,\"colors\":true,\"pin_persistence\":true}";
}
