#pragma once
#include "ui_language.hpp"
namespace studio_ui {
inline std::map<std::pair<float,bool>,std::unique_ptr<Font>>& fontCache(){static std::map<std::pair<float,bool>,std::unique_ptr<Font>> fonts;return fonts;}
inline void clearFonts(){fontCache().clear();}
inline Font* cachedFont(float size,bool bold){auto& f=fontCache()[{size,bold}];if(!f)f.reset(new Font(L"Microsoft YaHei UI",size,bold?FontStyleBold:FontStyleRegular,UnitPixel));return f.get();}

inline Color teal(){return Color(255,24,117,112);}
inline Color ink(){return Color(255,34,48,59);}
inline Color muted(){return Color(255,105,121,133);}
inline Color background(){return Color(255,245,248,249);}
inline void round(Graphics& g,RectF r,float radius,Color fill){
    GraphicsPath path;float d=radius*2;path.AddArc(r.X,r.Y,d,d,180,90);path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);path.CloseFigure();SolidBrush brush(fill);g.FillPath(&brush,&path);
}
inline void label(Graphics& g,const std::wstring& text,RectF r,float size,Color color,bool bold=false,bool center=false){
    if(!g.IsVisible(r))return;Font* f=cachedFont(size,bold);SolidBrush b(color);StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap|(studio_i18n::rtl()?StringFormatFlagsDirectionRightToLeft:0));format.SetLineAlignment(StringAlignmentCenter);format.SetTrimming(StringTrimmingEllipsisCharacter);if(studio_i18n::rtl())format.SetAlignment(StringAlignmentFar);if(center)format.SetAlignment(StringAlignmentCenter);auto localized=studio_i18n::tr(text);g.DrawString(localized.c_str(),int(localized.size()),f,r,&format,&b);
}
inline bool button(DRAWITEMSTRUCT* item,bool primary=false,bool selected=false,float customScale=0){
    if(item->CtlType!=ODT_BUTTON)return false;Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    float dpi=customScale>0?customScale:GetDpiForWindow(item->hwndItem)/96.f;RectF r(0,0,float(item->rcItem.right),float(item->rcItem.bottom));SolidBrush bg(background());g.FillRectangle(&bg,r);
    bool disabled=(item->itemState&ODS_DISABLED)!=0,pressed=(item->itemState&ODS_SELECTED)!=0;
    Color fill=primary?teal():selected?Color(255,225,241,239):Color(255,255,255,255);
    if(pressed)fill=Color(255,207,229,226);if(disabled)fill=Color(255,232,237,239);
    round(g,RectF(1,1,r.Width-2,r.Height-2),6*dpi,fill);
    int n=studio_i18n::getWindowTextLength(item->hwndItem);std::wstring value(size_t(n)+1,0);studio_i18n::getWindowText(item->hwndItem,&value[0],n+1);value.resize(n);
    float size=12*dpi;auto localized=studio_i18n::tr(value);RectF bounds;StringFormat measure;measure.SetFormatFlags(StringFormatFlagsNoWrap);
    g.MeasureString(localized.c_str(),int(localized.size()),cachedFont(size,primary||selected),PointF(0,0),&measure,&bounds);
    if(bounds.Width>r.Width-16*dpi)size=std::max(9*dpi,size*(r.Width-16*dpi)/bounds.Width);
    label(g,value,RectF(8*dpi,0,r.Width-16*dpi,r.Height),size,disabled?muted():primary&&!pressed?Color(255,255,255,255):selected?teal():ink(),primary||selected,true);
    if(item->itemState&ODS_FOCUS){RECT focus=item->rcItem;InflateRect(&focus,-4,-4);DrawFocusRect(item->hDC,&focus);}return true;
}
inline HFONT font(HWND h,int size=13,bool bold=false){return CreateFontW(-MulDiv(size,GetDpiForWindow(h),96),0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");}
}
