"""Offline tool-call replay for the mechanism drawing assistant."""
from pathlib import Path
import json
import sys
import xml.etree.ElementTree as ET
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
from ai_plugins import draw_plugin
import ai_dsh


def request(prompt, scene=True):
    req = ET.Element('request', mode='draw')
    for tag, value in [('prompt', prompt), ('key', 'offline'), ('model', 'offline')]:
        ET.SubElement(req, tag).text = value
    if scene:
        nodes = ET.SubElement(req, 'scene')
        for ref, kind, x, element in [('atom:0','atom',0,'C'),('atom:1','atom',14,'O'),('bond:0','bond',7,'')]:
            obj = ET.SubElement(nodes, 'object', ref=ref, kind=kind, molecule='molecule:0',
                                x=str(x), x2=str(x), y='0', y2='0', element=element)
            if kind=='bond':
                obj.attrib.update(begin='atom:0', end='atom:1', order='2')
    return req


def call(name, args):
    return dict(role='assistant',tool_calls=[dict(id='call',type='function',function=dict(name=name,arguments=json.dumps(args)))])


def replay(req, messages):
    stream = iter(messages)
    def reply(url, payload, *args):
        return {'choices':[{'message':next(stream)}]}
    with patch.object(ai, 'request_json', side_effect=reply):
        result = ai.run(req)
    assert next(stream, None) is None
    return result


def test():
    draw_plugin.install_plugin(vars(ai))
    move = {'source':'bond:0','target':'atom:1'}
    req = request('在现有结构上补画机理箭头')
    text = dict(role='assistant',content='已画好。')
    step = call('draw_mechanism_step',{'moves':[move],'generate':False})
    for electrons in (1,2):
        args = {'moves':[{**move,'electrons':electrons}],'generate':False}
        out = replay(req,[text,call('draw_mechanism_step',args),text])
        arrows = out.findall('actions/draw_arrow')
        assert len(arrows)==1 and arrows[0].get('source_ref')=='bond:0' and arrows[0].get('target_ref')=='atom:1'
        assert arrows[0].get('fishhook')==str(electrons==1).lower() and out.find('actions/mechanism_step') is None
    added = replay(req,[call('draw_mechanism_step',{'moves':[move]}),text])
    assert added.find('actions/mechanism_step') is None and added.find('actions/draw_arrow') is not None
    inferred = replay(request('补上机理箭头并生成下一步'),[call('draw_mechanism_step',{'moves':[move]}),text])
    assert inferred.find('actions/mechanism_step') is not None
    for ignored in ([],[call('inspect_document',{})],[call('draw_curved_arrow',{'x':90,'y':80,'x2':150,'y2':105})]):
        out=replay(req,ignored+[text,text])
        assert out.get('partial')=='true' and len(out.find('actions'))==0 and '已画好' not in out.findtext('message')
    retry=replay(req,[call('draw_mechanism_step',{'moves':[move,{**move,'target':'missing'}],'generate':False}),text,step,text])
    assert len(retry.findall('actions/draw_arrow'))==1
    for prompt in ('只解释反应机理','只画总反应式，不用画机理箭头','请解释这幅机理图','把机理箭头改为蓝色','How to draw a curved arrow?'):
        assert not ai.wants_electron_arrows(request(prompt))
        assert replay(request(prompt),[text]).get('partial') is None
    blank=request('画出亲核取代反应机理',False)
    row={'reactants':['smiles:[OH-:1]','smiles:[CH3:2][Br:3]'],'products':['smiles:CO','smiles:[Br-]'],
         'electron_moves':[{'source':'1:@1','target':'2:@2'},{'source':'2:@2-@3','target':'2:@3'}]}
    out=replay(blank,[call('draw_scheme',{'steps':[row]}),text])
    assert len(out.findall('actions/draw_arrow[@style="mechanism"]'))==2 and out.get('partial') is None
    engine=ai.DrawingTools({})
    ai.configure_arrow_request(engine,request('每一步都画机理箭头'))
    second={k:v for k,v in row.items() if k!='electron_moves'}
    rows=engine.call('draw_scheme',{'steps':[row,second]})
    hint=ai.arrow_tool_result(engine,'draw_scheme',rows)['rows_without_electron_arrows']
    assert len(hint)==1 and hint[0]['row']==rows['steps'][1]['arrow']
    assert hint[0]['row'] in ai.missing_arrow_feedback(engine)
    prod=rows['steps'][1]['products'][0]['ref']
    sites=[o['ref'] for o in engine.scene if o.get('owner')==prod and o['kind']=='atom']
    try:
        engine.call('draw_mechanism_step',{'moves':[{'source':sites[0],'target':sites[1]}],'generate':False})
    except ValueError as exc:
        assert '产物列' in str(exc)
    else:
        raise AssertionError('accepted product-side placeholder')
    if len(sys.argv)>1:
        ET.ElementTree(out).write(sys.argv[1],encoding='utf8',xml_declaration=True)
    req.set('advanced','1')
    def advanced(plan,check):
        responses=[{'choices':[{'message':{'content':json.dumps(x)}}]} for x in (plan,check)]
        with patch.object(ai_dsh,'available',return_value=False),patch.object(ai,'request_json',side_effect=responses):
            return ai.run(req)
    first={'plan':[],'message':'已画好。'}
    missing=advanced(first,{'fixes':[],'done':True})
    assert missing.get('partial')=='true' and '已画好' not in missing.findtext('message')
    fixed=advanced(first,{'fixes':[{'tool':'draw_mechanism_step','args':{'moves':[move],'generate':False}}],'done':True})
    assert len(fixed.findall('actions/draw_arrow'))==1
    print('Mechanism tool-call replay passed.')


if __name__=='__main__':
    test()
