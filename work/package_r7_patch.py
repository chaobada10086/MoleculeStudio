from pathlib import Path
import difflib
root=Path.cwd();out=Path('C:/Users/ChaobaDa/Documents/Codex/2026-11-16/new-chat-2/outputs');out.mkdir(parents=True,exist_ok=True)
reader=root/'studio/engine/cdxml_reader.cpp';s=reader.read_text(encoding='utf-8')
s=s.replace('#include "engine/projection_export.hpp"\n','').replace('#include "engine/projection_migration.hpp"\n','').replace('studio_projection_io::normalize(studio_projection_export::strip(bytes))','studio_projection_io::normalize(bytes)').replace('    projection_molecule::migrateLegacy(scratch);\n','').replace(',"MoleculeStudioProjection"})','})').replace(',"MoleculeStudioProjectionBegin","MoleculeStudioProjectionEnd"})','})')
(root/'work/r7-backup/cdxml_reader.cpp').write_text(s,encoding='utf-8')
files=['studio/projection_templates.hpp','studio/projection_actions.hpp','studio/projection_checks.hpp','studio/studio.cpp','studio/interop.hpp','studio/chemistry_open.py','studio/cdx_binary.hpp','studio/engine/session.cpp','studio/engine/molecular_render.hpp','studio/engine/cdxml_writer.cpp','studio/engine/cdxml_reader.cpp']
new=['studio/engine/projection_molecule.hpp','studio/engine/projection_migration.hpp','studio/engine/projection_export.hpp']
patch=[]
for name in files+new:
    old=[] if name in new else (root/'work/r7-backup'/Path(name).name).read_text(encoding='utf-8-sig').splitlines(True)
    current=(root/name).read_text(encoding='utf-8-sig').splitlines(True)
    patch.extend(difflib.unified_diff(old,current,fromfile='/dev/null' if name in new else 'a/'+name,tofile='b/'+name,n=3))
(out/'projection-molecule-r7.patch').write_text(''.join(patch),encoding='utf-8')
print('R7 patch:',len(patch),'lines')
