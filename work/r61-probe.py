from pathlib import Path as P
import subprocess, os
p=P('work')
for version in ('60',):
    target=p/'r61-probe.exe'
    files=sorted(P('out/Studio-r'+version+'/engine').glob('*.o'))
    includes=(['-I'+str(p),'-Iengine/next/integration'] if version=='57' else [])+['-Iengine','-Istudio']
    args=['C:/tools/mingw64/bin/g++.exe','-std=c++11','-O2','-static',*includes,'work/r61-probe.cpp','-o',target,*files,*('-l'+lib for lib in 'opengl32 glu32 gdiplus gdi32 user32 comdlg32 comctl32 shell32 ole32 oleaut32 uuid imm32 crypt32 d3d11 dxgi d3dcompiler'.split())]
    env=dict(os.environ,PATH='C:/tools/mingw64/bin;'+os.environ['PATH'])
    subprocess.run(args,check=True,env=env)
    result=subprocess.run([target],check=True,capture_output=True,timeout=80)
    print(version,result.stdout.decode('utf8','replace').strip(),flush=True)
