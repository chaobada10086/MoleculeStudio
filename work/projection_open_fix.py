from pathlib import Path
p=Path('studio/engine/session.cpp');s=p.read_text(encoding='utf-8');s='#include "engine/projection_export.hpp"\n'+s;s=s.replace('bytes=studio_projection_io::normalize(bytes);','bytes=studio_projection_io::normalize(studio_projection_export::strip(bytes));');p.write_text(s,encoding='utf-8')
