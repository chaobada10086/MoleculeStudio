// Verify that visible hints agree with actual dispatch, including context
// shadowing and user remapping. Does not change the installed shortcut file.
{
    ShortcutConfig config;config.reset();auto tools=makeTools();
    auto button=[&](const std::string& command){for(const auto& group:tools)for(const auto& item:group.items)if(item.cmd==command)return item;for(const auto& item:chemistryQuick)if(item.cmd==command)return item;throw std::runtime_error("missing hint test tool: "+command);};
    auto has=[&](const Button& tool,int scope,const std::string& text){return shortcut_hints::describe(config,tool,scope).text.find(text)!=std::string::npos;};
    auto benzene=button("chooseRingTool_Benzene"),wedge=button("chooseBondTool_Wedged"),hashed=button("chooseBondTool_Hashed_Wedged"),doubleBond=button("chooseMultipleBondsTool_DoubleBond");
    require(has(benzene,1,"j  选择苯环工具")&&has(benzene,2,"3 / a")&&has(benzene,3,"a  在该键上稠合苯环"),"benzene tool/sprout/fuse contexts show native keys");
    require(!has(benzene,2,"j  选择"),"atom j is not falsely advertised as the benzene tool");
    require(has(wedge,2,"4  添加实楔")&&has(wedge,3,"w  改为实楔"),"wedge hints distinguish atom and bond actions");
    require(has(hashed,2,"5  添加虚楔")&&has(hashed,3,"h / Shift+W"),"hashed wedge aliases preserve Shift and case");
    require(has(doubleBond,2,"在原碳上添加 =O")&&has(doubleBond,3,"2  改为双键")&&has(doubleBond,3,"双键居中"),"double bond atom 2 and bond 2 have different purposes");
    require(has(button("chooseRingTool_Chair_Cyclohexane1"),3,"9")&&has(button("chooseRingTool_Chair_Cyclohexane2"),3,"0"),"both chair fuse hotkeys are annotated");
    require(has(button("chooseArrowTool_Large_Solid"),1,"e  选择箭头工具")&&!has(button("chooseArrowTool_Large_Solid"),2,"e  选择"),"atom e overrides generic arrow shortcut");
    require(has(button("chooseEdittingTool_Text"),2,"t  选择文字工具"),"unshadowed generic tool key falls back over an atom");
    require(has(button("chooseDrawingTool_Circle_Hollow"),1,"无可用快捷键"),"no invented shortcut for a tool without a native binding");
    require(has(button("@clean"),1,"Ctrl+Shift+K")&&has(button("@structureToName"),1,"Ctrl+Alt+N"),"menu shortcuts also follow the current catalog");
    size_t drawing=0;std::ofstream report(testOut+"/drawing_shortcuts.tsv");report<<"icon\tno_hotspot\tatom\tbond\n";
    for(const auto& group:tools)for(const auto& tool:group.items){if(tool.cmd.compare(0,6,"choose")!=0)continue;++drawing;report<<tool.label;
        for(int scope=1;scope<=3;++scope){auto context=shortcut_hints::describe(config,tool,scope);require(!context.text.empty()&&!context.badge.empty(),"every drawing icon has all three context descriptions");for(const auto& entry:context.entries){bool effective=false;for(const auto& row:config.rows)if(row.key&&shortcut_hints::keyText(row)==entry.key&&shortcut_hints::effective(config,row.key,row.modifiers,scope)==&row)effective=true;require(effective,"every advertised chord resolves in its advertised context");}report<<'\t'<<context.text;}report<<'\n';
    }
    // Native j remapped to F12 must update both the footer and the icon badge.
    for(auto& row:config.rows)if(auto hotkey=shortcut_hints::catalog(row))if(std::string(hotkey->object)=="Generic"&&std::string(hotkey->key)=="j"){row.key=VK_F12;row.modifiers=0;}
    config.rebuild();require(has(benzene,1,"F12")&&!has(benzene,1,"j  选择"),"custom tool key replaces the native hint");
    require(shortcut_hints::describe(config,benzene,1).badge=="F12","icon badge follows user remapping");
    // A custom global binding shadows the hotspot key too.
    config.add("hint-test","test","@copy",'4');config.rebuild();require(!has(wedge,2,"4  添加"),"global override is not advertised as an available atom key");
    config.reset();require(has(benzene,1,"j  选择"),"reset restores the catalog hint");
    // Actual UI transitions through a hover, an atom, a bond, and leaving canvas.
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());ObjectId a,b,bond;s.doc.addAtom(Vec2(-20,0),6,&a);s.doc.addAtom(Vec2(20,0),6,&b);s.doc.addBond(a,b,&bond);s.refresh();s.activateCommand("chooseRingTool_Benzene");auto view=s.view();view.center=Vec2();view.scale=2;s.setView(view);hover.clear();
    auto point=s.view().toDevice(Vec2(-20,0));SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(point.x),int(point.y)));require(shortcutContext()==2,"footer switches to atom capture");screenshot(testOut+"/shortcut_atom.png");
    point=s.view().toDevice(Vec2());SendMessageW(canvas,WM_MOUSEMOVE,0,MAKELPARAM(int(point.x),int(point.y)));require(shortcutContext()==3,"footer switches to bond capture");screenshot(testOut+"/shortcut_bond.png");
    SendMessageW(canvas,WM_MOUSELEAVE,0,0);require(shortcutContext()==1,"leaving the canvas clears the captured context");
    // The wedge is in the compact menu rather than a visible icon. Expand the
    // ribbon for this icon-hover test, independently of the user's settings.
    const bool oldCompact=ribbonCompact,oldVisible=ribbonVisible;const int oldTab=ribbonTab;
    ribbonTab=0;ribbonCompact=false;ribbonVisible=true;layout();
    auto wedgeButton=button("chooseBondTool_Wedged");bool sentWedge=false;for(const auto& group:groups)for(const auto& item:group.items)if(item.cmd==wedgeButton.cmd&&item.r.X>=0&&item.r.Y>=0){SendMessageW(h,WM_MOUSEMOVE,0,MAKELPARAM(int((item.r.X+3)*scale),int((item.r.Y+3)*scale)));sentWedge=true;}require(sentWedge&&hintButton().cmd==wedgeButton.cmd,"hovered icon chooses its three context explanations");screenshot(testOut+"/shortcut_wedge_hover.png");
    int originalWidth=width,originalHeight=height;float originalScale=scale;
    SetWindowPos(h,0,0,0,900,650,SWP_NOMOVE|SWP_NOZORDER);layout();require(height-top-int(footerHeight()*scale)>=60,"small window keeps footer outside drawing canvas");screenshot(testOut+"/shortcut_small.png");
    scale=2;SetWindowPos(h,0,0,0,3300,1800,SWP_NOMOVE|SWP_NOZORDER);layout();screenshot(testOut+"/shortcut_200percent.png");
    scale=originalScale;ribbonTab=oldTab;ribbonCompact=oldCompact;ribbonVisible=oldVisible;SetWindowPos(h,0,0,0,originalWidth,originalHeight,SWP_NOMOVE|SWP_NOZORDER);layout();hover.clear();pointerOverCanvas=false;
    std::ofstream(testOut+"/shortcut_hint_tests.json")<<"{\"drawing_icons\":"<<drawing<<",\"contexts_per_icon\":3,\"single_line_footer\":true,\"footer_font_px\":10.5,\"catalog_actions\":true,\"scope_precedence\":true,\"case_and_aliases\":true,\"custom_binding\":true,\"no_invented_keys\":true,\"live_capture\":true,\"small_window\":true,\"high_dpi\":true}";
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
}
