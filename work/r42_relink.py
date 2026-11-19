from pathlib import Path
import sys
p=Path('studio/build.py').resolve();s=p.read_text();a=s.index("obj=out/'studio.o'");b=s.index("libs=",a)
s=s[:a]+"obj=out/'studio.o'; source=studio/'studio.cpp'\n"+s[b:]
sys.argv=[str(p),'--dx11','--exe','MoleculeStudio-Progress-20261117-r42.exe','--compiler','C:/tools/mingw64/bin/g++.exe','--jobs','6']
exec(compile(s,str(p),'exec'),{'__file__':str(p),'__name__':'__main__'})
