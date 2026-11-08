"""Build Studio and its matching engine. Header/source fingerprints prevent stale ABI objects.
Use --clean-engine to force recompilation. --exe names the executable in out/Studio.
"""
from pathlib import Path
import subprocess, json, hashlib, sys, concurrent.futures, shutil, os
root=Path(__file__).resolve().parents[1]
base=root/'engine'; snap=base; studio=root/'studio'
out=root/'out/Studio'; engine=out/'engine'; engine.mkdir(parents=True,exist_ok=True)
(out/"ChemistryJobs").mkdir(exist_ok=True)
shutil.copyfile(studio/'chemistry_3d.py',out/'chemistry_3d.py')
shutil.copyfile(studio/'chemistry_open.py',out/'chemistry_open.py')
shutil.copyfile(studio/'ai_assistant.py',out/'ai_assistant.py')
for name in ('ai_dsh.py','dsh_proxy.py'):
    shutil.copyfile(studio/name,out/name)
if (studio/'dsh_overlay.yml').exists():
    overlay_target=out/'ai/overlay.yml'
    overlay_target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(studio/'dsh_overlay.yml',overlay_target)
shutil.copyfile(studio/'ai_attachments.py',out/'ai_attachments.py')
shutil.copyfile(studio/'ai_ocsr_bridge.py',out/'ai_ocsr_bridge.py')
shutil.copyfile(studio/'ai_scene.py',out/'ai_scene.py')
shutil.copyfile(studio/'ai_local_sources.py',out/'ai_local_sources.py')
shutil.copyfile(studio/'ocsr_service.py',out/'ocsr_service.py')
shutil.copyfile(studio/'ocsr_client.py',out/'ocsr_client.py')
subprocess.run([sys.executable,str(studio/'tools/export_ai_ring_catalog.py')],check=True)
subprocess.run([sys.executable,str(studio/'tools/export_ai_tool_reference.py')],check=True)
for name in ('ai_library.py','ai_ring_catalog.json','AI_TOOL_REFERENCE.json','AI_TOOL_API.md','AI 使用说明.md'):shutil.copyfile(studio/name,out/name)
shutil.copyfile(studio/'nicknames.json',out/'nicknames.json')
shutil.copyfile(studio/'nmr_hose.cjs',out/'nmr_hose.cjs')
plugin_source=studio/'ai_plugins'
if (studio/'bio_templates').exists():
    shutil.copytree(studio/'bio_templates',out/'bio_templates',dirs_exist_ok=True)
