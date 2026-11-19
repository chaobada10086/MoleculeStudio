#pragma once
#include "next/core/document.hpp"
#include "next/arrows_curves/model.hpp"
#include "mechanism_roles.hpp"
#include "mechanism_electrons.hpp"
#include "virtual_bonds.hpp"
#include "fragment_layout_mass.hpp"
#include "projection_reaction.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <limits>
#include <set>
#include <vector>

namespace r62 { namespace mechanism {
using namespace core;
struct ElectronMove {
    ObjectRef source,target;
    int electronCount;
    int sourceLonePair=-1; // visual index of the donated pair, when known
    bool sharedTarget; // a new bond drawn as two hooks meeting in blank space
    bool retainDrawnTarget=false;
    ObjectId bondingEnd=kInvalidObjectId; // identified terminal of a concerted pi donation
    ElectronMove():electronCount(2),sharedTarget(false){}
    ElectronMove(ObjectRef s,ObjectRef t,int count=2,bool shared=false):source(s),target(t),electronCount(count),sharedTarget(shared){}
};
namespace step_detail {
typedef std::pair<ObjectId,ObjectId> Edge;
inline Edge edge(ObjectId a,ObjectId b){return a<b?Edge(a,b):Edge(b,a);}
inline Error fail(const std::string& s){return Error(ErrorCode::InvalidArgument,s);}
// Deliberately restricted closed-shell states. Unknown valence is not permission
// to invent hydrogens or accept a guessed reaction.
inline int valence(int z,int q){
    if(z==1)return q==0?1:(q==1||q==-1?0:-1);
    if(z==6)return q==0?4:(q==1||q==-1?3:-1);
    if(z==7)return q>=-1&&q<=1?3+q:-1;
    if(z==8)return q>=-1&&q<=1?2+q:-1;
    if(z==15)return q==0?3:(q==1?4:-1);
    if(z==5||z==13)return q==0?3:q==-1?4:-1;
    if(z==14||z==50)return q==0?4:-1;
    if(z==16||z==34||z==52)return q==0?2:q==1?3:q==-1?1:-1;
    if(z==9||z==17||z==35||z==53)return q==0?1:(q==-1?0:-1);
    return -1;
}
struct Change {std::map<Edge,int> bonds;std::map<ObjectId,int> charge,radicals;std::map<Edge,int> halfBonds,piUses;};
inline ObjectId substitutionCarbon(const Document& doc,const ElectronMove& departure){
    if(departure.electronCount!=2||departure.source.kind!=ObjectKind::Bond||departure.target.kind!=ObjectKind::Atom)
        return kInvalidObjectId;
    const auto* bond=doc.bonds().alive(departure.source.id);
    if(!bond||bond->order!=BondOrder::Single||bond->semantics!=BondSemantics::Normal)
        return kInvalidObjectId;
    const auto leaving=departure.target.id;
    if(leaving!=bond->beginAtom&&leaving!=bond->endAtom)return kInvalidObjectId;
    const auto center=leaving==bond->beginAtom?bond->endAtom:bond->beginAtom;
    const auto* atom=doc.atoms().alive(center);
    if(!atom||atom->element!=6||atom->semantics.charge||atom->semantics.radical||atom->semantics.aromatic||
       atom->semantics.nodeType!=uint8_t(NodeType::Element))return kInvalidObjectId;
    for(auto id:doc.incidentBonds(center)){
        const auto* incident=doc.bonds().alive(id);
        if(!incident||incident->order!=BondOrder::Single||incident->semantics!=BondSemantics::Normal)
            return kInvalidObjectId;
    }
    return center;
}
inline ObjectId ringMigrationEndpoint(const Document& doc,const ElectronMove& shift,const std::vector<ElectronMove>& moves){
    if(shift.electronCount!=2||shift.source.kind!=ObjectKind::Bond||shift.target.kind!=ObjectKind::Atom)
        return kInvalidObjectId;
    const auto* sigma=doc.bonds().alive(shift.source.id);
    const auto target=shift.target.id;
    auto isCarbon=[&](ObjectId id){const auto* atom=doc.atoms().alive(id);return atom&&atom->element==6;};
    if(!sigma||sigma->order!=BondOrder::Single||sigma->semantics!=BondSemantics::Normal||
       target==sigma->beginAtom||target==sigma->endAtom||
       !isCarbon(target)||!isCarbon(sigma->beginAtom)||!isCarbon(sigma->endAtom))return kInvalidObjectId;
    const bool adjacentBegin=doc.bondBetween(target,sigma->beginAtom)!=kInvalidObjectId;
    const bool adjacentEnd=doc.bondBetween(target,sigma->endAtom)!=kInvalidObjectId;
    if(adjacentBegin==adjacentEnd)return kInvalidObjectId;
    const auto stationary=adjacentBegin?sigma->beginAtom:sigma->endAtom;
    const auto migrating=adjacentBegin?sigma->endAtom:sigma->beginAtom;
    const auto& sem=doc.atoms().alive(migrating)->semantics;
    if(sem.charge||sem.radical||sem.aromatic||sem.nodeType!=uint8_t(NodeType::Element))return kInvalidObjectId;
    const bool losesPi=std::any_of(moves.begin(),moves.end(),[&](const ElectronMove& move){
        if(move.electronCount!=2||move.source.kind!=ObjectKind::Bond||move.target.kind!=ObjectKind::Bond)return false;
        const auto* pi=doc.bonds().alive(move.source.id);
        const auto* next=doc.bonds().alive(move.target.id);
        if(!pi||!next||pi->order!=BondOrder::Double||next->order!=BondOrder::Single||
           pi->semantics!=BondSemantics::Normal||next->semantics!=BondSemantics::Normal)return false;
        const auto other=pi->beginAtom==target?pi->endAtom:pi->endAtom==target?pi->beginAtom:kInvalidObjectId;
        return other!=kInvalidObjectId&&(next->beginAtom==other||next->endAtom==other)&&
               next->beginAtom!=target&&next->endAtom!=target;
    });
    const bool losesLeavingGroup=std::any_of(moves.begin(),moves.end(),[&](const ElectronMove& move){
        return move.target.id!=stationary&&move.target.id!=migrating&&substitutionCarbon(doc,move)==target;
    });
    const auto* acceptor=doc.atoms().alive(target);
    const bool carbeneMigration=acceptor->semantics.radical==uint8_t(Radical::Singlet)&&!acceptor->semantics.charge&&
        std::any_of(moves.begin(),moves.end(),[&](const ElectronMove& move){
            if(move.electronCount!=2||move.source!=ObjectRef(ObjectKind::Atom,target)||move.target.kind!=ObjectKind::Bond)return false;
            auto receiving=doc.bonds().alive(move.target.id);if(!receiving||receiving->order!=BondOrder::Single||edge(receiving->beginAtom,receiving->endAtom)!=edge(target,stationary))return false;
            for(auto id:doc.incidentBonds(stationary)){auto b=doc.bonds().alive(id);auto other=b->beginAtom==stationary?b->endAtom:b->beginAtom;if(b->order==BondOrder::Double&&doc.atoms().alive(other)->element==8)return true;}return false;
        });
    if(!losesPi&&!losesLeavingGroup&&!carbeneMigration&&acceptor->semantics.charge!=1)return kInvalidObjectId;
    // Find an alternative route between the sigma endpoints to confirm that
    // this is ring migration. Stop at the reaction solver's component limit.
    std::vector<ObjectId> queue{stationary};
    std::set<ObjectId> seen{stationary};
    for(size_t i=0;i<queue.size()&&queue.size()<=500;++i)
        for(auto id:doc.incidentBonds(queue[i])){
            if(id==sigma->id)continue;
            const auto& b=*doc.bonds().alive(id);
            const auto other=b.beginAtom==queue[i]?b.endAtom:b.beginAtom;
            if(other==migrating)return migrating;
            if(seen.insert(other).second)queue.push_back(other);
        }
    return kInvalidObjectId;
}
inline int wedgeDepth(const BondRecord& bond){
    if(bond.stereo==BondStereo::Either||bond.display==BondDisplay::Wavy)return 0;
    if(bond.display==BondDisplay::Wedge||bond.display==BondDisplay::HollowWedge)
        return bond.stereo==BondStereo::Down?0:1;
    if(bond.display==BondDisplay::WedgeHash||bond.display==BondDisplay::Hash)
        return bond.stereo==BondStereo::Up?0:-1;
    return bond.stereo==BondStereo::Up?1:bond.stereo==BondStereo::Down?-1:0;
}
inline bool hasStereoMark(const BondRecord& bond){
    return bond.stereo!=BondStereo::None||bond.display==BondDisplay::Wedge||
           bond.display==BondDisplay::HollowWedge||bond.display==BondDisplay::Hash||
           bond.display==BondDisplay::WedgeHash||bond.display==BondDisplay::Wavy;
}
struct Substitution {
    ObjectId nucleophile,leaving;
};
struct Inversion {
    std::array<ObjectId,4> ligands{};
    std::array<int,4> depth{};
    double originalVolume=0;
};
// Ligands are kept in the same order, with the entering group taking the
// leaving group's slot. The sign is independent of the CIP priority order.
inline double tetrahedralVolume(const std::array<Vec2,4>& points,const std::array<int,4>& depth){
    const auto a=points[0]-points[3],b=points[1]-points[3],c=points[2]-points[3];
    return (depth[0]-depth[3])*(b.x*c.y-b.y*c.x)
         - (depth[1]-depth[3])*(a.x*c.y-a.y*c.x)
         + (depth[2]-depth[3])*(a.x*b.y-a.y*b.x);
}
inline void transfer(Change& c,ObjectId donor,ObjectId acceptor){++c.charge[donor];--c.charge[acceptor];}
inline Vec2 rotate(Vec2 p,double a){return Vec2(p.x*std::cos(a)-p.y*std::sin(a),p.x*std::sin(a)+p.y*std::cos(a));}
inline double pointSegment(Vec2 p,Vec2 a,Vec2 b){Vec2 d=b-a;double n=d.x*d.x+d.y*d.y,t=n>1e-12?((p.x-a.x)*d.x+(p.y-a.y)*d.y)/n:0;return length(p-(a+d*std::max(0.,std::min(1.,t))));}
inline double cross(Vec2 a,Vec2 b){return a.x*b.y-a.y*b.x;}
inline bool segmentsCross(Vec2 a,Vec2 b,Vec2 c,Vec2 d){
    const double eps=1e-7;
    if(std::max(a.x,b.x)+eps<std::min(c.x,d.x)||std::max(c.x,d.x)+eps<std::min(a.x,b.x)||std::max(a.y,b.y)+eps<std::min(c.y,d.y)||std::max(c.y,d.y)+eps<std::min(a.y,b.y))return false;
    double u=cross(b-a,c-a),v=cross(b-a,d-a),w=cross(d-c,a-c),z=cross(d-c,b-c);
    if(((u>eps&&v<-eps)||(u<-eps&&v>eps))&&((w>eps&&z<-eps)||(w<-eps&&z>eps)))return true;
    return pointSegment(a,c,d)<eps||pointSegment(b,c,d)<eps||pointSegment(c,a,b)<eps||pointSegment(d,a,b)<eps;
}
// A bounded geometric preflight: nonincident atoms must stay off bonds, and
// nonincident bonds cannot intersect. Fixed pre-existing crossings are rejected
// too: copying a tangled source is not a successful product layout.
inline double collisionCost(const std::map<ObjectId,Vec2>& p,const std::vector<Edge>& bonds,double unit,size_t& budget){
    double score=0;
    for(auto a=p.begin();a!=p.end();++a)for(auto b=std::next(a);b!=p.end();++b){if(!budget)return -1;--budget;double d=length(a->second-b->second)/unit;if(d<.3)score+=10000*(.3-d)+1;}
    for(const auto& a:p)for(auto e:bonds){if(!budget)return -1;--budget;if(a.first==e.first||a.first==e.second)continue;double d=pointSegment(a.second,p.at(e.first),p.at(e.second))/unit;if(d<.22)score+=10000*(.22-d)+1;}
    for(size_t i=0;i<bonds.size();++i)for(size_t j=0;j<i;++j){if(!budget)return -1;--budget;auto a=bonds[i],b=bonds[j];if(a.first==b.first||a.first==b.second||a.second==b.first||a.second==b.second)continue;if(segmentsCross(p.at(a.first),p.at(a.second),p.at(b.first),p.at(b.second)))score+=10000;}
    return score;
}
// Contract a ring around an unchanged edge. Multi-attachment outer components
// (the rest of a fused system) remain fixed, including their attachment atoms.
inline bool placeAnchoredContraction(const std::vector<ObjectId>& ring,ObjectId acceptor,ObjectId migrating,
                                     const std::vector<Edge>& bonds,const std::map<Edge,int>& orders,double unit,size_t& budget,std::map<ObjectId,Vec2>& positions){
    std::map<ObjectId,std::vector<ObjectId>> neighbors;for(auto b:bonds){neighbors[b.first].push_back(b.second);neighbors[b.second].push_back(b.first);}
    const std::set<ObjectId> cycle(ring.begin(),ring.end());std::set<ObjectId> visited,fixed;
    struct Branch {std::vector<ObjectId> atoms;std::set<ObjectId> anchors;};std::vector<Branch> branches;
    for(auto id:ring)for(auto next:neighbors[id])if(!cycle.count(next)&&visited.insert(next).second){
        Branch branch;branch.atoms.push_back(next);
        for(size_t i=0;i<branch.atoms.size();++i)for(auto other:neighbors[branch.atoms[i]]){
            if(cycle.count(other))branch.anchors.insert(other);else if(visited.insert(other).second)branch.atoms.push_back(other);
        }
        if(branch.anchors.size()>1)fixed.insert(branch.anchors.begin(),branch.anchors.end());branches.push_back(branch);
    }
    size_t start=ring.size();
    for(size_t i=0;i<ring.size();++i)if(fixed.count(ring[i])&&fixed.count(ring[(i+1)%ring.size()])){start=i;break;}
    if(start==ring.size())for(size_t i=0;i<ring.size();++i){auto a=ring[i],b=ring[(i+1)%ring.size()];if(a!=acceptor&&b!=acceptor&&a!=migrating&&b!=migrating){start=i;break;}}
    if(start==ring.size())return false;
    const auto a=ring[start],b=ring[(start+1)%ring.size()];fixed.insert(a);fixed.insert(b);
    const Vec2 first=positions.at(a),edgeVector=positions.at(b)-first;const double L=length(edgeVector);if(L<1e-8)return false;
    double best=std::numeric_limits<double>::infinity();std::map<ObjectId,Vec2> result;
    for(double sense:{-1.,1.}){
        auto trial=positions;auto cursor=positions.at(b);trial[a]=first;trial[b]=cursor;Vec2 side=edgeVector;
        for(size_t i=2;i<ring.size();++i){side=rotate(side,sense*6.283185307179586/ring.size());cursor=cursor+side;trial[ring[(start+i)%ring.size()]]=cursor;}
        bool fits=true;for(auto id:fixed)if(length(trial.at(id)-positions.at(id))>unit*.02)fits=false;if(!fits)continue;
        Vec2 center;for(auto id:ring)center=center+trial.at(id);center=center*(1./ring.size());
        for(const auto& branch:branches)if(branch.anchors.size()==1){auto root=*branch.anchors.begin();auto shift=trial.at(root)-positions.at(root);
            for(auto id:branch.atoms)trial[id]=positions.at(id)+shift;
            // The expelled two-atom ketene arm points away from the new ring.
            if(root==acceptor&&branch.atoms.size()==2){auto d=branch.atoms[0],o=branch.atoms[1];
                if(neighbors[d].size()==2&&neighbors[o].size()==1&&orders.at(edge(root,d))==2&&orders.at(edge(d,o))==2){auto out=trial.at(root)-center;if(length(out)>1e-8){out=out*(L/length(out));trial[d]=trial.at(root)+out;trial[o]=trial[d]+out;}}
            }
        }
        const auto collisions=collisionCost(trial,bonds,unit,budget);if(collisions<0)return false;if(collisions>0)continue;
        double cost=0;for(auto id:ring)cost+=lengthSq(trial.at(id)-positions.at(id));if(cost<best){best=cost;result=std::move(trial);}
    }
    if(result.empty())return false;positions=std::move(result);return true;
}
// Place the new cycle of an adjacent bond migration while anchoring the
// acceptor and preserving every ring or branch attached only to that atom.
inline bool placeMigratedRing(ObjectId acceptor,ObjectId donor,ObjectId migrating,
                              const std::vector<Edge>& bonds,const std::map<Edge,int>& orders,double unit,size_t& budget,
                              std::map<ObjectId,Vec2>& positions){
    std::map<ObjectId,std::vector<ObjectId>> neighbors;
    for(auto bond:bonds){neighbors[bond.first].push_back(bond.second);neighbors[bond.second].push_back(bond.first);}
    // The new bond is already in bonds. Find the rest of the ring through
    // the donor for expansion, or directly from the acceptor for contraction.
    auto retainedPath=[&](ObjectId start,ObjectId excluded){
        std::vector<ObjectId> queue{start},path;
        std::map<ObjectId,ObjectId> parent{{start,start}};
        for(size_t i=0;i<queue.size()&&!parent.count(migrating);++i)
            for(auto other:neighbors.at(queue[i]))
                if(other!=excluded&&edge(queue[i],other)!=edge(acceptor,migrating)&&
                   parent.emplace(other,queue[i]).second)queue.push_back(other);
        if(parent.count(migrating)){
            for(auto id=migrating;id!=start;id=parent.at(id))path.push_back(id);
            path.push_back(start);
            std::reverse(path.begin(),path.end());
        }
        return path;
    };
    auto ring=retainedPath(donor,acceptor);
    const bool expansion=!ring.empty();
    if(expansion)ring.insert(ring.begin(),acceptor);
    else ring=retainedPath(acceptor,donor);
    if(ring.size()<4||ring.size()>12)return false;
    if(!expansion&&placeAnchoredContraction(ring,acceptor,migrating,bonds,orders,unit,budget,positions))return true;
    std::set<ObjectId> cycle(ring.begin(),ring.end());
    bool keepSharedEdge=false;
    if(expansion){
        std::vector<ObjectId> queue{acceptor};
        auto seen=cycle;seen.erase(donor);
        for(size_t i=0;i<queue.size()&&!keepSharedEdge;++i)
            for(auto other:neighbors.at(queue[i])){
                if(edge(queue[i],other)==edge(acceptor,donor))continue;
                if(other==donor){keepSharedEdge=true;break;}
                if(seen.insert(other).second)queue.push_back(other);
            }
    }
    std::map<ObjectId,std::vector<ObjectId>> branches;
    std::set<ObjectId> assigned;
    Vec2 oldCenter;
    for(size_t i=0;i<ring.size();++i){
        const auto id=ring[i],prev=ring[(i+ring.size()-1)%ring.size()],next=ring[(i+1)%ring.size()];
        if(i>0)oldCenter=oldCenter+positions.at(id);
        for(auto other:neighbors[id]){
            if(other==prev||other==next)continue;
            if(cycle.count(other))return false;
            if(i==0||(keepSharedEdge&&id==donor))continue;
            auto& branch=branches[id];
            if(!assigned.insert(other).second)return false;
            const size_t begin=branch.size();
            branch.push_back(other);
            for(size_t k=begin;k<branch.size();++k)for(auto connected:neighbors[branch[k]]){
                if(connected==id)continue;
                if(cycle.count(connected))return false;
                if(assigned.insert(connected).second)branch.push_back(connected);
            }
        }
    }
    oldCenter=oldCenter*(1./(ring.size()-1));
    const auto anchor=positions.at(acceptor);
    const auto heading=positions.at(donor)-anchor;
    const double headingAngle=std::atan2(heading.y,heading.x);
    const double bondLength=keepSharedEdge?length(heading):unit;
    constexpr double pi=3.14159265358979323846;
    double best=std::numeric_limits<double>::infinity();
    std::map<ObjectId,Vec2> result;
    for(double sense:{1.,-1.})for(int direction=0;direction<(keepSharedEdge?1:24);++direction){
        auto trial=positions;
        Vec2 cursor=anchor,center=anchor;
        const double angle=headingAngle+direction*pi/12.;
        for(size_t i=1;i<ring.size();++i){
            const double a=angle+sense*(i-1)*2*pi/ring.size();
            cursor=keepSharedEdge&&i==1?positions.at(donor):
                   cursor+Vec2(std::cos(a),std::sin(a))*bondLength;
            trial[ring[i]]=cursor;center=center+cursor;
        }
        center=center*(1./ring.size());
        for(const auto& branch:branches){
            const auto id=branch.first;
            const auto before=positions.at(id)-oldCenter,after=trial.at(id)-center;
            const auto turn=std::atan2(cross(before,after),dot(before,after));
            for(auto other:branch.second){
                auto offset=positions.at(other)-positions.at(id);
                if(branch.second.size()==1)offset=after*(length(offset)/length(after));
                else offset=rotate(offset,turn);
                trial[other]=trial.at(id)+offset;
            }
        }
        const double collisions=collisionCost(trial,bonds,unit,budget);
        if(collisions<0)return false;
        if(collisions>0)continue;
        double cost=0;
        for(auto id:ring){auto d=(trial.at(id)-positions.at(id))*(1./unit);cost+=dot(d,d);}
        if(cost<best){best=cost;result=std::move(trial);}
    }
    if(result.empty())return false;
    positions=std::move(result);
    return true;
}

struct RingLayoutGraph {
    std::map<ObjectId,std::vector<ObjectId>> neighbors,branches;
    explicit RingLayoutGraph(const std::vector<Edge>& bonds,Edge excluded=Edge(kInvalidObjectId,kInvalidObjectId)){
        for(auto link:bonds)if(link!=excluded){
            neighbors[link.first].push_back(link.second);
            neighbors[link.second].push_back(link.first);
        }
    }
    std::vector<ObjectId> path(ObjectId a,ObjectId b,const std::set<ObjectId>& excluded={}) const {
        if(!neighbors.count(a)||!neighbors.count(b))return {};
        std::vector<ObjectId> queue{a},result;
        std::map<ObjectId,ObjectId> parent{{a,a}};
        for(size_t i=0;i<queue.size()&&!parent.count(b);++i)
            for(auto next:neighbors.at(queue[i]))
                if(!excluded.count(next)&&parent.emplace(next,queue[i]).second)queue.push_back(next);
        if(parent.count(b)){
            for(auto id=b;id!=a;id=parent.at(id))result.push_back(id);
            result.push_back(a);
            std::reverse(result.begin(),result.end());
        }
        return result;
    }
    bool collectBranches(const std::set<ObjectId>& cycle,Edge bridgeheads=Edge(kInvalidObjectId,kInvalidObjectId)){
        std::set<ObjectId> assigned;
        for(auto id:cycle){
            int ringDegree=0;
            auto& branch=branches[id];
            for(auto next:neighbors.at(id)){
                if(cycle.count(next)){++ringDegree;continue;}
                if(!assigned.insert(next).second)return false;
                branch.push_back(next);
            }
            const int expected=(id==bridgeheads.first||id==bridgeheads.second)?3:2;
            if(ringDegree!=expected)return false;
            for(size_t i=0;i<branch.size();++i)for(auto next:neighbors.at(branch[i])){
                if(next==id)continue;
                if(cycle.count(next))return false;
                if(assigned.insert(next).second)branch.push_back(next);
            }
        }
        return true;
    }
    void placeBranches(const std::map<ObjectId,Vec2>& original,std::map<ObjectId,Vec2>& trial,bool outward=false) const {
        Vec2 oldCenter,newCenter;
        for(const auto& item:branches){oldCenter=oldCenter+original.at(item.first);newCenter=newCenter+trial.at(item.first);}
        oldCenter=oldCenter*(1./branches.size());newCenter=newCenter*(1./branches.size());
        for(const auto& item:branches){
            if(item.second.empty())continue;
            const auto id=item.first;
            const auto before=outward?original.at(item.second.front())-original.at(id):original.at(id)-oldCenter;
            const auto after=trial.at(id)-newCenter;
            const double turn=std::atan2(cross(before,after),dot(before,after));
            for(auto member:item.second)
                trial[member]=trial.at(id)+rotate(original.at(member)-original.at(id),turn);
        }
    }
};

// Keep sp C/N centers linear and trigonal carbon branches at 120 degrees.
// Cyclic edges supply the fixed frame; ring closure constraints are retained.
inline void alignTrigonalBranches(const std::map<Edge,int>& bonds,
                                 const std::map<ObjectId,AtomRecord>& atoms,
                                 const std::map<ObjectId,int>& charges,double unit,size_t& budget,
                                 std::map<ObjectId,Vec2>& positions,const std::set<ObjectId>* active=nullptr,
                                 const std::map<ObjectId,double>* masses=nullptr){
    std::map<ObjectId,std::vector<ObjectId>> neighbors;
    std::vector<Edge> links;
    for(const auto& bond:bonds)if(bond.second>0){
        neighbors[bond.first.first].push_back(bond.first.second);
        neighbors[bond.first.second].push_back(bond.first.first);
        links.push_back(bond.first);
    }
    for(const auto& junction:neighbors){
        const auto center=junction.first;
        if(active&&!active->count(center))continue;
        const auto& atom=atoms.at(center);
        const auto charge=charges.find(center);
        if(atom.semantics.nodeType!=uint8_t(NodeType::Element)||atom.semantics.radical||atom.semantics.aromatic)continue;
        const int q=atom.semantics.charge+(charge==charges.end()?0:charge->second);
        bool linear=false;
        if(junction.second.size()==2&&(atom.element==6||atom.element==7)){
            const int a=bonds.at(edge(center,junction.second[0])),b=bonds.at(edge(center,junction.second[1]));
            linear=(a==3&&b==1)||(a==1&&b==3)||
                (a==2&&b==2&&((atom.element==6&&!q)||(atom.element==7&&q==1)));
        }
        if(linear){
            double best=std::numeric_limits<double>::infinity();std::map<ObjectId,Vec2> placed;
            for(int side=0;side<2;++side){
                const auto first=junction.second[side],opposite=junction.second[1-side];
                std::vector<ObjectId> branch{first};std::set<ObjectId> seen{first};bool cyclic=false;
                for(size_t i=0;i<branch.size()&&!cyclic;++i)for(auto next:neighbors.at(branch[i])){
                    if(edge(branch[i],next)==edge(center,first))continue;
                    if(next==center){cyclic=true;break;}
                    if(seen.insert(next).second)branch.push_back(next);
                }
                if(cyclic)continue;
                std::vector<ObjectId> fixedBranch{opposite};std::set<ObjectId> fixedSeen{opposite};
                for(size_t i=0;i<fixedBranch.size();++i)for(auto next:neighbors.at(fixedBranch[i])){if(next==center)continue;if(fixedSeen.insert(next).second)fixedBranch.push_back(next);}
                const auto movingMass=studio_fragment_mass::total(branch,atoms,masses),fixedMass=studio_fragment_mass::total(fixedBranch,atoms,masses);
                if(movingMass>fixedMass+1e-6)continue;
                const auto pivot=positions.at(center),original=positions.at(first)-pivot;
                const auto target=pivot-positions.at(opposite);
                if(length(original)<1e-8||length(target)<1e-8)continue;
                const auto angle=std::atan2(cross(original,target),dot(original,target));
                auto trial=positions;
                for(auto id:branch)trial[id]=pivot+rotate(positions.at(id)-pivot,angle);
                const auto collisions=collisionCost(trial,links,unit,budget);
                if(collisions<0)return;
                if(collisions>0)continue;
                const auto cost=movingMass+std::abs(angle)*.001;
                if(cost<best){best=cost;placed=std::move(trial);}
            }
            if(!placed.empty())positions=std::move(placed);
            continue;
        }
        if(atom.element!=6||q!=0)continue;
        if(junction.second.size()!=3)continue;
        ObjectId doubleEnd=kInvalidObjectId;
        int sum=0;
        for(auto other:junction.second){
            const auto order=bonds.at(edge(center,other));
            sum+=order;
            if(order==2)doubleEnd=other;
        }
        if(sum!=4||doubleEnd==kInvalidObjectId)continue;
        std::vector<std::vector<ObjectId>> branches;
        bool acyclic=true;
        for(auto first:junction.second){
            std::vector<ObjectId> branch{first};std::set<ObjectId> seen{first};
            for(size_t i=0;i<branch.size()&&acyclic;++i)for(auto next:neighbors.at(branch[i])){
                if(edge(branch[i],next)==edge(center,first))continue;
                if(next==center){acyclic=false;break;}
                if(seen.insert(next).second)branch.push_back(next);
            }
            branches.push_back(branch);
        }
        if(acyclic&&branches.size()==3){
            size_t fixed=0;double maximum=-1;
            for(size_t i=0;i<branches.size();++i){double mass=studio_fragment_mass::total(branches[i],atoms,masses);
                if(mass>maximum+1e-6||(std::abs(mass-maximum)<=1e-6&&branches[i].front()==doubleEnd)){maximum=mass;fixed=i;}}
            const auto pivot=positions.at(center),axis=positions.at(branches[fixed].front())-pivot;
            double best=std::numeric_limits<double>::infinity();std::map<ObjectId,Vec2> placed;
            for(double sense:{-1.,1.}){
                auto trial=positions;
                size_t side=0;
                for(size_t branch=0;branch<branches.size();++branch)if(branch!=fixed){
                    const auto original=positions.at(branches[branch].front())-pivot;
                    const auto target=rotate(axis,sense*(side++?1:-1)*2.09439510239319549);
                    const auto turn=std::atan2(cross(original,target),dot(original,target));
                    for(auto id:branches[branch])trial[id]=pivot+rotate(positions.at(id)-pivot,turn);
                }
                const auto collisions=collisionCost(trial,links,unit,budget);if(collisions<0)return;
                double cost=collisions*1000;
                for(const auto& item:trial)cost+=lengthSq(item.second-positions.at(item.first))*.00001;
                if(cost<best){best=cost;placed=std::move(trial);}
            }
            if(!placed.empty())positions=std::move(placed);
            continue;
        }
        for(auto first:junction.second){
            if(first==doubleEnd)continue;
            std::vector<ObjectId> branch{first};
            std::set<ObjectId> seen{first};
            bool cyclic=false;
            for(size_t i=0;i<branch.size()&&!cyclic;++i)for(auto next:neighbors.at(branch[i])){
                if(edge(branch[i],next)==edge(center,first))continue;
                if(next==center){cyclic=true;break;}
                if(seen.insert(next).second)branch.push_back(next);
            }
            if(cyclic)continue;
            const auto pivot=positions.at(center),original=positions.at(first)-pivot;
            const auto axis=positions.at(doubleEnd)-pivot;
            if(length(original)<1e-8||length(axis)<1e-8)continue;
            double best=std::numeric_limits<double>::infinity();
            std::map<ObjectId,Vec2> placed;
            for(double sense:{-1.,1.}){
                const auto target=rotate(axis*(length(original)/length(axis)),sense*2.09439510239319549);
                const auto angle=std::atan2(cross(original,target),dot(original,target));
                auto trial=positions;
                for(auto id:branch)trial[id]=pivot+rotate(positions.at(id)-pivot,angle);
                const auto collisions=collisionCost(trial,links,unit,budget);
                if(collisions<0)return;
                if(collisions>0)continue;
                double score=std::abs(angle)*.001;
                for(auto other:junction.second)if(other!=first&&other!=doubleEnd){
                    const auto direction=trial.at(other)-pivot;
                    const auto cosine=dot(target,direction)/std::max(1e-9,length(target)*length(direction));
                    score+=(cosine+.5)*(cosine+.5);
                }
                if(score<best){best=score;placed=std::move(trial);}
            }
            if(!placed.empty())positions=std::move(placed);
        }
    }
}

// A new chord of a simple ring can be drawn as two polygons sharing the new
// bond. Attached trees/rings stay rigid.
inline bool placeRingChord(Edge chord,const std::vector<Edge>& bonds,double unit,
                           size_t& budget,std::map<ObjectId,Vec2>& positions){
    const auto a=chord.first,b=chord.second;
    RingLayoutGraph graph(bonds,chord);
    const auto first=graph.path(a,b);
    if(first.size()<3||first.size()>8)return false;
    const auto second=graph.path(a,b,std::set<ObjectId>(first.begin()+1,first.end()-1));
    if(second.size()<3||second.size()>8)return false;
    std::set<ObjectId> cycle(first.begin(),first.end());
    cycle.insert(second.begin(),second.end());
    if(!graph.collectBranches(cycle))return false;
    const auto midpoint=(positions.at(a)+positions.at(b))*.5;
    const auto axis=positions.at(b)-positions.at(a);
    const double heading=std::atan2(axis.y,axis.x);
    constexpr double pi=3.14159265358979323846;
    double best=std::numeric_limits<double>::infinity();
    std::map<ObjectId,Vec2> result;
    for(double sense:{1.,-1.})for(int direction=0;direction<24;++direction){
        auto trial=positions;
        const double angle=heading+direction*pi/12.;
        const Vec2 half=Vec2(std::cos(angle),std::sin(angle))*(unit*.5);
        trial[a]=midpoint-half;trial[b]=midpoint+half;
        auto laySide=[&](const std::vector<ObjectId>& side,double turn){
            auto cursor=trial.at(b);double facing=angle;
            for(size_t i=side.size()-2;i>0;--i){
                facing+=turn*2*pi/side.size();
                cursor=cursor+Vec2(std::cos(facing),std::sin(facing))*unit;
                trial[side[i]]=cursor;
            }
        };
        laySide(first,sense);laySide(second,-sense);
        auto frame=trial;
        graph.placeBranches(positions,trial);
        double collisions=collisionCost(trial,bonds,unit,budget);
        if(collisions>0){
            trial=std::move(frame);
            graph.placeBranches(positions,trial,true);
            collisions=collisionCost(trial,bonds,unit,budget);
        }
        if(collisions<0)return false;
        if(collisions>0)continue;
        double cost=0;
        for(auto id:cycle){auto delta=trial.at(id)-positions.at(id);cost+=dot(delta,delta);}
        if(cost<best){best=cost;result=std::move(trial);}
    }
    if(result.empty())return false;
    positions=std::move(result);
    return true;
}

// The closing bond need not be the shared edge of a fused product.
inline bool placeClosedRing(Edge closing,const std::vector<Edge>& bonds,double unit,
                            size_t& budget,std::map<ObjectId,Vec2>& positions){
    RingLayoutGraph open(bonds,closing);
    const auto ring=open.path(closing.first,closing.second);
    if(ring.size()<3||ring.size()>12)return false;
    RingLayoutGraph graph(bonds);
    if(!graph.collectBranches(std::set<ObjectId>(ring.begin(),ring.end()))){
        if(placeRingChord(closing,bonds,unit,budget,positions))return true;
        for(size_t i=1;i<ring.size();++i){
            auto a=ring[i-1],b=ring[i];
            if(graph.neighbors.at(a).size()>=3&&graph.neighbors.at(b).size()>=3&&
               placeRingChord(edge(a,b),bonds,unit,budget,positions))return true;
        }
        return false;
    }
    Vec2 center;
    for(auto id:ring)center=center+positions.at(id);
    center=center*(1./ring.size());
    const double turn=2*std::acos(-1.)/ring.size(),radius=unit/(2*std::sin(turn*.5));
    std::map<ObjectId,Vec2> best;double cost=std::numeric_limits<double>::infinity();
    for(double sense:{-1.,1.}){
        Vec2 fit;
        for(size_t i=0;i<ring.size();++i)fit=fit+rotate(positions.at(ring[i])-center,-sense*turn*i);
        const double offset=std::atan2(fit.y,fit.x);
        auto trial=positions;double movement=0;
        for(size_t i=0;i<ring.size();++i){
            auto id=ring[i];trial[id]=center+rotate(Vec2(radius,0),offset+sense*turn*i);
            auto d=trial[id]-positions.at(id);movement+=dot(d,d);
        }
        graph.placeBranches(positions,trial,true);
        if(movement<cost&&collisionCost(trial,bonds,unit,budget)==0){cost=movement;best=std::move(trial);}
    }
    if(best.empty())return false;
    positions=std::move(best);return true;
}

// A bridged bicyclic graph has three independent routes between bridgeheads.
// Use two for the perimeter and draw a route with no branches through its
// interior. This avoids forcing the reagent's substituents across the ring.
inline bool placeBridgedRing(Edge bridgeheads,const std::vector<Edge>& bonds,double unit,
                             size_t& budget,std::map<ObjectId,Vec2>& positions){
    const auto a=bridgeheads.first,b=bridgeheads.second;
    if(a==b)return false;
    RingLayoutGraph graph(bonds);
    std::array<std::vector<ObjectId>,3> paths;
    std::set<ObjectId> internal;
    for(auto& path:paths){
        path=graph.path(a,b,internal);
        if(path.size()<3||path.size()>7)return false;
        internal.insert(path.begin()+1,path.end()-1);
    }
    auto cycle=internal;cycle.insert(a);cycle.insert(b);
    if(!graph.collectBranches(cycle,bridgeheads))return false;
    const auto axis=positions.at(b)-positions.at(a);
    const double heading=std::atan2(axis.y,axis.x);
    constexpr double pi=3.14159265358979323846;
    double best=std::numeric_limits<double>::infinity();
    std::map<ObjectId,Vec2> result;
    for(size_t inner=0;inner<paths.size();++inner){
        const auto& bridge=paths[inner];
        if(std::any_of(bridge.begin()+1,bridge.end()-1,[&](ObjectId id){return !graph.branches.at(id).empty();}))continue;
        const auto& first=paths[(inner+1)%3];
        const auto& second=paths[(inner+2)%3];
        std::vector<ObjectId> perimeter=first;
        perimeter.insert(perimeter.end(),second.rbegin()+1,second.rend()-1);
        const double radius=unit/(2*std::sin(pi/perimeter.size()));
        auto radial=[&](size_t i){const double t=2*pi*i/perimeter.size();return Vec2(std::cos(t),std::sin(t))*radius;};
        const auto firstPoint=radial(0),secondPoint=radial(first.size()-1);
        const auto chord=secondPoint-firstPoint;
        const double ratio=length(chord)/((bridge.size()-1)*unit);
        if(ratio<.5||ratio>1.4)continue;
        for(double sense:{1.,-1.})for(int direction=0;direction<24;++direction){
            auto trial=positions;
            const double turn=heading+direction*pi/12.-std::atan2(sense*chord.y,chord.x);
            for(size_t i=0;i<perimeter.size();++i){
                auto delta=radial(i)-firstPoint;delta.y*=sense;
                trial[perimeter[i]]=positions.at(a)+rotate(delta,turn);
            }
            for(size_t i=1;i+1<bridge.size();++i)
                trial[bridge[i]]=trial.at(a)+(trial.at(b)-trial.at(a))*(double(i)/(bridge.size()-1));
            graph.placeBranches(positions,trial,true);
            const double collisions=collisionCost(trial,bonds,unit,budget);
            if(collisions<0)return false;
            if(collisions>0)continue;
            double cost=0;
            for(auto id:cycle){auto delta=trial.at(id)-positions.at(id);cost+=dot(delta,delta);}
            if(cost<best){best=cost;result=std::move(trial);}
        }
    }
    if(result.empty())return false;
    positions=std::move(result);
    return true;
}
}

struct StepIdentity {
    std::map<step_detail::Edge,int> bonds;
    std::map<ObjectId,std::array<int,3>> states;
    std::map<ObjectId,std::pair<ObjectId,ObjectId>> inversions;
    bool operator==(const StepIdentity& other)const{
        return bonds==other.bonds&&states==other.states&&inversions==other.inversions;
    }
};

// Caller wraps this operation in one command/transaction. All chemical validation
// precedes mutation; the command provides rollback for allocation/storage errors.
// Only source components are copied. Original structures and arrows are untouched.
inline Error generateNextStep(Document& doc,const std::vector<ElectronMove>& moves,std::vector<ObjectRef>* created,
                              StepIdentity* chemistryOnly=nullptr,bool* arranged=nullptr){
    if(arranged)*arranged=false;
    using namespace step_detail;
    if(created)created->clear();
    if(moves.empty())return fail("请先选中这一步的机理箭头。");
    if(moves.size()>32)return fail("一次最多处理 32 根机理箭头，请按反应步骤分别生成。");
    std::set<ObjectId> atoms;
    std::set<std::pair<ObjectRef,ObjectRef>> unique;
    std::map<ObjectRef,int> sources;
    for(const auto& m:moves){
        if(m.source==m.target)return fail("机理箭头起点和终点不能是同一个对象。");
        if(!unique.insert({m.source,m.target}).second)return fail("有重复的机理箭头，请只保留一次电子转移。");
        if((m.electronCount!=1&&m.electronCount!=2)||(sources[m.source]+=m.electronCount)>2)return fail("同一个电子来源被多次使用，请拆成连续步骤。");
        for(auto r:{m.source,m.target}){
            if(r.kind==ObjectKind::Atom&&doc.atoms().alive(r.id))atoms.insert(r.id);
            else if(r.kind==ObjectKind::Bond&&doc.bonds().alive(r.id)){auto b=doc.bonds().alive(r.id);atoms.insert(b->beginAtom);atoms.insert(b->endAtom);}
            else{
                ObjectId a,b;
                if(r==m.target&&virtual_bonds::endpoints(doc,r,a,b)){atoms.insert(a);atoms.insert(b);}
                else return fail("箭头端点没有绑定到有效原子或化学键，请重新指定起点和终点。");
            }
        }
    }
    // Atom-started pi donation also changes the other end of the pi bond.
    for(const auto& m:moves)if(m.electronCount==2&&m.source.kind==ObjectKind::Atom){auto a=doc.atoms().alive(m.source.id);if(a&&a->element==6&&!a->semantics.charge&&!a->semantics.radical)for(auto bid:doc.incidentBonds(a->id)){auto b=doc.bonds().alive(bid);if(b&&b->order==BondOrder::Double&&b->semantics==BondSemantics::Normal){atoms.insert(b->beginAtom);atoms.insert(b->endAtom);}}}
    const std::set<ObjectId> potentialReacting=atoms;
    std::vector<ObjectId> queue(atoms.begin(),atoms.end());
    for(size_t i=0;i<queue.size();++i){
        if(queue.size()>500)return fail("这一步结构过大，请将反应拆分后生成。");
        for(auto id:doc.incidentBonds(queue[i]))if(auto b=doc.bonds().alive(id)){
            auto other=b->beginAtom==queue[i]?b->endAtom:b->beginAtom;
            if(atoms.insert(other).second)queue.push_back(other);
        }
    }
    std::map<Edge,int> original;
    std::map<Edge,BondRecord> records;
    std::map<ObjectId,int> sums,hydrogens;
    std::map<ObjectId,AtomRecord> atomRecords;
    std::map<ObjectId,std::string> roles;
    std::map<ObjectId,std::string> compactPhosphines;
    std::map<ObjectId,std::string> abbreviations;
    for(const auto& b:doc.bonds().records())if(b.isAlive()&&atoms.count(b.beginAtom)&&atoms.count(b.endAtom)){
        bool ordinary=b.semantics==BondSemantics::Normal&&int(b.order)>=1&&int(b.order)<=3;
        bool aromatic=b.semantics==BondSemantics::Aromatic&&(b.order==BondOrder::Aromatic||b.order==BondOrder::Single||b.order==BondOrder::Double);
        if(!ordinary&&(!aromatic||potentialReacting.count(b.beginAtom)||potentialReacting.count(b.endAtom)))return fail("反应中心暂不支持芳香键、配位键或查询键；远端未参与反应的芳香环可原样保留。");
        auto e=edge(b.beginAtom,b.endAtom);original[e]=int(b.order);records[e]=b;sums[b.beginAtom]+=int(b.order);sums[b.endAtom]+=int(b.order);
    }
    for(auto id:atoms){auto a=doc.atoms().alive(id);if(!a)return fail("反应结构包含失效原子。");
        const auto label=doc.atomLabel(id);
        if(studioions::compact(doc,id)){
            abbreviations[id]=label;atomRecords[id]=*a;sums[id]+=studioions::hiddenValence(doc,id);
            hydrogens[id]=std::max(0,int(a->semantics.implicitHydrogens));continue;
        }
        if(triphenylphosphineLabel(label)&&(a->element==15||
           (a->element==6&&a->semantics.nodeType!=uint8_t(NodeType::Element)))){
            // The three phenyl bonds are represented by the compact label.
            const auto& sem=a->semantics;
            if(sem.radical||sem.isotope||sem.aromatic||sem.implicitHydrogens>0||sums[id]+3!=valence(15,sem.charge))
                return fail("三苯基膦缩写的成键或电荷不完整，请检查磷原子。");
            compactPhosphines[id]=label;atomRecords[id]=*a;atomRecords[id].element=15;
            hydrogens[id]=0;sums[id]+=3;continue;
        }
        auto role=genericRole(doc,id);
        if(!role.empty()){
            if(a->semantics.radical||a->semantics.isotope||a->semantics.implicitHydrogens!=0||sums[id]!=genericRoleValence(role,a->semantics.charge))return fail("通用 B/Nu 仅支持未成键的负离子或形成一根单键的中性状态。");
            roles[id]=role;atomRecords[id]=*a;hydrogens[id]=0;continue;
        }
        const auto ref=ObjectRef(ObjectKind::Atom,id);
        const auto& attributes=static_cast<const Document&>(doc).extensions(ref);
        if(!label.empty()&&attributes.findValue("studio.nickname.fragment")){
            if(potentialReacting.count(id))
                return fail("箭头直接涉及缩写官能团 "+label+"，请先手动展开该官能团并指定反应端点。");
            abbreviations[id]=label;
            atomRecords[id]=*a;
            atomRecords[id].semantics.nodeType=uint8_t(NodeType::Nickname);
            atomRecords[id].semantics.showLabel=true;
            hydrogens[id]=a->semantics.implicitHydrogens;
            continue;
        }
        // The current editor can leave an empty GenericNickname after erasing
        // a skeletal carbon label. Normalize only an unlabelled, closed-shell
        // neutral three-coordinate carbon with bond-order sum four and no
        // hidden text/fragment/query payload (carbonyl and enolate alike). This is
        // a local product copy, never an edit to the user's source document.
        AtomRecord normalized=*a;
        if(a->semantics.nodeType==uint8_t(NodeType::Query)&&a->element==6&&doc.atomLabel(id).empty()&&a->semantics.charge==0&&!a->semantics.radical&&!a->semantics.aromatic){
            const auto& bag=static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Atom,id));bool hidden=false;
            for(const auto& entry:bag.entries())if((entry.key=="studio.nickname.fragment"||entry.key=="studio.atom.runs"||entry.key=="studio.label.text"||entry.key.find("query")!=std::string::npos||entry.key.find("ElementList")!=std::string::npos)&&!entry.value.empty())hidden=true;
            int singles=0,doubles=0,total=0;
            for(auto bid:doc.incidentBonds(id))if(auto bond=doc.bonds().alive(bid)){++total;auto other=doc.atoms().alive(bond->beginAtom==id?bond->endAtom:bond->beginAtom);if(bond->semantics==BondSemantics::Normal&&other){if(bond->order==BondOrder::Double)++doubles;if(bond->order==BondOrder::Single)++singles;}}
            if(!hidden&&total==3&&doubles==1&&singles==2){normalized.semantics.nodeType=uint8_t(NodeType::Element);normalized.semantics.showLabel=false;a=&normalized;}
        }
        if(a->semantics.nodeType!=uint8_t(NodeType::Element)||(a->semantics.radical&&a->semantics.radical!=uint8_t(Radical::Doublet)&&!(a->element==6&&!a->semantics.charge&&a->semantics.radical==uint8_t(Radical::Singlet))))return fail("请展开缩写；当前支持闭壳层和单自由基原子。");
        if(!label.empty()&&label!=elementSymbol(a->element))return fail("请先展开带缩写或自定义标签的原子，再生成下一步。");
        atomRecords[id]=*a;
        if(!potentialReacting.count(id)){hydrogens[id]=a->semantics.implicitHydrogens;continue;}
        int base=valence(a->element,a->semantics.charge);
        if(a->element==25||a->element==24||a->element==76||a->element==44||a->element==50||
           ((a->element==53||a->element==15||a->element==16||a->element==34||a->element==52)&&sums[id]>base))
            base=sums[id]+std::max(0,int(a->semantics.implicitHydrogens))+(a->semantics.radical==uint8_t(Radical::Doublet)?1:a->semantics.radical==uint8_t(Radical::Singlet)?2:0);
        int v=base-(a->semantics.radical==uint8_t(Radical::Doublet)?1:a->semantics.radical==uint8_t(Radical::Singlet)?2:0);if(v<0)return fail("反应中心暂支持 C、N、O、H、P 和卤素的常见价态；未参与反应的其他元素可原样保留。");
        int h=a->semantics.implicitHydrogens<0?std::max(0,v-sums[id]):a->semantics.implicitHydrogens;
        if(sums[id]+h!=v)return fail("反应物的价态或氢数不完整，请先检查结构。");
        hydrogens[id]=h;atomRecords[id]=*a;
    }
    auto electronDeficit=[](const AtomRecord& atom){return atom.semantics.radical==uint8_t(Radical::Singlet)?2:atom.semantics.radical==uint8_t(Radical::Doublet)?1:0;};
    auto stateValence=[&](ObjectId id,int q){
        const auto& atom=atomRecords.at(id);
        int base=valence(atom.element,q);
        if(atom.element==25||atom.element==24||atom.element==76||atom.element==44||atom.element==50||
           ((atom.element==53||atom.element==15||atom.element==16||atom.element==34||atom.element==52)&&sums[id]>valence(atom.element,atom.semantics.charge)))
            base=sums[id]+hydrogens[id]+electronDeficit(atom)+q-atom.semantics.charge;
        return base;
    };
    auto neutralRadical=[&](ObjectId id){
        const auto& s=atomRecords.at(id).semantics;
        return !s.charge&&s.radical==uint8_t(Radical::Doublet);
    };
    auto attacks=[&](const ElectronMove& m,const BondRecord& bond,ObjectId end){
        if(m.electronCount!=1||m.source.kind!=ObjectKind::Atom||!neutralRadical(m.source.id))return false;
        if(m.target==ObjectRef(ObjectKind::Atom,end))return true;
        return m.target==ObjectRef(ObjectKind::Bond,bond.id)&&bond.order!=BondOrder::Single;
    };
    auto otherEnd=[](const BondRecord& bond,ObjectId id){return bond.beginAtom==id?bond.endAtom:bond.beginAtom;};
    std::vector<Change> candidates(1);
    for(const auto& m:moves){
        std::vector<Change> alternatives;
        ObjectId guideBegin,guideEnd;
        if(virtual_bonds::endpoints(doc,m.target,guideBegin,guideEnd)){
            Change c;
            ObjectId donor=kInvalidObjectId,other=kInvalidObjectId;
            if(m.source.kind==ObjectKind::Atom){
                donor=m.source.id;
                if(donor!=guideBegin&&donor!=guideEnd)return fail("电子来源必须位于待成键的一个端点。");
                if(m.electronCount==1){
                    if(!neutralRadical(donor))return fail("鱼钩箭头的待成键来源需要自由基电子。");
                    --c.radicals[donor];
                }
            }else{
                const auto& source=*doc.bonds().alive(m.source.id);
                const bool begin=source.beginAtom==guideBegin||source.beginAtom==guideEnd;
                const bool end=source.endAtom==guideBegin||source.endAtom==guideEnd;
                if(begin==end)return fail("电子来源键必须与待成键共用一个端点。");
                donor=begin?source.beginAtom:source.endAtom;
                other=begin?source.endAtom:source.beginAtom;
                if(m.electronCount==1){
                    --c.halfBonds[edge(donor,other)];
                    // With a single departing hook the companion electron
                    // remains on the other atom. An explicit second hook
                    // accounts for that electron itself.
                    if(sources[m.source]==1){--c.halfBonds[edge(donor,other)];++c.radicals[other];}
                }
                else --c.bonds[edge(donor,other)];
            }
            const auto acceptor=donor==guideBegin?guideEnd:guideBegin;
            if(m.electronCount==1)++c.halfBonds[edge(guideBegin,guideEnd)];
            else{++c.bonds[edge(guideBegin,guideEnd)];transfer(c,other==kInvalidObjectId?donor:other,acceptor);}
            alternatives.push_back(c);
        }else if(m.electronCount==1){
            Change c;
            if(m.source.kind==ObjectKind::Atom){
                auto donor=m.source.id;ObjectId acceptor=m.target.id;
                if(m.target.kind==ObjectKind::Bond){
                    auto b=doc.bonds().alive(m.target.id);
                    if(b->beginAtom==donor||b->endAtom==donor)acceptor=otherEnd(*b,donor);
                    else{
                        if(!neutralRadical(donor)||b->order==BondOrder::Single)
                            return fail("自由基可直接进攻多重键；其他成键请指向具体原子。");
                        for(auto end:{b->beginAtom,b->endAtom}){
                            Change addition;++addition.halfBonds[edge(donor,end)];--addition.radicals[donor];
                            alternatives.push_back(addition);
                        }
                        // Each candidate specifies which end receives the bond;
                        // the other fishhooks and valence decide between them.
                        acceptor=kInvalidObjectId;
                    }
                }
                if(acceptor!=kInvalidObjectId){
                const auto& ds=atomRecords[donor].semantics;const auto& ts=atomRecords[acceptor].semantics;
                // An electron delivered into an empty cation orbital is SET,
                // not half of an invented covalent bond.
                if(m.target.kind==ObjectKind::Atom&&ts.charge==1&&!ts.radical&&
                   ((ds.charge==-1&&!ds.radical)||(ds.charge==0&&ds.radical==uint8_t(Radical::Doublet)))){
                    transfer(c,donor,acceptor);c.radicals[donor]+=ds.radical?-1:1;++c.radicals[acceptor];
                }else{++c.halfBonds[edge(donor,acceptor)];--c.radicals[donor];}
                }
            }else{
                auto b=doc.bonds().alive(m.source.id);auto e=edge(b->beginAtom,b->endAtom);
                const bool oneHook=sources[m.source]==1;
                if(m.target.kind==ObjectKind::Bond){
                    auto receiving=doc.bonds().alive(m.target.id);
                    ObjectId shared=kInvalidObjectId;
                    for(auto id:{b->beginAtom,b->endAtom})if(id==receiving->beginAtom||id==receiving->endAtom)shared=id;
                    if(shared==kInvalidObjectId)return fail("单电子从键转移到键时，请指定共享原子的两根键。");
                    const auto leaving=otherEnd(*b,shared);
                    --c.halfBonds[e];++c.halfBonds[edge(receiving->beginAtom,receiving->endAtom)];
                    if(oneHook){
                        // Beta scission: the second electron can remain on the
                        // departing atom, or pair with an explicit attack there.
                        --c.halfBonds[e];++c.radicals[leaving];
                        alternatives.push_back(c);
                        for(const auto& incoming:moves)if(attacks(incoming,*b,leaving)&&
                            incoming.target.kind==ObjectKind::Atom&&incoming.source.id!=shared){
                            Change paired=c;--paired.radicals[leaving];
                            ++paired.halfBonds[edge(leaving,incoming.source.id)];alternatives.push_back(paired);
                        }
                    }
                }else{
                    auto t=m.target.id;
                    if(t==b->beginAtom||t==b->endAtom){
                        --c.halfBonds[e];++c.radicals[t];
                        if(!oneHook)for(const auto& incoming:moves)if(incoming.electronCount==1&&incoming.source.kind==ObjectKind::Atom&&incoming.source.id!=b->beginAtom&&incoming.source.id!=b->endAtom&&attacks(incoming,*b,t)){
                            Change paired=c;--paired.radicals[t];++paired.halfBonds[edge(t,incoming.source.id)];alternatives.push_back(paired);
                        }
                        if(oneHook){
                            const auto donor=otherEnd(*b,t);
                            --c.halfBonds[e];++c.radicals[donor];
                            alternatives.push_back(c);
                            for(const auto& incoming:moves){
                                Edge receiving;
                                if(attacks(incoming,*b,donor)&&incoming.source.id!=t&&incoming.source.id!=donor){
                                    receiving=edge(donor,incoming.source.id);
                                }else if(incoming.electronCount==1&&incoming.source.kind==ObjectKind::Atom&&
                                         incoming.target.kind==ObjectKind::Bond&&neutralRadical(incoming.source.id)){
                                    auto target=doc.bonds().alive(incoming.target.id);
                                    if((target->beginAtom==donor&&target->endAtom==incoming.source.id)||
                                       (target->endAtom==donor&&target->beginAtom==incoming.source.id))
                                        receiving=edge(target->beginAtom,target->endAtom);
                                }
                                if(receiving.first==receiving.second)continue;
                                Change paired=c;--paired.radicals[donor];++paired.halfBonds[receiving];
                                alternatives.push_back(paired);
                            }
                        }
                    }else for(auto donor:{b->beginAtom,b->endAtom}){
                        Change half;--half.halfBonds[e];++half.halfBonds[edge(donor,t)];alternatives.push_back(half);
                        if(oneHook)for(const auto& incoming:moves)if(incoming.source==ObjectRef(ObjectKind::Atom,t)&&attacks(incoming,*b,donor)){
                            Change paired=half;--paired.halfBonds[e];++paired.radicals[otherEnd(*b,donor)];
                            alternatives.push_back(paired);break;
                        }
                    }
                }
            }
            if(alternatives.empty())alternatives.push_back(c);
        }else if(m.source.kind==ObjectKind::Atom){
            ObjectId donor=m.source.id,acceptor=kInvalidObjectId;
            if(m.target.kind==ObjectKind::Atom)acceptor=m.target.id;
            else {auto b=doc.bonds().alive(m.target.id);if(b->beginAtom!=donor&&b->endAtom!=donor)return fail("原子指向化学键时，起点必须是该键的一个端点。");acceptor=b->beginAtom==donor?b->endAtom:b->beginAtom;}
            // Starting a pair arrow on a neutral alkene carbon identifies the
            // bonding end of its pi electrons, not a nonexistent lone pair.
            // Keep the indicated carbon as the new bond endpoint; enumerate
            // adjacent double bonds and validate all simultaneous moves below.
            const auto& sourceAtom=atomRecords[donor];
            if(sourceAtom.element==6&&sourceAtom.semantics.charge==0&&!sourceAtom.semantics.radical&&lonePairCapacity(doc,donor)==0){
                for(auto bid:doc.incidentBonds(donor)){auto pi=doc.bonds().alive(bid);if(!pi||pi->order!=BondOrder::Double||pi->semantics!=BondSemantics::Normal)continue;ObjectId other=pi->beginAtom==donor?pi->endAtom:pi->beginAtom;if(other==acceptor)continue;
                    Change c;c.piUses[edge(donor,other)]+=2;--c.bonds[edge(donor,other)];++c.bonds[edge(donor,acceptor)];transfer(c,other,acceptor);alternatives.push_back(c);
                }
            }
            if(alternatives.empty()){Change c;++c.bonds[edge(donor,acceptor)];transfer(c,donor,acceptor);alternatives.push_back(c);}
        }else{
            auto b=doc.bonds().alive(m.source.id);Edge source=edge(b->beginAtom,b->endAtom);
            if(m.target.kind==ObjectKind::Atom){ObjectId t=m.target.id;
                if(t==b->beginAtom||t==b->endAtom){Change c;--c.bonds[source];transfer(c,t==b->beginAtom?b->endAtom:b->beginAtom,t);alternatives.push_back(c);}
                else for(auto donor:{b->beginAtom,b->endAtom}){
                    if(m.bondingEnd!=kInvalidObjectId&&m.bondingEnd!=donor)continue;
                    Change c;--c.bonds[source];++c.bonds[edge(donor,t)];
                    transfer(c,otherEnd(*b,donor),t);alternatives.push_back(c);
                }
            }else{
                auto t=doc.bonds().alive(m.target.id);ObjectId shared=kInvalidObjectId;
                for(auto a:{b->beginAtom,b->endAtom})if(a==t->beginAtom||a==t->endAtom)shared=a;
                if(shared==kInvalidObjectId){
                    // Pi donation into a second pi system. The outgoing arrow
                    // on the acceptor already accounts for its bond-order loss.
                    if(b->order!=BondOrder::Double||t->order!=BondOrder::Double||sources[m.target]!=2)
                        return fail("不相邻键之间成键时，请指定受攻原子，或补齐受体 π 键的电子去向。");
                    for(auto from:{b->beginAtom,b->endAtom})for(auto to:{t->beginAtom,t->endAtom}){
                        Change c;--c.bonds[source];++c.bonds[edge(from,to)];
                        transfer(c,otherEnd(*b,from),to);alternatives.push_back(c);
                    }
                }else{
                    Change c;--c.bonds[source];++c.bonds[edge(t->beginAtom,t->endAtom)];transfer(c,b->beginAtom==shared?b->endAtom:b->beginAtom,t->beginAtom==shared?t->endAtom:t->beginAtom);alternatives.push_back(c);
                }
            }
        }
        if(m.source.kind==ObjectKind::Bond){auto b=doc.bonds().alive(m.source.id);if(b&&b->order==BondOrder::Double)for(auto& alt:alternatives){
            auto source=edge(b->beginAtom,b->endAtom);
            alt.piUses[source]+=m.electronCount==1?-alt.halfBonds.at(source):2;
        }}
        if(candidates.size()*alternatives.size()>256)return fail("电子来源方向存在过多歧义，请分步绘制。");
        std::vector<Change> next;
        for(const auto& base:candidates)for(const auto& alt:alternatives){Change c=base;for(auto d:alt.bonds)c.bonds[d.first]+=d.second;for(auto d:alt.charge)c.charge[d.first]+=d.second;for(auto d:alt.radicals)c.radicals[d.first]+=d.second;for(auto d:alt.halfBonds)c.halfBonds[d.first]+=d.second;for(auto d:alt.piUses)c.piUses[d.first]+=d.second;next.push_back(c);}
        candidates.swap(next);
    }
    std::vector<Change> valid;
    for(auto c:candidates){bool ok=true;for(auto use:c.piUses)if(use.second>2)ok=false;for(auto d:c.halfBonds){if(d.second%2)ok=false;else c.bonds[d.first]+=d.second/2;}auto degree=sums;int chargeSum=0;
        for(auto d:c.bonds){int before=original.count(d.first)?original.at(d.first):0;int after=before+d.second;if(after<0||after>3){ok=false;break;}degree[d.first.first]+=d.second;degree[d.first.second]+=d.second;}
        for(auto id:potentialReacting){
            const auto& atom=atomRecords.at(id);
            int q=atom.semantics.charge+c.charge[id];
            int r=electronDeficit(atom)+c.radicals[id];
            if(atom.element==6&&!q){
                bool alpha=false;
                for(const auto& move:moves)if(move.electronCount==2&&move.source.kind==ObjectKind::Bond&&move.target.kind==ObjectKind::Atom){
                    const auto* lost=doc.bonds().alive(move.source.id);
                    const auto leaving=move.target.id;
                    alpha|=(lost->order==BondOrder::Single||lost->order==BondOrder::Double)&&
                        ((lost->beginAtom==id&&lost->endAtom==leaving)||(lost->endAtom==id&&lost->beginAtom==leaving))&&
                        atomRecords.at(leaving).element!=6&&atomRecords.at(leaving).element!=1;
                }
                if(alpha&&degree[id]+hydrogens[id]==2){r=2;c.radicals[id]=r-electronDeficit(atom);}
            }
            if(atom.semantics.radical==uint8_t(Radical::Singlet)&&degree[id]!=sums[id]){
                r=0;c.radicals[id]=-2;
            }
            chargeSum+=c.charge[id];
            if(r<0||r>2||(r==2&&atom.element!=6)||(r&&(q||compactPhosphines.count(id))))ok=false;
            const int v=roles.count(id)?genericRoleValence(roles[id],q):stateValence(id,q)-r;
            const int actual=degree[id]+hydrogens[id];
            // Concerted oxidative cleavage reduces a supplied high-valent
            // reagent by two without manufacturing hydrogens or ionic charge.
            const bool reduced=!r&&q==atom.semantics.charge&&v>=5&&actual==v-2&&
                (atom.element==53||atom.element==25||atom.element==24||atom.element==76||atom.element==44);
            if(v<0||(actual!=v&&!reduced))ok=false;
        }
        for(auto entry:roles){auto id=entry.first;for(const auto& originalBond:original){int order=originalBond.second+(c.bonds.count(originalBond.first)?c.bonds.at(originalBond.first):0);if(order>0&&(originalBond.first.first==id||originalBond.first.second==id)){auto other=originalBond.first.first==id?originalBond.first.second:originalBond.first.first;if(order!=1||(entry.second=="base"&&atomRecords[other].element!=1))ok=false;}}
            for(const auto& added:c.bonds)if(!original.count(added.first)&&added.second>0&&(added.first.first==id||added.first.second==id)){auto other=added.first.first==id?added.first.second:added.first.first;if(added.second!=1||(entry.second=="base"&&atomRecords[other].element!=1))ok=false;}
        }
        if(chargeSum)ok=false;
        if(ok){bool duplicate=false;for(const auto& prior:valid)if(prior.bonds==c.bonds&&prior.charge==c.charge&&prior.radicals==c.radicals)duplicate=true;if(!duplicate)valid.push_back(c);}
    }
    if(valid.empty())return fail("这些箭头无法同时得到合理价态。请检查是否漏画电子转移、端点是否选对，以及电荷和氢数。");
    if(valid.size()>1){
        // An adjacent ring sigma bond migrates onto the acceptor when an
        // electron pair or leaving group departs, or the acceptor is cationic.
        // Valence alone can also allow an unintended open chain with a
        // double bond on the existing edge.
        for(const auto& shift:moves){
            const auto migrating=ringMigrationEndpoint(doc,shift,moves);
            if(migrating==kInvalidObjectId)continue;
            const auto& sigma=*doc.bonds().alive(shift.source.id);
            const auto target=shift.target.id;
            const auto stationary=sigma.beginAtom==migrating?sigma.endAtom:sigma.beginAtom;
            const auto addition=edge(target,migrating);
            const auto isMigration=[&](const Change& option){
                const auto entry=option.bonds.find(addition);
                return entry!=option.bonds.end()&&entry->second==1;
            };
            if(std::any_of(valid.begin(),valid.end(),isMigration)){
                const auto bridge=edge(target,stationary);
                valid.erase(std::remove_if(valid.begin(),valid.end(),[&](const Change& option){
                    const auto raised=option.bonds.find(bridge);
                    const auto charge=option.charge.find(migrating);
                    return raised!=option.bonds.end()&&raised->second==1&&
                           charge!=option.charge.end()&&charge->second==1&&!isMigration(option);
                }),valid.end());
            }
        }
    }
    if(valid.size()!=1)return fail("电子从键的哪一端参与成键仍有歧义，请补全同一步的其他机理箭头，或改用明确的原子起点。");
    auto change=valid.front();auto product=original;bool changed=false;
    for(auto d:change.bonds){product[d.first]+=d.second;if(d.second)changed=true;}
    for(auto d:change.charge)if(d.second)changed=true;
    for(auto d:change.radicals)if(d.second)changed=true;
    if(!changed)return fail("这些箭头互相抵消，没有产生下一步结构。");
    std::set<ObjectId> reacting;
    for(auto d:change.bonds)if(d.second){reacting.insert(d.first.first);reacting.insert(d.first.second);}
    for(auto d:change.charge)if(d.second)reacting.insert(d.first);
    for(auto d:change.radicals)if(d.second)reacting.insert(d.first);
    std::map<ObjectId,Substitution> substitutions;
    for(const auto& departure:moves){
        const auto center=substitutionCarbon(doc,departure);
        if(center==kInvalidObjectId||change.charge[center]||change.radicals[center])continue;
        const auto leaving=departure.target.id;
        if(product.at(edge(center,leaving))!=0)continue;
        for(const auto& attack:moves){
            if(attack.electronCount!=2||attack.source.kind!=ObjectKind::Atom||
               attack.target!=ObjectRef(ObjectKind::Atom,center))continue;
            const auto incoming=edge(center,attack.source.id);
            const auto entering=product.find(incoming);
            if(original.count(incoming)||entering==product.end()||entering->second!=1)continue;
            int changes=0;
            for(const auto& bond:change.bonds)
                if(bond.second&&(bond.first.first==center||bond.first.second==center))++changes;
            if(changes==2)substitutions.emplace(center,Substitution{attack.source.id,leaving});
        }
    }
    const auto stereoError=[](){
        return fail("反应中心的立体构型无法确定，请检查楔键方向和机理箭头。");
    };
    std::set<ObjectId> carbeneAdditionEnds;
    for(const auto& atom:atomRecords)if(atom.second.element==6&&atom.second.semantics.radical==uint8_t(Radical::Singlet)){
        std::vector<ObjectId> entering;
        for(const auto& link:change.bonds)if(link.second==1&&!original.count(link.first)&&
            (link.first.first==atom.first||link.first.second==atom.first))
            entering.push_back(link.first.first==atom.first?link.first.second:link.first.first);
        if(entering.size()==2){
            const auto pi=edge(entering[0],entering[1]);
            if(original.count(pi)&&original.at(pi)==2&&product.at(pi)==1){
                carbeneAdditionEnds.insert(entering.begin(),entering.end());
            }
        }
    }
    std::map<ObjectId,Inversion> inversions;
    for(const auto& item:records){const auto& b=item.second;
        if(!hasStereoMark(b)||(!reacting.count(b.beginAtom)&&!reacting.count(b.endAtom)))continue;
        if(!wedgeDepth(b))return stereoError();
        if(!reacting.count(b.beginAtom)&&product.at(item.first)==1){
            continue;
        }
        if(carbeneAdditionEnds.count(b.beginAtom)&&product.at(item.first)==1&&!change.bonds[item.first])continue;
        if(!substitutions.count(b.beginAtom))return stereoError();
        inversions.emplace(b.beginAtom,Inversion{});
    }
    for(auto& item:inversions){
        const auto center=item.first;
        auto& inversion=item.second;
        const auto& incident=doc.incidentBonds(center);
        if(incident.size()+hydrogens[center]!=4||hydrogens[center]>1)return stereoError();
        std::array<Vec2,4> points;
        size_t slot=0;
        for(auto id:incident){
            const auto& bond=*doc.bonds().alive(id);
            const auto ligand=bond.beginAtom==center?bond.endAtom:bond.beginAtom;
            inversion.ligands[slot]=ligand;
            inversion.depth[slot]=bond.beginAtom==center?wedgeDepth(bond):0;
            points[slot++]=atomRecords.at(ligand).position;
        }
        if(slot==3){
            inversion.ligands[slot]=center; // use the center as the implicit hydrogen projection
            points[slot]=atomRecords.at(center).position;
        }
        inversion.originalVolume=tetrahedralVolume(points,inversion.depth);
        if(std::fabs(inversion.originalVolume)<1e-6)return stereoError();
    }

