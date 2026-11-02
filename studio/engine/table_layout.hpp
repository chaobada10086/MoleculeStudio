#pragma once
#include "next/graphics/tables.hpp"
namespace r62 { namespace graphics {
struct TableSegment {core::Vec2 a,b;double width;bool dashed;};
inline std::string tableStyle(const core::Document& doc,core::ObjectId id){auto value=doc.extensions(core::ObjectRef(core::ObjectKind::Table,id)).findValue("studio.table.style");return value?*value:"grid";}
inline bool coveredCell(const TableGrid& g,uint32_t r,uint32_t c){for(const auto& m:g.merged)if(r>=m.row&&r<m.row+m.rowSpan&&c>=m.column&&c<m.column+m.columnSpan)return r!=m.row||c!=m.column;return false;}
inline std::vector<TableSegment> tableSegments(const core::Document& doc,core::ObjectId id,const TableData& data){
    const auto& g=data.grid;std::vector<TableSegment> lines;auto style=tableStyle(doc,id);double width=g.borderWidth>0?g.borderWidth:.6;
    auto add=[&](core::Vec2 a,core::Vec2 b,double w,bool dashed){if(!lines.empty()){auto& p=lines.back();if(p.b==a&&p.width==w&&p.dashed==dashed&&((p.a.x==p.b.x&&a.x==b.x)||(p.a.y==p.b.y&&a.y==b.y))){p.b=b;return;}}lines.push_back({a,b,w,dashed});};
    double y=g.frame.min.y;
    for(uint32_t r=0;r<=g.rows();++r){bool outer=r==0||r==g.rows();bool three=style=="three";
        bool draw=style!="none"&&(three?(outer||r==1):(outer?g.borders.outer!=TableBorderStyle::None:g.borders.inner!=TableBorderStyle::None));double x=g.frame.min.x;
        for(uint32_t c=0;c<g.columns();++c){bool hidden=false;for(const auto& m:g.merged)if(r>m.row&&r<m.row+m.rowSpan&&c>=m.column&&c<m.column+m.columnSpan)hidden=true;
            if(draw&&!hidden)add(core::Vec2(x,y),core::Vec2(x+g.columnWidths[c],y),three?(outer?width*2:width):width,(outer?g.borders.outer:g.borders.inner)==TableBorderStyle::Dashed);x+=g.columnWidths[c];}
        if(r<g.rows())y+=g.rowHeights[r];
    }
    if(style!="three"&&style!="horizontal"&&style!="none"){
        double x=g.frame.min.x;for(uint32_t c=0;c<=g.columns();++c){bool outer=c==0||c==g.columns();bool draw=outer?g.borders.outer!=TableBorderStyle::None:g.borders.inner!=TableBorderStyle::None;double y=g.frame.min.y;
            for(uint32_t r=0;r<g.rows();++r){bool hidden=false;for(const auto& m:g.merged)if(c>m.column&&c<m.column+m.columnSpan&&r>=m.row&&r<m.row+m.rowSpan)hidden=true;
                if(draw&&!hidden)add(core::Vec2(x,y),core::Vec2(x,y+g.rowHeights[r]),width,(outer?g.borders.outer:g.borders.inner)==TableBorderStyle::Dashed);y+=g.rowHeights[r];}
            if(c<g.columns())x+=g.columnWidths[c];
        }
    }return lines;
}
} }
