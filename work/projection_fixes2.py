from pathlib import Path
p=Path('studio/cdx_binary.hpp');s=p.read_text(encoding='utf-8').replace('compare(0,23,"MoleculeStudioProjection")','compare(0,24,"MoleculeStudioProjection")');p.write_text(s,encoding='utf-8')
p=Path('studio/interop.hpp');s=p.read_text(encoding='utf-8').replace('compare(0,29,"pst.attr.MoleculeStudioProject")','compare(0,30,"pst.attr.MoleculeStudioProject")');p.write_text(s,encoding='utf-8')
p=Path('studio/engine/projection_molecule.hpp');s=p.read_text(encoding='utf-8');s=s.replace('#include <set>','#include <set>\n#include <algorithm>\n#include <cstdlib>')
s=s.replace('auto n=b->beginAtom==a.id?b->endAtom:b->beginAtom;auto p=', 'if(d.extensions(ObjectRef(ObjectKind::Bond,bid)).findValue(vectorKey(*b,a.id)))continue;auto n=b->beginAtom==a.id?b->endAtom:b->beginAtom;auto p=')
s=s.replace('inline Error convert(Document& d,ObjectId axisId,int k,int variant){','inline Error convertInPlace(Document& d,ObjectId axisId,int k,int variant){\n    if(k<0||k>2)return Error(ErrorCode::InvalidArgument,"未知投影类型");')
needle='    if(d.incidentBonds(a).size()!=4||d.incidentBonds(b).size()!=4)return Error(ErrorCode::InvalidArgument,"请选择两个四配位碳之间的键；先补齐其取代基");'
assert needle in s
s=s.replace(needle,'''    // Make implicit hydrogens explicit when a one-centre Fischer projection is
    // viewed along a terminal C-C bond. Atom identity and formula stay chemical.
    for(auto c:{a,b}){auto atom=d.atoms().alive(c);if(atom->semantics.nodeType!=uint8_t(NodeType::Element)||atom->semantics.charge||atom->semantics.radical)return Error(ErrorCode::InvalidArgument,"转换轴需为中性饱和碳；请先展开该碳的缩写");auto bonds=d.incidentBonds(c);if(bonds.size()>4)return Error(ErrorCode::InvalidArgument,"投影碳的连接数超过四个");for(auto id:bonds)if(d.bonds().alive(id)->order!=BondOrder::Single)return Error(ErrorCode::InvalidArgument,"投影轴的两个碳需为四面体构型");
        auto position=atom->position;for(size_t i=bonds.size();i<4;++i){ObjectId h,edge;double t=i*2.09439510239;auto e=d.addAtom(position+Vec2(cos(t),sin(t))*d.defaultBondLength(),1,&h);if(!e.ok())return e;e=d.addBond(c,h,&edge);if(!e.ok())return e;}if(!marked(d,c))mark(d,c,0,0);
    }
    seed(d);axisBond=d.bonds().alive(axisId);''')
# Keep stereo metadata for remote centres but stop drawing their former Newman rings after switching the active view axis.
needle='    const double L=d.defaultBondLength();'
s=s.replace(needle,'''    for(auto id:component(d,a))if(id!=a&&id!=b&&marked(d,id))mark(d,id,3,0);
'''+needle)
s=s.replace('}return Error();\n}', '}return Error::success();\n}')
s=s.replace('\n} }','''
// Conversion is atomic even when called outside the UI undo transaction.
inline Error convert(Document& d,ObjectId axisId,int k,int variant){Document candidate=d;auto e=convertInPlace(candidate,axisId,k,variant);if(e.ok())d=std::move(candidate);return e;}
} }''')
p.write_text(s,encoding='utf-8')
