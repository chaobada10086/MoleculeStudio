#pragma once
#include "ui_language.hpp"
#include <array>
#include <cmath>
#include <sstream>
namespace studio_ui {
inline std::map<std::pair<float,bool>,std::unique_ptr<Font>>& fontCache(){static std::map<std::pair<float,bool>,std::unique_ptr<Font>> fonts;return fonts;}
inline void clearFonts(){fontCache().clear();}
inline Font* cachedFont(float size,bool bold){auto& f=fontCache()[{size,bold}];if(!f)f.reset(new Font(L"Microsoft YaHei UI",size,bold?FontStyleBold:FontStyleRegular,UnitPixel));return f.get();}

struct SkinPreset {const wchar_t* name; COLORREF color;};
inline const std::array<SkinPreset,5>& skinPresets(){
    static const std::array<SkinPreset,5> presets={{
        {L"柔和蓝",RGB(64,98,190)}, {L"GPT 绿",RGB(23,117,109)},
        {L"岩板紫",RGB(113,86,161)}, {L"琥珀棕",RGB(158,91,37)},
        {L"石墨灰",RGB(76,94,107)}
    }};
    return presets;
}
struct SkinSettings {
    size_t selected=0;
    std::array<COLORREF,5> colors;
    SkinSettings(){for(size_t i=0;i<colors.size();++i)colors[i]=skinPresets()[i].color;}
    static SkinSettings load(const std::wstring& path){
        wchar_t value[128]={};
        GetPrivateProfileStringW(L"Skin",L"Palette",L"",value,128,path.c_str());
        std::wistringstream input(value);
        SkinSettings result;
        if(!(input>>result.selected)||result.selected>=result.colors.size())return SkinSettings();
        for(auto& color:result.colors)if(!(input>>color)||color>0xFFFFFF)return SkinSettings();
        return result;
    }
    bool save(const std::wstring& path)const{
        std::wstring value=std::to_wstring(selected);
        for(auto color:colors)value+=L" "+std::to_wstring(color);
        return WritePrivateProfileStringW(L"Skin",L"Palette",value.c_str(),path.c_str())!=FALSE;
    }
};
inline SkinSettings& currentSkin(){
    static SkinSettings settings=SkinSettings::load(studio_i18n::path());
    return settings;
}
inline Color fromColorRef(COLORREF color){return Color(255,GetRValue(color),GetGValue(color),GetBValue(color));}
inline Color mix(Color a,Color b,double t){
    auto blend=[t](BYTE x,BYTE y){return BYTE(std::lround(x+(y-x)*t));};
    return Color(255,blend(a.GetR(),b.GetR()),blend(a.GetG(),b.GetG()),blend(a.GetB(),b.GetB()));
}
struct SkinColors {
    Color accent,strong,selected,hover,soft,frame,tab,border;
    explicit SkinColors(COLORREF color){
        Color base=fromColorRef(color), white(Color::White);
        accent=base;
        auto linear=[](double v){v/=255;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
        auto luminance=[&](Color c){return .2126*linear(c.GetR())+.7152*linear(c.GetG())+.0722*linear(c.GetB());};
        // Keep white text readable even when the user picks a very light color.
        while(luminance(accent)>.1833)accent=mix(accent,Color(Color::Black),.07);
        strong=mix(accent,Color(Color::Black),.20);
        selected=mix(white,base,.19);
        hover=mix(white,base,.105);
        soft=mix(white,base,.045);
        frame=mix(Color(255,246,247,249),base,.036);
        tab=mix(Color(255,243,244,246),base,.075);
        border=mix(Color(255,221,225,231),base,.105);
    }
};
inline SkinColors& skinColors(){
    const auto& settings=currentSkin();
    static SkinColors colors(settings.colors[settings.selected]);
    return colors;
}
inline unsigned& skinRevision(){static unsigned revision=0;return revision;}
inline void applySkin(const SkinSettings& settings){
    currentSkin()=settings;
    skinColors()=SkinColors(settings.colors[settings.selected]);
    ++skinRevision();
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL{
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
        return TRUE;
    },0);
}
inline Color accent(){return skinColors().accent;}
inline Color ink(){return Color(255,34,48,59);}
inline Color muted(){return Color(255,105,121,133);}
inline Color background(){return skinColors().frame;}
inline void roundPath(GraphicsPath& path,RectF r,float radius){
    float d=radius*2;path.AddArc(r.X,r.Y,d,d,180,90);path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);path.CloseFigure();
}
inline void round(Graphics& g,RectF r,float radius,Color fill){
    GraphicsPath path;roundPath(path,r,radius);SolidBrush brush(fill);g.FillPath(&brush,&path);
}
inline void label(Graphics& g,const std::wstring& text,RectF r,float size,Color color,bool bold=false,bool center=false){
    if(!g.IsVisible(r))return;Font* f=cachedFont(size,bold);SolidBrush b(color);StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap|(studio_i18n::rtl()?StringFormatFlagsDirectionRightToLeft:0));format.SetLineAlignment(StringAlignmentCenter);format.SetTrimming(StringTrimmingEllipsisCharacter);if(studio_i18n::rtl())format.SetAlignment(StringAlignmentFar);if(center)format.SetAlignment(StringAlignmentCenter);auto localized=studio_i18n::tr(text);g.DrawString(localized.c_str(),int(localized.size()),f,r,&format,&b);
}
inline bool button(DRAWITEMSTRUCT* item,bool primary=false,bool selected=false,float customScale=0){
    if(item->CtlType!=ODT_BUTTON)return false;Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    float dpi=customScale>0?customScale:GetDpiForWindow(item->hwndItem)/96.f;RectF r(0,0,float(item->rcItem.right),float(item->rcItem.bottom));SolidBrush bg(background());g.FillRectangle(&bg,r);
    bool disabled=(item->itemState&ODS_DISABLED)!=0,pressed=(item->itemState&ODS_SELECTED)!=0;
    // Secondary buttons must read as buttons, not as plain text: give them a tinted
    // surface and a visible outline on the white settings pages.
    Color fill=primary?accent():selected?skinColors().selected:skinColors().soft;
    Color border=primary?skinColors().strong:skinColors().border;
    if(pressed)fill=primary?skinColors().strong:skinColors().selected;if(disabled){fill=Color(255,232,237,239);border=Color(255,214,224,220);}
    GraphicsPath shape;roundPath(shape,RectF(1,1,r.Width-2,r.Height-2),6*dpi);
    SolidBrush brush(fill);g.FillPath(&brush,&shape);
    Pen outline(border,1*dpi);g.DrawPath(&outline,&shape);
    int n=studio_i18n::getWindowTextLength(item->hwndItem);std::wstring value(size_t(n)+1,0);studio_i18n::getWindowText(item->hwndItem,&value[0],n+1);value.resize(n);
    float size=12*dpi;auto localized=studio_i18n::tr(value);RectF bounds;StringFormat measure;measure.SetFormatFlags(StringFormatFlagsNoWrap);
    g.MeasureString(localized.c_str(),int(localized.size()),cachedFont(size,primary||selected),PointF(0,0),&measure,&bounds);
    if(bounds.Width>r.Width-16*dpi)size=std::max(9*dpi,size*(r.Width-16*dpi)/bounds.Width);
    label(g,value,RectF(8*dpi,0,r.Width-16*dpi,r.Height),size,disabled?muted():primary?Color(255,255,255,255):selected?accent():ink(),primary||selected,true);
    if(item->itemState&ODS_FOCUS){RECT focus=item->rcItem;InflateRect(&focus,-4,-4);DrawFocusRect(item->hDC,&focus);}return true;
}
inline HFONT font(HWND h,int size=13,bool bold=false){return CreateFontW(-MulDiv(size,GetDpiForWindow(h),96),0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");}
}
