"""Offline shape geometry, layout, rollback and native fixture."""
from pathlib import Path
import sys, copy, json
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
import ai_assistant as ai
from ai_plugins import draw_plugin as p
from ai_plugins.presentation_shapes import SHAPES

def main():
    output=Path(sys.argv[1]);output.mkdir(parents=True,exist_ok=True)
    p.install_plugin(vars(ai))
    def fresh(): return ai.DrawingTools({'selected':0,'bond_length':14.4})
    e=fresh()
    for i,(key,title,category,points) in enumerate(SHAPES):
        result=e.call('draw_autoshape',{'shape':key,'x':(i%5)*150,'y':(i//5)*100,'width':120,'height':70})
        assert result['ref'] and result['coordinate_system']=='insertion-relative-pt'
        e.call('add_text',{'x':(i%5)*150,'y':(i//5)*100+85,'text':title,'size':9})
    def save(engine,name):
        root=ET.Element('result',ok='true');ET.SubElement(root,'message').text='Shape fixture'
        ET.SubElement(root,'actions').extend(engine.actions)
        (output/name).write_text(ET.tostring(root,encoding='unicode'),encoding='utf-8')
    save(e,'shapes-plan.xml')
    curve=fresh()
    pts=[[0,20],[20,0],[40,20],[60,40],[80,20]]
    result=curve.call('draw_curve',{'points':pts,'width':2})
    assert result['ref'] and [pt.get('control') for pt in curve.actions[-1]]==[None,'true',None,'true',None]
    try:curve.call('draw_curve',{'points':pts[:-1]})
    except ValueError:pass
    else:raise AssertionError('incomplete curve segment accepted')
    assert len(curve.actions)==1
    edges=[{'source':str(i),'target':str(j)} for i,j in [(0,2),(3,5),(1,4),(5,3)]]
    routed=fresh().call('draw_diagram',{'nodes':[{'id':str(i),'text':str(i)} for i in range(6)],'edges':edges})
    lanes=[edge['points'][1][1] for edge in routed['edges']]
    assert lanes[0]==lanes[1] and lanes[2]<lanes[1] and lanes[3]>lanes[0]
    for direction in ('row','column'):
        e=fresh();args={'direction':direction,'nodes':[{'id':'a','text':'样品准备'},{'id':'b','text':'测试合格？','shape':'diamond'},{'id':'c','text':'记录结果','shape':'document'},{'id':'d','text':'完成','shape':'ellipse'}],
                       'edges':[{'source':'a','target':'b'},{'source':'b','target':'c','label':'是'},{'source':'c','target':'d'},{'source':'b','target':'d','label':'否'}]}
        result=e.call('draw_diagram',args)
        assert not e.call('layout_report',{})['overlaps'], 'intentional node text or connector bbox reported as a collision'
        boxes=[n['bounds'] for n in result['nodes'].values()]
        for left,right in zip(boxes,boxes[1:]):
            assert (right[0]-left[2] if direction=='row' else right[1]-left[3])>=result['gap']-.001
        for node in result['nodes'].values():
            b=node['bounds'];t=p._select_one(e,node['text_ref'])[1]
            assert b[0]<=t[0]<t[2]<=b[2] and b[1]<=t[1]<t[3]<=b[3],(b,t)
        save(e,'diagram-'+direction+'-plan.xml')
        again=e.call('draw_diagram',args)
        assert again['bounds'][1]>result['bounds'][3]
        before=copy.deepcopy(e.actions),copy.deepcopy(e.scene),e.created_count
        try:e.call('draw_diagram',{'nodes':[{'id':'a','text':'OK'},{'id':'b','text':'bad','fill':'INVALID'}]})
        except ValueError as exc:assert exc.diagnostics['stage'] and exc.diagnostics['suggestion']
        else:raise AssertionError('invalid color accepted')
        assert [ET.tostring(n) for n in e.actions]==[ET.tostring(n) for n in before[0]] and e.scene==before[1] and e.created_count==before[2]
    print(json.dumps({'shapes':len(SHAPES),'directions':2,'label_containment':True,'existing_content_avoidance':True,'atomic_rollback':True}))

if __name__=='__main__':main()
