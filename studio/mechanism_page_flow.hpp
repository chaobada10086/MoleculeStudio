#pragma once
namespace studio_mechanism_page_flow {
using namespace r62;using namespace core;

// Measure the ink once, including curved arrows, atom labels, charges and
// byproducts. An undrawn carbon vertex still contributes its actual position.
struct InkBounds {
    std::map<ObjectRef,Box> byObject;
    explicit InkBounds(const Document& doc){
        std::unique_ptr<integration::Session> view(new integration::Session);
        view->doc=doc;view->refresh();
        for(const auto& p:view->scene.primitives()){
            if(!p.source.valid()||p.bounds.empty())continue;
            auto entry=byObject.emplace(p.source,p.bounds);
            if(!entry.second){entry.first->second.add(p.bounds.min);entry.first->second.add(p.bounds.max);}
        }
        for(const auto& a:doc.atoms().records())if(a.isAlive())
            byObject.emplace(ObjectRef(ObjectKind::Atom,a.id),Box(a.position,a.position));
    }
    Box bounds(const std::vector<ObjectRef>& refs)const{
        Box result(Vec2(1,1),Vec2(-1,-1));
        for(auto ref:refs){
            auto it=byObject.find(ref);if(it==byObject.end())continue;
            if(result.empty())result=it->second;
            else{result.add(it->second.min);result.add(it->second.max);}
        }
        return result;
    }
};

inline Error place(Document& doc,const std::vector<ObjectRef>& made,const std::vector<ObjectRef>& existing,
                   const std::vector<ObjectRef>& participants,bool* wrapped){
    if(wrapped)*wrapped=false;
    InkBounds ink(doc);
    const Box block=ink.bounds(made),source=ink.bounds(participants);
    if(block.empty()||source.empty())return Error();
    auto page=studioio::Page::get(doc);
    const double margin=24, gap=std::max(4.,doc.defaultBondLength()*.65);
    const double rowGap=std::max(32.,doc.defaultBondLength()*1.5);
    std::vector<Box> occupied;
    for(auto ref:existing){
        auto it=ink.byObject.find(ref);
        if(it!=ink.byObject.end()&&ref.kind!=ObjectKind::Group)occupied.push_back(it->second);
    }
    // Take the first free interval beside this step, even when other schemes
    // lie farther right on the same row.
    double x=source.max.x+gap,y=block.min.y;
    std::sort(occupied.begin(),occupied.end(),[](const Box& a,const Box& b){return a.min.x<b.min.x;});
    for(const auto& box:occupied){
        if(box.min.x>=x+block.width()+gap)break;
        if(box.max.y+gap>y&&box.min.y<y+block.height()+gap)
            x=std::max(x,box.max.x+gap);
    }
    if(x+block.width()+margin>page.x+page.width*page.across){
        x=page.x+margin;y=std::max(page.y+margin,source.max.y+rowGap);
        std::sort(occupied.begin(),occupied.end(),[](const Box& a,const Box& b){return a.min.y<b.min.y;});
        for(const auto& box:occupied){
            if(box.min.y>=y+block.height()+gap)break;
            if(box.min.x<x+block.width()+gap&&box.max.x+gap>x&&box.max.y+gap>y)
                y=std::max(y,box.max.y+rowGap);
        }
        if(wrapped)*wrapped=std::abs(x-block.min.x)>1e-7||std::abs(y-block.min.y)>1e-7;
    }
    const Vec2 delta(x-block.min.x,y-block.min.y);
    if(length(delta)>1e-7){
        auto e=integration::translateDocumentObjects(doc,made,delta);if(!e.ok())return e;
    }
    page.across=std::max(page.across,int(std::ceil((x+block.width()+margin-page.x)/page.width)));
    page.down=std::max(page.down,int(std::ceil((y+block.height()+margin-page.y)/page.height)));
    if(!page.valid())return Error(ErrorCode::InvalidGeometry,"页面空间不足，请整理画布后再生成。");
    page.store(doc);return Error();
}
}
