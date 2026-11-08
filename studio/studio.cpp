// New presentation layer. No dependency on the previous Window/Ribbon classes.
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "ui_language.hpp"
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <wincrypt.h>
#include <imm.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <objidl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <GL/gl.h>
#include "ui_native_language.hpp"
#include "formula.hpp"
#include "project_test_data.hpp"
#include "ring_library_data.hpp"
#include "engine/sprout_layout.hpp"
#include "engine/valence_warnings.hpp"
#include "engine/name_link.hpp"
#include "engine/graphic_style.hpp"
#include "page_dialog.hpp"
#include "next/core/commands.hpp"
#include "next/selection/transform.hpp"
#include "next/selection/grouping.hpp"
#include "next/selection/clipboard.hpp"
#include "next/chemistry/edit.hpp"
#include "next/advanced/perspective.hpp"
#include "next/advanced/arrange.hpp"
#include "next/integration/ring_geometry.hpp"
#include "engine/molecular_render.hpp"
#include <fstream>
#include <cmath>
#include <cstdio>
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <sstream>
#include <algorithm>
#include <climits>
#include <cwctype>
#include "compatible_renderer.hpp"
#ifdef MOLECULE_STUDIO_DX11
#include "dx11_renderer.hpp"
#endif
#include "ole_embed.hpp"
#include "ole_preview.hpp"
#include "tlc_dialog.hpp"
using namespace Gdiplus;
using namespace studio;
using r62::integration::Session;
namespace {
struct MarqueeBackdrop {
    HDC memory=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;int width=0,height=0;
    uint64_t serial=0,viewSerial=0;bool valid=false;
    void clear(){valid=false;if(memory&&previous)SelectObject(memory,previous);if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);memory=nullptr;bitmap=nullptr;previous=nullptr;}
    ~MarqueeBackdrop(){clear();}
};
struct App;
struct TabHost;
static ole::DocumentObject* g_oleClipboard=nullptr;
// Editor window that currently owns clipboard/OLE UI work. In tabbed mode this
// follows the active tab so copy/OLE commands always target what the user sees.
static App* g_currentApp=nullptr;
Color ink(255,40,52,53), muted(255,117,129,129), teal(255,23,117,109), line(255,224,230,227), paper(255,250,251,248), white(255,255,255,255);
std::wstring W(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),0,0);std::wstring w(n,L' ');if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),&w[0],n);return w;}
std::string U(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),0,0,0,0);std::string u(n,' ');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),&u[0],n,0,0);return u;}
std::string fullText(const Document& d,ObjectId id){std::string result;for(auto& r:d.runs(id))result+=r.utf8;return result;}
float uiTextWidth(const std::string& source,float size){auto text=studio_i18n::tr(W(source));static std::map<std::pair<std::wstring,float>,float> cache;auto key=std::make_pair(text,size);auto it=cache.find(key);if(it!=cache.end())return it->second;Bitmap surface(1,1,PixelFormat32bppARGB);Graphics g(&surface);Font f(L"Microsoft YaHei UI",size,FontStyleRegular,UnitPixel);RectF bounds;StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap);g.MeasureString(text.c_str(),int(text.size()),&f,PointF(0,0),&format,&bounds);if(cache.size()>4096)cache.clear();return cache[key]=bounds.Width;}

std::string displayFormula(const std::string& plain){auto in=W(plain);std::wstring out;bool isotope=false,charge=false;const wchar_t* supers=L"⁰¹²³⁴⁵⁶⁷⁸⁹";for(wchar_t ch:in){if(ch=='[')isotope=true;if(ch==']')isotope=false;if(ch=='(')charge=true;if(ch>='0'&&ch<='9'&&!charge)out+=isotope?supers[ch-'0']:wchar_t(0x2080+ch-'0');else out+=ch;}return U(out);}
#include "mass_dialog.hpp"
void txt(Graphics& g,const std::string& s,RectF r,float size=12,Color c=ink,bool bold=false,bool center=false){if(!g.IsVisible(r))return;float measured=uiTextWidth(s,size);if(measured>r.Width-3&&r.Width>15)size=std::max(8.5f,size*(r.Width-3)/measured);Font f(L"Microsoft YaHei UI",size,bold?FontStyleBold:FontStyleRegular,UnitPixel);SolidBrush b(c);StringFormat fmt;fmt.SetLineAlignment(StringAlignmentCenter);fmt.SetAlignment(center?StringAlignmentCenter:StringAlignmentNear);fmt.SetTrimming(StringTrimmingEllipsisCharacter);fmt.SetFormatFlags(StringFormatFlagsNoWrap);auto w=studio_i18n::tr(W(s));g.DrawString(w.c_str(),int(w.size()),&f,r,&fmt,&b);}
void rawTxt(Graphics& g,const std::string& s,RectF r,float size=12,Color c=ink,bool bold=false,bool center=false){if(!g.IsVisible(r))return;Font f(L"Microsoft YaHei UI",size,bold?FontStyleBold:FontStyleRegular,UnitPixel);SolidBrush b(c);StringFormat fmt;fmt.SetLineAlignment(StringAlignmentCenter);fmt.SetAlignment(center?StringAlignmentCenter:StringAlignmentNear);fmt.SetTrimming(StringTrimmingEllipsisCharacter);auto w=W(s);g.DrawString(w.c_str(),int(w.size()),&f,r,&fmt,&b);}
void wrappedTxt(Graphics& g,const std::string& s,RectF r,float size,Color c){if(!g.IsVisible(r))return;Font font(L"Microsoft YaHei UI",size,FontStyleRegular,UnitPixel);SolidBrush brush(c);StringFormat format;format.SetLineAlignment(StringAlignmentCenter);format.SetTrimming(StringTrimmingEllipsisWord);auto text=studio_i18n::tr(W(s));g.DrawString(text.c_str(),int(text.size()),&font,r,&format,&brush);}
void roundbox(Graphics& g,RectF r,float radius,Color c){if(!g.IsVisible(r))return;GraphicsPath p;float d=radius*2;p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();SolidBrush b(c);g.FillPath(&b,&p);}
struct Button {std::string id,label,icon,cmd;int arg=0;bool enabled=true;RectF r;Button(std::string i,std::string l,std::string ic,std::string c="",int a=0):id(i),label(l),icon(ic),cmd(c),arg(a){} };
struct Group {std::string name;std::vector<Button> items;RectF r;};
std::string shortcutFor(const Button& b){
    if(!b.enabled)return "";
    if(b.cmd=="@undo")return "Ctrl+Z";if(b.cmd=="@redo")return "Ctrl+Shift+Z";
    if(b.cmd=="@all")return "Ctrl+A";if(b.cmd=="@new")return "Ctrl+N";
    if(b.cmd=="@open")return "Ctrl+O";if(b.cmd=="@save")return "Ctrl+S";
    if(b.cmd=="@fit")return "F6";if(b.cmd=="@copy")return "Ctrl+C";if(b.cmd=="@cut")return "Ctrl+X";if(b.cmd=="@paste")return "Ctrl+V";
    if(b.cmd=="@flag"){if(b.arg==kBold)return "Ctrl+B";if(b.arg==kItalic)return "Ctrl+I";if(b.arg==kUnderline)return "Ctrl+U";}
    if(b.cmd=="chooseSelectionTool_Marquee")return "空格（无热点时）";
    if(b.cmd=="chooseBondTool_Solid")return "x（无热点时）";
    if(b.cmd=="chooseEdittingTool_Text")return "t（无热点时）";
    return "";
}
std::string buttonHint(const Button& b){if(!b.enabled)return b.label+"  ·  尚未接通";auto key=shortcutFor(b);return b.label+"  ·  "+(key.empty()?"未设置快捷键":key);}
// Small vector drawings, constructed specifically for this shell.
std::vector<PointF> chairIconVertices(bool second){
    auto preset=second?rings_chains::RingPresetId::Chair2:rings_chains::RingPresetId::Chair1;
    rings_chains::RingGeometry geometry;rings_chains::buildRingGeometry(preset,1,&geometry);
    double angle=rings_chains::chairOrientationDegrees(preset)*3.141592653589793/180;
    std::vector<Vec2> vertices;Box bounds;
    for(const auto& vertex:geometry.vertices){auto p=vertex.local;Vec2 rotated(p.x*cos(angle)+p.y*sin(angle),-p.x*sin(angle)+p.y*cos(angle));vertices.push_back(rotated);bounds.add(rotated);}
    Vec2 center=(bounds.min+bounds.max)*.5,extent=bounds.max-bounds.min;
    double scale=std::min(24/extent.x,20/extent.y);std::vector<PointF> result;
    for(auto p:vertices)result.push_back(PointF(float(14+(p.x-center.x)*scale),float(14+(p.y-center.y)*scale)));
    return result;
}
#include "shell_visuals.hpp"
void icon(Graphics& g,const std::string& id,RectF r,Color c){if(!g.IsVisible(r))return;auto st=g.Save();g.TranslateTransform(r.X,r.Y);g.ScaleTransform(r.Width/28,r.Height/28);Pen p(c,1.6f);p.SetLineJoin(LineJoinRound);SolidBrush b(c);auto L=[&](float a,float z,float x,float y){g.DrawLine(&p,a,z,x,y);};
    if(utilityGlyph(g,id,c)){}
    else if(id=="templateImport"||id=="templateExport"){g.DrawRectangle(&p,4,3,15,22);L(8,7,15,7);L(8,11,15,11);if(id=="templateImport"){L(27,18,12,18);L(12,18,17,13);L(12,18,17,23);}else{L(12,18,27,18);L(27,18,22,13);L(27,18,22,23);}}
    else if(id=="aiChat"){PointF outline[]={{4,4},{24,4},{24,19},{12,19},{6,24},{6,19},{4,19}};g.DrawPolygon(&p,outline,7);L(8,9,20,9);L(8,14,16,14);}
    else if(id=="apiKey"){g.DrawEllipse(&p,3,3,10,10);L(12,12,24,24);L(18,18,21,15);L(22,22,25,19);}
    else if(id=="select"){PointF a[]={{6,3},{7,23},{12,17},{17,25},{20,23},{15,15},{23,14}};g.DrawPolygon(&p,a,7);}
    else if(id=="lasso"){g.DrawEllipse(&p,3,4,22,17);g.DrawBezier(&p,7,18,1,18,1,27,12,24);}
    else if(id=="erase"){PointF a[]={{4,17},{14,5},{25,13},{15,25},{10,25}};g.DrawPolygon(&p,a,5);L(9,12,20,20);}
    else if(id=="bond"||id=="double"||id=="triple"||id=="bold"||id=="dash"||id=="dot"){if(id=="bold")p.SetWidth(4);if(id=="dash")p.SetDashStyle(DashStyleDash);if(id=="dot")p.SetDashStyle(DashStyleDot);L(5,23,23,5);if(id=="double"||id=="triple")L(3,18,18,3);if(id=="triple")L(10,25,25,10);}
    else if(id=="wedge"||id=="hollow"){PointF a[]={{4,24},{19,3},{26,10}};if(id=="wedge")g.FillPolygon(&b,a,3);else g.DrawPolygon(&p,a,3);}
    else if(id=="hash"||id=="hashed"){for(int i=0;i<6;i++){float t=i/5.f,x=5+17*t,y=23-17*t,w=id=="hash"?3:1+5*t;L(x-w,y-w,x+w,y+w);}}
    else if(id=="wave"||id=="curve"){g.DrawBezier(&p,3,23,2,2,18,31,14,13);g.DrawBezier(&p,14,13,9,-1,28,20,25,4);}
    else if(id.compare(0,4,"ring")==0||id=="benzene"||id=="diene"){int n=id=="benzene"?6:id=="diene"?5:atoi(id.substr(4).c_str());PointF a[8];for(int i=0;i<n;i++){double t=6.2831853*i/n-1.5707963;a[i]=PointF(float(14+10*cos(t)),float(14+10*sin(t)));}g.DrawPolygon(&p,a,n);if(id=="benzene"||id=="diene")for(int i=0;i<n;i+=2){if(id=="diene"&&i==4)break;auto x=a[i],y=a[(i+1)%n];L(14+(x.X-14)*.67f,14+(x.Y-14)*.67f,14+(y.X-14)*.67f,14+(y.Y-14)*.67f);}}
    else if(id=="otherRings"){PointF left[]={{3,9},{9,5},{15,9},{15,17},{9,21},{3,17}};PointF right[]={{15,9},{21,5},{27,9},{27,17},{21,21},{15,17}};g.DrawPolygon(&p,left,6);g.DrawPolygon(&p,right,6);L(20,23,23,26);L(23,26,26,23);}
    else if(id=="chair"||id=="chair2"){auto points=chairIconVertices(id=="chair2");g.DrawPolygon(&p,points.data(),int(points.size()));}
    else if(id=="chain"){const float h=1.73205f;PointF a[]={{2,14+h},{8,14-h},{14,14+h},{20,14-h},{26,14+h}};g.DrawLines(&p,a,5);}
    else if(id=="arrow"||id=="equil"){L(3,13,25,13);L(19,8,25,13);if(id=="arrow")L(19,18,25,13);else{L(3,20,25,20);L(3,20,9,25);}}
    else if(id=="oval"){g.DrawEllipse(&p,3,3,22,22);}
    else if(id=="rect"||id=="dottedRect"||id=="table"){if(id=="dottedRect")p.SetDashStyle(DashStyleDot);g.DrawRectangle(&p,4,4,20,20);if(id=="table"){L(14,4,14,24);L(4,14,24,14);}}
    else if(id=="roundRect"||id=="roundDashed"){if(id=="roundDashed")p.SetDashStyle(DashStyleDash);GraphicsPath path;path.AddArc(4,4,8,8,180,90);path.AddArc(16,4,8,8,270,90);path.AddArc(16,16,8,8,0,90);path.AddArc(4,16,8,8,90,90);path.CloseFigure();g.DrawPath(&p,&path);}
    else if(id=="bracket"){L(9,3,5,3);L(5,3,5,25);L(5,25,9,25);L(19,3,23,3);L(23,3,23,25);L(23,25,19,25);}
    else if(id=="paren"){g.DrawArc(&p,6,2,9,24,100,160);g.DrawArc(&p,13,2,9,24,-80,160);}
    else if(id=="plus"||id=="minus"){g.DrawEllipse(&p,4,4,20,20);L(9,14,19,14);if(id=="plus")L(14,9,14,19);}
    else if(id=="radical"){SolidBrush dot(c);g.FillEllipse(&dot,9,9,10,10);}
    else if(id=="copy"){g.DrawRectangle(&p,4,3,14,17);g.DrawRectangle(&p,10,9,14,17);}
    else if(id=="threeLines"){g.DrawLine(&p,3,5,25,5);g.DrawLine(&p,3,12,25,12);g.DrawLine(&p,3,24,25,24);}
    else if(id=="rowPlus"||id=="colPlus"){g.DrawRectangle(&p,3,4,19,19);if(id=="rowPlus"){L(3,10,22,10);L(3,17,22,17);}else{L(9,4,9,23);L(16,4,16,23);}L(20,18,28,18);L(24,14,24,22);}
    else if(id=="mergeCells"||id=="splitCells"){g.DrawRectangle(&p,3,5,22,18);L(3,14,25,14);if(id=="splitCells")L(14,5,14,23);else{L(14,14,14,23);L(7,9,20,9);}}
    else if(id=="cut"){g.DrawEllipse(&p,3,17,7,7);g.DrawEllipse(&p,17,17,7,7);L(8,19,22,3);L(20,19,6,3);}
    else if(id=="paste"){g.DrawRectangle(&p,5,5,18,21);g.DrawRectangle(&p,10,2,8,6);L(9,13,19,13);L(9,18,19,18);}
    else if(id=="undo"||id=="redo"){if(id=="redo"){g.TranslateTransform(28,0);g.ScaleTransform(-1,1);}g.DrawArc(&p,5,8,18,16,190,230);L(4,13,4,5);L(4,13,12,13);}
    else if(id=="shapeFill"){SolidBrush fill(Color(255,198,232,224));g.FillRectangle(&fill,4,5,20,18);g.DrawRectangle(&p,4,5,20,18);L(7,19,14,12);L(14,12,18,16);}
    else if(id=="color"){txt(g,"A",RectF(1,0,26,22),22,c,true,true);SolidBrush a(teal);g.FillRectangle(&a,4,24,20,3);}
    else if(id=="ocsr"){g.DrawRectangle(&p,4,3,16,21);L(8,8,16,8);L(8,12,16,12);g.DrawEllipse(&p,14,14,10,10);L(21,23,26,28);}
    else {std::string t=id=="text"?"A":id=="boldText"?"B":id=="italic"?"I":id=="under"?"U":id=="sub"?"x₂":id=="super"?"x²":id=="formula"?"CH₂":id=="sizeUp"?"A+":id=="sizeDown"?"A−":id=="font"?"Aa":id=="fit"?"⌖":id=="stamp"?"♟":id=="pen"?"✎":id;txt(g,t,RectF(0,0,28,28),id=="formula"?12:21,c,id=="boldText",true);}
    g.Restore(st);
}
std::vector<Group> makeTools(){std::vector<Group> v;
    auto G=[&](std::string n){v.push_back(Group());v.back().name=n;};
    auto T=[&](std::string l,std::string i,std::string c){v.back().items.push_back(Button(c,l,i,c));};
    auto A=[&](std::string l,std::string i,std::string id,int n=0){v.back().items.push_back(Button(id+std::to_string(n),l,i,"@"+id,n));};
    G("选择与编辑");T("框选","select","chooseSelectionTool_Marquee");T("套索","lasso","chooseSelectionTool_Lasso");T("橡皮","erase","chooseEdittingTool_Eraser");A("撤销","undo","undo");A("重做","redo","redo");A("全选","group","all");A("复制","copy","copy");A("剪切","cut","cut");A("粘贴","paste","paste");
    G("化学键");T("单键","bond","chooseBondTool_Solid");T("双键","double","chooseMultipleBondsTool_DoubleBond");T("三键","triple","chooseMultipleBondsTool_TripleBond");T("实楔","wedge","chooseBondTool_Wedged");T("虚楔","hashed","chooseBondTool_Hashed_Wedged");T("空楔","hollow","chooseBondTool_Hollow_Wedged");T("虚线","dash","chooseBondTool_Dashed");T("点线","dot","chooseBondTool_DottedHydrogen");T("短线","hash","chooseBondTool_Hashed");T("粗键","bold","chooseBondTool_Bold");T("波浪","wave","chooseBondTool_Wavy");T("配位","arrow","chooseMultipleBondsTool_DativeBond");
    G("环与碳链");const char* names[]={"三元环","四元环","五元环","六元环","七元环","八元环"};const char* commands[]={"Cyclopropane","Cyclobutane","Cyclopentane","Cyclohexane","Cycloheptane","Cyclooctane"};for(int i=0;i<6;i++)T(names[i],"ring"+std::to_string(i+3),"chooseRingTool_"+std::string(commands[i]));T("苯环","benzene","chooseRingTool_Benzene");T("二烯环","diene","chooseRingTool_Cyclopentadiene");T("椅式 Ⅰ","chair","chooseRingTool_Chair_Cyclohexane1");T("椅式 Ⅱ","chair2","chooseRingTool_Chair_Cyclohexane2");T("曲折链","chain","chooseAlkaneTool_Snaking_Chain");
    G("图形与标注");T("箭头","arrow","chooseArrowTool_Large_Solid");T("曲线","curve","choosePenTool_DrawCurve");T("圆形","oval","chooseDrawingTool_Circle_Hollow");T("矩形","rect","chooseDrawingTool_Rectangle_Hollow");T("圆角矩形","roundRect","chooseDrawingTool_RoundRect_Hollow");T("方括号","bracket","chooseBracketTool_SquareBrackets");T("圆括号","paren","chooseBracketTool_Parenthesis");T("负电荷","minus","chooseSymbolTool_CircleMinus");T("正电荷","plus","chooseSymbolTool_CirclePlus");T("自由基","radical","chooseSymbolTool_Radical");T("编辑曲线","pen","choosePenTool_EditCurve");T("直线","bond","chooseDrawingTool_Line");T("虚线框","dottedRect","chooseDrawingTool_Rectangle_Dashed");T("圆角虚线框","roundDashed","chooseDrawingTool_RoundRect_Dashed");
    G("文字与格式");T("文字","text","chooseEdittingTool_Text");A("字体","font","font");A("颜色","color","color");A("加粗","boldText","flag",kBold);A("斜体","italic","flag",kItalic);A("下划线","under","flag",kUnderline);A("化学式","formula","flag",kFormula);A("下标","sub","flag",kSubscript);A("上标","super","flag",kSuperscript);A("左对齐","textalign0","textalign",0);A("居中","textalign1","textalign",1);A("右对齐","textalign2","textalign",2);A("字号 +","sizeUp","size",1);A("字号 −","sizeDown","size",-1);A("两端对齐","textalign3","textalign",3);
    G("排列与变换");A("编组","group","group");A("解组","ungroup","ungroup");A("合并","merge","merge");A("左对齐","align0","align",0);A("水平居中","align1","align",1);A("右对齐","align2","align",2);A("顶部对齐","align3","align",3);A("垂直居中","align4","align",4);A("底部对齐","align5","align",5);A("水平分布","distH","distribute",0);A("垂直分布","distV","distribute",1);A("旋转 90°","rotate","rotate");A("水平翻转","flipH","flip",0);A("垂直翻转","flipV","flip",1);A("置于顶层","front","unsupported",1);A("置于底层","back","unsupported",2);A("前移一层","layerUp","unsupported",3);A("后移一层","layerDown","unsupported",4);
    for(auto& g:v)for(auto& b:g.items){if(b.cmd=="@unsupported"&&b.arg==1)b.cmd="@front";if(b.cmd=="@unsupported"&&b.arg==2)b.cmd="@back";if(b.cmd=="@unsupported")b.enabled=false;}
    return v;
}
#include "ribbon_tabs.hpp"
uint32_t mods(){return (GetKeyState(VK_CONTROL)<0?uint32_t(kModControl):0u)|(GetKeyState(VK_SHIFT)<0?uint32_t(kModShift):0u)|(GetKeyState(VK_MENU)<0?uint32_t(kModAlt):0u);}
#include "nicknames.hpp"
#include "chem_service.hpp"
#include "inline_text.hpp"
#include "shortcut_config.hpp"
#include "shortcut_hints.hpp"
#include "settings_dialog.hpp"
#include "drawing_settings.hpp"
#include "chem_dialog.hpp"
#include "nmr_dialog.hpp"
#include "rotation_cursor.hpp"
#include "ui_theme.hpp"
#include "chat_transcript.hpp"
#include "ai_window.hpp"
#include "settings_window.hpp"
#include "table_editor.hpp"
static bool chooseStudioColor(HWND owner,uint32_t* rgba){
    static COLORREF last=RGB(0,0,0),custom[16]={};
    const COLORREF colors[]={RGB(0,0,0),RGB(220,30,30),RGB(0,70,220),RGB(0,140,70),RGB(180,0,180),RGB(230,130,0),RGB(110,110,110),RGB(255,255,255)};
    const wchar_t* names[]={L"黑色",L"红色",L"蓝色",L"绿色",L"紫色",L"橙色",L"灰色",L"白色"};HMENU menu=CreatePopupMenu();
    for(int i=0;i<8;++i)studio_i18n::appendMenu(menu,MF_STRING|(colors[i]==last?MF_CHECKED:0),i+1,names[i]);studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);studio_i18n::appendMenu(menu,MF_STRING,20,L"更多颜色…");POINT point;GetCursorPos(&point);int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,owner,0);DestroyMenu(menu);if(!selected)return false;
    if(selected==20){CHOOSECOLORW cc={};cc.lStructSize=sizeof(cc);cc.hwndOwner=owner;cc.lpCustColors=custom;cc.rgbResult=last;cc.Flags=CC_FULLOPEN|CC_RGBINIT;if(!studio_i18n::chooseColor(&cc))return false;last=cc.rgbResult;}else last=colors[selected-1];
    *rgba=(uint32_t(GetRValue(last))<<24)|(uint32_t(GetGValue(last))<<16)|(uint32_t(GetBValue(last))<<8)|255;return true;
}
std::wstring arrowLabel(const arrows::PresetSpec& preset);
static std::wstring executableDirectory(){
    wchar_t exe[32768]={};
    GetModuleFileNameW(nullptr,exe,32768);
    std::wstring path=exe;
    auto slash=path.find_last_of(L"\\/");
    return slash==std::wstring::npos?std::wstring():path.substr(0,slash);
}
static bool ocsrRuntimeAvailable(){
    wchar_t env[32768]={};
    std::wstring path;
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_OCSR_RUNTIME",env,32768)&&env[0])path=env;
    else path=executableDirectory()+L"\\ocsr_runtime";
    DWORD attr=GetFileAttributesW(path.c_str());
    return attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_DIRECTORY);
}
static bool fileExists(const std::wstring& path){
    DWORD attr=GetFileAttributesW(path.c_str());
    return attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_DIRECTORY);
}
static bool aiFolderAvailable(){
    wchar_t env[32768]={};
    std::wstring dir;
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_AI_RUNTIME",env,32768)&&env[0])dir=env;
    else {
        dir=executableDirectory()+L"\\ai";
        DWORD compact=GetFileAttributesW(dir.c_str());
        if(compact==INVALID_FILE_ATTRIBUTES||!(compact&FILE_ATTRIBUTE_DIRECTORY))dir=executableDirectory()+L"\\AI 套件";
    }
    DWORD attr=GetFileAttributesW(dir.c_str());
    return attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_DIRECTORY);
}
static bool aiDrawingAvailable(){
    std::wstring dir=executableDirectory();
    return aiFolderAvailable()&&fileExists(dir+L"\\python\\python.exe")&&fileExists(dir+L"\\ai_plugins\\plugin.xml");
}
static bool aiRecognitionAvailable(){
    return aiFolderAvailable()&&ocsrRuntimeAvailable();
}
struct App : TextEditHost {
    void textEditChanged()override;void textEditSelectionChanged()override;void textEditCompositionChanged(bool on)override;void textEditFinishing()override;
    StudioTableEditor tableEditor;bool tableChecking=false;void tableCheck(const std::string& dir);void tableCommand(const std::string& op);bool tablePick(Vec2 point,bool open=false);ObjectId insertTable(uint32_t rows,uint32_t cols,bool three,double cellWidth=64,double cellHeight=24);
    bool tableDragPending=false,tableDragActive=false;ObjectId draggedTable=kInvalidObjectId;DocumentId tableDragDocument=0;uint64_t tableDragSerial=0;Vec2 tableDragPress,tableDragDelta;
    ObjectId tableAt(Vec2 point,uint32_t* row=nullptr,uint32_t* col=nullptr)const;bool beginTableDrag(Vec2 pixel,uint32_t modifiers);void updateTableDrag(Vec2 pixel,uint32_t modifiers);void finishTableDrag(Vec2 pixel,uint32_t modifiers);void cancelTableDrag();const render::Scene& tableDragScene();
    render::Scene tablePreview;std::vector<size_t> tablePreviewSlots;uint64_t tablePreviewDocument=0,tablePreviewSerial=0;void tableContextMenu(Vec2 point,POINT screen);
    struct TableControls{ObjectId table=kInvalidObjectId;uint32_t rows=0,cols=0;RectF addRow,delRow,addCol,delCol;};bool tableControlLayout(TableControls* out)const;bool tableControlClick(Vec2 pixel,uint32_t modifiers);
    AiWindow ai;DocumentId aiDocument=0;uint64_t aiSerial=0;std::vector<ObjectRef> aiSelection;Vec2 aiOrigin;double aiWidth=0;uint32_t aiColor=0x000000FFu;
    std::string aiContext();std::string applyAI(const std::string& bytes);void showAI(const std::string& prompt="",bool settings=false);void aiCheck(const std::string& dir);void aiUxCheck(const std::string& dir);
    void showOcsr();void showOcsrDemo();void showImageToStructure();void openImageToStructure(const std::wstring& imagePath);bool insertOcsrStructure(const std::string& cdxmlPath,const std::string& recordPath,std::wstring& error);bool copyOcsrStructure(const std::string& cdxmlPath,std::wstring& error);
    bool stereoLabelChecking=false;void stereoLabelCheck(const std::string& dir);bool uiChecking=false,chromeChecking=false,aiChecking=false,aiLiveChecking=false,textWrapChecking=false,textExtendChecking=false,ringLibraryChecking=false,templateLibraryChecking=false;void templateLibraryCheck(const std::string& dir);void ringLibraryCheck(const std::string& dir);void textExtendCheck(const std::string& dir);void settingsAiCheck(const std::string& dir);void groupDirectionCheck(const std::string& dir);void completionCheck(const std::string& dir);bool groupDirectionChecking=false,completionChecking=false;void textWrapCheck(const std::string& dir);bool arrowShortcutChecking=false;void arrowShortcutCheck(const std::string& dir);
    bool crosshairs=false,rulers=false,splitter=false,pointerOverCanvas=false,selectionCursorReset=true;uint64_t cursorRequests=0;Vec2 pointerPixel;std::string pendingName;
    void canvasGuides();
    StudioSettingsWindow settingsWindow;void showSettings(int category=0);
    DrawingSettings drawingSettings;
    void applyDrawingDefaults(){drawingSettings.apply(s.doc);s.graphicsWidth=0;s.refresh();}
    bool radicalPlacing=false;void applyRadical(const std::vector<ObjectId>& atoms);
    bool ringPlacing=false,ringPreferencesLoaded=false,ringHoldPending=false,ringHoldFired=false;int ringVariant=10;Vec2 ringHoldPoint;std::vector<int> ringRecent;
    bool ringButtonAt(float x,float y);void ringMenu();void loadRingPreferences();void rememberRing(int index);void beginRingPlacement();void cancelRingPlacement();ObjectId insertRingTemplate(int index,Vec2 center);
    bool customPlacement=false;std::string customPlacementName;void addCustomTemplate();void transferCustomTemplate(bool importing);void customTemplateMenu();void placeCustomTemplate(Vec2 center);void customTemplateCheck(const std::string& dir);bool customTemplateChecking=false;
    bool bioPlacing=false,bioPreferencesLoaded=false;int bioIndex=0;std::vector<int> bioRecent;std::shared_ptr<Session> bioSource;
    void cancelBioPlacement();void placeBioComponent(Vec2 center);void loadBioPreferences();void rememberBio(int index);
    bool phenalenePlacing=false;ObjectId insertPhenalene(Vec2 center);void cancelPhenalenePlacement();
    bool transformDragging=false;int transformHandle=-1;Box transformBox;Vec2 transformPivot,transformPress;
    selection::Affine2 transformMatrix;std::vector<ObjectRef> transformRefs;std::set<ObjectRef> transformOwned;uint64_t transformRevision=0;
    bool wrapDragging=false;int wrapHandle=-1;ObjectId wrapText=kInvalidObjectId;int wrapAlign=0;double wrapLeft=0,wrapRight=0,wrapStartWidth=0,wrapOldPositionX=0;bool wrapHadWidth=false;std::string wrapOldWidth;uint64_t wrapPreviewAt=0;
    bool textWrapBox(Box* box);bool beginTextWrap(Vec2 pixel,int handle);void updateTextWrap(Vec2 pixel);void finishTextWrap(Vec2 pixel);void cancelTextWrap();
    std::vector<Vec2> selectionHandlePoints();int selectionHandleAt(Vec2 pixel);bool beginSelectionTransform(Vec2 pixel,uint32_t modifiers);
    void updateSelectionTransform(Vec2 pixel,uint32_t modifiers);void finishSelectionTransform(Vec2 pixel,uint32_t modifiers);void cancelSelectionTransform();const render::Scene& selectionTransformScene();
    render::Scene transformPreview;std::vector<size_t> transformPreviewSlots;uint64_t transformPreviewDocument=0,transformPreviewSerial=0;
#ifdef MOLECULE_STUDIO_DX11
    std::vector<dx11::Renderer::RotationTarget> rotationTargets;uint64_t rotationGesture=0;bool rotationGpuEligible=false;
#endif
    ShortcutConfig shortcuts;bool cloneDragging=false;Vec2 clonePress,cloneDelta;std::vector<ObjectRef> cloneRefs;std::shared_ptr<Session> cloneSource;bool selectionArrowEdit=false;
    mutable uint64_t hintGeneration=uint64_t(-1);mutable std::map<std::string,std::vector<shortcut_hints::Context>> hintCache;
    InlineTextEditor textEditor;bool textPlacing=false;Vec2 textPress;HWND fontBox=0,sizeBox=0;HFONT textUiFont=0;bool syncingTextControls=false;
    MarqueeBackdrop textBackdrop;bool textBackdropBuilding=false;
    size_t textBackgroundBuilds=0;bool renderTextBackdrop();
    bool embedded=false;ole::DocumentObject* oleObject=nullptr;
    bool liveSizing=false,resizePending=false;size_t resizeLayouts=0,resizePreviews=0;
    bool benchmarking=false;size_t shellPaints=0,canvasPaints=0;double shellMs=0,canvasMs=0;render::RenderStats lastStats;double lastOverlayMs=0,lastSceneMs=0,lastDrawMs=0,lastPresentMs=0,lastGuidesMs=0;std::string lastTitle,lastChrome,lastStatus,lastHover;
    static double now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    void benchmark();
    void benchmarkFile(const std::string& file);
    void canvasBench(const std::string& file);
    void firstEditBench(const std::string& file,const std::string& out);void rigidDragCheck(const std::string& dir);
    std::unique_ptr<NmrDialog> nmrPage;HWND nmrChild=0;void openNmrTab(const std::string& snapshot);HWND focusSurface()const{return nmrChild?nmrChild:(textEditor.active&&IsWindowVisible(edit)?edit:canvas);}
    bool reviewEditor=false;std::function<void()> reviewChanged;
    TabHost* host=nullptr;bool tabChild=false,quitOnDestroy=true;void createGraphics();void notifyTitleChanged();
    float tabBandH()const;void layoutTabBand();void paintTabBand(Graphics& g);bool tabHotkey(UINT m,WPARAM wp);RectF infoRect;
    Session s;HWND h=0,canvas=0,hbar=0,vbar=0,edit=0,apply=0,cancel=0,tip=0;HDC dc=0;HGLRC rc=0;std::unique_ptr<render::Backend> gl;compatible::Renderer* compatibleRenderer=nullptr;bool closeChecking=false,canvasDestroySeen=false,rendererReleasedBeforeCanvasDestroy=false;
#ifdef MOLECULE_STUDIO_DX11
    dx11::Renderer* dx11Renderer=nullptr;
#endif
    void shutdownRenderer(){if(gl)rendererReleasedBeforeCanvasDestroy=!canvasDestroySeen&&IsWindow(canvas);if(rc&&dc)wglMakeCurrent(dc,rc);if(gl){gl->destroy();gl.reset();}compatibleRenderer=nullptr;
#ifdef MOLECULE_STUDIO_DX11
        dx11Renderer=nullptr;
#endif
if(rc){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(rc);rc=nullptr;}if(dc){ReleaseDC(canvas,dc);dc=nullptr;}}
    ~App(){shutdownRenderer();}
    bool expandPending=false;uint64_t expandDigest=0;DocumentId expandDocument=0;std::vector<ObjectRef> expandSelection;
    void expandGroups();void collapseGroups();
    std::vector<Group> groups=makeTools();std::vector<Button> quick;std::vector<Button> chemistryQuick={Button("quickStructureToName","结构转命名","","@structureToName"),Button("quickNameToStructure","命名转结构","","@nameToStructure"),Button("quickCleanStructure","结构整理","","@clean"),Button("quickExpandGroups","官能团展开","","@expandGroups"),Button("quickCollapseGroups","官能团收起","","@collapseGroups"),Button("quickTextTidy","文字整理","","@textTidy"),Button("quickImageToStructure","图片转结构","","@imageToStructure"),Button("quickStereoLabels","构型标注","","@stereoLabels"),Button("quickNMR","预测核磁","","@nmr")};float scale=1;int width=1500,height=920,top=260;bool down=false,pan=false,testing=false;Vec2 panAt;ObjectId textId=kInvalidObjectId,atomId=kInvalidObjectId;
    // A double-click on a bond endpoint must edit that atom's label, not first
    // sprout a new bond from it. Track the click's history cursor so the sprout
    // committed by the first click can be undone when the second click arrives.
    size_t clickHistory=0;Vec2 clickPoint;bool lastBondClick=false;size_t lastBondClickHistory=0;Vec2 lastBondClickPoint;uint64_t lastBondClickTick=0;std::string hover,status="准备就绪",fileName="未命名结构",testOut;Formula info;uint64_t formulaSerial=0;std::vector<ObjectRef> formulaRefs;size_t formulaBuilds=0;
    // Hover popup for a system-name caption whose structure changed; and the
    // modal format question asked when pasting an external structure.
    HWND nameLinkTip=nullptr;std::wstring nameLinkTipText;
    std::string nameLinkTipAt(Vec2 documentPoint);
    void updateNameLinkTip(Vec2 documentPoint);
    void hideNameLinkTip();
    bool askPasteFormat(const studioio::DrawingFormat& source,const studioio::DrawingFormat& target,bool* adoptSource);
    App(){quick={Button("new","新建","","@new"),Button("open","打开","","@open"),Button("save","保存","","@save"),Button("export","导出","","@export"),Button("fit","适合窗口","","@fit"),Button("settings","设置","","@settings"),Button("ribbonSize","展开","","@ribbonSize"),Button("fixedLength","锁定键长","","@fixedLength"),Button("ribbonPin","锁定","","@ribbonPin"),Button("fileMenu","文件","","@fileMenu")};s.input.setKeymap(&shortcuts.chemistry);loadRibbon();applyDrawingDefaults();
        if(!ocsrRuntimeAvailable()){
            chemistryQuick.erase(std::remove_if(chemistryQuick.begin(),chemistryQuick.end(),[](const Button& b){return b.id=="quickImageToStructure";}),chemistryQuick.end());
            for(auto& group:groups)group.items.erase(std::remove_if(group.items.begin(),group.items.end(),[](const Button& b){return b.cmd=="@ocsr";}),group.items.end());
            groups.erase(std::remove_if(groups.begin(),groups.end(),[](const Group& g){return g.items.empty();}),groups.end());
        }
        for(auto& group:aiGroups){
            if(!aiFolderAvailable())group.items.erase(std::remove_if(group.items.begin(),group.items.end(),[](const Button& b){return b.id=="ai.open"||b.id=="ai.example"||b.id=="ai.settings"||b.id=="ai.ocsr";}),group.items.end());
            else{
                if(!aiRecognitionAvailable())group.items.erase(std::remove_if(group.items.begin(),group.items.end(),[](const Button& b){return b.id=="ai.ocsr";}),group.items.end());
                if(!aiDrawingAvailable())group.items.erase(std::remove_if(group.items.begin(),group.items.end(),[](const Button& b){return b.id=="ai.open"||b.id=="ai.example";}),group.items.end());
            }
        }
        aiGroups.erase(std::remove_if(aiGroups.begin(),aiGroups.end(),[](const Group& g){return g.items.empty();}),aiGroups.end());
        // The clipboard layer lives in interop.hpp with no UI dependency; install
        // the real dialog here. g_currentApp follows the active tab, so the
        // question always belongs to the document the user is looking at.
        if(!studioio::pasteFormatAsker())studioio::pasteFormatAsker()=[](const studioio::DrawingFormat& source,const studioio::DrawingFormat& target,bool* adoptSource){
            App* app=g_currentApp;if(!app)return false;return app->askPasteFormat(source,target,adoptSource);
        };
    }
    int ribbonTab=0;bool ribbonCompact=false,ribbonPinned=true,ribbonVisible=true;
    std::vector<std::pair<RectF,size_t>> compactMenus;bool graphicsDense=false,graphicsAutoCompact=false,swallowHotkeyChar=false;
    std::wstring interfacePath(){wchar_t exe[32768]={};GetModuleFileNameW(nullptr,exe,32768);std::wstring path=exe;return path.substr(0,path.find_last_of(L"\\/"))+L"\\interface.ini";}
    // A first install has no saved Ribbon keys: the expanded ribbon is the default
    // (ChemDraw opens with its toolbars visible). Only an explicitly saved
    // Compact=1 or Pinned=0 restores a collapsed/auto-hidden toolbar.
    void loadRibbon(const std::wstring& settingsPath=std::wstring()){auto path=settingsPath.empty()?interfacePath():settingsPath;ribbonCompact=GetPrivateProfileIntW(L"Ribbon",L"Compact",0,path.c_str())!=0;ribbonPinned=GetPrivateProfileIntW(L"Ribbon",L"Pinned",1,path.c_str())!=0;ribbonVisible=ribbonPinned;auto& tlc=studio_tlc::preferences();tlc.lanes=std::max(1,std::min(12,int(GetPrivateProfileIntW(L"TLC",L"Lanes",4,path.c_str()))));tlc.spots=std::max(0,std::min(10,int(GetPrivateProfileIntW(L"TLC",L"Spots",1,path.c_str()))));tlc.diameter=std::max(1.,std::min(16.,GetPrivateProfileIntW(L"TLC",L"DiameterTenths",40,path.c_str())/10.));tlc.guides=GetPrivateProfileIntW(L"TLC",L"Guides",0,path.c_str())!=0;tlc.origin=GetPrivateProfileIntW(L"TLC",L"Origin",1,path.c_str())!=0;tlc.front=GetPrivateProfileIntW(L"TLC",L"Front",1,path.c_str())!=0;tlc.showRf=GetPrivateProfileIntW(L"TLC",L"ShowRf",0,path.c_str())!=0;std::vector<wchar_t> payload(131072);GetPrivateProfileStringW(L"TLC",L"Template",L"",payload.data(),DWORD(payload.size()),path.c_str());bool valid=false;auto draft=r62::experimental::decodeFigure(U(payload.data()),&valid);if(valid&&draft.kind==r62::experimental::FigureKind::TlcPlate&&r62::experimental::validateFigure(draft).ok()){tlc.plateTemplate=draft;tlc.customTemplate=true;}arrowPreset=GetPrivateProfileIntW(L"Arrow",L"Preset",0,path.c_str());auto arrow=arrows::presetById(arrowPreset);if(!arrow){arrowPreset=0;arrow=arrows::presetById(0);}syncArrowSurfaces();}
    void saveRibbon(){auto path=interfacePath();WritePrivateProfileStringW(L"Ribbon",L"Compact",ribbonCompact?L"1":L"0",path.c_str());WritePrivateProfileStringW(L"Ribbon",L"Pinned",ribbonPinned?L"1":L"0",path.c_str());}
    void showRibbonGroup(size_t index){if(index>=visibleGroups().size())return;auto group=visibleGroups()[index];for(auto& b:group.items)if(b.cmd=="@addCustomTemplate")b.enabled=!s.selected.empty();for(size_t i=0,n=group.items.size();i<n;++i)if(group.items[i].cmd=="chooseTLCPlateTool")group.items.insert(group.items.begin()+i+1,Button("tlcSetup","薄层板点样设置…","table","@tlcMenu"));HMENU menu=CreatePopupMenu();for(size_t i=0;i<group.items.size();i++){const auto& item=group.items[i];auto title=W(item.label);auto shortcut=shortcutFor(item);if(!shortcut.empty())title+=L"\t"+W(shortcut);studio_i18n::appendMenu(menu,MF_STRING|(item.enabled?0:MF_GRAYED),UINT_PTR(i+1),title.c_str());}POINT at={int(group.r.X*scale),int(group.r.GetBottom()*scale)};ClientToScreen(h,&at);int result=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,at.x,at.y,0,h,nullptr);DestroyMenu(menu);if(result>0&&size_t(result)<=group.items.size())execute(group.items[size_t(result)-1]);}

    std::vector<Group> graphicsGroups=makeExpandedTools(false),textGroups=makeExpandedTools(true),aiGroups=makeAITools(),tableGroups=makeTableTools(),templateGroups=makeTemplateTools();
    std::vector<Button> ribbonTabs={Button("tabHome","开始","","@tab",0),Button("tabGraphics","图形","","@tab",1),Button("tabText","文字","","@tab",2),Button("tabAI","AI","","@tab",3),Button("tabTable","表格","","@tab",4),Button("tabTemplates","模板","","@tab",5)};bool aiTabVisible=aiFolderAvailable();
    std::vector<Group>& visibleGroups(){return ribbonTab==1?graphicsGroups:ribbonTab==2?textGroups:ribbonTab==3?aiGroups:ribbonTab==4?tableGroups:ribbonTab==5?templateGroups:groups;}
    const std::vector<Group>& visibleGroups()const{return ribbonTab==1?graphicsGroups:ribbonTab==2?textGroups:ribbonTab==3?aiGroups:ribbonTab==4?tableGroups:ribbonTab==5?templateGroups:groups;}
    void switchTab(int tab){expandPending=false;if(tab==3&&!aiTabVisible)return;if(textEditor.active&&!finishText(true))return;if(tab<0||size_t(tab)>=ribbonTabs.size()||(tab==ribbonTab&&ribbonVisible))return;ribbonVisible=true;ribbonTab=tab;hover.clear();layout();invalidate();}
    uint32_t arrowPreset=0;bool arrowHoldPending=false,arrowHoldFired=false;Vec2 arrowHoldPoint;
    // The preset chosen in the Graphics tab is the only thing the arrow tool
    // shortcut may use; no surface may hard-code the Large_Solid default.
    std::string arrowCommand()const{auto preset=arrows::presetById(arrowPreset);return preset?preset->sourceCommand:std::string("chooseArrowTool_Large_Solid");}
    void syncArrowSurfaces();std::string arrowPresetName()const;
    bool arrowButtonAt(float x,float y)const{for(const auto& group:visibleGroups())for(const auto& b:group.items)if((b.id=="chooseArrowTool_Large_Solid"||b.id=="graphics.arrowStyle")&&b.r.Contains(x,y))return true;return false;}
    uint64_t scrollSerial=~0ull,scrollBoundsSerial=~0ull;double scrollScale=-1,scrollDpiScale=-1;int scrollW=0,scrollH=0;Vec2 scrollCenter;Box scrollBounds;bool scrollValid=false,scrollBoundsValid=false;
    bool hScrollActive=false,vScrollActive=false;
    double hScrollPos=0,hScrollMax=0,hScrollPage=0,hScrollContent=0,vScrollPos=0,vScrollMax=0,vScrollPage=0,vScrollContent=0;
    std::vector<Button> windowControls={Button("windowMin","最小化","windowMin","@windowMin"),Button("windowMax","最大化","windowMax","@windowMax"),Button("windowClose","关闭","windowClose","@windowClose")};
    void chooseArrowPreset(uint32_t id);
    void graphicsStyleMenu(bool width);
    bool cycleAtomHydrogens(ObjectId id,bool initialize=false,int replacement=0);bool preferAtomHydride(ObjectId id,int element);void hydrideDefaultCheck(const std::string& dir);void bioMenu();void insertBioComponent(size_t index);void shapeMenu();void shapeFillMenu();void insertAutoShape(size_t index);bool drawShapeItem(DRAWITEMSTRUCT* item);uint32_t shapeFill=0xE6F2EFFFu;bool shapeFillEnabled=true;
    bool patternPlacing=false,patternDragging=false,patternPreviewPending=false;int patternShape=-1;Vec2 patternStart,patternEnd;void updatePatternPreview();void finishPatternPlacement(Vec2 end);void cancelPatternPlacement();
    void paintGraphicFormatControl(Graphics& g,const Button& button);
    bool drawGraphicStyleItem(DRAWITEMSTRUCT* item);
    Error applyTextColor(uint32_t color);
    // Text objects reached by the current selection, including text inside
    // selected groups: formatting buttons and the font/size boxes act on these
    // directly, without first opening the inline editor.
    std::vector<ObjectId> selectedTexts(){std::vector<ObjectId> texts;std::set<ObjectId> seen;for(auto ref:studioio::expanded(s.doc,s.selected))if(ref.kind==ObjectKind::Text&&s.doc.texts().alive(ref.id)&&seen.insert(ref.id).second)texts.push_back(ref.id);return texts;}
    // Point size a text object effectively uses: its own size, else the first
    // explicit run size, else the document caption size.
    static double textBaseSize(const r62::core::Document& d,ObjectId id){auto text=d.texts().alive(id);if(!text)return d.defaultTextSize();if(text->size>0)return text->size;for(const auto& run:d.runs(id))if(run.size>0)return run.size;return d.defaultTextSize();}
    Error applyGraphicsStyle(double width,uint32_t color,bool setWidth,bool setColor);
    bool drawArrowMenuItem(DRAWITEMSTRUCT* item);
    void chromeCheck(const std::string& dir);
    void graphicStyleCheck(const std::string& dir);
