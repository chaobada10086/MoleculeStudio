#pragma once
namespace studio_mechanism_page_flow {
using namespace r62;using namespace core;

inline bool onArrowSide(const Document& doc,ObjectRef ref){
    return ref.kind==ObjectKind::Arrow||doc.extensions(ref).findValue(studio_mechanism_products::arrowSideKey);
}

inline Error alignStep(Document& doc,const Document& source,const std::vector<ObjectRef>& made){
    std::vector<ObjectRef> structures,arrowSide;
    std::vector<double> shifts;
    ObjectId connector=kInvalidObjectId;
    Box skeleton(Vec2(1,1),Vec2(-1,-1));
    for(auto ref:made){
        if(onArrowSide(doc,ref)){
            arrowSide.push_back(ref);
            if(ref.kind==ObjectKind::Arrow)connector=ref.id;
            continue;
        }
        structures.push_back(ref);
        if(ref.kind!=ObjectKind::Atom)continue;
        const auto* product=doc.atoms().alive(ref.id);
        if(!product||product->element==1)continue;
        if(skeleton.empty())skeleton=Box(product->position,product->position);
        else skeleton.add(product->position);
        const auto* original=source.atoms().alive(studio_mechanism_products::sourceAtom(doc,ref.id));
        if(original&&product->element==original->element)
            shifts.push_back(original->position.y-product->position.y);
    }
    double shift=0;
    if(!shifts.empty()){
        // The median follows the retained skeleton even when a local branch was redrawn.
        std::sort(shifts.begin(),shifts.end());
        shift=(shifts[(shifts.size()-1)/2]+shifts[shifts.size()/2])*.5;
        if(std::abs(shift)<=1e-7)shift=0;
        else{
            auto e=integration::translateDocumentObjects(doc,structures,Vec2(0,shift));
            if(!e.ok())return e;
        }
    }
    if(skeleton.empty()||connector==kInvalidObjectId)return Error();
    arrows::ArrowParams arrow;bool valid=false;
    auto e=arrows::readArrow(doc,connector,&arrow,&valid);
    if(!e.ok()||!valid)return e;
    // The aligned product sets the row center before any page wrap.
    const double centerY=(skeleton.min.y+skeleton.max.y)*.5+shift;
    const double arrowShift=centerY-(arrow.begin.y+arrow.end.y)*.5;
    if(std::abs(arrowShift)<=1e-7)return Error();
    return integration::translateDocumentObjects(doc,arrowSide,Vec2(0,arrowShift));
}

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
    // Find horizontal space without moving anything on the original row.
    auto availableX=[&](const Box& bounds){
        double x=source.max.x+gap;
        for(const auto& box:occupied){
            if(box.min.x>=x+bounds.width()+gap)break;
            if(box.max.y+gap>bounds.min.y&&box.min.y<bounds.max.y+gap)
                x=std::max(x,box.max.x+gap);
        }
        return x;
    };
    std::sort(occupied.begin(),occupied.end(),[](const Box& a,const Box& b){return a.min.x<b.min.x;});
    double x=availableX(block),y=block.min.y;
    double rightEdge=x+block.width(),bottomEdge=y+block.height();
    if(rightEdge+margin<=page.x+page.width*page.across){
        const Vec2 delta(x-block.min.x,0);
        if(length(delta)>1e-7){
            auto e=integration::translateDocumentObjects(doc,made,delta);if(!e.ok())return e;
        }
    }else{
        std::vector<ObjectRef> arrowSide,structures;
        for(auto ref:made){
            if(onArrowSide(doc,ref))arrowSide.push_back(ref);
            else structures.push_back(ref);
        }
        auto arrowBounds=ink.bounds(arrowSide);
        auto structureBounds=ink.bounds(structures);
        rightEdge=page.x;bottomEdge=page.y;
        if(arrowBounds.empty()||structureBounds.empty()){
            structures=made;structureBounds=block;
        }else{
            const Vec2 delta(availableX(arrowBounds)-arrowBounds.min.x,0);
            if(length(delta)>1e-7){
                auto e=integration::translateDocumentObjects(doc,arrowSide,delta);if(!e.ok())return e;
            }
            arrowBounds.min=arrowBounds.min+delta;arrowBounds.max=arrowBounds.max+delta;
            occupied.push_back(arrowBounds);
            rightEdge=arrowBounds.max.x;bottomEdge=arrowBounds.max.y;
        }
        x=page.x+margin;y=std::max(page.y+margin,source.max.y+rowGap);
        std::sort(occupied.begin(),occupied.end(),[](const Box& a,const Box& b){return a.min.y<b.min.y;});
        for(const auto& box:occupied){
            if(box.min.y>=y+structureBounds.height()+gap)break;
            if(box.min.x<x+structureBounds.width()+gap&&box.max.x+gap>x&&box.max.y+gap>y)
                y=std::max(y,box.max.y+rowGap);
        }
        const Vec2 delta(x-structureBounds.min.x,y-structureBounds.min.y);
        if(length(delta)>1e-7){
            auto e=integration::translateDocumentObjects(doc,structures,delta);if(!e.ok())return e;
            if(wrapped)*wrapped=true;
        }
        rightEdge=std::max(rightEdge,x+structureBounds.width());
        bottomEdge=std::max(bottomEdge,y+structureBounds.height());
    }
    page.across=std::max(page.across,int(std::ceil((rightEdge+margin-page.x)/page.width)));
    page.down=std::max(page.down,int(std::ceil((bottomEdge+margin-page.y)/page.height)));
    if(!page.valid())return Error(ErrorCode::InvalidGeometry,"页面空间不足，请整理画布后再生成。");
    page.store(doc);return Error();
}

}
