from pathlib import Path
import json, subprocess, sys
root=Path.cwd();out=root/'out/Studio'
manifest=json.loads((out/'manifest.json').read_text())
cxx='C:/tools/mingw64/bin/g++.exe'
objects=[str(out/'engine'/(rel.replace('/','_')+'.o')) for rel in manifest['engine_sources']]
core=root/'work/r8-dev-core.o'
subprocess.run([cxx,'-std=c++11','-O0','-Iengine','-Istudio','-Iengine/next/core','-c','studio/engine/core_document.cpp','-o',str(core)],check=True)
objects=[str(core) if p.endswith('next_core_document.cpp.o') else p for p in objects]
assert all(Path(p).exists() for p in objects)
obj=root/'work/r8-dev-main.o'
if '--link' not in sys.argv:
    subprocess.run([cxx,'-std=c++11','-O0','-Wa,-mbig-obj','-Iengine','-Istudio','-DMOLECULE_STUDIO_DX11','-DMOLECULE_STUDIO_DX11_DEFAULT','-c','studio/studio.cpp','-o',str(obj)],check=True)
libs=['opengl32','glu32','gdiplus','gdi32','user32','comdlg32','comctl32','shell32','ole32','oleaut32','uuid','imm32','crypt32','d3d11','dxgi','d3dcompiler']
subprocess.run([cxx,'-static','-mwindows','-o',str(out/'MoleculeStudio-r8-native-dev.exe'),str(obj),str(out/'app-resource.o')]+objects+['-l'+x for x in libs],check=True)
print('DEV build complete')
