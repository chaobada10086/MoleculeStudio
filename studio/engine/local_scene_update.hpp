#ifndef STUDIO_LOCAL_SCENE_UPDATE_HPP
#define STUDIO_LOCAL_SCENE_UPDATE_HPP
#include <cstdio>
#include <cstdlib>
// A local chemical edit can only change the old/new connected components.
// Preserve all other rendered objects, including their exact painter order.
namespace r62 { namespace integration { namespace local_scene {
struct Update {
    Session* owner=nullptr;
    std::set<ObjectId> atoms,bonds;
    std::set<ObjectRef> graphics;
    bool contains(ObjectRef ref) const {
        return ref.kind==ObjectKind::Atom?atoms.count(ref.id)!=0:
               ref.kind==ObjectKind::Bond?bonds.count(ref.id)!=0:graphics.count(ref)!=0;
    }
};
inline const Update*& pending(){static thread_local const Update* value=nullptr;return value;}
inline bool OrderCache::matches(const Session& s) const {
    return owner==&s&&document==s.doc.id()&&generation==s.scene.generation()&&keys.size()==s.scene.size();
}
inline OrderCache& orderCache(Session& s){return s.sceneOrderCache;}
inline bool plan(Session& s,const ChangeSet& change,Update& update){
    if(!orderCache(s).matches(s)||change.empty()||change.defaults.present){
        if(std::getenv("MOLECULE_STUDIO_PERF_TRACE"))std::fprintf(stderr,"[local] plan fail why=cache cache=%d empty=%d defaults=%d\n",orderCache(s).matches(s)?1:0,change.empty()?1:0,change.defaults.present?1:0);
        return false;}
    const auto& p=change.forward;
    // Standalone graphics do not alter molecular rings, labels or bond clipping.
    // Include both directions so insertion, deletion, undo and redo use the
    // same update, while any mixed or nonlocal edit retains the full rebuild.
    if(!std::getenv("MOLECULE_STUDIO_INSERT_LEGACY")&&p.atomsAfter.empty()&&p.bondsAfter.empty()&&
       p.textsAfter.empty()&&p.tablesAfter.empty()&&p.symbolsAfter.empty()&&p.orbitalsAfter.empty()&&p.bioShapesAfter.empty()&&p.groupsAfter.empty()){
        auto paths=[&](const SlabPatch& patch){
            for(const auto& run:patch.pathsAfter)for(size_t i=0;i<run.records.size();++i)update.graphics.insert(ObjectRef(ObjectKind::Path,run.offset+ObjectId(i)));
            for(const auto& run:patch.arrowsAfter)for(size_t i=0;i<run.records.size();++i)update.graphics.insert(ObjectRef(ObjectKind::Arrow,run.offset+ObjectId(i)));
            for(const auto& run:patch.bracketsAfter)for(size_t i=0;i<run.records.size();++i)update.graphics.insert(ObjectRef(ObjectKind::Bracket,run.offset+ObjectId(i)));
        };paths(p);paths(change.reverse);
        bool compatible=true;for(const auto& ext:change.extensions){if(ext.ref.kind==ObjectKind::Path||ext.ref.kind==ObjectKind::Arrow||ext.ref.kind==ObjectKind::Bracket)update.graphics.insert(ext.ref);else compatible=false;}
        if(compatible&&!update.graphics.empty()){update.owner=&s;return true;}
        update.graphics.clear();
    }
    // Captions, annotations, groups and document properties can have nonlocal
    // dependencies. Keep the normal full rebuild for these operations.
    if(!p.textsAfter.empty()||!p.pathsAfter.empty()||!p.arrowsAfter.empty()||
       !p.bracketsAfter.empty()||!p.orbitalsAfter.empty()||!p.symbolsAfter.empty()||
       !p.bioShapesAfter.empty()||!p.tablesAfter.empty()||!p.groupsAfter.empty()){
        if(std::getenv("MOLECULE_STUDIO_PERF_TRACE"))std::fprintf(stderr,"[local] plan fail why=nonlocal\n");
        return false;}
    size_t count=std::max(s.doc.atoms().size(),size_t(std::max(change.forward.atomsSize,change.reverse.atomsSize)));
    std::vector<std::vector<ObjectId>> edges(count);
    auto addBond=[&](const BondRecord& b){if(b.isAlive()&&b.beginAtom<count&&b.endAtom<count){edges[b.beginAtom].push_back(b.endAtom);edges[b.endAtom].push_back(b.beginAtom);}};
    for(const auto& b:s.doc.bonds().records())addBond(b);
    auto bondRuns=[&](const std::vector<RecordRun<BondRecord>>& runs){for(const auto& run:runs)for(const auto& b:run.records){update.bonds.insert(b.id);if(b.isAlive()){addBond(b);update.atoms.insert(b.beginAtom);update.atoms.insert(b.endAtom);}}};
    // Include both sides: deleting a bridge, undoing insertion or rewiring a
    // bond must invalidate both of the components that used to be connected.
    bondRuns(change.forward.bondsAfter);bondRuns(change.reverse.bondsAfter);
    for(const auto& run:p.atomsAfter)for(size_t i=0;i<run.records.size();++i)update.atoms.insert(run.offset+ObjectId(i));
    for(const auto& run:change.reverse.atomsAfter)for(size_t i=0;i<run.records.size();++i)update.atoms.insert(run.offset+ObjectId(i));
    for(const auto& ext:change.extensions){
        if(ext.ref.kind==ObjectKind::Atom)update.atoms.insert(ext.ref.id);
        else if(ext.ref.kind==ObjectKind::Bond){update.bonds.insert(ext.ref.id);if(auto b=s.doc.bonds().alive(ext.ref.id)){update.atoms.insert(b->beginAtom);update.atoms.insert(b->endAtom);}}
        else return false;
    }
    std::vector<int> beforeDegree(count),afterDegree(count);
    for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&!update.bonds.count(b.id)){++beforeDegree[b.beginAtom];++beforeDegree[b.endAtom];}
    afterDegree=beforeDegree;
    // Unchanged bond records can be in update.bonds because an extension changed.
    std::set<ObjectId> patchedBonds;
    for(const auto& run:p.bondsAfter)for(const auto& b:run.records)patchedBonds.insert(b.id);
    for(const auto& run:change.reverse.bondsAfter)for(const auto& b:run.records)patchedBonds.insert(b.id);
    for(auto id:update.bonds)if(!patchedBonds.count(id))if(auto b=s.doc.bonds().alive(id)){
        ++beforeDegree[b->beginAtom];++beforeDegree[b->endAtom];++afterDegree[b->beginAtom];++afterDegree[b->endAtom];
    }
    for(const auto& run:p.bondsAfter)for(const auto& b:run.records)if(b.isAlive()){++afterDegree[b.beginAtom];++afterDegree[b.endAtom];}
    for(const auto& run:change.reverse.bondsAfter)for(const auto& b:run.records)if(b.isAlive()){++beforeDegree[b.beginAtom];++beforeDegree[b.endAtom];}
    std::map<ObjectId,bool> beforeAlive,afterAlive;
    for(const auto& run:p.atomsAfter)for(const auto& a:run.records)afterAlive[a.id]=a.isAlive();
    for(const auto& run:change.reverse.atomsAfter)for(const auto& a:run.records)beforeAlive[a.id]=a.isAlive();
    for(auto id:update.atoms){
        if(id>=count){if(std::getenv("MOLECULE_STUDIO_PERF_TRACE"))std::fprintf(stderr,"[local] plan fail why=id>=count\n");return false;}
        bool was=id<change.reverse.atomsSize&&(beforeAlive.count(id)?beforeAlive[id]:s.doc.atoms().alive(id)!=nullptr);
        bool now=id<change.forward.atomsSize&&(afterAlive.count(id)?afterAlive[id]:s.doc.atoms().alive(id)!=nullptr);
        if((was&&beforeDegree[id]==0)||(now&&afterDegree[id]==0)){if(std::getenv("MOLECULE_STUDIO_PERF_TRACE"))std::fprintf(stderr,"[local] plan fail why=isolated id=%u was=%d now=%d\n",unsigned(id),was?1:0,now?1:0);return false;}
    }
    // Text runs share an ID namespace with atoms and captions. Be conservative
    // when an edit could also have changed a caption with the same ID.
    for(const auto& run:p.textRunsAfter)if(!update.atoms.count(run.offset)||s.doc.texts().alive(run.offset)){if(std::getenv("MOLECULE_STUDIO_PERF_TRACE"))std::fprintf(stderr,"[local] plan fail why=textrun offset=%u text=%d affected=%d\n",unsigned(run.offset),s.doc.texts().alive(run.offset)?1:0,update.atoms.count(run.offset)?1:0);return false;}
    if(update.atoms.empty()){if(std::getenv("MOLECULE_STUDIO_PERF_TRACE"))std::fprintf(stderr,"[local] plan fail why=no atoms\n");return false;}
    // A changed isolated label can clip an unrelated terminal carbon nearby.
    // This uncommon case takes the full path. Unchanged isolated labels are
    // still included in the local label calculation below.
    for(auto id:update.atoms)if(id>=count||edges[id].empty()){if(std::getenv("MOLECULE_STUDIO_PERF_TRACE"))std::fprintf(stderr,"[local] plan fail why=no edges id=%u\n",unsigned(id));return false;}
    std::vector<ObjectId> todo(update.atoms.begin(),update.atoms.end());
    for(size_t i=0;i<todo.size();++i)for(auto id:edges[todo[i]])if(update.atoms.insert(id).second)todo.push_back(id);
    for(const auto& b:s.doc.bonds().records())if(b.isAlive()&&(update.atoms.count(b.beginAtom)||update.atoms.count(b.endAtom)))update.bonds.insert(b.id);
    update.owner=&s;return true;
}
inline void refresh(Session& s,const ChangeSet& change){
    Update update;
    if(!plan(s,change,update)){
        if(std::getenv("MOLECULE_STUDIO_PERF_TRACE"))std::fprintf(stderr,"[local] fallback to full scene\n");
        s.refresh();return;}
    if(std::getenv("MOLECULE_STUDIO_PERF_TRACE"))std::fprintf(stderr,"[local] partial atoms=%zu bonds=%zu\n",update.atoms.size(),update.bonds.size());
    struct Scope {const Update* old;Scope(const Update* value):old(pending()){pending()=value;}~Scope(){pending()=old;}} scope(&update);
    s.refresh();
}
} } }
#endif
