#pragma once
// Independent orthographic structure tilt. The molecular graph and stereo
// descriptors are never changed; depth is drawing metadata, not a conformer.
namespace structure_perspective {
using namespace r62::core;
inline double depth(const Document& d,ObjectId id){auto v=d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.atom.z");if(!v)v=d.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("pst.attr.MoleculeStudioPerspectiveZ");return v?std::atof(v->c_str()):0;}
inline bool cyclic(const Document& d,const BondRecord& b){
    std::set<ObjectId> seen{b.beginAtom};std::vector<ObjectId> q{b.beginAtom};
    for(size_t i=0;i<q.size();++i)for(auto bid:d.incidentBonds(q[i]))if(bid!=b.id){auto e=d.bonds().alive(bid);auto n=e->beginAtom==q[i]?e->endAtom:e->beginAtom;if(n==b.endAtom)return true;if(seen.insert(n).second)q.push_back(n);}return false;
}
inline Error tilt(Document& d,const std::set<ObjectId>& ids,Vec2 pivot,double yaw,double pitch){
    double centerZ=0;for(auto id:ids)centerZ+=depth(d,id);centerZ/=std::max(size_t(1),ids.size());
    const double cx=std::cos(pitch),sx=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw);
    double lo=1e100,hi=-1e100;
    struct Sample{ObjectId id;Vec2 xy;double z;};std::vector<Sample> samples;
    for(auto id:ids){auto a=d.atoms().alive(id);if(!a)return Error(ErrorCode::NotFound,"透视目标已改变");Vec2 p=a->position-pivot;double z=depth(d,id)-centerZ;
        double y=p.y*cx-z*sx,z1=p.y*sx+z*cx,x=p.x*cy+z1*sy,z2=-p.x*sy+z1*cy+centerZ;
        samples.push_back({id,pivot+Vec2(x,y),z2});lo=std::min(lo,z2);hi=std::max(hi,z2);
    }
    // The ordinary 2D setter invalidates conformer depth. Stage every coordinate
    // first and restore drawing depth only after all XY updates are complete.
    for(auto id:ids)d.extensions(ObjectRef(ObjectKind::Atom,id)).erase("studio.atom.z");
    for(const auto& sample:samples){auto e=d.setAtomPosition(sample.id,sample.xy);if(!e.ok())return e;}
    for(const auto& sample:samples){std::ostringstream out;out.precision(17);out<<sample.z;auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,sample.id));bag.set("studio.atom.z",out.str());bag.erase("pst.attr.MoleculeStudioPerspectiveZ");bag.erase("pst.attr.xyz");}
    for(const auto& b:d.bonds().records())if(b.isAlive()&&ids.count(b.beginAtom)&&ids.count(b.endAtom)){
        auto& ext=d.extensions(ObjectRef(ObjectKind::Bond,b.id));ext.erase("pst.attr.MoleculeStudioPerspectiveFront");
        if((b.display==BondDisplay::Normal||b.display==BondDisplay::Bold)&&b.stereo==BondStereo::None&&cyclic(d,b)){
            bool front=hi-lo>d.defaultBondLength()*.03&&(depth(d,b.beginAtom)+depth(d,b.endAtom))*.5<lo+(hi-lo)*.38;
            d.setBondDisplay(b.id,front?BondDisplay::Bold:BondDisplay::Normal);
            if(front)ext.set("pst.attr.BoldWidth",std::to_string(d.defaultLineWidth()*2.22));
        }
    }
    return Error();
}
inline Command command(std::set<ObjectId> ids,Vec2 pivot,double yaw,double pitch,DocumentId doc,uint64_t rev){return Command("结构透视",kInvalidateGeometry|kInvalidateAppearance,[=](CommandContext& c){if(c.document.id()!=doc||c.document.revision()!=rev)return CommandResult::fail(ErrorCode::InvalidArgument,"结构已改变，请重新拖动");auto e=tilt(c.document,ids,pivot,yaw,pitch);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);});}
}
