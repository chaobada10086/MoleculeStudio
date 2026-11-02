#pragma once
// In-app regression for 排列与变换 -> 左对齐 / 水平居中 / 右对齐 on captions.
//
// WHY THIS CHECK EXISTS
// -----------------------------------------------------------------------------
// Alignment used to box a Text object as its anchor POINT, because the selection
// module has no font engine. A left-align therefore collapsed every caption's
// anchor onto one x, and the following right-align (and centre-align) had no
// delta left to apply: the click looked dead. On top of that the selection
// rectangle around a caption was drawn from the measured glyphs, so the two
// disagreed. The fix feeds the align engine the measured ink box through
// selection::TextBoundsProvider (StudioTextBounds in arrange_actions.hpp).
//
// This check drives the REAL ribbon path - App::execute with the same Button
// values makeTools() builds - so it covers dispatch, the transaction and the
// metrics adapter, not just alignObjects.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

namespace aligncheck {

inline bool same(double a,double b){return std::fabs(a-b)<=1e-6;}

inline int alignTextCheck(App& app,const std::string& dir){
    Session& s=app.s;
    s.cancel();
    s.doc=Document();
    s.history.clear();
    s.selected.clear();
    s.doc.setDefaultTextSize(12);
    s.input.bindDocument(s.doc.id());
    s.refresh();

    ObjectId first=kInvalidObjectId,second=kInvalidObjectId,third=kInvalidObjectId;
    s.doc.addText(Vec2(20,20),12.0,"A",&first);
    s.doc.addText(Vec2(120,60),20.0,"LONG CAPTION",&second);
    s.doc.addText(Vec2(320,110),14.0,"MEDIUM",&third);
    s.doc.rebuildDerived();
    s.refresh();
    s.selected=s.all();

    StudioTextBounds metrics(s.glyphs);
    struct Ink {
        Box a,b,c;
    };
    auto ink=[](StudioTextBounds& m,Document& doc,ObjectId id){
        Box box(Vec2(1,1),Vec2(-1,-1));
        if(!m.textInkBox(doc,id,&box))box=Box(Vec2(),Vec2());
        return box;
    };
    auto measure=[&](Ink* out){
        out->a=ink(metrics,s.doc,first);
        out->b=ink(metrics,s.doc,second);
        out->c=ink(metrics,s.doc,third);
    };

    Ink start; measure(&start);
    const double widthA=start.a.width(),widthB=start.b.width(),widthC=start.c.width();
    const double anchorB0=s.doc.texts().get(second)->position.x;
    const double anchorC0=s.doc.texts().get(third)->position.x;

    // The ribbon calls execute() with these values (studio.cpp makeTools()).
    app.execute(Button("align0","左对齐","align0","@align",0));
    Ink left; measure(&left);
    app.execute(Button("align2","右对齐","align2","@align",2));
    Ink right; measure(&right);
    const double anchorB2=s.doc.texts().get(second)->position.x;
    const double anchorC2=s.doc.texts().get(third)->position.x;
    app.execute(Button("align1","水平居中","align1","@align",1));
    Ink centre; measure(&centre);

    const bool widths = widthA>0.0&&widthB>widthA&&widthC>0.0;
    const bool leftEdges = same(left.a.min.x,left.b.min.x)&&same(left.b.min.x,left.c.min.x);
    const bool rightEdges = same(right.a.max.x,right.b.max.x)&&same(right.b.max.x,right.c.max.x);
    const bool rightMoved = std::fabs(anchorB2-anchorB0)>1e-6||std::fabs(anchorC2-anchorC0)>1e-6;
    const double ca=(centre.a.min.x+centre.a.max.x)*0.5;
    const double cb=(centre.b.min.x+centre.b.max.x)*0.5;
    const double cc=(centre.c.min.x+centre.c.max.x)*0.5;
    const bool centred = same(ca,cb)&&same(cb,cc);
    const bool captioned = right.a.width()>0.0&&right.b.width()>0.0&&right.c.width()>0.0;

    bool boxed=true;
    // Wrapped, centred and right-aligned captions use their complete frame.
    for(auto id:{first,second,third}){auto f=s.doc.texts().get(id)->format;f.align=uint8_t(id%3);s.doc.setTextFormat(id,f);s.doc.extensions(ObjectRef(ObjectKind::Text,id)).set("studio.text.width",std::to_string(70+id*25));}
    TextRun multiline;multiline.utf8="Long first line\nSecond line\nEnd";s.doc.setTextRuns(second,{multiline});s.refresh();
    for(int mode=0;mode<6;++mode){s.selected=s.all();app.execute(Button("","对齐","","@align",mode));Ink boxes;measure(&boxes);auto edge=[&](const Box& b){return mode==0?b.min.x:mode==1?(b.min.x+b.max.x)*.5:mode==2?b.max.x:mode==3?b.min.y:mode==4?(b.min.y+b.max.y)*.5:b.max.y;};boxed=boxed&&same(edge(boxes.a),edge(boxes.b))&&same(edge(boxes.b),edge(boxes.c));}
    const bool ok=widths&&leftEdges&&rightEdges&&rightMoved&&centred&&captioned&&boxed;

    const std::string path=dir+"/align-check.json";
    std::ofstream out(path.c_str());
    out<<"{\n"
       <<"  \"pass\": "<<(ok?"true":"false")<<",\n"
       <<"  \"ink_widths\": ["<<widthA<<", "<<widthB<<", "<<widthC<<"],\n"
       <<"  \"left_edges\": ["<<left.a.min.x<<", "<<left.b.min.x<<", "<<left.c.min.x<<"],\n"
       <<"  \"right_edges_after_right\": ["<<right.a.max.x<<", "<<right.b.max.x<<", "<<right.c.max.x<<"],\n"
       <<"  \"centres\": ["<<ca<<", "<<cb<<", "<<cc<<"],\n"
       <<"  \"right_align_moved_anchor\": "<<(rightMoved?"true":"false")<<",\n"
       <<"  \"status\": \""<<app.s.status<<"\"\n"
       <<"}\n";
    out.close();
    std::printf("align-check %s\n",ok?"PASS":"FAIL");
    std::printf("  ink widths        %f %f %f\n",widthA,widthB,widthC);
    std::printf("  left edges        %f %f %f\n",left.a.min.x,left.b.min.x,left.c.min.x);
    std::printf("  right edges       %f %f %f\n",right.a.max.x,right.b.max.x,right.c.max.x);
    std::printf("  centres           %f %f %f\n",ca,cb,cc);
    std::printf("  right moved anchor %s\n",rightMoved?"yes":"no");
    std::printf("  status            %s\n",app.s.status.c_str());
    return ok?0:1;
}

} // namespace aligncheck

inline int alignTextCheck(App& app,const std::string& dir){return aligncheck::alignTextCheck(app,dir);}
