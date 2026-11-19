from pathlib import Path
import json,subprocess
root=Path.cwd();out=root/'out/Studio';m=json.loads((out/'manifest.json').read_text());cxx='C:/tools/mingw64/bin/g++.exe'
objects=[str(out/'engine'/(rel.replace('/','_')+'.o')) for rel in m['engine_sources']]
libs=['opengl32','glu32','gdiplus','gdi32','user32','comdlg32','comctl32','shell32','ole32','oleaut32','uuid','imm32','crypt32','d3d11','dxgi','d3dcompiler']
subprocess.run([cxx,'-std=c++11','-O0','-Wa,-mbig-obj','-Iengine','-Istudio','work/debug_projection_route.cpp','-static','-o','work/debug_projection_route.exe']+objects+['-l'+x for x in libs],check=True)