void cdxmlColorCheck(const std::string& dir);
    bool shortcut(UINT key,uint32_t mask,bool editor=false);
    void settingsMenu();void fileMenu();void arrowMenu();void tlcMenu();void tlcSettings(ObjectId id=kInvalidObjectId,uint32_t spot=0);
    void editorHotkey(int index);
    bool chemistryAction(const std::string& op,int arg=0);
    bool extraAction(const std::string& op,int arg=0);
    bool arrangeAction(const std::string& op,int arg=0);
    void conformer3D();
    std::vector<ObjectRef> chemicalScope();
    Error finishClone(Vec2 p,uint32_t pointerModifiers=0);
    WORD canvasCursorId(Vec2 pixel){if(pan&&!std::getenv("MOLECULE_STUDIO_PAN_LEGACY"))return 32646;if(bioPlacing||ringPlacing||phenalenePlacing)return 32515;if(patternPlacing)return 32515;if(radicalPlacing||s.entries[s.activeIndex].command=="chooseSymbolTool_Radical")return 32515;const auto& tool=s.entries[s.activeIndex];bool selection=tool.owner=="T05"&&tool.command.find("Eraser")==std::string::npos;if(down&&s.selectionDragActive())return 32512;if(down&&s.objectDragActive()&&!std::getenv("MOLECULE_STUDIO_MOVE_LEGACY"))return 32646;if(wrapDragging)return 32644;if(transformDragging)return transformHandle==8?32649:32646;if(selection){int hit=selectionHandleAt(pixel);if(hit>=0){if(s.dragFixedLength){auto refs=studioio::expanded(s.doc,s.selected);for(auto r:refs)if(r.kind==ObjectKind::Atom&&(hit!=8||s.nearestAtom(s.view().toDocument(pixel),8)!=kInvalidObjectId))return 32646;}return hit==8?32649:(hit==1||hit==6)?32645:(hit==3||hit==4)?32644:(hit==0||hit==7)?32642:32643;}}if(tableDragPending||((ribbonTab==4||selection)&&tableAt(s.view().toDocument(pixel))!=kInvalidObjectId))return 32646;if(selection&&selectionCursorReset)return 32512;if(pan||cloneDragging||(down&&selection&&!s.selected.empty()))return 32646;if(tool.owner=="T09")return 32513;if(selection&&!s.selected.empty()&&studioio::inside(studioio::selectionBounds(s),s.view().toDocument(pixel),s.view().deviceRadiusToDocument(3)))return 32646;return 32512;}
    bool rotationHot=false;HCURSOR cursorAt(Vec2 point){WORD id=canvasCursorId(point);return ((transformDragging&&transformHandle==8)||(id==32649&&selectionHandleAt(point)==8))?rotationPointer(GetDpiForWindow(canvas)):LoadCursorW(0,MAKEINTRESOURCEW(id));}
    void updateCanvasCursor(bool clientMessage=false){++cursorRequests;POINT point;if(!GetCursorPos(&point))return;if(!clientMessage&&GetCapture()!=canvas&&WindowFromPoint(point)!=canvas)return;ScreenToClient(canvas,&point);bool hot=!transformDragging&&canvasCursorId(Vec2(point.x,point.y))==32649&&selectionHandleAt(Vec2(point.x,point.y))==8;if(hot!=rotationHot){rotationHot=hot;InvalidateRect(canvas,0,FALSE);}SetCursor(cursorAt(Vec2(point.x,point.y)));}
    int shortcutContext()const{if(!pointerOverCanvas)return 1;auto context=s.input.hotspot().context();return context==interaction::HotspotContext::Atom?2:context==interaction::HotspotContext::Bond?3:1;}
    static float footerHeight(){return 30;}
    Button hintButton()const{for(const auto& tab:ribbonTabs)if(tab.id==hover)return tab;for(const auto& group:visibleGroups())for(const auto& b:group.items)if(b.id==hover)return b;for(const auto& b:quick)if(b.id==hover)return b;for(const auto& b:chemistryQuick)if(b.id==hover)return b;auto command=s.entries[s.activeIndex].command;for(const auto& group:visibleGroups())for(const auto& b:group.items)if(b.cmd==command)return b;return Button("",command=="chooseAlkaneTool_Acyclic_Chain"?"直链":"当前绘图工具","",command);}
    const shortcut_hints::Context& toolHint(const Button& b,int scope)const{if(hintGeneration!=shortcuts.generation){hintCache.clear();hintGeneration=shortcuts.generation;}auto arrow=arrowCommand();std::string key=b.cmd+"|"+std::to_string(b.arg)+"|"+b.label+"|"+arrow;auto found=hintCache.find(key);if(found==hintCache.end()){std::vector<shortcut_hints::Context> contexts;for(int i=1;i<=3;++i)contexts.push_back(shortcut_hints::describe(shortcuts,b,i,arrow,arrowPresetName()));found=hintCache.insert({key,contexts}).first;}return found->second[scope-1];}
    std::string hint(const Button& b){return b.label+" · "+toolHint(b,shortcutContext()).text;}
    void invalidate(bool redrawCanvas=true){
        // Panning changes only the viewport. Defer tool/title/formula syncing
        // until release, and update scrollbars once per painted frame.
        if(pan&&!std::getenv("MOLECULE_STUDIO_PAN_LEGACY")){if(redrawCanvas&&canvas)InvalidateRect(canvas,0,FALSE);return;}
        if(reviewEditor){updateScrollbars();syncTextControls();if(redrawCanvas&&canvas)InvalidateRect(canvas,0,FALSE);if(reviewChanged)reviewChanged();return;}
        if((down&&fastGraphicTool())||fastMarquee()||(down&&s.objectDragActive()&&!std::getenv("MOLECULE_STUDIO_MOVE_LEGACY"))){if(redrawCanvas)InvalidateRect(canvas,0,FALSE);return;}
        updateScrollbars();
        auto activeArrow=arrows::presetByCommand(s.entries[s.activeIndex].command);
        if(activeArrow){arrowPreset=activeArrow->presetId;syncArrowSurfaces();}
        for(auto& gr:templateGroups)for(auto& b:gr.items)if(b.cmd=="@addCustomTemplate")b.enabled=!s.selected.empty();
        updateInfo();syncTextControls();touched();
        std::string title="Molecule Studio 0.95  ·  "+fileName+(s.dirty?" *":"");
        if(title!=lastTitle){lastTitle=title;studio_i18n::setWindowText(h,W(title).c_str());if(host)notifyTitleChanged();}
        std::ostringstream key;key<<s.graphicsWidth<<'|'<<s.graphicsColor<<'|'<<ribbonTab<<'|'<<title<<'|'<<s.activeIndex<<'|'<<info.valid<<'|'<<info.plain<<'|'<<info.mass<<'|'<<s.selected.empty()<<'|'<<shortcutContext()<<'|'<<shortcuts.generation<<'|'<<hover;
        if(key.str()!=lastChrome){lastChrome=key.str();RECT r={0,0,width,top};InvalidateRect(h,&r,FALSE);}
        key.str("");key<<hover<<'|'<<status<<'|'<<s.serial<<'|'<<info.note<<'|'<<s.view().scale<<'|'<<s.activeIndex<<'|'<<shortcutContext()<<'|'<<textEditor.active<<'|'<<shortcuts.generation;
        if(key.str()!=lastStatus){lastStatus=key.str();RECT r={0,height-int(footerHeight()*scale),width,height};InvalidateRect(h,&r,FALSE);}
        if(hover!=lastHover){
            auto dirty=[&](const Button& b){if(b.id==hover||b.id==lastHover){RECT r={int(b.r.X*scale)-2,int(b.r.Y*scale)-2,int(b.r.GetRight()*scale)+2,int(b.r.GetBottom()*scale)+2};InvalidateRect(h,&r,FALSE);}};
            for(auto& gr:visibleGroups())for(auto& b:gr.items)dirty(b);for(auto& b:quick)dirty(b);for(auto& b:chemistryQuick)dirty(b);for(auto& b:windowControls)dirty(b);for(auto& b:ribbonTabs)dirty(b);lastHover=hover;
        }
        if(redrawCanvas)InvalidateRect(canvas,0,FALSE);
    }
    void updateInfo(){if(formulaSerial==s.serial&&formulaRefs==s.selected)return;formulaSerial=s.serial;formulaRefs=s.selected;
    // Nothing selected: leave the top-right card empty.  Analysing the whole
    // document there was slow and ambiguous, because the formula expands
    // abbreviations and implicit hydrogens so it can never match the status
    // bar's atom count.
    if(s.selected.empty()){info=Formula();return;}
    info=analyze(s.doc,s.selected);++formulaBuilds;}
    void showMassCalculator(){updateInfo();MassCalculatorDialog::show(h,info);}
    void error(Error e){if(!e.ok())status=e.message;}
    void layout(){++resizeLayouts;if(tabChild){float dpi=GetDpiForWindow(h)/96.f;if(dpi>0.25f)scale=dpi;}float bh=tabBandH();lastChrome.clear();lastStatus.clear();InvalidateRect(h,0,FALSE);RECT r;GetClientRect(h,&r);width=r.right;height=r.bottom;int reserved=ai.docked&&IsWindowVisible(ai.hwnd)?ai.dockWidth(width,scale)+int(12*scale):0;
        if(reviewEditor){
            top=int(32*scale);int sb=GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(h));
            auto v=s.view();v.dpiScale=scale*96.0/72.0;s.setView(v);
            int cw=std::max(1,width-sb),ch=std::max(1,height-top-sb);
            if(canvas)MoveWindow(canvas,0,top,cw,ch,TRUE);
            if(hbar)MoveWindow(hbar,0,top+ch,cw,sb,TRUE);
            if(vbar)MoveWindow(vbar,cw,top,sb,ch,TRUE);
            if(fontBox)MoveWindow(fontBox,int(3*scale),int(3*scale),int(150*scale),int(240*scale),TRUE);
            if(sizeBox)MoveWindow(sizeBox,int(157*scale),int(3*scale),int(48*scale),int(240*scale),TRUE);
            for(int i=0;i<4;++i)if(auto b=GetDlgItem(h,410+i))MoveWindow(b,int((211+30*i)*scale),int(3*scale),int(28*scale),int(25*scale),TRUE);
            textEditor.position();updateScrollbars(true);return;
        }
        if(nmrPage){
            layoutTabBand();for(size_t i=0;i<windowControls.size();++i)windowControls[i].r=RectF(width/scale-120+float(i)*40,0,40,40);
            for(HWND c:{canvas,hbar,vbar,fontBox,sizeBox})if(c)ShowWindow(c,SW_HIDE);
            if(nmrChild)MoveWindow(nmrChild,0,int(40*scale),width,std::max(1,height-int(40*scale)),TRUE);return;
        }
        float fullW=width/scale,w=(width-reserved)/scale;bool compactQuick=reserved&&w<640;float columns=0;for(const auto& g:visibleGroups())columns+=float((g.items.size()+2)/3);
        float cell=std::min(29.f,std::max(24.f,(w-342-10*visibleGroups().size())/columns));
        if(ribbonTab)cell=ribbonTab==1?std::min(112.f,std::max(78.f,(w-552.f)/std::max(1.f,columns-2))):ribbonTab==5?164.f:ribbonTab==3?140.f:ribbonTab==2?std::max(64.f,std::min(76.f,(w-230.f)/8.f)):90.f;float gh=ribbonTab?112.f:98.f,step=ribbonTab?30.f:26.f;
        float groupsTop=80.f;float x=12,y=groupsTop;for(auto& g:visibleGroups()){bool arrowCard=!g.items.empty()&&g.items.front().id=="graphics.arrowStyle";bool formatCard=!g.items.empty()&&g.items.front().id=="graphics.width";float groupCell=cell;if(studio_i18n::current()>1&&ribbonTab&&ribbonTab!=3&&!arrowCard&&!formatCard){for(const auto& b:g.items)groupCell=std::max(groupCell,std::min(190.f,uiTextWidth(b.label,10.f)+46.f));}float gw=arrowCard?176.f:formatCard?236.f:float((g.items.size()+2)/3)*groupCell+10;float limit=!ribbonTab&&!compactQuick?w-318:(ribbonTab==2?w-168.f:w-12);if(x+gw>limit+.01f&&x>12){x=12;y+=gh+4;}
            g.r=RectF(x,y,gw,gh);for(size_t i=0;i<g.items.size();++i){g.items[i].r=arrowCard?RectF(x+8,y+4,gw-16,24):formatCard?RectF(x+8+float(i)*114,y+4,106,88):RectF(x+5+float(i/3)*groupCell,y+3+float(i%3)*step,groupCell-2,step-2);}x+=gw+(ribbonTab==1?8:0);}
        graphicsDense=ribbonTab==1&&w<1320;graphicsAutoCompact=ribbonTab==1&&w<1040;
        if(ribbonTab==1&&!ribbonCompact&&!graphicsAutoCompact){
            x=12;y=groupsTop;gh=112;float regular=std::min(96.f,std::max(78.f,(w-454.f)/std::max(1.f,columns-2.f)));
            for(auto& group:visibleGroups()){
                bool arrow=!group.items.empty()&&group.items.front().id=="graphics.arrowStyle",format=!group.items.empty()&&group.items.front().id=="graphics.width",arrange=group.name=="排列与变换";
                float pitch=graphicsDense?(arrange?26.f:82.f):regular;if(studio_i18n::current()>1&&!arrow&&!format){for(const auto& b:group.items)pitch=std::max(pitch,std::min(180.f,uiTextWidth(b.label,10.f)+44.f));}
                float gw=arrow?140.f:format?204.f:float((group.items.size()+2)/3)*pitch+10;
                if(x+gw>w-12&&x>12){x=12;y+=gh+4;}group.r=RectF(x,y,gw,gh);
                for(size_t i=0;i<group.items.size();++i)group.items[i].r=arrow?RectF(x+6,y+4,gw-12,24):format?RectF(x+5+float(i)*98,y+4,96,88):RectF(x+5+float(i/3)*pitch,y+3+float(i%3)*30,pitch-2,28);
                x+=gw+6;
            }
        }
        compactMenus.clear();
        if((ribbonCompact||graphicsAutoCompact)&&ribbonTab!=5){x=12;y=groupsTop;gh=48;for(size_t index=0;index<visibleGroups().size();index++){auto& group=visibleGroups()[index];size_t count=std::min(size_t(3),group.items.size());if(ribbonTab==1)count=std::min(count,size_t(std::max(0.f,std::floor(((w-24)/visibleGroups().size()-36)/26))));float gw=float(count)*26+30;float limit=!ribbonTab&&y==groupsTop&&!compactQuick?w-318:w-12;if(x+gw>limit&&x>12){x=12;y+=52;}group.r=RectF(x,y,gw,48);for(auto& item:group.items)item.r=RectF(-10000,-10000,1,1);for(size_t i=0;i<count;i++)group.items[i].r=RectF(x+4+float(i)*26,y+4,24,24);compactMenus.push_back({RectF(x+gw-24,y+4,22,24),index});x+=gw+6;}}
        if(!ribbonVisible){y=groupsTop;gh=0;compactMenus.clear();for(auto& group:visibleGroups()){group.r=RectF(-10000,-10000,1,1);for(auto& item:group.items)item.r=RectF(-10000,-10000,1,1);}}
        if(!ribbonCompact&&ribbonVisible&&ribbonTab==3){auto& group=visibleGroups().front();float cardWidth=(w-40-12*float(group.items.size()-1))/float(std::max(size_t(1),group.items.size()));group.r=RectF(12,groupsTop,w-24,112);for(size_t i=0;i<group.items.size();i++)group.items[i].r=RectF(20+float(i)*(cardWidth+12),78,cardWidth,76);}
        // Document units are points; include monitor scaling for both rendering
        // and pointer conversion, including moves between monitors.
        auto view=s.view();view.dpiScale=scale*96.0/72.0;s.setView(view);
        // The quick column is part of the toolbar band: with seven actions it
        // reaches a third 30px row, and the compact ribbon (48px) is shorter
        // than that. Keep the canvas below the last quick button in every mode.
        int quickRows=0;float quickBottom=0;
        if(!ribbonTab&&ribbonVisible&&!chemistryQuick.empty()){
            if(compactQuick){quickRows=int((chemistryQuick.size()+3)/4);quickBottom=y+gh+8+float(quickRows-1)*30+24;}
            else{quickRows=int((chemistryQuick.size()+2)/3);quickBottom=groupsTop+float(quickRows-1)*30+24;}
        }
        float canvasTop=y+gh+8;
        if(quickRows>0)canvasTop=std::max(canvasTop,quickBottom+6);
        top=int(canvasTop*scale);
        layoutTabBand();int sb=GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(h));int ch=std::max(60,height-top-int(footerHeight()*scale)-sb);int cw=std::max(100,width-int(32*scale)-reserved-sb);
        // Batch children without synchronous paints during native edge tracking.
        HDWP positions=BeginDeferWindowPos(3);
        if(positions)positions=DeferWindowPos(positions,canvas,0,int(16*scale),top,cw,ch,SWP_NOZORDER|SWP_NOACTIVATE);
        if(positions)positions=DeferWindowPos(positions,hbar,0,int(16*scale),top+ch,cw,sb,SWP_NOZORDER|SWP_NOACTIVATE);
        if(positions)positions=DeferWindowPos(positions,vbar,0,int(16*scale)+cw,top,sb,ch,SWP_NOZORDER|SWP_NOACTIVATE);
        if(positions)EndDeferWindowPos(positions);
        else {MoveWindow(canvas,int(16*scale),top,cw,ch,FALSE);MoveWindow(hbar,int(16*scale),top+ch,cw,sb,FALSE);MoveWindow(vbar,int(16*scale)+cw,top,sb,ch,FALSE);}
        ShowWindow(hbar,SW_SHOW);ShowWindow(vbar,SW_SHOW);
        updateScrollbars(true);
        if(reserved){if(ai.dockScale!=scale){ai.dockScale=scale;ai.updateFont();}MoveWindow(ai.hwnd,width-reserved,int(groupsTop*scale),reserved-int(12*scale),std::max(60,height-int((68+footerHeight())*scale)),TRUE);}
        {float tabX=96.f;for(size_t i=0;i<ribbonTabs.size();++i){if(i==3)continue;auto& tab=ribbonTabs[i];float tw=uiTextWidth(tab.label,13.5f)+16.f;tab.r=RectF(tabX,40.f,tw,36.f);tabX+=tw+24.f;}
         auto& aiTab=ribbonTabs[3];float aiW=uiTextWidth(aiTab.label,13.5f)+20.f;aiTab.r=RectF(fullW-16.f-aiW,40.f,aiW,36.f);}
        for(auto& b:quick){
            if(b.id=="new"||b.id=="open"||b.id=="save"||b.id=="export"){b.r=RectF(-10000,-10000,1,1);continue;}
            if(b.id=="fileMenu")b.r=RectF(16.f,44.f,64.f,28.f);
        }
        {float fitW=uiTextWidth("适合窗口",13.f)+20.f,setW=uiTextWidth("设置",13.f)+20.f;
         float rx=ribbonTabs[3].r.X-14.f;
         auto place=[&](const char* id,float w){for(auto& b:quick)if(b.id==id){rx-=w;b.r=RectF(rx,44.f,w,28.f);rx-=4.f;}};
         auto quickWidth=[&](const char* id){for(auto& b:quick)if(b.id==id)return uiTextWidth(b.id=="ribbonPin"?(ribbonPinned?"锁定":"自动"):b.id=="fixedLength"?(s.dragFixedLength?"键长锁定":"锁定键长"):b.id=="ribbonSize"?(ribbonCompact?"展开":"收起"):b.label,12.f)+18.f;return 52.f;};
         place("ribbonPin",quickWidth("ribbonPin"));place("fixedLength",quickWidth("fixedLength"));place("ribbonSize",quickWidth("ribbonSize"));place("settings",setW);place("fit",fitW);}
        infoRect=RectF(fullW-344.f,6.f,204.f,28.f);
        for(size_t i=0;i<windowControls.size();++i)windowControls[i].r=RectF(fullW-120+float(i)*40,0,40,40);
        // Seven quick actions: three per row by the formula card, four per row
        // when the AI dock squeezes the ribbon (otherwise the row overflows).
        for(size_t i=0;i<chemistryQuick.size();++i){
            if(!ribbonVisible){chemistryQuick[i].r=RectF(-10000,-10000,1,1);continue;}
            if(compactQuick){
                const size_t perRow=4;const float gap=6.f;
                float qw=std::max(48.f,std::min(90.f,(w-24.f-float(perRow-1)*gap)/float(perRow)));
                chemistryQuick[i].r=RectF(12+float(i%perRow)*(qw+gap),y+gh+8+float(i/perRow)*30,qw,24);
            }else chemistryQuick[i].r=RectF(w-302+float(i%3)*98,groupsTop+float(i/3)*30,90,24);
        }
        textEditor.position();if(fontBox){float fx=w-160.f;bool show=ribbonTab==2;for(HWND c:{fontBox,sizeBox})ShowWindow(c,show?SW_SHOW:SW_HIDE);MoveWindow(fontBox,int(fx*scale),int(84.f*scale),int(100*scale),int(240*scale),TRUE);MoveWindow(sizeBox,int((fx+104)*scale),int(84.f*scale),int(48*scale),int(240*scale),TRUE);}
    }
    void updateScrollbars(bool force=false){
        if(!canvas||!IsWindow(canvas))return;
        int w=s.renderView.widthPx,h=s.renderView.heightPx;if(w<=0||h<=0)return;
        auto v=s.view();
        if(!force&&scrollValid&&scrollSerial==s.serial&&scrollScale==v.scale&&scrollDpiScale==v.dpiScale&&scrollW==w&&scrollH==h&&scrollCenter.x==v.center.x&&scrollCenter.y==v.center.y)return;
        scrollValid=true;scrollSerial=s.serial;scrollScale=v.scale;scrollDpiScale=v.dpiScale;scrollW=w;scrollH=h;scrollCenter=v.center;
        if(!scrollBoundsValid||scrollBoundsSerial!=s.serial){
            Box b=s.scene.bounds();
            auto page=studioio::Page::get(s.doc);
            if(page.width>0&&page.height>0){b.add(Vec2(page.x,page.y));b.add(Vec2(page.x+page.width*page.across,page.y+page.height*page.down));}
            double dpd=v.docPerDevice();
            if(!b.empty()){
                double mx=std::max(8.0*dpd,(b.max.x-b.min.x)*.02),my=std::max(8.0*dpd,(b.max.y-b.min.y)*.02);
                b.add(Vec2(b.min.x-mx,b.min.y-my));b.add(Vec2(b.max.x+mx,b.max.y+my));
            }
            scrollBounds=b;scrollBoundsValid=true;scrollBoundsSerial=s.serial;
        }
        auto set=[&](bool horiz,double minDoc,double maxDoc,int pagePx,double centerDoc,bool& active,double& posOut,double& maxOut,double& pageOut,double& contentOut){
            double dpd=v.docPerDevice();
            double contentPx=(maxDoc-minDoc)/std::max(1e-9,dpd);
            double page=double(pagePx);
            double maxPos=std::max(0.0,contentPx-page);
            double pos=(centerDoc-minDoc)/std::max(1e-9,dpd)-page*.5;
            pos=std::max(0.0,std::min(maxPos,pos));
            active=maxPos>0.5;
            posOut=pos;maxOut=maxPos;pageOut=page;contentOut=contentPx;
        };
        Box b=scrollBounds;
        if(b.empty()){b.add(v.center);b.add(v.center);}
        set(true,b.min.x,b.max.x,w,v.center.x,hScrollActive,hScrollPos,hScrollMax,hScrollPage,hScrollContent);
        set(false,b.min.y,b.max.y,h,v.center.y,vScrollActive,vScrollPos,vScrollMax,vScrollPage,vScrollContent);
        auto ctrl=[&](HWND control,double content,double page,double pos){
            if(!control)return;
            SCROLLINFO si={};si.cbSize=sizeof(si);si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;si.nMin=0;si.nMax=std::max(0,int(std::ceil(content))-1);si.nPage=UINT(std::max(1.0,page));si.nPos=int(std::llround(pos));
            SetScrollInfo(control,SB_CTL,&si,TRUE);
        };
        ctrl(hbar,hScrollContent,hScrollPage,hScrollPos);ctrl(vbar,vScrollContent,vScrollPage,vScrollPos);
    }
    void applyScroll(int bar,double pos){
        if(!scrollBoundsValid)updateScrollbars(true);
        auto v=s.view();double dpd=v.docPerDevice();
        if(bar==SB_HORZ){
            if(hScrollMax<=0)return;
            pos=std::max(0.0,std::min(hScrollMax,pos));
            v.center.x=scrollBounds.min.x+(pos+hScrollPage*.5)*dpd;
        }else{
            if(vScrollMax<=0)return;
            pos=std::max(0.0,std::min(vScrollMax,pos));
            v.center.y=scrollBounds.min.y+(pos+vScrollPage*.5)*dpd;
        }
        s.setView(v);updateScrollbars(true);invalidate();
    }
    void paint(Graphics& g,bool chrome=true,bool statusBar=true){float bh=tabBandH();g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.ScaleTransform(scale,scale);float w=width/scale,hh=height/scale;SolidBrush bg(paper);g.FillRectangle(&bg,0.f,0.f,w,hh);
        if(chrome&&nmrPage){
            paintTabBand(g);roundbox(g,RectF(16,8,24,24),6,teal);icon(g,"benzene",RectF(19,11,18,18),white);txt(g,"Molecule Studio",RectF(52,0,132,40),14,ink,true);
            for(auto& button:windowControls){if(hover==button.id)roundbox(g,button.r,0,button.id=="windowClose"?Color(255,205,49,59):Color(255,228,234,229));icon(g,button.id=="windowMax"&&IsZoomed(h)?"windowRestore":button.icon,RectF(button.r.X+10,10,20,20),hover=="windowClose"&&button.id=="windowClose"?white:ink);}return;
        }
        if(chrome){
        paintTabBand(g);
        roundbox(g,RectF(16,8,24,24),6,teal);icon(g,"benzene",RectF(19,11,18,18),white);
        txt(g,"Molecule Studio",RectF(52,0,132,40),14,ink,true);
        for(const auto& tab:ribbonTabs){
            if(tab.arg==3&&!aiTabVisible)continue;
            if(tab.arg==ribbonTab){SolidBrush underline(teal);g.FillRectangle(&underline,tab.r.X+2,tab.r.GetBottom()-2,tab.r.Width-4,2.f);}
            else if(hover==tab.id)roundbox(g,tab.r,4,Color(255,236,241,237));
            txt(g,tab.label,tab.r,13.5f,tab.arg==ribbonTab?teal:ink,tab.arg==ribbonTab,true);
        }
        for(auto& button:windowControls){if(hover==button.id)roundbox(g,button.r,0,button.id=="windowClose"?Color(255,205,49,59):Color(255,228,234,229));
            icon(g,button.id=="windowMax"&&IsZoomed(h)?"windowRestore":button.icon,RectF(button.r.X+10,10,20,20),hover=="windowClose"&&button.id=="windowClose"?white:ink);}
        for(auto& q:quick){if(q.r.X<-1000.f)continue;if(hover==q.id||(q.id=="ribbonPin"&&ribbonPinned)||(q.id=="fixedLength"&&s.dragFixedLength))roundbox(g,q.r,4,Color(255,225,238,230));std::string label=q.id=="ribbonSize"?(ribbonCompact?"展开":"收起"):q.id=="ribbonPin"?(ribbonPinned?"锁定":"自动"):q.id=="fixedLength"?(s.dragFixedLength?"键长锁定":"锁定键长"):q.label;bool accent=(q.id=="ribbonPin"&&ribbonPinned)||(q.id=="fixedLength"&&s.dragFixedLength);txt(g,label,q.r,q.id=="fileMenu"?13.5f:12.f,q.id=="fileMenu"?ink:(accent?teal:ink),q.id=="fileMenu",true);}
        // Selection card: only shown while something is selected, above the
        // tools at the far right.
        const bool showCard=!s.selected.empty();
        RectF card=infoRect;bool cardHot=showCard&&hover=="molecule-info";
        // Prominent selection card: a teal outline, a soft teal wash and a
        // heavier formula/mass pair so the live molecular weight reads at a glance.
        // Hovering brightens the outline/fill and the hand cursor + status hint
        // make the click-to-open calculator discoverable.
        if(showCard){
        roundbox(g,card,7,cardHot?Color(255,12,92,86):Color(255,23,117,109));
        roundbox(g,RectF(card.X+1,card.Y+1,card.Width-2,card.Height-2),6,cardHot?Color(255,203,235,230):Color(255,222,241,237));
        SolidBrush cardAccent(cardHot?Color(255,9,82,77):teal);g.FillRectangle(&cardAccent,card.X+2,card.Y+8,3.f,card.Height-16.f);
        }
        if(showCard&&info.valid){
            txt(g,displayFormula(info.plain),RectF(card.X+11,card.Y,88,card.Height),16,cardHot?Color(255,8,54,51):Color(255,16,72,68),true);
            Pen cardDivider(cardHot?Color(255,120,181,173):Color(255,166,204,198),1);g.DrawLine(&cardDivider,card.X+101,card.Y+7,card.X+101,card.Y+card.Height-7);
            txt(g,info.mass+" g/mol",RectF(card.X+107,card.Y,card.Width-113,card.Height),13.5f,cardHot?Color(255,7,74,69):teal,true);
        }else if(showCard){txt(g,"分子式 —   分子量 —",card,12.5f,muted,false,true);}
        if(!ribbonTab&&ribbonVisible)for(const auto& b:chemistryQuick){roundbox(g,b.r,4,hover==b.id?Color(255,218,237,227):Color(255,235,242,237));txt(g,b.label,b.r,12,hover==b.id?teal:ink,false,true);}
        for(auto& gr:visibleGroups()){
            if(!ribbonVisible)continue;
            if((ribbonCompact||graphicsAutoCompact)&&ribbonTab!=5){roundbox(g,gr.r,6,white);for(auto& b:gr.items){if(b.r.X<0)continue;if(hover==b.id)roundbox(g,b.r,4,Color(255,225,240,235));icon(g,b.icon,RectF(b.r.X+3,b.r.Y+3,18,18),ink);}Pen more(teal,1.5f);float cx=gr.r.GetRight()-13,cy=gr.r.Y+15;g.DrawLine(&more,cx-3,cy-2,cx,cy+1);g.DrawLine(&more,cx,cy+1,cx+3,cy-2);txt(g,gr.name,RectF(gr.r.X+3,gr.r.Y+29,gr.r.Width-6,16),9,muted,false,true);continue;}
            roundbox(g,gr.r,6,white);for(auto& b:gr.items){
            if(ribbonTab==3){bool primary=b.id=="ai.open";roundbox(g,b.r,8,primary?Color(255,226,242,239):hover==b.id?Color(255,235,242,243):Color(255,246,248,249));icon(g,b.icon,RectF(b.r.X+12,b.r.Y+13,20,20),teal);txt(g,b.label,RectF(b.r.X+40,b.r.Y+8,b.r.Width-48,29),13,ink,true);std::string detail=primary?"独立窗口 / 画布侧栏":"API、模型与预设提示词";txt(g,detail,RectF(b.r.X+12,b.r.Y+43,b.r.Width-24,24),10,muted);continue;}
            if(b.id=="graphics.width"||b.id=="graphics.color"){paintGraphicFormatControl(g,b);continue;}
            if(b.id=="graphics.arrowStyle"){
                RectF selector=b.r;
                roundbox(g,selector,4,hover==b.id?Color(255,218,237,227):Color(255,237,244,240));
                txt(g,b.label,RectF(selector.X+6,selector.Y,selector.Width-22,selector.Height),11,teal);
                Pen chevron(teal,1.4f);float cx=selector.GetRight()-10,cy=selector.Y+10;g.DrawLine(&chevron,cx-3,cy,cx,cy+3);g.DrawLine(&chevron,cx,cy+3,cx+3,cy);
                if(b.id=="graphics.arrowStyle"){
                    RectF preview(gr.r.X+8,gr.r.Y+35,gr.r.Width-16,54);roundbox(g,preview,5,Color(255,247,250,249));
                    uint32_t rgba=s.graphicsColor;Color color(BYTE(rgba),BYTE(rgba>>24),BYTE(rgba>>16),BYTE(rgba>>8));
                    arrowPreview(g,arrowPreset,RectF(preview.X+22,preview.Y+5,preview.Width-44,25),color,s.graphicsWidth,s.doc.defaultLineWidth());
                    auto preset=arrows::presetById(arrowPreset);std::string label=preset?U(arrowLabel(*preset)):"";
                    txt(g,label,RectF(preview.X+4,preview.Y+33,preview.Width-8,16),9,muted,false,true);
                }continue;
            }
            bool selected=b.cmd==s.entries[s.activeIndex].command;
            if(selected)roundbox(g,b.r,4,Color(255,218,237,227));else if(hover==b.id)roundbox(g,b.r,4,Color(255,239,244,238));
            bool addTemplate=b.cmd=="@addCustomTemplate"&&b.enabled;if(addTemplate)roundbox(g,b.r,4,Color(255,218,237,227));Color c=b.enabled?((selected||addTemplate)?teal:ink):Color(255,181,188,184);
            auto preset=arrows::presetByCommand(b.cmd);
            if((ribbonTab==1&&!(graphicsDense&&gr.name=="排列与变换"))||ribbonTab==2||ribbonTab==3||ribbonTab==4||ribbonTab==5){
                if(preset||b.cmd=="@arrows")arrowPreview(g,preset?preset->presetId:arrowPreset,RectF(b.r.X+3,b.r.Y+5,24,18),c);
                else icon(g,b.icon,RectF(b.r.X+4,b.r.Y+5,18,18),c);
                txt(g,b.label,RectF(b.r.X+(ribbonTab==2?27.f:30.f),b.r.Y,ribbonTab==2?b.r.Width-29.f:b.r.Width-(b.cmd=="chooseTLCPlateTool"?40:31),b.r.Height),10,c);
            }else{
                if(preset||b.cmd=="@arrows")arrowPreview(g,preset?preset->presetId:arrowPreset,RectF(b.r.X+2,b.r.Y+1,b.r.Width-4,16),c);
                else icon(g,b.icon,RectF(b.r.X+(b.r.Width-17)/2,b.r.Y,17,17),c);
            }
            if(ribbonTab!=2&&(b.id=="chooseArrowTool_Large_Solid"||b.cmd=="@arrows"||b.cmd=="chooseTLCPlateTool")){Pen chevron(c,1.f);float x=b.r.GetRight()-5,y=b.r.Y+19;g.DrawLine(&chevron,x-2,y,x,y+2);g.DrawLine(&chevron,x,y+2,x+2,y);}
            if(ribbonTab!=1&&ribbonTab!=2&&b.cmd.compare(0,6,"choose")==0&&g.IsVisible(b.r)){const auto& keys=toolHint(b,shortcutContext());txt(g,studio_i18n::current()>1&&keys.badge=="空格"?std::string("Spc"):keys.badge,RectF(b.r.X,b.r.Y+15,b.r.Width,12),8.5f,b.enabled?teal:muted,false,true);}}
            if(gr.items.empty()||gr.items.front().id!="graphics.arrowStyle")txt(g,gr.name,RectF(gr.r.X,gr.r.GetBottom()-14,gr.r.Width,12),9,muted,false,true);
        }
        }
        if(statusBar){float y=hh-footerHeight();auto button=hintButton();auto key=shortcutFor(button);std::string title="快捷键 · "+button.label+(key.empty()?"":"  "+key)+"   |   大写字母需 Shift";
        if(patternPlacing)title="插入图案 · 拖动确定大小，单击使用默认大小，Esc 取消";
        else if(textEditor.active)title="文字编辑中 · Enter / Esc 保存，Shift+Enter 换行";
        if(hover=="molecule-info"&&!s.selected.empty())title="质谱计算器 · 点击打开"+(info.valid?"  |  "+info.plain+"  ·  "+info.mass+" g/mol":"");
        txt(g,title,RectF(18,y+1,w-330,27),10.5f,ink);
        std::ostringstream ss;auto stats=s.doc.stats();ss<<"原子总数 "<<stats.aliveAtoms<<" · 键 "<<stats.aliveBonds<<" | "<<int(s.view().scale*100)<<"%";txt(g,ss.str(),RectF(w-290,y+1,274,27),10,muted);
        }
    }
    render::Scene editingScene(){render::Scene scene;if(s.objectDragActive()){uint64_t ordinal=0;for(auto p:s.displayScene().primitives()){p.source=ObjectRef();p.version=s.serial*1000000+(++ordinal);scene.add(p);}return scene;}for(auto p:s.displayScene().primitives())if(p.layer.value==0)scene.add(p);
        // Keep each primitive's cacheKey/version: only the edited object is
        // removed.  Re-stamping every primitive made the renderer treat all
        // 90k of them as new and rebuild 12.7 M triangles (~2.4 s) every time
        // the inline text editor opened.
        const bool legacy=std::getenv("MOLECULE_STUDIO_LEGACY_EDITING_SCENE")!=nullptr;uint64_t ordinal=0;
        for(auto p:s.scene.primitives()){if(p.source==textEditor.editingObject())continue;p.source=ObjectRef();if(legacy)p.version=s.serial*1000000+(++ordinal);scene.add(p);}return scene;}
    render::Scene clonePreviewOverlay(const render::Scene& overlay){
        auto preview=overlay;
        if(cloneSource)for(auto p:cloneSource->scene.primitives()){
            for(auto& pt:p.points)pt=pt+cloneDelta;
            for(auto& sub:p.subpaths)for(auto& pt:sub.points)pt=pt+cloneDelta;
            p.bounds.min=p.bounds.min+cloneDelta;p.bounds.max=p.bounds.max+cloneDelta;
            // This is transient geometry, not a document object. Hash its actual
            // coordinates each frame instead of reusing the source's cache ID
            // or a fixed version that hides movement from the renderer.
            p.source=ObjectRef();p.cacheKey=0;p.version=0;preview.add(p);
        }
        return preview;
    }
    void clonePreviewCheck(const std::string& dir);void performanceAiCheck(const std::string& file,const std::string& dir);
    MarqueeBackdrop marqueeBackdrop;bool marqueeBuilding=false;size_t marqueeBackgroundBuilds=0,marqueeFastFrames=0;
    bool fastMarquee()const{const char* disabled=std::getenv("MOLECULE_STUDIO_MARQUEE_LEGACY");return !(disabled&&disabled[0]=='1')&&s.selectionDragActive();}
    bool renderMarquee();
    bool insertionPreviewPending=false;Vec2 insertionPreviewPixel,insertionPreviewPoint;uint32_t insertionPreviewModifiers=0;size_t insertionPreviewTool=0;
    bool fastGraphicTool()const{
        if(std::getenv("MOLECULE_STUDIO_INSERT_LEGACY"))return false;
        const auto& c=s.entries[s.activeIndex].command;
        return c.find("chooseArrowTool_")==0||c.find("chooseDrawingTool_")==0||c.find("chooseBracketTool_")==0||c=="chooseTableTool";
    }
    void insertionPerformanceCheck(const std::string& dir);
    double lastMovePaintMs=0; bool movePreviewPending=false;Vec2 movePreviewPoint;uint32_t movePreviewModifiers=0;
    void patternPerformanceCheck(const std::string& dir);void moveCheck(const std::string& dir);void interopRingCheck(const std::string& dir);
    double lastPreMs=0,lastFrameMs=0;
    std::unique_ptr<render::OverlayScene> panOverlay;
    std::vector<ObjectRef> panOverlaySelection;
    uint64_t panOverlaySerial=0,panOverlayToken=0;
    double panOverlayScale=0,panOverlayDpi=0;
    void render(){
        double tEnter=now();lastMovePaintMs=tEnter;if(pan&&!std::getenv("MOLECULE_STUDIO_PAN_LEGACY"))updateScrollbars();
        if(insertionPreviewPending){insertionPreviewPending=false;if(fastGraphicTool()&&s.activeIndex==insertionPreviewTool){route(interaction::InputKind::PointerMove,MAKELPARAM(int(insertionPreviewPixel.x),int(insertionPreviewPixel.y)));if(down)error(s.gesture(PointerPhase::Move,insertionPreviewPoint,insertionPreviewModifiers));}}
        if(patternPreviewPending){patternPreviewPending=false;if(patternPlacing&&patternDragging)updatePatternPreview();}
        if(movePreviewPending){movePreviewPending=false;if(down&&s.objectDragActive())error(s.gesture(PointerPhase::Move,movePreviewPoint,movePreviewModifiers));}
        if(textEditor.active&&!textBackdropBuilding&&renderTextBackdrop())return;
        if(!textEditor.active)textBackdrop.clear();
        if(!marqueeBuilding&&fastMarquee()&&renderMarquee())return;
        if(!s.selectionDragActive())marqueeBackdrop.clear();
        if(!gl)return;if(rc&&!wglMakeCurrent(dc,rc)){status="无法恢复绘图上下文";return;}
#ifdef MOLECULE_STUDIO_DX11
        if(dx11Renderer&&!transformDragging&&!s.objectDragActive()){static const std::vector<dx11::Renderer::RotationTarget> empty;dx11Renderer->setRotationPreview(nullptr,empty,nullptr,0);}
        if(dx11Renderer)dx11Renderer->setInsertionPreview((patternDragging||(down&&fastGraphicTool()))&&!std::getenv("MOLECULE_STUDIO_INSERT_LEGACY"));
        if(dx11Renderer)dx11Renderer->setSceneRevision(&s.gpuScene,s.serial);
        if(dx11Renderer)dx11Renderer->setCopyPreview(cloneDragging&&cloneSource?&cloneSource->scene:nullptr,cloneDelta,cloneSource?cloneSource->doc.id():0);
#endif
        lastPreMs=now()-tEnter;
        double t0=now();
        const bool stablePan=pan&&!std::getenv("MOLECULE_STUDIO_PAN_LEGACY");
        render::OverlayScene transientOverlay;
        if(stablePan){
            auto v=s.view();
            if(!panOverlay||panOverlaySerial!=s.serial||panOverlaySelection!=s.selected||panOverlayScale!=v.scale||panOverlayDpi!=v.dpiScale){
                panOverlay.reset(new render::OverlayScene(s.overlay()));
                panOverlaySerial=s.serial;panOverlaySelection=s.selected;panOverlayScale=v.scale;panOverlayDpi=v.dpiScale;++panOverlayToken;
            }
        }else{panOverlay.reset();transientOverlay=s.overlay();}
        auto& o=stablePan?*panOverlay:transientOverlay;
#ifdef MOLECULE_STUDIO_DX11
        if(dx11Renderer)dx11Renderer->setPanOverlay(stablePan?&o.scene():nullptr,panOverlayToken);
#endif
        double t1=now();
        render::RenderStats stats;
        if(textEditor.active){auto scene=editingScene();double t2=now();lastSceneMs=t2-t1;error(gl->draw(scene,&o.scene(),s.renderView,&stats));}
        else if(transformDragging){bool applied=false;
#ifdef MOLECULE_STUDIO_DX11
            if(dx11Renderer&&rotationGpuEligible&&transformHandle==8){const double matrix[]={transformMatrix.m00,transformMatrix.m01,transformMatrix.m02,transformMatrix.m10,transformMatrix.m11,transformMatrix.m12};dx11Renderer->setRotationPreview(&s.gpuScene,rotationTargets,matrix,rotationGesture);lastSceneMs=now()-t1;error(gl->draw(s.gpuScene,nullptr,s.renderView,&stats));applied=dx11Renderer->rotationPreviewApplied();}
#endif
            if(!applied){const auto& preview=selectionTransformScene();lastSceneMs=now()-t1;error(gl->draw(preview,nullptr,s.renderView,&stats));}}
        else if(tableDragPending&&tableDragActive){const auto& preview=tableDragScene();double t2=now();lastSceneMs=t2-t1;error(gl->draw(preview,nullptr,s.renderView,&stats));}
        else if(cloneDragging&&cloneSource){
#ifdef MOLECULE_STUDIO_DX11
            if(dx11Renderer){lastSceneMs=now()-t1;error(gl->draw(s.gpuScene,&o.scene(),s.renderView,&stats));}else
#endif
            {auto preview=clonePreviewOverlay(o.scene());lastSceneMs=now()-t1;error(gl->draw(s.gpuScene,&preview,s.renderView,&stats));}}
        else {
            bool applied=false;
#ifdef MOLECULE_STUDIO_DX11
            Vec2 delta;std::vector<uint64_t> keys;
            if(dx11Renderer&&!std::getenv("MOLECULE_STUDIO_MOVE_GPU_LEGACY")&&s.rigidDragPreview(&delta,&keys)){
                std::vector<dx11::Renderer::RotationTarget> targets;uint64_t token=s.serial+1469598103934665603ull;
                for(auto key:keys){targets.emplace_back(key,1,Vec2());token=(token^key)*1099511628211ull;}
                const double matrix[]={1,0,delta.x,0,1,delta.y};dx11Renderer->setRotationPreview(&s.gpuScene,targets,matrix,token);
                lastSceneMs=now()-t1;error(gl->draw(s.gpuScene,&o.scene(),s.renderView,&stats));applied=dx11Renderer->rotationPreviewApplied();
            }
#endif
            if(!applied){const auto& scene=s.displayScene();double t2=now();lastSceneMs=t2-t1;error(gl->draw(scene,&o.scene(),s.renderView,&stats));}
        }
        lastStats=stats;double t3=now();lastOverlayMs=t1-t0;lastDrawMs=t3-(t1+lastSceneMs);
#ifdef MOLECULE_STUDIO_DX11
        if(dx11Renderer)dx11Renderer->present(dc);else
#endif
        if(compatibleRenderer)compatibleRenderer->present(dc);else SwapBuffers(dc);
        double t4=now();lastPresentMs=t4-t3;canvasGuides();double t5=now();lastGuidesMs=t5-t4;lastFrameMs=t5-tEnter;
        if(std::getenv("MOLECULE_STUDIO_PERF_TRACE")&&lastFrameMs>100)
            std::fprintf(stderr,"[frame] total=%.1f pre=%.1f ov=%.1f scene=%.1f draw=%.1f pres=%.1f guides=%.1f\n",lastFrameMs,lastPreMs,lastOverlayMs,lastSceneMs,lastDrawMs,lastPresentMs,lastGuidesMs);
    }
    void route(interaction::InputKind k,LPARAM lp,uint32_t key=0,bool repeat=false){interaction::InputEvent e;e.kind=k;e.devicePosition=Vec2(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));e.focus=interaction::InputFocus::Canvas;e.modifiers=mods();e.button=kButtonLeft;e.virtualKey=key;e.repeat=repeat;e.character=k==interaction::InputKind::Character?key:0;e.documentId=s.doc.id();e.viewGeneration=s.viewSerial;if(k==interaction::InputKind::Character||k==interaction::InputKind::KeyDown)s.input.refreshHotspot(&s);s.input.handle(e,&s);if(s.editAtom!=kInvalidObjectId){auto id=s.editAtom;s.editAtom=kInvalidObjectId;openText(kInvalidObjectId,id);}}
    std::string file(bool save,bool xml=false){wchar_t buf[32768]={};OPENFILENAMEW f={};f.lStructSize=sizeof(f);f.hwndOwner=h;f.lpstrFile=buf;f.nMaxFile=32768;f.lpstrFilter=save?(xml?L"ChemDraw XML\0*.cdxml\0\0":L"Molecule 文档\0*.r62doc\0\0"):L"结构文档\0*.cdxml;*.r62doc;*.t01doc\0所有文件\0*.*\0\0";f.lpstrDefExt=save?(xml?L"cdxml":L"r62doc"):0;f.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);s.cancel();s.input.setFocus(interaction::InputFocus::ModalDialog);BOOL ok=save?studio_i18n::saveFileName(&f):GetOpenFileNameW(&f);s.input.setFocus(interaction::InputFocus::Canvas);return ok?U(buf):"";}
    bool save(bool xml=false,bool saveAs=false){if(IsWindowVisible(tableEditor.hwnd)&&tableEditor.valid()&&!tableEditor.commit())return false;if(textEditor.active&&!finishText(true))return false;
        if(embedded&&oleObject&&!saveAs){oleObject->markDirty();bool ok=oleObject->saveNow();status=ok?"已更新到容器文档":"无法写入容器存储";if(ok)s.dirty=false;return ok;}
        std::string p=saveAs||s.sourcePath.empty()?file(true,xml):s.sourcePath;if(p.empty())return false;Error e;if(!saveAs&&!s.sourcePath.empty()){std::string bytes;std::string suffix=p.size()>=6?p.substr(p.size()-6):p;for(auto& ch:suffix)ch=char(tolower((unsigned char)ch));bool xmlCurrent=suffix==".cdxml";if(xmlCurrent)e=studioio::exportXml(s.doc,&bytes);else{std::ostringstream out;e=integration::saveSafeNative(s.doc,out);bytes=out.str();}if(e.ok())e=persistence::writeFileAtomic(p,bytes);if(e.ok())s.dirty=false;}else e=s.save(p,xml);error(e);if(e.ok()){s.sourcePath=p;fileName=p.substr(p.find_last_of("/\\")+1);status="已保存";}return e.ok();}
    bool leave(){if(IsWindowVisible(tableEditor.hwnd)&&tableEditor.valid()&&!tableEditor.commit())return false;if(textEditor.active&&!finishText(true))return false;if(!s.dirty)return true;int a=studio_i18n::messageBox(h,L"保存当前结构后继续？",L"未保存的结构",MB_YESNOCANCEL|MB_ICONQUESTION);return a==IDNO||(a==IDYES&&save(true));}
    void openText(ObjectId t,ObjectId atom=kInvalidObjectId){textEditor.begin(t,atom,s.view().center);edit=textEditor.hwnd;textBackdrop.clear();status="文字编辑 · Enter / Esc 保存，Shift+Enter 换行";invalidate();}
    // The atom a text-tool click or a typed character labels. A click on a bond
    // endpoint labels that endpoint's atom exactly like a double-click on the
    // atom (pick() prefers the bond near its ends, so the endpoint is resolved
    // from the bond record). When useSelection is set, a pointer that hits
    // nothing falls back to the selection, so "select the endpoint, switch to
    // the text tool, type" lands in that atom's label editor.
    ObjectId textToolTarget(Vec2 point,bool useSelection){
        auto nearestEnd=[&](const BondRecord& bond){auto a=s.doc.atoms().get(bond.beginAtom)->position,b=s.doc.atoms().get(bond.endAtom)->position;return core::length(a-point)<=core::length(b-point)?bond.beginAtom:bond.endAtom;};
        auto hit=s.pick(point);
        if(hit.kind==ObjectKind::Atom)return hit.id;
        if(hit.kind==ObjectKind::Bond){if(const BondRecord* bond=s.doc.bonds().alive(hit.id))return nearestEnd(*bond);}
        if(!useSelection)return kInvalidObjectId;
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id))return ref.id;
        for(auto ref:s.selected)if(ref.kind==ObjectKind::Bond){if(const BondRecord* bond=s.doc.bonds().alive(ref.id))return nearestEnd(*bond);}
        return kInvalidObjectId;
    }
    bool finishText(bool accept){auto result=textEditor.finish(accept);textBackdrop.clear();error(result);invalidate();return result.ok();}
    void demo(){s.cancel();s.doc=Document();drawingSettings.apply(s.doc);s.history.clear();s.selected.clear();std::vector<ObjectId> atoms;for(int i=0;i<6;i++){ObjectId a;double t=6.2831853*i/6;s.doc.addAtom(Vec2(45*cos(t),45*sin(t)),6,&a);atoms.push_back(a);}for(int i=0;i<6;i++){ObjectId b;s.doc.addBond(atoms[i],atoms[(i+1)%6],&b);if(i%2==0)s.doc.setBondOrder(b,BondOrder::Double);}
        ObjectId a;
        s.doc.addText(Vec2(-46,82),11,"Benzene",&a);s.selected.push_back(ObjectRef(ObjectKind::Atom,atoms[0]));s.input.bindDocument(s.doc.id());s.refresh();s.fit();auto view=s.view();view.scale=1.3;s.setView(view);s.dirty=false;fileName="苯 · 示例";status="点选结构，右上角分子信息自动更新";}
    void initializeTextControls();
    void syncTextControls(){if(!fontBox||syncingTextControls)return;syncingTextControls=true;std::string family=textEditor.nextFamily;double points=textEditor.nextSize;if(textEditor.active){CHARFORMAT2W c={};c.cbSize=sizeof(c);studio_i18n::sendMessage(edit,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&c);family=U(c.szFaceName);points=c.yHeight/20.0;}else{for(auto id:selectedTexts()){auto text=s.doc.texts().alive(id);if(text){family=text->format.fontName.empty()?s.doc.defaultFont():text->format.fontName;points=textBaseSize(s.doc,id);auto runs=s.doc.runs(id);if(!runs.empty()&&!runs.front().fontName.empty())family=runs.front().fontName;break;}}for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id)){family=s.doc.defaultFont();points=10;if(auto v=s.doc.documentExtensions().findValue("studio.style.LabelSize"))points=std::atof(v->c_str());auto runs=studioatom::runs(s.doc,ref.id);if(!runs.empty()){if(!runs.front().fontName.empty())family=runs.front().fontName;if(runs.front().size>0)points=runs.front().size;}break;}}wchar_t num[32];swprintf(num,32,L"%.1f",points);std::wstring number=num;if(number.size()>2&&number.substr(number.size()-2)==L".0")number.resize(number.size()-2);for(auto item:{std::make_pair(fontBox,W(family)),std::make_pair(sizeBox,number)}){COMBOBOXINFO cb={};cb.cbSize=sizeof(cb);GetComboBoxInfo(item.first,&cb);if(GetFocus()==item.first||GetFocus()==cb.hwndItem)continue;wchar_t old[256]={};studio_i18n::getWindowText(item.first,old,256);if(item.second!=old)studio_i18n::setWindowText(item.first,item.second.c_str());}syncingTextControls=false;}
    void styleFromBox(HWND box,bool selected){if(syncingTextControls)return;syncingTextControls=true;wchar_t value[256]={};int index=int(studio_i18n::sendMessage(box,CB_GETCURSEL,0,0));if(selected&&index>=0)studio_i18n::sendMessage(box,CB_GETLBTEXT,index,(LPARAM)value);else studio_i18n::getWindowText(box,value,256);bool face=box==fontBox;std::string name=face?U(value):"";wchar_t* end=nullptr;double points=face?0:wcstod(value,&end);if((face&&name.empty())||(!face&&(!end||*end||points<4||points>200))){syncingTextControls=false;syncTextControls();return;}textEditor.setFont(name,points);if(!textEditor.active){std::vector<ObjectId> texts=selectedTexts();if(!texts.empty())error(s.submit(Command("字体与字号",kInvalidateAppearance|kInvalidateGeometry,[texts,name,points,face](CommandContext& c){for(auto id:texts){auto runs=c.document.runs(id);for(auto& run:runs)if(face)run.fontName=name;else run.size=0;auto e=c.document.setTextRuns(id,runs);if(!e.ok())return CommandResult::fail(e.code,e.message);if(face){auto f=c.document.texts().get(id)->format;f.fontName=name;e=c.document.setTextFormat(id,f);}else e=c.document.setTextSize(id,points);if(!e.ok())return CommandResult::fail(e.code,e.message);}return CommandResult::success();}),"字体与字号"));}syncingTextControls=false;invalidate();}
    void execute(const Button& b);
    void fitPage(){auto p=studioio::Page::get(s.doc);auto v=s.view();v.center=Vec2(p.x+p.width*p.across*.5,p.y+p.height*p.down*.5);v.scale=.92*std::min(s.renderView.widthPx/(p.width*p.across*v.dpiScale),s.renderView.heightPx/(p.height*p.down*v.dpiScale));s.setView(v);}
    bool publishOleClipboard(const Document& subset,const std::string& native,const std::string& xml);
    void touched();
    void beginEmbedded(ole::DocumentObject& object);
    void finishEmbedded();
    void screenshot(const std::string& path,bool chrome=true,bool statusBar=true);
    Error copySelectionPng();
    void probe(const std::string& dir);
    void selftest();
};

