from pathlib import Path
import shutil
backup=Path('work/ribbon-r3-backup');backup.mkdir(exist_ok=True)
for name in ['studio.cpp','ribbon_tabs.hpp','current_fixes_checks.hpp']:shutil.copy2(Path('studio')/name,backup/name)
p=Path('studio/ribbon_tabs.hpp');s=p.read_text(encoding='utf-8');s=s.replace('"线宽","","@graphicsWidth"','"线宽","bold","@graphicsWidth"').replace('"颜色","","@graphicsColor"','"颜色","color","@graphicsColor"')
s=s.replace('Group next;next.name="生成产物";next.items={step.items.back()};step.items.pop_back();','Group next;next.name="生成产物";next.items={step.items.back(),Button("mechanism.transfer","下一步 + 质子转移","arrow","@mechanismTransfer"),Button("mechanism.deprotonate","下一步 − 质子","arrow","@mechanismDeprotonate")};step.items.pop_back();');p.write_text(s,encoding='utf-8')
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8');s=s.replace('bool graphicsDense=false,','bool ribbonIconsOnly=false,graphicsDense=false,')
s=s.replace('float(nextCard?g.items.size():(g.items.size()+2)/3)*groupCell+10','float((g.items.size()+2)/3)*groupCell+10')
s=s.replace('nextCard?RectF(x+5+float(i)*groupCell,y+3,groupCell-2,88)','nextCard?RectF(x+5,y+3+float(i)*30,groupCell-2,28)')
s=s.replace('if(ribbonTab==1&&!ribbonCompact&&!graphicsAutoCompact){','if(ribbonTab==1&&!ribbonCompact){')
a=s.index('        compactMenus.clear();',s.index('    void layout()'));b=s.index('        if(ribbonTab==0&&(ribbonCompact||homeAutoCompact))',a)
s=s[:a]+'''        // Remove button captions before hiding tools or wrapping a second band.
        ribbonIconsOnly=ribbonTab!=0&&ribbonTab!=3&&!ribbonCompact&&(y>groupsTop||x>w-12.f);
        graphicsAutoCompact=ribbonTab==1&&ribbonIconsOnly;
        otherAutoCompact=ribbonTab>1&&ribbonTab!=3&&ribbonIconsOnly;
        compactMenus.clear();
        if(ribbonTab!=0&&(ribbonCompact||ribbonIconsOnly)){
            x=12;y=groupsTop;gh=ribbonCompact?48.f:112.f;
            auto& list=visibleGroups();size_t n=list.size(),rows=ribbonCompact?1:3;
            std::vector<size_t> cols(n,1);
            auto generation=[&](size_t i){return !list[i].items.empty()&&list[i].items.front().id=="mechanism.next";};
            auto groupWidth=[&](size_t i,size_t columns){if(generation(i)&&!ribbonCompact)return 158.f;bool more=columns*rows<list[i].items.size();return std::max(50.f,float(columns)*32+10+(more?24.f:0.f));};
            float used=0;for(size_t i=0;i<n;++i)used+=groupWidth(i,cols[i])+6;
            bool grew=true;while(grew){grew=false;for(size_t i=0;i<n;++i)if(!generation(i)&&cols[i]<(list[i].items.size()+rows-1)/rows){float delta=groupWidth(i,cols[i]+1)-groupWidth(i,cols[i]);if(used+delta<=w-24){++cols[i];used+=delta;grew=true;}}}
            for(size_t index=0;index<n;++index){auto& group=list[index];bool next=generation(index)&&!ribbonCompact;
                float gw=groupWidth(index,cols[index]);size_t count=next?group.items.size():std::min(group.items.size(),cols[index]*rows);
                group.r=RectF(x,y,gw,gh);for(auto& item:group.items)item.r=RectF(-10000,-10000,1,1);
                for(size_t i=0;i<count;++i)group.items[i].r=next?RectF(x+4,y+3+float(i)*30,gw-8,28):RectF(x+5+float(i/rows)*32,y+3+float(i%rows)*30,30,28);
                if(count<group.items.size())compactMenus.push_back({RectF(x+gw-24,y+4,22,24),index});x+=gw+6;
            }
        }
'''+s[b:]
# Paint the stacked commands in wide layouts as well.
s=s.replace('            if(b.iconOnly){icon(','''            if(ribbonTab==6&&gr.name=="生成产物"){
                roundbox(g,b.r,4,selected?Color(255,218,237,227):Color(255,239,247,243));
                icon(g,b.icon,RectF(b.r.X+5,b.r.Y+4,20,20),teal);
                txt(g,b.label,RectF(b.r.X+29,b.r.Y,b.r.Width-32,b.r.Height),10,c);continue;
            }
            if(b.iconOnly){icon(''')
