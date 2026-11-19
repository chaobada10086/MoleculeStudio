from pathlib import Path
p=Path('studio/engine/projection_migration.hpp');s=p.read_text(encoding='utf-8-sig').replace('if(g.isAlive())groups.push_back(g.id);','if(g.isAlive()&&(g.label=="Fischer projection"||g.label=="Sawhorse projection"||g.label=="Newman projection"))groups.push_back(g.id);');p.write_text(s,encoding='utf-8')
