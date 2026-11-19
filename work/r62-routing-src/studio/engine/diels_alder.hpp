#pragma once
// Included with the endpoint inference helpers in r62::mechanism.
namespace cycloaddition {
inline bool pi(const BondRecord& b){
    return b.isAlive()&&b.semantics==BondSemantics::Normal&&
           (b.order==BondOrder::Double||b.order==BondOrder::Triple);
}
inline const BondRecord* sourceBond(const Document& d,const arrows::ArrowParams& p,double unit){
    const auto ref=p.electronSource;
    if(ref.kind==ObjectKind::Bond){
        const auto b=d.bonds().alive(ref.id);
        return b&&pi(*b)?b:nullptr;
    }
    if(ref.valid()&&ref.kind!=ObjectKind::Atom)return nullptr;
    if(ref.kind==ObjectKind::Atom){
        const auto atom=d.atoms().alive(ref.id);
        if(!atom||atom->element!=6||atom->semantics.charge||atom->semantics.radical||
           p.sourceVisualAnchor==2||p.sourceVisualAnchor==3||p.sourceVisualAnchor==4)return nullptr;
    }
    const BondRecord* unique=nullptr;
    for(const auto& b:d.bonds().records())if(pi(b)){
        if(ref.valid()&&ref.id!=b.beginAtom&&ref.id!=b.endAtom)continue;
        const auto a=d.atoms().alive(b.beginAtom)->position,z=d.atoms().alive(b.endAtom)->position;
        if(step_detail::pointSegment(p.begin,a,z)>unit*.42)continue;
        if(unique)return nullptr;
        unique=&b;
    }
    return unique;
}

// A tip can identify an atom, the receiving pi bond, or a point along the
// prospective new sigma bond. A tip in the middle of a receiving pi bond
// leaves both ends possible; the other arrows can still specify the pairing.
inline bool matchesTip(const Document& d,const arrows::ArrowParams& p,ObjectId from,ObjectId to,
                       const BondRecord& receiving,ObjectRef central,double unit){
    auto ref=p.electronTarget;
    const auto a=d.atoms().alive(from)->position,b=d.atoms().alive(to)->position;
    if(central.valid()&&ref==central)return true;
    if(ref.kind==ObjectKind::Atom)return ref.id==to;
    if(ref.valid()){
        if(ref!=ObjectRef(ObjectKind::Bond,receiving.id))return false;
        const auto other=d.atoms().alive(receiving.beginAtom==to?receiving.endAtom:receiving.beginAtom)->position;
        return length(p.end-b)<=length(p.end-other)+unit*.12;
    }
    if(length(p.end-b)<=unit*.32)return true;
    const auto axis=b-a;const double norm=dot(axis,axis);
    if(norm<1e-8)return false;
    const double t=dot(p.end-a,axis)/norm;
    return t>=.12&&t<=1.08&&step_detail::pointSegment(p.end,a,b)<=unit*.35;
}

inline bool resolve(const Document& d,const std::vector<arrows::ArrowParams>& arrows,
                    std::vector<ElectronMove>& solution,Error& error){
    if(arrows.size()!=3)return false;
    std::array<const BondRecord*,3> sources;
    std::array<double,3> units;
    std::set<ObjectId> atoms;
    for(int i=0;i<3;++i){
        const auto& p=arrows[i];
        if(!p.mechanism||p.head.sides!=arrows::HeadSides::Both||p.head.atBegin||!p.head.atEnd||
           p.shafts!=arrows::ShaftCount::One||p.electronTarget.kind==ObjectKind::Arrow)return false;
        units[i]=infer_detail::localUnit(d,p.begin);
        for(auto side:{false,true}){
            auto ref=side?p.electronTarget:p.electronSource;
            double score;
            if(ref.valid()&&!infer_detail::nearRef(d,ref,side?p.end:p.begin,Vec2(),units[i],&score))return false;
        }
        sources[i]=sourceBond(d,p,units[i]);
        if(!sources[i])return false;
        atoms.insert(sources[i]->beginAtom);atoms.insert(sources[i]->endAtom);
    }
    if(atoms.size()!=6)return false;
    std::set<step_detail::Edge> pairing;
    ObjectId chosenCenter=kInvalidObjectId;
    double chosenSpan=std::numeric_limits<double>::infinity();
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){
        if(i==j)continue;
        const int k=3-i-j;
        const auto& left=*sources[i];const auto& right=*sources[j];const auto& ene=*sources[k];
        if(left.order!=BondOrder::Double||right.order!=BondOrder::Double)continue;
        for(auto b:{left.beginAtom,left.endAtom})for(auto c:{right.beginAtom,right.endAtom}){
            const auto* center=d.bonds().alive(d.bondBetween(b,c));
            if(!center||center->order!=BondOrder::Single||center->semantics!=BondSemantics::Normal)continue;
            const ObjectRef central(ObjectKind::Bond,center->id);
            if(!matchesTip(d,arrows[i],b,c,right,central,units[i]))continue;
            const auto a=left.beginAtom==b?left.endAtom:left.beginAtom;
            const auto z=right.beginAtom==c?right.endAtom:right.beginAtom;
            for(auto e:{ene.beginAtom,ene.endAtom}){
                const auto f=ene.beginAtom==e?ene.endAtom:ene.beginAtom;
                if((arrows[j].electronSource.kind==ObjectKind::Atom&&arrows[j].electronSource.id!=z)||
                   (arrows[k].electronSource.kind==ObjectKind::Atom&&arrows[k].electronSource.id!=f))continue;
                if(d.bondBetween(z,e)!=kInvalidObjectId||d.bondBetween(f,a)!=kInvalidObjectId)continue;
                if(!matchesTip(d,arrows[j],z,e,ene,ObjectRef(),units[j])||
                   !matchesTip(d,arrows[k],f,a,left,ObjectRef(),units[k]))continue;
                const std::set<step_detail::Edge> ends={step_detail::edge(z,e),step_detail::edge(f,a)};
                const auto ep=d.atoms().alive(e),fp=d.atoms().alive(f);
                const double span=length(ep->position-d.atoms().alive(z)->position)+length(fp->position-d.atoms().alive(a)->position);
                if(!solution.empty()&&(pairing!=ends||chosenCenter!=center->id)){
                    const bool equivalent=chosenCenter==center->id&&d.incidentBonds(e).size()==1&&d.incidentBonds(f).size()==1&&
                        ep->element==fp->element&&ep->semantics==fp->semantics&&d.atomLabel(e)==d.atomLabel(f);
                    if(!equivalent){
                        error=Error(ErrorCode::InvalidSemantics,"DA 环加成有两组成键位置，请把一支成键箭头指向具体的末端原子。");
                        return true;
                    }
                    if(span>=chosenSpan)continue;
                }
                pairing=ends;chosenCenter=center->id;chosenSpan=span;solution.resize(3);
                solution[i]=ElectronMove(ObjectRef(ObjectKind::Bond,left.id),central,2);
                solution[j]=ElectronMove(ObjectRef(ObjectKind::Bond,right.id),ObjectRef(ObjectKind::Atom,e),2);
                solution[k]=ElectronMove(ObjectRef(ObjectKind::Bond,ene.id),ObjectRef(ObjectKind::Atom,a),2);
                solution[j].bondingEnd=z;solution[k].bondingEnd=f;
                for(int n=0;n<3;++n)solution[n].retainDrawnTarget=arrows[n].electronTarget!=solution[n].target;
            }
        }
    }
    return !solution.empty();
}
}
