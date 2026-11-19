from pathlib import Path as P
import subprocess, os
p=P('work/r59-benchmark');p.mkdir(exist_ok=True)
old=p/'next/integration/session.hpp';old.parent.mkdir(parents=True,exist_ok=True)
s=P('engine/next/integration/session.hpp').read_text(encoding='utf8')
s=s.replace('dragBaseActive||movingBoundary||!deferredDragArrows.empty()||','movingBoundary||')
old.write_text(''.join(l for l in s.splitlines(keepends=True) if not any(x in l for x in ['std::set<ObjectRef> deferredDragArrows;', 'std::vector<size_t> deferredDragSlots;', 'bool dragCatalytic=false;'])),encoding='utf8')
for version in ('57','59'):
    target=p/('drag-r'+version+'.exe')
    files=sorted(P('out/Studio-r'+version+'/engine').glob('*.o'))
    includes=(['-I'+str(p),'-Iengine/next/integration'] if version=='57' else [])+['-Iengine','-Istudio']
    args=['C:/tools/mingw64/bin/g++.exe','-std=c++11','-O2','-static',*includes,'work/r59-bench.cpp','-o',target,*files,*('-l'+lib for lib in 'opengl32 glu32 gdiplus gdi32 user32 comdlg32 comctl32 shell32 ole32 oleaut32 uuid imm32 crypt32 d3d11 dxgi d3dcompiler'.split())]
    env=dict(os.environ,PATH='C:/tools/mingw64/bin;'+os.environ['PATH'])
    subprocess.run(args,check=True,env=env)
    result=subprocess.run([target],check=True,capture_output=True,timeout=80)
    print(version,result.stdout.decode().strip(),flush=True)
