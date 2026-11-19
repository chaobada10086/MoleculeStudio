from pathlib import Path
import subprocess,json
r=Path(__file__).resolve().parents[1];out=r/'out/Studio';compiler='C:/tools/mingw64/bin/g++.exe'
sources=json.loads((out/'engine/build_state.json').read_text())
objects=[str(out/'engine'/('_'.join(Path(s).parts)+'.o')) for s in sources]
libs=['opengl32','glu32','gdiplus','gdi32','user32','comdlg32','comctl32','shell32','ole32','oleaut32','uuid','imm32','crypt32','d3d11','dxgi','d3dcompiler']
subprocess.run([compiler,'-static','-std=c++11','-O0','-I'+str(r/'engine'),str(r/'work/arrow_geometry_probe.cpp'),'-o',str(r/'work/arrow-probe.exe')]+objects+['-l'+x for x in libs],check=True)
subprocess.run([str(r/'work/arrow-probe.exe')],cwd=r,check=True)
from PIL import Image,ImageDraw
im=Image.new('RGB',(1160,1760),'white');d=ImageDraw.Draw(im)
for i,row in enumerate(json.loads((r/'work/arrow-probe.json').read_text())):
 for s in row['shafts']:d.line([(x*4,y*4) for x,y in s],fill='black',width=3)
 for h in row['heads']:d.polygon([(x*4,y*4) for x,y in h],fill='black')
 d.text((40,(35+i*48)*4),str(i+1),fill='black')
im.save(r/'work/arrow-probe.png')
