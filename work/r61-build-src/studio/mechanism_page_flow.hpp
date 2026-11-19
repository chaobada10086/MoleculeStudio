#pragma once
namespace studio_mechanism_page_flow {
using namespace r62;using namespace core;
inline Error place(Document& doc,const std::vector<ObjectRef>& made,const std::vector<ObjectRef>& existing,const Box& sourceRow,bool* wrapped){
    if(wrapped)*wrapped=false;auto page=studioio::Page::get(doc);auto bounds=selection::selectionBounds(doc,made);if(bounds.empty())return Error();
    const double margin=24,gap=std::max(32.,doc.defaultBondLength()*1.5);
    if(bounds.max.x+margin>page.x+page.width*page.across){
        Vec2 delta(page.x+margin-bounds.min.x,std::max(page.y+margin,sourceRow.empty()?bounds.min.y:sourceRow.max.y+gap)-bounds.min.y);
        auto candidate=bounds;candidate.min=candidate.min+delta;candidate.max=candidate.max+delta;
        std::vector<Box> occupied;for(auto ref:existing)if(ref.kind!=ObjectKind::Group){auto box=selection::selectionBounds(doc,{ref});if(!box.empty())occupied.push_back(box);}
        for(size_t pass=0;pass<=occupied.size();++pass){double next=candidate.min.y;
            for(const auto& box:occupied)if(candidate.min.x<box.max.x+8&&candidate.max.x>box.min.x-8&&candidate.min.y<box.max.y+8&&candidate.max.y>box.min.y-8)next=std::max(next,box.max.y+gap);
            if(next<=candidate.min.y+1e-8)break;double shift=next-candidate.min.y;candidate.min.y+=shift;candidate.max.y+=shift;delta.y+=shift;
        }
        auto e=integration::translateDocumentObjects(doc,made,delta);if(!e.ok())return e;bounds=selection::selectionBounds(doc,made);if(wrapped)*wrapped=true;
    }
    // Width is preserved for blocks that fit a row. Only intrinsically wider
    // blocks need additional columns; normal continuation grows downwards.
    page.across=std::max(page.across,int(std::ceil((bounds.max.x+margin-page.x)/page.width)));
    page.down=std::max(page.down,int(std::ceil((bounds.max.y+margin-page.y)/page.height)));
    if(!page.valid())return Error(ErrorCode::InvalidGeometry,"页面空间不足，请整理画布后再生成。");page.store(doc);return Error();
}
}
