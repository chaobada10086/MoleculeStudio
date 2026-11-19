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
    def reply(url, payload, *args, **kwargs):
        return {'choices':[{'message':next(stream)}]}
    with patch.object(ai, 'request_json', side_effect=reply):
        result = ai.run(req)
    assert next(stream, None) is None
    return result


def test_chained_steps():
    engine = ai.DrawingTools({})
    ai.configure_arrow_request(engine, request('画出每步机理'))
    first = {'reactants':['smiles:BrCCBr','smiles:[OH-]'], 'electron_moves':[
        {'source':'2:1','target':'1:2'}, {'source':'1:1-2','target':'1:1'}]}
    def actions():
        return [ET.tostring(action) for action in engine.actions]
    def receipt(row):
        return ai.arrow_tool_result(engine, 'draw_reaction',row)['step_results'][0]
    sites = engine.call('inspect_reaction_sites', {'reactants': first['reactants']})['reactants']
    assert sites[0]['bonds']['1:1-2'] == 1 and sites[1]['atoms']['2:1'] == ['O', -1]
    assert not engine.actions and not engine.scene
    intermediate = receipt(engine.call('draw_reaction', first))
    assert intermediate['electron_arrows'] == 2 and intermediate['product_source'] == 'inferred'
    product = intermediate['products'][0]
    assert product['spec'] == 'smiles:OCCBr'
    second = {'reactants':[product['next_reactant'], 'smiles:[OH-]'], 'electron_moves':[
        {'source':'2:1','target':'1:3'}, {'source':'1:3-4','target':'1:4'}]}
    saved = actions()
    sites = engine.call('inspect_reaction_sites', {'reactants':second['reactants']})['reactants']
    assert sites[0]['atoms']['1:4'] == ['Br', 0] and actions() == saved
    for tool, args in [('draw_reaction', {**second, 'products':['smiles:C=C']}),
                       ('draw_scheme', {'steps':[first, {**second, 'reactants':['previous:8']}]})]:
        try:
            engine.call(tool, args)
        except ValueError as exc:
            assert ('实际得到' if tool == 'draw_reaction' else '第 2 步') in str(exc)
        else:
            raise AssertionError('invalid step was accepted')
        assert actions() == saved
    final = receipt(engine.call('draw_reaction',second))
    assert [p['spec'] for p in final['products']] == ['smiles:OCCO','smiles:[Br-]']
    batch = engine.call('draw_scheme', {'steps':[first,{**second,'reactants':['previous:1','smiles:[OH-]']}]})
    assert batch['steps'][1]['products'][0]['next_spec'] == 'smiles:OCCO'
    # A failed plan is editable in memory; neither its valid prefix nor a
    # duplicate of it may reach the canvas when the missing row is supplied.
    compact = {'transformation':'[OH-:1].C[CH2:2][Br:3]>>C[CH2:2][OH:1].[Br-:3]'}
    inferred = engine.call('draw_scheme', {'steps':[compact], 'target_product':'smiles:CCO'})
    assert len(inferred['steps'][0]['electron_refs'])==2 and inferred['verified_target']=='smiles:CCO'
    compact_acid = {'transformation':'C[O-:1].[H:2][Cl:3]>>C[O:1][H:2].[Cl-:3]'}
    assert engine.call('draw_scheme', {'steps':[compact_acid], 'target_product':'smiles:CO'})['verified_target']=='smiles:CO'
    compact_carbonyl = {'transformation':'C[C:1](=[O:2])C.[OH-:3]>>C[C:1]([O-:2])([OH:3])C'}
    assert len(engine.call('draw_scheme', {'steps':[compact_carbonyl], 'target_product':'smiles:CC(O)([O-])C'})['steps'][0]['electron_refs'])==2
    implicit_h = {'transformation':'C[O-:1].[ClH:3]>>C[OH:1].[Cl-:3]'}
    checked = engine.call('draw_scheme', {'steps':[implicit_h], 'target_product':'smiles:CO'})
    assert len(checked['steps'][0]['electron_refs'])==2
    first_plan = {'target_product':'smiles:OCCO','steps':[first]}
    saved = actions()
    try:
        engine.call('draw_scheme', first_plan)
    except ValueError as exc:
        assert '末步' in str(exc) and actions()==saved
    else:
        raise AssertionError('accepted a plan ending at the wrong intermediate')
    correction = {'start':2,'remove':0,'steps':[{**second,'reactants':['step:1:1','smiles:[OH-]']}]}
    fixed = engine.call('revise_scheme_plan', correction)
    assert fixed['verified_target']=='smiles:OCCO' and len(fixed['steps'])==2
    assert engine._pending_mechanism is None
    req = request('原料、条件和产物都已给出，画出完整反应机理', False)
    plan = {**first_plan, 'steps':first_plan['steps'] + correction['steps']}
    result = replay(req, [call('draw_scheme', plan)])
    assert len(result.findall('actions/draw_arrow[@style="mechanism"]'))==4
    assert '一次绘制' in result.findtext('message')
    salt = engine.call('draw_reaction', {**first, 'reactants':first['reactants']+['smiles:[Na+]']})
    assert {p['next_spec'] for p in salt['products']} == {'smiles:OCCBr','smiles:[Br-]','smiles:[Na+]'}
    acid = engine.call('draw_reaction', {'reactants':['smiles:[OH-]','smiles:[H]Cl'],
                       'electron_moves':[{'source':'1:1','target':'2:1'},{'source':'2:1-2','target':'2:2'}]})
    assert {p['next_spec'] for p in acid['products']} == {'smiles:O','smiles:[Cl-]'}
    radical = {'reactants':['smiles:ClCl'], 'electron_moves':[
        {'source':'1:1-2','target':site,'electrons':1} for site in ('1:1','1:2')]}
    advanced = ai._advanced_execute(engine,[{'tool':'draw_reaction','args':radical}],1,None)[0]
    assert advanced['ok'] and 'smiles:[Cl]' in advanced['result']
    assert len([a for a in engine.actions if a.get('fishhook') == 'true']) == 2


