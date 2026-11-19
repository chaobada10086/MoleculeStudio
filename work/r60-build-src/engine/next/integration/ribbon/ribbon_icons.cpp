#include "ribbon_icons.hpp"
#include <algorithm>
#include <cmath>

namespace r62 { namespace integration { namespace ribbon {
namespace {

void sline(HDC dc,int x1,int y1,int x2,int y2,COLORREF c,int w){
    HPEN p=CreatePen(PS_SOLID,w,c);HGDIOBJ o=SelectObject(dc,p);
    MoveToEx(dc,x1,y1,0);LineTo(dc,x2,y2);
    SelectObject(dc,o);DeleteObject(p);
}
void spoly(HDC dc,const POINT* pts,int n,bool closed,COLORREF c,int w,bool filled=false,COLORREF fill=0){
    HPEN p=CreatePen(PS_SOLID,w,c);HGDIOBJ o=SelectObject(dc,p);
    HBRUSH b=filled?CreateSolidBrush(fill):(HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ ob=SelectObject(dc,b);
    if(closed)Polygon(dc,(POINT*)pts,n);else Polyline(dc,pts,n);
    SelectObject(dc,ob);if(filled)DeleteObject(b);
    SelectObject(dc,o);DeleteObject(p);
}
void sellipse(HDC dc,int l,int t,int r,int b,COLORREF c,int w,bool filled=false,COLORREF fill=0){
    HPEN p=CreatePen(PS_SOLID,w,c);HGDIOBJ o=SelectObject(dc,p);
    HBRUSH br=filled?CreateSolidBrush(fill):(HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ ob=SelectObject(dc,br);
    Ellipse(dc,l,t,r,b);
    SelectObject(dc,ob);if(filled)DeleteObject(br);
    SelectObject(dc,o);DeleteObject(p);
}
void srect(HDC dc,int l,int t,int r,int b,COLORREF c,int w,bool filled=false,COLORREF fill=0){
    HPEN p=CreatePen(PS_SOLID,w,c);HGDIOBJ o=SelectObject(dc,p);
    HBRUSH br=filled?CreateSolidBrush(fill):(HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ ob=SelectObject(dc,br);
    Rectangle(dc,l,t,r,b);
    SelectObject(dc,ob);if(filled)DeleteObject(br);
    SelectObject(dc,o);DeleteObject(p);
}
void sround(HDC dc,int l,int t,int r,int b,int rad,COLORREF c,int w){
    HPEN p=CreatePen(PS_SOLID,w,c);HGDIOBJ o=SelectObject(dc,p);
    HBRUSH br=(HBRUSH)GetStockObject(NULL_BRUSH);HGDIOBJ ob=SelectObject(dc,br);
    RoundRect(dc,l,t,r,b,rad,rad);
    SelectObject(dc,ob);SelectObject(dc,o);DeleteObject(p);
}
void sarc(HDC dc,int l,int t,int r,int b,double a1,double a2,COLORREF c,int w){
    double cx=(l+r)/2.0,cy=(t+b)/2.0,rx=(r-l)/2.0,ry=(b-t)/2.0;
    double s=a1*3.14159265358979/180.0,e=a2*3.14159265358979/180.0;
    if(e<=s)e+=6.28318530717959;
    POINT p[48];int n=0;
    for(int i=0;i<=32&&n<48;++i){double a=s+(e-s)*i/32.0;p[n].x=int(cx+std::cos(a)*rx);p[n].y=int(cy-std::sin(a)*ry);++n;}
    HPEN pen=CreatePen(PS_SOLID,w,c);HGDIOBJ o=SelectObject(dc,pen);
    Polyline(dc,p,n);SelectObject(dc,o);DeleteObject(pen);
}
void arrowHead(HDC dc,int x,int y,double ang,double len,COLORREF c,int w){
    double a1=ang+2.6,a2=ang-2.6;
    sline(dc,x,y,x+int(std::cos(a1)*len),y+int(std::sin(a1)*len),c,w);
    sline(dc,x,y,x+int(std::cos(a2)*len),y+int(std::sin(a2)*len),c,w);
}

struct Cv {
    HDC dc;int ox,oy,s;COLORREF fg,ac;double u;
    Cv(HDC d,int x,int y,int size,COLORREF f,COLORREF a):dc(d),ox(x),oy(y),s(size),fg(f),ac(a),u(size/16.0<1?1:size/16.0){}
    int X(double v)const{return ox+int(v*s+0.5);}
    int Y(double v)const{return oy+int(v*s+0.5);}
    int W(double k=1.0)const{int w=int(u*k+0.5);return w<1?1:w;}
    void L(double x1,double y1,double x2,double y2,COLORREF c,int w){sline(dc,X(x1),Y(y1),X(x2),Y(y2),c,w);}
    void E(double l,double t,double r,double b,COLORREF c,int w,bool f=false,COLORREF fl=0){sellipse(dc,X(l),Y(t),X(r),Y(b),c,w,f,fl);}
    void R(double l,double t,double r,double b,COLORREF c,int w,bool f=false,COLORREF fl=0){srect(dc,X(l),Y(t),X(r),Y(b),c,w,f,fl);}
    void RD(double l,double t,double r,double b,COLORREF c,int w){sround(dc,X(l),Y(t),X(r),Y(b),int(s*0.25),c,w);}
    void AR(double x1,double y1,double x2,double y2,double a1,double a2,COLORREF c,int w){sarc(dc,X(x1),Y(y1),X(x2),Y(y2),a1,a2,c,w);}
    void poly(const double* uv,int n,bool closed,COLORREF c,int w,bool f=false,COLORREF fl=0){
        POINT p[64];int m=n<64?n:64;for(int i=0;i<m;++i){p[i].x=X(uv[i*2]);p[i].y=Y(uv[i*2+1]);}
        spoly(dc,p,m,closed,c,w,f,fl);
    }
    void P(double x,double y,COLORREF c,int w){sline(dc,X(x),Y(y),X(x)+1,Y(y)+1,c,w);}
    void arrow(double x1,double y1,double x2,double y2,COLORREF c,int w,double hl){
        L(x1,y1,x2,y2,c,w);
        arrowHead(dc,X(x2),Y(y2),std::atan2(double(Y(y2)-Y(y1)),double(X(x2)-X(x1))),hl,c,w);
    }
};

static void textGlyph(HDC dc,Cv& c,const wchar_t* t,COLORREF col,int pts){
    HFONT f=CreateFontW(-pts,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    HGDIOBJ o=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,col);
    RECT r;r.left=c.ox;r.top=c.oy;r.right=c.ox+c.s;r.bottom=c.oy+c.s;
    DrawTextW(dc,t,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(dc,o);DeleteObject(f);
}
static void ring(Cv& c,int n,COLORREF col,int w,double rad=0.38){
    POINT p[12];for(int i=0;i<n;++i){double a=-1.5707963+2*3.14159265*i/n;p[i].x=c.X(0.5+std::cos(a)*rad);p[i].y=c.Y(0.5+std::sin(a)*rad);}
    spoly(c.dc,p,n,true,col,w);
}
static void bondLine(Cv& c,double order,COLORREF col,int w){
    if(order<=1){c.L(0.16,0.78,0.84,0.22,col,w);return;}
    double off=0.07;
    if(order==2){c.L(0.16,0.72,0.84,0.16,col,w);c.L(0.16,0.84,0.84,0.28,col,w);return;}
    if(order==3){c.L(0.16,0.68,0.84,0.12,col,w);c.L(0.12,0.78,0.80,0.22,col,w);c.L(0.20,0.88,0.88,0.32,col,w);return;}
    c.L(0.14,0.64,0.82,0.08,col,w);c.L(0.14,0.74,0.82,0.18,col,w);c.L(0.14,0.84,0.82,0.28,col,w);c.L(0.14,0.94,0.82,0.38,col,w);
    (void)off;
}
static void zig(Cv& c,COLORREF col,int w,int n){
    POINT p[16];double step=0.8/(n-1);for(int i=0;i<n;++i){p[i].x=c.X(0.1+i*step);p[i].y=c.Y(i%2?0.30:0.70);}
    spoly(c.dc,p,n,false,col,w);
}
static void blob(Cv& c,COLORREF col,int w,int kind){
    switch(kind){
    case 0:{ // enzyme: pac-man
        c.E(0.18,0.18,0.82,0.82,col,w);
        c.poly((const double[]){0.5,0.5,0.86,0.34,0.86,0.66},3,true,c.fg,w);break;}
    case 1:{ // receptor: membrane + notch
        c.L(0.10,0.80,0.90,0.80,col,w);c.L(0.34,0.80,0.34,0.30,col,w);c.L(0.60,0.80,0.60,0.30,col,w);
        c.L(0.34,0.30,0.60,0.30,col,w);break;}
    case 2:{ // ion channel: barrel
        c.L(0.20,0.80,0.28,0.24,col,w);c.L(0.40,0.80,0.44,0.20,col,w);c.L(0.60,0.80,0.56,0.20,col,w);c.L(0.80,0.80,0.72,0.24,col,w);break;}
    case 3:{ // DNA helix
        for(int i=0;i<4;++i){double t=0.2+i*0.2;c.L(t,0.18,1.0-t+0.2,0.82,col,w);c.L(1.0-t+0.2,0.18,t,0.82,col,w);}break;}
    case 4:{ // membrane
        c.L(0.1,0.42,0.9,0.42,col,w);c.L(0.1,0.58,0.9,0.58,col,w);
        c.E(0.22,0.36,0.30,0.64,col,w);c.E(0.58,0.36,0.66,0.64,col,w);break;}
    case 5:{ // organelle
        c.E(0.16,0.24,0.84,0.76,col,w);c.L(0.30,0.40,0.52,0.56,col,w);c.L(0.52,0.56,0.70,0.34,col,w);break;}
    case 6:{ // ribosome
        c.E(0.18,0.34,0.82,0.66,col,w);c.E(0.30,0.20,0.70,0.46,col,w);break;}
    case 7:{ // virus
        c.E(0.28,0.28,0.72,0.72,col,w);
        for(int i=0;i<8;++i){double a=i*0.785;c.L(0.5+std::cos(a)*0.22,0.5+std::sin(a)*0.22,0.5+std::cos(a)*0.38,0.5+std::sin(a)*0.38,col,w);}break;}
    case 8:{ // bacterium
        c.E(0.12,0.34,0.88,0.66,col,w);c.L(0.30,0.44,0.42,0.56,col,w);c.L(0.58,0.42,0.70,0.56,col,w);break;}
    case 9:{ // plasmid
        c.E(0.20,0.20,0.80,0.80,col,w);c.E(0.42,0.42,0.58,0.58,col,w);break;}
    case 10:{ // protein helix
        for(int i=0;i<3;++i){c.E(0.20+i*0.20,0.28,0.44+i*0.20,0.72,col,w);}break;}
    case 11:{ // cloud
        c.E(0.14,0.44,0.50,0.76,col,w);c.E(0.34,0.28,0.74,0.72,col,w);c.E(0.56,0.44,0.86,0.76,col,w);break;}
    default:{ // generic bio shape
        c.poly((const double[]){0.5,0.14,0.86,0.36,0.76,0.78,0.24,0.78,0.14,0.36},5,true,col,w);break;}
    }
}
static void arrowKind(Cv& c,COLORREF col,int w,int kind){
    switch(kind){
    case 0:c.arrow(0.14,0.72,0.84,0.28,col,w,c.s*0.20);break;
    case 1:c.arrow(0.14,0.50,0.86,0.50,col,w,c.s*0.18);break;
    case 2:{c.R(0.10,0.18,0.90,0.82,col,w);c.L(0.10,0.18,0.90,0.82,col,w);break;}
    case 3:{c.arrow(0.14,0.30,0.72,0.30,col,w,c.s*0.16);c.arrow(0.86,0.70,0.28,0.70,col,w,c.s*0.16);break;}
    case 4:{c.arrow(0.14,0.30,0.80,0.30,col,w,c.s*0.16);c.arrow(0.20,0.64,0.86,0.64,col,w,c.s*0.16);break;}
    case 5:{c.arrow(0.16,0.36,0.74,0.36,col,w,c.s*0.14);c.E(0.30,0.42,0.62,0.66,col,w);break;}
    case 6:{c.arrow(0.16,0.50,0.80,0.50,col,w,c.s*0.16);c.L(0.36,0.30,0.64,0.70,col,w);c.L(0.64,0.30,0.36,0.70,col,w);break;}
    case 7:{c.arrow(0.14,0.34,0.74,0.34,col,w,c.s*0.14);c.arrow(0.86,0.66,0.26,0.66,col,w,c.s*0.14);c.L(0.44,0.44,0.58,0.56,col,w);c.L(0.58,0.44,0.44,0.56,col,w);break;}
    default:{c.arrow(0.20,0.80,0.64,0.30,col,w,c.s*0.16);c.arrow(0.36,0.30,0.80,0.80,col,w,c.s*0.16);break;}
    }
}
} // namespace

void drawIcon(HDC dc,int icon,int variant,int x,int y,int size,COLORREF fg,COLORREF accent){
    Cv c(dc,x,y,size,fg,accent);int w=c.W(1.0),wt=c.W(2.0);
    if(size<6)return;
    switch(icon){
    case IconNone:break;
    case IconCursor:{const double p[]={0.28,0.14,0.28,0.86,0.46,0.66,0.58,0.90,0.70,0.84,0.58,0.60,0.80,0.58};c.poly(p,8,true,fg,w,true,fg);break;}
    case IconLasso:{c.E(0.16,0.16,0.84,0.62,fg,w);c.L(0.50,0.62,0.50,0.86,fg,w);c.E(0.44,0.80,0.56,0.92,fg,w);break;}
    case IconEraser:{const double p[]={0.22,0.72,0.54,0.24,0.82,0.42,0.50,0.86,0.22,0.86};c.poly(p,5,true,fg,w);c.L(0.36,0.52,0.64,0.68,accent,w);break;}
    case IconBond:bondLine(c,1,fg,w);break;
    case IconBondDouble:bondLine(c,2,fg,w);break;
    case IconBondTriple:bondLine(c,3,fg,w);break;
    case IconBondQuad:bondLine(c,4,fg,w);break;
    case IconBondAny:{c.L(0.16,0.78,0.84,0.22,fg,w);textGlyph(dc,c,L"?",accent,int(size*0.45));break;}
    case IconBondWedge:{const double p[]={0.18,0.78,0.84,0.18,0.84,0.34,0.18,0.86};c.poly(p,4,true,fg,w,true,fg);break;}
    case IconBondHash:{const double p[]={0.18,0.78,0.84,0.18,0.84,0.30,0.18,0.84};c.poly(p,4,true,fg,w);for(int i=1;i<=4;++i){double t=i/5.0;c.L(0.18+(0.84-0.18)*t,0.78-(0.78-0.18)*t,0.18+(0.84-0.18)*t+0.10,0.78-(0.78-0.18)*t-0.14,fg,w);}break;}
    case IconBondHollowWedge:{const double p[]={0.18,0.78,0.84,0.18,0.84,0.36,0.18,0.88};c.poly(p,4,true,fg,w);break;}
    case IconBondBold:{const double p[]={0.16,0.84,0.84,0.20,0.84,0.30,0.16,0.90};c.poly(p,4,true,fg,w,true,fg);break;}
    case IconBondDashed:{for(int i=0;i<4;++i){double t=i/4.0;c.L(0.14+0.70*t,0.84-0.62*t,0.14+0.70*t+0.12,0.84-0.62*t-0.10,fg,w);}break;}
    case IconBondDotted:{for(int i=0;i<6;++i){double t=i/6.0;c.P(0.16+0.68*t,0.82-0.60*t,fg,wt);}break;}
    case IconBondWavy:{POINT p[10];for(int i=0;i<10;++i){double t=i/9.0;p[i].x=c.X(0.16+0.68*t);p[i].y=c.Y(0.82-0.60*t+std::sin(t*9.0)*0.06);}spoly(dc,p,10,false,fg,w);break;}
    case IconBondDative:{c.arrow(0.20,0.80,0.80,0.24,fg,w,c.s*0.18);c.L(0.20,0.64,0.36,0.80,accent,w);break;}
    case IconRing3:ring(c,variant?variant:3,fg,w);break;
    case IconRing4:ring(c,4,fg,w);break;
    case IconRing5:ring(c,5,fg,w);break;
    case IconRing6:ring(c,6,fg,w);break;
    case IconRing7:ring(c,7,fg,w);break;
    case IconRing8:ring(c,8,fg,w);break;
    case IconRingAromatic:{ring(c,6,fg,w);c.E(0.34,0.34,0.66,0.66,accent,w);break;}
    case IconRingDiene:{ring(c,5,fg,w);c.L(0.30,0.62,0.44,0.36,accent,w);c.L(0.58,0.38,0.72,0.64,accent,w);break;}
    case IconChair:{const double p[]={0.24,0.26,0.60,0.36,0.80,0.72,0.44,0.62};c.poly(p,4,true,fg,w);c.L(0.24,0.26,0.44,0.62,fg,w);c.L(0.60,0.36,0.80,0.72,fg,w);break;}
    case IconChain:zig(c,fg,w,5);break;
    case IconChainSnake:zig(c,fg,w,7);break;
    case IconText:case IconTextBold:case IconTextItalic:case IconTextUnderline:case IconTextSub:case IconTextSup:
        textGlyph(dc,c,L"A",fg,int(size*0.62));
        if(icon==IconTextBold)textGlyph(dc,c,L"B",accent,int(size*0.34));
        if(icon==IconTextItalic)textGlyph(dc,c,L"I",accent,int(size*0.34));
        if(icon==IconTextUnderline)c.L(0.24,0.86,0.76,0.86,accent,w);
        if(icon==IconTextSub)textGlyph(dc,c,L"2",accent,int(size*0.30));
        if(icon==IconTextSup)textGlyph(dc,c,L"2",accent,int(size*0.30));
        break;
    case IconArrowLarge:arrowKind(c,fg,w,0);break;
    case IconArrowSmall:arrowKind(c,fg,w,1);break;
    case IconArrowHollow:c.E(0.20,0.30,0.80,0.70,fg,w);c.L(0.50,0.30,0.86,0.50,fg,w);c.L(0.50,0.70,0.86,0.50,fg,w);break;
    case IconArrowEquilibrium:arrowKind(c,fg,w,3);break;
    case IconArrowUnbalanced:arrowKind(c,fg,w,4);break;
    case IconArrowResonance:arrowKind(c,fg,w,5);break;
    case IconArrowNoGo:arrowKind(c,fg,w,7);break;
    case IconArrowRetro:arrowKind(c,fg,w,8);break;
    case IconArrowArc:{c.arrow(0.16,0.70,0.70,0.70,fg,w,c.s*0.14);c.arrow(0.70,0.70,0.84,0.34,fg,w,c.s*0.14);break;}
    case IconRect:c.R(0.16,0.26,0.84,0.74,fg,w);break;
    case IconOval:c.E(0.12,0.28,0.88,0.72,fg,w);break;
    case IconCircle:c.E(0.18,0.18,0.82,0.82,fg,w);break;
    case IconRoundRect:c.RD(0.14,0.26,0.86,0.74,fg,w);break;
    case IconLine:c.L(0.16,0.80,0.84,0.24,fg,w);break;
    case IconArc:{int a=variant?variant:180;sarc(dc,c.X(0.12),c.Y(0.12),c.X(0.88),c.Y(0.88),0.0,double(a),fg,w);break;}
    case IconCurve:{POINT p[4]={{c.X(0.14),c.Y(0.78)},{c.X(0.36),c.Y(0.24)},{c.X(0.64),c.Y(0.80)},{c.X(0.86),c.Y(0.28)}};spoly(dc,p,4,false,fg,w);c.P(0.36,0.24,accent,wt);c.P(0.64,0.80,accent,wt);break;}
    case IconEditCurve:{POINT p[4]={{c.X(0.14),c.Y(0.78)},{c.X(0.36),c.Y(0.24)},{c.X(0.64),c.Y(0.80)},{c.X(0.86),c.Y(0.28)}};spoly(dc,p,4,false,fg,w);c.R(0.28,0.16,0.44,0.32,accent,w,true,accent);c.R(0.72,0.20,0.88,0.36,accent,w,true,accent);break;}
    case IconBracketSquare:c.L(0.34,0.18,0.24,0.18,fg,w);c.L(0.24,0.18,0.24,0.82,fg,w);c.L(0.24,0.82,0.34,0.82,fg,w);c.L(0.66,0.18,0.76,0.18,fg,w);c.L(0.76,0.18,0.76,0.82,fg,w);c.L(0.76,0.82,0.66,0.82,fg,w);break;
    case IconBracketParen:{POINT a[5]={{c.X(0.40),c.Y(0.14)},{c.X(0.26),c.Y(0.30)},{c.X(0.26),c.Y(0.70)},{c.X(0.40),c.Y(0.86)},{c.X(0.40),c.Y(0.86)}};spoly(dc,a,4,false,fg,w);
        POINT b[4]={{c.X(0.60),c.Y(0.14)},{c.X(0.74),c.Y(0.30)},{c.X(0.74),c.Y(0.70)},{c.X(0.60),c.Y(0.86)}};spoly(dc,b,4,false,fg,w);break;}
    case IconBracketCurly:{POINT a[6]={{c.X(0.42),c.Y(0.14)},{c.X(0.30),c.Y(0.22)},{c.X(0.30),c.Y(0.44)},{c.X(0.20),c.Y(0.50)},{c.X(0.30),c.Y(0.56)},{c.X(0.30),c.Y(0.78)}};spoly(dc,a,6,false,fg,w);c.L(0.30,0.78,0.42,0.86,fg,w);
        POINT b[6]={{c.X(0.58),c.Y(0.14)},{c.X(0.70),c.Y(0.22)},{c.X(0.70),c.Y(0.44)},{c.X(0.80),c.Y(0.50)},{c.X(0.70),c.Y(0.56)},{c.X(0.70),c.Y(0.78)}};spoly(dc,b,6,false,fg,w);c.L(0.70,0.78,0.58,0.86,fg,w);break;}
    case IconDagger:case IconPlus:{c.L(0.5,0.16,0.5,0.84,fg,w);c.L(0.16,0.5,0.84,0.5,fg,w);break;}
    case IconMinus:c.L(0.16,0.5,0.84,0.5,fg,w);break;
    case IconRadical:{c.E(0.30,0.30,0.70,0.70,fg,w);c.P(0.78,0.22,accent,wt);break;}
    case IconDoubleRadical:{c.E(0.26,0.34,0.58,0.66,fg,w);c.E(0.52,0.34,0.84,0.66,fg,w);break;}
    case IconRadicalCation:{c.E(0.20,0.20,0.80,0.80,fg,w);c.L(0.5,0.30,0.5,0.70,accent,w);c.L(0.30,0.5,0.70,0.5,accent,w);c.P(0.86,0.18,accent,wt);break;}
    case IconRadicalAnion:{c.E(0.20,0.20,0.80,0.80,fg,w);c.L(0.30,0.5,0.70,0.5,accent,w);c.P(0.86,0.18,accent,wt);break;}
    case IconHDot:{textGlyph(dc,c,L"H",fg,int(size*0.52));c.P(0.78,0.30,accent,wt);break;}
    case IconHDash:{textGlyph(dc,c,L"H",fg,int(size*0.52));c.L(0.68,0.30,0.86,0.30,accent,w);break;}
    case IconAttachment:{const double p[]={0.5,0.16,0.78,0.5,0.5,0.84,0.22,0.5};c.poly(p,4,true,fg,w);break;}
    case IconPushElectron:{c.arrow(0.16,0.72,0.80,0.36,fg,w,c.s*0.18);c.E(0.10,0.10,0.30,0.30,accent,w);break;}
    case IconOrbitalS:{c.E(0.20,0.20,0.80,0.80,fg,w);c.E(0.38,0.38,0.62,0.62,accent,w);break;}
    case IconOrbitalP:{c.E(0.16,0.32,0.48,0.68,fg,w);c.E(0.52,0.32,0.84,0.68,fg,w);break;}
    case IconOrbitalSigma:{c.E(0.12,0.34,0.46,0.66,fg,w);c.E(0.54,0.34,0.88,0.66,fg,w);c.L(0.46,0.50,0.54,0.50,accent,w);break;}
    case IconOrbitalSpLobe:{c.E(0.14,0.30,0.52,0.70,fg,w);c.E(0.56,0.20,0.80,0.80,accent,w);break;}
    case IconOrbitalSp3:{c.E(0.12,0.34,0.44,0.66,fg,w);c.E(0.50,0.14,0.74,0.46,accent,w);c.E(0.50,0.60,0.74,0.92,accent,w);break;}
    case IconOrbitalD:{c.E(0.10,0.24,0.44,0.76,fg,w);c.E(0.46,0.24,0.80,0.76,accent,w);c.E(0.30,0.34,0.60,0.66,fg,w);break;}
    case IconQuery:{ring(c,6,fg,w,0.34);textGlyph(dc,c,L"?",accent,int(size*0.42));break;}
    case IconStereoAbs:{textGlyph(dc,c,L"ABS",fg,int(size*0.32));break;}
    case IconStereoRac:{textGlyph(dc,c,L"RAC",fg,int(size*0.32));break;}
    case IconStereoRel:{textGlyph(dc,c,L"REL",fg,int(size*0.32));break;}
    case IconMap:{c.L(0.14,0.26,0.86,0.26,fg,w);c.L(0.14,0.50,0.86,0.50,fg,w);c.L(0.14,0.74,0.86,0.74,fg,w);c.L(0.40,0.16,0.40,0.84,accent,w);break;}
    case IconFragment:{const double p[]={0.20,0.20,0.60,0.20,0.60,0.44,0.44,0.58,0.60,0.72,0.60,0.84,0.20,0.84,0.20,0.58,0.34,0.44,0.20,0.30};c.poly(p,10,true,fg,w);break;}
    case IconPerspective:{c.R(0.14,0.34,0.86,0.80,fg,w);c.L(0.14,0.34,0.34,0.16,fg,w);c.L(0.86,0.34,0.66,0.16,fg,w);c.L(0.34,0.16,0.66,0.16,fg,w);c.L(0.66,0.16,0.66,0.62,accent,w);c.L(0.66,0.62,0.86,0.80,accent,w);break;}
    case IconTable:{c.R(0.12,0.24,0.88,0.76,fg,w);c.L(0.12,0.42,0.88,0.42,fg,w);c.L(0.12,0.60,0.88,0.60,fg,w);c.L(0.38,0.24,0.38,0.76,fg,w);c.L(0.64,0.24,0.64,0.76,fg,w);break;}
    case IconTemplate:{c.R(0.16,0.20,0.84,0.80,fg,w);c.L(0.16,0.36,0.84,0.36,fg,w);c.L(0.34,0.36,0.34,0.80,accent,w);c.L(0.16,0.56,0.84,0.56,fg,w);break;}
    case IconPlate:{c.R(0.12,0.28,0.88,0.72,fg,w);for(int i=0;i<4;++i)c.L(0.24+i*0.18,0.32,0.24+i*0.18,0.68,accent,w);break;}
    case IconBioEnzyme:blob(c,fg,w,0);break;
    case IconBioGProtein:blob(c,fg,w,4);break;
    case IconBioReceptor:blob(c,fg,w,1);break;
    case IconBioIonChannel:blob(c,fg,w,2);break;
    case IconBioImmunoglobin:{const double p[]={0.5,0.16,0.86,0.50,0.50,0.84,0.14,0.50};c.poly(p,4,true,fg,w);c.L(0.30,0.50,0.70,0.50,accent,w);break;}
    case IconBioNuclearPore:{c.E(0.14,0.14,0.86,0.86,fg,w);c.E(0.38,0.38,0.62,0.62,accent,w);break;}
    case IconBioDNA:blob(c,fg,w,3);break;
    case IconBioRNA:{blob(c,fg,w,3);c.P(0.24,0.24,accent,wt);break;}
    case IconBioMembrane:blob(c,fg,w,4);break;
    case IconBioOrganelle:blob(c,fg,w,5);break;
    case IconBioRibosome:blob(c,fg,w,6);break;
    case IconBioVirus:blob(c,fg,w,7);break;
    case IconBioBacterium:blob(c,fg,w,8);break;
    case IconBioPlasmid:blob(c,fg,w,9);break;
    case IconBioProtein:blob(c,fg,w,10);break;
    case IconBioCloud:blob(c,fg,w,11);break;
    case IconBioShape:blob(c,fg,w,12);break;
    case IconUndo:{c.arrow(0.84,0.70,0.30,0.70,fg,w,c.s*0.16);c.L(0.30,0.70,0.30,0.34,fg,w);c.L(0.30,0.34,0.56,0.34,accent,w);break;}
    case IconRedo:{c.arrow(0.16,0.70,0.70,0.70,fg,w,c.s*0.16);c.L(0.70,0.70,0.70,0.34,fg,w);c.L(0.70,0.34,0.44,0.34,accent,w);break;}
    case IconRepeat:{c.arrow(0.30,0.34,0.72,0.34,fg,w,c.s*0.14);c.L(0.72,0.34,0.72,0.24,fg,w);c.arrow(0.70,0.66,0.28,0.66,fg,w,c.s*0.14);c.L(0.28,0.66,0.28,0.76,fg,w);break;}
    case IconCopy:{c.R(0.16,0.16,0.64,0.64,fg,w);c.R(0.36,0.36,0.84,0.84,accent,w);break;}
    case IconCut:{c.L(0.24,0.20,0.70,0.72,fg,w);c.L(0.76,0.20,0.30,0.72,fg,w);c.E(0.16,0.70,0.32,0.86,accent,w);c.E(0.68,0.70,0.84,0.86,accent,w);break;}
    case IconPaste:{c.R(0.22,0.24,0.78,0.86,fg,w);c.R(0.36,0.12,0.64,0.30,fg,w);c.L(0.34,0.50,0.66,0.50,accent,w);c.L(0.34,0.66,0.66,0.66,accent,w);break;}
    case IconDelete:{c.L(0.24,0.28,0.76,0.28,fg,w);c.R(0.30,0.28,0.70,0.86,fg,w);c.R(0.42,0.14,0.58,0.28,fg,w);c.L(0.42,0.42,0.42,0.74,accent,w);c.L(0.58,0.42,0.58,0.74,accent,w);break;}
    case IconSelectAll:{c.R(0.16,0.16,0.84,0.84,accent,w);c.R(0.28,0.28,0.72,0.72,fg,w);break;}
    case IconZoomIn:{c.E(0.14,0.14,0.62,0.62,fg,w);c.L(0.58,0.58,0.86,0.86,fg,wt);c.L(0.38,0.24,0.38,0.52,accent,w);c.L(0.24,0.38,0.52,0.38,accent,w);break;}
    case IconZoomOut:{c.E(0.14,0.14,0.62,0.62,fg,w);c.L(0.58,0.58,0.86,0.86,fg,wt);c.L(0.24,0.38,0.52,0.38,accent,w);break;}
    case IconZoomFit:{c.R(0.12,0.20,0.88,0.80,accent,w);c.R(0.28,0.32,0.72,0.68,fg,w);break;}
    case IconZoomActual:{c.E(0.18,0.18,0.82,0.82,fg,w);textGlyph(dc,c,L"1:1",accent,int(size*0.30));break;}
    case IconPan:{c.L(0.5,0.14,0.5,0.86,fg,w);c.L(0.14,0.5,0.86,0.5,fg,w);arrowHead(dc,c.X(0.5),c.Y(0.14),-1.5708,c.s*0.16,fg,w);arrowHead(dc,c.X(0.5),c.Y(0.86),1.5708,c.s*0.16,fg,w);arrowHead(dc,c.X(0.14),c.Y(0.5),3.1416,c.s*0.16,fg,w);arrowHead(dc,c.X(0.86),c.Y(0.5),0,c.s*0.16,fg,w);break;}
    case IconAlignLeft:{c.R(0.14,0.14,0.34,0.44,fg,w);c.R(0.14,0.56,0.70,0.86,fg,w);c.L(0.10,0.10,0.10,0.90,accent,wt);break;}
    case IconAlignCenterH:{c.R(0.14,0.14,0.86,0.44,fg,w);c.R(0.28,0.56,0.72,0.86,fg,w);c.L(0.50,0.10,0.50,0.90,accent,wt);break;}
    case IconAlignRight:{c.R(0.66,0.14,0.86,0.44,fg,w);c.R(0.30,0.56,0.86,0.86,fg,w);c.L(0.90,0.10,0.90,0.90,accent,wt);break;}
    case IconAlignTop:{c.R(0.14,0.14,0.44,0.70,fg,w);c.R(0.56,0.14,0.86,0.44,fg,w);c.L(0.10,0.10,0.90,0.10,accent,wt);break;}
    case IconAlignCenterV:{c.R(0.14,0.14,0.44,0.86,fg,w);c.R(0.56,0.28,0.86,0.72,fg,w);c.L(0.10,0.50,0.90,0.50,accent,wt);break;}
    case IconAlignBottom:{c.R(0.14,0.56,0.44,0.86,fg,w);c.R(0.56,0.30,0.86,0.86,fg,w);c.L(0.10,0.90,0.90,0.90,accent,wt);break;}
    case IconDistributeH:{c.R(0.10,0.28,0.24,0.72,fg,w);c.R(0.43,0.28,0.57,0.72,fg,w);c.R(0.76,0.28,0.90,0.72,fg,w);break;}
    case IconDistributeV:{c.R(0.28,0.10,0.72,0.24,fg,w);c.R(0.28,0.43,0.72,0.57,fg,w);c.R(0.28,0.76,0.72,0.90,fg,w);break;}
    case IconGroup:{c.R(0.12,0.12,0.64,0.64,accent,w);c.R(0.36,0.36,0.88,0.88,fg,w);break;}
    case IconUngroup:{c.R(0.10,0.10,0.50,0.50,fg,w);c.R(0.50,0.50,0.90,0.90,fg,w);c.L(0.50,0.10,0.90,0.50,accent,w);c.L(0.10,0.50,0.50,0.90,accent,w);break;}
    case IconFront:{c.R(0.12,0.12,0.66,0.66,accent,w,true,accent);c.R(0.34,0.34,0.88,0.88,fg,w);break;}
    case IconBack:{c.R(0.12,0.12,0.66,0.66,fg,w);c.R(0.34,0.34,0.88,0.88,accent,w,true,accent);break;}
    case IconForward:{c.R(0.16,0.16,0.60,0.60,accent,w);c.R(0.40,0.40,0.84,0.84,fg,w);break;}
    case IconBackward:{c.R(0.16,0.16,0.60,0.60,fg,w);c.R(0.40,0.40,0.84,0.84,accent,w);break;}
    case IconRotateL:{c.AR(0.14,0.14,0.86,0.86,90,330,fg,w);c.arrow(0.30,0.16,0.16,0.30,accent,w,c.s*0.14);break;}
    case IconRotateR:{c.AR(0.14,0.14,0.86,0.86,210,450,fg,w);c.arrow(0.70,0.16,0.84,0.30,accent,w,c.s*0.14);break;}
    case IconFlipH:{c.L(0.5,0.10,0.5,0.90,accent,w);const double p[]={0.14,0.30,0.40,0.30,0.40,0.70,0.14,0.70};c.poly(p,4,true,fg,w);c.R(0.60,0.30,0.86,0.70,fg,w);break;}
    case IconFlipV:{c.L(0.10,0.5,0.90,0.5,accent,w);const double p[]={0.30,0.14,0.70,0.14,0.70,0.40,0.30,0.40};c.poly(p,4,true,fg,w);c.R(0.30,0.60,0.70,0.86,fg,w);break;}
    case IconMerge:{c.R(0.10,0.20,0.44,0.54,fg,w);c.R(0.56,0.46,0.90,0.80,fg,w);c.arrow(0.30,0.62,0.70,0.34,accent,w,c.s*0.14);break;}
    case IconColor:{c.E(0.18,0.18,0.82,0.82,fg,w,true,accent);c.E(0.18,0.18,0.82,0.82,fg,w);break;}
    case IconLineWidth:{for(int i=0;i<3;++i)c.L(0.14,0.28+i*0.22,0.86,0.28+i*0.22,fg,(i+1));break;}
    case IconFill:{c.R(0.16,0.20,0.84,0.80,fg,w,true,accent);c.R(0.16,0.20,0.84,0.80,fg,w);break;}
    case IconGrid:{c.R(0.12,0.12,0.88,0.88,fg,w);c.L(0.37,0.12,0.37,0.88,accent,w);c.L(0.63,0.12,0.63,0.88,accent,w);c.L(0.12,0.37,0.88,0.37,accent,w);c.L(0.12,0.63,0.88,0.63,accent,w);break;}
    case IconPanel:{c.R(0.10,0.14,0.90,0.86,fg,w);c.R(0.58,0.14,0.90,0.86,accent,w,true,accent);break;}
    case IconHost:{c.R(0.12,0.24,0.88,0.80,fg,w);c.L(0.12,0.34,0.88,0.34,fg,w);c.L(0.30,0.56,0.70,0.56,accent,w);break;}
    case IconHelp:{c.E(0.14,0.14,0.86,0.86,fg,w);textGlyph(dc,c,L"?",accent,int(size*0.45));break;}
    case IconFile:{const double p[]={0.24,0.10,0.62,0.10,0.78,0.28,0.78,0.90,0.24,0.90};c.poly(p,5,true,fg,w);c.L(0.62,0.10,0.62,0.28,fg,w);c.L(0.62,0.28,0.78,0.28,fg,w);break;}
    case IconNew:case IconOpen:case IconSave:case IconExport:
        {const double p[]={0.24,0.10,0.62,0.10,0.78,0.28,0.78,0.90,0.24,0.90};c.poly(p,5,true,fg,w);
         if(icon==IconNew){c.L(0.51,0.40,0.51,0.68,accent,w);c.L(0.37,0.54,0.65,0.54,accent,w);}
         if(icon==IconOpen){c.L(0.20,0.92,0.86,0.92,accent,w);c.L(0.20,0.92,0.34,0.66,accent,w);c.L(0.86,0.92,1.00,0.66,accent,w);c.L(0.34,0.66,1.00,0.66,accent,w);}
         if(icon==IconSave){c.R(0.36,0.56,0.66,0.88,accent,w);c.R(0.38,0.14,0.64,0.32,accent,w);}
         if(icon==IconExport){c.arrow(0.38,0.70,0.66,0.42,accent,w,c.s*0.12);c.L(0.66,0.42,0.52,0.42,accent,w);c.L(0.66,0.42,0.66,0.56,accent,w);}break;}
    case IconLink:{c.E(0.12,0.38,0.46,0.66,fg,w);c.E(0.54,0.34,0.88,0.62,fg,w);c.L(0.40,0.52,0.60,0.48,accent,w);break;}
    case IconPropBond:{bondLine(c,2,fg,w);break;}
    case IconPropAtom:{c.E(0.30,0.30,0.70,0.70,fg,w);c.E(0.44,0.14,0.56,0.26,fg,w);c.E(0.44,0.74,0.56,0.86,fg,w);break;}
    case IconPropText:textGlyph(dc,c,L"T",fg,int(size*0.60));break;
    default:blob(c,fg,w,12);break;
    }
}

}}}
