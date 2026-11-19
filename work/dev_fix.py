from pathlib import Path
p=Path('studio/engine/projection_export.hpp');s=p.read_text(encoding='utf-8-sig').replace('#include <map>','#include <map>\n#include <vector>\n#include <algorithm>');p.write_text(s,encoding='utf-8')
p=Path('work/build-r7-dev.py');s=p.read_text(encoding='utf-8-sig').replace("'-O0','-Iengine'","'-O0','-Wa,-mbig-obj','-Iengine'");p.write_text(s,encoding='utf-8')
