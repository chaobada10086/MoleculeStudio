from pathlib import Path
p=Path('studio/structure_perspective_checks.hpp');s=p.read_text(encoding='utf-8').replace('s.redo()','s.undo(true)');p.write_text(s,encoding='utf-8')
p=Path('studio/structure_perspective_ui.hpp');s=p.read_text(encoding='utf-8').replace('    s.cancel();s.selected.clear();','''#ifdef MOLECULE_STUDIO_DX11
    rotationGpuEligible=false;
#endif
    s.cancel();s.selected.clear();''');p.write_text(s,encoding='utf-8')
