from pathlib import Path
import subprocess,json
r=Path(__file__).resolve().parents[1];out=r/'out/Studio';compiler='C:/tools/mingw64/bin/g++.exe'
args=[compiler,'-std=c++11','-O0','-Wa,-mbig-obj','-I'+str(r/'engine'),'-I'+str(r/'studio'),'-DMOLECULE_STUDIO_DX11','-DMOLECULE_STUDIO_DX11_DEFAULT','-c',str(r/'studio/studio.cpp'),'-o',str(r/'work/arc-debug.o')]
subprocess.run(args,check=True)
sources=json.loads((out/'engine/build_state.json').read_text())
objects=[str(out/'engine'/('_'.join(Path(s).parts)+'.o')) for s in sources]
libs=['opengl32','glu32','gdiplus','gdi32','user32','comdlg32','comctl32','shell32','ole32','oleaut32','uuid','imm32','crypt32','d3d11','dxgi','d3dcompiler']
subprocess.run([compiler,'-static','-mwindows','-o',str(out/'MoleculeStudio-ArcDebug.exe'),str(r/'work/arc-debug.o'),str(out/'app-resource.o')]+objects+['-l'+x for x in libs],check=True)
