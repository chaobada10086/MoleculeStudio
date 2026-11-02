#pragma once
#include "next/arrows_curves/model.hpp"
#include "next/graphics/model.hpp"
#include "next/graphics/tables.hpp"
#include <cmath>
#include <sstream>

namespace studio { namespace graphic_style {
using namespace r62;using namespace core;
inline bool supported(ObjectKind kind){return kind==ObjectKind::Atom||kind==ObjectKind::Bond||kind==ObjectKind::Text||kind==ObjectKind::Arrow||kind==ObjectKind::Path||kind==ObjectKind::Bracket||kind==ObjectKind::Table||kind==ObjectKind::Symbol||kind==ObjectKind::Orbital||kind==ObjectKind::BioShape;}
inline bool supportsWidth(ObjectKind kind){return supported(kind)&&kind!=ObjectKind::Atom&&kind!=ObjectKind::Bond&&kind!=ObjectKind::Text;}
inline std::string number(double value){std::ostringstream out;out.precision(17);out<<value;return out.str();}
inline Error apply(Document& doc,ObjectRef ref,double width,uint32_t color,bool setWidth,bool setColor){
    if(!supported(ref.kind))return Error();
    setWidth=setWidth&&supportsWidth(ref.kind);
    if(!setWidth&&!setColor)return Error();
    const auto& original=static_cast<const Document&>(doc).extensions(ref);
    Error e;
    if(ref.kind==ObjectKind::Arrow&&original.find("gfx.shape.params.1")){
        graphics::ShapeParams p;bool ok=false;e=graphics::readShape(doc,ref.id,&p,&ok);if(!e.ok())return e;
        if(setWidth)p.style.width=width;if(setColor)p.style.colorRGBA=color;
        e=graphics::writeShape(doc,ref.id,p,doc.defaultLineWidth());
    }else if(ref.kind==ObjectKind::Arrow&&original.find(arrows::kArrowParamsKey)){
        arrows::ArrowParams p;bool ok=false;e=arrows::readArrow(doc,ref.id,&p,&ok);if(!e.ok())return e;
        if(setWidth)p.strokeWidth=width/(p.scale>0?p.scale:1);
        e=arrows::writeArrow(doc,ref.id,p,doc.defaultLineWidth());
    }else if(ref.kind==ObjectKind::Bracket&&original.find("gfx.bracket.params.1")){
        graphics::BracketParams p;bool ok=false;e=graphics::readBracket(doc,ref.id,&p,&ok);if(!e.ok())return e;
        if(setWidth)p.style.width=width;if(setColor)p.style.colorRGBA=color;
        e=graphics::writeBracket(doc,ref.id,p,doc.defaultLineWidth());
    }else if(ref.kind==ObjectKind::Path){
        auto p=doc.paths().alive(ref.id);if(!p)return Error(ErrorCode::NotFound,"Path not found");
        auto style=p->style;if(setWidth)style.width=width;if(setColor)style.colorRGBA=color;e=doc.setLineStyle(ref,style);
    }else if(ref.kind==ObjectKind::Table&&setWidth){
        graphics::TableData table;bool ok=false;e=graphics::readTable(doc,ref.id,&table,&ok);if(e.ok()&&ok){table.grid.borderWidth=width;e=graphics::writeTable(doc,ref.id,table);}
    }
    if(!e.ok())return e;
    // Glyphs use their font's outlines; line width belongs to strokes, not text.
    auto& bag=doc.extensions(ref);if(setWidth&&ref.kind!=ObjectKind::Atom&&ref.kind!=ObjectKind::Text){if(width>0)bag.set("studio.graphics.width",number(width));else bag.erase("studio.graphics.width");}
    if(setColor)bag.set("studio.color.override",std::to_string(color));
    doc.noteTouchedExtensions(ref,false);return Error();
}
struct Counts {
    size_t arrows,paths,brackets,tables,symbols,atoms,bonds,texts,orbitals,bioShapes;
    explicit Counts(const Document& doc):arrows(doc.arrows().size()),paths(doc.paths().size()),brackets(doc.brackets().size()),tables(doc.tables().size()),symbols(doc.symbols().size()),atoms(doc.atoms().size()),bonds(doc.bonds().size()),texts(doc.texts().size()),orbitals(doc.orbitals().size()),bioShapes(doc.bioShapes().size()){}
    Error styleNew(Document& doc,double width,uint32_t color)const{
        auto applyRange=[&](ObjectKind kind,size_t begin,size_t end){for(size_t i=begin;i<end;++i){if(kind==ObjectKind::Text&&static_cast<const Document&>(doc).extensions(ObjectRef(kind,ObjectId(i))).findValue("studio.text.colors.edited"))continue;auto e=apply(doc,ObjectRef(kind,ObjectId(i)),width,color,width>0,true);if(!e.ok())return e;}return Error();};
        Error e=applyRange(ObjectKind::Arrow,arrows,doc.arrows().size());if(e.ok())e=applyRange(ObjectKind::Path,paths,doc.paths().size());if(e.ok())e=applyRange(ObjectKind::Bracket,brackets,doc.brackets().size());if(e.ok())e=applyRange(ObjectKind::Table,tables,doc.tables().size());if(e.ok())e=applyRange(ObjectKind::Symbol,symbols,doc.symbols().size());if(e.ok())e=applyRange(ObjectKind::Atom,atoms,doc.atoms().size());if(e.ok())e=applyRange(ObjectKind::Bond,bonds,doc.bonds().size());if(e.ok())e=applyRange(ObjectKind::Text,texts,doc.texts().size());if(e.ok())e=applyRange(ObjectKind::Orbital,orbitals,doc.orbitals().size());if(e.ok())e=applyRange(ObjectKind::BioShape,bioShapes,doc.bioShapes().size());return e;
    }
};
} }
