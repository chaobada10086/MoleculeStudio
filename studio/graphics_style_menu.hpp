#pragma once
static const double graphicWidths[]={0,.25,.5,.6,.75,1,1.5,2,3,4};
static const uint32_t graphicColors[]={0x000000FFu,0xDC1E1EFFu,0x0046DCFFu,0x008C46FFu,0xB400B4FFu,0xE68200FFu,0x6E6E6EFFu,0xFFFFFFFFu};
static const char* graphicColorNames[]={"黑色","红色","蓝色","绿色","紫色","橙色","灰色","白色"};

std::string graphicWidthText(double width){if(width<=0)return "自动（按样式）";std::ostringstream out;out<<width<<" 磅";return out.str();}
std::string graphicColorText(uint32_t color){for(int i=0;i<8;++i)if(color==graphicColors[i])return graphicColorNames[i];char hex[16];std::snprintf(hex,sizeof(hex),"#%06X",unsigned(color>>8));return hex;}
void App::paintGraphicFormatControl(Graphics& g,const Button& button){
    auto r=button.r;bool width=button.id=="graphics.width";
    if(hover==button.id)roundbox(g,r,4,Color(255,237,244,240));
    txt(g,button.label,RectF(r.X+3,r.Y+1,r.Width-6,19),11,muted,false,true);
    RectF value(r.X+3,r.Y+24,r.Width-6,28);roundbox(g,value,3,white);Pen border(line,1);g.DrawRectangle(&border,value);
    Pen chevron(teal,1.3f);float cx=value.GetRight()-9,cy=value.Y+12;g.DrawLine(&chevron,cx-3,cy,cx,cy+3);g.DrawLine(&chevron,cx,cy+3,cx+3,cy);
    uint32_t rgba=s.graphicsColor;Color current(BYTE(rgba),BYTE(rgba>>24),BYTE(rgba>>16),BYTE(rgba>>8));
    if(width){
        double amount=s.graphicsWidth;if(amount<=0){amount=s.doc.defaultLineWidth();if(auto preset=arrows::presetByCommand(s.entries[s.activeIndex].command)){auto params=arrows::resolvePreset(preset->presetId,Vec2(),Vec2(72,0),amount);amount=arrows::arrowStrokeWidth(params,amount);}}
        txt(g,graphicWidthText(amount),RectF(value.X+5,value.Y,value.Width-20,value.Height),12,ink,false,true);
        Pen sample(current,float(std::max(.7,std::min(7.,amount*2))));g.DrawLine(&sample,r.X+16,r.Y+68,r.GetRight()-16,r.Y+68);
        if(s.graphicsWidth<=0)txt(g,"自动",RectF(r.X+3,r.Y+76,r.Width-6,12),8.5f,muted,false,true);
    }else{
        SolidBrush swatch(current);g.FillRectangle(&swatch,value.X+7,value.Y+8,14.f,12.f);g.DrawRectangle(&border,value.X+7,value.Y+8,14.f,12.f);
        txt(g,graphicColorText(rgba),RectF(value.X+26,value.Y,value.Width-40,value.Height),11,ink,false,true);
        RectF bar(r.X+16,r.Y+66,r.Width-32,4);g.FillRectangle(&swatch,bar);
    }
}

Error App::applyTextColor(uint32_t color){
    if(textEditor.active){
        CHARFORMAT2W f={};f.cbSize=sizeof(f);f.dwMask=CFM_COLOR;
        f.crTextColor=RGB((color>>24)&255,(color>>16)&255,(color>>8)&255);
        studio_i18n::sendMessage(edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&f);
        textEditor.format.colorRGBA=color;textEditor.resize();SetFocus(edit);invalidate(false);return Error();
    }
    std::vector<ObjectRef> refs;for(auto ref:studioio::expanded(s.doc,s.selected))if(graphic_style::supported(ref.kind))refs.push_back(ref);
    if(refs.empty()){s.graphicsColor=color;invalidate();return Error();}
    auto e=s.submit(Command("对象颜色",kInvalidateAppearance,[refs,color](CommandContext& c){
        for(auto ref:refs){auto e=graphic_style::apply(c.document,ref,0,color,false,true);if(!e.ok())return CommandResult::fail(e.code,e.message);
            auto& bag=c.document.extensions(ref);bag.set("studio.color",std::to_string(color));bag.erase("studio.text.colors");bag.erase("studio.text.colors.edited");
            if(ref.kind==ObjectKind::Text){auto fmt=c.document.texts().get(ref.id)->format;fmt.colorRGBA=color;e=c.document.setTextFormat(ref.id,fmt);if(!e.ok())return CommandResult::fail(e.code,e.message);}
        }return CommandResult::success();}),"对象颜色");invalidate();return e;
}

