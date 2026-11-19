from pathlib import Path
import shutil
backup=Path('work/ribbon-r4-backup');backup.mkdir(exist_ok=True)
for name in ['studio.cpp','app_actions.hpp','current_fixes_checks.hpp']:shutil.copy2(Path('studio')/name,backup/name)
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8')
# Smaller icons, tighter grid, and compact generation rectangles.
s=s.replace('if(roleColumn)gw-=groupCell-36.f;','if(nextCard){groupCell=130.f;gw=140.f;}if(roleColumn)gw-=groupCell-36.f;')
s=s.replace('nextCard?RectF(x+5,y+3+float(i)*30,groupCell-2,28)','nextCard?RectF(x+5,y+3+float(i)*24,groupCell-2,22)')
s=s.replace('gh=ribbonCompact?48.f:112.f;','gh=ribbonCompact?44.f:92.f;')
s=s.replace('return 158.f;','return 134.f;').replace('float(columns)*32+10+(more?24.f:0.f)','float(columns)*26+8+(more?20.f:0.f)')
s=s.replace('RectF(x+4,y+3+float(i)*30,gw-8,28):RectF(x+5+float(i/rows)*32,y+3+float(i%rows)*30,30,28)','RectF(x+4,y+3+float(i)*24,gw-8,22):RectF(x+4+float(i/rows)*26,y+3+float(i%rows)*24,24,22)')
s=s.replace('RectF(b.r.X+(b.r.Width-24)/2,b.r.Y+(b.r.Height-24)/2,24,24);','RectF(b.r.X+(b.r.Width-18)/2,b.r.Y+(b.r.Height-18)/2,18,18);')
s=s.replace('RectF(b.r.X+5,b.r.Y+4,20,20)','RectF(b.r.X+4,b.r.Y+3,16,16)')
s=s.replace('RectF(b.r.X+29,b.r.Y,b.r.Width-32,b.r.Height),10,c)','RectF(b.r.X+23,b.r.Y,b.r.Width-26,b.r.Height),9,c)')
# Migrate retired fixed-angle choices, including the old persisted 270 preset.
s=s.replace('if(!arrow){arrowPreset=0;arrow=arrows::presetById(0);}syncArrowSurfaces();','if(!arrow){arrowPreset=0;arrow=arrows::presetById(0);}if(arrow&&arrow->family==arrows::ArrowFamily::Arc&&arrowPreset!=58){arrowPreset=58;WritePrivateProfileStringW(L"Arrow",L"Preset",L"58",path.c_str());}syncArrowSurfaces();')
s=s.replace('if(activeArrow){arrowPreset=activeArrow->presetId;syncArrowSurfaces();}','if(activeArrow){arrowPreset=activeArrow->family==arrows::ArrowFamily::Arc?58:activeArrow->presetId;syncArrowSurfaces();}')
# Native tooltip: increase the actual platform font by 2 points and refresh on DPI changes.
s=s.replace('HWND toolTip=nullptr;std::wstring toolTipText;','HWND toolTip=nullptr;HFONT toolTipFont=nullptr;UINT toolTipFontDpi=0;int toolTipBaseHeight=0;std::wstring toolTipText;')
needle='        TOOLINFOW info={};info.cbSize=sizeof(info);info.hwnd=h;info.uId=1;info.lpszText=(LPWSTR)toolTipText.c_str();SendMessageW(toolTip,TTM_UPDATETIPTEXTW'
i=s.index(needle)
s=s[:i]+'''        UINT dpi=GetDpiForWindow(h);if(!dpi)dpi=96;
        if(!toolTipFont||toolTipFontDpi!=dpi){
            NONCLIENTMETRICSW metrics={};metrics.cbSize=sizeof(metrics);SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi);
            LOGFONTW font=metrics.lfStatusFont;
            if(!toolTipFont){auto native=(HFONT)SendMessageW(toolTip,WM_GETFONT,0,0);if(native)GetObjectW(native,sizeof(font),&font);}
            toolTipBaseHeight=std::abs(font.lfHeight);font.lfHeight=-(toolTipBaseHeight+MulDiv(2,dpi,72));font.lfQuality=CLEARTYPE_QUALITY;
            auto enlarged=CreateFontIndirectW(&font);if(enlarged){SendMessageW(toolTip,WM_SETFONT,(WPARAM)enlarged,TRUE);if(toolTipFont)DeleteObject(toolTipFont);toolTipFont=enlarged;toolTipFontDpi=dpi;}
        }
        SendMessageW(toolTip,TTM_SETMAXTIPWIDTH,0,MulDiv(520,dpi,96));
'''+s[i:]
s=s.replace('a->toolTip=nullptr;}a->shutdownRenderer();','a->toolTip=nullptr;}if(a->toolTipFont){DeleteObject(a->toolTipFont);a->toolTipFont=nullptr;}a->shutdownRenderer();')
p.write_text(s,encoding='utf-8')
p=Path('studio/app_actions.hpp');s=p.read_text(encoding='utf-8');s=s.replace('if(preset.presetId==58)return L"弧线箭头";','if(preset.family==arrows::ArrowFamily::Arc)return L"弧线箭头";')
s=s.replace('    auto preset=arrows::presetById(id);if(!preset||!preset->implemented)return;\n    std::vector<ObjectId> selected;', '    auto preset=arrows::presetById(id);if(!preset||!preset->implemented)return;\n    if(preset->family==arrows::ArrowFamily::Arc&&id!=58){id=58;preset=arrows::presetById(id);}\n    std::vector<ObjectId> selected;')
s=s.replace('    {auto path=interfacePath();auto stored=std::to_wstring(id);WritePrivateProfileStringW(L"Arrow",L"Preset",stored.c_str(),path.c_str());}','    if(!testing){auto path=interfacePath();auto stored=std::to_wstring(id);WritePrivateProfileStringW(L"Arrow",L"Preset",stored.c_str(),path.c_str());}')
p.write_text(s,encoding='utf-8')
# Extend the existing layout regression and verify native tooltip font, rather than source text.
p=Path('studio/current_fixes_checks.hpp');s=p.read_text(encoding='utf-8');s=s.replace('if(group.items.size()!=3||group.r.Width>200)','if(group.items.size()!=3||group.r.Width>142)').replace('r.Height>30||(j&&','r.Height>22||(j&&')
s=s.replace('if(logicalWidth==900&&tab!=6&&ribbonIconsOnly&&(button.r.Width>32||button.r.Height>30))','if(logicalWidth==900&&tab!=6&&ribbonIconsOnly&&(button.r.Width>24||button.r.Height>22))')
i=s.index('    ribbonCompact=savedCompact;\n    SetWindowPos',s.index('responsive-icons-checks.json'))
s=s[:i]+'''    auto originalArrow=arrowPreset;chooseArrowPreset(25);
    if(arrowPreset!=58||arrowPresetName().find("270")!=std::string::npos||arrowCommand()!=arrows::presetById(58)->sourceCommand)throw std::runtime_error("retired fixed-angle preset returns to toolbar");
    chooseArrowPreset(originalArrow);ribbonTab=6;layout();
    for(const auto& group:mechanismGroups)for(const auto& button:group.items)if(button.id=="mechanism.positive"){
        hover=button.id;showToolTip();LOGFONTW font={};auto handle=(HFONT)SendMessageW(toolTip,WM_GETFONT,0,0);
        if(!handle||!GetObjectW(handle,sizeof(font),&font)||std::abs(font.lfHeight)!=toolTipBaseHeight+MulDiv(2,toolTipFontDpi,72))throw std::runtime_error("tooltip did not grow two points");
        hideToolTip();hover.clear();
    }
'''+s[i:];p.write_text(s,encoding='utf-8')
