#ifndef STUDIO_RADICAL_ACTIONS_HPP
#define STUDIO_RADICAL_ACTIONS_HPP
// Apply or clear the radical marker on the given atoms. One transaction, one
// undo step. The visual dot is derived from atom.semantics.radical by the scene
// builder, so nothing else has to be created or kept in sync.
void App::applyRadical(const std::vector<ObjectId>& atoms){
    if(atoms.empty())return;
    bool all=true;
    for(auto id:atoms){auto atom=s.doc.atoms().alive(id);if(!atom||atom->semantics.radical==0){all=false;break;}}
    const uint8_t target=all?0:uint8_t(Radical::Doublet);
    auto e=s.submit(chemistry::setRadicalCommand(atoms,target),"自由基");error(e);
    if(e.ok())status=target?"已添加自由基并更新隐含氢 · 再点一次取消":"已取消自由基并更新隐含氢";
    invalidate();
}
#endif
