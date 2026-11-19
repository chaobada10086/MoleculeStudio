#pragma once
#include "projection_molecule.hpp"
#include <map>
namespace r62 { namespace projection_reaction {
using namespace core;
typedef std::pair<ObjectId,ObjectId> Edge;
inline Edge edge(ObjectId a,ObjectId b){return std::minmax(a,b);}
inline double triple(projection_molecule::V3 a,projection_molecule::V3 b,projection_molecule::V3 c){return a.x*(b.y*c.z-b.z*c.y)+a.y*(b.z*c.x-b.x*c.z)+a.z*(b.x*c.y-b.y*c.x);}
// A projection is a depiction of tetrahedra. Forming an alkene consumes the
// local tetrahedra: flatten against the departing ligands in anti alignment,
// rather than guessing E/Z from their arbitrary screen coordinates.
inline Error flattenEliminations(const Document& source,const std::map<Edge,int>& product,double unit,std::map<ObjectId,Vec2>& positions){
    std::map<ObjectId,std::vector<ObjectId>> adjacency;for(auto e:product)if(e.second>0){adjacency[e.first.first].push_back(e.first.second);adjacency[e.first.second].push_back(e.first.first);}
    for(auto e:product)if(e.second==2){auto old=source.bonds().alive(source.bondBetween(e.first.first,e.first.second));if(!old||old->order!=BondOrder::Single||!projection_molecule::axis(source,*old))continue;
        ObjectId ends[2]={e.first.first,e.first.second},leaving[2]={kInvalidObjectId,kInvalidObjectId};
        for(int p=0;p<2;++p)for(auto bid:source.incidentBonds(ends[p])){auto b=source.bonds().alive(bid);auto n=b->beginAtom==ends[p]?b->endAtom:b->beginAtom;auto found=product.find(edge(ends[p],n));if(found==product.end()||found->second==0){if(leaving[p]!=kInvalidObjectId)return Error(ErrorCode::InvalidArgument,"投影消除的离去基团不唯一。");leaving[p]=n;}}
        if(leaving[0]==kInvalidObjectId||leaving[1]==kInvalidObjectId)return Error(ErrorCode::InvalidArgument,"投影消除需要明确参与反应的两个离去基团。");
        auto center=(positions[ends[0]]+positions[ends[1]])*.5;Vec2 centers[2]={center-Vec2(unit*.5,0),center+Vec2(unit*.5,0)};
        for(int p=0;p<2;++p){auto c=ends[p];const auto axis=projection_molecule::vector(source,*old,c);auto departure=source.bonds().alive(source.bondBetween(c,leaving[p]));auto lv=projection_molecule::vector(source,*departure,c);int sides=0;
            for(auto root:adjacency[c])if(root!=ends[1-p]){
                auto original=source.bonds().alive(source.bondBetween(c,root));if(!original)return Error(ErrorCode::InvalidArgument,"无法确定投影消除的保留取代基。");
                double side=triple(axis,lv,projection_molecule::vector(source,*original,c));if(std::abs(side)<1e-8)return Error(ErrorCode::InvalidArgument,"投影的立体信息不足以确定消除产物构型。");int sign=side>0?1:-1;if(sides==sign)return Error(ErrorCode::InvalidArgument,"投影的两个保留取代基占据同一立体位置。");sides=sign;
                std::vector<ObjectId> branch{root};std::set<ObjectId> seen{c,root};for(size_t i=0;i<branch.size();++i)for(auto n:adjacency[branch[i]])if(n!=c&&seen.insert(n).second)branch.push_back(n);
                if(seen.count(ends[1-p]))return Error(ErrorCode::InvalidArgument,"环状投影消除需要单独的构型约束。");
                auto from=positions[root]-positions[c];Vec2 to((p?1:-1)*unit*.5,sign*unit*.8660254037844386);double turn=atan2(to.y,to.x)-atan2(from.y,from.x),cs=cos(turn),sn=sin(turn);auto target=centers[p]+to,origin=positions[root];
                for(auto id:branch){auto v=positions[id]-origin;positions[id]=target+Vec2(cs*v.x-sn*v.y,sn*v.x+cs*v.y);}
            }
        }
        positions[ends[0]]=centers[0];positions[ends[1]]=centers[1];
    }return Error();
}
} }
