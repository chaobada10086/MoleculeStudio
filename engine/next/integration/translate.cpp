#include "session.hpp"
#include "next/selection/transform.hpp"
#include <functional>
#include <set>
namespace r62 { namespace integration {
Error translateDocumentObjects(Document& d,const std::vector<ObjectRef>& refs,Vec2 delta){
    std::set<ObjectRef> unique(refs.begin(),refs.end());
    // A group has no geometry of its own: moving it means moving every leaf
    // member. Nested groups are flattened, and the visited set breaks cycles.
    std::set<ObjectRef> expanded;std::set<ObjectId> visitedGroups;
    std::function<void(const ObjectRef&)> collect=[&](const ObjectRef& r){
        if(r.kind!=ObjectKind::Group){expanded.insert(r);return;}
        if(!visitedGroups.insert(r.id).second)return;
        const GroupRecord* g=d.groups().alive(r.id);if(!g)return;
        for(const auto& member:g->members)if(member.valid())collect(member);
    };
    for(const auto& r:unique)collect(r);
    std::vector<ObjectRef> base;
    for(auto r:expanded){
        Error e;bool ok=false;
        switch(r.kind){
        case ObjectKind::Atom:case ObjectKind::Bond:case ObjectKind::Text:case ObjectKind::Path:base.push_back(r);break;
        case ObjectKind::Arrow:
            if(static_cast<const Document&>(d).extensions(r).find("gfx.shape.params.1"))e=graphics::translateShape(d,r.id,delta);
            else if(static_cast<const Document&>(d).extensions(r).find("so.push.params.1")){symbols::PushParams p;e=symbols::readPush(d,r.id,&p,&ok);if(e.ok()){p.begin=p.begin+delta;p.end=p.end+delta;e=symbols::writePush(d,r.id,p,d.defaultLineWidth());}}
            else {arrows::ArrowParams p;e=arrows::readArrow(d,r.id,&p,&ok);if(e.ok()&&ok){p.begin=p.begin+delta;p.end=p.end+delta;e=arrows::writeArrow(d,r.id,p,d.defaultLineWidth());}else return Error(ErrorCode::NotImplemented,"Unrecognized arrow");}break;
        case ObjectKind::Bracket:e=graphics::translateBracket(d,r.id,delta);break;
        case ObjectKind::BioShape:{biodraw::BioShapeParams p;e=biodraw::readBioShape(d,r.id,&p,&ok);if(e.ok()){p.centre=p.centre+delta;e=biodraw::writeBioShape(d,r.id,p,d.defaultLineWidth());}break;}
        case ObjectKind::Orbital:{symbols::OrbitalParams p;e=symbols::readOrbital(d,r.id,&p,&ok);if(e.ok()){p.position=p.position+delta;e=symbols::writeOrbital(d,r.id,p,d.defaultLineWidth());}break;}
        case ObjectKind::Symbol:
            if(experimental::isFigureObject(d,r)){experimental::PlateFrame f;e=experimental::readFrame(d,r.id,&f);if(e.ok()){f.centre=f.centre+delta;e=experimental::writeFrame(d,r.id,f);}}
            else if(static_cast<const Document&>(d).extensions(r).find("so.push.params.1")){symbols::PushParams p;e=symbols::readPush(d,r.id,&p,&ok);if(e.ok()){p.begin=p.begin+delta;p.end=p.end+delta;e=symbols::writePush(d,r.id,p,d.defaultLineWidth());}}
            else {symbols::SymbolParams p;e=symbols::readSymbol(d,r.id,&p,&ok);if(e.ok()){p.position=p.position+delta;e=symbols::writeSymbol(d,r.id,p,d.defaultLineWidth());}}break;
        case ObjectKind::Table:{graphics::TableData t;e=graphics::readTable(d,r.id,&t,&ok);if(e.ok()){t.grid.frame.min=t.grid.frame.min+delta;t.grid.frame.max=t.grid.frame.max+delta;e=graphics::writeTable(d,r.id,t);}break;}
        default:return Error(ErrorCode::NotImplemented,"Unrecognized object: move refused atomically");
        }
        if(!e.ok())return e;
    }
    selection::TransformReport rep=selection::translateObjects(d,base,delta);
    if(rep.refsSkippedUnsupported)return Error(ErrorCode::NotImplemented,"Unsupported transform");
    return Error();
}
} }
