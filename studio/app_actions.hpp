#ifndef STUDIO_APP_ACTIONS_HPP
#include "keymap_data.hpp"
#define STUDIO_APP_ACTIONS_HPP
#include "graphics_style_menu.hpp"
Error App::finishClone(Vec2 p,uint32_t pointerModifiers){
    cloneDragging=false;s.cancel();if(!cloneSource)return Error();auto source=cloneSource;cloneSource.reset();auto delta=p-clonePress;if((mods()|pointerModifiers)&kModShift){if(std::abs(delta.x)>=std::abs(delta.y))delta.y=0;else delta.x=0;}if(core::length(delta)<s.view().deviceRadiusToDocument(2))return Error();auto refs=source->all();auto inserted=std::make_shared<std::vector<ObjectRef>>();auto e=s.submit(Command("拖动复制",kInvalidateStructure|kInvalidateGeometry,[source,refs,delta,inserted](CommandContext& c){inserted->clear();auto e=studioio::append(source->doc,c.document,refs,delta,inserted.get());return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"Ctrl 拖动复制");if(e.ok()){s.selected=*inserted;status="已复制结构";}return e;
}
bool App::shortcut(UINT key,uint32_t mask,bool editor){
    auto r=shortcuts.match(key,mask,0);
    if(editor){if(r&&(r->command=="@nudge"||r->command=="@delete"||r->command=="@rotateStep"||r->command=="@fixedAngles"||r->command=="@fixedLength"))return false;}
    else {
        if(r&&r->command=="@delete"&&s.selected.empty())r=nullptr;
        if(!r){s.input.refreshHotspot(&s);auto context=s.input.hotspot().context();int scope=context==interaction::HotspotContext::Atom?2:context==interaction::HotspotContext::Bond?3:1;r=shortcuts.match(key,mask,scope);if(!r)r=shortcuts.match(key,mask,1);
            if(r&&r->command=="@cycleHydrogen"){swallowHotkeyChar=true;if(!cycleAtomHydrogens(s.input.hotspot().object.id))editorHotkey(r->arg);invalidate();return true;}
            if(r&&r->command=="@defaultHotkey"){
                const auto& binding=keymap::hotkeys[r->arg];auto atom=scope==2?s.doc.atoms().alive(s.input.hotspot().object.id):nullptr;
                if(atom&&std::string(binding.command)=="LABELTEXT"&&(std::string(binding.value)=="O"||std::string(binding.value)=="N")){swallowHotkeyChar=true;preferAtomHydride(atom->id,std::string(binding.value)=="O"?8:7);invalidate();return true;}
                editorHotkey(r->arg);invalidate();return true;
            }
            if(r&&scope>=2){auto binding=r->chemistry;interaction::ActionRequest request;request.action=binding.action;request.context=context;request.binding=&binding;request.modifiers=mask;request.hotspot=s.input.hotspot();request.pointer=request.hotspot.point;request.origin="configured shortcut";auto result=s.router.dispatch(request);if(result==interaction::RouterOutcome::Unsupported)status="此热点命令尚未实现："+r->label;else status=s.status;if(s.editAtom!=kInvalidObjectId){auto id=s.editAtom;s.editAtom=kInvalidObjectId;openText(kInvalidObjectId,id);}invalidate();return true;}
        }
    }
    if(!r)return false;
    if(!editor&&r->command=="@delete"&&s.selected.empty())return false;
    // A printable key that selected a tool must not also be typed into whatever
    // the tool opens. TranslateMessage turns the same key press into a WM_CHAR
    // that would land in a text box created by this shortcut ("t" appeared in a
    // brand-new caption); the canvas drops the pending character instead.
    if(!editor&&key>=32&&key<127&&(mask&(kModControl|kModAlt))==0)swallowHotkeyChar=true;
    execute(Button(r->id,r->label,"",r->command,r->arg));return true;
}
void App::settingsMenu(){showSettings(0);}
void App::showSettings(int category){
    ai.context=[this](){return aiContext();};ai.apply=[this](const std::string& bytes){return applyAI(bytes);};ai.capture=[this](const std::wstring& path){screenshot(U(path),false,false);return true;};
    ai.openSettings=[this](){showSettings(1);};ai.dockChanged=[this](){if(IsWindow(h)&&canvas){layout();invalidate();}};ai.show(h,"",false,false);
    settingsWindow.drawing=&drawingSettings;settingsWindow.ai=&ai;settingsWindow.getPage=[this](){return studioio::Page::get(s.doc);};settingsWindow.getDocument=[this](){return s.doc.id();};
    settingsWindow.applyDrawing=[this](const DrawingSettings& settings,bool apply){if(!apply)return;
        auto e=s.submit(Command("结构绘图默认格式",kInvalidateAppearance|kInvalidateGeometry,[settings](CommandContext& c){settings.apply(c.document);return CommandResult::success();}),"结构绘图默认格式");if(!e.ok())throw std::runtime_error(e.message);s.graphicsWidth=0;invalidate();};
    settingsWindow.applyPage=[this](const studioio::Page& page,DocumentId id){if(s.doc.id()!=id)throw std::runtime_error("文档已切换，请重新打开设置。");auto e=s.submit(Command("页面设置",kInvalidateAppearance,[page](CommandContext& c){page.store(c.document);return CommandResult::success();}),"页面设置");if(!e.ok())throw std::runtime_error(e.message);fitPage();invalidate();};
    settingsWindow.saveGroup=[this](const std::string& name){customgroups::saveSelection(s,name);};
    settingsWindow.editShortcuts=[this](){s.cancel();s.input.setFocus(interaction::InputFocus::ModalDialog);if(ShortcutDialog::show(settingsWindow.hwnd,shortcuts)){s.input.setKeymap(&shortcuts.chemistry);status="快捷键设置已保存";}s.input.setFocus(interaction::InputFocus::Canvas);};
    settingsWindow.shortcutSummary=[this](){std::wstring result;for(const auto& row:shortcuts.rows){if(row.scope!=0)continue;result+=studio_i18n::tr(W(row.label))+L"    "+ShortcutConfig::chord(row.key,row.modifiers)+L"\r\n";}return result;};
    settingsWindow.show(h,category);
}
std::wstring arrowLabel(const arrows::PresetSpec& preset){std::string title=preset.displayName;auto replace=[&](const std::string& from,const std::string& to){size_t at=0;while((at=title.find(from,at))!=std::string::npos){title.replace(at,from.size(),to);at+=to.size();}};replace("Large ","");replace("Medium ","");replace("Small ","");replace("Unbalanced Equilibrium","不等长平衡箭头");replace("Equilibrium","平衡箭头");replace("One-Sided Left","左半头箭头");replace("One-Sided Right","右半头箭头");replace("Retrosynthetic","逆合成箭头");replace("Resonance","共振双向箭头");replace("No-Go Cross","禁止反应（交叉）");replace("No-Go Hash","禁止反应（双斜线）");replace("Solid","实心箭头");replace("Bold","粗箭头");replace("Dashed","虚线箭头");replace("Hollow","空心箭头");replace("Elliptical","椭圆");replace("Double","双线");replace("Single","单线");replace("CCW","逆时针");replace("CW","顺时针");replace("2-Sided","双向");return W(title);}
// Single source of truth for every arrow surface: arrowPreset (persisted to
// interface.ini) decides which command the shortcut and the Home icon fire, and
// the Graphics selector card mirrors the same preset id in its arg.
void App::syncArrowSurfaces(){auto command=arrowCommand();for(auto* list:{&groups,&graphicsGroups})for(auto& group:*list)for(auto& b:group.items){if(b.id=="chooseArrowTool_Large_Solid")b.cmd=command;if(b.id=="graphics.arrowStyle")b.arg=int(arrowPreset);}}
std::string App::arrowPresetName()const{auto preset=arrows::presetById(arrowPreset);return preset?U(arrowLabel(*preset)):std::string();}
void App::arrowMenu(){
    HMENU menu=CreatePopupMenu();HMENU groups[4]={CreatePopupMenu(),CreatePopupMenu(),CreatePopupMenu(),CreatePopupMenu()};
    for(size_t i=0;i<arrows::arrowPresetCount();i++){auto& preset=arrows::arrowPresets()[i];int g=preset.family==arrows::ArrowFamily::Arc?3:preset.sizeClass==arrows::ArrowSize::Small?2:preset.sizeClass==arrows::ArrowSize::Medium?1:0;studio_i18n::appendMenu(groups[g],MF_OWNERDRAW|(preset.implemented?0:MF_GRAYED),UINT_PTR(1000+preset.presetId),(LPCWSTR)UINT_PTR(1000+preset.presetId));}
    for(int i=0;i<4;i++)studio_i18n::appendMenu(menu,MF_POPUP|MF_OWNERDRAW,(UINT_PTR)groups[i],(LPCWSTR)UINT_PTR(900+i));
    POINT p;GetCursorPos(&p);if(ribbonTab==1)for(const auto& group:graphicsGroups)for(const auto& b:group.items)if(b.id=="graphics.arrowStyle"){p={int((b.r.X+2)*scale),int((b.r.Y+24)*scale)};ClientToScreen(h,&p);}
    int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN,p.x,p.y,0,h,0);DestroyMenu(menu);if(id<1000)return;auto preset=arrows::presetById(uint32_t(id-1000));if(!preset)return;
    chooseArrowPreset(preset->presetId);
}
void App::chooseArrowPreset(uint32_t id){
    auto preset=arrows::presetById(id);if(!preset||!preset->implemented)return;
    std::vector<ObjectId> selected;for(auto ref:s.selected)if(ref.kind==ObjectKind::Arrow&&s.doc.arrows().alive(ref.id)&&static_cast<const Document&>(s.doc).extensions(ref).find(arrows::kArrowParamsKey))selected.push_back(ref.id);
    if(!selected.empty()){auto presetId=preset->presetId;error(s.submit(Command("箭头样式",kInvalidateAppearance|kInvalidateGeometry,[selected,presetId](CommandContext& c){for(auto id:selected){arrows::ArrowParams params;bool ok=false;auto e=arrows::readArrow(c.document,id,&params,&ok);if(e.ok())e=arrows::writeArrow(c.document,id,arrows::applyPresetStyle(params,presetId,c.document.defaultLineWidth()),c.document.defaultLineWidth());if(!e.ok())return CommandResult::fail(e.code,e.message);}return CommandResult::success();}),"箭头样式"));}
    arrowPreset=id;
    {auto path=interfacePath();auto stored=std::to_wstring(id);WritePrivateProfileStringW(L"Arrow",L"Preset",stored.c_str(),path.c_str());}
    syncArrowSurfaces();
    s.activateCommand(preset->sourceCommand);status="拖动绘制箭头；选中后拖动两端调整长度，中间点调整弧度。Alt 解除角度约束。";
}
bool App::drawArrowMenuItem(DRAWITEMSTRUCT* item){
    if(!item||item->CtlType!=ODT_MENU||item->itemData<900)return false;
    uint32_t data=uint32_t(item->itemData);bool category=data<904;
    auto preset=data>=1000?arrows::presetById(data-1000):nullptr;
    if(!category&&!preset)return false;
    const char* commands[]={"chooseArrowTool_Large_Solid","chooseArrowTool_Medium_Solid","chooseArrowTool_Small_Solid","chooseArrowTool_90_CW"};
    if(category){preset=arrows::presetByCommand(commands[data-900]);if(!preset&&data==903)for(size_t i=0;i<arrows::arrowPresetCount();++i)if(arrows::arrowPresets()[i].family==arrows::ArrowFamily::Arc){preset=&arrows::arrowPresets()[i];break;}}
    Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.TranslateTransform(float(item->rcItem.left),float(item->rcItem.top));g.ScaleTransform(scale,scale);
    float width=(item->rcItem.right-item->rcItem.left)/scale,height=(item->rcItem.bottom-item->rcItem.top)/scale;
    bool hot=(item->itemState&ODS_SELECTED)!=0,disabled=(item->itemState&ODS_DISABLED)!=0;
    SolidBrush background(hot?Color(255,225,239,232):white);g.FillRectangle(&background,0.f,0.f,width,height);
    Color color=disabled?muted:ink;
    if(preset)arrowPreview(g,preset->presetId,RectF(24,4,66,height-8),disabled?muted:teal);
    if(!category&&preset&&preset->presetId==arrowPreset){Pen pen(teal,1.7f);g.DrawLine(&pen,7.f,height*.5f,11.f,height*.5f+4);g.DrawLine(&pen,11.f,height*.5f+4,17.f,height*.5f-4);}
    const char* names[]={"大箭头","中箭头","小箭头","弧形箭头"};
    txt(g,category?names[data-900]:U(arrowLabel(*preset)),RectF(100,0,width-116,height),11,color);
    return true;
}
#endif
