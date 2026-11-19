from pathlib import Path
r=Path(__file__).resolve().parents[1]
def change(file,old,new,count=1):
 p=r/file;s=p.read_text(encoding='utf-8');assert s.count(old)==count,(file,old[:65],s.count(old));p.write_text(s.replace(old,new),encoding='utf-8',newline='\r\n')
change('studio/calculator_dialog.hpp','MassCalculatorDialog mass;HFONT font=0,bold=0;', 'HWND massPreview=0;MassCalculatorDialog mass;HFONT font=0,bold=0,keyFont=0;')
p=r/'studio/calculator_dialog.hpp';s=p.read_text(encoding='utf-8')
start=s.index('        referenceIndex=selection(reference)-1;');end=s.index('        bool any=false;',start)
s=s[:start]+'''        haveInitial=yieldcalc::number(text(start),initial,true);initial*=startFactor();
        haveInitial=yieldcalc::number(text(startCoefficient),initialCoefficient,true)&&haveInitial;
'''+s[end:]
start=s.index('        control(yieldPage,L"STATIC",L"起始物",');end=s.index('        WNDCLASSW cls=',start)
s=s[:start]+'''        double remembered=0,rememberedCoefficient=1;for(const auto& row:rows)if(row.initialMol>0){remembered=row.initialMol;rememberedCoefficient=row.initialCoefficient;break;}
        control(yieldPage,L"STATIC",L"起始物物质的量",0,8,8,112,24);start=edit(yieldPage,602,124,4,104,remembered>0?yieldcalc::compact(remembered*1000):L"");
        startUnit=combo(yieldPage,603,234,4,78,{L"mmol",L"mol",L"µmol"},0);
        control(yieldPage,L"STATIC",L"计量系数",0,330,8,68,24);startCoefficient=edit(yieldPage,604,402,4,54,yieldcalc::compact(rememberedCoefficient));
        status=control(yieldPage,L"STATIC",L"",605,8,42,728,24);
'''+s[end:]
s=s.replace('haveInitial?L"产率按所选起始物及计量系数计算":L"可填写起始物用量，或选用某个化合物的用量"','haveInitial?L"产率按填写的起始物用量及计量系数计算":L"填写起始物用量后显示产率"')
s=s.replace('px(8),px(108),px(730),px(392)','px(8),px(72),px(730),px(428)')
s=s.replace('ShowWindow(massPage,activeTab?SW_SHOW:SW_HIDE);\n    }','ShowWindow(massPage,activeTab?SW_SHOW:SW_HIDE);if(massPreview)InvalidateRect(massPreview,nullptr,TRUE);\n    }')
s=s.replace('for(auto h:keyButtons)EnableWindow(h,!activeTab);if(!activeTab&&start)SetFocus(referenceIndex>=0&&!fields.empty()?fields.front().mass:start);','for(auto h:keyButtons)ShowWindow(h,activeTab?SW_HIDE:SW_SHOW);for(int id:{800,801})ShowWindow(GetDlgItem(hwnd,id),activeTab?SW_HIDE:SW_SHOW);ShowWindow(massPreview,activeTab?SW_SHOW:SW_HIDE);ShowWindow(GetDlgItem(hwnd,802),activeTab?SW_SHOW:SW_HIDE);if(!activeTab&&start)SetFocus(start);')
s=s.replace('        if(id==601&&notification==CBN_SELCHANGE){refreshResults();return;}\n','')
s=s.replace('if(selection(reference)==0){double value=0;', '{double value=0;')
s=s.replace('i?L"质谱计算器":L"产率计算器"','i?L"质谱计算器":L"产率计算器"')
s=s.replace('d->control(h,L"STATIC",L"点击输入框后使用\\n支持小数和科学计数法",801,790,332,218,60);','d->control(h,L"STATIC",L"点击输入框后使用\\n支持小数和科学计数法",801,790,456,218,60);\n            d->control(h,L"STATIC",L"所选结构",802,790,68,218,28);\n            d->massPreview=d->control(h,L"STATIC",L"",803,780,108,236,288,SS_OWNERDRAW);ShowWindow(d->massPreview,SW_HIDE);ShowWindow(GetDlgItem(h,802),SW_HIDE);')
s=s.replace('for(int i=0;i<16;++i)d->keyButtons.push_back(d->control(h,L"BUTTON",keys[i],9000+i,784+(i%4)*55,108+(i/4)*52,51,46,WS_TABSTOP));','d->keyFont=CreateFontW(-d->px(23),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");\n            for(int i=0;i<16;++i){auto key=d->control(h,L"BUTTON",keys[i],9000+i,780+(i%4)*60,108+(i/4)*84,56,78,WS_TABSTOP);if(i!=7&&i!=15)SendMessageW(key,WM_SETFONT,(WPARAM)d->keyFont,TRUE);d->keyButtons.push_back(key);}')
s=s.replace('        if(m==WM_CLOSE){EndDialog(h,0);return TRUE;}','''        if(m==WM_DRAWITEM&&((DRAWITEMSTRUCT*)l)->hwndItem==d->massPreview){auto draw=(DRAWITEMSTRUCT*)l;Graphics g(draw->hDC);g.Clear(white);int index=d->selection(d->massChoice);if(index>=0&&size_t(index)<d->fields.size()&&d->fields[size_t(index)].thumbnail){auto rect=draw->rcItem;d->fields[size_t(index)].thumbnail->paint(g,RectF(8,8,float(rect.right-rect.left-16),float(rect.bottom-rect.top-16)));}return TRUE;}
        if(m==WM_CLOSE){EndDialog(h,0);return TRUE;}''')
