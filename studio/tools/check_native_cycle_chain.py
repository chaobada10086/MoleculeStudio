from pathlib import Path
import sys
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import ai_assistant as ai
from ai_plugins import draw_plugin


def check(path):
    draw_plugin.install_plugin(vars(ai))
    engine=ai.DrawingTools({})
    nodes=[dict(smiles=spec,step=step) for spec,step in zip(
        ['[Pd]','C[Pd]Br','C[Pd]C'],['氧化加成：CH₃Br','转金属：CH₃ZnBr','还原消除：乙烷'])]
    try:
        engine.call('draw_catalytic_cycle',dict(title='rollback',nodes=nodes[:2]+nodes[:1]))
    except ValueError:
        assert not engine.actions and not engine.scene
    else:
        raise AssertionError('duplicate accepted')
    result=engine.call('draw_catalytic_cycle',dict(title='调用链测试 · 省略辅助配体',nodes=nodes))
    assert result['steps']==3 and len(set(result['intermediates']))==3
    cycles=[item for item in engine.actions if item.tag=='catalytic_cycle']
    assert len(cycles)==1 and len(cycles[0].findall('ref'))==3
    assert not any(item.tag=='draw_arrow' for item in engine.actions)
    root=ET.Element('result',ok='true')
    ET.SubElement(root,'actions').extend(engine.actions)
    ET.ElementTree(root).write(path,encoding='utf-8')


if __name__=='__main__':
    check(sys.argv[1])