// Hover popup for "the structure this caption names has changed". A plain popup
// window (same idea as the tab tooltip in tabs_host.hpp) because the caption is
// canvas content, not a child control.
LRESULT CALLBACK nameLinkTipProc(HWND hwnd,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_PAINT){
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        HBRUSH bg=CreateSolidBrush(RGB(254,244,244));FillRect(dc,&rc,bg);DeleteObject(bg);
        FrameRect(dc,&rc,(HBRUSH)GetStockObject(GRAY_BRUSH));
        wchar_t text[512]={};GetWindowTextW(hwnd,text,512);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(176,32,32));
        HGDIOBJ old=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
        RECT inner={7,4,rc.right-7,rc.bottom-4};
        DrawTextW(dc,text,-1,&inner,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(dc,old);EndPaint(hwnd,&ps);return 0;
    }
    if(m==WM_ERASEBKGND)return 1;
    return DefWindowProcW(hwnd,m,wp,lp);
}

static bool captureCanvasPixels(HWND hwnd,std::vector<uint8_t>& rgba,int& w,int& h){
    RECT rc={};if(!GetClientRect(hwnd,&rc))return false;
    w=rc.right-rc.left;h=rc.bottom-rc.top;if(w<=0||h<=0)return false;
    HDC win=GetDC(hwnd);if(!win)return false;
    HDC mem=CreateCompatibleDC(win);HBITMAP bmp=CreateCompatibleBitmap(win,w,h);
    if(!mem||!bmp){if(mem)DeleteDC(mem);if(bmp)DeleteObject(bmp);ReleaseDC(hwnd,win);return false;}
    HGDIOBJ old=SelectObject(mem,bmp);
    // PW_RENDERFULLCONTENT: capture the composed window, not just the invalid region.
    BOOL ok=PrintWindow(hwnd,mem,2);
    if(!ok)ok=BitBlt(mem,0,0,w,h,win,0,0,SRCCOPY);
    BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    std::vector<uint8_t> bgra(size_t(w)*size_t(h)*4);
    if(ok)ok=GetDIBits(mem,bmp,0,h,bgra.data(),&bi,DIB_RGB_COLORS)!=0;
    SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(hwnd,win);
    if(!ok)return false;
    rgba.resize(bgra.size());
    for(size_t i=0;i<bgra.size();i+=4){rgba[i]=bgra[i+2];rgba[i+1]=bgra[i+1];rgba[i+2]=bgra[i];rgba[i+3]=bgra[i+3];}
    return true;
}
bool App::publishOleClipboard(const Document& subset,const std::string& native,const std::string& xml){
    // Another installed/portable build may have replaced this CLSID since this
    // window was opened. A newly copied object must reopen the current editor.
    const bool embeddingAvailable=ole::ensureRegistered(); // Chemical formats do not require COM server registration.
    Session temp;temp.doc=subset;
    temp.renderView.transform.scale=1;temp.renderView.transform.dpiScale=1;
    temp.refresh();
    Box bounds=temp.scene.bounds();
    for(const auto& a:subset.atoms().records())if(a.isAlive())bounds.add(a.position);
    if(bounds.empty()){s.status="选择的内容没有可嵌入的绘图范围";return false;}
    const double himetricPerPoint=2540.0/72.0;
    // Use actual rendered ink, including glyph bearings, with a 2 pt border.
    SIZE extent;extent.cx=LONG((bounds.width()+4)*himetricPerPoint);extent.cy=LONG((bounds.height()+4)*himetricPerPoint);
    if(extent.cx<100)extent.cx=100;if(extent.cy<100)extent.cy=100;
    ole::DocumentObject* object=new ole::DocumentObject();
    if(!embeddingAvailable)object->setFormatMask(4|8|16|32);
    Document embeddedDocument=subset;embeddedDocument.documentExtensions().set("studio.ole.fixed-preview","1");
    std::ostringstream embeddedBytes;integration::saveSafeNative(embeddedDocument,embeddedBytes);
    object->setContents(embeddedBytes.str(),false);
    object->setExtentHimetric(extent);
    UINT dpi=GetDpiForWindow(h);olepreview::previewDpi()=dpi?dpi:96;olepreview::previewMeasurer()=s.measurer.get();
    // Clipboard and reopened servers share the same complete vector renderer.
    object->setPreview([](const std::string& bytes,SIZE size){return olepreview::fromNative(bytes,size);});
    // Structural data belongs in chemical formats, not plain text: Office
    // otherwise prefers the XML source over the OLE/vector representation.
    object->setSerializers(nullptr,[xml]{return xml;},nullptr);
    HRESULT setHr=OleSetClipboard(static_cast<IDataObject*>(object));
    if(FAILED(setHr)){ole::logf("publishOleClipboard OleSetClipboard hr=0x%08lx",(unsigned long)setHr);s.status="无法发布可编辑的 OLE 对象，剪贴板可能正被其他程序占用，请重试";object->Release();return false;}
    // Keep the live IDataObject on the clipboard. A flushed/static clipboard makes
    // Word fall back to its CF_ENHMETAFILE and insert a picture (InlineShape.Type=3),
    // which cannot activate the server. The live object lets Word create a real
    // embedded object and double-click runs our -Embedding server. ChemDraw also
    // only flushes when the app exits or another app takes the clipboard.
    if(g_oleClipboard){g_oleClipboard->Release();g_oleClipboard=nullptr;}
    g_oleClipboard=object;
    ole::logf("publishOleClipboard live set=0x%08lx",(unsigned long)setHr);
    return true;
}
void App::touched(){if(embedded&&oleObject&&s.dirty)oleObject->markDirty();}
std::string App::nameLinkTipAt(Vec2 documentPoint){
    // Only captions bound to a structure whose digest no longer matches report
    // the change; hit testing uses the same generated text box the user sees.
    const std::vector<ObjectId> stale=namelink::staleTexts(s.doc);
    for(size_t i=0;i<stale.size();++i){
        Box bounds(Vec2(1,1),Vec2(-1,-1));
        for(const auto& primitive:s.scene.primitives())if(primitive.source==ObjectRef(ObjectKind::Text,stale[i])&&!primitive.bounds.empty()){bounds.add(primitive.bounds.min);bounds.add(primitive.bounds.max);}
        if(bounds.empty()){
            const TextRecord* text=s.doc.texts().alive(stale[i]);if(!text)continue;
            double size=text->size>0?text->size:s.doc.defaultTextSize();
            bounds=Box(text->position,text->position+Vec2(std::max(8.,size*2.),std::max(4.,size*1.2)));
        }
        const double pad=std::max(1.5,s.view().deviceRadiusToDocument(3));
        if(studioio::inside(bounds,documentPoint,pad))return std::string(namelink::staleMessage());
    }
    return std::string();
}
void App::hideNameLinkTip(){if(nameLinkTip)ShowWindow(nameLinkTip,SW_HIDE);}
void App::updateNameLinkTip(Vec2 documentPoint){
    const std::string tip=nameLinkTipAt(documentPoint);
    if(tip.empty()){if(!nameLinkTipText.empty()){nameLinkTipText.clear();hideNameLinkTip();}return;}
    const std::wstring wide=W(tip);
    if(wide==nameLinkTipText){if(nameLinkTip&&!IsWindowVisible(nameLinkTip))ShowWindow(nameLinkTip,SW_SHOWNOACTIVATE);return;}
    nameLinkTipText=wide;
    if(!nameLinkTip){
        static bool registered=false;
        if(!registered){WNDCLASSW wc={};wc.lpfnWndProc=nameLinkTipProc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"MoleculeStudioNameLinkTip";RegisterClassW(&wc);registered=true;}
        nameLinkTip=studio_i18n::createWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"MoleculeStudioNameLinkTip",L"",WS_POPUP,0,0,10,10,h,nullptr,GetModuleHandleW(nullptr),nullptr);
    }
    if(!nameLinkTip)return;
    SetWindowTextW(nameLinkTip,wide.c_str());
    HDC dc=GetDC(nameLinkTip);SIZE extent={};HGDIOBJ old=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32W(dc,wide.c_str(),int(wide.size()),&extent);
    SelectObject(dc,old);ReleaseDC(nameLinkTip,dc);
    POINT cursor={};GetCursorPos(&cursor);
    SetWindowPos(nameLinkTip,HWND_TOPMOST,cursor.x+12,cursor.y+22,extent.cx+16,extent.cy+12,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(nameLinkTip,nullptr,TRUE);
}
bool App::askPasteFormat(const studioio::DrawingFormat& source,const studioio::DrawingFormat& target,bool* adoptSource){
    // A headless run must never open a modal window it cannot dismiss.
    if(testing||!IsWindow(canvas))return false;
    std::wstring text=L"从外部粘贴的内容带有与当前文档不同的默认格式。\n\n复制源格式："+W(studioio::drawingFormatSummary(source))+L"\n当前文档格式："+W(studioio::drawingFormatSummary(target))+L"\n\n是否采用复制源的格式？\n「是」= 采用复制源格式（默认）\n「否」= 保留当前文档格式";
    int choice=studio_i18n::messageBox(canvas,text.c_str(),L"粘贴格式",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON1);
    if(choice==IDYES){*adoptSource=true;return true;}
    if(choice==IDNO){*adoptSource=false;return true;}
    return false;
}
void App::beginEmbedded(ole::DocumentObject& object){
    embedded=false;oleObject=&object;
    std::string bytes=object.contents();
    s.cancel();s.whiteCanvas=true;s.doc=Document();drawingSettings.apply(s.doc);s.graphicsWidth=0;s.history.clear();s.selected.clear();s.sourcePath.clear();
    if(!bytes.empty()){
        std::istringstream in(bytes);
        auto e=integration::loadSafeNative(s.doc,in);
        if(!e.ok())status="嵌入对象读取失败："+e.message;
    }
    s.input.bindDocument(s.doc.id());s.refresh();s.fit();s.dirty=false;
    fileName="嵌入对象";status="编辑嵌入对象 · 关闭窗口即写回容器文档";
    if(object.extent().cx<=0){
        Box bounds=studioio::selectionBounds(s);
        if(!bounds.empty()){
            SIZE size;size.cx=LONG((bounds.width()+8)*2540/72);size.cy=LONG((bounds.height()+8)*2540/72);
            object.setExtentHimetric(size);
        }
    }
    // OLE containers may size the server window to the object's small extent.
    // The full ribbon needs a normal editor window; otherwise the canvas is
    // clipped and the document looks read-only. Size and re-fit before showing.
    RECT work={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int ww=std::min(1400,int(work.right-work.left)-80);
    int hh=std::min(900,int(work.bottom-work.top)-80);
    if(ww<900)ww=900; if(hh<620)hh=620;
    int wx=work.left+(int(work.right-work.left)-ww)/2;
    int wy=work.top+(int(work.bottom-work.top)-hh)/2;
    SetWindowPos(h,0,wx,wy,ww,hh,SWP_NOZORDER);
    layout();s.fit();
    ShowWindow(h,SW_SHOW);
    // Some OLE containers restore the server to the object's extent after the
    // first show; force the editor size again once the window is visible.
    SetWindowPos(h,0,wx,wy,ww,hh,SWP_NOZORDER|SWP_FRAMECHANGED);
    layout();s.fit();
    SetForegroundWindow(h);SetFocus(canvas);UpdateWindow(h);invalidate();
    object.setSerializers([this]{std::ostringstream out;integration::saveSafeNative(s.doc,out);return out.str();},
                          [this]{std::string xml;studioio::exportXml(s.doc,&xml);return xml;},
                          [this]{return info.plain;});
    embedded=true;
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_OLE_CYCLE_TEST",nullptr,0))SetTimer(h,94,120,nullptr);
}
void App::finishEmbedded(){
    if(textEditor.active)finishText(true);
    if(oleObject){if(!oleObject->requestCloseFromEditor()){status="写回失败，请重试保存；编辑窗口已保留";invalidate();return;}oleObject=nullptr;}
    PostQuitMessage(0);
}
struct StudioOleHost:ole::Host{
    App* app=nullptr;
    void activateDocument(ole::DocumentObject& object)override{App* target=app?app:g_currentApp;if(target)target->beginEmbedded(object);}
    void deactivateDocument()override{App* target=app?app:g_currentApp;if(target&&target->h)ShowWindow(target->h,SW_HIDE);}
    void objectClosed()override{PostQuitMessage(0);}
};
LRESULT CALLBACK fontEntryProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){auto a=(App*)data;if(m==WM_KEYDOWN&&(w==VK_RETURN||w==VK_ESCAPE)){if(w==VK_RETURN)a->styleFromBox(GetParent(h),false);SetFocus(a->textEditor.active?a->edit:a->canvas);return 0;}return DefSubclassProc(h,m,w,l);}
#include "hydrogen_cycle.hpp"
#include "app_actions.hpp"
#include "arrange_actions.hpp"
#include "align_check.hpp"
#include "radical_actions.hpp"
#include "chemistry_actions.hpp"
#include "conformer3d.hpp"
#include "editor_hotkeys.hpp"
#include "extra_actions.hpp"
#include "canvas_guides.hpp"
#include "ai_actions.hpp"
#include "ocsr_review_edit.hpp"
static void styleOcsr(Session& source,const Session& canvas);
static void initializeReviewEditor(App& app,HWND parent);

