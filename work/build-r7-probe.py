from pathlib import Path
import subprocess,json,sys
root=Path.cwd();out=root/'out/Studio';manifest=json.loads((out/'manifest.json').read_text());cxx='C:/tools/mingw64/bin/g++.exe'
objects=[]
for rel,entry in manifest['engine_sources'].items():
    obj=out/'engine'/(rel.replace('/','_')+'.o')
    if Path(entry['path']).name in ('cdxml_reader.cpp','cdxml_writer.cpp','session.cpp'):
        obj=root/'work'/('r7-dev-'+Path(entry['path']).stem+'.o')
        if Path(entry['path']).name!='session.cpp' or not obj.exists() or Path(entry['path']).stat().st_mtime>obj.stat().st_mtime:
            subprocess.run([cxx,'-std=c++11','-O0','-Wa,-mbig-obj','-Iengine','-Istudio','-I'+str((root/'engine'/rel).parent),'-c',entry['path'],'-o',str(obj)],check=True)
    assert obj.exists(),obj
    objects.append(str(obj))
obj=root/'work/r7-probe.o'
if True:
    subprocess.run([cxx,'-std=c++11','-O0','-Wa,-mbig-obj','-Iengine','-Istudio','-DMOLECULE_STUDIO_DX11','-DMOLECULE_STUDIO_DX11_DEFAULT','-c','work/projection_read_probe.cpp','-o',str(obj)],check=True)
libs=['opengl32','glu32','gdiplus','gdi32','user32','comdlg32','comctl32','shell32','ole32','oleaut32','uuid','imm32','crypt32','d3d11','dxgi','d3dcompiler']
subprocess.run([cxx,'-static','-o',str(root/'work/projection-read-probe.exe'),str(obj),str(out/'app-resource.o')]+objects+['-l'+x for x in libs],check=True)
print('DEV build complete')