plugin_target=out/'ai_plugins'
if plugin_source.exists():
    if plugin_target.exists():shutil.rmtree(plugin_target)
    shutil.copytree(plugin_source,plugin_target,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
# Built-in AI drawing skill ships with Core so it can be exported from Settings even
# when no AI addon is installed. The installer is copied next to the executable so the
# manual update button can start the exact same downloader; nothing auto-runs it.
skill_source=root/'skills'/'moleculestudio-draw'
if skill_source.exists():
    skill_target=out/'skills'/'moleculestudio-draw'
    if skill_target.exists():shutil.rmtree(skill_target)
    shutil.copytree(skill_source,skill_target,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
installer_source=root/'dist'/'MoleculeStudio-OnlineSetup.exe'
if installer_source.exists():shutil.copyfile(installer_source,out/'MoleculeStudio-OnlineSetup.exe')
backend=out/'chemistry_backend.ini'
if not backend.exists():backend.write_text('[Backend]\nPython='+sys.executable+'\n',encoding='utf-16')
compiler_arg=(sys.argv[sys.argv.index('--compiler')+1] if '--compiler' in sys.argv else os.environ.get('MOLECULE_STUDIO_CXX', 'g++'))
compiler=shutil.which(compiler_arg)
if not compiler: raise SystemExit('MinGW-w64 g++ not found. Use --compiler C:/path/to/mingw64/bin/g++.exe or set MOLECULE_STUDIO_CXX.')
compiler=str(Path(compiler).resolve())
jobs=int(sys.argv[sys.argv.index('--jobs')+1]) if '--jobs' in sys.argv else min(6,os.cpu_count() or 1)
if jobs<1: raise SystemExit('--jobs must be positive')
if not Path(compiler).with_name('windres.exe').exists(): raise SystemExit('windres.exe must be next to g++.exe')
manifest=json.loads((base/'pre_run_manifest.json').read_text(encoding='utf-8'))
keep={'session.cpp','translate.cpp','native_guard.cpp','ring_geometry.cpp','chemistry_actions.cpp'}
sources=[s.replace(chr(92),'/') for s in manifest['linked_sources']]
sources=[s for s in sources if not s.startswith('tests/') and ('/integration/' not in s or Path(s).name in keep)]
overrides={s:studio/'engine'/Path(s).name for s in sources if (studio/'engine'/Path(s).name).exists()}
overrides.update({'next/core/commands.cpp':studio/'engine/core_commands.cpp','next/core/document.cpp':studio/'engine/core_document.cpp'})
overrides.update({'next/arrows_curves/tools.cpp':studio/'engine/arrows_tools.cpp','next/arrows_curves/preset.cpp':studio/'engine/arrows_preset.cpp'})
overrides['next/arrows_curves/geometry.cpp']=studio/'engine/arrows_geometry.cpp'
overrides['next/arrows_curves/model.cpp']=studio/'engine/arrows_model.cpp'
overrides['next/graphics/render_bridge.cpp']=studio/'engine/graphics_render_bridge.cpp'
overrides['next/render/geometry.cpp']=studio/'engine/render_geometry.cpp'
overrides['next/bonds/geometry.cpp']=studio/'engine/bonds_geometry.cpp'
overrides['next/bonds/adapters.cpp']=studio/'engine/bonds_adapters.cpp'
overrides['next/bonds/spec.cpp']=studio/'engine/bonds_spec.cpp'
overrides['next/graphics/tables.cpp']=studio/'engine/tables.cpp'
overrides['next/experimental/tools.cpp']=studio/'engine/experimental_tools.cpp'
overrides['next/experimental/render_bridge.cpp']=studio/'engine/experimental_render_bridge.cpp'
headers=sorted(list(snap.rglob('*.hpp'))+list(snap.rglob('*.h'))+[studio/'interop.hpp',studio/'arrow_interop.hpp']+list((studio/'engine').glob('*.hpp')))
hash_headers=hashlib.sha256()
for p in headers: hash_headers.update(str(p.relative_to(root)).encode());hash_headers.update(p.read_bytes())
header_digest=hash_headers.hexdigest()
state_path=engine/'build_state.json'
state=json.loads(state_path.read_text()) if state_path.exists() else {}
flags=['-std=c++11','-O2','-I'+str(snap),'-I'+str(studio)]
dx11='--dx11' in sys.argv
studio_flags=flags+((['-DMOLECULE_STUDIO_DX11','-DMOLECULE_STUDIO_DX11_DEFAULT']) if dx11 else [])
records={}
def compile_engine(rel):
    if 'ribbon' in rel or Path(rel).name=='window.cpp':raise RuntimeError('Old UI source: '+rel)
    src=overrides.get(rel,snap/rel); obj=engine/('_'.join(Path(rel).parts)+'.o')
    extra=(studio/'tlc_preferences.hpp').read_bytes() if rel in ('next/experimental/tools.cpp','next/integration/session.cpp','next/experimental/render_bridge.cpp') else ((studio/'tlc_cdxml_writer.inc').read_bytes() if Path(rel).name=='cdxml_writer.cpp' else b'')
    digest=hashlib.sha256(src.read_bytes()+extra+header_digest.encode()+compiler.encode()).hexdigest()
    if '--clean-engine' in sys.argv or state.get(rel)!=digest or not obj.exists():
        result=subprocess.run([compiler]+flags+['-I'+str((snap/rel).parent),'-c',str(src),'-o',str(obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
        if result.returncode:raise RuntimeError(str(src)+'\n'+result.stdout)
    return obj,digest
with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
    built=list(pool.map(compile_engine,sources))
objects=[p for p,digest in built]
state_path.write_text(json.dumps({s:digest for s,(p,digest) in zip(sources,built)},indent=2),encoding='utf-8')
exe=out/('MoleculeStudio-fixed.exe' if '--side-by-side' in sys.argv else ('MoleculeStudio-dx11.exe' if dx11 else 'MoleculeStudio.exe'))
if '--exe' in sys.argv:exe=out/Path(sys.argv[sys.argv.index('--exe')+1]).name
obj=out/'studio.o'; source=studio/'studio.cpp'
result=subprocess.run([compiler]+studio_flags+['-Wall','-Wextra','-Wno-misleading-indentation','-c',str(source),'-o',str(obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'build.log').write_text(result.stdout,encoding='utf-8')
if result.returncode:raise RuntimeError(result.stdout)
libs=['-lopengl32','-lglu32','-lgdiplus','-lgdi32','-luser32','-lcomdlg32','-lcomctl32','-lshell32','-lole32','-loleaut32','-luuid','-limm32','-lcrypt32']
if dx11:libs+=['-ld3d11','-ldxgi','-ld3dcompiler']
resource=out/'app-resource.o'
subprocess.run([str(Path(compiler).with_name('windres.exe')),'-i','app.rc','-o',str(resource),'-O','coff'],cwd=studio,check=True)
result=subprocess.run([compiler,'-static','-mwindows','-o',str(exe),str(obj),str(resource)]+[str(p) for p in objects]+libs,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
with (out/'build.log').open('a',encoding='utf-8') as f:f.write(result.stdout)
if result.returncode:raise RuntimeError(result.stdout)
record={'exe':str(exe),'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'headers_sha256':header_digest,'engine_sources':{s:{'path':str(overrides.get(s,snap/s)),'sha256':hashlib.sha256(overrides.get(s,snap/s).read_bytes()).hexdigest()} for s in sources},'studio_sources':{str(p.relative_to(studio)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source]+list(studio.glob('*.hpp'))},'old_ui_linked':False,'backend_sources':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [studio/'chemistry_open.py',studio/'chemistry_3d.py',studio/'nmr_hose.cjs',studio/'nicknames.json',studio/'ai_assistant.py',studio/'ai_scene.py',studio/'ai_library.py',studio/'ai_ring_catalog.json',studio/'AI_TOOL_REFERENCE.json']}}
(out/'manifest.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
print(exe)
