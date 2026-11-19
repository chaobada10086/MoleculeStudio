import sys
import xml.etree.ElementTree as ET
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
from ai_plugins import draw_plugin


def test():
    draw_plugin.install_plugin(vars(ai))
    engine=ai.DrawingTools({})
    nodes=[dict(metal='Ni',oxidation=state,ligands=labels,step=name) for state,labels,name in
           [('0',['L','L'],'氧化加成'),('II',['Ar','X','L','L'],'转金属'),('II',['Ar','Me','L','L'],'还原消除')]]
    nodes[0]['incoming_smiles']='Brc1ccccc1'
    primary=dict(title='主环',nodes=nodes)
    secondary=dict(title='辅环',nodes=nodes,coupling=dict(parent_edge=2,couple_edge=3))
    try:
        engine.call('draw_cycle_atlas',dict(cycles=[primary,{**secondary,'coupling':dict(parent_edge=8,couple_edge=3)}]))
    except ValueError:
        assert not engine.actions and not engine.scene
    else:
        raise AssertionError('invalid junction accepted')
    engine.call('draw_cycle_atlas',dict(cycles=[primary,secondary]))
    cycles=[action for action in engine.actions if action.tag=='catalytic_cycle']
    assert len(cycles)==2
    for cycle in cycles:
        branch=cycle.find('branch')
        owner=branch.get('partner_ref').split('/')[0]
        assert any(item.get('handle')==owner for item in engine.actions[:engine.actions.index(cycle)])
    assert float(cycles[0].get('rotation'))==-90 and cycles[1].get('couple_edge')=='3'
    req=ET.fromstring('<request><prompt>绘制催化循环，不需要孤对电子箭头</prompt></request>')
    assert not ai.wants_electron_arrows(req)
    print('atlas rollback, coupling orientation, partner order and prompt routing passed')


if __name__=='__main__':test()
