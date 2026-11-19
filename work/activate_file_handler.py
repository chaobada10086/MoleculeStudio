from pathlib import Path
import winreg,json,ctypes,sys,hashlib
r=Path(__file__).resolve().parents[1]
exe=Path(sys.argv[1]) if len(sys.argv)>1 else Path(json.loads((r/'out/Studio/manifest.json').read_text(encoding='utf-8'))['exe'])
assert exe.is_file() and exe.parent.resolve()==(r/'out/Studio').resolve()
base=r'Software\Classes\Applications\MoleculeStudio.exe'
backup=r/'work/file-handler-before-r30.json'
if not backup.exists():
 old={}
 for suffix in [r'\shell\open\command',r'\DefaultIcon']:
  try:
   with winreg.OpenKey(winreg.HKEY_CURRENT_USER,base+suffix) as key:old[suffix]=winreg.QueryValueEx(key,'')[0]
  except FileNotFoundError:old[suffix]=None
 backup.write_text(json.dumps(old,ensure_ascii=False,indent=2),encoding='utf-8')
# Keep the user's existing Windows UserChoice. Update only the executable of
# the MoleculeStudio application entry that Windows already selected.
for suffix,value in [(r'\shell\open\command',f'"{exe}" "%1"'),(r'\DefaultIcon',f'"{exe}",0')]:
 with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER,base+suffix,0,winreg.KEY_SET_VALUE) as key:winreg.SetValueEx(key,'',0,winreg.REG_SZ,value)
ctypes.windll.shell32.SHChangeNotify(0x08000000,0,None,None)
size=ctypes.c_ulong(32768);buffer=ctypes.create_unicode_buffer(size.value)
result=ctypes.windll.shlwapi.AssocQueryStringW(0,2,'.cdxml',None,buffer,ctypes.byref(size))
assert result==0 and Path(buffer.value).resolve()==exe.resolve(),(result,buffer.value)
evidence={'extension':'.cdxml','resolved_executable':buffer.value,'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'windows_user_choice_preserved':True}
(r/'work/file-handler-r30.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(evidence,ensure_ascii=False))
