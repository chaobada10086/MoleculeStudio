from pathlib import Path
import re,json,subprocess,concurrent.futures
root=Path.cwd();out=root/'out/Studio';cxx='C:/tools/mingw64/bin/g++.exe'
m=json.loads((out/'manifest.json').read_text())
target=(root/'studio/engine/molecular_render.hpp').resolve()
def depends(file,extra,seen):
    file=file.resolve()
    if file==target:return True
    if file in seen:return False
    seen.add(file)
    for inc in re.findall(r'^\s*#include\s*"([^"]+)"',file.read_text(encoding='utf-8',errors='replace'),re.M):
        for folder in (file.parent,root/'engine',root/'studio',extra):
            dep=folder/inc
            if dep.is_file():
                if depends(dep,extra,seen):return True
                break
    return False
def build(item):
    rel,data=item;src=Path(data['path']);extra=root/'engine'/Path(rel).parent
    obj=out/'engine'/(rel.replace('/','_')+'.o')
    if depends(src,extra,set()):
        obj=root/'work'/('r9-'+rel.replace('/','_')+'.o')
        if not obj.exists() or obj.stat().st_mtime<max(target.stat().st_mtime,src.stat().st_mtime):
            print('compile',rel,flush=True)
            subprocess.run([cxx,'-std=c++11','-O0','-Wa,-mbig-obj','-Iengine','-Istudio','-I'+str(extra),'-c',str(src),'-o',str(obj)],check=True)
    return str(obj)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:objects=list(pool.map(build,m['engine_sources'].items()))
obj=root/'work/r9-dev-main.o'
subprocess.run([cxx,'-std=c++11','-O0','-Wa,-mbig-obj','-Iengine','-Istudio','-DMOLECULE_STUDIO_DX11','-DMOLECULE_STUDIO_DX11_DEFAULT','-c','studio/studio.cpp','-o',str(obj)],check=True)
libs=['opengl32','glu32','gdiplus','gdi32','user32','comdlg32','comctl32','shell32','ole32','oleaut32','uuid','imm32','crypt32','d3d11','dxgi','d3dcompiler']
subprocess.run([cxx,'-static','-mwindows','-o',str(out/'MoleculeStudio-r9-dev.exe'),str(obj),str(out/'app-resource.o')]+objects+['-l'+x for x in libs],check=True)
print('DEV complete',flush=True)