#include "ocsr_dialog.hpp"
#include "table_actions.hpp"
#include "ring_library_ui.hpp"
#include "ring_library_actions.hpp"
#include "phenalene_actions.hpp"
#include "selection_transform.hpp"
#include "text_preview.hpp"
#include "marquee_preview.hpp"
#include "move_checks.hpp"
#include "interop_ring_checks.hpp"
#include "presentation_actions.hpp"
#include "bio_actions.hpp"
#include "custom_templates.hpp"
#include "tlc_actions.hpp"
#include "tabs_host.hpp"
#include "nmr_tab.hpp"
void App::showOcsr(){
    if(!ocsrRuntimeAvailable()){status="未安装图片转结构组件（缺少 ocsr_runtime 文件夹）";invalidate();return;}
    if(textEditor.active&&!finishText(true))return;
    ocsr::Dialog::show(h,[this](const std::string& cdxml,const std::string& record,std::wstring& error){return insertOcsrStructure(cdxml,record,error);},[this](const std::string& cdxml,std::wstring& error){return copyOcsrStructure(cdxml,error);},[this](Session& preview){styleOcsr(preview,s);});
}
void App::showOcsrDemo(){
    wchar_t pdf[32768]={};
    if(!GetEnvironmentVariableW(L"MOLECULE_STUDIO_OCSR_DEMO_PDF",pdf,32768)||!pdf[0])return;
    wchar_t text[128]={};
    int page=6;
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_OCSR_DEMO_PAGE",text,128))page=std::max(1,_wtoi(text));
    int cropX=0,cropY=0,cropW=0,cropH=0;
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_OCSR_DEMO_CROP",text,128))swscanf(text,L"%d,%d,%d,%d",&cropX,&cropY,&cropW,&cropH);
    if(cropW<=0||cropH<=0)return;
    bool doInsert=GetEnvironmentVariableW(L"MOLECULE_STUDIO_OCSR_DEMO_INSERT",text,128)!=0;
    ocsr::Dialog::showDemo(h,[this](const std::string& cdxml,const std::string& record,std::wstring& error){
        return insertOcsrStructure(cdxml,record,error);
    },pdf,page,cropX,cropY,cropW,cropH,doInsert,[this](Session& preview){styleOcsr(preview,s);});
}

// Recognition coordinates are in image units. Apply one uniform scale so ring
// geometry/stereobonds survive, then use the active canvas drawing style.
static void styleOcsr(Session& source,const Session& canvas){
    // Reviewed output is already styled. Preserve the edited geometry and
    // appearance even if the main canvas defaults changed while it was open.
    if(source.doc.documentExtensions().findValue("pst.attr.MoleculeStudioOcsrReviewed"))return;
    std::vector<double> lengths;
    for(const auto& b:source.doc.bonds().records())if(b.isAlive()){
        auto a=source.doc.atoms().alive(b.beginAtom),z=source.doc.atoms().alive(b.endAtom);
        if(a&&z){double n=core::length(z->position-a->position);if(n>1e-6)lengths.push_back(n);}
    }
    double factor=1;
    if(!lengths.empty()){std::sort(lengths.begin(),lengths.end());factor=canvas.doc.defaultBondLength()/lengths[lengths.size()/2];}
    Box box;for(const auto& a:source.doc.atoms().records())if(a.isAlive())box.add(a.position);
    Vec2 center=box.empty()?Vec2():(box.min+box.max)*.5;
    source.doc.setDefaultBondLength(canvas.doc.defaultBondLength());
    double width=canvas.graphicsWidth>0?canvas.graphicsWidth:canvas.doc.defaultLineWidth();
    source.doc.setDefaultLineWidth(width);source.doc.setDefaultFont(canvas.doc.defaultFont());source.doc.setDefaultTextSize(canvas.doc.defaultTextSize());
    for(auto key:{"LabelSize","LabelFont","BoldWidth","HashSpacing","BondSpacing","MarginWidth"}){
        auto name=std::string("studio.style.")+key;source.doc.documentExtensions().erase(name);
        if(auto value=canvas.doc.documentExtensions().findValue(name))source.doc.documentExtensions().set(name,*value);
    }
    for(const auto& a:source.doc.atoms().records())if(a.isAlive()){
        source.doc.setAtomPosition(a.id,center+(a.position-center)*factor);
        auto ref=ObjectRef(ObjectKind::Atom,a.id);auto& ext=source.doc.extensions(ref);
        ext.erase("studio.label.offset");ext.erase("studio.label.text");ext.erase("studio.atom.source.xy");ext.erase("studio.atom.runs");
        studio::graphic_style::apply(source.doc,ref,width,canvas.graphicsColor,false,true);
    }
    for(const auto& b:source.doc.bonds().records())if(b.isAlive())studio::graphic_style::apply(source.doc,ObjectRef(ObjectKind::Bond,b.id),width,canvas.graphicsColor,true,true);
    source.graphicsWidth=canvas.graphicsWidth;source.graphicsColor=canvas.graphicsColor;
    source.doc.documentExtensions().set("pst.attr.MoleculeStudioOcsrReviewed","1");
}

bool App::insertOcsrStructure(const std::string& cdxmlPath,const std::string& recordPath,std::wstring& error){
    (void)recordPath;
    FILE* resultFile=_wfopen(W(cdxmlPath).c_str(),L"rb");
    if(!resultFile){error=L"找不到识别结果 CDXML 文件";return false;}
    std::string cdxml;
    if(fseek(resultFile,0,SEEK_END)==0){long size=ftell(resultFile);if(size>0&&size<4*1024*1024){cdxml.resize(size_t(size));rewind(resultFile);if(fread(&cdxml[0],1,size_t(size),resultFile)!=size_t(size))cdxml.clear();}}
    fclose(resultFile);
    if(cdxml.empty()){error=L"识别结果文件为空或过大";return false;}
    if(cdxml.find("<CDXML")==std::string::npos){error=L"识别结果文件不是有效的 CDXML";return false;}
    Session preview;
    auto imported=persistence::readCdxml(cdxml,preview.doc);
    if(!imported.ok()){error=L"识别结果无法导入："+W(imported.message);return false;}
    bool reviewed=preview.doc.documentExtensions().findValue("pst.attr.MoleculeStudioOcsrReviewed")!=nullptr;
    if(preview.doc.atoms().size()==0&&!reviewed){error=L"识别结果没有原子，未插入";return false;}
    if(preview.doc.atoms().size()>400){error=L"识别结果超过 400 个原子，超出画布上限";return false;}
    if(!reviewed&&(preview.doc.texts().size()||preview.doc.arrows().size()||preview.doc.paths().size()||
       preview.doc.symbols().size()||preview.doc.bioShapes().size()||preview.doc.orbitals().size()||
       preview.doc.tables().size()||preview.doc.brackets().size())){
        error=L"识别结果含有非分子对象，已拒绝插入";return false;
    }
    styleOcsr(preview,s);
    auto styled=std::make_shared<Session>();styled->doc=preview.doc;
    auto inserted=std::make_shared<std::vector<ObjectRef>>();
    Vec2 center=s.view().center;
    auto submitted=s.submit(Command("插入文献识别结构",kInvalidateStructure|kInvalidateGeometry,[styled,center,inserted](CommandContext& c){
        Session source;source.doc=styled->doc;
        Box bounds;
        for(const auto& atom:source.doc.atoms().records())if(atom.isAlive())bounds.add(atom.position);
        if(bounds.empty()){source.refresh();bounds=source.scene.bounds();}
        if(bounds.empty())return CommandResult::fail(ErrorCode::InvalidArgument,"审核画布为空");
        Vec2 middle=(bounds.min+bounds.max)*.5;
        for(const auto& atom:source.doc.atoms().records())if(atom.isAlive())source.doc.setAtomPosition(atom.id,center+(atom.position-middle));
        auto appended=studioio::append(source.doc,c.document,source.all(),Vec2(),inserted.get());
        if(!appended.ok())return CommandResult::fail(appended.code,appended.message);
        return CommandResult::success();
    }),"插入文献识别结构");
    if(!submitted.ok()){error=L"插入失败（文档未改变）："+W(submitted.message);return false;}
    s.selected=*inserted;
    s.dirty=true;
    s.refresh();
    invalidate();
    status="已插入文献识别结构 · 可编辑 · Ctrl+Z 撤销";
    return true;
}
void App::initializeTextControls(){fontBox=studio_i18n::createWindowEx(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWN|WS_VSCROLL,0,0,100,240,h,(HMENU)401,GetModuleHandleW(0),0);sizeBox=studio_i18n::createWindowEx(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWN|WS_VSCROLL,0,0,60,240,h,(HMENU)402,GetModuleHandleW(0),0);studio_i18n::protect(fontBox);textUiFont=CreateFontW(-int(12*scale),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Arial");for(HWND box:{fontBox,sizeBox}){studio_i18n::sendMessage(box,WM_SETFONT,(WPARAM)textUiFont,TRUE);COMBOBOXINFO cb={};cb.cbSize=sizeof(cb);GetComboBoxInfo(box,&cb);SetWindowSubclass(cb.hwndItem,fontEntryProc,1,(DWORD_PTR)this);}for(auto f:{L"Arial",L"Times New Roman",L"Calibri",L"Courier New",L"Microsoft YaHei",L"Symbol"})studio_i18n::sendMessage(fontBox,CB_ADDSTRING,0,(LPARAM)f);for(auto n:{L"8",L"9",L"10",L"11",L"12",L"14",L"16",L"18",L"20",L"24",L"28",L"32",L"36",L"48",L"72"})studio_i18n::sendMessage(sizeBox,CB_ADDSTRING,0,(LPARAM)n);syncTextControls();}
void App::showImageToStructure(){
    if(!ocsrRuntimeAvailable()){status="未安装图片转结构组件（缺少 ocsr_runtime 文件夹）";invalidate();return;}
    if(textEditor.active&&!finishText(true))return;
    wchar_t file[32768]={};
    // Acceptance hook: the picker is modal, so headless verification can name
    // the image directly (same idea as MOLECULE_STUDIO_OCSR_DEMO).
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_IMAGE_DEMO",file,32768)&&file[0]){openImageToStructure(file);return;}
    // Open the workspace first so clipboard import does not require a file.
    openImageToStructure(L"");
}
void App::openImageToStructure(const std::wstring& imagePath){
    if(!ocsrRuntimeAvailable()){status="未安装图片转结构组件（缺少 ocsr_runtime 文件夹）";invalidate();return;}
    ocsr::Dialog::showImage(h,[this](const std::string& cdxml,const std::string& record,std::wstring& error){
        return insertOcsrStructure(cdxml,record,error);
    },[this](const std::string& cdxml,std::wstring& error){
        return copyOcsrStructure(cdxml,error);
    },imagePath,[this](Session& preview){styleOcsr(preview,s);});
}
bool App::copyOcsrStructure(const std::string& cdxmlPath,std::wstring& error){
    FILE* resultFile=_wfopen(W(cdxmlPath).c_str(),L"rb");
    if(!resultFile){error=L"找不到识别结果 CDXML 文件";return false;}
    std::string cdxml;
    if(fseek(resultFile,0,SEEK_END)==0){long size=ftell(resultFile);if(size>0&&size<4*1024*1024){cdxml.resize(size_t(size));rewind(resultFile);if(fread(&cdxml[0],1,size_t(size),resultFile)!=size_t(size))cdxml.clear();}}
    fclose(resultFile);
    if(cdxml.empty()){error=L"识别结果文件为空或过大";return false;}
    if(cdxml.find("<CDXML")==std::string::npos){error=L"识别结果文件不是有效的 CDXML";return false;}
    Session temp;
    auto imported=persistence::readCdxml(cdxml,temp.doc);
    if(!imported.ok()){error=L"识别结果无法导入："+W(imported.message);return false;}
    styleOcsr(temp,s);
    if((cdxml=persistence::writeCdxmlToString(temp.doc)).empty()){error=L"无法应用画布样式";return false;}
    temp.refresh();
    temp.selected=temp.all();
    if(temp.selected.empty()){error=L"识别结果没有可复制的对象";return false;}
    auto copied=studioio::copy(temp,h,false);
    if(!copied.ok()){error=W(copied.message);return false;}
    // studioio::copy publishes the EMF/OLE payload (identical to the editor's own
    // copy) but deliberately omits plain text so Office prefers the vector form.
    // The 复制结构 button promises "paste anywhere", so add the CDXML text too.
    int wideLength=MultiByteToWideChar(CP_UTF8,0,cdxml.c_str(),-1,0,0);
    if(wideLength>0){
        std::wstring wide(size_t(wideLength),L'\0');
        MultiByteToWideChar(CP_UTF8,0,cdxml.c_str(),-1,&wide[0],wideLength);
        if(OpenClipboard(h)){
            HGLOBAL payload=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,wide.size()*sizeof(wchar_t));
            if(payload){
                void* locked=GlobalLock(payload);
                if(locked){memcpy(locked,wide.c_str(),wide.size()*sizeof(wchar_t));GlobalUnlock(payload);if(!SetClipboardData(CF_UNICODETEXT,payload))GlobalFree(payload);}
                else GlobalFree(payload);
            }
            CloseClipboard();
        }
    }
    status="已复制结构 · 可直接粘贴到 ChemDraw / Word / PowerPoint";
    invalidate();
    return true;
}
void App::execute(const Button& b){
    if(b.cmd=="@expandGroups"){expandGroups();return;}if(b.cmd=="@collapseGroups"){collapseGroups();return;}if(b.cmd=="@customGroups"){showSettings(6);return;}
    expandPending=false;
    if(patternPlacing)cancelPatternPlacement();
    if(ringPlacing)cancelRingPlacement();
    if(bioPlacing)cancelBioPlacement();
    if(phenalenePlacing)cancelPhenalenePlacement();
    radicalPlacing=false;
    cancelSelectionTransform();
    if(tableDragPending)cancelTableDrag();
    if(b.cmd=="@copyPng"){
        if(s.selected.empty()){status="请先选择要复制的结构";invalidate();return;}
        auto copyError=copySelectionPng();status=copyError.ok()?s.status:copyError.message;invalidate();return;
    }
    if(b.cmd.compare(0,7,"@table:")==0){tableCommand(b.cmd.substr(7));return;}
    if(b.cmd=="@tab"){switchTab(b.arg);return;}
    if(b.cmd=="@ribbonSize"){ribbonCompact=!ribbonCompact;ribbonVisible=true;saveRibbon();layout();invalidate();return;}
    if(b.cmd=="@ribbonPin"){ribbonPinned=!ribbonPinned;ribbonVisible=ribbonPinned;saveRibbon();status=ribbonPinned?"工具栏已锁定":"工具栏自动收起 · 点击分类展开，选择工具后收起";layout();invalidate();return;}
    if(b.cmd=="@ai"||b.cmd=="@aiSettings"||b.cmd=="@aiExample"){showAI(b.cmd=="@aiExample"?"绘制一个青蒿素":"",b.cmd=="@aiSettings");return;}
    if(b.cmd=="@ocsr"){showOcsr();return;}
    if(b.cmd=="@imageToStructure"){showImageToStructure();return;}
    if(b.cmd=="@windowMin"){ShowWindow(GetAncestor(h,GA_ROOT),SW_MINIMIZE);return;}
    if(b.cmd=="@windowMax"){HWND root=GetAncestor(h,GA_ROOT);ShowWindow(root,IsZoomed(root)?SW_RESTORE:SW_MAXIMIZE);return;}
    if(b.cmd=="@windowClose"){PostMessageW(GetAncestor(h,GA_ROOT),WM_CLOSE,0,0);return;}
    if(b.cmd=="@importCustomTemplate"||b.cmd=="@exportCustomTemplate"){transferCustomTemplate(b.cmd=="@importCustomTemplate");return;}
    if(b.cmd=="@addCustomTemplate"){addCustomTemplate();return;}
    if(b.cmd=="@customTemplates"){customTemplateMenu();return;}
    if(b.cmd=="@bioComponents"){bioMenu();return;}
    if(b.cmd=="@shapes"){if(textEditor.active)finishText(true);shapeMenu();return;}
    if(b.cmd=="@shapeFill"){shapeFillMenu();return;}
    if(b.cmd=="@graphicsWidth"||b.cmd=="@graphicsColor"){if(textEditor.active)finishText(true);graphicsStyleMenu(b.cmd=="@graphicsWidth");invalidate();return;}
    if(b.cmd=="@tlcMenu"){tlcMenu();return;}
    if(b.cmd=="@arrows"){if(textEditor.active)finishText(true);arrowMenu();invalidate();return;}
    // The configurable arrow shortcut resolves its tool command at press time.
    if(b.cmd=="@arrowTool"){execute(Button(b.id,b.label,b.icon,arrowCommand()));return;}
    if(b.cmd=="@plain"){if(textEditor.active){CHARFORMAT2W f={};f.cbSize=sizeof(f);f.dwMask=CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE|CFM_SUBSCRIPT|CFM_SUPERSCRIPT;studio_i18n::sendMessage(edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&f);textEditor.resize();SetFocus(edit);}else{auto refs=s.selected;error(s.submit(Command("常规文字",kInvalidateAppearance,[refs](CommandContext& c){for(auto r:refs)if(r.kind==ObjectKind::Text){auto runs=c.document.runs(r.id);for(auto& run:runs)run.flags=0;c.document.setTextRuns(r.id,runs);}return CommandResult::success();}),"常规文字"));}invalidate();return;}
    if(b.cmd=="@otherRings"){ringMenu();return;}
    if(b.cmd=="@ringLibrary"){ringMenu();return;}
    if(b.cmd=="@phenalene"){if(textEditor.active&&!finishText(true))return;phenalenePlacing=true;status="3a1H-菲烯：点击画布放置，Esc 取消";SetFocus(canvas);SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32515)));invalidate();return;}
    if(b.cmd=="@radical"){
        std::vector<ObjectId> atoms;
        bool grouped=false;for(auto ref:s.selected)if(ref.kind==ObjectKind::Group)grouped=true;
        if(grouped){for(auto ref:studioio::expanded(s.doc,s.selected))if(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id))atoms.push_back(ref.id);}
        else for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id))atoms.push_back(ref.id);
        if(atoms.empty()){auto hotspot=s.input.hotspot();if(hotspot.kind==interaction::HotspotKind::Atom&&s.doc.atoms().alive(hotspot.object.id))atoms.push_back(hotspot.object.id);}
        if(!atoms.empty()){applyRadical(atoms);return;}
        // Nothing selected: enter a one-click placement mode. The next canvas
        // click on an atom adds/clears its radical; Esc cancels.
        radicalPlacing=true;status="自由基：点击一个原子添加/取消，Esc 取消";SetFocus(canvas);updateCanvasCursor();invalidate();return;
    }
    if(textEditor.active&&(b.cmd=="@copy"||b.cmd=="@cut"||b.cmd=="@paste"||b.cmd=="@all"||b.cmd=="@undo"||b.cmd=="@redo")){if(b.cmd=="@all")textEditor.select(0,-1);else studio_i18n::sendMessage(edit,b.cmd=="@copy"?WM_COPY:b.cmd=="@cut"?WM_CUT:b.cmd=="@paste"?WM_PASTE:b.cmd=="@undo"?EM_UNDO:EM_REDO,0,0);textEditor.resize();SetFocus(edit);return;}
    bool formatting=b.cmd=="@flag"||b.cmd=="@size"||b.cmd=="@textalign"||b.cmd=="@font"||b.cmd=="@color";
    if(textEditor.active&&formatting){if(b.cmd=="@flag")textEditor.toggle(b.arg);else if(b.cmd=="@textalign")textEditor.align(b.arg);else if(b.cmd=="@size"){CHARFORMAT2W f={};f.cbSize=sizeof(f);studio_i18n::sendMessage(edit,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&f);textEditor.setFont("",std::max(4.,f.yHeight/20.0+b.arg));}else if(b.cmd=="@font"){LOGFONTW lf={};wcsncpy(lf.lfFaceName,W(textEditor.family).c_str(),LF_FACESIZE-1);CHOOSEFONTW cf={};cf.lStructSize=sizeof(cf);cf.hwndOwner=h;cf.lpLogFont=&lf;cf.Flags=CF_SCREENFONTS|CF_INITTOLOGFONTSTRUCT;if(ChooseFontW(&cf))textEditor.setFont(U(lf.lfFaceName),cf.iPointSize/10.);}else{uint32_t color;if(chooseStudioColor(h,&color))error(applyTextColor(color));}invalidate(false);return;}
    bool convertingName=textEditor.active&&b.cmd=="@nameToStructure";Vec2 nameAnchor=textEditor.anchor;
    if(convertingName){pendingName.clear();for(auto& run:textEditor.runs())pendingName+=run.utf8;}
    if(textEditor.active&&!finishText(true))return;
    if(convertingName)for(auto& text:s.doc.texts().records())if(text.isAlive()&&text.position==nameAnchor&&fullText(s.doc,text.id)==pendingName)s.selected={ObjectRef(ObjectKind::Text,text.id)};
    if(!b.enabled){status="该功能尚未接通："+b.label;invalidate();return;}status=b.label;
    if(b.cmd.empty())return;if(b.cmd[0]!='@'){s.cancel();down=pan=cloneDragging=selectionArrowEdit=textPlacing=false;cloneSource.reset();selectionCursorReset=true;if(GetCapture()==canvas)ReleaseCapture();if(!s.activateCommand(b.cmd))status="工具不可用："+b.label;SetFocus(canvas);updateCanvasCursor();if(!ribbonPinned){ribbonVisible=false;layout();}invalidate();return;}
    std::string op=b.cmd.substr(1);
    if(chemistryAction(op,b.arg)||extraAction(op,b.arg)){}
    else if(op=="color"){
        uint32_t color;if(chooseStudioColor(h,&color))error(applyTextColor(color));
    }
    else if(op=="settings")settingsMenu();
    else if(op=="copy"||op=="copyxml"||op=="cut"||op=="paste"){auto e=op=="paste"?studioio::paste(s,canvas):studioio::copy(s,canvas,op=="cut");status=e.ok()?s.status:e.message;}
    else if(op=="undo"){error(s.undo());if(IsWindowVisible(tableEditor.hwnd))tableEditor.refresh();}else if(op=="redo"){error(s.undo(true));if(IsWindowVisible(tableEditor.hwnd))tableEditor.refresh();}else if(op=="all")s.selected=s.all();else if(op=="fit")s.fit();
    else if(op=="open"){if(host){auto p=file(false);if(!p.empty())host->newTab(p);}else if(leave()){auto p=file(false);if(!p.empty()){auto e=s.open(p);error(e);if(e.ok()){fileName=p.substr(p.find_last_of("/\\")+1);textEditor.nextFamily=s.doc.defaultFont();textEditor.nextSize=s.doc.defaultTextSize();}else studio_i18n::messageBox(h,W(e.message).c_str(),L"打开失败",MB_OK|MB_ICONINFORMATION);}}}
    else if(op=="fileMenu")fileMenu();
    else if(op=="saveas")save(false,true);
    else if(op=="save"||op=="export")save(true,op!="save");else if(op=="demo"){if(leave())demo();}
    else if(op=="close"){if(host){host->closeTab(this);return;}if(leave()){s.cancel();s.doc=Document();drawingSettings.apply(s.doc);s.graphicsWidth=0;s.history.clear();s.selected.clear();s.sourcePath.clear();s.input.bindDocument(s.doc.id());s.refresh();s.dirty=false;textEditor.nextFamily=s.doc.defaultFont();textEditor.nextSize=s.doc.defaultTextSize();textEditor.nextFlags=0;fileName="未命名结构";}}
    else if(op=="actual"||op=="zoom"){auto v=s.view();v.scale=op=="actual"?1:std::max(.02,std::min(80.,v.scale*(b.arg>0?1.25:.8)));s.setView(v);}
    else if(op=="fixedLength"){s.dragFixedLength=!s.dragFixedLength;status=s.dragFixedLength?"锁定键长：开":"锁定键长：关";}
    else if(op=="fixedAngles"){s.fixedAngle=!s.fixedAngle;status=s.fixedAngle?"固定角度：开":"固定角度：关";}
    else if(op=="delete"){error(s.submit(commands::deleteObjects(s.selected),"删除"));s.selected.clear();}
    else if(op=="repeat"){error(s.history.repeat(s.doc));s.dirty=true;s.refresh();}
    else if(op=="nudge"){UINT key=UINT(b.arg&255);double step=(b.arg&256)?10:1;error(s.move(s.selected,Vec2(key==VK_LEFT?-step:key==VK_RIGHT?step:0,key==VK_UP?-step:key==VK_DOWN?step:0)));}
    else if(op=="rotateStep"||op=="rotateDialog"||op=="scaleDialog"){if(s.selected.empty()){status="请先选择结构";invalidate();return;}double value=b.arg;if(op!="rotateStep"){if(!NumberDialog::show(h,op=="rotateDialog"?L"旋转对象":L"缩放对象",op=="rotateDialog"?L"角度（正值顺时针）":L"比例（%）",op=="rotateDialog"?90:100,&value,op=="rotateDialog"?-3600:.1,op=="rotateDialog"?3600:10000))return;}auto refs=s.selected;auto bounds=studioio::selectionBounds(s);Vec2 pivot=bounds.empty()?s.view().center:(bounds.min+bounds.max)*.5;error(s.submit(Command("变换对象",kInvalidateGeometry,[refs,pivot,value,op](CommandContext& c){selection::TransformOptions opts;if(op=="scaleDialog")selection::scaleObjects(c.document,refs,pivot,value/100,opts);else selection::rotateObjects(c.document,refs,pivot,-value,opts);return CommandResult::success();}),"变换对象"));}
    else if(op=="page"){auto p=studioio::Page::get(s.doc);s.cancel();s.input.setFocus(interaction::InputFocus::ModalDialog);if(studioio::PageDialog::show(h,&p)){error(s.submit(Command("页面设置",kInvalidateAppearance,[p](CommandContext& c){p.store(c.document);return CommandResult::success();}),"页面设置"));fitPage();}s.input.setFocus(interaction::InputFocus::Canvas);}
    else if(op=="newwindow"){if(host)host->newWindow();return;}
    else if(op=="new"){if(host){host->newTab();return;}if(leave()){s.cancel();s.doc=Document();drawingSettings.apply(s.doc);s.graphicsWidth=0;s.history.clear();s.selected.clear();s.sourcePath.clear();s.input.bindDocument(s.doc.id());s.refresh();s.dirty=false;textEditor.nextFamily=s.doc.defaultFont();textEditor.nextSize=s.doc.defaultTextSize();textEditor.nextFlags=0;fileName="未命名结构";}}
    else {
        auto refs=s.selected;
        // Formatting acts on every selected text object, including text inside a
        // selected group, without requiring the inline editor to be open first.
        std::vector<ObjectId> texts=selectedTexts();
        if(op=="flag"||op=="size"||op=="textalign"||op=="font"||op=="color"){
            if(texts.empty()){status="先选中文字或包含文字的组，再设置"+b.label;invalidate();return;}
            uint32_t color=0;std::string face;double fontSize=0;
            std::string currentFace=s.doc.defaultFont();double currentSize=s.doc.defaultTextSize();
            if(auto text=s.doc.texts().alive(texts.front())){
                currentFace=text->format.fontName.empty()?s.doc.defaultFont():text->format.fontName;
                currentSize=textBaseSize(s.doc,texts.front());
                auto runs=s.doc.runs(texts.front());if(!runs.empty()&&!runs.front().fontName.empty())currentFace=runs.front().fontName;
            }
            if(op=="color"){CHOOSECOLORW cc={};static COLORREF custom[16]={};cc.lStructSize=sizeof(cc);cc.hwndOwner=h;cc.lpCustColors=custom;cc.Flags=CC_FULLOPEN|CC_RGBINIT;if(!studio_i18n::chooseColor(&cc))return;color=(uint32_t(GetRValue(cc.rgbResult))<<24)|(uint32_t(GetGValue(cc.rgbResult))<<16)|(uint32_t(GetBValue(cc.rgbResult))<<8)|255;}
            if(op=="font"){
                // Start the dialog from the selection's own face and size instead
                // of a hard-coded Arial 10, so "choose font" edits rather than resets.
                LOGFONTW lf={};HDC screen=GetDC(h);int logpixels=screen?GetDeviceCaps(screen,LOGPIXELSY):96;if(screen)ReleaseDC(h,screen);if(logpixels<=0)logpixels=96;
                lf.lfHeight=-LONG(std::lround(currentSize*logpixels/72.0));wcscpy(lf.lfFaceName,W(currentFace).c_str());
                CHOOSEFONTW cf={};cf.lStructSize=sizeof(cf);cf.hwndOwner=h;cf.lpLogFont=&lf;cf.Flags=CF_SCREENFONTS|CF_INITTOLOGFONTSTRUCT;
                if(!ChooseFontW(&cf))return;face=U(lf.lfFaceName);fontSize=cf.iPointSize>0?cf.iPointSize/10.:currentSize;
            }
            int arg=b.arg;error(s.submit(Command("文字格式",kInvalidateAppearance|kInvalidateGeometry,[texts,op,arg,color,face,fontSize](CommandContext& c){
                for(auto id:texts){auto runs=c.document.runs(id);auto rec=c.document.texts().get(id);if(!rec)return CommandResult::fail(ErrorCode::NotFound,"文字对象不存在");Error e;
                    if(op=="flag"){
                        if(arg==kFormula){for(auto& r:runs){r.flags&=~(kSubscript|kSuperscript);r.flags|=kFormula;}e=c.document.setTextRuns(id,runs);}
                        else{
                            // Clicking a style button applies one decision to the whole
                            // selection: any run already carrying it clears all runs.
                            bool on=true;for(const auto& r:runs)if(r.flags&arg)on=false;
                            for(auto& r:runs){if(on){r.flags|=uint16_t(arg);if(arg==kSubscript)r.flags&=~kSuperscript;if(arg==kSuperscript)r.flags&=~kSubscript;}else r.flags&=~uint16_t(arg);}
                            e=c.document.setTextRuns(id,runs);
                        }
                    }
                    else if(op=="size"){double base=App::textBaseSize(c.document,id);for(auto& r:runs)r.size=0;e=c.document.setTextRuns(id,runs);if(e.ok())e=c.document.setTextSize(id,std::max(4.,base+arg));}
                    else if(op=="font"){for(auto& r:runs){r.fontName=face;r.size=0;}e=c.document.setTextRuns(id,runs);if(e.ok())e=c.document.setTextSize(id,fontSize);}
                    else{auto fmt=rec->format;if(op=="textalign")fmt.align=uint8_t(arg);else fmt.colorRGBA=color;e=c.document.setTextFormat(id,fmt);}if(!e.ok())return CommandResult::fail(e.code,e.message);}
                return CommandResult::success();}),"文字格式"));
            syncTextControls();
        }else if(op=="align"||op=="distribute"||op=="group"||op=="ungroup"){arrangeAction(op,b.arg);}
        else if(refs.empty())status="请先选择要排列的对象";
        else {int arg=b.arg;Box bounds=studioio::selectionBounds(s);Vec2 pivot=bounds.empty()?s.view().center:(bounds.min+bounds.max)*.5;
            error(s.submit(Command("排列与变换",kInvalidateGeometry|kInvalidateStructure,[refs,op,arg,pivot](CommandContext& c){selection::TransformOptions opts;
                if(op=="rotate")selection::rotateObjects(c.document,refs,pivot,-90,opts);
                else if(op=="flip")selection::flipObjects(c.document,refs,pivot,arg==0,opts);
                else if(op=="merge"){auto rep=selection::mergeSelection(c.document,refs,2.0);if(!rep.mergedPairs)return CommandResult::fail(ErrorCode::Unsupported,"没有可合并的重合原子");}
                else return CommandResult::fail(ErrorCode::Unsupported,"功能尚未接通");return CommandResult::success();}),"排列与变换"));
        }
    }SetFocus(canvas);invalidate();
}
void App::createGraphics(){
    dc=GetDC(canvas);if(!dc)throw std::runtime_error("无法创建设备上下文");
    wchar_t mode[32]={};GetEnvironmentVariableW(L"MOLECULE_STUDIO_RENDERER",mode,32);
    bool useDx11=false;std::string dx11Error;
#ifdef MOLECULE_STUDIO_DX11
    bool preferDx11=_wcsicmp(mode,L"dx11")==0;
#ifdef MOLECULE_STUDIO_DX11_DEFAULT
    if(!mode[0])preferDx11=true;
#endif
    if(_wcsicmp(mode,L"compatible")==0)preferDx11=false;
    if(preferDx11){
        dx11Renderer=new dx11::Renderer();gl.reset(dx11Renderer);
        std::string dwhy;if(gl->initialize(&dwhy))useDx11=true;
        else{dx11Error=dwhy;gl->destroy();gl.reset();dx11Renderer=nullptr;}
    }
#endif
    if(!useDx11){
        bool useCompatible=compatible::vmwareAdapter();if(std::wstring(mode)==L"compatible")useCompatible=true;
        if(useCompatible){compatibleRenderer=new compatible::Renderer();gl.reset(compatibleRenderer);}else{
            PIXELFORMATDESCRIPTOR pf={};pf.nSize=sizeof(pf);pf.nVersion=1;pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pf.iPixelType=PFD_TYPE_RGBA;pf.cColorBits=32;
            int format=ChoosePixelFormat(dc,&pf);if(!format||!SetPixelFormat(dc,format,&pf))throw std::runtime_error("无法设置绘图像素格式");
            rc=wglCreateContext(dc);if(!rc||!wglMakeCurrent(dc,rc))throw std::runtime_error("无法创建 OpenGL 上下文");gl.reset(render::createGLBackend());
        }
        std::string why;if(!gl->initialize(&why))throw std::runtime_error(why);
    }
    wchar_t exePath[32768]={};GetModuleFileNameW(nullptr,exePath,32768);auto rendererLog=std::wstring(exePath);rendererLog=rendererLog.substr(0,rendererLog.find_last_of(L"\\/"))+L"\\renderer-status.txt";
    std::ofstream rendererStatus(U(rendererLog));rendererStatus<<"backend="<<gl->name()<<"\nvmware_adapter="<<compatible::vmwareAdapter()<<"\nopengl_context="<<(rc?1:0)<<"\n";
#ifdef MOLECULE_STUDIO_DX11
    if(dx11Renderer)rendererStatus<<"dx11_samples="<<dx11Renderer->samples()<<"\n";
#endif
    if(!dx11Error.empty())rendererStatus<<"dx11_error="<<dx11Error<<"\n";rendererStatus.close();
}
void App::notifyTitleChanged(){if(host)host->titleChanged(this);}
void App::fileMenu(){
    HMENU menu=CreatePopupMenu();
    studio_i18n::appendMenu(menu,MF_STRING,1,L"新建\tCtrl+N");
    studio_i18n::appendMenu(menu,MF_STRING,2,L"打开…\tCtrl+O");
    studio_i18n::appendMenu(menu,MF_STRING,3,L"保存\tCtrl+S");
    studio_i18n::appendMenu(menu,MF_STRING,4,L"另存为…");
    studio_i18n::appendMenu(menu,MF_STRING,5,L"导出 CDXML…");
    studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);
    studio_i18n::appendMenu(menu,MF_STRING,6,L"关闭标签页\tCtrl+W");
    studio_i18n::appendMenu(menu,MF_STRING,7,L"新建窗口\tCtrl+Shift+N");
    studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);
    POINT point;GetCursorPos(&point);
    int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN,point.x,point.y,0,h,0);
    DestroyMenu(menu);
    if(id==1)execute(Button("","新建","","@new"));
    else if(id==2)execute(Button("","打开","","@open"));
    else if(id==3)execute(Button("","保存","","@save"));
    else if(id==4)execute(Button("","另存为","","@saveas"));
    else if(id==5)execute(Button("","导出","","@export"));
    else if(id==6)execute(Button("","关闭","","@close"));
    else if(id==7)execute(Button("","新建窗口","","@newwindow"));
}
bool App::tabHotkey(UINT m,WPARAM wp){
    expandPending=false;
    if(!host)return false;
    if(m==WM_SYSKEYDOWN&&wp==VK_F4){PostMessageW(GetAncestor(h,GA_ROOT),WM_CLOSE,0,0);return true;}
    uint32_t mask=mods();
    if(mask&kModControl){
        if(wp=='T'&&!(mask&kModShift)){host->newTab();return true;}
        if(wp=='N'&&(mask&kModShift)){host->newWindow();return true;}
        if(wp=='W'){host->closeTab(this);return true;}
        if(wp==VK_F4){host->closeTab(this);return true;}
        if(wp==VK_TAB){host->cycle((mask&kModShift)?-1:1);return true;}
        if(wp>='1'&&wp<='9'){host->selectIndex(int(wp-'1'));return true;}
    }
    return false;
}
float App::tabBandH()const{return (tabChild&&host)?host->bandH:0.f;}
void App::layoutTabBand(){if(tabChild&&host)host->layoutTabs(float(width)/scale);}
void App::paintTabBand(Graphics& g){if(!tabChild||!host)return;auto state=g.Save();g.TranslateTransform(0.f,0.f);host->paintBand(g,float(width)/scale);g.Restore(state);}
// Right-click menu for a box-selected drawing. Kept as one builder so the
// product menu and the acceptance suite inspect exactly the same entries; the
// "copy as PNG" entry sits right after 复制 and is greyed when nothing is selected.
const int kContextCopyPngId=8;
void appendSelectionContextMenu(HMENU menu,bool structure,bool hasSelection){
    studio_i18n::appendMenu(menu,MF_STRING,1,L"复制\tCtrl+C");
    studio_i18n::appendMenu(menu,hasSelection?MF_STRING:MF_GRAYED,kContextCopyPngId,L"复制为 PNG");
    studio_i18n::appendMenu(menu,MF_STRING,2,L"剪切\tCtrl+X");
    studio_i18n::appendMenu(menu,MF_STRING,3,L"删除\tDelete");
    studio_i18n::appendMenu(menu,MF_SEPARATOR,0,0);
    studio_i18n::appendMenu(menu,structure?MF_STRING:MF_GRAYED,4,L"整理结构");
    studio_i18n::appendMenu(menu,MF_STRING,7,L"文字整理");
    studio_i18n::appendMenu(menu,structure?MF_STRING:MF_GRAYED,5,L"预测核磁…");
    studio_i18n::appendMenu(menu,structure?MF_STRING:MF_GRAYED,6,L"结构转命名");
}
LRESULT CALLBACK canvasProc(HWND h,UINT m,WPARAM wp,LPARAM lp){auto a=(App*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){a=(App*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)a);a->canvas=h;}if(!a)return DefWindowProcW(h,m,wp,lp);if(m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN||m==WM_MBUTTONDOWN||m==WM_MOUSEWHEEL||m==WM_KEYDOWN||m==WM_CHAR)a->expandPending=false;auto& s=a->s;Vec2 pixel(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));uint32_t pointerModifiers=mods();if(wp&MK_CONTROL)pointerModifiers|=kModControl;if(wp&MK_SHIFT)pointerModifiers|=kModShift;
    switch(m){case WM_SETFOCUS:if(a->textEditor.active&&!a->textEditor.closing){SetFocus(a->edit);return 0;}break;case WM_DESTROY:a->canvasDestroySeen=true;a->shutdownRenderer();return 0;case WM_SIZE:s.renderView.widthPx=std::max(1,int(LOWORD(lp)));s.renderView.heightPx=std::max(1,int(HIWORD(lp)));{auto v=s.view();v.viewportCenter=Vec2(s.renderView.widthPx*.5,s.renderView.heightPx*.5);s.setView(v);}a->updateScrollbars();return 0;
    case WM_LBUTTONDOWN:a->insertionPreviewPending=false;
        if(a->bioPlacing){a->placeBioComponent(s.view().toDocument(pixel));return 0;}
        if(a->ringPlacing){a->ringPlacing=false;a->insertRingTemplate(a->ringVariant,s.view().toDocument(pixel));a->updateCanvasCursor();return 0;}
        if(a->patternPlacing){a->patternStart=s.view().toDocument(pixel);a->patternEnd=a->patternStart;a->patternDragging=true;a->patternPreviewPending=false;SetCapture(h);s.ghost.clear();a->updateCanvasCursor();return 0;}
        if(a->radicalPlacing){
            auto point=s.view().toDocument(pixel);auto atom=s.nearestAtom(point,s.snapRadiusDevice());
            if(atom==kInvalidObjectId){a->status="自由基：这里没有原子，请点击原子或按 Esc 取消";a->invalidate();return 0;}
            a->radicalPlacing=false;a->applyRadical(std::vector<ObjectId>(1,atom));a->updateCanvasCursor();return 0;
        }
        if(a->phenalenePlacing){
            a->phenalenePlacing=false;a->insertPhenalene(s.view().toDocument(pixel));a->updateCanvasCursor();return 0;
        }
        if(a->beginSelectionTransform(pixel,pointerModifiers))return 0;
        if(a->tableControlClick(pixel,pointerModifiers))return 0;
        if(a->beginTableDrag(pixel,pointerModifiers))return 0;
        if(a->splitter){auto page=studioio::Page::get(s.doc);double pw=page.width*page.across,ph=page.height*page.down;double ow=180,oh=std::min(300.,ow*ph/pw),left=s.renderView.widthPx-ow-16,top=34,k=std::min((ow-8)/pw,(oh-8)/ph);if(pixel.x>=left&&pixel.x<=left+ow&&pixel.y>=top&&pixel.y<=top+oh){auto view=s.view();view.center=Vec2(page.x+(pixel.x-left-4)/k,page.y+(pixel.y-top-4)/k);s.setView(view);a->invalidate();return 0;}}
        if(a->textEditor.active){auto hit=s.pick(s.view().toDocument(pixel));if(!a->finishText(true))return 0;if(s.entries[s.activeIndex].owner=="T09"&&hit.kind!=ObjectKind::Text&&hit.kind!=ObjectKind::Atom){SetFocus(h);a->invalidate();return 0;}}
        if(s.entries[s.activeIndex].owner=="T09"){SetFocus(h);a->textPlacing=true;a->textPress=s.view().toDocument(pixel);SetCapture(h);return 0;}
        if(s.entries[s.activeIndex].owner=="T05"&&s.entries[s.activeIndex].command.find("Eraser")==std::string::npos){
            auto point=s.view().toDocument(pixel);bool handle=false;
            if(!(pointerModifiers&kModControl))for(auto r:s.selected)if(r.kind==ObjectKind::Arrow){arrows::ArrowParams params;bool ok=false;if(arrows::readArrow(s.doc,r.id,&params,&ok).ok()){for(auto hnd:arrows::arrowHandles(params,s.doc.defaultLineWidth(),arrows::Tessellation()))if(core::length(hnd.point-point)<s.view().deviceRadiusToDocument(6)){auto preset=arrows::presetById(params.presetId);if(preset){s.activateCommand(preset->sourceCommand);a->selectionArrowEdit=handle=true;}break;}}if(handle)break;}
            if(!handle&&(pointerModifiers&kModControl)&&(s.pick(point).valid()||studioio::inside(studioio::selectionBounds(s),point)))a->cloneDragging=true;
        }
        a->movePreviewPending=false;a->marqueeBackdrop.clear();a->clickHistory=s.history.cursor();a->clickPoint=s.view().toDocument(pixel);a->lastBondClick=false;SetFocus(h);SetCapture(h);a->down=true;a->route(interaction::InputKind::PointerDown,lp);a->error(s.gesture(PointerPhase::Down,s.view().toDocument(pixel),pointerModifiers));
        if(a->cloneDragging){a->clonePress=s.view().toDocument(pixel);a->cloneDelta=Vec2();a->cloneRefs=s.selected;a->cloneSource=std::make_shared<Session>();auto e=studioio::append(s.doc,a->cloneSource->doc,a->cloneRefs,Vec2(),nullptr);if(!e.ok()){a->cloneDragging=false;a->error(e);}else a->cloneSource->refresh();}break;
    case WM_MOUSEMOVE:a->updateNameLinkTip(s.view().toDocument(pixel));if(a->pan&&!std::getenv("MOLECULE_STUDIO_PAN_LEGACY")){a->pointerPixel=pixel;a->pointerOverCanvas=true;auto v=s.view();v.center=v.center-(pixel-a->panAt)*v.docPerDevice();a->panAt=pixel;s.setView(v);SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32646)));InvalidateRect(h,0,FALSE);return 0;}if(a->patternPlacing){a->pointerPixel=pixel;a->pointerOverCanvas=true;
        if(std::getenv("MOLECULE_STUDIO_PATTERN_LEGACY")){if(a->patternDragging){a->patternEnd=s.view().toDocument(pixel);a->updatePatternPreview();}a->updateCanvasCursor();a->invalidate();return 0;}
        // Coalesce high-rate pointer input; placement changes only the canvas.
        if(a->patternDragging){a->patternEnd=s.view().toDocument(pixel);a->patternPreviewPending=true;}
        SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32515)));
        if(a->patternDragging||a->crosshairs)InvalidateRect(h,0,FALSE);return 0;}if(a->down&&a->fastMarquee()){a->pointerPixel=pixel;a->error(s.gesture(PointerPhase::Move,s.view().toDocument(pixel),pointerModifiers));SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32512)));InvalidateRect(h,0,FALSE);return 0;}if(a->down&&s.objectDragActive()&&!a->cloneDragging&&!std::getenv("MOLECULE_STUDIO_MOVE_LEGACY")){a->pointerPixel=pixel;a->movePreviewPoint=s.view().toDocument(pixel);a->movePreviewModifiers=pointerModifiers;a->movePreviewPending=true;SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32646)));InvalidateRect(h,0,FALSE);/* Bound paint starvation under continuous mouse input. */if(a->now()-a->lastMovePaintMs>=8.0)UpdateWindow(h);return 0;}if(a->fastGraphicTool()&&!a->wrapDragging&&!a->transformDragging&&!a->tableDragPending&&!a->cloneDragging&&!a->textPlacing){
        if(!a->pointerOverCanvas){TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);}a->pointerPixel=pixel;a->pointerOverCanvas=true;a->hover.clear();a->insertionPreviewPixel=pixel;a->insertionPreviewPoint=s.view().toDocument(pixel);a->insertionPreviewModifiers=pointerModifiers;a->insertionPreviewTool=s.activeIndex;a->insertionPreviewPending=true;
        SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32512)));InvalidateRect(h,0,FALSE);return 0;
        }if(a->wrapDragging){a->updateTextWrap(pixel);return 0;}if(a->transformDragging){a->updateSelectionTransform(pixel,pointerModifiers);return 0;}if(a->tableDragPending){a->updateTableDrag(pixel,pointerModifiers);return 0;}a->selectionCursorReset=false;a->pointerOverCanvas=true;{TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);}a->pointerPixel=pixel;if(a->crosshairs)a->invalidate();if(a->cloneDragging){a->cloneDelta=s.view().toDocument(pixel)-a->clonePress;if(pointerModifiers&kModShift){if(std::abs(a->cloneDelta.x)>=std::abs(a->cloneDelta.y))a->cloneDelta.y=0;else a->cloneDelta.x=0;}s.ghost.clear();a->invalidate();return 0;}if(a->textPlacing){s.ghost.clear();PreviewItem r;r.primitive=PreviewPrimitive::Rect;r.points={a->textPress,s.view().toDocument(pixel)};r.style.colorRGBA=0x999999FF;s.ghost.add(r);a->invalidate();return 0;}{auto old=s.input.hotspot();a->hover.clear();if(a->pan){auto v=s.view();v.center=v.center-(pixel-a->panAt)*v.docPerDevice();a->panAt=pixel;s.setView(v);}else{a->route(interaction::InputKind::PointerMove,lp);if(a->down)a->error(s.gesture(PointerPhase::Move,s.view().toDocument(pixel),pointerModifiers));if(a->cloneDragging){a->cloneDelta=s.view().toDocument(pixel)-a->clonePress;if(mods()&kModShift){if(std::abs(a->cloneDelta.x)>=std::abs(a->cloneDelta.y))a->cloneDelta.y=0;else a->cloneDelta.x=0;}}}
        auto current=s.input.hotspot();bool changed=old.kind!=current.kind||!(old.object==current.object)||old.point.x!=current.point.x||old.point.y!=current.point.y;
        a->updateCanvasCursor();a->invalidate(a->pan||a->down||changed);return 0;}
    case WM_LBUTTONUP:if(a->down&&a->fastGraphicTool()){a->error(s.gesture(PointerPhase::Move,s.view().toDocument(pixel),pointerModifiers));}a->insertionPreviewPending=false;if(a->patternPlacing){if(a->patternDragging)a->finishPatternPlacement(s.view().toDocument(pixel));return 0;}if(a->wrapDragging){a->finishTextWrap(pixel);return 0;}if(a->transformDragging){a->finishSelectionTransform(pixel,pointerModifiers);return 0;}if(a->tableDragPending){a->finishTableDrag(pixel,pointerModifiers);return 0;}if(a->textPlacing){a->textPlacing=false;ReleaseCapture();s.ghost.clear();auto r=s.pick(a->textPress);double wrap=std::abs(s.view().toDocument(pixel).x-a->textPress.x);if(wrap<s.view().deviceRadiusToDocument(8))wrap=0;if(r.kind==ObjectKind::Text){a->openText(r.id);a->textEditor.pointCaret(pixel);}else{auto endpoint=a->textToolTarget(a->textPress,false);if(endpoint!=kInvalidObjectId)a->openText(kInvalidObjectId,endpoint);else{a->textEditor.begin(kInvalidObjectId,kInvalidObjectId,a->textPress,true,wrap);a->textBackdrop.clear();a->status="文字编辑 · Enter / Esc 保存，Shift+Enter 换行";a->invalidate();}}return 0;}
        if(a->cloneDragging){a->route(interaction::InputKind::PointerUp,lp);a->down=false;a->error(a->finishClone(s.view().toDocument(pixel),pointerModifiers));ReleaseCapture();a->invalidate();return 0;}
        if(a->down){a->route(interaction::InputKind::PointerUp,lp);a->down=false;a->error(s.gesture(PointerPhase::Up,s.view().toDocument(pixel),pointerModifiers));
            // A plain click with a bond tool sprouts a bond/atom. Remember it so a
            // following double-click on the same endpoint can retract that sprout
            // instead of leaving a stray atom and labelling the wrong one.
            std::string activeCommand=s.entries[s.activeIndex].command;
            bool bondTool=activeCommand.find("chooseBondTool")!=std::string::npos||activeCommand.find("chooseMultipleBondsTool")!=std::string::npos;
            if(bondTool&&s.history.cursor()==a->clickHistory+1&&s.history.undoLabel()=="bond.addFromSnap"){
                a->lastBondClick=true;a->lastBondClickHistory=a->clickHistory;a->lastBondClickPoint=a->clickPoint;a->lastBondClickTick=GetTickCount64();
            }
            ReleaseCapture();if(a->selectionArrowEdit){a->selectionArrowEdit=false;s.activateCommand("chooseSelectionTool_Marquee");}if(s.entries[s.activeIndex].owner=="T09"&&s.createdTextId!=kInvalidObjectId){auto created=s.createdTextId;s.createdTextId=kInvalidObjectId;a->openText(created);}}break;
    case WM_LBUTTONDBLCLK:{a->cancelTableDrag();if(a->textEditor.active)a->finishText(true);
        if(a->lastBondClick){
            bool recent=GetTickCount64()-a->lastBondClickTick<=DWORD(GetDoubleClickTime())+80;
            bool sameSpot=core::length(s.view().toDocument(pixel)-a->lastBondClickPoint)<s.view().deviceRadiusToDocument(8);
            bool stillLast=s.history.cursor()==a->lastBondClickHistory+1&&s.history.undoLabel()=="bond.addFromSnap";
            if(recent&&sameSpot&&stillLast){a->error(s.undo());s.refresh();}
            a->lastBondClick=false;
        }
        if(a->tablePick(s.view().toDocument(pixel),true))return 0;auto r=s.pick(s.view().toDocument(pixel));if(r62::experimental::isFigureObject(s.doc,r)){r62::experimental::PlateFigure plate;r62::experimental::PlateFrame frame;uint32_t spot=0;if(r62::experimental::readFigureAndFrame(s.doc,r.id,&plate,&frame,0).ok()){auto element=r62::experimental::pickElement(plate,frame,s.view().toDocument(pixel),s.view().deviceRadiusToDocument(8));if(element.kind==r62::experimental::ElementKind::Spot)spot=element.id;}a->tlcSettings(r.id,spot);return 0;}if(r.kind==ObjectKind::Text){a->openText(r.id);a->textEditor.pointCaret(pixel,true);}if(r.kind==ObjectKind::Atom)a->openText(kInvalidObjectId,r.id);}break;
    case WM_MBUTTONDOWN:a->cancelTableDrag();if(a->textEditor.active)a->finishText(true);a->pan=true;a->panAt=pixel;s.cancel();SetCapture(h);break;case WM_MBUTTONUP:a->pan=false;ReleaseCapture();break;
    case WM_CONTEXTMENU:{
        if(a->patternPlacing){a->cancelPatternPlacement();return 0;}
        if(a->down||a->pan)return 0;
        POINT screen={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(screen.x==-1&&screen.y==-1)GetCursorPos(&screen);
        POINT client=screen;ScreenToClient(h,&client);Vec2 point=s.view().toDocument(Vec2(client.x,client.y));
        if(a->tableAt(point)!=kInvalidObjectId){a->tableContextMenu(point,screen);return 0;}
        ObjectId target=kInvalidObjectId;double best=1e100;
        for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()&&(validation::ignored(s.doc,atom.id)||validation::overValent(s.doc,atom))){
            Box bounds(atom.position,atom.position);
            for(const auto& primitive:s.scene.primitives())if(primitive.source==ObjectRef(ObjectKind::Atom,atom.id)){bounds.add(primitive.bounds.min);bounds.add(primitive.bounds.max);}
            bounds=validation::warningSquare(bounds);
            if(studioio::inside(bounds,point,s.view().deviceRadiusToDocument(3))){double distance=std::hypot(point.x-atom.position.x,point.y-atom.position.y);if(distance<best){best=distance;target=atom.id;}}
        }
        if(target==kInvalidObjectId){
            // Box-selected content gets a full editing/chemistry context menu.
            if(s.selected.empty())return 0;
            auto refs=studioio::expanded(s.doc,s.selected);
            bool structure=false;
            for(auto r:refs)if(r.kind==ObjectKind::Atom||r.kind==ObjectKind::Bond){structure=true;break;}
            HMENU menu=CreatePopupMenu();
            appendSelectionContextMenu(menu,structure,!s.selected.empty());
            int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,screen.x,screen.y,0,h,nullptr);DestroyMenu(menu);
            if(choice==1)a->execute(Button("ctxCopy","复制","","@copy"));
            else if(choice==kContextCopyPngId)a->execute(Button("ctxCopyPng","复制为 PNG","","@copyPng"));
            else if(choice==2)a->execute(Button("ctxCut","剪切","","@cut"));
            else if(choice==3)a->execute(Button("ctxDelete","删除","","@delete"));
            else if(choice==4)a->execute(Button("ctxClean","整理结构","","@clean"));
            else if(choice==7)a->execute(Button("ctxTextTidy","文字整理","","@textTidy"));
            else if(choice==5)a->execute(Button("ctxNmr","预测核磁","","@nmr"));
            else if(choice==6)a->execute(Button("ctxName","结构转命名","","@structureToName"));
            if(choice)a->updateCanvasCursor();
            return 0;
        }
        bool ignored=validation::ignored(s.doc,target);HMENU menu=CreatePopupMenu();
        studio_i18n::appendMenu(menu,MF_STRING,1,ignored?L"恢复此原子的价态检查":L"忽略此原子的价态错误");
        int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,screen.x,screen.y,0,h,nullptr);DestroyMenu(menu);
        if(choice==1){a->error(s.submit(validation::setIgnored(target,!ignored),"价态提示"));a->status=ignored?"已恢复此原子的价态检查":"已忽略此原子的价态错误 · 右键可恢复";a->invalidate();}
        return 0;
    }
    case WM_MOUSELEAVE:if(a->rotationHot){a->rotationHot=false;InvalidateRect(h,0,FALSE);}a->hideNameLinkTip();a->nameLinkTipText.clear();a->pointerOverCanvas=false;a->invalidate(false);return 0;
    case WM_MOUSEWHEEL:{{POINT bandPoint={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};POINT shellPoint=bandPoint;ScreenToClient(a->h,&shellPoint);float bh=a->tabBandH();if(bh>0&&shellPoint.y/a->scale>=40.f&&shellPoint.y/a->scale<40.f+bh){int delta=GET_WHEEL_DELTA_WPARAM(wp);a->host->scrollBy(delta>0?-60.f:60.f);a->host->layoutTabs(float(a->width)/a->scale);RECT br={0,int(40*a->scale),a->width,int((40+bh)*a->scale)};InvalidateRect(a->h,&br,FALSE);return 0;}}a->cancelTextWrap();a->cancelSelectionTransform();a->cancelTableDrag();if(a->textEditor.active)a->finishText(true);POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&p);Vec2 pos(p.x,p.y);auto v=s.view();auto anchor=v.toDocument(pos);v.scale=std::max(.02,std::min(80.,v.scale*std::pow(1.15,double(GET_WHEEL_DELTA_WPARAM(wp))/WHEEL_DELTA)));v.center=v.center+anchor-v.toDocument(pos);s.setView(v);
