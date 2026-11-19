from pathlib import Path
p=Path('studio/engine/cdxml_reader.cpp');s=p.read_text(encoding='utf-8');s='#include "engine/projection_migration.hpp"\n'+s;s=s.replace('    resolveProjectionGroups(input,scratch);','    resolveProjectionGroups(input,scratch);\n    projection_molecule::migrateLegacy(scratch);');p.write_text(s,encoding='utf-8')
