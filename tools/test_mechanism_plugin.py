from pathlib import Path
import sys,xml.etree.ElementTree as E
r=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(r/'studio'));import ai_assistant as ai
from ai_plugins import draw_plugin as plug
plug.install_plugin(vars(ai))
e=ai.DrawingTools({'bond_length':30,'color':'#000000','line_width':.6})
e.scene=[{'ref':'atom:1','kind':'atom','x':0,'y':0,'element':'O'}, {'ref':'bond:1','kind':'bond','x':30,'y':0,'x2':50,'y2':20}]
a=e.call('draw_curved_arrow',{'source':'atom:1','target':'bond:1','fishhook':True,'route':False})
assert len(e.actions)==1 and e.actions[0].tag=='draw_arrow'
assert e.actions[0].get('x2')=='40.0' and e.actions[0].get('fishhook')=='true'
assert a['ref']==a['head_ref']
for arg in [{'source':'bad','target':'bond:1'},{'x':0,'y':0,'x2':0,'y2':0},{'x':0,'y':0,'x2':20,'y2':0,'route':'yes'}]:
 before=len(e.actions)
 try:e.call('draw_curved_arrow',arg)
 except ValueError:pass
 else:raise AssertionError(arg)
 assert len(e.actions)==before
print('PASS: native object, exact reference anchors, fishhook, fixed route, atomic invalid input')

# Whole-step failures revert the staged scene, including earlier arrows in this call.
before=([E.tostring(node) for node in e.actions],e.created_count)
try:
    e.call('draw_mechanism_step',{'moves':[{'source':'atom:1','target':'bond:1'},{'source':'missing','target':'bond:1'}]})
except ValueError: pass
else: raise AssertionError('accepted stale endpoint')
assert ([E.tostring(node) for node in e.actions],e.created_count)==before
assert e.actions[0].get('source_ref')=='atom:1' and e.actions[0].get('target_ref')=='bond:1'

def check_native(app):
    import json
    import subprocess
    import tempfile
    from unittest.mock import patch
    app=app.resolve()
    exe=app/Path(json.loads((app/'manifest.json').read_text(encoding='utf-8'))['exe']).name
    request=E.fromstring('<request><model>offline</model><key>offline</key><prompt>生成亲核取代机理</prompt><context bond_length="30"/><scene/></request>')
    for ref,kind,x,x2 in [('atom:0','atom',0,0),('atom:1','atom',30,30),('bond:0','bond',0,30)]:
        E.SubElement(request.find('scene'),'object',ref=ref,kind=kind,x=str(x),y='0',x2=str(x2),y2='0')
    calls=[{'tool':'add_mechanism_reagent','args':{'role':'nucleophile','x':-40,'y':0}},
           {'tool':'draw_mechanism_step','args':{'moves':[{'source':'new:1','target':'atom:0'},{'source':'bond:0','target':'atom:1'}]}}]
    messages=[{'role':'assistant','tool_calls':[{'id':str(i),'type':'function','function':{'name':call['tool'],'arguments':json.dumps(call['args'])}} for i,call in enumerate(calls)]},
              {'role':'assistant','content':'已提交'}]
    def response(message):return {'choices':[{'message':message}]}
    with patch.object(ai,'request_json',side_effect=[response(m) for m in messages]):
        regular=ai.run(request)
    request.set('advanced','1')
    with patch('ai_dsh.available',return_value=False), patch.dict('os.environ',{'MOLECULE_STUDIO_DSH_STUB':''}), patch.object(ai,'request_json',side_effect=[
            response({'content':json.dumps({'plan':calls})}),response({'content':'{"done":true,"fixes":[]}'})]):
        advanced=ai.run(request)
    assert E.tostring(regular.find('actions'))==E.tostring(advanced.find('actions'))
    with tempfile.TemporaryDirectory() as temporary:
        folder=Path(temporary)
        E.ElementTree(regular).write(folder/'mechanism-plan.xml',encoding='utf-8')
        result=subprocess.run([str(exe),'--ai-check',str(folder)],timeout=120)
        if result.returncode:raise AssertionError((folder/'failure.txt').read_text(encoding='utf-8'))
        subprocess.run([str(exe),'--ai-scene',str(folder/'ai-mechanism.cdxml'),str(folder/'reopened.xml')],timeout=60,check=True)
        scene=E.parse(folder/'reopened.xml').findall('scene/object')
        arrows=[o for o in scene if o.get('mechanism')=='true']
        assert len(arrows)==2 and all(o.get('source') and o.get('target') for o in arrows)
        print((folder/'ai-check-progress.txt').read_text(encoding='utf-8').strip())
        print('PASS: normal and advanced request loops, native transaction, CDXML round trip')

if len(sys.argv)==3 and sys.argv[1]=='--app':
    check_native(Path(sys.argv[2]))