    // Endpoint inference compares chemistry in the original atom ID space.
    // Page coordinates and the layout of a trial candidate cannot decide a reaction.
    if(chemistryOnly){
        StepIdentity result;
        for(auto link:product)if(link.second>0)result.bonds.insert(link);
        for(auto id:atoms)result.states[id]={{atomRecords.at(id).semantics.charge+change.charge[id],
                                               electronDeficit(atomRecords.at(id))+change.radicals[id],hydrogens[id]}};
        for(auto item:inversions)result.inversions[item.first]={substitutions.at(item.first).nucleophile,substitutions.at(item.first).leaving};
        *chemistryOnly=std::move(result);return Error();
    }
    // Arrange the surviving fragments around the newly formed bonds.
    // Existing rings stay rigid unless the reaction changes their topology.
    std::map<ObjectId,int> component;int nextComponent=0;
    for(auto id:atoms)if(!component.count(id)){std::vector<ObjectId> pending(1,id);component[id]=nextComponent;for(size_t i=0;i<pending.size();++i)for(auto bid:doc.incidentBonds(pending[i]))if(auto b=doc.bonds().alive(bid)){auto other=b->beginAtom==pending[i]?b->endAtom:b->beginAtom;if(atoms.count(other)&&product[edge(pending[i],other)]>0&&!component.count(other)){component[other]=nextComponent;pending.push_back(other);}}++nextComponent;}
    std::map<ObjectId,Vec2> positions;std::vector<double> lengths;
    for(auto id:atoms)positions[id]=atomRecords[id].position;
    // Coincident Newman carbon positions are meaningful only in the source
    // depiction. Unfold the scratch product frame before geometry validation.
    {Document frame=doc;for(const auto& b:doc.bonds().records())if(b.isAlive()&&projection_molecule::axis(doc,b)&&projection_molecule::kind(doc,b.beginAtom)==2){auto e=projection_molecule::convert(frame,b.id,1,0);if(!e.ok())return e;}
     for(auto id:atoms)positions[id]=frame.atoms().alive(id)->position;}
    for(auto e:original){double len=length(positions[e.first.first]-positions[e.first.second]);if(len>1)lengths.push_back(len);}
    std::sort(lengths.begin(),lengths.end());double unit=lengths.empty()?24:lengths[lengths.size()/2];unit=std::max(10.,std::min(100.,unit));
    std::vector<Edge> placedBonds;
    for(auto e:product)if(e.second>0&&original.count(e.first))placedBonds.push_back(e.first);
    std::map<ObjectId,ObjectId> hydroxyls;
    std::map<ObjectId,int> finalDegree;
    for(auto e:product)if(e.second>0){
        finalDegree[e.first.first]+=e.second;finalDegree[e.first.second]+=e.second;
    }
    for(auto e:product)if(e.second==1)for(auto id:{e.first.first,e.first.second}){
        const auto& atom=atomRecords.at(id);
        if(atom.element==8&&hydrogens[id]!=0&&finalDegree[id]==1&&
           atom.semantics.charge+change.charge[id]==0&&
           (atom.semantics.radical==uint8_t(Radical::Doublet)?1:0)+change.radicals[id]==0)
            hydroxyls[id]=e.first.first==id?e.first.second:e.first.first;
    }
    const double labelEm=integration::molecular::setting(doc,"LabelSize",10);
    const auto layoutMasses=studio_fragment_mass::weights(doc);
    std::set<Edge> positionedLinks;
    size_t geometryBudget=30000000;
    for(auto e:product)if(e.second>0&&!original.count(e.first)&&!positionedLinks.count(e.first)){
        auto a=e.first.first,b=e.first.second;
        if(substitutions.count(b)&&substitutions.at(b).nucleophile==a)std::swap(a,b);
        const auto substitution=substitutions.find(a);
        const bool substitutionLink=substitution!=substitutions.end()&&substitution->second.nucleophile==b;
        // Use molecular mass, including hidden abbreviation atoms and H,
        // rather than atom IDs, donor/acceptor roles, or displayed atom counts.
        // Keep the heavier surviving component as the fixed coordinate frame.
        if(component[a]!=component[b]){
            double first=0,second=0;for(auto id:atoms){if(component[id]==component[a])first+=layoutMasses.at(id);if(component[id]==component[b])second+=layoutMasses.at(id);}
            if(second>first+1e-6)std::swap(a,b);
        }
        int fixed=component[a],moving=component[b];
        placedBonds.push_back(e.first);
        if(fixed==moving){
            bool placed=false;
            for(const auto& move:moves){
                if(move.electronCount!=2||move.source.kind!=ObjectKind::Bond||move.target.kind!=ObjectKind::Atom)continue;
                const auto acceptor=move.target.id;
                if(acceptor!=a&&acceptor!=b)continue;
                const auto migrating=acceptor==a?b:a;
                const auto* lost=doc.bonds().alive(move.source.id);
                if(!lost||(lost->beginAtom!=migrating&&lost->endAtom!=migrating))continue;
                const auto donor=lost->beginAtom==migrating?lost->endAtom:lost->beginAtom;
                const auto bridge=edge(acceptor,donor);
                if(!original.count(bridge)||product.at(bridge)<=0||product.at(edge(donor,migrating))!=0)continue;
                placed=placeMigratedRing(acceptor,donor,migrating,placedBonds,product,unit,geometryBudget,positions);
                if(placed)break;
            }
            const double distance=length(positions[a]-positions[b])/unit;
            bool needsLayout=distance<.7||distance>1.35;
            if(!placed&&!needsLayout){
                for(auto id:atoms)if(id!=a&&id!=b&&component[id]==fixed&&pointSegment(positions[id],positions[a],positions[b])<unit*.22)needsLayout=true;
                for(auto link:placedBonds)if(link.first!=a&&link.first!=b&&link.second!=a&&link.second!=b&&
                    component[link.first]==fixed&&segmentsCross(positions[a],positions[b],positions[link.first],positions[link.second]))needsLayout=true;
            }
            if(!placed&&needsLayout&&!placeClosedRing(e.first,placedBonds,unit,geometryBudget,positions))
                return Error(ErrorCode::InvalidGeometry,"成键关系已确定，但新环无法自动排布，请调整反应物的位置。");
            if(arranged&&(placed||needsLayout))*arranged=true;
            continue;
        }
        Vec2 pivot=positions[b];std::map<ObjectId,Vec2> bestPositions;
        auto local=[&](ObjectId id){return component.at(id)==fixed||component.at(id)==moving;};
        std::vector<Edge> localBonds;
        for(auto edge:placedBonds)if(local(edge.first)&&local(edge.second))localBonds.push_back(edge);
        // Concerted steps can join the same two components at both ends.
        // Evaluate all new links in the same pose before committing either.
        std::set<Edge> joining{e.first};
        for(auto link:product)if(link.second>0&&link.first!=e.first&&!original.count(link.first)&&
            !positionedLinks.count(link.first)&&local(link.first.first)&&local(link.first.second)&&
            component.at(link.first.first)!=component.at(link.first.second)){
            joining.insert(link.first);
            localBonds.push_back(link.first);
        }
        const bool concerted=joining.size()>1;
        const auto stretches=concerted?std::vector<double>{.52,.65,.8,1.,1.15,1.30}:std::vector<double>{1.,1.15,1.30};
        const int samples=concerted?24:12;
        double best=1e100;
        // Try the normal length first. Crowded junctions can need a slightly
        // longer new line in a flat drawing, especially two tertiary centers.
        for(double stretch:stretches){
            for(int direction=0;direction<samples;++direction)for(int rotation=0;rotation<samples;++rotation){
                const double turnUnit=6.28318530717958647692/samples;
                double angle=direction*turnUnit,turn=rotation*turnUnit;
                Vec2 anchor=positions[a]+Vec2(std::cos(angle),std::sin(angle))*(unit*stretch);
                std::map<ObjectId,Vec2> trial,pose;
                for(auto id:atoms)if(local(id)){
                    if(component[id]==moving)trial[id]=anchor+rotate(positions[id]-pivot,turn);
                    else pose[id]=positions[id];
                }
                double cost=.001*std::min(rotation,samples-rotation);
                for(auto p:trial)for(auto q:pose){
                    if(joining.count(edge(p.first,q.first)))continue;
                    double distance=length(p.second-q.second)/unit;
                    if(distance<.9)cost+=100*(.9-distance)*(.9-distance);
                    cost+=.01/(.1+distance*distance);
                }
                pose.insert(trial.begin(),trial.end());
                if(substitutionLink){
                    const auto oldDirection=atomRecords.at(substitution->second.leaving).position-atomRecords.at(substitution->first).position;
                    const auto newDirection=pose.at(substitution->second.nucleophile)-pose.at(substitution->first);
                    const double scale=length(oldDirection)*length(newDirection);
                    if(scale>1e-8)cost+=2*(1-dot(oldDirection,newDirection)/scale);
                }
                if(concerted){
                    bool fit=true;
                    for(auto link:joining){
                        const double ratio=length(pose.at(link.first)-pose.at(link.second))/unit;
                        if(ratio<.50||ratio>1.40){fit=false;break;}
                        cost+=8*(ratio-1)*(ratio-1);
                    }
                    if(!fit)continue;
                }
                double collisions=collisionCost(pose,localBonds,unit,geometryBudget);
                if(collisions<0)return Error(ErrorCode::InvalidGeometry,"结构排布搜索量过大，请简化所选反应或手动靠近反应物后再试。");
                if(collisions>0)continue;
                for(const auto& hydroxyl:hydroxyls){
                    if(!local(hydroxyl.first))continue;
                    const auto oxygen=pose.at(hydroxyl.first);
                    const auto neighbor=pose.at(hydroxyl.second)-oxygen;
                    // Reserve the extra glyph on the open side of terminal O.
                    const double side=neighbor.x>length(neighbor)*.5?-1.:1.;
                    const auto hydrogen=oxygen+Vec2(side*labelEm*.72,0);
                    for(auto bond:localBonds)if(bond.first!=hydroxyl.first&&bond.second!=hydroxyl.first){
                        const double gap=pointSegment(hydrogen,pose.at(bond.first),pose.at(bond.second));
                        const double crowding=std::max(0.,.52-gap/std::max(1.,labelEm));
                        cost+=100*crowding*crowding;
                    }
                }
                for(auto old:localBonds){
                    if(joining.count(old))continue;
                    auto junction=old.first==a||old.second==a?a:old.first==b||old.second==b?b:kInvalidObjectId;
                    if(junction==kInvalidObjectId)continue;
                    auto neighbor=old.first==junction?old.second:old.first;
                    Vec2 u=pose.at(neighbor)-pose.at(junction),v=pose.at(junction==a?b:a)-pose.at(junction);
                    double cosine=dot(u,v)/std::max(1e-9,length(u)*length(v));
                    cost+=(cosine+.5)*(cosine+.5);
                }
                if(cost<best){best=cost;bestPositions=trial;}
            }
            if(!concerted&&!bestPositions.empty())break;
        }
        if(bestPositions.empty()){
            bool bridged=false;
            if(joining.size()==2){
                const auto companion=*std::find_if(joining.begin(),joining.end(),[&](Edge link){return link!=e.first;});
                const auto other=component.at(companion.first)==fixed?companion.first:companion.second;
                bridged=placeBridgedRing(edge(a,other),localBonds,unit,geometryBudget,positions);
                if(!bridged){
                    const auto opposite=companion.first==other?companion.second:companion.first;
                    bridged=placeBridgedRing(edge(b,opposite),localBonds,unit,geometryBudget,positions);
                }
            }
            if(!bridged&&!(joining.size()==2&&placeClosedRing(e.first,localBonds,unit,geometryBudget,positions)))
                return Error(ErrorCode::InvalidGeometry,"产物的新键附近过于拥挤，请先调整反应物的布局。");
        }
        for(auto p:bestPositions)positions[p.first]=p.second;
        for(auto link:joining)if(link!=e.first){
            placedBonds.push_back(link);
            positionedLinks.insert(link);
        }
        for(auto id:atoms)if(component[id]==moving)component[id]=fixed;
    }
    alignTrigonalBranches(product,atomRecords,change.charge,unit,geometryBudget,positions,&reacting,&layoutMasses);
    {auto e=projection_reaction::flattenEliminations(doc,product,doc.defaultBondLength(),positions);if(!e.ok())return e;}
    std::map<ObjectId,ObjectId> projectedWedgeLigand;
    for(const auto& reaction:substitutions)if(projection_molecule::marked(doc,reaction.first)){
        auto center=reaction.first;ObjectId spine=kInvalidObjectId;std::vector<ObjectId> roots;
        for(auto bid:doc.incidentBonds(center)){auto b=doc.bonds().alive(bid);auto n=b->beginAtom==center?b->endAtom:b->beginAtom;if(n==reaction.second.leaving)n=reaction.second.nucleophile;
            if(spine==kInvalidObjectId&&projection_molecule::marked(doc,n))spine=n;else roots.push_back(n);
        }if(spine==kInvalidObjectId||roots.size()!=3)continue;
        auto pivot=positions.at(center),axis=positions.at(spine)-pivot;double base=atan2(axis.y,axis.x);
        // Three planar ligands surround the centre; the remaining ligand is
        // wedged between two of them. Avoid a Fischer cross with a single wedge:
        // opposite collinear ligands make 2D wedge interpretation ambiguous.
        for(size_t i=0;i<3;++i){double angle=base+(i==0?120.:i==1?180.:240.)*3.141592653589793/180.;Vec2 target=pivot+Vec2(cos(angle),sin(angle))*doc.defaultBondLength();auto root=roots[i];std::vector<ObjectId> branch{root};std::set<ObjectId> seen{center,root};
            for(size_t j=0;j<branch.size();++j)for(auto e:product)if(e.second>0){ObjectId n=e.first.first==branch[j]?e.first.second:e.first.second==branch[j]?e.first.first:kInvalidObjectId;if(n!=kInvalidObjectId&&n!=center&&seen.insert(n).second)branch.push_back(n);}
            if(seen.count(spine))return stereoError();auto origin=positions[root],from=origin-pivot;double turn=angle-atan2(from.y,from.x);
            for(auto id:branch){auto p=positions[id]-origin;positions[id]=target+rotate(p,turn);}
        }projectedWedgeLigand[center]=roots[1];
    }
    // A bond cleavage changes the connected components. Pack those FINAL
    // components, not the original reactants: transferred H/BH must not remain
    // underneath the implicit-H label on the newly charged carbon. Every move
    // below is a rigid translation, preserving all internal bond geometry.
    std::map<ObjectId,std::vector<ObjectId>> productNeighbors;
    for(auto e:product)if(e.second>0){productNeighbors[e.first.first].push_back(e.first.second);productNeighbors[e.first.second].push_back(e.first.first);}
    std::set<ObjectId> packedSeen;std::vector<std::vector<ObjectId>> productParts;
    for(auto id:atoms)if(packedSeen.insert(id).second){std::vector<ObjectId> part(1,id);for(size_t i=0;i<part.size();++i)for(auto other:productNeighbors[part[i]])if(packedSeen.insert(other).second)part.push_back(other);productParts.push_back(part);}
    if(productParts.size()>1){
        std::stable_sort(productParts.begin(),productParts.end(),[](const std::vector<ObjectId>& a,const std::vector<ObjectId>& b){return a.size()>b.size();});
        double labelSize=std::max(10.,doc.defaultTextSize());
        if(auto setting=static_cast<const Document&>(doc).documentExtensions().findValue("studio.style.LabelSize")){double value=std::atof(setting->c_str());if(std::isfinite(value)&&value>0)labelSize=std::max(labelSize,value);}
        auto labelledBounds=[&](const std::vector<ObjectId>& part){Box box(positions.at(part.front()),positions.at(part.front()));for(auto id:part){
            const auto& a=atomRecords[id];int charge=a.semantics.charge+change.charge[id];
            bool visible=a.element!=6||charge||a.semantics.radical||a.semantics.isotope||a.semantics.showLabel||roles.count(id)||abbreviations.count(id);
            // Hydrogens may be rendered below a carbon at a crowded junction.
            // Symmetric allowance also covers left-oriented heteroatom labels.
            double x=visible?labelSize*(hydrogens[id]>0?1.6:1.):2.;
            double y=visible?labelSize*(hydrogens[id]>0?1.7:1.1):2.;
            box.add(positions[id]-Vec2(x,y));box.add(positions[id]+Vec2(x,y));
        }return box;};
        auto mainBounds=labelledBounds(productParts.front());double cursor=mainBounds.min.x,baseline=(mainBounds.min.y+mainBounds.max.y)*.5;
        const double gap=std::max(labelSize*1.3,unit*.7);
        for(const auto& part:productParts){auto bounds=labelledBounds(part);Vec2 delta(cursor-bounds.min.x,baseline-(bounds.min.y+bounds.max.y)*.5);for(auto id:part)positions[id]=positions[id]+delta;cursor+=bounds.max.x-bounds.min.x+gap;}
    }
    double collisions=collisionCost(positions,placedBonds,unit,geometryBudget);
    if(collisions>0){
        // Ring opening can leave a terminal carbonyl superimposed on a former
        // neighbour. Keep the skeleton fixed and refit only that terminal atom.
        RingLayoutGraph graph(placedBonds);
        for(auto item:graph.neighbors){
            const auto id=item.first;
            if(item.second.size()!=1||atomRecords.at(id).element!=8)continue;
            const auto center=item.second.front();
            if(product.at(edge(id,center))!=2||!reacting.count(center))continue;
            const Vec2 pivot=positions.at(center),old=positions.at(id)-pivot;
            const double span=length(old);
            if(span<1e-8)continue;
            Vec2 best=positions.at(id);double bestCost=collisions,bestAngles=1e100;
            for(int i=1;i<24;++i){
                auto ray=rotate(old,i*std::acos(-1.)/12.);positions[id]=pivot+ray;
                double cost=collisionCost(positions,placedBonds,unit,geometryBudget);
                if(cost<0)return Error(ErrorCode::InvalidGeometry,"产物排布检查量过大，请拆分反应步骤。");
                if(cost>=collisions||cost>bestCost)continue;
                double angles=0;
                for(auto other:graph.neighbors.at(center))if(other!=id){
                    const auto v=positions.at(other)-pivot;
                    const double deviation=dot(ray,v)/std::max(1e-8,span*length(v))+.5;
                    angles+=deviation*deviation;
                }
                if(cost<bestCost||angles<bestAngles){bestCost=cost;bestAngles=angles;best=positions[id];}
            }
            if(arranged&&bestCost<collisions)*arranged=true;
            positions[id]=best;collisions=bestCost;
            if(collisions==0)break;
        }
    }
    if(collisions<0)return Error(ErrorCode::InvalidGeometry,"结构排布检查量过大，请拆分反应步骤。");
    if(collisions>0)return Error(ErrorCode::InvalidGeometry,"产物排布仍有原子重叠或化学键交叉；请先整理反应物布局后重试。");
    // Use a single directed wedge in the product. Prefer the original
    // marked ligand, transferring its slot to the incoming group if needed.
    std::map<Edge,std::pair<ObjectId,int>> productWedges;
    for(const auto& item:inversions){
        const auto center=item.first;
        const auto& inversion=item.second;
        const auto& substitution=substitutions.at(center);
        auto ligands=inversion.ligands;
        std::array<Vec2,4> points;
        for(size_t i=0;i<ligands.size();++i){
            if(ligands[i]==substitution.leaving)ligands[i]=substitution.nucleophile;
            points[i]=positions.at(ligands[i]);
        }
        bool placed=false;
        for(bool preferOriginal:{true,false}){
            for(size_t i=0;i<ligands.size();++i){
                if(ligands[i]==center||bool(inversion.depth[i])!=preferOriginal)continue;
                const auto link=edge(center,ligands[i]);
                const auto old=records.find(link);
                if(productWedges.count(link)||(old!=records.end()&&hasStereoMark(old->second)&&old->second.beginAtom!=center))continue;
                std::array<int,4> depth{};depth[i]=1;
                const auto volume=tetrahedralVolume(points,depth);
                if(std::fabs(volume)<1e-6)continue;
                productWedges.emplace(link,std::make_pair(center,volume*inversion.originalVolume<0?1:-1));
                placed=true;
                break;
            }
            if(placed)break;
        }
        if(!placed)return stereoError();
    }
    // Transfer every surviving projected tetrahedron to an ordinary wedge.
    // The incoming SN2 ligand occupies the leaving slot with inverted parity;
    // spectator centres keep their parity even when the view was Newman.
    for(auto center:atoms)if(projection_molecule::marked(doc,center)){
        const auto& incident=doc.incidentBonds(center);if(incident.size()!=4)continue;
        std::vector<projection_molecule::V3> vectors;std::array<Vec2,4> points;std::array<ObjectId,4> ligands;size_t slot=0;bool tetrahedral=true;
        for(auto bid:incident){auto b=doc.bonds().alive(bid);auto n=b->beginAtom==center?b->endAtom:b->beginAtom;vectors.push_back(projection_molecule::vector(doc,*b,center));
            if(substitutions.count(center)&&n==substitutions.at(center).leaving)n=substitutions.at(center).nucleophile;
            auto found=product.find(edge(center,n));if(found==product.end()||found->second!=1){tetrahedral=false;break;}ligands[slot]=n;points[slot++]=positions.at(n);
        }
        if(!tetrahedral||slot!=4)continue;double desired=-projection_molecule::parity(vectors)*(substitutions.count(center)?-1.:1.);if(std::abs(desired)<1e-8)return stereoError();
        bool done=false;for(size_t i=0;i<4&&!done;++i){auto link=edge(center,ligands[i]);if((projectedWedgeLigand.count(center)&&ligands[i]!=projectedWedgeLigand.at(center))||projection_molecule::marked(doc,ligands[i])||productWedges.count(link))continue;std::array<int,4> depths{};depths[i]=1;double volume=tetrahedralVolume(points,depths);if(std::abs(volume)<1e-8)continue;productWedges[link]={center,volume*desired>0?1:-1};done=true;}
        if(!done)return stereoError();
    }
    double right=-1e100,sourceLeft=1e100,sourceRight=-1e100,top=1e100,bottom=-1e100;
    for(auto id:atoms){auto p=atomRecords[id].position;sourceLeft=std::min(sourceLeft,p.x);sourceRight=std::max(sourceRight,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y);}
    right=sourceRight+12;
    auto overlapsRow=[&](double low,double high){return high>=top-24&&low<=bottom+24;};
    for(const auto& a:doc.atoms().records())if(a.isAlive()&&overlapsRow(a.position.y-12,a.position.y+12))right=std::max(right,a.position.x+12);
    for(const auto& t:doc.texts().records())if(t.isAlive()){double size=std::max(10.,t.size);if(overlapsRow(t.position.y-size,t.position.y+size*.4))right=std::max(right,t.position.x+doc.plainText(t.id).size()*size*.6);}
    for(const auto& a:doc.arrows().records())if(a.isAlive()){double low=1e100,high=-1e100,last=-1e100;for(auto p:a.geometry){low=std::min(low,p.y);high=std::max(high,p.y);last=std::max(last,p.x);}if(overlapsRow(low-4,high+4))right=std::max(right,last+12);}
    for(const auto& p:doc.paths().records())if(p.isAlive()){double low=1e100,high=-1e100,last=-1e100;for(auto pt:p.points){low=std::min(low,pt.y);high=std::max(high,pt.y);last=std::max(last,pt.x);}if(overlapsRow(low-4,high+4))right=std::max(right,last+12);}
    double minX=1e100,minY=1e100,maxY=-1e100;
    for(auto p:positions){minX=std::min(minX,p.second.x);minY=std::min(minY,p.second.y);maxY=std::max(maxY,p.second.y);}
    double corridor=std::max(90.,unit*3.5);Vec2 shift(right+corridor-minX,(top+bottom-minY-maxY)*.5);
    std::map<ObjectId,ObjectId> copied;
    // Preserve only appearance extensions, never old ids, source snapshots,
    // named-structure links, or charge/hydrogen text overrides.
    auto appearance=[&](ObjectRef src,ObjectRef dst){const auto entries=static_cast<const Document&>(doc).extensions(src).entries();
        for(const auto& v:entries)if(v.key.compare(0,16,"studio.graphics.")==0||
            v.key=="studio.color"||v.key=="studio.color.override"||
            (src.kind==ObjectKind::Atom&&(v.key=="studio.text.colors"||v.key=="studio.text.colors.edited")))
            doc.extensions(dst).set(v.key,v.value);
        if(src.kind==ObjectKind::Atom&&dst.kind==ObjectKind::Atom){auto old=doc.atoms().alive(src.id),fresh=doc.atoms().alive(dst.id);if(old&&fresh&&old->semantics.charge&&old->semantics.charge==fresh->semantics.charge)
            for(const auto& value:entries)if(value.key=="pst.attr.MoleculeStudioChargePosition")doc.extensions(dst).set(value.key,value.value);}
    };
    std::map<ObjectId,std::set<int>> usedPairs;
    for(const auto& move:moves){
        if(move.electronCount!=2||move.source.kind!=ObjectKind::Atom)continue;
        const auto id=move.source.id;
        const int count=lonePairCount(doc,id);
        if(!count)continue;
        auto& used=usedPairs[id];
        int index=move.sourceLonePair;
        if(index<0||index>=count||used.count(index)){
            index=0;while(index<count&&used.count(index))++index;
        }
        if(index<count)used.insert(index);
    }
    for(auto id:atoms){auto a=atomRecords[id];a.position=positions[id]+shift;a.sourceId=0;a.semantics.charge+=change.charge[id];if(change.radicals[id]){const int r=electronDeficit(a)+change.radicals[id];a.semantics.radical=r==2?uint8_t(Radical::Singlet):r==1?uint8_t(Radical::Doublet):0;}a.semantics.implicitHydrogens=hydrogens[id];ObjectId fresh;auto err=doc.addAtomRecord(a,&fresh);if(!err.ok())return err;copied[id]=fresh;ObjectRef ref(ObjectKind::Atom,fresh);doc.extensions(ref).set("studio.mechanism.source.atom",std::to_string(id));appearance(ObjectRef(ObjectKind::Atom,id),ref);if(roles.count(id)){doc.setAtomLabel(fresh,roles[id]=="base"?"B":"Nu");doc.extensions(ref).set(genericRoleKey(),roles[id]);}if(created)created->push_back(ref);}
    for(const auto* labels:{&compactPhosphines,&abbreviations})for(const auto& label:*labels){
        const auto id=label.first,fresh=copied.at(id);
        auto err=doc.setAtomLabel(fresh,label.second);
        if(!err.ok())return err;
        const auto& attrs=static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Atom,id));
        for(auto key:{"studio.atom.runs","studio.label.offset","studio.label.text","studio.color",
                      "studio.text.LabelJustification","studio.text.Justification","studio.nickname.fragment",
                      "studio.mechanism.ion.base","studio.mechanism.compact.valence","pst.attr.MoleculeStudioChargePosition",
                      "pst.attr.MoleculeStudioLiteralLabel"})
            if(auto value=attrs.findValue(key))
                doc.extensions(ObjectRef(ObjectKind::Atom,fresh)).set(key,*value);
        if(studioions::compact(doc,fresh)&&doc.atoms().alive(fresh)->semantics.charge&&
           !doc.extensions(ObjectRef(ObjectKind::Atom,fresh)).findValue("pst.attr.MoleculeStudioChargePosition"))
            doc.extensions(ObjectRef(ObjectKind::Atom,fresh)).set("pst.attr.MoleculeStudioChargePosition","auto");
    }
    for(auto e:product)if(e.second>0){BondRecord b;if(records.count(e.first))b=records[e.first];b.sourceId=0;b.beginAtom=copied[e.first.first];b.endAtom=copied[e.first.second];b.order=BondOrder(e.second);
        // Preserve original endpoint order for wedge semantics; changed bonds
        // receive no invented stereochemistry.
        if(records.count(e.first)&&records[e.first].beginAtom==e.first.second)std::swap(b.beginAtom,b.endAtom);
        bool altered=change.bonds[e.first]!=0||change.charge[e.first.first]!=0||change.charge[e.first.second]!=0;
        const auto old=records.find(e.first);
        if(old!=records.end()&&hasStereoMark(old->second))
            altered=inversions.count(old->second.beginAtom)!=0;
        if(altered){b.stereo=BondStereo::None;b.display=BondDisplay::Normal;}
        const auto wedge=productWedges.find(e.first);
        if(wedge!=productWedges.end()){
            const auto center=wedge->second.first;
            b.beginAtom=copied[center];b.endAtom=copied[center==e.first.first?e.first.second:e.first.first];
            const bool up=wedge->second.second>0;
            b.display=up?BondDisplay::Wedge:BondDisplay::WedgeHash;
            b.stereo=up?BondStereo::Up:BondStereo::Down;
        }
        ObjectId fresh;auto err=doc.addBondRecord(b,&fresh);if(!err.ok())return err;ObjectRef ref(ObjectKind::Bond,fresh);if(records.count(e.first))appearance(ObjectRef(ObjectKind::Bond,records[e.first].id),ref);if(created)created->push_back(ref);
    }
    // Surviving annotations keep their offsets. Capacity must be measured
    // after the new bonds exist, since bonding changes the atom's valence.
    for(const auto& entry:copied){
        const auto id=entry.first,fresh=entry.second;
        const int before=lonePairCount(doc,id);
        if(!before)continue;
        const auto offsets=lonePairOffsets(doc,id);
        const auto& used=usedPairs[id];
        const int capacity=lonePairCapacity(doc,fresh);
        int count=0;
        for(int i=0;i<before&&count<capacity;++i){
            if(used.count(i))continue;
            auto offset=offsets.find(i);
            if(offset!=offsets.end())setLonePairOffset(doc,fresh,count,offset->second);
            ++count;
        }
        if(count)doc.extensions(ObjectRef(ObjectKind::Atom,fresh)).set(lonePairKey(),std::to_string(count));
    }
    auto arrow=arrows::resolvePreset(8,Vec2(right+18,(top+bottom)*.5),Vec2(right+corridor-20,(top+bottom)*.5),doc.defaultLineWidth());ObjectId aid;auto err=arrows::addArrow(doc,arrow,doc.defaultLineWidth(),&aid);if(!err.ok())return err;if(created)created->push_back(ObjectRef(ObjectKind::Arrow,aid));
    return Error();
}
} }
