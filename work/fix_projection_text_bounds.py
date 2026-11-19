from pathlib import Path
p=Path('studio/projection_actions.hpp');s=p.read_text(encoding='utf-8');a=s.index('    double pad=',s.index('// Pick existing captions'));b=s.index('    double best=',a)
s=s[:a]+'''    double pad=s.view().deviceRadiusToDocument(2);
    for(const auto& g:s.doc.groups().records())if(g.isAlive()&&isProjectionGroupName(g.label))
        for(auto ref:g.members)if(ref.kind==ObjectKind::Text&&s.doc.texts().alive(ref.id)){
            Box box;for(const auto& primitive:s.scene.primitives())if(primitive.source==ref){box.add(primitive.bounds.min);box.add(primitive.bounds.max);}
            if(box.empty())box=selection::objectBounds(s.doc,ref);
            if(point.x>=box.min.x-pad&&point.x<=box.max.x+pad&&point.y>=box.min.y-pad&&point.y<=box.max.y+pad){openText(ref.id);return true;}
        }
'''+s[b:];p.write_text(s,encoding='utf-8')
