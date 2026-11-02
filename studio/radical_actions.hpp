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
    error(s.submit(Command("自由基",kInvalidateAppearance|kInvalidateSemantics,[atoms,target](CommandContext& c){
        for(auto id:atoms){auto atom=c.document.atoms().alive(id);if(!atom)continue;auto semantics=atom->semantics;semantics.radical=target;auto e=c.document.setAtomSemantics(id,semantics);if(!e.ok())return CommandResult::fail(e.code,e.message);}
        return CommandResult::success();
    }),"自由基"));
    status=target?"已添加自由基 · 再点一次取消":"已取消自由基";
    invalidate();
}
#endif