s=s.replace('if(d->font)DeleteObject(d->font);return TRUE;', 'if(d->font)DeleteObject(d->font);if(d->keyFont)DeleteObject(d->keyFont);return TRUE;')
p.write_text(s,encoding='utf-8',newline='\r\n')
change('studio/tlc_dialog.hpp','d->diameterSelectedOnly=id==231;CheckDlgButton(h,230,id==230?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(h,231,id==231?BST_CHECKED:BST_UNCHECKED);','d->diameterSelectedOnly=id==231?IsDlgButtonChecked(h,231)==BST_CHECKED:IsDlgButtonChecked(h,230)!=BST_CHECKED;CheckDlgButton(h,230,d->diameterSelectedOnly?BST_UNCHECKED:BST_CHECKED);CheckDlgButton(h,231,d->diameterSelectedOnly?BST_CHECKED:BST_UNCHECKED);')
# Include all native plate corners when translating the exported copy onto paper.
change('studio/interop.hpp','double minX=1e30,minY=1e30,maxX=-1e30,maxY=-1e30;','''double minX=1e30,minY=1e30,maxX=-1e30,maxY=-1e30;bool offCanvasTlcX=false,offCanvasTlcY=false;
        std::function<void(tinyxml2::XMLElement*)> inspectTlc=[&](tinyxml2::XMLElement* node){for(auto e=node->FirstChildElement();e;e=e->NextSiblingElement()){if(std::string(e->Name())=="tlcplate")for(auto key:{"TopLeft","TopRight","BottomLeft","BottomRight"})if(auto value=e->Attribute(key)){double x=0,y=0;std::istringstream in(value);if(in>>x>>y){offCanvasTlcX|=x<ox;offCanvasTlcY|=y<oy;}}inspectTlc(e);}};inspectTlc(pg);''')
change('studio/interop.hpp','"Center3D","MajorAxisEnd3D","MinorAxisEnd3D"','"Center3D","MajorAxisEnd3D","MinorAxisEnd3D","TopLeft","TopRight","BottomLeft","BottomRight"')
change('studio/interop.hpp','if(p.x<0&&minX<1e29','if((p.x<0||offCanvasTlcX)&&minX<1e29')
change('studio/interop.hpp','if(p.y<0&&minY<1e29','if((p.y<0||offCanvasTlcY)&&minY<1e29')
# Native TLC writes spots by lane. Compare metadata in that same order before
# restoring IDs, custom colours and tail opacity after a save/reopen.
p=r/'studio/engine/cdxml_reader.cpp';s=p.read_text(encoding='utf-8');start=s.index('                        if(unchanged)for(size_t i=0;i<figure.spots.size();++i){');end=s.index('                        if(unchanged)figure=original;',start)
s=s[:start]+'''                        if(unchanged)for(size_t laneIndex=0;laneIndex<figure.lanes.size()&&unchanged;++laneIndex){
                            std::vector<const experimental::Spot*> saved,native;
                            for(const auto& spot:original.spots)if(spot.laneId==original.lanes[laneIndex].id)saved.push_back(&spot);
                            for(const auto& spot:figure.spots)if(spot.laneId==figure.lanes[laneIndex].id)native.push_back(&spot);
                            if(saved.size()!=native.size()){unchanged=false;break;}
                            for(size_t i=0;i<saved.size();++i){const auto& a=*saved[i];const auto& b=*native[i];if(std::abs(a.v-b.v)>1e-5||std::abs(a.radiusU-b.radiusU)>1e-5||std::abs(a.radiusV-b.radiusV)>1e-5||std::abs(a.tailLengthV-b.tailLengthV)>1e-5){unchanged=false;break;}}
                        }
'''+s[end:];p.write_text(s,encoding='utf-8',newline='\r\n')
print('calculator layout, TLC toggles, TLC coordinate export and metadata order updated')
