import sys,os,json,ctypes as ct,xml.etree.ElementTree as E
from pathlib import Path
from urllib.parse import urlsplit
os.environ['MOLECULE_STUDIO_TEST_EXE']=str(next(Path('out/Studio-r62').glob('*r62.exe')).resolve())
runtime=Path(sys.argv[1]).resolve()
sys.path.insert(0,str(runtime))
import ai_assistant as ai
root=Path(os.environ['LOCALAPPDATA'])/'MoleculeStudio/AI'
config=E.parse(root/'settings.xml').getroot()
endpoint=config.get('endpoint')
assert urlsplit(endpoint).hostname=='api.deepseek.com' and urlsplit(endpoint).scheme=='https'
class Blob(ct.Structure):
    _fields_=[('size',ct.c_uint32),('ptr',ct.c_void_p)]
encrypted=(root/'credential.bin').read_bytes();buf=ct.create_string_buffer(encrypted)
src=Blob(len(encrypted),ct.addressof(buf));out=Blob()
assert ct.windll.crypt32.CryptUnprotectData(ct.byref(src),None,None,None,None,1,ct.byref(out))
req=E.Element('request',mode='draw')
try: E.SubElement(req,'key').text=ct.string_at(out.ptr,out.size).decode()
finally:
    ct.memset(out.ptr,0,out.size);ct.windll.kernel32.LocalFree(ct.c_void_p(out.ptr))
for k,v in [('endpoint',endpoint),('model',config.get('model')),('prompt',Path(sys.argv[3]).read_text(encoding='utf-8'))]:
    E.SubElement(req,k).text=v
E.SubElement(req,'context',selected='0',bond_length='14.4')
E.SubElement(E.SubElement(req,'plugins'),'plugin',name='绘图增强',module='ai_plugins',entry='install_plugin',path='ai_plugins')
orig=ai.request_json
count=0
def traced(url,payload=None,*a,**kw):
    global count
    if url.endswith('/chat/completions'):
        count+=1
        if count>50:raise RuntimeError('test request limit')
    for m in reversed((payload or {}).get('messages',[])):
        if m.get('role')!='tool':break
        val=json.loads(m['content']);print('receipt',str(val.get('error') or val.get('step_results') or list(val)[:3])[:1800],flush=True)
    r=orig(url,payload,*a,**kw)
    if url.endswith('/chat/completions'):
        m=r['choices'][0]['message'];print('round',count,[(c['function']['name'], c['function']['arguments'][:1600]) for c in m.get('tool_calls',[])],flush=True)
    return r
ai.request_json=traced
result=ai.run(req)
E.ElementTree(result).write('work/'+sys.argv[2]+'.xml',encoding='utf-8',xml_declaration=True)
arrows=result.findall('actions/draw_arrow[@style="mechanism"]')
print('message',result.findtext('message'),flush=True)
print('model',result.get('model'),'tokens',result.get('tokens'),'partial',result.get('partial'),'electron_arrows',[(a.get('route'),a.get('bend'),a.get('source_ref'),a.get('target_ref')) for a in arrows])
