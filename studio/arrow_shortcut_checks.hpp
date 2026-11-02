#pragma once
// T5: the arrow tool shortcut (default E) must activate the arrow preset the
// user picked in the Graphics tab, not a hard-coded "Large Solid". The checks
// below drive the same path a key press takes - App::shortcut (and one real
// WM_KEYDOWN on the canvas) -> editorHotkey -> execute - and then draw an arrow
// with the tool that path selected, so the document proves the preset.
void App::arrowShortcutCheck(const std::string& dir){
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const bool oldCompact=ribbonCompact,oldVisible=ribbonVisible;const int oldTab=ribbonTab;
    ribbonCompact=false;ribbonVisible=true;scale=1;SetWindowPos(h,0,0,0,1400,900,SWP_NOMOVE|SWP_NOZORDER);layout();
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
    auto homeCommand=[&](){std::string command;for(const auto& group:groups)for(const auto& b:group.items)if(b.id=="chooseArrowTool_Large_Solid")command=b.cmd;return command;};
    auto graphicsCard=[&]()->const Button*{for(const auto& group:graphicsGroups)for(const auto& b:group.items)if(b.id=="graphics.arrowStyle")return &b;return nullptr;};
    // Start from a non-arrow tool so only the shortcut can select the preset.
    auto fire=[&](){s.activateCommand("chooseSelectionTool_Marquee");invalidate(false);require(shortcut('E',0),"the arrow shortcut is bound");return s.entries[s.activeIndex].command;};
    int presses=0;
    for(size_t i=0;i<arrows::arrowPresetCount();++i){
        auto& preset=arrows::arrowPresets()[i];if(!preset.implemented)continue;
        chooseArrowPreset(preset.presetId);require(arrowPreset==preset.presetId,"menu stores the chosen preset");
        require(arrowCommand()==preset.sourceCommand,"one source of truth for the arrow command");
        require(homeCommand()==preset.sourceCommand,"home arrow icon shows the chosen preset");
        require(fire()==preset.sourceCommand,"arrow shortcut activates the chosen preset");
        ++presses;
    }
    require(presses>10,"every implemented arrow preset is covered");
    // A real key press on the canvas must reach the same code path.
    {auto preset=arrows::presetByCommand("chooseArrowTool_Large_Equilibrium");require(preset&&preset->implemented,"equilibrium preset");
     chooseArrowPreset(preset->presetId);s.activateCommand("chooseSelectionTool_Marquee");invalidate(false);
     SendMessageW(canvas,WM_KEYDOWN,'E',0);
     require(s.entries[s.activeIndex].command==preset->sourceCommand,"WM_KEYDOWN E activates the chosen preset");}
    
    // Draw with the shortcut-selected tool: solid, dashed/equilibrium and arc.
    const char* fixtures[]={"chooseArrowTool_Large_Solid","chooseArrowTool_Large_Equilibrium","chooseArrowTool_270_CW"};
    for(int f=0;f<3;++f)for(int repeat=0;repeat<2;++repeat){
        auto preset=arrows::presetByCommand(fixtures[f]);require(preset&&preset->implemented,"fixture preset is implemented");
        chooseArrowPreset(preset->presetId);require(fire()==preset->sourceCommand,"shortcut selects the fixture preset");
        size_t before=s.doc.arrows().size();double y=double(f)*90.+double(repeat)*30.;
        s.gesture(PointerPhase::Down,Vec2(-240,y));s.gesture(PointerPhase::Up,Vec2(-60,y));
        require(s.doc.arrows().size()==before+1,"shortcut-selected tool draws an arrow");
        arrows::ArrowParams params;bool ok=false;
        require(arrows::readArrow(s.doc,ObjectId(s.doc.arrows().size()-1),&params,&ok).ok()&&ok,"drawn arrow is readable");
        require(params.presetId==preset->presetId,"drawn arrow keeps the preset the shortcut selected");
    }
    
    // The Graphics tab selector and its hit test must agree with the preset.
    auto fixture=arrows::presetByCommand("chooseArrowTool_270_CW");chooseArrowPreset(fixture->presetId);
    SetWindowPos(h,0,0,0,1100,900,SWP_NOMOVE|SWP_NOZORDER);scale=1;
    switchTab(1);
    layout();
    invalidate(false);
    auto card=graphicsCard();require(card!=nullptr,"graphics tab exposes one arrow selector");
    require(card->arg==int(arrowPreset),"graphics selector mirrors the selected preset");
    require(arrowButtonAt(card->r.X+card->r.Width*.5f,card->r.Y+card->r.Height*.5f),"graphics selector hits the arrow button test");
    screenshot(dir+"/arrow-shortcut-graphics.png");
    switchTab(0);layout();invalidate(false);
    {RectF row;for(const auto& group:groups)for(const auto& b:group.items)if(b.id=="chooseArrowTool_Large_Solid")row=b.r;
     require(homeCommand()==fixture->sourceCommand&&arrowButtonAt(row.X+row.Width*.5f,row.Y+row.Height*.5f),"home arrow button agrees with the preset");}
    
    // Restart-equivalent: interface.ini [Arrow] Preset is re-read at startup.
    auto path=interfacePath();wchar_t previous[64]={};GetPrivateProfileStringW(L"Arrow",L"Preset",L"",previous,64,path.c_str());
    auto persisted=arrows::presetByCommand("chooseArrowTool_Large_Equilibrium");
    WritePrivateProfileStringW(L"Arrow",L"Preset",std::to_wstring(persisted->presetId).c_str(),path.c_str());
    arrowPreset=0;loadRibbon();require(arrowPreset==persisted->presetId,"interface.ini restores the arrow preset");
    require(fire()==persisted->sourceCommand,"shortcut after reload uses the persisted preset");
    
    {App fresh;require(fresh.arrowPreset==persisted->presetId,"fresh App loads the persisted preset");
     require(fresh.shortcut('E',0)&&fresh.s.entries[fresh.s.activeIndex].command==persisted->sourceCommand,"fresh App shortcut uses the persisted preset");}
    if(previous[0])WritePrivateProfileStringW(L"Arrow",L"Preset",previous,path.c_str());else WritePrivateProfileStringW(L"Arrow",L"Preset",nullptr,path.c_str());
    loadRibbon();
    std::ofstream(dir+"/arrow-shortcut-checks.json")<<"{\"presets\":"<<presses<<",\"shortcut_uses_selected_preset\":true,\"wm_keydown_path\":true,\"drawn_arrow_preset\":true,\"graphics_selector_matches\":true,\"home_button_matches\":true,\"restart_restores_preset\":true}";
    ribbonCompact=oldCompact;ribbonVisible=oldVisible;switchTab(oldTab);layout();
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}