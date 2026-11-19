#pragma once
#include "metal_expansion_layout.hpp"
Error cleanExpandedGeometry(HWND owner,Document& doc,const std::vector<ObjectRef>& expanded,bool* changed,const std::set<ObjectId>& metalCenters){
 bool metalChanged=false;auto protectedMetals=metal_expansion_layout::run(doc,expanded,metalCenters,&metalChanged);if(changed)*changed=metalChanged;
 using namespace r62;using namespace core;studio_local_cleanup::Scope local;for(auto ref:expanded)if(ref.kind==ObjectKind::Atom&&doc.atoms().alive(ref.id))local.movable.insert(ref.id);if(local.movable.empty())return Error();
 std::vector<ObjectId> queue(local.movable.begin(),local.movable.end());local.atoms=local.movable;for(size_t i=0;i<queue.size();++i)for(auto bid:doc.incidentBonds(queue[i])){auto b=doc.bonds().alive(bid);auto n=b->beginAtom==queue[i]?b->endAtom:b->beginAtom;if(local.atoms.insert(n).second)queue.push_back(n);}
 for(const auto& b:doc.bonds().records())if(b.isAlive()&&mechanism::step_detail::hasStereoMark(b))for(auto id:{b.beginAtom,b.endAtom}){local.movable.erase(id);for(auto bid:doc.incidentBonds(id)){auto e=doc.bonds().alive(bid);local.movable.erase(e->beginAtom);local.movable.erase(e->endAtom);}}
 // Isolated expanded compounds need a frame as well. Anchor one authored edge;
 // attached groups instead use the entire original unselected skeleton.
 if(local.atoms.size()-local.movable.size()<2){for(const auto& b:doc.bonds().records())if(b.isAlive()&&local.atoms.count(b.beginAtom)&&local.atoms.count(b.endAtom)){local.movable.erase(b.beginAtom);break;}}
 for(auto id:local.atoms)if(protectedMetals.count(id)||studio::metal_labels::find(core::elementSymbol(doc.atoms().alive(id)->element)).valid())local.movable.erase(id);
 if(local.movable.empty())return Error();bool updated=false;studio_local_cleanup::Request request;
 if(studio_local_cleanup::request(doc,local,request)){std::string output;if(chemservice::run(owner,request.xml,"text/xml","text/xml","clean",&output)){updated=studio_local_cleanup::apply(doc,request,output);}else if(output=="已取消化学服务")return Error(ErrorCode::InvalidArgument,"已取消展开，原结构保持不变");}
 if(!updated){auto q=studio_local_cleanup::quality(doc,local);if(q.crowd>1e-6||q.shape>.2)updated=studio_local_cleanup::relax(doc,local);}updated=studio_local_cleanup::straightenIsocyanates(doc,local.movable)||updated;if(changed)*changed=updated||metalChanged;return Error();
}