#ifdef MOLECULE_STUDIO_DX11
        // A wheel gesture is interactive: the renderer may stretch the existing
        // device batch instead of re-tessellating every glyph per notch.  The exact
        // rebuild runs once, 180 ms after the last notch.
        if(a->dx11Renderer){a->dx11Renderer->setInteractive(true);SetTimer(h,7,180,0);}
#endif
        }break;
    case WM_TIMER:if(wp==7){KillTimer(h,7);
#ifdef MOLECULE_STUDIO_DX11
        if(a->dx11Renderer)a->dx11Renderer->setInteractive(false);
#endif
        a->invalidate();return 0;}break;
    case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT){a->updateCanvasCursor(true);return TRUE;}return DefWindowProcW(h,m,wp,lp);
    case WM_SYSKEYDOWN:case WM_KEYDOWN:
        // An open in-place caption owns the keyboard even when the canvas is the
        // focus window (a ribbon tool button or a redirected shell key leaves it
        // there): every key, Space above all, goes to the editor instead of a
        // tool shortcut. Space only switches to the marquee tool when nothing is
        // being edited.
        if(a->textEditor.active){studio_i18n::sendMessage(a->edit,m,wp,lp);return 0;}
        if(m==WM_KEYDOWN&&wp=='R'&&!(mods()&(kModControl|kModAlt))&&a->pointerOverCanvas&&!a->down){
            auto id=s.nearestAtom(s.view().toDocument(a->pointerPixel),s.snapRadiusDevice());
            if(id!=kInvalidObjectId&&s.doc.incidentBonds(id).size()==1){
                a->insertionPreviewPending=false;auto base=nicknames::labelCommand(id,"R");a->error(s.submit(Command("末端 R 基团",kInvalidateStructure|kInvalidateSemantics|kInvalidateAppearance,[id,base](CommandContext& c)mutable{auto result=base.execute(c);if(!result.ok())return result;auto sem=c.document.atoms().alive(id)->semantics;sem.showLabel=true;auto e=c.document.setAtomSemantics(id,sem);if(e.ok())e=c.document.setAtomLabel(id,"R");return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"末端 R 基团"));a->swallowHotkeyChar=true;a->status="已将末端标为 R 基团 · 可撤销";a->invalidate();return 0;
            }
        }
        a->swallowHotkeyChar=false;
        if(a->tabHotkey(m,wp))return 0;
        if(wp==VK_F9){a->showOcsr();return 0;}
        if(wp==VK_ESCAPE&&a->patternPlacing){a->cancelPatternPlacement();return 0;}
        if(wp==VK_ESCAPE&&a->radicalPlacing){a->radicalPlacing=false;a->status="已取消自由基";a->updateCanvasCursor();a->invalidate();return 0;}
        if(wp==VK_ESCAPE&&a->bioPlacing){a->cancelBioPlacement();return 0;}
        if(wp==VK_ESCAPE&&a->ringPlacing){a->cancelRingPlacement();return 0;}
        if(wp==VK_ESCAPE&&a->phenalenePlacing){a->cancelPhenalenePlacement();return 0;}
        if(wp==VK_ESCAPE&&a->wrapDragging){a->cancelTextWrap();return 0;}if(wp==VK_ESCAPE&&a->transformDragging){a->cancelSelectionTransform();return 0;}
        if(wp==VK_ESCAPE||(wp==VK_SPACE&&mods()==0&&!a->textEditor.active)){a->radicalPlacing=false;a->cancelTableDrag();s.cancel();s.selected.clear();s.activateCommand("chooseSelectionTool_Marquee");a->down=a->pan=a->cloneDragging=a->selectionArrowEdit=a->textPlacing=false;a->cloneSource.reset();a->selectionCursorReset=true;if(GetCapture()==h)ReleaseCapture();a->updateCanvasCursor();a->invalidate();return 0;}
        if(a->shortcut(UINT(wp),mods())){a->updateCanvasCursor();return 0;}
        if(wp==VK_ESCAPE){s.cancel();s.activateCommand("chooseSelectionTool_Marquee");a->down=a->pan=a->cloneDragging=a->selectionArrowEdit=a->textPlacing=false;a->cloneSource.reset();a->selectionCursorReset=true;if(GetCapture()==h)ReleaseCapture();a->updateCanvasCursor();}
        else if((mods()&(kModControl|kModAlt))||(wp>=VK_F5&&wp<=VK_F10)){if(m==WM_SYSKEYDOWN&&wp==VK_F4)return DefWindowProcW(h,m,wp,lp);return 0;}
        else a->route(interaction::InputKind::KeyDown,0,uint32_t(wp),(lp&(1<<30))!=0);break;
    case WM_CHAR:if(a->textEditor.active){studio_i18n::sendMessage(a->edit,WM_CHAR,wp,lp);return 0;}if(a->swallowHotkeyChar){a->swallowHotkeyChar=false;return 0;}if(wp>32&&wp!=127&&!(mods()&kModControl)){a->route(interaction::InputKind::Character,0,uint32_t(wp),(lp&(1<<30))!=0);if(a->textEditor.active)studio_i18n::sendMessage(a->edit,WM_CHAR,wp,lp);else if(s.entries[s.activeIndex].owner=="T09"){auto atom=a->textToolTarget(s.view().toDocument(a->pointerPixel),true);if(atom!=kInvalidObjectId){a->openText(kInvalidObjectId,atom);studio_i18n::sendMessage(a->edit,WM_CHAR,wp,lp);}}}break;
    case WM_KILLFOCUS:case WM_CAPTURECHANGED:a->insertionPreviewPending=false;if(a->patternDragging)a->cancelPatternPlacement();a->cancelTextWrap();a->cancelSelectionTransform();a->cancelTableDrag();if(a->down||a->pan){s.cancel();a->down=a->pan=a->cloneDragging=a->selectionArrowEdit=false;a->cloneSource.reset();}break;
    case WM_ERASEBKGND:return 1;case WM_PAINT:{double start=App::now();PAINTSTRUCT ps;BeginPaint(h,&ps);bool preview=false;
#ifdef MOLECULE_STUDIO_DX11
        if(a->liveSizing&&a->dx11Renderer){RECT bounds;GetClientRect(h,&bounds);preview=a->dx11Renderer->presentPreview(ps.hdc,bounds.right,bounds.bottom);if(preview)++a->resizePreviews;}
#endif
        if(!preview)a->render();EndPaint(h,&ps);++a->canvasPaints;a->canvasMs+=App::now()-start;return 0;}
    default:return DefWindowProcW(h,m,wp,lp);}
    a->updateCanvasCursor();a->invalidate();return 0;
}
LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM wp,LPARAM lp){auto a=(App*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){a=(App*)((CREATESTRUCTW*)lp)->lpCreateParams;a->h=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)a);}if(!a)return DefWindowProcW(h,m,wp,lp);
    if(a->reviewEditor){
        if(m==WM_NCHITTEST)return HTCLIENT;
        if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);FillRect(dc,&ps.rcPaint,(HBRUSH)GetStockObject(WHITE_BRUSH));EndPaint(h,&ps);return 0;}
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_CLOSE){DestroyWindow(h);return 0;}
        if(m==WM_COMMAND&&LOWORD(wp)>=410&&LOWORD(wp)<414){
            const int flags[]={kBold,kItalic,kSubscript,kSuperscript};
            a->execute(Button("review.text","","","@flag",flags[LOWORD(wp)-410]));return 0;
        }
        if(m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_LBUTTONDBLCLK||m==WM_MOUSEMOVE||m==WM_MOUSELEAVE||m==WM_RBUTTONUP)
            return DefWindowProcW(h,m,wp,lp);
    }
    switch(m){
    case WM_NCCALCSIZE:if(wp){if(IsZoomed(h)){MONITORINFO monitor={};monitor.cbSize=sizeof(monitor);if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor))((NCCALCSIZE_PARAMS*)lp)->rgrc[0]=monitor.rcWork;}return 0;}break;
    case WM_NCHITTEST:{POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&point);RECT client;GetClientRect(h,&client);
        int edge=int(7*a->scale);bool ring=!IsZoomed(h)&&(point.x<edge||point.x>=client.right-edge||point.y<edge||point.y>=client.bottom-edge);
        float x=point.x/a->scale,y=point.y/a->scale;
        if(a->tabChild){
            // The shell covers the frame's whole client area.  The border ring and
            // the empty caption band must reach the same-thread tab host, or the
            // frame never enters the native sizing loop and never sees a caption
            // double-click; buttons, tabs and the information card stay CLIENT.
            bool control=false;
            if(y<40.f){for(const auto& b:a->quick)if(b.r.Contains(x,y))control=true;for(const auto& b:a->windowControls)if(b.r.Contains(x,y))control=true;
                if(!a->s.selected.empty()&&a->infoRect.Contains(x,y))control=true;
                if(a->host&&!control){auto bandHit=a->host->hitBand(x,y);control=!bandHit.kind.empty();}}
            if(control)return HTCLIENT;
            return (ring||y<40.f)?HTTRANSPARENT:HTCLIENT;
        }
        if(ring){bool left=point.x<edge,right=point.x>=client.right-edge,top=point.y<edge,bottom=point.y>=client.bottom-edge;
            if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;
            if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;}
        if(y<40){for(const auto& b:a->quick)if(b.r.Contains(x,y))return HTCLIENT;for(const auto& b:a->windowControls)if(b.r.Contains(x,y))return HTCLIENT;return HTCAPTION;}return HTCLIENT;}
    case WM_MEASUREITEM:{auto item=(MEASUREITEMSTRUCT*)lp;if(item->CtlType==ODT_MENU){item->itemWidth=UINT(254*a->scale);item->itemHeight=UINT(34*a->scale);return TRUE;}break;}
    case WM_DRAWITEM:if(a->drawShapeItem((DRAWITEMSTRUCT*)lp)||a->drawGraphicStyleItem((DRAWITEMSTRUCT*)lp)||a->drawArrowMenuItem((DRAWITEMSTRUCT*)lp))return TRUE;break;
    case WM_ENTERSIZEMOVE:a->liveSizing=true;return 0;
    case WM_EXITSIZEMOVE:KillTimer(h,71);a->liveSizing=false;a->resizePending=false;if(a->canvas){a->layout();a->invalidate();}return 0;
    case WM_SIZE:if(a->canvas){if(a->liveSizing){if(!a->resizePending){a->resizePending=true;SetTimer(h,71,33,0);}}else{a->layout();a->invalidate();}}return 0;
    case WM_GETMINMAXINFO:{auto p=(MINMAXINFO*)lp;MONITORINFO monitor={};monitor.cbSize=sizeof(monitor);if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor)){p->ptMaxPosition.x=monitor.rcWork.left-monitor.rcMonitor.left;p->ptMaxPosition.y=monitor.rcWork.top-monitor.rcMonitor.top;p->ptMaxSize.x=monitor.rcWork.right-monitor.rcWork.left;p->ptMaxSize.y=monitor.rcWork.bottom-monitor.rcWork.top;}int minW=int(800*a->scale),minH=int(500*a->scale);if(p->ptMaxSize.x>0)minW=std::min(minW,int(p->ptMaxSize.x*3/4));if(p->ptMaxSize.y>0)minH=std::min(minH,int(p->ptMaxSize.y*3/4));p->ptMinTrackSize.x=minW;p->ptMinTrackSize.y=minH;if(a->testing){p->ptMaxTrackSize.x=8192;p->ptMaxTrackSize.y=8192;}return 0;}
    case WM_DPICHANGED:{a->scale=HIWORD(wp)/96.f;auto r=(RECT*)lp;SetWindowPos(h,0,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);a->layout();return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        double start=App::now();
        HRGN update=CreateRectRgn(0,0,0,0);GetUpdateRgn(h,update,FALSE);
        PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);
        Graphics dest(dc);dest.SetPageUnit(UnitPixel);
        // Never allocate or rasterize a full-screen bitmap behind the GL child.
        // Paint the ribbon, status bar and narrow canvas margins separately.
        RECT corner={};
        if(a->hbar&&a->vbar){RECT hb,vb;GetWindowRect(a->hbar,&hb);GetWindowRect(a->vbar,&vb);corner=RECT{vb.left,hb.top,vb.right,hb.bottom};MapWindowPoints(HWND_DESKTOP,h,(POINT*)&corner,2);}
        RECT bands[]={{0,0,a->width,a->top},{0,a->height-int(a->footerHeight()*a->scale),a->width,a->height},
            {0,a->top,int(16*a->scale),a->height-int(a->footerHeight()*a->scale)},
            {a->width-int(16*a->scale),a->top,a->width,a->height-int(a->footerHeight()*a->scale)},corner};
        for(int i=0;i<5;++i){auto band=bands[i];RECT dirty;HRGN part=CreateRectRgnIndirect(&band);CombineRgn(part,part,update,RGN_AND);int kind=GetRgnBox(part,&dirty);DeleteObject(part);if(kind==NULLREGION||kind==ERROR)continue;
            int width=dirty.right-dirty.left,height=dirty.bottom-dirty.top;if(width<=0||height<=0)continue;
            Bitmap buffer(width,height,PixelFormat32bppARGB);
            {Graphics g(&buffer);g.SetPageUnit(UnitPixel);g.TranslateTransform(float(-dirty.left),float(-dirty.top));a->paint(g,i==0,i==1);}
            dest.DrawImage(&buffer,Rect(dirty.left,dirty.top,width,height),0,0,width,height,UnitPixel);
        }
        DeleteObject(update);EndPaint(h,&ps);++a->shellPaints;a->shellMs+=App::now()-start;return 0;}
    case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT){POINT p;GetCursorPos(&p);ScreenToClient(h,&p);if(!a->s.selected.empty()&&a->infoRect.Contains(p.x/a->scale,p.y/a->scale)){SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32649)));return TRUE;}}break;
    case WM_MOUSEMOVE:{TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);float x=GET_X_LPARAM(lp)/a->scale,y=GET_Y_LPARAM(lp)/a->scale;float bandHNow=a->tabBandH();if(a->ringHoldPending&&!a->ringButtonAt(x,y)&&(std::abs(x-a->ringHoldPoint.x)>8.f||std::abs(y-a->ringHoldPoint.y)>8.f)){KillTimer(h,4);a->ringHoldPending=false;}if(a->arrowHoldPending&&!a->arrowButtonAt(x,y)&&(std::abs(x-a->arrowHoldPoint.x)>8.f||std::abs(y-a->arrowHoldPoint.y)>8.f)){KillTimer(h,3);a->arrowHoldPending=false;}
        auto bandDirty=[&](){if(bandHNow>0){RECT br={0,0,a->width,int(bandHNow*a->scale)};InvalidateRect(h,&br,FALSE);}};
        if(bandHNow>0&&y<bandHNow&&x>=210.f&&x<=a->width/a->scale-344.f){auto bandHit=a->host->hitBand(x,y);std::string band=bandHit.kind;if(band=="tab"||band=="close")band+=":"+std::to_string(bandHit.index);if(band!=a->hover){a->hover=band;if(a->host)a->host->hover=band;bandDirty();KillTimer(h,2);if(a->host)a->host->hideTip();if(band.rfind("tab:",0)==0||band.rfind("close:",0)==0)SetTimer(h,2,450,0);}return 0;}
        if(bandHNow>0&&(a->hover=="plus"||a->hover.rfind("tab:",0)==0||a->hover.rfind("close:",0)==0)){a->hover.clear();if(a->host)a->host->hover.clear();bandDirty();KillTimer(h,2);if(a->host)a->host->hideTip();}
        std::string label;for(const auto& item:a->compactMenus)if(item.first.Contains(x,y))label="more:"+std::to_string(item.second);for(auto& gr:a->visibleGroups())for(auto& b:gr.items)if(b.r.Contains(x,y))label=b.id;for(auto& b:a->quick)if(b.r.Contains(x,y))label=b.id;if(!a->ribbonTab)for(auto& b:a->chemistryQuick)if(b.r.Contains(x,y))label=b.id;for(auto& b:a->windowControls)if(b.r.Contains(x,y))label=b.id;for(auto& b:a->ribbonTabs)if((b.arg!=3||a->aiTabVisible)&&b.r.Contains(x,y))label=b.id;if(!a->s.selected.empty()&&a->infoRect.Contains(x,y))label="molecule-info";if(label!=a->hover){a->hover=label;a->invalidate(false);}return 0;}
    case WM_MOUSELEAVE:{float bandHLeave=a->tabBandH();KillTimer(h,4);a->ringHoldPending=false;KillTimer(h,3);a->arrowHoldPending=false;a->hover.clear();if(a->host)a->host->hover.clear();KillTimer(h,2);if(a->host)a->host->hideTip();a->invalidate(false);if(bandHLeave>0){RECT br={0,0,a->width,int(bandHLeave*a->scale)};InvalidateRect(h,&br,FALSE);}return 0;}
    case WM_LBUTTONDOWN:{float x=GET_X_LPARAM(lp)/a->scale,y=GET_Y_LPARAM(lp)/a->scale;bool expandClick=false;for(const auto& b:a->chemistryQuick)if(b.cmd=="@expandGroups"&&b.r.Contains(x,y))expandClick=true;if(!expandClick)a->expandPending=false;float bh=a->tabBandH();a->ringHoldPending=false;a->ringHoldFired=false;if(a->ringButtonAt(x,y)){a->ringHoldPending=true;a->ringHoldPoint=Vec2(x,y);SetTimer(h,4,500,0);}a->arrowHoldPending=false;a->arrowHoldFired=false;if(a->arrowButtonAt(x,y)){a->arrowHoldPending=true;a->arrowHoldPoint=Vec2(x,y);SetTimer(h,3,500,0);}bool onControl=false;for(auto& b:a->quick)if(b.r.Contains(x,y))onControl=true;for(auto& b:a->windowControls)if(b.r.Contains(x,y))onControl=true;for(auto& t:a->ribbonTabs)if((t.arg!=3||a->aiTabVisible)&&t.r.Contains(x,y))onControl=true;if(!a->s.selected.empty()&&a->infoRect.Contains(x,y))onControl=true;if(bh>0&&y<bh&&x>=210.f&&x<=a->width/a->scale-344.f){auto hit=a->host->hitBand(x,y);if(!hit.kind.empty()){KillTimer(h,2);a->host->hideTip();a->host->bandClick(hit);return 0;}}if(a->tabChild&&y<bh&&!onControl){KillTimer(h,2);a->host->hideTip();ReleaseCapture();studio_i18n::sendMessage(GetAncestor(h,GA_ROOT),WM_NCLBUTTONDOWN,HTCAPTION,0);return 0;}break;}
    case WM_RBUTTONUP:{float x=GET_X_LPARAM(lp)/a->scale,y=GET_Y_LPARAM(lp)/a->scale;float bh=a->tabBandH();if(a->ringButtonAt(x,y)){KillTimer(h,4);a->ringHoldPending=false;a->ringMenu();return 0;}if(a->arrowButtonAt(x,y)){KillTimer(h,3);a->arrowHoldPending=false;a->arrowHoldFired=false;a->arrowMenu();a->invalidate();return 0;}if(bh>0&&y<bh&&x>=210.f&&x<=a->width/a->scale-344.f){auto hit=a->host->hitBand(x,y);if(hit.kind=="tab")a->host->selectTab(hit.index);if(hit.kind=="plus"||hit.kind=="tab")a->host->plusMenu();return 0;}break;}
    case WM_LBUTTONUP:{float x=GET_X_LPARAM(lp)/a->scale,y=GET_Y_LPARAM(lp)/a->scale;float bh=a->tabBandH();bool ringHeld=a->ringHoldFired;a->ringHoldPending=false;a->ringHoldFired=false;KillTimer(h,4);if(ringHeld)return 0;bool arrowHeld=a->arrowHoldFired;a->arrowHoldPending=false;a->arrowHoldFired=false;KillTimer(h,3);if(arrowHeld)return 0;if(bh>0&&y<bh&&x>=210.f&&x<=a->width/a->scale-344.f)return 0;for(const auto& item:a->compactMenus)if(item.first.Contains(x,y)){a->showRibbonGroup(item.second);return 0;}for(const auto& tab:a->ribbonTabs)if((tab.arg!=3||a->aiTabVisible)&&tab.r.Contains(x,y)){a->switchTab(tab.arg);return 0;}for(auto& b:a->windowControls)if(b.r.Contains(x,y)){a->execute(b);return 0;}for(auto& gr:a->visibleGroups())for(auto& b:gr.items)if(b.r.Contains(x,y)){if(b.id=="chooseArrowTool_Large_Solid"&&x>=b.r.GetRight()-8){a->arrowMenu();a->invalidate();}else if(b.cmd=="chooseTLCPlateTool"&&x>=b.r.GetRight()-12){a->tlcMenu();}else a->execute(b);return 0;}for(auto& b:a->quick)if(b.r.Contains(x,y)){a->execute(b);return 0;}if(!a->ribbonTab)for(auto& b:a->chemistryQuick)if(b.r.Contains(x,y)){a->execute(b);return 0;}if(a->infoRect.Contains(x,y)){a->showMassCalculator();return 0;}return 0;}
    case WM_HSCROLL:case WM_VSCROLL:{
        HWND ctrl=(HWND)lp;if(!ctrl)return 0;SCROLLINFO si={};si.cbSize=sizeof(si);si.fMask=SIF_ALL;if(!GetScrollInfo(ctrl,SB_CTL,&si))return 0;int pos=si.nPos;
        switch(LOWORD(wp)){
            case SB_LINELEFT:pos-=40;break;
            case SB_LINERIGHT:pos+=40;break;
            case SB_PAGELEFT:pos-=std::max(1,int(si.nPage));break;
            case SB_PAGERIGHT:pos+=std::max(1,int(si.nPage));break;
            case SB_THUMBTRACK:case SB_THUMBPOSITION:pos=si.nTrackPos;break;
            case SB_LEFT:pos=si.nMin;break;
            case SB_RIGHT:pos=si.nMax;break;
            default:return 0;
        }
        int maxPos=std::max(0,si.nMax-int(si.nPage)+1);pos=std::max(0,std::min(maxPos,pos));
        a->applyScroll((m==WM_HSCROLL)?SB_HORZ:SB_VERT,pos);return 0;
    }
    case WM_MOUSEWHEEL:if(a->tabBandH()>0){POINT wheelPoint={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&wheelPoint);float wy=wheelPoint.y/a->scale;if(wy<a->tabBandH()){int delta=GET_WHEEL_DELTA_WPARAM(wp);a->host->scrollBy(delta>0?-60.f:60.f);a->host->layoutTabs(float(a->width)/a->scale);RECT wr={0,int(40*a->scale),a->width,int((40+a->tabBandH())*a->scale)};InvalidateRect(h,&wr,FALSE);return 0;}}break;
    case WM_SETFOCUS:if(a->canvas){SetFocus(a->focusSurface());return 0;}break;
    case WM_KEYDOWN:if(a->canvas){studio_i18n::sendMessage(a->canvas,WM_KEYDOWN,wp,lp);return 0;}break;
    case WM_DROPFILES:if(a->host){a->host->dropFiles((HDROP)wp);return 0;}break;
    case WM_NOTIFY:if(a->textEditor.notify((NMHDR*)lp)){if(!a->textEditor.loading&&((NMHDR*)lp)->code==EN_SELCHANGE)a->syncTextControls();return 0;}break;
    case WM_COMMAND:if(LOWORD(wp)==304||LOWORD(wp)==305){a->finishText(LOWORD(wp)==304);return 0;}if((LOWORD(wp)==401||LOWORD(wp)==402)&&(HIWORD(wp)==CBN_SELCHANGE||HIWORD(wp)==CBN_KILLFOCUS)){a->styleFromBox((HWND)lp,HIWORD(wp)==CBN_SELCHANGE);return 0;}if(LOWORD(wp)==301&&HIWORD(wp)==EN_CHANGE&&!a->textEditor.loading)a->textEditor.noteContentChange();return 0;
    case studio_i18n::languageMessage:a->ai.bgValid=false;a->layout();a->invalidate();return 0;
    case WM_APP+31:a->invalidate();return 0;
    case WM_APP+40:{
        // One-shot startup fit after the window has reached its final size.
        a->layout();
        if(a->s.doc.atoms().size()||a->s.doc.stats().aliveBonds)a->s.fit();
        a->invalidate();
        return 0;
    }
    case WM_APP+32:a->execute(Button("","","",wp=='S'?"@save":wp=='O'?"@open":"@new"));return 0;
    case WM_TIMER:if(wp==94){KillTimer(h,94);if(a->embedded&&a->oleObject){wchar_t mode[16]={};GetEnvironmentVariableW(L"MOLECULE_STUDIO_OLE_CYCLE_TEST",mode,16);if(wcscmp(mode,L"edit")==0){for(const auto& atom:a->s.doc.atoms().records())if(atom.isAlive()){a->s.doc.setElement(atom.id,atom.element==6?7:6);a->s.refresh();break;}a->s.doc.documentExtensions().set("studio.test.ole.cycle",std::to_string(GetTickCount64()));a->oleObject->markDirty();}a->finishEmbedded();}return 0;}if(wp==71){KillTimer(h,71);a->resizePending=false;if(a->canvas){a->layout();InvalidateRect(a->canvas,0,FALSE);}return 0;}if(wp==4){KillTimer(h,4);if(a->ringHoldPending){a->ringHoldPending=false;a->ringHoldFired=true;a->ringMenu();}return 0;}if(wp==3){KillTimer(h,3);if(a->arrowHoldPending){a->arrowHoldPending=false;a->arrowHoldFired=true;a->arrowMenu();a->invalidate();}return 0;}if(wp==2){KillTimer(h,2);if(a->host)a->host->showTip();return 0;}KillTimer(h,1);if(a->closeChecking){a->demo();a->render();a->s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return 0;}if(a->benchmarking){a->benchmark();PostQuitMessage(0);return 0;}if(a->testing){try{if(a->customTemplateChecking)a->customTemplateCheck(a->testOut);else if(a->tableChecking)a->tableCheck(a->testOut);else if(a->uiChecking)a->settingsAiCheck(a->testOut);else if(a->aiChecking)a->aiCheck(a->testOut);else if(a->chromeChecking){if(std::getenv("MOLECULE_STUDIO_FORMAT_CHECK")){a->graphicStyleCheck(a->testOut);a->s.dirty=false;PostQuitMessage(0);}else a->chromeCheck(a->testOut);}else if(a->templateLibraryChecking)a->templateLibraryCheck(a->testOut);else if(a->ringLibraryChecking)a->ringLibraryCheck(a->testOut);else if(a->textExtendChecking)a->textExtendCheck(a->testOut);else if(a->groupDirectionChecking)a->groupDirectionCheck(a->testOut);else if(a->completionChecking)a->completionCheck(a->testOut);else if(a->stereoLabelChecking)a->stereoLabelCheck(a->testOut);else if(a->arrowShortcutChecking)a->arrowShortcutCheck(a->testOut);else if(a->textWrapChecking){if(std::getenv("MOLECULE_STUDIO_INTEROP_RING_CHECK"))a->interopRingCheck(a->testOut);else if(std::getenv("MOLECULE_STUDIO_INSERT_CHECK"))a->insertionPerformanceCheck(a->testOut);else if(std::getenv("MOLECULE_STUDIO_PATTERN_CHECK"))a->patternPerformanceCheck(a->testOut);else if(std::getenv("MOLECULE_STUDIO_MOVE_CHECK"))a->moveCheck(a->testOut);else a->textWrapCheck(a->testOut);}else a->selftest();}catch(const std::exception& e){std::ofstream(a->testOut+"/failure.txt")<<e.what();PostQuitMessage(3);}return 0;}break;
    case WM_CLOSE:if(a->embedded&&!a->testing){a->finishEmbedded();return 0;}if(!a->testing&&!a->leave())return 0;
        // Destroying the clipboard owner makes Windows render every offered
        // format synchronously (WM_RENDERALLFORMATS) on this thread; a large
        // structure froze the app for tens of seconds at close. Drop the OLE
        // clipboard first - pasting later needs a fresh copy either way.
        if(!a->host){OleSetClipboard(nullptr);if(g_oleClipboard){g_oleClipboard->Release();g_oleClipboard=nullptr;}}
        a->shutdownRenderer();DestroyWindow(h);return 0;
    case WM_DESTROY:a->shutdownRenderer();if(a->host)a->host->tabDestroyed(a);
        if(a->quitOnDestroy){
            // Last window: see WM_CLOSE - never let the OLE clipboard render
            // every format synchronously while the process tears down.
            OleSetClipboard(nullptr);if(g_oleClipboard){g_oleClipboard->Release();g_oleClipboard=nullptr;}
            PostQuitMessage(0);
        }return 0;
    }return DefWindowProcW(h,m,wp,lp);
}
void App::screenshot(const std::string& path,bool chrome,bool statusBar){Bitmap bitmap(width,height,PixelFormat32bppARGB);Graphics g(&bitmap);paint(g,chrome,statusBar);g.ResetTransform();if(rc)wglMakeCurrent(dc,rc);auto overlay=s.overlay();render::RenderStats stats;
#ifdef MOLECULE_STUDIO_DX11
    if(dx11Renderer){dx11Renderer->setCopyPreview(nullptr,Vec2(),0);static const std::vector<dx11::Renderer::RotationTarget> empty;dx11Renderer->setRotationPreview(nullptr,empty,nullptr,0);}
#endif
    if(transformDragging){const auto& preview=selectionTransformScene();gl->draw(preview,nullptr,s.renderView,&stats);}else if(tableDragPending&&tableDragActive){const auto& preview=tableDragScene();gl->draw(preview,nullptr,s.renderView,&stats);}else if(cloneDragging&&cloneSource){auto preview=clonePreviewOverlay(overlay.scene());gl->draw(s.gpuScene,&preview,s.renderView,&stats);}else gl->draw(s.displayScene(),&overlay.scene(),s.renderView,&stats);if(rc)glFinish();std::vector<uint8_t> px;gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&px);int w=s.renderView.widthPx,hg=s.renderView.heightPx;
    for(size_t i=0;i<px.size();i+=4)std::swap(px[i],px[i+2]);Bitmap cv(w,hg,-w*4,PixelFormat32bppARGB,px.data()+size_t(hg-1)*w*4);g.DrawImage(&cv,int(16*scale),top,w,hg);
    UINT n=0,sz=0;GetImageEncodersSize(&n,&sz);std::vector<uint8_t> enc(sz);auto list=(ImageCodecInfo*)enc.data();GetImageEncoders(n,sz,list);for(UINT i=0;i<n;i++)if(wcscmp(list[i].MimeType,L"image/png")==0)bitmap.Save(W(path).c_str(),&list[i].Clsid,0);
}
// "复制为 PNG": render exactly the selected objects (content only, current
// document style) into an offscreen white canvas and publish both the original
// PNG bytes and a CF_DIB so every pasting application has a raster it can read.
// The document and the undo stack are never touched.
Error App::copySelectionPng(){
    if(s.selected.empty())return Error(ErrorCode::InvalidArgument,"请先选择要复制的结构");
    if(s.scene.empty())s.refresh();
    const auto refs=studioio::expanded(s.doc,s.selected);
    const Box bounds=studioio::selectionBounds(s);
    if(bounds.empty())return Error(ErrorCode::NotFound,"选区没有可绘制的内容");
    // Document units are PostScript points. 300 DPI is a printable resolution and
    // about 4x a 100% screen view, so small structures stay crisp without a huge
    // surface. Oversized selections are scaled down instead of failing.
    double scale=300.0/72.0;
    const double contentWidth=std::max(1.0,bounds.width()),contentHeight=std::max(1.0,bounds.height());
    const int padding=10; // device pixels of white margin on every side
    const double maxEdge=4096.0,maxPixels=16.0*1024.0*1024.0;
    struct PixelSize{int width;int height;};
    auto extent=[&](double factor){
        PixelSize size;
        size.width=int(std::ceil(contentWidth*factor))+padding*2;
        size.height=int(std::ceil(contentHeight*factor))+padding*2;
        return size;
    };
    PixelSize size=extent(scale);
    if(std::max(size.width,size.height)>maxEdge){scale*=maxEdge/std::max(size.width,size.height);size=extent(scale);}
    if(double(size.width)*double(size.height)>maxPixels){scale*=std::sqrt(maxPixels/(double(size.width)*double(size.height)));size=extent(scale);}
    if(size.width<8||size.height<8||size.width>8192||size.height>8192)return Error(ErrorCode::InvalidArgument,"选区尺寸无效，无法生成 PNG");
    render::Scene selectedScene;
    for(const auto& primitive:s.scene.primitives())if(refs.count(primitive.source))selectedScene.add(primitive);
    if(selectedScene.empty())return Error(ErrorCode::NotFound,"选区没有可绘制的内容");
    compatible::Renderer renderer;std::string rendererWhy;
    if(!renderer.initialize(&rendererWhy))return Error(ErrorCode::IoError,"无法创建离屏渲染器"+(rendererWhy.empty()?std::string():std::string("：")+rendererWhy));
    render::View view=s.renderView;
    view.widthPx=double(size.width);view.heightPx=double(size.height);
    view.clipEnabled=false;
    view.transform.center=(bounds.min+bounds.max)*0.5;
    view.transform.viewportCenter=Vec2(size.width*0.5,size.height*0.5);
    view.transform.scale=scale;view.transform.dpiScale=1.0;
    render::RenderStats stats;
    Error e=renderer.draw(selectedScene,nullptr,view,&stats);
    if(!e.ok())return e;
    std::vector<uint8_t> rgba;
    e=renderer.readPixels(0,0,size.width,size.height,&rgba);
    if(!e.ok())return e;
    renderer.destroy();
    if(rgba.size()!=size_t(size.width)*size_t(size.height)*4)return Error(ErrorCode::IoError,"离屏渲染像素尺寸不一致");
    // Backend::readPixels hands back rows bottom-up (the canvas screenshot path
    // flips them the same way). GDI+ wants a top-down BGRA buffer for 32bpp, so
    // flip once here; the CF_DIB below is bottom-up and can reuse the rows by
    // copying them in this same order.
    std::vector<uint8_t> bgra(rgba.size());
    for(int y=0;y<size.height;++y){
        const uint8_t* source=&rgba[size_t(size.height-1-y)*size_t(size.width)*4];
        uint8_t* target=&bgra[size_t(y)*size_t(size.width)*4];
        for(int x=0;x<size.width;++x){target[size_t(x)*4]=source[size_t(x)*4+2];target[size_t(x)*4+1]=source[size_t(x)*4+1];target[size_t(x)*4+2]=source[size_t(x)*4];target[size_t(x)*4+3]=255;}
    }
    Bitmap image(size.width,size.height,size.width*4,PixelFormat32bppARGB,bgra.data());
    if(image.GetLastStatus()!=Ok)return Error(ErrorCode::IoError,"无法创建 PNG 图像");
    UINT encoderCount=0,encoderBytes=0;GetImageEncodersSize(&encoderCount,&encoderBytes);
    std::vector<uint8_t> encoderStorage(encoderBytes);auto encoders=(ImageCodecInfo*)encoderStorage.data();
    CLSID pngEncoder={};bool encoderFound=false;
    if(encoderBytes&&GetImageEncoders(encoderCount,encoderBytes,encoders)==Ok)for(UINT i=0;i<encoderCount;++i)if(encoders[i].MimeType&&wcscmp(encoders[i].MimeType,L"image/png")==0){pngEncoder=encoders[i].Clsid;encoderFound=true;break;}
    if(!encoderFound)return Error(ErrorCode::IoError,"PNG 编码器不可用");
    IStream* stream=nullptr;
    if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))||!stream)return Error(ErrorCode::IoError,"无法创建 PNG 编码流");
    if(image.Save(stream,&pngEncoder,nullptr)!=Ok){stream->Release();return Error(ErrorCode::IoError,"PNG 编码失败");}
    std::vector<uint8_t> png;STATSTG streamStat={};
    if(SUCCEEDED(stream->Stat(&streamStat,STATFLAG_NONAME))&&streamStat.cbSize.QuadPart>0&&streamStat.cbSize.QuadPart<64ull*1024ull*1024ull){
        HGLOBAL memory=nullptr;
        if(SUCCEEDED(GetHGlobalFromStream(stream,&memory))&&memory){
            void* locked=GlobalLock(memory);
            if(locked){png.assign(static_cast<uint8_t*>(locked),static_cast<uint8_t*>(locked)+size_t(streamStat.cbSize.QuadPart));GlobalUnlock(memory);}
        }
    }
    stream->Release();
    if(png.size()<8||png[0]!=0x89||png[1]!=0x50||png[2]!=0x4E||png[3]!=0x47)return Error(ErrorCode::IoError,"PNG 编码结果无效");
    // CF_DIB: a plain bottom-up 24-bit BI_RGB image is what Word, PowerPoint and
    // chat clients paste most reliably; the white background means no alpha is
    // needed. A 32-bit CF_DIBV5 is deliberately not offered because some Office
    // builds interpret its alpha channel.
    const int stride=((size.width*24+31)/32)*4;
    HGLOBAL dib=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(BITMAPINFOHEADER)+size_t(stride)*size_t(size.height));
    if(!dib)return Error(ErrorCode::IoError,"剪贴板内存不足");
    {
        BITMAPINFOHEADER* header=static_cast<BITMAPINFOHEADER*>(GlobalLock(dib));
        if(!header){GlobalFree(dib);return Error(ErrorCode::IoError,"剪贴板内存分配失败");}
        header->biSize=sizeof(BITMAPINFOHEADER);header->biWidth=size.width;header->biHeight=size.height;header->biPlanes=1;header->biBitCount=24;header->biCompression=BI_RGB;header->biSizeImage=DWORD(size_t(stride)*size_t(size.height));
        uint8_t* pixels=reinterpret_cast<uint8_t*>(header+1);
        for(int y=0;y<size.height;++y){
            const uint8_t* source=&bgra[size_t(y)*size_t(size.width)*4];
            uint8_t* target=pixels+size_t(size.height-1-y)*size_t(stride);
            for(int x=0;x<size.width;++x){target[size_t(x)*3]=source[size_t(x)*4];target[size_t(x)*3+1]=source[size_t(x)*4+1];target[size_t(x)*3+2]=source[size_t(x)*4+2];}
        }
        GlobalUnlock(dib);
    }
    HGLOBAL pngPayload=GlobalAlloc(GMEM_MOVEABLE,png.size());
    if(!pngPayload){GlobalFree(dib);return Error(ErrorCode::IoError,"剪贴板内存不足");}
    {void* locked=GlobalLock(pngPayload);if(!locked){GlobalFree(pngPayload);GlobalFree(dib);return Error(ErrorCode::IoError,"剪贴板内存分配失败");}memcpy(locked,png.data(),png.size());GlobalUnlock(pngPayload);}
    if(!OpenClipboard(h)){GlobalFree(pngPayload);GlobalFree(dib);return Error(ErrorCode::Locked,"剪贴板被占用，请重试");}
    if(!EmptyClipboard()){CloseClipboard();GlobalFree(pngPayload);GlobalFree(dib);return Error(ErrorCode::IoError,"无法清空剪贴板");}
    const UINT pngFormat=RegisterClipboardFormatW(L"PNG");
    bool pngStored=pngFormat&&SetClipboardData(pngFormat,pngPayload)!=nullptr;
    if(pngStored)pngPayload=nullptr;
    bool dibStored=pngStored&&SetClipboardData(CF_DIB,dib)!=nullptr;
    if(dibStored)dib=nullptr;
    if(!pngStored||!dibStored){
        // Roll back so a consumer can never see half an image: the formats
        // already handed over belong to the clipboard and are cleared with it.
        EmptyClipboard();
        if(pngPayload)GlobalFree(pngPayload);
        if(dib)GlobalFree(dib);
        CloseClipboard();
        return Error(ErrorCode::IoError,"写入剪贴板失败，已回滚");
    }
    CloseClipboard();
    s.status="已复制为 PNG（"+std::to_string(size.width)+"×"+std::to_string(size.height)+"）";
    return Error();
}
#include "ai_checks.hpp"
#include "graphic_style_checks.hpp"
#include "cdxml_color_checks.hpp"
#include "chrome_checks.hpp"
#include "stereo_label_checks.hpp"
#include "text_wrap_checks.hpp"
#include "arrow_shortcut_checks.hpp"
#include "text_extend_checks.hpp"
#include "ring_library_checks.hpp"
#include "template_library_checks.hpp"
#include "settings_ai_checks.hpp"
#include "table_checks.hpp"
void App::probe(const std::string& dir){
    SetWindowPos(h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);layout();
    auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();};
    auto place=[&](const std::string& cmd){bool ok=s.activateCommand(cmd);s.gesture(PointerPhase::Down,Vec2(),0);s.gesture(PointerPhase::Up,Vec2(),0);std::ofstream f(dir+"/dump.txt",std::ios::app);f<<"place "<<cmd<<" ok="<<ok<<" active="<<s.entries[s.activeIndex].command<<" owner="<<s.entries[s.activeIndex].owner<<" lim="<<s.entries[s.activeIndex].limitation<<"\n";};
    auto shot=[&](const std::string& name){s.refresh();s.fit();invalidate();UpdateWindow(h);UpdateWindow(canvas);screenshot(dir+"/"+name+".png",false,false);};
    auto dumpRings=[&](const std::string& name){
        std::ofstream f(dir+"/dump.txt",std::ios::app);
        f<<"== "<<name<<" ==\n";
        r62::integration::RingGeometry rg(s.doc);
        f<<"spacingPercent=18 lineWidth="<<s.doc.defaultLineWidth()<<" bondLen="<<s.doc.defaultBondLength()<<"\n";
        for(const auto& b:s.doc.bonds().records()){if(!b.isAlive())continue;
            Vec2 a=s.doc.atoms().get(b.beginAtom)->position,z=s.doc.atoms().get(b.endAtom)->position;
            f<<"bond id="<<b.id<<" order="<<int(b.order)<<" A="<<b.beginAtom<<"("<<a.x<<","<<a.y<<") Z="<<b.endAtom<<"("<<z.x<<","<<z.y<<")\n";
            auto d=rg.build(b);
            f<<"  note="<<d.note<<" shapes="<<d.shapes.size()<<"\n";
            for(size_t i=0;i<d.shapes.size();++i){f<<"   shape"<<i<<" pts=";for(auto p:d.shapes[i].points)f<<"("<<p.x<<","<<p.y<<")";f<<"\n";}
        }
    };
    reset();{auto result=persistence::readCdxml(projectAcetoneXml(),s.doc);if(!result.ok())throw std::runtime_error(result.message);s.refresh();shot("carbonyl_project");}
    reset();place("chooseRingTool_Cyclopentane");shot("ring5");
    reset();place("chooseRingTool_Cyclopentadiene");dumpRings("diene");shot("diene");
    reset();place("chooseRingTool_Benzene");shot("benzene");
    reset();place("chooseRingTool_Chair_Cyclohexane1");dumpRings("chair1");shot("chair1");
    reset();place("chooseRingTool_Chair_Cyclohexane2");dumpRings("chair2");shot("chair2");
    auto dbg=[&](const char* tag){std::ofstream f(dir+"/dump.txt",std::ios::app);
        f<<"[dbg] "<<tag<<" syms="<<s.doc.symbols().size()<<" atoms="<<s.doc.stats().aliveAtoms
         <<" toolid="<<(s.entries[s.activeIndex].tool?s.entries[s.activeIndex].tool->id():std::string("<none>"))<<"\n";
        s.refresh();int i=0;
        for(const auto&pr:s.scene.primitives())f<<"  p"<<i++<<" "<<render::primKindName(pr.kind)<<" text="<<pr.text<<"\n";};
    reset();place("chooseSymbolTool_CirclePlus");dbg("plus");shot("plus");
    reset();place("chooseSymbolTool_CircleMinus");dbg("minus");shot("minus");
    reset();s.activateCommand("chooseTLCPlateTool");s.gesture(PointerPhase::Down,Vec2(-110,-85),0);s.gesture(PointerPhase::Move,Vec2(110,85),0);s.gesture(PointerPhase::Up,Vec2(110,85),0);shot("tlc");
    reset();s.activateCommand("chooseAlkaneTool_Snaking_Chain");s.gesture(PointerPhase::Down,Vec2(-110,0),0);s.gesture(PointerPhase::Move,Vec2(120,25),0);s.gesture(PointerPhase::Up,Vec2(120,25),0);shot("chain");
    auto raw=[&](const std::string& name){invalidate();UpdateWindow(h);UpdateWindow(canvas);screenshot(dir+"/"+name+".png",false,false);};
    reset();place("chooseRingTool_Benzene");s.fit();
    s.activateCommand("chooseSelectionTool_Marquee");
    s.gesture(PointerPhase::Down,Vec2(-30,-20),0);s.gesture(PointerPhase::Move,Vec2(10,12),0);raw("marquee_mid");
    s.gesture(PointerPhase::Up,Vec2(10,12),0);raw("marquee_after");
    reset();place("chooseRingTool_Benzene");s.fit();s.selected.clear();
    s.activateCommand("chooseEdittingTool_Text");s.gesture(PointerPhase::Down,Vec2(40,30),0);s.gesture(PointerPhase::Up,Vec2(40,30),0);
    s.selected=s.all();raw("text_after");
    s.dirty=false;PostQuitMessage(0);
}
void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
void App::benchmark(){
    s.activateCommand("chooseRingTool_Benzene");s.gesture(PointerPhase::Down,Vec2(),0);s.gesture(PointerPhase::Up,Vec2(),0);s.activateCommand("chooseSelectionTool_Marquee");
    invalidate();UpdateWindow(h);UpdateWindow(canvas);
    std::ofstream out(testOut);const char* backendName=gl?gl->name():"none";const char* backendVendor=compatibleRenderer?"Windows GDI+ (VMware driver compatibility)":
#ifdef MOLECULE_STUDIO_DX11
        dx11Renderer?"Direct3D 11 (hardware or WARP)":
#endif
        (gl?reinterpret_cast<const char*>(glGetString(GL_VENDOR)):"none");out<<"renderer="<<backendName<<"\nvendor="<<backendVendor<<"\nsize="<<width<<"x"<<height<<" dpi="<<scale<<"\n";
    for(int drag=0;drag<2;++drag){shellPaints=canvasPaints=0;shellMs=canvasMs=0;double start=now();
        if(drag)studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));
        for(int i=0;i<60;++i){studio_i18n::sendMessage(canvas,WM_MOUSEMOVE,drag?MK_LBUTTON:0,MAKELPARAM(120+i*4,140+i));UpdateWindow(h);UpdateWindow(canvas);}
        if(drag)studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,MAKELPARAM(356,199));
        out<<(drag?"drag":"hover")<<" total_ms="<<now()-start<<" shell_paints="<<shellPaints<<" shell_ms="<<shellMs<<" canvas_paints="<<canvasPaints<<" canvas_ms="<<canvasMs<<"\n";
    }
    for(int drawing=0;drawing<2;++drawing){shellPaints=canvasPaints=0;shellMs=canvasMs=0;double start=now();
        s.activateCommand("chooseRingTool_Benzene");
        for(int i=0;i<20;++i){if(drawing){
            studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(200+i*80,300));
            studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,MAKELPARAM(200+i*80,300));
        }else{auto r=groups[0].items[i%6].r;studio_i18n::sendMessage(h,WM_MOUSEMOVE,0,MAKELPARAM(int((r.X+4)*scale),int((r.Y+4)*scale)));}
        UpdateWindow(h);UpdateWindow(canvas);}
        out<<(drawing?"draw_20_rings":"toolbar_hover_20")<<" total_ms="<<now()-start<<" shell_paints="<<shellPaints<<" shell_ms="<<shellMs<<" canvas_paints="<<canvasPaints<<" canvas_ms="<<canvasMs<<"\n";
    }
}
void App::benchmarkFile(const std::string& file){
    std::ofstream out(testOut);
    double loadStart=now();
    auto openError=s.open(file);
    double loadMs=now()-loadStart;
    out<<"file="<<file<<"\nopen_ok="<<(openError.ok()?1:0)<<" load_ms="<<loadMs<<"\n";
    if(!openError.ok()){out<<"open_error="<<openError.message<<"\n";return;}
    s.fit();updateInfo();invalidate();UpdateWindow(h);UpdateWindow(canvas);
    if(std::getenv("MOLECULE_STUDIO_MARQUEE_ONLY")){
        s.selected.clear();s.activateCommand("chooseSelectionTool_Marquee");s.cancel();
        int x=3,y=3;while(y<80&&s.pick(s.view().toDocument(Vec2(x,y))).valid())y+=4;
        studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));UpdateWindow(canvas);
        bool deferred=s.selectionDragActive();auto revision=s.serial;auto rebuild=s.rebuilds;auto events=s.input.stats().events;auto formulas=formulaBuilds;auto backgrounds=marqueeBackgroundBuilds;
        double started=now();const int frames=80;
        for(int i=0;i<frames;++i){int px=100+int((s.renderView.widthPx-120)*i/(frames-1)),py=100+int((s.renderView.heightPx-120)*i/(frames-1));studio_i18n::sendMessage(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(px,py));UpdateWindow(canvas);deferred=deferred&&s.selected.empty()&&s.serial==revision&&s.rebuilds==rebuild;}
        double movesMs=now()-started;auto movesEvents=s.input.stats().events-events;auto dragFormulaBuilds=formulaBuilds-formulas;
        started=now();studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,MAKELPARAM(int(s.renderView.widthPx)-20,int(s.renderView.heightPx)-20));double releaseMs=now()-started;
        out<<"marquee_frames="<<frames<<" move_ms="<<movesMs<<" per_frame_ms="<<movesMs/frames<<" release_ms="<<releaseMs<<" selected="<<s.selected.size()<<" deferred_selection="<<deferred<<" routed_move_events="<<movesEvents<<" formula_rebuilds_during_drag="<<dragFormulaBuilds<<" backdrop_rebuilds_during_drag="<<(marqueeBackgroundBuilds-backgrounds)<<" fast_frames="<<marqueeFastFrames<<"\n";
        out<<"checks="<<((deferred&&!s.selected.empty()&&!s.selectionDragActive())?"PASS":"FAIL")<<"\n";s.cancel();return;
    }
    int effectiveSamples=0;
