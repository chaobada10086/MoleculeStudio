from pathlib import Path
p=Path('studio/structure_perspective.hpp');s=p.read_text(encoding='utf-8');s=s.replace('    for(auto id:ids){auto a=', '    struct Sample{ObjectId id;Vec2 xy;double z;};std::vector<Sample> samples;\n    for(auto id:ids){auto a=',1)
start=s.index('        auto e=d.setAtomPosition');end=s.index('    for(const auto& b:',start)
s=s[:start]+'''        samples.push_back({id,pivot+Vec2(x,y),z2});lo=std::min(lo,z2);hi=std::max(hi,z2);
    }
    // The ordinary 2D setter invalidates conformer depth. Stage every coordinate
    // first and restore drawing depth only after all XY updates are complete.
    for(auto id:ids)d.extensions(ObjectRef(ObjectKind::Atom,id)).erase("studio.atom.z");
    for(const auto& sample:samples){auto e=d.setAtomPosition(sample.id,sample.xy);if(!e.ok())return e;}
    for(const auto& sample:samples){std::ostringstream out;out.precision(17);out<<sample.z;auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,sample.id));bag.set("studio.atom.z",out.str());bag.erase("pst.attr.MoleculeStudioPerspectiveZ");bag.erase("pst.attr.xyz");}
'''+s[end:];p.write_text(s,encoding='utf-8')
