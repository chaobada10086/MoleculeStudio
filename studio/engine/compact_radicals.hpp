#pragma once
#include "compound_catalog.hpp"
#include "engine/atom_runs.hpp"
#include "next/text/rich_text.hpp"
#include <cstring>
namespace studio { namespace compact_radicals {
using namespace r62;using namespace core;
inline const compounddata::Hydride* find(const std::string& label){
    const auto* entry=compounddata::find(label);if(!entry)return nullptr;
    for(const auto& h:compounddata::hydrides)if(std::string(entry->name)==std::string(h.base)+"·")return &h;return nullptr;
}
inline std::vector<TextRun> residueRuns(const compounddata::Hydride& h,const std::vector<TextRun>& source){
    std::string plain;for(const auto& run:source)plain+=run.utf8;
    if(!source.empty()&&plain.compare(0,std::strlen(h.base),h.base)!=0){auto run=source.front();run.utf8=h.base;run.flags=kFormula;return {run};}
    std::vector<TextRun> out;size_t left=std::strlen(h.base);
    for(auto run:source){if(!left)break;run.utf8=run.utf8.substr(0,left);left-=run.utf8.size();if(!run.utf8.empty())out.push_back(run);}
    if(out.empty()){TextRun run;run.utf8=h.base;run.flags=kFormula;out.push_back(run);}return out;
}
inline Error apply(Document& doc,ObjectId id,const std::string& label,const std::vector<TextRun>& source={}){
    const auto* h=find(label);const auto* atom=doc.atoms().alive(id);if(!h||!atom)return Error(ErrorCode::InvalidArgument,"未知自由基缩写");
    auto sem=atom->semantics;sem.nodeType=uint8_t(NodeType::Nickname);sem.showLabel=true;sem.charge=0;sem.radical=uint8_t(Radical::Doublet);sem.isotope=0;sem.aromatic=false;sem.implicitHydrogens=0;
    auto e=doc.setElement(id,h->element);if(e.ok())e=doc.setAtomSemantics(id,sem);if(e.ok())e=doc.setAtomLabel(id,h->base);if(!e.ok())return e;
    auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,id));bag.erase("studio.compound.fragment");bag.erase("pst.attr.MoleculeStudioLiteralLabel");bag.erase("studio.hydrogens.manual");bag.erase("studio.query.description");bag.set("studio.nickname.fragment",h->fragment);bag.set("studio.mechanism.ion.base",h->base);bag.set("studio.mechanism.compact.valence","3");studioatom::set(doc,id,residueRuns(*h,source));return e;
}
inline Vec2 captionCenter(const Document& doc,const TextRecord& caption,text::GlyphCache& cache){
    text::LayoutOptions o;o.defaultSize=caption.size;o.defaultFont=caption.format.fontName.empty()?doc.defaultFont():caption.format.fontName;o.defaultFlags=caption.format.flags;o.triangulateGlyphs=false;
    text::RichLayout layout;if(!text::layoutRichText(doc.runs(caption.id),Vec2(),o,&cache,&layout).ok())return caption.position;
    Vec2 offset=caption.position-Vec2(0,layout.metrics.firstBaseline);if(caption.format.align==1)offset.x-=layout.metrics.width*.5;else if(caption.format.align==2)offset.x-=layout.metrics.width;
    Box ink;bool any=false;for(size_t i=0;i+1<layout.glyphs.size();++i)if(layout.glyphs[i].codepoint=='S'&&(layout.glyphs[i+1].codepoint=='n'||layout.glyphs[i+1].codepoint=='i')){for(size_t j=i;j<=i+1;++j)for(auto contour:layout.glyphs[j].contours)for(auto p:contour){p=p+offset;if(any)ink.add(p);else{ink=Box(p,p);any=true;}}break;}
    return any?(ink.min+ink.max)*.5:caption.position;
}
inline Error upgrade(Document& doc,text::GlyphCache& cache,bool& changed){
    std::vector<ObjectId> atoms,texts;for(const auto& a:doc.atoms().records())if(a.isAlive()&&find(doc.atomLabel(a.id)))atoms.push_back(a.id);
    for(auto id:atoms){auto e=apply(doc,id,doc.atomLabel(id),studioatom::runs(doc,id));if(!e.ok())return e;changed=true;}
    for(const auto& t:doc.texts().records())if(t.isAlive()&&find(doc.plainText(t.id)))texts.push_back(t.id);
    for(auto id:texts){auto label=doc.plainText(id);auto runs=doc.runs(id);auto position=captionCenter(doc,*doc.texts().alive(id),cache);ObjectId atom;auto e=doc.addAtom(position,6,&atom);if(e.ok())e=apply(doc,atom,label,runs);if(e.ok())e=doc.removeObject(ObjectRef(ObjectKind::Text,id));if(!e.ok())return e;changed=true;}return Error();
}
} }