#ifdef MOLECULE_STUDIO_DX11
    if(dx11Renderer)effectiveSamples=dx11Renderer->samples();
#endif
    out<<"samples="<<effectiveSamples<<"\n";
    int editX=0,editY=0;ObjectId editTarget=kInvalidObjectId;double lastOpMs=0;
    auto measure=[&](const char* label,int frames,int mode){
        shellPaints=canvasPaints=0;shellMs=canvasMs=0;
        std::vector<double> samples;
        double start=now();uint64_t buildUs=0,drawUs=0,syncUs=0,batchUs=0,vbUs=0;int panWhy=0;double ovMs=0,scMs=0,drMs=0,prMs=0,guMs=0,opMs=0;size_t meshRebuilds=0;
        for(int i=0;i<frames;++i){
            if(mode==1){auto v=s.view();v.center.x+=6;s.setView(v);}
            else if(mode==2){auto v=s.view();v.scale*=(i%2?1.04:1.0/1.04);s.setView(v);}
            else if(mode==3){if(i%2)s.selected=s.all();else s.selected.clear();}
            else if(mode==4){int w=1000+(i%5)*220,h=680+(i%4)*130;SetWindowPos(this->h,0,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER);layout();}
            else if(mode==5){studio_i18n::sendMessage(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(editX+i*3,editY));}
            else if(mode==6){s.undo(i%2!=0);}
            else if(mode==7){auto* atom=s.doc.atoms().mutableGet(editTarget);if(atom&&atom->isAlive()){atom->element=(i%2)?6:8;double t=now();s.refresh();lastOpMs=now()-t;}}
            else if(mode==8){auto* anchor=s.doc.atoms().mutableGet(editTarget);if(anchor&&anchor->isAlive()){ObjectId id;Vec2 p=anchor->position+Vec2(12.0+i*2.0,12.0);if(s.doc.addAtom(p,6,&id).ok())s.doc.addBond(editTarget,id,nullptr);double t=now();s.refresh();lastOpMs=now()-t;}}
            else if(mode==9){if(editTarget!=kInvalidObjectId){ObjectId anchor=editTarget;double t=now();s.submit(Command("perf add",kInvalidateStructure|kInvalidateGeometry,[anchor](CommandContext& c){auto* a=c.document.atoms().alive(anchor);if(!a)return CommandResult::success();ObjectId id;if(c.document.addAtom(a->position+Vec2(12.0,12.0),6,&id).ok())c.document.addBond(anchor,id,nullptr);return CommandResult::success();}),"perf add");lastOpMs=now()-t;}}
            else if(mode==10){if(editTarget!=kInvalidObjectId){ObjectId id=editTarget;double t=now();s.submit(Command("perf element",kInvalidateStructure|kInvalidateGeometry,[id](CommandContext& c){auto* a=c.document.atoms().mutableGet(id);if(a&&a->isAlive())a->element=(a->element==6)?8:6;return CommandResult::success();}),"perf element");lastOpMs=now()-t;}}
            double before=canvasMs;invalidate();UpdateWindow(canvas);
            samples.push_back(canvasMs-before);buildUs+=lastStats.buildMicros;drawUs+=lastStats.drawMicros;
            ovMs+=lastOverlayMs;scMs+=lastSceneMs;drMs+=lastDrawMs;prMs+=lastPresentMs;guMs+=lastGuidesMs;meshRebuilds+=lastStats.meshRebuilds;opMs+=lastOpMs;lastOpMs=0;
#ifdef MOLECULE_STUDIO_DX11
            if(dx11Renderer){syncUs+=dx11Renderer->syncMicros();batchUs+=dx11Renderer->batchMicros();vbUs+=dx11Renderer->vertexMicros();panWhy=dx11Renderer->panWhy();}
#endif
        }
        std::sort(samples.begin(),samples.end());
        double p50=samples.empty()?0:samples[samples.size()/2];
        double p95=samples.empty()?0:samples[std::min<size_t>(samples.size()-1,size_t(samples.size()*0.95))];
        out<<label<<" frames="<<frames<<" canvas_paints="<<canvasPaints
           <<" canvas_ms="<<canvasMs<<" avg_ms="<<(canvasPaints?canvasMs/canvasPaints:0)
           <<" build_us="<<(frames?buildUs/frames:0)<<" draw_us="<<(frames?drawUs/frames:0)
           <<" sync_us="<<(frames?syncUs/frames:0)<<" batch_us="<<(frames?batchUs/frames:0)<<" vb_us="<<(frames?vbUs/frames:0)<<" pan_why="<<panWhy
           <<" ov_ms="<<(frames?ovMs/frames:0)<<" sc_ms="<<(frames?scMs/frames:0)<<" dr_ms="<<(frames?drMs/frames:0)<<" pr_ms="<<(frames?prMs/frames:0)<<" gu_ms="<<(frames?guMs/frames:0)
           <<" mesh_rebuilds="<<(frames?meshRebuilds/frames:0)<<" op_ms="<<(frames?opMs/frames:0)
           <<" min_ms="<<(samples.empty()?0:samples.front())
           <<" p50_ms="<<p50<<" p95_ms="<<p95<<" max_ms="<<(samples.empty()?0:samples.back())
           <<" wall_ms="<<(now()-start)<<"\n";
    };
    auto setZoom=[&](double zoom){s.selected.clear();s.fit();if(zoom!=1.0){auto v=s.view();v.scale*=zoom;s.setView(v);}invalidate();UpdateWindow(canvas);};
    measure("static",20,0);
    s.fit();invalidate();UpdateWindow(canvas);
    measure("pan",40,1);
    s.fit();invalidate();UpdateWindow(canvas);
    measure("zoom",30,2);
    s.fit();invalidate();UpdateWindow(canvas);
    measure("overlay",30,3);
    for(double z: {2.0,4.0,8.0}){
        std::string n=std::to_string(int(z));
        setZoom(z);measure(("static_x"+n).c_str(),15,0);
        measure(("pan_x"+n).c_str(),20,1);
    }
    SetWindowPos(h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);layout();
    measure("resize",12,4);

    // A real atom drag: press on an atom near the viewport centre, then move it
    // three device pixels per frame.  This exercises dragScene, not the command
    // history, exactly like an interactive move.
    s.selected.clear();s.fit();invalidate();UpdateWindow(h);UpdateWindow(canvas);
    Vec2 editDev;double bestDistance=1e18;
    for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()){
        Vec2 d=s.view().toDevice(atom.position);
        if(d.x<30||d.y<30||d.x>width-30||d.y>height-30)continue;
        double distance=std::hypot(d.x-width*0.5,d.y-height*0.5);
        if(distance<bestDistance){bestDistance=distance;editTarget=atom.id;editDev=d;}
    }
    bool editHit=false;
    if(editTarget!=kInvalidObjectId){
        editX=int(editDev.x+0.5);editY=int(editDev.y+0.5);
        double pressStart=now();
        studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(editX,editY));
        out<<"edit_press_ms="<<(now()-pressStart)<<"\n";
        for(const auto& ref:s.selected)if(ref.kind==ObjectKind::Atom&&ref.id==editTarget)editHit=true;
        out<<"edit_hit="<<(editHit?1:0)<<" edit_atom="<<editTarget<<"\n";
        if(editHit){
            measure("edit_move",30,5);
            studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,MAKELPARAM(editX+87,editY));
            updateInfo();invalidate();UpdateWindow(canvas);
            screenshot(testOut+".edit.png",false,false);
            measure("undo_redo",10,6);
            s.selected.clear();s.fit();invalidate();UpdateWindow(canvas);
            measure("struct_element",10,7);
            measure("struct_add",8,8);
            measure("struct_cmd_add",8,9);
            measure("struct_cmd_element",10,10);
        }else{
            studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,MAKELPARAM(editX,editY));
        }
    }
    out<<"edit_done=1\n";
}

