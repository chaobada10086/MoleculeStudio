from pathlib import Path
p=Path('work/debug_projection_route.cpp');s=p.read_text();s=s.replace('ObjectId base=kInvalidObjectId','for(const auto& ar:s.doc.arrows().records())if(ar.isAlive())s.doc.removeObject(ObjectRef(ObjectKind::Arrow,ar.id));ObjectId base=kInvalidObjectId');p.write_text(s)
