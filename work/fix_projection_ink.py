from pathlib import Path
p=Path('studio/projection_actions.hpp');s=p.read_text(encoding='utf-8');a=s.index('    for(const auto& g:',s.index('// Pick existing captions'));b=s.index('    double best=',a)
s=s[:a]+'''    double pad=s.view().deviceRadiusToDocument(2);
    auto hits=s.objectsInBox(Box(point-Vec2(pad,pad),point+Vec2(pad,pad)));
    for(const auto& g:s.doc.groups().records())if(g.isAlive()&&isProjectionGroupName(g.label))
        for(auto ref:g.members)if(ref.kind==ObjectKind::Text&&s.doc.texts().alive(ref.id)&&std::find(hits.begin(),hits.end(),ref)!=hits.end()){openText(ref.id);return true;}
'''+s[b:];p.write_text(s,encoding='utf-8')
p=Path('studio/projection_checks.hpp');s=p.read_text(encoding='utf-8');s=s.replace('auto labelBox=selection::objectBounds(app.s.doc,ObjectRef(ObjectKind::Text,label));\n        auto labelPoint=(labelBox.min+labelBox.max)*.5;', '''Box labelBox;for(const auto& primitive:app.s.scene.primitives())if(primitive.source==ObjectRef(ObjectKind::Text,label)){labelBox.add(primitive.bounds.min);labelBox.add(primitive.bounds.max);}
        require(labelBox.max.x-labelBox.min.x>20,"long caption has measured visible width");
        auto labelPoint=Vec2(labelBox.max.x-1,(labelBox.min.y+labelBox.max.y)*.5);''');p.write_text(s,encoding='utf-8')