void App::canvasBench(const std::string& file){
    // Interactive repaint budget of one document, measured through the real
    // canvas render path (no WM_PAINT, so a slow frame cannot hide behind the
    // message queue).  Every phase a frame pays for is reported separately.
    std::ofstream out(testOut);
    double openStart=now();
    auto openError=s.open(file);
    out<<"file="<<file<<"\nopen_ok="<<(openError.ok()?1:0)<<" open_ms="<<(now()-openStart)<<"\n";
    if(!openError.ok()){out<<"open_error="<<openError.message<<"\n";return;}
    s.dirty=false;
    RECT cr={};GetClientRect(canvas,&cr);
    out<<"canvas_px="<<(cr.right-cr.left)<<"x"<<(cr.bottom-cr.top)<<"\n";
    auto drawFrame=[&](){double a=now();render();return now()-a;};
    s.fit();
    double warmMs=drawFrame();
    out<<"warmup_ms="<<warmMs;
#ifdef MOLECULE_STUDIO_DX11
    if(dx11Renderer)out<<" samples="<<dx11Renderer->samplesUsed()<<" resolve_us="<<dx11Renderer->resolveMicros()<<" readback_us="<<dx11Renderer->readbackMicros()<<" present_us="<<dx11Renderer->presentMicros();
#endif
    out<<" triangles="<<lastStats.triangles<<" prims="<<lastStats.primitivesTotal<<" drawn="<<lastStats.primitivesDrawn<<" culled="<<lastStats.primitivesCulled<<"\n";

    // Optional pixel dumps: two runs at the same view must produce the same
    // pixels, so a rendering optimisation can be checked instead of asserted.
    int dumpIndex=0;
    const char* dumpEnv=std::getenv("MOLECULE_STUDIO_BENCH_DUMP");
    auto dump=[&](const char* label){
        if(!dumpEnv||!gl)return;
        std::vector<uint8_t> pixels;
        if(!gl->readPixels(0,0,s.renderView.widthPx,s.renderView.heightPx,&pixels).ok())return;
        std::ostringstream name;name<<testOut<<".dump"<<dumpIndex++<<"."<<label<<".rgba";
        std::ofstream file(name.str(),std::ios::binary);
        file.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
        out<<"dump="<<name.str()<<" bytes="<<pixels.size()<<"\n";
    };
    std::function<void(const char*,int,const std::function<void(int)>&)> run=
        [&](const char* label,int frames,const std::function<void(int)>& step){
        std::vector<double> samples;
        double total=0,build=0,draw=0,resolve=0,readback=0,present=0,guides=0,overlay=0,scene=0;
        size_t triangles=0;
        for(int i=0;i<frames;++i){
            double frameStart=now();step(i);
            drawFrame();double ms=now()-frameStart;samples.push_back(ms);total+=ms;
            build+=double(lastStats.buildMicros);draw+=double(lastStats.drawMicros);
            present+=lastPresentMs;guides+=lastGuidesMs;overlay+=lastOverlayMs;scene+=lastSceneMs;
            triangles+=lastStats.triangles;
#ifdef MOLECULE_STUDIO_DX11
            if(dx11Renderer){resolve+=double(dx11Renderer->resolveMicros());readback+=double(dx11Renderer->readbackMicros());}
#endif
        }
        std::sort(samples.begin(),samples.end());
        out<<label<<" frames="<<frames<<" avg_ms="<<total/frames<<" p50_ms="<<samples[samples.size()/2]
           <<" min_ms="<<samples.front()<<" max_ms="<<samples.back()
           <<" build_us="<<build/frames<<" draw_us="<<draw/frames<<" resolve_us="<<resolve/frames<<" readback_us="<<readback/frames
           <<" present_ms="<<present/frames<<" guides_ms="<<guides/frames<<" overlay_ms="<<overlay/frames<<" scene_ms="<<scene/frames
           <<" triangles="<<(triangles/frames)<<" drawn="<<lastStats.primitivesDrawn<<" culled="<<lastStats.primitivesCulled<<"\n";
    };
    auto frameLine=[&](const char* label,int i,double ms){
        out<<label<<i<<" ms="<<ms<<" build_us="<<lastStats.buildMicros<<" draw_us="<<lastStats.drawMicros;
#ifdef MOLECULE_STUDIO_DX11
        if(dx11Renderer)out<<" hash_us="<<dx11Renderer->hashMicros()<<" fg_us="<<dx11Renderer->foregroundMicros()<<" sync_us="<<dx11Renderer->syncMicros()<<" batch_us="<<dx11Renderer->batchMicros()<<" vb_us="<<dx11Renderer->vertexMicros()<<" readback_us="<<dx11Renderer->readbackMicros();
#endif
        out<<"\n";
    };

    run("static",6,[](int){});
    dump("fit");
    run("pan_x",24,[&](int){auto v=s.view();v.center.x+=6;s.setView(v);});
    run("pan_y",24,[&](int){auto v=s.view();v.center.y+=6;s.setView(v);});
    for(bool selected:{false,true}){
        s.selected=selected?s.all():std::vector<ObjectRef>();
        auto originalSelection=s.selected;auto revision=s.serial;auto builds=s.rebuilds;auto history=s.history.cursor();auto dirty=s.dirty;
        s.fit();drawFrame();auto before=s.view();
        studio_i18n::sendMessage(canvas,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(300,200));
        run(selected?"mouse_pan_selected":"mouse_pan",24,[&](int i){studio_i18n::sendMessage(canvas,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(304+i*4,202+i*2));});
        dump(selected?"pan_selected":"pan_empty");
        auto after=s.view();
        studio_i18n::sendMessage(canvas,WM_MBUTTONUP,0,MAKELPARAM(396,248));
        bool preserved=s.serial==revision&&s.rebuilds==builds&&s.history.cursor()==history&&s.dirty==dirty&&s.selected==originalSelection;
        bool exact=core::length(after.center-(before.center-Vec2(96,48)*before.docPerDevice()))<1e-7;
        out<<"mouse_pan_check selected="<<selected<<" preserved="<<preserved<<" exact_delta="<<exact<<" released="<<(!pan&&GetCapture()!=canvas)<<"\n";
        drawFrame();
    }
    s.selected.clear();s.fit();drawFrame();
#ifdef MOLECULE_STUDIO_DX11
    if(dx11Renderer)dx11Renderer->setInteractive(true);
#endif
    run("zoom_in",24,[&](int){auto v=s.view();v.scale*=1.06;s.setView(v);});
    dump("zoom_in_stretched");
#ifdef MOLECULE_STUDIO_DX11
    if(dx11Renderer){dx11Renderer->setInteractive(false);double settle=drawFrame();out<<"zoom_in_settle_ms="<<settle<<" build_us="<<lastStats.buildMicros<<" draw_us="<<lastStats.drawMicros<<" triangles="<<lastStats.triangles<<"\n";}
    dump("zoom_in_settle");
    if(dx11Renderer)dx11Renderer->setInteractive(true);
#endif
    run("zoom_out",24,[&](int){auto v=s.view();v.scale/=1.06;s.setView(v);});
#ifdef MOLECULE_STUDIO_DX11
    if(dx11Renderer){dx11Renderer->setInteractive(false);double settle=drawFrame();out<<"zoom_out_settle_ms="<<settle<<" build_us="<<lastStats.buildMicros<<" draw_us="<<lastStats.drawMicros<<" triangles="<<lastStats.triangles<<"\n";}
    dump("zoom_out_settle");
#endif
    run("select_toggle",8,[&](int i){if(i%2)s.selected=s.all();else s.selected.clear();});
    for(double zoom: {2.0,4.0}){
        s.selected.clear();s.fit();{auto v=s.view();v.scale*=zoom;s.setView(v);}drawFrame();
        std::string prefix="x"+std::to_string(int(zoom));
        run((prefix+"_static").c_str(),4,[](int){});
        dump(prefix.c_str());
        run((prefix+"_pan").c_str(),12,[&](int){auto v=s.view();v.center.x+=6;s.setView(v);});
    }
    s.selected.clear();s.fit();drawFrame();
    ObjectId target=kInvalidObjectId;Vec2 dev;double best=1e18;
    for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()){
        Vec2 d=s.view().toDevice(atom.position);
        if(d.x<30||d.y<30||d.x>s.renderView.widthPx-30||d.y>s.renderView.heightPx-30)continue;
        double distance=std::hypot(d.x-s.renderView.widthPx*0.5,d.y-s.renderView.heightPx*0.5);
        if(distance<best){best=distance;target=atom.id;dev=d;}
    }
    if(target!=kInvalidObjectId){
        int x=int(dev.x),y=int(dev.y);
        studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        out<<"drag_atom_hit="<<(s.selected.empty()?0:1)<<"\n";
        for(int i=0;i<16;++i){
            studio_i18n::sendMessage(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+i*3,y+i*2));
            frameLine("drag_atom_frame",i,drawFrame());
        }
        double releaseStart=now();
        studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,MAKELPARAM(x+45,y+30));
        out<<"drag_atom_release_ms="<<(now()-releaseStart)<<"\n";
        drawFrame();
        dump("drag_atom");
        out<<"drag_atom_after_ms="<<lastFrameMs<<" build_us="<<lastStats.buildMicros<<" draw_us="<<lastStats.drawMicros<<"\n";
    }
    s.selected=s.all();s.fit();drawFrame();
    out<<"all_selected="<<s.selected.size()<<"\n";
    if(!s.selected.empty()){
        Vec2 anchor=s.view().toDevice(Vec2());
        // Pick an atom inside the frame, away from resize/rotate handles.
        for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()){
            auto point=s.view().toDevice(atom.position);
            if(point.x>20&&point.y>20&&point.x<s.renderView.widthPx-20&&point.y<s.renderView.heightPx-20&&selectionHandleAt(point)<0){anchor=point;break;}
        }
        int x=int(anchor.x),y=int(anchor.y);
        studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        Vec2 diagnosticDelta;std::vector<uint64_t> diagnosticKeys;
        out<<"drag_selection_object="<<s.objectDragActive()<<" transform="<<transformDragging<<" rigid="<<s.rigidDragPreview(&diagnosticDelta,&diagnosticKeys)<<"\n";
        run("drag_selection",10,[&](int i){studio_i18n::sendMessage(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+i*3,y+i*2));});
        studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,MAKELPARAM(x+27,y+18));
        drawFrame();
        out<<"drag_selection_after_ms="<<lastFrameMs<<" build_us="<<lastStats.buildMicros<<" draw_us="<<lastStats.drawMicros<<"\n";
    }
    s.selected.clear();
    out<<"done\n";
}
#include "clone_preview_checks.hpp"
#include "rigid_drag_checks.hpp"
#include "performance_ai_checks.hpp"
}
static App* createEditorTab(HINSTANCE,HWND,const std::string&,bool);
namespace {
#include "nmr_tabs_rotation_checks.hpp"
#include "nmr_parallel_checks.hpp"
void App::firstEditBench(const std::string& file,const std::string& outPath){
    if(std::getenv("MOLECULE_STUDIO_EXPAND_ARTIFACT_CHECK")){
      CreateDirectoryW(W(outPath).c_str(),0);
      auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
      auto adjacency=[&](){for(const auto& a:s.doc.atoms().records())if(a.isAlive()){
        std::vector<ObjectId> actual,expected;
        for(auto id:s.doc.incidentBonds(a.id)){auto b=s.doc.bonds().alive(id);if(b)actual.push_back(id);}
        for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&(b.beginAtom==a.id||b.endAtom==a.id))expected.push_back(b.id);
        std::sort(actual.begin(),actual.end());std::sort(expected.begin(),expected.end());check(actual==expected,"adjacency matches live bond endpoints");
      }};
      check(s.open(file).ok(),"open fixture");s.selected.clear();s.fit();screenshot(outPath+"/before.png");s.selected=s.all();auto original=s.doc.digest();expandGroups();check(expandPending,"expanded");adjacency();check(studio::validation::overValentAtoms(s.doc).empty(),"expansion has no false valence warnings");
      auto expanded=s.doc.digest();s.selected.clear();s.fit();screenshot(outPath+"/expanded-live.png");s.save(outPath+"/expanded-live.cdxml",true);s.refresh();screenshot(outPath+"/expanded-refresh.png");
      check(s.undo().ok()&&s.doc.digest()==original,"undo expansion");adjacency();check(s.undo(true).ok()&&s.doc.digest()==expanded,"redo expansion");adjacency();check(studio::validation::overValentAtoms(s.doc).empty(),"redo valence");
      s.selected=s.all();chemistryAction("clean");adjacency();check(studio::validation::overValentAtoms(s.doc).empty(),"cleanup valence");s.selected.clear();s.fit();screenshot(outPath+"/clean.png");
      std::ofstream(outPath+"/artifact-checks.json")<<"{\"passed\":true,\"adjacency_matches_bonds\":true,\"no_false_valence\":true,\"undo_redo\":true,\"cleanup\":true}";s.dirty=false;return;
    }
    if(std::getenv("MOLECULE_STUDIO_NMR_PARALLEL_CHECK")){checkNmrParallel(*this,file,outPath);return;}
    if(std::getenv("MOLECULE_STUDIO_NMR_ROTATION_CHECK")){checkNmrTabsAndRotation(*this,file,outPath);return;}
    if(std::getenv("MOLECULE_STUDIO_PERFORMANCE_AI_CHECK")){performanceAiCheck(file,outPath);return;}
    if(std::getenv("MOLECULE_STUDIO_RIGID_DRAG_CHECK")){rigidDragCheck(outPath);return;}
    if(std::getenv("MOLECULE_STUDIO_CLONE_CHECK")){clonePreviewCheck(outPath);return;}
    std::ofstream out(outPath);
    double t0=now();
    auto openError=s.open(file);
    out<<"open_ok="<<(openError.ok()?1:0)<<" open_ms="<<(now()-t0)<<"\n";
    if(!openError.ok()){out<<"open_error="<<openError.message<<"\n";return;}
    s.fit();updateInfo();invalidate();UpdateWindow(h);UpdateWindow(canvas);
    ObjectId target=kInvalidObjectId;Vec2 dev;double best=1e18;
    for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()){
        Vec2 d=s.view().toDevice(atom.position);
        if(d.x<30||d.y<30||d.x>width-30||d.y>height-30)continue;
        double distance=std::hypot(d.x-width*0.5,d.y-height*0.5);
        if(distance<best){best=distance;target=atom.id;dev=d;}
    }
    out<<"target="<<target<<"\n";
    if(target==kInvalidObjectId)return;
    if(std::getenv("MOLECULE_STUDIO_FIRST_TEXT")){
        int x=int(dev.x),y=int(dev.y);
        double a=now();
        studio_i18n::sendMessage(canvas,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(x,y));
        UpdateWindow(h);UpdateWindow(canvas);
        out<<"text_open_ms="<<(now()-a)<<" edit="<<(edit?1:0)<<"\n";
        if(edit){
            const wchar_t* keys=L"NO";
            for(int i=0;i<3;++i){
                a=now();
                if(i<2)studio_i18n::sendMessage(edit,WM_CHAR,WPARAM(keys[i]),1);
                else studio_i18n::sendMessage(edit,WM_CHAR,WPARAM(VK_BACK),1);
                UpdateWindow(h);UpdateWindow(canvas);
                out<<"text_char"<<i<<"_ms="<<(now()-a)<<" frame_ms="<<lastFrameMs<<" pre_ms="<<lastPreMs<<" build_us="<<lastStats.buildMicros<<" sc_ms="<<lastSceneMs<<" dr_ms="<<lastDrawMs<<"\n";
            }
            screenshot(outPath+".text.png",false,false);
            a=now();studio_i18n::sendMessage(canvas,WM_KEYDOWN,VK_ESCAPE,0);UpdateWindow(h);UpdateWindow(canvas);
            out<<"text_close_ms="<<(now()-a)<<"\n";
        }
        out<<"done\n";return;
    }
    if(std::getenv("MOLECULE_STUDIO_FIRST_DRAG")){
        int x=int(dev.x),y=int(dev.y);
        double a=now();studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));UpdateWindow(canvas);
        out<<"drag_press_ms="<<(now()-a)<<" canvas_ms="<<canvasMs<<"\n";
        for(int pass=1;pass<=2;++pass){
            if(pass==2){
                a=now();studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));UpdateWindow(canvas);
                out<<"drag2_press_ms="<<(now()-a)<<" canvas_ms="<<canvasMs<<"\n";
            }
            for(int i=1;i<=6;++i){
                a=now();double c0=canvasMs;
                studio_i18n::sendMessage(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+(pass-1)*40+i*3,y+(pass-1)*40+i*3));UpdateWindow(canvas);
                out<<"drag"<<pass<<"_move"<<i<<"_ms="<<(now()-a)<<" canvas_ms="<<(canvasMs-c0)<<" build_us="<<lastStats.buildMicros<<" frame_ms="<<lastFrameMs<<" pre_ms="<<lastPreMs<<" ov_ms="<<lastOverlayMs<<" dr_ms="<<lastDrawMs<<" sc_ms="<<lastSceneMs<<" gu_ms="<<lastGuidesMs<<"\n";
            }
            a=now();studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,MAKELPARAM(x+(pass-1)*40+18,y+(pass-1)*40+18));UpdateWindow(canvas);
            out<<"drag"<<pass<<"_release_ms="<<(now()-a)<<" canvas_ms="<<canvasMs<<"\n";
        }
        out<<"done\n";return;
    }
    s.selected={ObjectRef(ObjectKind::Atom,target)};
    double a=now();updateInfo();invalidate();UpdateWindow(h);UpdateWindow(canvas);out<<"select_ms="<<(now()-a)<<"\n";
    auto step=[&](const char* tag,int element){
        a=now();
        auto e=s.submit(commands::setElements({target},element),tag);
        double b=now();
        updateInfo();invalidate();double c0=canvasMs;UpdateWindow(h);UpdateWindow(canvas);double c=now();
        out<<tag<<" ok="<<(e.ok()?1:0)<<" submit_ms="<<(b-a)<<" paint_ms="<<(c-b)
           <<" canvas_ms="<<(canvasMs-c0)<<" build_us="<<lastStats.buildMicros<<" draw_us="<<lastStats.drawMicros
           <<" so_ms="<<lastSceneMs<<" ov_ms="<<lastOverlayMs<<" dr_ms="<<lastDrawMs<<" pr_ms="<<lastPresentMs;
#ifdef MOLECULE_STUDIO_DX11
        if(dx11Renderer)out<<" sync_us="<<dx11Renderer->syncMicros()<<" batch_us="<<dx11Renderer->batchMicros()<<" vb_us="<<dx11Renderer->vertexMicros()<<" mesh_rebuilds="<<lastStats.meshRebuilds;
#endif
        out<<"\n";
    };
    const int cur=s.doc.atoms().get(target)->element;
    const int other=cur==6?8:6;
    step("edit1_real",other);
    step("edit2_noop",other);
    step("edit3_real",cur);
    step("edit4_noop",cur);
    out<<"done\n";
}

#include "endpoint_double_click_checks.hpp"
void App::selftest(){
    // The suite writes fixtures, screenshots and the final tests.json into
    // testOut; the command line hands us a path that may not exist yet. Create
    // it so a clean "delete the output directory, then run --test" works.
    CreateDirectoryW(W(testOut).c_str(),0);
    DeleteFileW(W(testOut+"/failure.txt").c_str());DeleteFileW(W(testOut+"/tests.json").c_str());
    // ChemDraw-facing save invariants run first: they are cheap, self-contained, and
    // a regression here makes every CDXML the product writes unreadable.
    cdxmlColorCheck(testOut);
    endpointDoubleClickCheck(*this,testOut);
#include "valence_warning_tests.hpp"
#include "interaction_tests.hpp"
#include "sprout_hotkey_tests.hpp"
#include "chemistry_tests.hpp"
#include "carbonyl_tests.hpp"
#include "aldehyde_angle_tests.hpp"
#include "stereo_render_tests.hpp"
#include "shortcut_hint_tests.hpp"
#include "cursor_cleanup_tests.hpp"
#include "compatible_renderer_tests.hpp"
#include "paste_format_checks.hpp"
#include "name_link_checks.hpp"
    {
        Session drawn;drawn.activateCommand("chooseRingTool_Benzene");drawn.gesture(PointerPhase::Down,Vec2(),0);drawn.gesture(PointerPhase::Up,Vec2(),0);auto e=drawn.save(testOut+"/toolbar_benzene.cdxml",true);require(e.ok(),e.message.c_str());std::string bytes;persistence::readFileBytes(testOut+"/toolbar_benzene.cdxml",&bytes);tinyxml2::XMLDocument xml;require(xml.Parse(bytes.c_str())==tinyxml2::XML_SUCCESS,"parse actual toolbar benzene");int singles=0,doubles=0,aromatic=0;auto fragment=xml.RootElement()->FirstChildElement("page")->FirstChildElement("fragment");for(auto b=fragment->FirstChildElement("b");b;b=b->NextSiblingElement("b")){std::string order=b->Attribute("Order");if(order=="1")++singles;if(order=="2")++doubles;if(order=="1.5")++aromatic;}require(singles==3&&doubles==3&&aromatic==0,"tool-created benzene must export three explicit double bonds");
    }
    {
        s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();auto view=s.view();view.scale=1;view.center=Vec2();s.setView(view);s.activateCommand("chooseEdittingTool_Text");
        auto pixel=s.view().toDevice(Vec2(-30,20));LPARAM where=MAKELPARAM(int(pixel.x),int(pixel.y));auto before=s.doc.digest();studio_i18n::sendMessage(canvas,WM_LBUTTONDOWN,MK_LBUTTON,where);studio_i18n::sendMessage(canvas,WM_LBUTTONUP,0,where);require(textEditor.active&&IsWindowVisible(edit),"text tool enters inline editor");require(s.doc.stats().aliveTexts==0,"empty text draft creates no document object");
        RECT editorBounds,canvasBounds;GetWindowRect(edit,&editorBounds);GetWindowRect(canvas,&canvasBounds);require(std::abs(editorBounds.left-canvasBounds.left-int(pixel.x))<6,"inline editor stays at clicked x");require(editorBounds.top<canvasBounds.top+int(pixel.y),"text baseline at clicked y");
        studio_i18n::sendMessage(edit,EM_REPLACESEL,TRUE,(LPARAM)L"Benzene 123 中文");studio_i18n::sendMessage(edit,EM_REPLACESEL,TRUE,(LPARAM)L"\rSecond line");require(textEditor.value().find(L"中文")!=std::wstring::npos,"native Unicode entry");POINT scroll={};studio_i18n::sendMessage(edit,EM_GETSCROLLPOS,0,(LPARAM)&scroll);require(scroll.y==0,"inline multiline editor must keep the first line visible");studio_i18n::sendMessage(edit,WM_KEYDOWN,VK_RETURN,0);require(!textEditor.active&&s.doc.stats().aliveTexts==1,"Enter commits inline text");require(fullText(s.doc,0)=="Benzene 123 中文\nSecond line",(std::string("native Enter becomes a stored line break: ")+fullText(s.doc,0)).c_str());require(s.doc.texts().get(0)->size==10,"original Arial 10-point default");auto committed=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"one completed caption is one undo step");require(s.undo(true).ok()&&s.doc.digest()==committed,"redo complete caption");
        openText(0);textEditor.select(0,7);textEditor.toggle(kBold);textEditor.setFont("Times New Roman");finishText(true);auto runs=s.doc.runs(0);require(runs.size()>=2&&(runs.front().flags&kBold)&&runs.front().fontName=="Times New Roman","format only selected characters");require(fullText(s.doc,0)=="Benzene 123 中文\nSecond line","formatting preserves caption text");
        auto e=s.save(testOut+"/inline_text.cdxml",true);require(e.ok(),e.message.c_str());Session reloaded;e=reloaded.open(testOut+"/inline_text.cdxml");require(e.ok(),e.message.c_str());require(fullText(reloaded.doc,0)==fullText(s.doc,0),"inline multiline CDXML text roundtrip");
        textEditor.begin(kInvalidObjectId,kInvalidObjectId,Vec2(50,50));finishText(true);require(s.doc.stats().aliveTexts==1,"empty draft leaves no blank caption");
        s.selected=s.all();screenshot(testOut+"/text_at_100percent.png");
    }

    // P1 drawing UX: ChemDraw's Arial 10 default, automatic abbreviation case
    // at a bond terminus, object-level text formatting without the editor, and
    // the expanded first-run toolbar.
    {
        s.doc=Document();drawingSettings.apply(s.doc);s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
        require(s.doc.defaultFont()=="Arial"&&std::abs(s.doc.defaultTextSize()-10)<1e-6,"new document uses the ChemDraw Arial 10 default");
        textEditor.begin(kInvalidObjectId,kInvalidObjectId,Vec2(0,0));
        studio_i18n::sendMessage(edit,EM_REPLACESEL,TRUE,(LPARAM)L"Default font");require(textEditor.finish(true).ok(),"commit default-font caption");
        ObjectId caption=kInvalidObjectId;for(const auto& text:s.doc.texts().records())if(text.isAlive())caption=text.id;
        require(caption!=kInvalidObjectId&&!s.doc.runs(caption).empty()&&s.doc.runs(caption).front().fontName=="Arial","new caption is Arial");
        require(std::abs(textBaseSize(s.doc,caption)-10)<1e-6,"new caption is 10 pt");
        ObjectId left,right,bond;
        require(s.doc.addAtom(Vec2(0,40),6,&left).ok()&&s.doc.addAtom(Vec2(30,40),6,&right).ok()&&s.doc.addBond(left,right,&bond).ok(),"label fixture");
        s.refresh();openText(kInvalidObjectId,right);
        require(textEditor.family=="Arial"&&std::abs(textEditor.size-10)<1e-6,"bond-end label editor uses Arial 10");
        studio_i18n::sendMessage(edit,EM_REPLACESEL,TRUE,(LPARAM)L"cn");require(finishText(true),"commit lower-case cyano");
        require(s.doc.atomLabel(right)=="CN","bond-end cn is auto-cased to CN");
        require(static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,right)).findValue("studio.nickname.fragment")!=nullptr,"CN is recognised as a functional group");
        auto expansion=functional_groups::prepareExpansion(s.doc,right);
        require(expansion.anchor!=kInvalidObjectId&&expansion.graph&&expansion.graph->atoms().size()>=2,"cyano expands to a nitrile");
        require(chemcompletion::normalize("Co")=="Co"&&chemcompletion::normalize("Cn")=="Cn"&&chemcompletion::normalize("N")=="N","exactly typed element symbols keep their case");
        require(chemcompletion::normalize("bpin")=="Bpin"&&chemcompletion::normalize("tbu")=="tBu"&&chemcompletion::normalize("boc")=="Boc","new abbreviations are auto-cased");
        require(chemcompletion::normalize("iam")=="iam","unreviewed ambiguous key is not guessed");
        auto formatDigest=s.doc.digest();s.selected={ObjectRef(ObjectKind::Text,caption)};
        execute(Button("","加粗","","@flag",kBold));
        require(!s.doc.runs(caption).empty()&&(s.doc.runs(caption).front().flags&kBold)!=0,"bold applies to the selected text object without opening the editor");
        execute(Button("","增大字号","","@size",1));
        require(std::abs(textBaseSize(s.doc,caption)-11)<1e-6,"size button grows the selected text from its inherited size");
        require(s.undo().ok()&&s.undo().ok()&&s.doc.digest()==formatDigest,"object text formatting is undoable");
        std::wstring ribbon=W(testOut)+L"/ribbon-default.ini";DeleteFileW(ribbon.c_str());loadRibbon(ribbon);
        require(!ribbonCompact&&ribbonPinned&&ribbonVisible,"first run opens the expanded toolbar");
        WritePrivateProfileStringW(L"Ribbon",L"Compact",L"1",ribbon.c_str());loadRibbon(ribbon);
        require(ribbonCompact,"a saved compact preference is still honored");
        DeleteFileW(ribbon.c_str());loadRibbon();ribbonCompact=false;ribbonVisible=true;layout();
    }

    {
        std::vector<std::string> files;for(int variant=0;variant<32;++variant){auto path=testOut+"/project-corpus-"+std::to_string(variant)+".cdxml";std::ofstream(path)<<projectCorpusXml(variant);files.push_back(path);}std::ofstream report(testOut+"/corpus.tsv");report<<"file\tatoms\tbonds\tstatus\n";
        size_t i=0;for(auto f:files){Session a;auto e=a.open(f);require(e.ok(),(f+": "+e.message).c_str());std::string dst=testOut+"/corpus_"+std::to_string(i++)+".cdxml";e=a.save(dst,true);require(e.ok(),(f+": "+e.message).c_str());Session b;e=b.open(dst);require(e.ok(),(f+": "+e.message).c_str());require(a.doc.stats().aliveAtoms==b.doc.stats().aliveAtoms&&a.doc.stats().aliveBonds==b.doc.stats().aliveBonds,"corpus atom/bond counts changed");
            std::vector<std::string> left,right;auto graph=[](const Document& d){std::vector<std::string> keys;for(auto& atom:d.atoms().records())if(atom.isAlive()){std::vector<int> neighbors;for(auto& bond:d.bonds().records())if(bond.isAlive()&&(bond.beginAtom==atom.id||bond.endAtom==atom.id))neighbors.push_back(int(bond.order)*1000+d.atoms().get(bond.beginAtom==atom.id?bond.endAtom:bond.beginAtom)->element);std::sort(neighbors.begin(),neighbors.end());std::ostringstream key;key<<atom.element<<','<<atom.semantics.charge<<','<<atom.semantics.isotope<<','<<d.atomLabel(atom.id);for(auto n:neighbors)key<<','<<n;keys.push_back(key.str());}std::sort(keys.begin(),keys.end());return keys;};require(graph(a.doc)==graph(b.doc),(f+": chemistry changed").c_str());report<<f<<'\t'<<a.doc.stats().aliveAtoms<<'\t'<<a.doc.stats().aliveBonds<<"\tPASS\n";
        }
        require(files.size()>=30,"compatibility corpus missing");
    }

    {
        // ChemDraw 21 wraps query-atom labels in <objecttag Name="query"> and writes
        // page metadata as id-less <annotation> elements at page level. Both used to
        // abort the load with "unsupported node child" / "object has no CDXML id".
        const std::string fixture =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
            "<CDXML CreationProgram=\"MoleculeStudio 0.9.3.2\" BoundingBox=\"0 0 500 500\" LabelFont=\"3\" LabelSize=\"10\" BondLength=\"14.4\" color=\"0\" bgcolor=\"1\">\n"
            "<fonttable><font id=\"3\" charset=\"iso-8859-1\" name=\"Arial\"/></fonttable>\n"
            "<colortable><color r=\"1\" g=\"1\" b=\"1\"/><color r=\"0\" g=\"0\" b=\"0\"/></colortable>\n"
            "<page id=\"100\" BoundingBox=\"0 0 500 500\" WidthPages=\"1\" HeightPages=\"1\">\n"
            "<fragment id=\"101\">\n"
            "<n id=\"102\" p=\"100 100\" FreeSites=\"1\" AS=\"N\"><objecttag TagType=\"Unknown\" Name=\"query\"><t p=\"98 104\" CaptionLineHeight=\"variable\"><s font=\"3\" size=\"8.1\" color=\"0\">*</s></t></objecttag></n>\n"
            "</fragment>\n"
            "<annotation Keyword=\"GUID\" Content=\"00000000-0000-0000-0000-000000000000\"/>\n"
            "<annotation Keyword=\"Name\" Content=\"Test Page\"/>\n"
            "</page>\n</CDXML>\n";
        const std::string fixturePath=testOut+"/objecttag_annotation.cdxml";
        {std::ofstream f(fixturePath.c_str(),std::ios::binary);f.write(fixture.data(),fixture.size());}
        Session query;auto qe=query.open(fixturePath);require(qe.ok(),("objecttag query import: "+qe.message).c_str());
        require(query.doc.stats().aliveAtoms==1,"objecttag query atom imported");
        auto queryTags=static_cast<const Document&>(query.doc).extensions(ObjectRef(ObjectKind::Atom,0)).findValue("studio.atom.tags");
        require(queryTags&&queryTags->find(">*<")!=std::string::npos&&query.doc.atomLabel(0).empty(),"objecttag query stays a separate visible annotation");
        bool queryVisible=false;for(const auto& primitive:query.scene.primitives())if(primitive.source==ObjectRef(ObjectKind::Atom,0)&&(!primitive.points.empty()||!primitive.subpaths.empty()))queryVisible=true;
        require(queryVisible,"objecttag query produces visible geometry");
        const std::string roundTrip=testOut+"/objecttag_annotation_roundtrip.cdxml";
        auto qs=query.save(roundTrip,true);require(qs.ok(),qs.message.c_str());
        Session queryBack;auto qb=queryBack.open(roundTrip);require(qb.ok(),("objecttag roundtrip: "+qb.message).c_str());
        auto roundTripTags=static_cast<const Document&>(queryBack.doc).extensions(ObjectRef(ObjectKind::Atom,0)).findValue("studio.atom.tags");
        require(queryBack.doc.stats().aliveAtoms==1&&roundTripTags&&roundTripTags->find(">*<")!=std::string::npos&&queryBack.doc.atomLabel(0).empty(),"objecttag query survives save/reload");
    }

    {
        Session imported;auto e=persistence::readCdxml(projectBenzeneXml(),imported.doc);require(e.ok(),e.message.c_str());require(imported.doc.stats().aliveAtoms==6,"project CDXML import");require(imported.doc.defaultFont()=="Arial","resolve numeric font ID");
        ObjectId tid;imported.doc.addText(Vec2(80,80),12,"Font 中文",&tid);auto runs=imported.doc.runs(tid);for(auto& run:runs)run.fontName="Times New Roman";imported.doc.setTextRuns(tid,runs);imported.refresh();
        std::string original=projectBenzeneXml();auto encoding=original.find("UTF-8");if(encoding!=std::string::npos)original.replace(encoding,5,"UTF-16");auto wide=W(original);std::ofstream utf16(testOut+"/utf16.cdxml",std::ios::binary);utf16.put(char(255));utf16.put(char(254));utf16.write((const char*)wide.data(),wide.size()*sizeof(wchar_t));utf16.close();Session unicode;auto ue=unicode.open(testOut+"/utf16.cdxml");require(ue.ok()&&unicode.doc.stats().aliveAtoms==6,"UTF16 CDXML import");

        auto p=studioio::Page::get(imported.doc);require(p.valid(),"import page size");
        e=imported.save(testOut+"/cdxml_import_export.cdxml",true);require(e.ok(),e.message.c_str());Session fonts;e=fonts.open(testOut+"/cdxml_import_export.cdxml");require(e.ok(),e.message.c_str());bool times=false;for(const auto& text:fonts.doc.texts().records())if(text.isAlive())for(const auto& run:fonts.doc.runs(text.id))if(run.fontName=="Times New Roman")times=true;require(times,"new font table entry on imported document");
    }
    {
        Session clip;clip.activateCommand("chooseRingTool_Benzene");clip.gesture(PointerPhase::Down,Vec2(),0);clip.gesture(PointerPhase::Up,Vec2(),0);
        ObjectId text;clip.doc.addText(Vec2(-20,55),12,"Benzene 中文",&text);
        std::vector<TextRun> rich(2);rich[0].utf8="Benzene";rich[0].flags=kBold;rich[1].utf8=" 中文";rich[1].flags=kItalic;rich[1].size=14;clip.doc.setTextRuns(text,rich);
        PathRecord path;path.points={Vec2(80,20),Vec2(100,50)};ObjectId pid;clip.doc.addPath(path,&pid);clip.refresh();clip.selected=clip.all();
        Session oleSource;std::vector<ObjectId> ringIds;for(int i=0;i<6;i++){ObjectId id;double angle=6.2831853*i/6;oleSource.doc.addAtom(Vec2(45*cos(angle),45*sin(angle)),6,&id);ringIds.push_back(id);}for(int i=0;i<6;i++){ObjectId bond;oleSource.doc.addBond(ringIds[i],ringIds[(i+1)%6],&bond);if(i%2==0)oleSource.doc.setBondOrder(bond,BondOrder::Double);}oleSource.refresh();oleSource.selected=oleSource.all();
        require(studioio::copy(oleSource,canvas,false).ok(),"OLE clipboard copy");
        {IDataObject* oleClip=nullptr;require(SUCCEEDED(OleGetClipboard(&oleClip))&&oleClip,"OLE clipboard publisher");
         FORMATETC oleEtc={};oleEtc.cfFormat=CLIPFORMAT(ole::fmtEmbedSource());oleEtc.dwAspect=DVASPECT_CONTENT;oleEtc.lindex=-1;oleEtc.tymed=TYMED_ISTORAGE;
         STGMEDIUM oleMedium={};HRESULT oleHr=oleClip->GetData(&oleEtc,&oleMedium);
         {std::ofstream diagnostic(testOut+"/ole_getdata.txt");diagnostic<<"hr=0x"<<std::hex<<(unsigned long)oleHr<<std::dec<<" tymed=0x"<<std::hex<<(unsigned long)oleMedium.tymed<<std::dec<<" cf="<<unsigned(oleEtc.cfFormat)<<" contents="<<(oleMedium.pstg?ole::readContents(oleMedium.pstg).size():0)<<"\n";}
         require(SUCCEEDED(oleHr)&&oleMedium.tymed==TYMED_ISTORAGE,"OLE Embed Source storage");
         CLSID oleClass={};require(SUCCEEDED(ReadClassStg(oleMedium.pstg,&oleClass))&&IsEqualCLSID(oleClass,ole::kDocClsid),"OLE storage class");
         std::string oleContents=ole::readContents(oleMedium.pstg);require(!oleContents.empty(),"OLE CONTENTS stream");
         IStorage* oleTestStg=ole::createMemoryStorage();IOleObject* oleLoaded=nullptr;
         HRESULT oleCreateHr=OleCreateFromData(oleClip,IID_IOleObject,OLERENDER_DRAW,nullptr,nullptr,oleTestStg,(void**)&oleLoaded);
         require(SUCCEEDED(oleCreateHr)&&oleLoaded,"OLE CreateFromData");
         if(oleLoaded)oleLoaded->Release();if(oleTestStg)oleTestStg->Release();
         Document previewDoc;std::istringstream previewIn(oleContents);Error previewError=integration::loadSafeNative(previewDoc,previewIn);
         size_t previewTriangles=0;std::string previewDiagnostics;HENHMETAFILE preview=previewError.ok()?olepreview::renderDocument(previewDoc,SIZE{4000,3000},&previewTriangles,&previewDiagnostics):nullptr;
         {std::ofstream diagnosticFile(testOut+"/ole_scene.txt");diagnosticFile<<previewDiagnostics;}
         ENHMETAHEADER header={};UINT headerSize=preview?GetEnhMetaFileHeader(preview,sizeof(header),&header):0;UINT previewBits=preview?GetEnhMetaFileBits(preview,0,nullptr):0;
         {Session debugSession;debugSession.doc=previewDoc;debugSession.refresh();Box debugBounds=debugSession.scene.bounds();std::ofstream emfLog(testOut+"/ole_emf.txt");emfLog<<"load="<<previewError.ok()<<" message="<<previewError.message<<" handle="<<(void*)preview<<" header="<<headerSize<<" bits="<<previewBits<<" triangles="<<previewTriangles<<" scene="<<debugSession.scene.size()<<" bounds="<<debugBounds.min.x<<","<<debugBounds.min.y<<","<<debugBounds.max.x<<","<<debugBounds.max.y<<" status="<<debugSession.status<<"\n";}
         require(preview&&headerSize>0,"OLE EMF preview");
         if(preview){UINT previewBytes=GetEnhMetaFileBits(preview,0,nullptr);std::vector<BYTE> emfData(previewBytes);if(previewBytes)GetEnhMetaFileBits(preview,previewBytes,emfData.data());std::ofstream emfFile(testOut+"/ole_preview.emf",std::ios::binary);emfFile.write(reinterpret_cast<const char*>(emfData.data()),emfData.size());}
         if(preview)DeleteEnhMetaFile(preview);
         ReleaseStgMedium(&oleMedium);oleClip->Release();}
        require(studioio::copy(clip,canvas,false).ok(),"copy to real system clipboard");
        std::ofstream beforeFile(testOut+"/clipboard_before.r62doc");r62::integration::saveSafeNative(clip.doc,beforeFile);beforeFile.close();auto digest=clip.doc.digest();auto n=clip.doc.stats().aliveAtoms;
        auto e=studioio::paste(clip,canvas);require(e.ok(),e.message.c_str());require(clip.doc.stats().aliveAtoms==n*2&&clip.dirty,"paste draws and marks dirty");require(clip.doc.paths().size()==2,"copy graphics");require(clip.doc.runs(1).size()==2,"paste preserves rich text runs");
        auto pasted=clip.doc.digest();auto undoError=clip.undo();std::ofstream afterFile(testOut+"/clipboard_after_undo.r62doc");r62::integration::saveSafeNative(clip.doc,afterFile);afterFile.close();require(undoError.ok(),undoError.message.c_str());require(clip.doc.digest()==digest,"paste is one undo step");require(clip.undo(true).ok()&&clip.doc.digest()==pasted,"redo paste");
        auto first=clip.doc.atoms().records().back().position;e=studioio::paste(clip,canvas);require(e.ok(),e.message.c_str());auto second=clip.doc.atoms().records().back().position;require(second.x>first.x&&second.y>first.y,"repeated paste cascades down right");
        auto beforeCut=clip.doc.digest();e=studioio::copy(clip,canvas,true);require(e.ok(),e.message.c_str());require(clip.undo().ok()&&clip.doc.digest()==beforeCut,"cut is one undo step");
    }
    {
        Session sheet;studioio::Page p;p.width=842;p.height=595;p.across=2;p.down=3;p.store(sheet.doc);
        auto e=sheet.save(testOut+"/pages_2x3.cdxml",true);require(e.ok(),e.message.c_str());
        std::string savedBytes;require(persistence::readFileBytes(testOut+"/pages_2x3.cdxml",&savedBytes).ok(),"read saved page CDXML");
        tinyxml2::XMLDocument saved;require(saved.Parse(savedBytes.c_str())==tinyxml2::XML_SUCCESS,"parse saved page CDXML");
        auto savedPage=saved.RootElement()?saved.RootElement()->FirstChildElement("page"):nullptr;require(savedPage,"saved CDXML has a page");
        require(std::string(savedPage->Attribute("DrawingSpace")?savedPage->Attribute("DrawingSpace"):"")=="poster","canvas is written as a poster drawing space");
        require(std::abs(savedPage->DoubleAttribute("Width",0)-1684)<.1&&std::abs(savedPage->DoubleAttribute("Height",0)-1785)<.1,"canvas width and height are written");
        Session reopen;e=reopen.open(testOut+"/pages_2x3.cdxml");require(e.ok(),e.message.c_str());auto q=studioio::Page::get(reopen.doc);require(q.across==2&&q.down==3&&std::abs(q.width-842)<.1&&std::abs(q.height-595)<.1,"page settings roundtrip");
    }

    {Session ring;ring.activateCommand("chooseRingTool_Benzene");
     ring.gesture(PointerPhase::Down,Vec2(),0);ring.gesture(PointerPhase::Up,Vec2(),0);
     auto f=analyze(ring.doc,ring.all());require(f.valid&&f.plain=="C6H6"&&std::abs(f.averageMass-78.114)<.03,"drawn benzene formula including aromatic double semantics");}
    {Document d;ObjectId o;d.addAtom(Vec2(),8,&o);auto f=analyze(d,{ObjectRef(ObjectKind::Atom,o)});require(f.valid&&f.plain=="H2O"&&std::abs(f.averageMass-18.015)<.01,"water with implicit H");
     AtomSemantics sem;sem.isotope=18;d.setAtomSemantics(o,sem);f=analyze(d,{ObjectRef(ObjectKind::Atom,o)});require(f.valid&&std::abs(f.averageMass-20.015)<.01,"isotope mass");
     sem.charge=-1;d.setAtomSemantics(o,sem);require(!analyze(d,{ObjectRef(ObjectKind::Atom,o)}).valid,"unresolved ionic H");
     sem.implicitHydrogens=1;d.setAtomSemantics(o,sem);require(analyze(d,{ObjectRef(ObjectKind::Atom,o)}).valid,"explicit ionic H");
     d.setAtomLabel(o,"R");require(!analyze(d,{ObjectRef(ObjectKind::Atom,o)}).valid,"query label cannot invent mass");}
    {Document d;ObjectId c1,c2,o,b;d.addAtom(Vec2(),6,&c1);d.addAtom(Vec2(30,0),6,&c2);d.addAtom(Vec2(60,0),8,&o);d.addBond(c1,c2,&b);d.addBond(c2,o,&b);auto f=analyze(d,{ObjectRef(ObjectKind::Atom,c1)});require(f.valid&&f.plain=="C2H6O"&&std::abs(f.averageMass-46.069)<.02,"whole ethanol from atom selection");
     ObjectId water;d.addAtom(Vec2(100,0),8,&water);f=analyze(d,{ObjectRef(ObjectKind::Atom,c1),ObjectRef(ObjectKind::Atom,water)});require(f.components==2&&f.plain=="C2H8O2","multi-structure total");}
    // Test actual button dispatch, model changes, native file roundtrip and
    // information card invalidation; screenshots contain the real GL output.
    demo();require(s.save(testOut+"/studio_export.cdxml",true).ok(),"export to ChemDraw");{Document check;auto e=persistence::readCdxmlFile(testOut+"/studio_export.cdxml",check);require(e.ok(),e.message.c_str());require(check.atomLabel(0).empty(),"caption must not become atom label");}updateInfo();require(info.plain=="C6H6","benzene formula");require(std::abs(info.averageMass-78.114)<.03,"benzene mass");screenshot(testOut+"/studio.png");
    size_t cache=formulaBuilds;updateInfo();require(cache==formulaBuilds,"selection info recomputed without change");
    auto before=s.doc.digest();s.selected={ObjectRef(ObjectKind::Atom,0)};error(s.submit(commands::setElements({0},8),"test element"));updateInfo();require(!info.valid,"overvalence should not be invented");execute(Button("","","","@undo"));require(s.doc.digest()==before,"undo digest");updateInfo();require(info.plain=="C6H6","undo formula refresh");
    s.doc=Document();s.selected.clear();updateInfo();require(!info.valid&&info.atoms==0,"empty document has no formula");
    s.activateCommand("chooseBondTool_Solid");auto count=s.doc.stats().aliveAtoms;s.gesture(PointerPhase::Down,Vec2(-150,-100),0);s.gesture(PointerPhase::Move,Vec2(-120,-100),0);s.gesture(PointerPhase::Up,Vec2(-120,-100),0);require(s.doc.stats().aliveAtoms==count+2,"draw bond");execute(Button("","","","@undo"));require(s.doc.stats().aliveAtoms==count,"undo new bond");
    std::string f=testOut+"/roundtrip.r62doc";require(s.save(f).ok(),"save");Session restored;require(restored.open(f).ok()&&restored.doc.digest()==s.doc.digest(),"reload digest");
    // Exercise visible Home buttons; collapsed items are reached through group menus.
    ribbonTab=0;ribbonVisible=true;ribbonCompact=false;layout();
    for(auto& gr:groups)for(auto& b:gr.items)if(b.cmd[0]!='@'){bool found=false;for(auto& e:s.entries)if(e.command==b.cmd&&e.limitation.empty())found=true;require(found,b.cmd.c_str());
        if(b.r.X<0||b.r.Y<0)continue;studio_i18n::sendMessage(h,WM_LBUTTONUP,0,MAKELPARAM(int((b.r.X+b.r.Width/2)*scale),int((b.r.Y+b.r.Height/2)*scale)));require(s.entries[s.activeIndex].command==b.cmd,("toolbar click dispatch: "+b.cmd).c_str());}
    ObjectId tid;s.doc.addText(Vec2(0,110),12,"H2O 中文",&tid);s.refresh();s.selected={ObjectRef(ObjectKind::Text,tid)};
    execute(Button("","化学式","","@flag",kFormula));require(fullText(s.doc,tid)=="H2O 中文","formula formatting preserves UTF8");bool sub=false;for(auto& r:s.doc.runs(tid))if(r.flags&kFormula)sub=true;require(sub,"formula digit subscript");
    require(buttonHint(Button("","撤销","","@undo"))=="撤销  ·  Ctrl+Z","shortcut status label");
    demo();s.selected=s.all();s.activateCommand("chooseSelectionTool_Marquee");updateInfo();for(int w:{1100,1500}){SetWindowPos(h,0,0,0,w,900,SWP_NOMOVE|SWP_NOZORDER);layout();require(height-top-int(footerHeight()*scale)>height*.50,"toolbar leaves over half the window for drawing");for(const auto& b:chemistryQuick){require(b.r.GetRight()*scale<=width&&b.r.GetBottom()*scale<top,"chemistry quick button is inside the toolbar");for(const auto& group:groups)require(!b.r.IntersectsWith(group.r),"chemistry quick button does not overlap a tool group");}for(auto& gr:groups)for(auto& b:gr.items)require(b.r.GetRight()*scale<=width+1&&b.r.GetBottom()*scale<top,"toolbar clipped");screenshot(testOut+"/studio_"+std::to_string(w)+".png");}
    scale=2;SetWindowPos(h,0,0,0,3300,1800,SWP_NOMOVE|SWP_NOZORDER);layout();s.fit();for(auto& gr:groups)for(auto& b:gr.items)require(b.r.GetRight()*scale<=width+1&&b.r.GetBottom()*scale<top,"200 percent toolbar clipped");require(height-top-int(footerHeight()*scale)>height*.50,"high DPI canvas too small");screenshot(testOut+"/studio_200percent.png");
    auto& undoButton=groups[0].items[3];studio_i18n::sendMessage(h,WM_MOUSEMOVE,0,MAKELPARAM(int((undoButton.r.X+3)*scale),int((undoButton.r.Y+3)*scale)));require(hover==undoButton.id,"hover hit testing");screenshot(testOut+"/hover_shortcut.png");studio_i18n::sendMessage(h,WM_MOUSELEAVE,0,0);require(hover.empty(),"hover clears on leave");
    // Task 7/8 acceptance: unconfigured-API banner with its direct settings route, and AI turns
    // centred on the visible viewport without dragging the user's existing objects along.
    aiUxCheck(testOut);