Error App::applyGraphicsStyle(double width,uint32_t color,bool setWidth,bool setColor){
    if(setWidth&&(!std::isfinite(width)||width<0||width>12))return Error(ErrorCode::InvalidArgument,"线宽须在 0 到 12 磅之间");
    std::vector<ObjectRef> refs;for(auto ref:studioio::expanded(s.doc,s.selected))if((setColor&&graphic_style::supported(ref.kind))||(setWidth&&graphic_style::supportsWidth(ref.kind)))refs.push_back(ref);
    if(!refs.empty()){
        auto result=s.submit(Command("线宽与颜色",kInvalidateGeometry|kInvalidateAppearance,[refs,width,color,setWidth,setColor](CommandContext& context){for(auto ref:refs){auto e=graphic_style::apply(context.document,ref,width,color,setWidth,setColor);if(!e.ok())return CommandResult::fail(e.code,e.message);}return CommandResult::success();}),"线宽与颜色");
        if(!result.ok())return result;
    }
    if(setWidth)s.graphicsWidth=width;if(setColor)s.graphicsColor=color;
    invalidate();return Error();
}
void App::graphicsStyleMenu(bool width){
    HMENU menu=CreatePopupMenu();
    HMENU widthMenu=width?menu:nullptr,colorMenu=width?nullptr:menu;
    if(width)for(int i=0;i<10;++i)studio_i18n::appendMenu(widthMenu,MF_OWNERDRAW,2000+i,(LPCWSTR)UINT_PTR(2000+i));
    if(width){studio_i18n::appendMenu(widthMenu,MF_SEPARATOR,0,nullptr);studio_i18n::appendMenu(widthMenu,MF_STRING,2100,L"自定义线宽…");}
    if(!width)for(int i=0;i<8;++i)studio_i18n::appendMenu(colorMenu,MF_OWNERDRAW,3000+i,(LPCWSTR)UINT_PTR(3000+i));
    if(!width){studio_i18n::appendMenu(colorMenu,MF_SEPARATOR,0,nullptr);studio_i18n::appendMenu(colorMenu,MF_STRING,3100,L"更多颜色…");}
    POINT point;GetCursorPos(&point);for(const auto& group:graphicsGroups)for(const auto& b:group.items)if(b.id==(width?"graphics.width":"graphics.color")){point={int((b.r.X+3)*scale),int((b.r.Y+52)*scale)};ClientToScreen(h,&point);}
    int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,h,nullptr);DestroyMenu(menu);
    if(chosen>=2000&&chosen<2010)error(applyGraphicsStyle(graphicWidths[chosen-2000],s.graphicsColor,true,false));
    else if(chosen==2100){double width=s.graphicsWidth>0?s.graphicsWidth:s.doc.defaultLineWidth();if(NumberDialog::show(h,L"线宽",L"线宽（磅）",width,&width,.1,12))error(applyGraphicsStyle(width,s.graphicsColor,true,false));}
    else if(chosen>=3000&&chosen<3008)error(applyGraphicsStyle(s.graphicsWidth,graphicColors[chosen-3000],false,true));
    else if(chosen==3100){static COLORREF custom[16]={};CHOOSECOLORW cc={};cc.lStructSize=sizeof(cc);cc.hwndOwner=h;cc.lpCustColors=custom;cc.rgbResult=RGB(BYTE(s.graphicsColor>>24),BYTE(s.graphicsColor>>16),BYTE(s.graphicsColor>>8));cc.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(studio_i18n::chooseColor(&cc)){uint32_t color=(uint32_t(GetRValue(cc.rgbResult))<<24)|(uint32_t(GetGValue(cc.rgbResult))<<16)|(uint32_t(GetBValue(cc.rgbResult))<<8)|255;error(applyGraphicsStyle(s.graphicsWidth,color,false,true));}}
}
bool App::drawGraphicStyleItem(DRAWITEMSTRUCT* item){
    if(!item||item->CtlType!=ODT_MENU)return false;int data=int(item->itemData);bool width=data>=2000&&data<2010,color=data>=3000&&data<3008;if(!width&&!color)return false;
    Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.TranslateTransform(float(item->rcItem.left),float(item->rcItem.top));g.ScaleTransform(scale,scale);
    float w=(item->rcItem.right-item->rcItem.left)/scale,h=(item->rcItem.bottom-item->rcItem.top)/scale;SolidBrush bg(item->itemState&ODS_SELECTED?Color(255,225,239,232):white);g.FillRectangle(&bg,0.f,0.f,w,h);
    bool checked=width?std::abs(s.graphicsWidth-graphicWidths[data-2000])<1e-8:s.graphicsColor==graphicColors[data-3000];
    if(checked){Pen tick(teal,1.7f);g.DrawLine(&tick,7.f,h*.5f,11.f,h*.5f+4);g.DrawLine(&tick,11.f,h*.5f+4,17.f,h*.5f-4);}
    if(width){double value=graphicWidths[data-2000];Pen stroke(ink,float(std::max(.65,value>0?value*2:1.2)));g.DrawLine(&stroke,28.f,h*.5f,94.f,h*.5f);std::ostringstream label;if(value>0)label<<value<<" 磅";else label<<"自动（按样式）";txt(g,label.str(),RectF(108,0,w-118,h),11);}
    else{uint32_t rgba=graphicColors[data-3000];SolidBrush swatch(Color(BYTE(rgba),BYTE(rgba>>24),BYTE(rgba>>16),BYTE(rgba>>8)));g.FillRectangle(&swatch,29.f,7.f,23.f,h-14);Pen border(line,1);g.DrawRectangle(&border,29.f,7.f,23.f,h-14);txt(g,graphicColorNames[data-3000],RectF(68,0,w-78,h),11);}
    return true;
}