def test():
    draw_plugin.install_plugin(vars(ai))
    engine = ai.DrawingTools({})
    for row in (1, 2):
        for index in (1, 2):
            engine.scene.append(dict(ref=f'a{row}{index}', kind='atom', owner=f'm{row}',
                x=index*14., y=row*80., x2=index*14., y2=row*80.,
                reaction_row=f'r{row}', reaction_side='reactants'))
    first = dict(source='a11', target='a12')
    for moves, message in (([first, first], '重复'),
                           ([first, dict(source='a21', target='a22')], '同一步')):
        before = [ET.tostring(a) for a in engine.actions]
        try:
            engine.call('draw_mechanism_step', dict(moves=moves, generate=False))
        except ValueError as exc:
            assert message in str(exc)
        else:
            raise AssertionError('invalid arrow batch accepted')
        assert [ET.tostring(a) for a in engine.actions] == before
    engine.reactant_arrows_only = True
    engine.scene[0]['reaction_side'] = 'products'
    engine.scene[1].pop('reaction_row')
    try:
        engine.check_electron_scope('a11', 'a12')
    except ValueError as exc:
        assert '产物列' in str(exc)
    else:
        raise AssertionError('product endpoint accepted')
    test_chained_steps()
    blank = ai.DrawingTools({})
    ai.configure_arrow_request(blank, request('从零画出逐步机理，每步画电子箭头', scene=False))
    try:
        blank.call('draw_reaction', {'reactants':['smiles:CC=O'], 'products':['smiles:CCO']})
    except ValueError:
        assert not blank.actions
    else:
        raise AssertionError('unevidenced step accepted')
    addition = blank.call('draw_reaction', {'reactants':['smiles:C=C[O-]', 'smiles:CC(C)=O'], 'aldol':{}})
    assert len(addition['electron_refs']) == 3 and addition['products'][0]['next_spec'] == 'smiles:CC(C)([O-])CC=O'
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
    for prompt in ('解释给定转化，画全关键中间体和电子转移弯箭头', '逐行临摹完整机理', '写出完整反应机理'):
        engine = ai.DrawingTools({})
        assert ai.configure_arrow_request(engine, request(prompt, False))
        assert engine.require_step_evidence
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