#include "copy_png_checks.hpp"
    std::ofstream(testOut+"/tests.json")<<"{\"formula\":true,\"selection_clear\":true,\"undo_refresh\":true,\"cache\":true,\"draw_undo\":true,\"save_reload\":true,\"tool_routes\":true,\"responsive_layout\":true,\"old_ui_linked\":false,\"cdxml_interop\":true,\"clipboard_undo_redo\":true,\"ole_embed\":true,\"ole_emf_preview\":true,\"paper_layout\":true,\"toolbar_benzene_three_doubles\":true,\"inline_native_text\":true,\"inline_text_undo\":true,\"shortcut_defaults_and_customization\":true,\"interior_and_partial_drag\":true,\"ctrl_drag_copy_undo\":true,\"free_arrow_resize\":true,\"all_arrow_styles_roundtrip\":true,\"editor_hotkeys\":97,\"windows_command_shortcuts\":75,\"structural_nicknames\":521,\"query_nicknames\":43,\"native_systematic_naming\":true,\"native_2d_cleanup\":true,\"rdkit_3d_cleanup\":true,\"chemdraw_text_defaults\":true,\"abbreviation_auto_case\":true,\"functional_group_catalog\":true,\"text_format_without_editor\":true,\"ribbon_default_expanded\":true,\"paste_format_decision\":true,\"paste_blank_canvas_adopts_source\":true,\"paste_prompt_default_source\":true,\"paste_internal_no_prompt\":true,\"name_link_auto_group\":true,\"name_link_stale_frame\":true,\"name_link_hover_tip\":true,\"name_link_persists\":true,\"ai_config_banner\":true,\"ai_centred_insert\":true,\"copy_as_png\":true,\"copy_as_png_selection_only\":true,\"copy_as_png_clipboard_formats\":true,\"copy_as_png_document_unchanged\":true,\"copy_as_png_text_arrow\":true}";s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
}
#include "drawing_settings_checks.hpp"
#include "bond_hydrogen_checks.hpp"
#include "custom_group_checks.hpp"
#include "fuzzy_search_checks.hpp"
#include "expand_group_checks.hpp"
#include "sprout_layout_tests.hpp"
#include "triple_bond_checks.hpp"
#include "pattern_drag_checks.hpp"
#include "insertion_performance_checks.hpp"
#include "drag_preview_checks.hpp"
#include "fixed_drag_checks.hpp"
#include "nmr_visual_checks.hpp"
#include "alias_label_checks.hpp"
#include "groups_copy_checks.hpp"
#include "nickname_direction_checks.hpp"
#include "completion_checks.hpp"
// One wiring point for the inline text editor: the tabbed factory and the
// single-window path must stay identical. Content/selection/composition
// events reach the preview through the TextEditHost interface, which is
// injected at initialize() and cannot be forgotten; only the two callbacks
// that are genuinely per-window (status bar, app shortcuts) live here.
static void wireTextEditor(App& a){
    a.textEditor.completed=[&a](const Error& e,bool accept){a.status=e.ok()?(accept?"文字已保存 · Ctrl+Z 可撤销":"已取消本次文字修改"):"未保存："+e.message;a.invalidate();};
    a.textEditor.shortcut=[&a](UINT,WPARAM key){if(a.shortcut(UINT(key),mods(),true))return true;bool managed=((mods()&kModControl)&&key>='A'&&key<='Z')||(key>=VK_F5&&key<=VK_F10);return managed;};
}
void App::textEditChanged(){syncTextControls();if(reviewEditor&&reviewChanged)reviewChanged();}
void App::textEditSelectionChanged(){}
void App::textEditCompositionChanged(bool){}
void App::textEditFinishing(){textBackdrop.clear();}
namespace {
// The review embeds the real editor window, canvasProc, RichEdit host, renderer,
// input router and command history. Only the document chrome is compacted.
static void initializeReviewEditor(App& a,HWND parent){
    a.reviewEditor=true;a.tabChild=true;a.quitOnDestroy=false;
    HINSTANCE inst=GetModuleHandleW(nullptr);
    HWND h=studio_i18n::createWindowEx(0,L"MoleculeStudioShell",L"结构编辑小画布",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
        0,0,100,100,parent,nullptr,inst,&a);
    if(!h)throw std::runtime_error("无法创建审核编辑器");
    a.scale=GetDpiForWindow(h)/96.f;
    studio_i18n::createWindowEx(0,L"MoleculeStudioCanvas",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,100,100,h,nullptr,inst,&a);
    a.hbar=studio_i18n::createWindowEx(0,L"SCROLLBAR",L"",WS_CHILD|WS_VISIBLE|SBS_HORZ,0,0,10,10,h,nullptr,inst,nullptr);
    a.vbar=studio_i18n::createWindowEx(0,L"SCROLLBAR",L"",WS_CHILD|WS_VISIBLE|SBS_VERT,0,0,10,10,h,nullptr,inst,nullptr);
    ImmAssociateContext(a.canvas,nullptr);
    a.textEditor.initialize(h,a.canvas,&a.s,&a);a.edit=a.textEditor.hwnd;wireTextEditor(a);
    a.initializeTextControls();
    const wchar_t* labels[]={L"B",L"I",L"x₂",L"x²"};
    for(int i=0;i<4;++i){auto b=studio_i18n::createWindowEx(0,L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
        0,0,28,25,h,HMENU(UINT_PTR(410+i)),inst,nullptr);studio_i18n::sendMessage(b,WM_SETFONT,WPARAM(a.textUiFont),TRUE);}
    a.createGraphics();a.layout();
}

}
static App* createEditorTab(HINSTANCE inst,HWND parent,const std::string& path,bool demo){
    App* ap=new App();
    App& a=*ap;
    a.tabChild=true;
    a.quitOnDestroy=false;
    HWND h=studio_i18n::createWindowEx(0,L"MoleculeStudioShell",L"Molecule Studio 0.95",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1400,900,parent,0,inst,ap);
    if(!h){delete ap;return nullptr;}
    a.scale=GetDpiForWindow(h)/96.f;
    DragAcceptFiles(h,TRUE);
    {UINT dpi=GetDpiForWindow(h);olepreview::previewDpi()=dpi?dpi:96;}
    studio_i18n::createWindowEx(0,L"MoleculeStudioCanvas",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,100,100,h,0,inst,ap);
    a.hbar=studio_i18n::createWindowEx(0,L"SCROLLBAR",L"",WS_CHILD|WS_VISIBLE|SBS_HORZ,0,0,10,10,h,0,inst,0);
    a.vbar=studio_i18n::createWindowEx(0,L"SCROLLBAR",L"",WS_CHILD|WS_VISIBLE|SBS_VERT,0,0,10,10,h,0,inst,0);
    ImmAssociateContext(a.canvas,nullptr); // Drawing shortcuts stay outside IME; begin() restores the context for text.
    a.textEditor.initialize(h,a.canvas,&a.s,&a);a.edit=a.textEditor.hwnd;
    wireTextEditor(a);
    a.initializeTextControls();
    a.createGraphics();
    a.layout();
    { wchar_t demoFlag[8]={}; if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_OCSR_DEMO",demoFlag,8)) a.showOcsrDemo(); }
    { wchar_t imageFlag[32768]={}; if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_IMAGE_DEMO",imageFlag,32768)&&imageFlag[0]) a.showImageToStructure(); }
    if(!path.empty()){
        Error e=a.s.open(path);
        a.error(e);
        if(e.ok())a.fileName=path.substr(path.find_last_of("/\\")+1);
        else studio_i18n::messageBox(h,W("打开失败："+e.message).c_str(),L"打开失败",MB_OK|MB_ICONINFORMATION);
    }else if(demo)a.demo();
    a.textEditor.nextFamily=a.s.doc.defaultFont();
    a.textEditor.nextSize=a.s.doc.defaultTextSize();
    a.updateInfo();
    for(auto* list:{&a.groups,&a.graphicsGroups,&a.textGroups})for(auto& gr:*list)for(auto& b:gr.items)if(!b.cmd.empty()&&b.cmd[0]!='@'){bool found=false;for(auto& entry:a.s.entries)if(entry.command==b.cmd&&entry.limitation.empty())found=true;b.enabled=found;}
    g_currentApp=ap;
    // Acceptance hook: feed an existing recognition record straight into the very
    // function the 复制结构 button calls, and log the outcome.  It must run after
    // g_currentApp exists because the OLE/EMF publisher goes through the app.
    { wchar_t fixture[32768]={}; if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_OCSR_COPY_FIXTURE",fixture,32768)&&fixture[0]){std::wstring copyError;bool copied=a.copyOcsrStructure(U(std::wstring(fixture)),copyError);std::wstring line=copied?L"ok":(L"fail "+copyError);FILE* logFile=_wfopen(L"t6-copy-check.txt",L"wb");if(logFile){fwprintf(logFile,L"%ls\n",line.c_str());fclose(logFile);}} }
    PostMessageW(h,WM_APP+40,0,0);
    return ap;
}
static int runTabbedStudio(HINSTANCE inst,const std::string& initialFile){
    TabHost host;
    host.inst=inst;
    host.factory=[inst](HWND parent,const std::string& path,bool demo){return createEditorTab(inst,parent,path,demo);};
    host.create();
    if(!host.hwnd)return 2;
    studioio::olePublisher()=[](const Document& subset,const std::string& native,const std::string& xml){return g_currentApp?g_currentApp->publishOleClipboard(subset,native,xml):false;};
    host.newTab(initialFile);
    ShowWindow(host.hwnd,SW_MAXIMIZE);
    host.layout();
    if(host.active>=0&&host.active<int(host.tabs.size()))SetFocus(host.tabs[host.active]->canvas);
    MSG msg={};
    while(GetMessageW(&msg,0,0,0)>0){
        bool handled=false;
        for(size_t i=0;i<host.tabs.size();++i){
            App* app=host.tabs[i];
            if(app->nmrChild&&(msg.hwnd==app->nmrChild||IsChild(app->nmrChild,msg.hwnd))){
                if(msg.message==WM_KEYDOWN&&app->tabHotkey(msg.message,msg.wParam))handled=true;
                else handled=IsDialogMessageW(app->nmrChild,&msg)||handled;
            }
            if(IsWindow(app->tableEditor.hwnd)&&(msg.hwnd==app->tableEditor.hwnd||IsChild(app->tableEditor.hwnd,msg.hwnd)))handled=IsDialogMessageW(app->tableEditor.hwnd,&msg)||handled;
            if(IsWindow(app->settingsWindow.hwnd)&&(msg.hwnd==app->settingsWindow.hwnd||IsChild(app->settingsWindow.hwnd,msg.hwnd)))handled=IsDialogMessageW(app->settingsWindow.hwnd,&msg)||handled;
            if(IsWindow(app->ai.hwnd)&&(msg.hwnd==app->ai.hwnd||IsChild(app->ai.hwnd,msg.hwnd)))handled=IsDialogMessageW(app->ai.hwnd,&msg)||handled;
        }
        if(handled)continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    int code=int(msg.wParam);
    while(!host.tabs.empty()){
        App* app=host.tabs.back();
        host.tabs.pop_back();
        if(IsWindow(app->h))DestroyWindow(app->h);
        delete app;
    }
    host.reap();
    return code;
}
int WINAPI WinMain(HINSTANCE inst,HINSTANCE,LPSTR,int){int serviceArgc=0;LPWSTR* serviceArgv=CommandLineToArgvW(GetCommandLineW(),&serviceArgc);if(serviceArgc>1&&std::wstring(serviceArgv[1])==L"--chem-service"){int code=chemservice::worker(serviceArgc,serviceArgv);LocalFree(serviceArgv);return code;}LocalFree(serviceArgv);ULONG_PTR token;GdiplusStartupInput gi;GdiplusStartup(&token,&gi,0);int result=0;try{
    // Per-monitor scaling; all hit rectangles use the same logical coordinates.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT oleInit=OleInitialize(nullptr);
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::wstring first=argc>1?argv[1]:L"";
    if(first==L"--formula-check"){
        Session sample;ObjectId c1,c2,o,b;
        sample.doc.addAtom(Vec2(),6,&c1);sample.doc.addAtom(Vec2(30,0),6,&c2);sample.doc.addAtom(Vec2(60,0),8,&o);
        sample.doc.addBond(c1,c2,&b);sample.doc.addBond(c2,o,&b);
        auto f=analyze(sample.doc,{ObjectRef(ObjectKind::Atom,c1)});
        bool ok=f.valid&&f.plain=="C2H6O"&&std::abs(f.averageMass-46.069)<.02;
        if(argc>2){std::ofstream report(U(argv[2]));report<<f.plain<<" "<<f.averageMass<<" "<<(ok?"PASS":"FAIL");}
        LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();GdiplusShutdown(token);return ok?0:3;
    }
    bool embeddingMode=_wcsicmp(first.c_str(),L"-Embedding")==0||_wcsicmp(first.c_str(),L"--embedding")==0;
    bool clipboardCopyMode=_wcsicmp(first.c_str(),L"--clipboard-copy")==0;
    ole::setPreviewFactory([](const std::string& bytes,SIZE size){return olepreview::fromNative(bytes,size);});
    // Register the OLE class object before the comparatively slow UI/renderer
    // startup. Word probes the class immediately after launching -Embedding and
    // otherwise starts several throw-away servers that never receive a client.
    DWORD oleCookie=0;
    if(embeddingMode){
        HRESULT classHr=ole::registerClassObject(&oleCookie);
        if(FAILED(classHr)){studio_i18n::messageBox(0,L"无法注册 OLE 类对象。",L"Molecule Studio",MB_OK|MB_ICONERROR);LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return 3;}
    }
    if(_wcsicmp(first.c_str(),L"--register")==0){
        bool ok=ole::registerServer(ole::exePath(),false);
        studio_i18n::messageBox(0,ok?L"Molecule Studio 已注册为 OLE 对象服务器，可在 Word/PowerPoint 的“插入对象”里找到。":L"注册 OLE 对象服务器失败（无法写入 HKCU\Software\Classes）。",L"Molecule Studio",MB_OK|(ok?MB_ICONINFORMATION:MB_ICONERROR));
        LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return ok?0:1;
    }
    if(_wcsicmp(first.c_str(),L"--unregister")==0){
        ole::registerServer(ole::exePath(),true);LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return 0;
    }
    App a;
    if(argc>2&&first==L"--expand-group-check"){int code=0;try{expandGroupCheck(a,U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();code=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return code;}
    if(argc>2&&first==L"--fuzzy-search-check"){int code=0;try{fuzzySearchCheck(U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();code=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return code;}
    if(argc>2&&first==L"--custom-group-check"){int code=0;try{customGroupCheck(U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();code=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return code;}
    if(argc>2&&first==L"--hydrogen-check"){
        int code=0;try{bondHydrogenCheck(U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();code=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return code;
    }
    if(argc>2&&first==L"--sprout-check"){
        int code=0;try{sproutLayoutCheck(a,U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();code=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return code;
    }
    if(argc>2&&first==L"--drawing-check"){
        int checkCode=0;try{drawingSettingsCheck(a,U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();checkCode=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return checkCode;
    }
    if(argc>2&&first==L"--triple-check"){
        int checkCode=0;try{tripleBondCheck(a,U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();checkCode=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return checkCode;
    }
    if(argc>2&&first==L"--pattern-check"){
        int checkCode=0;try{patternDragCheck(a,U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();checkCode=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return checkCode;
    }
    if(argc>2&&first==L"--groups-copy-check"){int code=0;try{groupsCopyCheck(a,U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();code=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return code;}
    if(argc>2&&first==L"--alias-label-check"){
        int checkCode=0;try{aliasLabelCheck(U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();checkCode=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return checkCode;
    }
    if(argc>2&&first==L"--nmr-visual-check"){
        int checkCode=0;try{nmrVisualCheck(U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();checkCode=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return checkCode;
    }
    if(argc>2&&first==L"--fixed-drag-check"){
        int checkCode=0;try{fixedDragCheck(a,U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();checkCode=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return checkCode;
    }
    if(argc>2&&first==L"--drag-preview-check"){
        int checkCode=0;try{dragPreviewCheck(a,U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();checkCode=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return checkCode;
    }
    if(argc>2&&first==L"--align-check"){
        int checkCode=0;try{checkCode=alignTextCheck(a,U(argv[2]));}catch(const std::exception& e){std::ofstream(U(argv[2])+"/error.txt")<<e.what();checkCode=2;}LocalFree(argv);if(SUCCEEDED(oleInit))OleUninitialize();studio_ui::clearFonts();GdiplusShutdown(token);return checkCode;
    }
    a.customTemplateChecking=argc>2&&first==L"--custom-template-check";a.tableChecking=argc>2&&first==L"--table-check";a.uiChecking=argc>2&&first==L"--settings-ai-check";a.aiLiveChecking=argc>2&&first==L"--ai-live-check";a.aiChecking=a.aiLiveChecking||(argc>2&&first==L"--ai-check");a.chromeChecking=argc>2&&first==L"--chrome-check";a.stereoLabelChecking=argc>2&&first==L"--stereo-label-check";a.ringLibraryChecking=argc>2&&first==L"--ring-library-check";a.templateLibraryChecking=argc>2&&first==L"--template-library-check";a.textExtendChecking=argc>2&&first==L"--text-extend-check";a.groupDirectionChecking=argc>2&&first==L"--group-direction-check";a.completionChecking=argc>2&&first==L"--completion-check";a.textWrapChecking=argc>2&&first==L"--text-wrap-check";a.arrowShortcutChecking=argc>2&&first==L"--arrow-shortcut-check";a.testing=argc>2&&(first==L"--test"||a.customTemplateChecking||a.tableChecking||a.chromeChecking||a.aiChecking||a.uiChecking||a.stereoLabelChecking||a.textWrapChecking||a.arrowShortcutChecking||a.textExtendChecking||a.ringLibraryChecking||a.templateLibraryChecking||a.groupDirectionChecking||a.completionChecking);a.benchmarking=argc>2&&first==L"--bench";a.closeChecking=argc>2&&first==L"--close-check";bool probing=argc>2&&first==L"--probe";bool shotMode=argc>3&&first==L"--shot";bool perfMode=argc>3&&first==L"--perf";bool firstEditMode=argc>3&&first==L"--first-edit";bool canvasBenchMode=argc>3&&first==L"--canvas-bench";if(a.testing||a.benchmarking||probing||a.closeChecking)a.testOut=U(argv[2]);std::string shotFile,perfFile,firstEditFile;if(shotMode){a.testOut=U(argv[3]);shotFile=U(argv[2]);}if(perfMode){a.testOut=U(argv[2]);perfFile=U(argv[3]);}if(firstEditMode){a.testOut=U(argv[2]);firstEditFile=U(argv[3]);}if(canvasBenchMode){a.testOut=U(argv[2]);perfFile=U(argv[3]);}int shotW=1500,shotH=950;double shotPanX=0,shotPanY=0,shotZoom=1;if(shotMode&&argc>5){shotW=std::max(640,_wtoi(argv[4]));shotH=std::max(480,_wtoi(argv[5]));}if(shotMode&&argc>7){shotPanX=_wtof(argv[6]);shotPanY=_wtof(argv[7]);}if(shotMode&&argc>8)shotZoom=_wtof(argv[8]);
    studioio::olePublisher()=[&a](const Document& subset,const std::string& native,const std::string& xml){return a.publishOleClipboard(subset,native,xml);};
    if(!embeddingMode&&!a.testing&&!a.benchmarking&&!a.closeChecking&&!probing&&!shotMode&&!perfMode&&!firstEditMode&&!canvasBenchMode)ole::registerServer(ole::exePath(),false);
    WNDCLASSW wc={};wc.lpfnWndProc=windowProc;wc.hInstance=inst;wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(1));wc.lpszClassName=L"MoleculeStudioShell";RegisterClassW(&wc);wc.style=CS_OWNDC|CS_DBLCLKS;wc.lpfnWndProc=canvasProc;wc.lpszClassName=L"MoleculeStudioCanvas";RegisterClassW(&wc);
    // The measurement hook below runs in the single-window path, so it also disables tab hosting.
    bool tabbedMode=!embeddingMode&&!a.testing&&!a.benchmarking&&!a.closeChecking&&!probing&&!shotMode&&!perfMode&&!firstEditMode&&!canvasBenchMode&&!clipboardCopyMode&&GetEnvironmentVariableW(L"MOLECULE_STUDIO_OPEN_AI",nullptr,0)==0;
    if(tabbedMode){
        std::string initialFile=argc>1?U(argv[1]):std::string();
        LocalFree(argv);
        int code=runTabbedStudio(inst,initialFile);
        embedded::clearCaches();
        if(SUCCEEDED(oleInit))OleUninitialize();
        studio_ui::clearFonts();GdiplusShutdown(token);
        return code;
    }
    g_currentApp=&a;
    HWND h=studio_i18n::createWindowEx(0,L"MoleculeStudioShell",L"Molecule Studio 0.95",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1560,950,0,0,inst,&a);
    a.scale=(a.testing||shotMode||perfMode)?1.f:GetDpiForWindow(h)/96.f;{UINT dpi=GetDpiForWindow(h);olepreview::previewDpi()=dpi?dpi:96;}
    SetWindowPos(h,0,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    studio_i18n::createWindowEx(0,L"MoleculeStudioCanvas",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,0,0,100,100,h,0,inst,&a);
    a.hbar=studio_i18n::createWindowEx(0,L"SCROLLBAR",L"",WS_CHILD|WS_VISIBLE|SBS_HORZ,0,0,10,10,h,0,inst,0);
    a.vbar=studio_i18n::createWindowEx(0,L"SCROLLBAR",L"",WS_CHILD|WS_VISIBLE|SBS_VERT,0,0,10,10,h,0,inst,0);
    // Canvas letters are chemistry/tool shortcuts; the sibling RichEdit retains
    // its own IME context for actual Chinese text entry.
    ImmAssociateContext(a.canvas,nullptr); // Drawing shortcuts stay outside IME; begin() restores the context for text.
    a.textEditor.initialize(h,a.canvas,&a.s,&a);a.edit=a.textEditor.hwnd;
    wireTextEditor(a);
    a.initializeTextControls();
    a.createGraphics();
    a.layout();if(clipboardCopyMode){if(argc>2)a.error(a.s.open(U(argv[2])));else a.demo();a.s.selected=a.s.all();auto copyError=studioio::copy(a.s,a.canvas,false);a.status=copyError.ok()?a.s.status:copyError.message;a.s.refresh();a.invalidate();}else if(shotMode){auto p=U(argv[2]);a.error(a.s.open(p));a.fileName=p.substr(p.find_last_of("/\\")+1);}else if(argc>1&&!embeddingMode&&!a.testing&&!a.benchmarking&&!a.closeChecking&&!probing&&!shotMode&&!perfMode&&!firstEditMode){auto p=U(argv[1]);if(p=="--demo")a.demo();else{a.error(a.s.open(p));a.fileName=p.substr(p.find_last_of("/\\")+1);}}a.textEditor.nextFamily=a.s.doc.defaultFont();a.textEditor.nextSize=a.s.doc.defaultTextSize();a.updateInfo();LocalFree(argv);
    // Unknown tool names are never silently mapped to a different shape.
    for(auto* list:{&a.groups,&a.graphicsGroups,&a.textGroups})for(auto& gr:*list)for(auto& b:gr.items)if(!b.cmd.empty()&&b.cmd[0]!='@'){bool found=false;for(auto& e:a.s.entries)if(e.command==b.cmd&&e.limitation.empty())found=true;b.enabled=found;}
    static StudioOleHost oleHost;oleHost.app=&a;ole::setDefaultHost(&oleHost);
    if(embeddingMode){
        // Out-of-place OLE server: stay hidden until the container calls DoVerb.
        a.embedded=true;ShowWindow(h,SW_HIDE);
    }else{
        ShowWindow(h,a.closeChecking?SW_HIDE:(a.testing||probing||shotMode||perfMode||firstEditMode||canvasBenchMode)?SW_SHOWNOACTIVATE:SW_MAXIMIZE);
        // SW_MAXIMIZE resizes the shell AFTER the command-line file was opened and
        // fitted. When the display is smaller than the 1560x950 creation size, the
        // canvas shrinks but the view transform still belongs to the old viewport,
        // which leaves the drawing off-screen (a blank canvas). Re-layout and fit
        // once the final client size is known.
        if(!a.testing&&!a.benchmarking&&!a.closeChecking&&!probing&&!shotMode&&!perfMode){
            // SW_MAXIMIZE may still be settling when this line runs; the client
            // rect can change after it returns. Fit once the size messages have
            // been processed, or the drawing is fitted to the pre-maximize size.
            PostMessageW(h,WM_APP+40,0,0);
        }
    }
    UpdateWindow(h);
    // Test hook: open the assistant at start-up so its window can be measured without
    // driving the ribbon by hand. Inert unless the variable is set.
    if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_OPEN_AI",nullptr,0)>0&&!a.testing&&!embeddingMode)a.showAI();
    if(shotMode){
        SetWindowPos(h,0,0,0,shotW,shotH,SWP_NOMOVE|SWP_NOZORDER);a.layout();
        a.s.fit();if(shotZoom!=1.0){auto v=a.s.view();v.scale*=shotZoom;a.s.setView(v);}a.s.refresh();a.updateInfo();
        a.invalidate();UpdateWindow(h);UpdateWindow(a.canvas);
        if(shotPanX!=0||shotPanY!=0){auto v=a.s.view();double s=v.scale*v.dpiScale;v.center.x-=shotPanX/s;v.center.y-=shotPanY/s;a.s.setView(v);a.invalidate();UpdateWindow(a.canvas);}
        a.screenshot(a.testOut,false,false);a.s.dirty=false;PostQuitMessage(0);
    }
    if(perfMode){
        SetWindowPos(h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);a.layout();
        a.benchmarkFile(perfFile);a.s.dirty=false;PostQuitMessage(0);
    }
    if(firstEditMode){
        SetWindowPos(h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);a.layout();
        a.firstEditBench(firstEditFile,a.testOut);a.s.dirty=false;PostQuitMessage(0);
    }
    if(canvasBenchMode){
        SetWindowPos(h,0,0,0,1500,950,SWP_NOMOVE|SWP_NOZORDER);a.layout();
        a.canvasBench(perfFile);a.s.dirty=false;PostQuitMessage(0);
    }
    if(probing)a.probe(a.testOut);if(a.testing||a.benchmarking||a.closeChecking)SetTimer(h,1,300,0);else if(!probing&&!shotMode&&!perfMode&&!embeddingMode)SetFocus(a.canvas);
    MSG msg;while(GetMessageW(&msg,0,0,0)>0){if(IsWindow(a.tableEditor.hwnd)&&(msg.hwnd==a.tableEditor.hwnd||IsChild(a.tableEditor.hwnd,msg.hwnd))&&IsDialogMessageW(a.tableEditor.hwnd,&msg))continue;if(IsWindow(a.settingsWindow.hwnd)&&(msg.hwnd==a.settingsWindow.hwnd||IsChild(a.settingsWindow.hwnd,msg.hwnd))&&IsDialogMessageW(a.settingsWindow.hwnd,&msg))continue;if(IsWindow(a.ai.hwnd)&&(msg.hwnd==a.ai.hwnd||IsChild(a.ai.hwnd,msg.hwnd))&&IsDialogMessageW(a.ai.hwnd,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}result=int(msg.wParam);if(a.closeChecking){bool ok=a.canvasDestroySeen&&a.rendererReleasedBeforeCanvasDestroy&&!a.gl&&!a.rc&&!a.dc;std::ofstream(a.testOut)<<"{\"closed\":true,\"resources_released_before_canvas_destroy\":"<<(ok?"true":"false")<<"}";if(!ok)result=4;}a.shutdownRenderer();if(g_oleClipboard){/* Rendering every offered format here (OleFlushClipboard) froze the app for tens of seconds when the last copy held a large structure; drop the clipboard instead. */OleSetClipboard(nullptr);g_oleClipboard->Release();g_oleClipboard=nullptr;}if(oleCookie)CoRevokeClassObject(oleCookie);if(SUCCEEDED(oleInit))OleUninitialize();
}catch(const std::exception& e){studio_i18n::messageBox(0,W(e.what()).c_str(),L"Molecule Studio",MB_OK|MB_ICONERROR);result=2;}embedded::clearCaches();studio_ui::clearFonts();GdiplusShutdown(token);return result;}