a=s.index('            if(ribbonCompact||graphicsAutoCompact||homeAutoCompact||otherAutoCompact){if(gr.r.X<0)');b=s.index('            roundbox(g,gr.r,6,white);for(auto& b:gr.items){',a+20)
s=s[:a]+'''            if(ribbonCompact||ribbonIconsOnly||homeAutoCompact){
                if(gr.r.X<0)continue;roundbox(g,gr.r,6,white);
                for(auto& b:gr.items){if(b.r.X<0)continue;
                    bool active=b.cmd==s.entries[s.activeIndex].command;
                    if(active||hover==b.id)roundbox(g,b.r,4,Color(255,218,237,227));
                    Color c=b.enabled?(active?teal:ink):Color(255,181,188,184);
                    if(!ribbonCompact&&ribbonTab==6&&gr.name=="生成产物"){
                        roundbox(g,b.r,4,hover==b.id?Color(255,218,237,227):Color(255,239,247,243));
                        icon(g,b.icon,RectF(b.r.X+5,b.r.Y+4,20,20),teal);
                        txt(g,b.label,RectF(b.r.X+29,b.r.Y,b.r.Width-32,b.r.Height),10,c);continue;
                    }
                    RectF glyph(b.r.X+(b.r.Width-24)/2,b.r.Y+(b.r.Height-24)/2,24,24);
                    if(b.id=="graphics.arrowStyle")arrowPreview(g,arrowPreset,glyph,c);
                    else if(b.id=="graphics.color"){uint32_t rgba=s.graphicsColor;SolidBrush swatch(Color(BYTE(rgba),BYTE(rgba>>24),BYTE(rgba>>16),BYTE(rgba>>8)));g.FillRectangle(&swatch,glyph);Pen edge(ink,1);g.DrawRectangle(&edge,glyph);}
                    else icon(g,b.icon,glyph,c);
                }
                for(const auto& menu:compactMenus)if(gr.r.IntersectsWith(menu.first)){Pen more(teal,1.5f);float cx=menu.first.X+11,cy=menu.first.Y+11;g.DrawLine(&more,cx-3,cy-2,cx,cy+1);g.DrawLine(&more,cx,cy+1,cx+3,cy-2);}
                txt(g,gr.name,RectF(gr.r.X+3,gr.r.GetBottom()-19,gr.r.Width-6,16),9,muted,false,true);continue;
            }
'''+s[b:]
p.write_text(s,encoding='utf-8')
p=Path('studio/current_fixes_checks.hpp');s=p.read_text(encoding='utf-8');needle='    SetWindowPos(h,nullptr,0,0,originalWindow.right-originalWindow.left';i=s.index(needle,s.index('void App::currentFixesCheck'))
s=s[:i]+'''    // At smaller widths captions give way to complete grids of icon buttons.
    for(int logicalWidth:{640,900,1100,1500})for(int tab:{1,2,4,5,6}){
        SetWindowPos(h,nullptr,0,0,int(logicalWidth*scale),int(800*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ribbonTab=tab;ribbonCompact=false;layout();
        for(const auto& group:visibleGroups()){
            if(group.r.Y!=80.f||group.r.GetRight()>width/scale)throw std::runtime_error("icon ribbon wraps or clips");
            for(const auto& button:group.items)if(button.r.X>=0){if(button.r.GetRight()>group.r.GetRight()||button.r.GetBottom()>group.r.GetBottom()-19)throw std::runtime_error("icon button outside group");if(logicalWidth==900&&tab!=6&&ribbonIconsOnly&&(button.r.Width>32||button.r.Height>30))throw std::runtime_error("narrow button retains caption width");}
            if(logicalWidth==900)for(const auto& button:group.items)if(button.r.X<0)throw std::runtime_error("900px icon layout hides a tool");
            if(tab==6&&group.name=="生成产物"){if(group.items.size()!=3||group.r.Width>200)throw std::runtime_error("generation buttons not one column");for(size_t j=0;j<3;++j){auto r=group.items[j].r;if(r.X!=group.items[0].r.X||r.Height>30||(j&&r.Y<=group.items[j-1].r.Y))throw std::runtime_error("generation buttons not stacked rectangles");}}
        }
        if(logicalWidth==900||logicalWidth==1500)screenshot(dir+"/ribbon-tab-"+std::to_string(tab)+"-"+std::to_string(logicalWidth)+".png");
    }
    std::ofstream(dir+"/responsive-icons-checks.json")<<"{\\"widths\\":[640,900,1100,1500],\\"tabs\\":[1,2,4,5,6],\\"icon_grid\\":true,\\"stacked_generation_buttons\\":true}";
    ribbonCompact=savedCompact;
'''+s[i:];p.write_text(s,encoding='utf-8')
