#include "arc_properties.hpp"
#ifndef STUDIO_APP_ACTIONS_HPP
#include "keymap_data.hpp"
#define STUDIO_APP_ACTIONS_HPP
#include "graphics_style_menu.hpp"
Error App::finishClone(Vec2 p,uint32_t pointerModifiers){
    cloneDragging=false;s.cancel();if(!cloneSource)return Error();auto source=cloneSource;cloneSource.reset();auto delta=p-clonePress;if((mods()|pointerModifiers)&kModShift){if(std::abs(delta.x)>=std::abs(delta.y))delta.y=0;else delta.x=0;}if(core::length(delta)<s.view().deviceRadiusToDocument(2))return Error();auto refs=source->all();auto inserted=std::make_shared<std::vector<ObjectRef>>();auto e=s.submit(Command("拖动复制",kInvalidateStructure|kInvalidateGeometry,[source,refs,delta,inserted](CommandContext& c){inserted->clear();auto e=studioio::append(source->doc,c.document,refs,delta,inserted.get());return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"Ctrl 拖动复制");if(e.ok()){s.selected=*inserted;status="已复制结构";}return e;
}
bool App::shortcut(UINT key,uint32_t mask,bool editor){
    // Only legacy vector endpoints need direct caption entry. Real projection
    // atoms use the same configured element shortcuts as every other molecule.
    if(!editor&&!textEditor.active&&key>='A'&&key<='Z'&&!(mask&(kModControl|kModAlt))&&pointerOverCanvas&&!down&&!bioPlacing&&!ringPlacing&&!patternPlacing&&!radicalPlacing&&!virtualBondTool&&!phenalenePlacing&&!equivalencePlacement&&!partialChargePlacement&&s.nearestAtom(s.view().toDocument(pointerPixel),s.snapRadiusDevice())==kInvalidObjectId&&editProjectionEndpoint(s.view().toDocument(pointerPixel))){
        textEditor.select(0,-1);swallowHotkeyChar=key=='T';return true;
    }
    if(!editor&&perspectiveTool&&(key==VK_SPACE||key==VK_ESCAPE)){cancelSelectionTransform();perspectiveTool=false;s.activateCommand("chooseSelectionTool_Marquee");updateCanvasCursor();invalidate();return true;}
    // Delete follows the visible atom/bond hotspot when there is no explicit
    // selection. Keep text editing and selected-object deletion on their own
    // routes; never reuse a stale hotspot after the pointer leaves the canvas.
    if(!editor&&!textEditor.active&&key==VK_DELETE&&mask==0&&s.selected.empty()){
        rapidSprout.clear();
        if(!down&&pointerOverCanvas){
            s.input.refreshHotspot(&s);const auto spot=s.input.hotspot();
            const bool atom=spot.kind==interaction::HotspotKind::Atom&&s.doc.atoms().alive(spot.object.id);
            const bool bond=spot.kind==interaction::HotspotKind::Bond&&s.doc.bonds().alive(spot.object.id);
            if(atom||bond){
                s.cancel();const auto e=s.submit(commands::deleteObjects({spot.object}),"删除");
                error(e);if(e.ok())status=atom?"已删除捕捉的原子 · 可撤销":"已删除捕捉的化学键 · 可撤销";
                s.input.refreshHotspot(&s);invalidate();
            }
        }
        return true;
    }
    auto openShortcutAtomLabel=[&](){
        if(!pointerOverCanvas||down||textEditor.active)return;
        const auto atom=textToolTarget(s.view().toDocument(pointerPixel),true);
        if(atom!=kInvalidObjectId)openText(kInvalidObjectId,atom);
    };
    if(!editor&&(mask&(kModControl|kModAlt|kModShift))==0){
        if(key==VK_OEM_MINUS||key==VK_SUBTRACT){
            if(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Text&&s.doc.plainText(s.selected[0].id)=="B"){
                swallowHotkeyChar=true;insertMechanismRole(true);return true;
            }
        }
        if(key=='H'){
            s.input.refreshHotspot(&s);auto spot=s.input.hotspot();
            if(spot.kind==interaction::HotspotKind::Atom&&r62::mechanism::genericRole(s.doc,spot.object.id)=="base"){
                swallowHotkeyChar=true;attachMechanismHydrogen(spot.object.id);return true;
            }
        }
    }
    // A hovered atom owns the charge key even if another object is selected.
    // Text entry keeps the literal + / - characters.
    if(!editor){
        s.input.refreshHotspot(&s);auto spot=s.input.hotspot();auto chargeKey=shortcuts.match(key,mask,2);
        if(spot.kind==interaction::HotspotKind::Atom&&chargeKey&&chargeKey->command=="@defaultHotkey"&&std::string(keymap::hotkeys[chargeKey->arg].command)=="CHARGE"){
            auto selection=s.selected;s.selected={spot.object};swallowHotkeyChar=true;editorHotkey(chargeKey->arg);s.selected=selection;invalidate();return true;
        }
    }
    auto r=shortcuts.match(key,mask,0);
    if(editor){if(r&&(r->command=="@nudge"||r->command=="@delete"||r->command=="@rotateStep"||r->command=="@fixedAngles"||r->command=="@fixedLength"))return false;}
    else {
        // Charge editing follows the selected atoms even after the pointer leaves
        // the structure. Respect configured atom shortcuts and text-editor focus.
        if(!r&&!s.selected.empty()){
            auto chargeKey=shortcuts.match(key,mask,2);
            if(chargeKey&&chargeKey->command=="@defaultHotkey"&&std::string(keymap::hotkeys[chargeKey->arg].command)=="CHARGE"){
                bool hasAtom=false;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id))hasAtom=true;
                if(hasAtom){swallowHotkeyChar=true;editorHotkey(chargeKey->arg);invalidate();return true;}
            }
        }
        if(r&&r->command=="@delete"&&s.selected.empty())r=nullptr;
        if(!r){s.input.refreshHotspot(&s);auto context=s.input.hotspot().context();int scope=context==interaction::HotspotContext::Atom?2:context==interaction::HotspotContext::Bond?3:1;r=shortcuts.match(key,mask,scope);if(!r)r=shortcuts.match(key,mask,1);
            if(r&&scope!=1&&r->id.compare(0,7,"canvas.")==0)r=nullptr;
            // TranslateMessage also posts WM_CHAR for a handled letter key;
            // do not reopen a projection label editor with that character.
            if(r&&key>=32&&key<127&&(mask&(kModControl|kModAlt))==0)swallowHotkeyChar=true;
            if(r&&r->command=="@cycleHydrogen"){swallowHotkeyChar=true;if(!cycleAtomHydrogens(s.input.hotspot().object.id))editorHotkey(r->arg);invalidate();return true;}
            if(r&&r->command=="@defaultHotkey"){
                const auto& binding=keymap::hotkeys[r->arg];auto atom=scope==2?s.doc.atoms().alive(s.input.hotspot().object.id):nullptr;
                if(atom&&std::string(binding.command)=="LABELTEXT"&&(std::string(binding.value)=="O"||std::string(binding.value)=="N")){swallowHotkeyChar=true;preferAtomHydride(atom->id,std::string(binding.value)=="O"?8:7);invalidate();return true;}
                const bool opensText=std::string(binding.command)=="TOOLMODE"&&std::string(binding.value)=="TEXT";
                if(opensText)swallowHotkeyChar=true;
                editorHotkey(r->arg);
                if(opensText)openShortcutAtomLabel();
                invalidate();return true;
            }
            if(r&&r->scope>=2){auto binding=r->chemistry;interaction::ActionRequest request;request.action=binding.action;request.context=context;request.binding=&binding;request.modifiers=mask;request.hotspot=s.input.hotspot();request.pointer=request.hotspot.point;request.origin="configured shortcut";auto result=s.router.dispatch(request);if(result==interaction::RouterOutcome::Unsupported)status="此热点命令尚未实现："+r->label;else status=s.status;if(s.editAtom!=kInvalidObjectId){auto id=s.editAtom;s.editAtom=kInvalidObjectId;openText(kInvalidObjectId,id);}invalidate();return true;}
        }
    }
    if(!r)return false;
    if(!editor&&r->command=="@delete"&&s.selected.empty())return false;
    // A printable key that selected a tool must not also be typed into whatever
    // the tool opens. TranslateMessage turns the same key press into a WM_CHAR
    // that would land in a text box created by this shortcut ("t" appeared in a
    // brand-new caption); the canvas drops the pending character instead.
    if(!editor&&key>=32&&key<127&&(mask&(kModControl|kModAlt))==0)swallowHotkeyChar=true;
    execute(Button(r->id,r->label,"",r->command,r->arg));
    if(!editor&&r->command=="chooseEdittingTool_Text")openShortcutAtomLabel();
    return true;
}
void App::settingsMenu(){showSettings(0);}
void App::showSettings(int category){
    ai.context=[this](){return aiContext();};ai.apply=[this](const std::string& bytes){return applyAI(bytes);};ai.capture=[this](const std::wstring& path){screenshot(U(path),false,false);return true;};
    ai.openSettings=[this](){showSettings(1);};ai.dockChanged=[this](){if(IsWindow(h)&&canvas){layout();invalidate();}};ai.show(h,"",false,false);
    settingsWindow.drawing=&drawingSettings;settingsWindow.ai=&ai;settingsWindow.getPage=[this](){return studioio::Page::get(s.doc);};settingsWindow.getDocument=[this](){return s.doc.id();};
    settingsWindow.applyDrawing=[this](const DrawingSettings& settings,bool apply){if(!apply)return;
        auto e=s.submit(Command("结构绘图默认格式",kInvalidateAppearance|kInvalidateGeometry,[settings](CommandContext& c){settings.apply(c.document);return CommandResult::success();}),"结构绘图默认格式");if(!e.ok())throw std::runtime_error(e.message);textEditor.nextFamily=U(settings.captionFontName);textEditor.nextSize=settings.captionFontSize;s.graphicsWidth=0;invalidate();};
    settingsWindow.applyPage=[this](const studioio::Page& page,DocumentId id){if(s.doc.id()!=id)throw std::runtime_error("文档已切换，请重新打开设置。");auto e=s.submit(Command("页面设置",kInvalidateAppearance,[page](CommandContext& c){page.store(c.document);return CommandResult::success();}),"页面设置");if(!e.ok())throw std::runtime_error(e.message);fitPage();invalidate();};
    settingsWindow.editShortcuts=[this](){s.cancel();s.input.setFocus(interaction::InputFocus::ModalDialog);if(ShortcutDialog::show(settingsWindow.hwnd,shortcuts)){s.input.setKeymap(&shortcuts.chemistry);status="快捷键设置已保存";}s.input.setFocus(interaction::InputFocus::Canvas);};
    settingsWindow.shortcutSummary=[this](){std::wstring result;for(const auto& row:shortcuts.rows){if(row.scope!=0)continue;result+=studio_i18n::tr(W(row.label))+L"    "+ShortcutConfig::chord(row.key,row.modifiers)+L"\r\n";}return result;};
    settingsWindow.skinPath=interfacePath();
    settingsWindow.show(h,category);
}
void App::showCustomGroups(){
    groupsWindow.saveGroup=[this](const std::string& name){customgroups::saveSelection(s,name);};
    groupsWindow.groupsChanged=[this](){if(settingsWindow.completionReady)settingsWindow.fillCompletion();};
    groupsWindow.show(h,StudioSettingsWindow::kGroups,true);
}
std::wstring arrowLabel(const arrows::PresetSpec& preset){if(preset.presetId==49)return L"机理箭头";if(preset.presetId==57)return L"机理鱼钩箭头";if(preset.family==arrows::ArrowFamily::Arc)return L"弧线箭头";if(preset.presetId==8)return L"反应箭头";std::string title=preset.displayName;auto replace=[&](const std::string& from,const std::string& to){size_t at=0;while((at=title.find(from,at))!=std::string::npos){title.replace(at,from.size(),to);at+=to.size();}};replace("Large ","");replace("Medium ","");replace("Small ","");replace("Unbalanced Equilibrium","不等长平衡箭头");replace("Equilibrium","平衡箭头");replace("One-Sided Left","左半头箭头");replace("One-Sided Right","右半头箭头");replace("Retrosynthetic","逆合成箭头");replace("Resonance","共振双向箭头");replace("No-Go Cross","禁止反应（交叉）");replace("No-Go Hash","禁止反应（双斜线）");replace("Solid","实心箭头");replace("Bold","粗箭头");replace("Dashed","虚线箭头");replace("Hollow","空心箭头");replace("Elliptical","椭圆");replace("Double","双线");replace("Single","单线");replace("CCW","逆时针");replace("CW","顺时针");replace("2-Sided","双向");if(preset.family==arrows::ArrowFamily::Arc)title="弧线 · "+title;return W(title);}
// Single source of truth for every arrow surface: arrowPreset (persisted to
// interface.ini) decides which command the shortcut and the Home icon fire, and
// the Graphics selector card mirrors the same preset id in its arg.
void App::syncArrowSurfaces(){auto command=arrowCommand();for(auto* list:{&groups,&graphicsGroups})for(auto& group:*list)for(auto& b:group.items){if(b.id=="chooseArrowTool_Large_Solid")b.cmd=command;if(b.id=="graphics.arrowStyle")b.arg=int(arrowPreset);}}
std::string App::arrowPresetName()const{auto preset=arrows::presetById(arrowPreset);return preset?U(arrowLabel(*preset)):std::string();}
HMENU App::createArrowMenu(){
    HMENU menu=CreatePopupMenu();HMENU groups[4]={CreatePopupMenu(),CreatePopupMenu(),CreatePopupMenu(),CreatePopupMenu()};
    auto add=[&](HMENU parent,uint32_t id){auto preset=arrows::presetById(id);if(preset&&preset->implemented)studio_i18n::appendMenu(parent,MF_OWNERDRAW,UINT_PTR(1000+id),(LPCWSTR)UINT_PTR(1000+id));};
    // Both electron-flow tools share the mechanism submenu.
    // Size-only duplicates remain readable in existing documents and shortcuts.
    add(groups[0],49);add(groups[0],57);
    for(uint32_t id:{8u,10u,40u})add(groups[1],id);
    for(uint32_t id:{38u,39u,13u})add(groups[2],id);
    for(uint32_t id:{9u,32u,11u,12u,14u,15u})add(groups[3],id);
    auto category=[&](int n){studio_i18n::appendMenu(menu,MF_POPUP|MF_OWNERDRAW,(UINT_PTR)groups[n],(LPCWSTR)UINT_PTR(900+n));};
    category(1);add(menu,58);category(2);category(3);
    HMENU sizes=CreatePopupMenu();for(int tier=0;tier<5;++tier){auto text=W(arrows::headTierName(tier));studio_i18n::appendMenu(sizes,MF_STRING|(s.arrowHeadTier==tier?MF_CHECKED:0),2010+tier,text.c_str());}
    category(0);studio_i18n::appendMenu(menu,MF_POPUP,(UINT_PTR)sizes,L"箭头头部大小（非机理）");return menu;
}
void App::arrowMenu(){
    HMENU menu=createArrowMenu();
    POINT p;GetCursorPos(&p);if(ribbonTab==1)for(const auto& group:graphicsGroups)for(const auto& b:group.items)if(b.id=="graphics.arrowStyle"){p={int((b.r.X+2)*scale),int((b.r.Y+24)*scale)};ClientToScreen(h,&p);}
    int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN,p.x,p.y,0,h,0);DestroyMenu(menu);if(id>=2010&&id<2015){setArrowHeadTier(id-2010);return;}if(id<1000)return;auto preset=arrows::presetById(uint32_t(id-1000));if(!preset)return;
    chooseArrowPreset(preset->presetId);
}
namespace {struct ArcBendClipboard{bool available=false,curvatureAvailable=false,curvatureMajor=false,centerAvailable=false,radiusAvailable=false,startAvailable=false;double ratio=0,curvature=0,radius=0,start=0;Vec2 center;};ArcBendClipboard& arcBendClipboard(){static ArcBendClipboard state;return state;}}
ObjectId App::arcContextTarget(Vec2 point)const{
    double best=s.view().deviceRadiusToDocument(7);ObjectId result=kInvalidObjectId;
    for(const auto& a:s.doc.arrows().records())if(a.isAlive()){
        arrows::ArrowParams p;bool valid=false;if(!arrows::readArrow(s.doc,a.id,&p,&valid).ok()||!valid||arrows::isMechanismArrow(p))continue;
        arrows::ArrowGeometry g;if(!arrows::buildArrowGeometry(p,s.doc.defaultLineWidth(),arrows::Tessellation(),&g).ok())continue;
        if(arrows::hitArrow(g,point,0)){result=a.id;best=0;}
        for(const auto& line:g.shafts)for(size_t i=1;i<line.size();++i){auto d=line[i]-line[i-1];double length2=dot(d,d);if(length2<1e-10)continue;double t=std::max(0.,std::min(1.,dot(point-line[i-1],d)/length2));double distance=core::length(point-line[i-1]-d*t);if(distance<best){best=distance;result=a.id;}}
    }return result;
}
void App::appendArcContextMenu(HMENU menu,ObjectId target){
    if(target==kInvalidObjectId)return;studio_i18n::appendMenu(menu,MF_SEPARATOR,0,nullptr);
    auto& clip=arcBendClipboard();
    studio_i18n::appendMenu(menu,MF_STRING,48,L"复制圆心");if(clip.centerAvailable)studio_i18n::appendMenu(menu,MF_STRING,49,L"粘贴圆心");
    studio_i18n::appendMenu(menu,MF_STRING,50,L"复制半径");if(clip.radiusAvailable&&!s.arcCurvatureLocked)studio_i18n::appendMenu(menu,MF_STRING,51,L"粘贴半径");
    studio_i18n::appendMenu(menu,MF_STRING,40,L"复制弧度");if(clip.available&&!s.arcBendLocked)studio_i18n::appendMenu(menu,MF_STRING,41,L"粘贴弧度");
    studio_i18n::appendMenu(menu,MF_STRING,44,L"复制曲率");if(clip.curvatureAvailable&&!s.arcCurvatureLocked)studio_i18n::appendMenu(menu,MF_STRING,45,L"粘贴曲率");
    studio_i18n::appendMenu(menu,MF_STRING,52,L"复制起始角度");if(clip.startAvailable)studio_i18n::appendMenu(menu,MF_STRING,53,L"粘贴起始角度");
    studio_i18n::appendMenu(menu,MF_SEPARATOR,0,nullptr);
    studio_i18n::appendMenu(menu,MF_STRING,s.arcBendLocked?43:42,s.arcBendLocked?L"解锁弧度":L"锁定此弧度");
    studio_i18n::appendMenu(menu,MF_STRING,s.arcCurvatureLocked?47:46,s.arcCurvatureLocked?L"解锁曲率":L"锁定此曲率");
}
void App::arcCurvatureAction(ObjectId target,int action,bool persist){
    arrows::ArrowParams p;bool valid=false;if(!arrows::readArrow(s.doc,target,&p,&valid).ok()||!valid||arrows::isMechanismArrow(p))return;
    double chord=length(p.end-p.begin);if(chord<1e-8)return;double ratio=p.arcSagitta/chord;if(!std::isfinite(ratio)||std::abs(ratio)>4)return;
    double curvature=arrows::signedArcCurvature(p.arcSagitta,chord);
    auto circle=arc_properties::read(p);auto& clip=arcBendClipboard();
    if(action==48||action==50||action==52){if(!circle.valid){status="直线或非圆弧没有可复制的圆心、半径或起始角度";invalidate();return;}
        if(action==48){clip.center=circle.center;clip.centerAvailable=true;status="已复制圆心";}
        if(action==50){clip.radius=circle.radius;clip.radiusAvailable=true;status="已复制半径";}
        if(action==52){clip.start=circle.start;clip.startAvailable=true;status="已复制起始角度 · 画布向右 0°、向下 90°";}
    }
    else if(action==40){clip.ratio=ratio;clip.available=true;status="已复制弧度（圆心角）";}
    else if(action==44){clip.curvature=curvature;clip.curvatureMajor=std::abs(p.arcSagitta)>chord*.5;clip.curvatureAvailable=true;status="已复制曲率";}
    else if(action==41||action==45||action==49||action==51||action==53){
        if((action==41&&(s.arcBendLocked||!clip.available))||(action==45&&(s.arcCurvatureLocked||!clip.curvatureAvailable))||(action==49&&!clip.centerAvailable)||(action==51&&(s.arcCurvatureLocked||!clip.radiusAvailable))||(action==53&&!clip.startAvailable))return;
        if(circle.valid){
            if(action==41)circle.sweep=-4*std::atan(2*clip.ratio);
            if(action==49)circle.center=clip.center;
            if(action==51)circle.radius=clip.radius;
            if(action==53)circle.start=clip.start;
            if(action==45){if(std::abs(clip.curvature)<1e-9){status="零曲率为直线，无法在保持有限圆心和圆心角时粘贴";invalidate();return;}circle.radius=1/std::abs(clip.curvature);}
            if(!arc_properties::write(circle,p)){status="该参数无法组成有效圆弧";invalidate();return;}
        }else if(action==41||action==45){p.arcSagitta=action==41?chord*clip.ratio:arrows::sagittaAtCurvature(clip.curvature,chord,clip.curvatureMajor);p.family=std::abs(p.arcSagitta)>1e-8?arrows::ArrowFamily::Arc:arrows::ArrowFamily::Straight;p.sweep=p.family==arrows::ArrowFamily::Arc?arrows::ArcSweep::Free:arrows::ArcSweep::Straight;p.route.clear();}
        else{status="目标必须是圆弧";invalidate();return;}
        const char* label=action==41?"粘贴弧度":action==45?"粘贴曲率":action==49?"粘贴圆心":action==51?"粘贴半径":"粘贴起始角度";
        auto e=s.submit(Command(label,kInvalidateGeometry|kInvalidateAppearance,[p,target](CommandContext& c){auto error=arrows::writeArrow(c.document,target,p,c.document.defaultLineWidth());if(error.ok()){auto ref=ObjectRef(ObjectKind::Arrow,target);c.document.noteTouchedExtensions(ref,false);c.document.extensions(ref).erase("studio.cdxml.graphic");}return error.ok()?CommandResult::success():CommandResult::fail(error.code,error.message);}),label);if(!e.ok()){error(e);return;}status=std::string("已")+label;
    }else if(action==42||action==43||action==46||action==47){s.cancel();if(action==42||action==43){s.arcBendLocked=action==42;if(s.arcBendLocked){s.arcBendRatio=ratio;s.arcCurvatureLocked=false;}}else{s.arcCurvatureLocked=action==46;if(s.arcCurvatureLocked){s.arcCurvature=curvature;s.arcCurvatureMajor=std::abs(p.arcSagitta)>chord*.5;s.arcBendLocked=false;}}if(persist){auto path=interfacePath();auto value=std::to_wstring(s.arcBendRatio),curve=std::to_wstring(s.arcCurvature);WritePrivateProfileStringW(L"Arc",L"Bend",value.c_str(),path.c_str());WritePrivateProfileStringW(L"Arc",L"Locked",s.arcBendLocked?L"1":L"0",path.c_str());WritePrivateProfileStringW(L"Arc",L"Curvature",curve.c_str(),path.c_str());WritePrivateProfileStringW(L"Arc",L"CurvatureMajor",s.arcCurvatureMajor?L"1":L"0",path.c_str());WritePrivateProfileStringW(L"Arc",L"CurvatureLocked",s.arcCurvatureLocked?L"1":L"0",path.c_str());}status=s.arcCurvatureLocked?"曲率已锁定：新画及拖动端点时保持；拖动中点可自由调弯":s.arcBendLocked?"弧度已锁定：新画及拖动端点时保持；拖动中点可自由调弯":"已解锁";}
    invalidate();
}
void App::setArrowHeadTier(int tier,bool persist){
    // The annotation slab also contains shapes and imported graphics. They do
    // not have ArrowParams and must not invalidate the entire size operation.
    s.cancel();down=false;if(GetCapture()==canvas)ReleaseCapture();
    tier=std::max(0,std::min(4,tier));auto e=s.submit(Command("箭头头部大小",kInvalidateAppearance|kInvalidateGeometry,[tier](CommandContext& c){
        std::vector<ObjectId> ids;for(const auto& a:c.document.arrows().records())if(a.isAlive())ids.push_back(a.id);
        for(auto id:ids){arrows::ArrowParams p;bool valid=false;auto err=arrows::readArrow(c.document,id,&p,&valid);if(!err.ok())return CommandResult::fail(err.code,err.message);if(!valid||arrows::isMechanismArrow(p))continue;
            err=arrows::writeArrow(c.document,id,arrows::withHeadTier(p,tier),c.document.defaultLineWidth());if(!err.ok())return CommandResult::fail(err.code,err.message);
            auto ref=ObjectRef(ObjectKind::Arrow,id);c.document.noteTouchedExtensions(ref,false);c.document.extensions(ref).erase("studio.cdxml.graphic");
        }return CommandResult::success();}),"箭头头部大小");
    if(!e.ok()){error(e);return;}s.arrowHeadTier=tier;if(persist){auto path=interfacePath();auto value=std::to_wstring(tier);WritePrivateProfileStringW(L"Arrow",L"HeadTier",value.c_str(),path.c_str());}
    status=std::string("非机理箭头头部：")+arrows::headTierName(tier)+"；已有和新画箭头统一生效。";invalidate();
}
void App::chooseArrowPreset(uint32_t id){
    auto preset=arrows::presetById(id);if(!preset||!preset->implemented)return;
    if(preset->family==arrows::ArrowFamily::Arc&&id!=49&&id!=57&&id!=58){id=58;preset=arrows::presetById(id);}
    std::vector<ObjectId> selected;if(id!=49&&id!=57&&id!=58)for(auto ref:s.selected)if(ref.kind==ObjectKind::Arrow&&s.doc.arrows().alive(ref.id)&&static_cast<const Document&>(s.doc).extensions(ref).find(arrows::kArrowParamsKey))selected.push_back(ref.id);
    if(!selected.empty()){auto presetId=preset->presetId;int tier=s.arrowHeadTier;error(s.submit(Command("箭头样式",kInvalidateAppearance|kInvalidateGeometry,[selected,presetId,tier](CommandContext& c){for(auto id:selected){arrows::ArrowParams params;bool ok=false;auto e=arrows::readArrow(c.document,id,&params,&ok);if(e.ok()&&!arrows::isMechanismArrow(params))e=arrows::writeArrow(c.document,id,arrows::withHeadTier(arrows::applyPresetStyle(params,presetId,c.document.defaultLineWidth()),tier),c.document.defaultLineWidth());if(!e.ok())return CommandResult::fail(e.code,e.message);}return CommandResult::success();}),"箭头样式"));}
    arrowPreset=id;
    if(!testing){auto path=interfacePath();auto stored=std::to_wstring(id);WritePrivateProfileStringW(L"Arrow",L"Preset",stored.c_str(),path.c_str());}
    syncArrowSurfaces();
    s.activateCommand(preset->sourceCommand);status=id==58?"自由弧线：先定起点和终点，再移动鼠标选择弧度并单击完成；右键可复制、粘贴和锁定弧度。":id==57?"机理鱼钩箭头：单电子转移；从电子来源拖到目标。":id==49?"机理箭头：从电子来源拖到目标，自动选择路径；Alt 自由绘制。":"拖动绘制箭头；选中后拖动两端调整长度，中间点调整弧度。Alt 解除角度约束。";
}
bool App::drawArrowMenuItem(DRAWITEMSTRUCT* item){
    if(!item||item->CtlType!=ODT_MENU||item->itemData<900)return false;
    uint32_t data=uint32_t(item->itemData);bool category=data<905;
    auto preset=data>=1000?arrows::presetById(data-1000):nullptr;
    if(!category&&!preset)return false;
    const uint32_t previews[]={49,8,38,32,33};
    if(category)preset=arrows::presetById(previews[data-900]);
    Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.TranslateTransform(float(item->rcItem.left),float(item->rcItem.top));g.ScaleTransform(scale,scale);
    float width=(item->rcItem.right-item->rcItem.left)/scale,height=(item->rcItem.bottom-item->rcItem.top)/scale;
    bool hot=(item->itemState&ODS_SELECTED)!=0,disabled=(item->itemState&ODS_DISABLED)!=0;
    SolidBrush background(hot?studio_ui::skinColors().hover:white);g.FillRectangle(&background,0.f,0.f,width,height);
    Color color=disabled?muted:ink;
    if(preset)arrowPreview(g,preset->presetId,RectF(24,4,66,height-8),disabled?muted:teal);
    if(!category&&preset&&preset->presetId==arrowPreset){Pen pen(teal,1.7f);g.DrawLine(&pen,7.f,height*.5f,11.f,height*.5f+4);g.DrawLine(&pen,11.f,height*.5f+4,17.f,height*.5f-4);}
    const char* names[]={"机理箭头","反应箭头","平衡与共振","更多样式","弧线样式"};
    txt(g,category?names[data-900]:U(arrowLabel(*preset)),RectF(100,0,width-116,height),11,color);
    return true;
}
#endif
