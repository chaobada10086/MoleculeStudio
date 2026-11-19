"""Offline geometry, atomicity and native-action fixture for plugin 1.2."""
import copy
import json
import math
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import ai_assistant as ai
from ai_plugins import draw_plugin as dp
from chemistry_open import Chem, identity, one_molecule, to_cdxml


def engine():
    dp.install_plugin(vars(ai))
    return ai.DrawingTools({'selected': 0, 'bond_length': 14.4, 'font': 'Times New Roman'})


def snapshot(e):
    return ([ET.tostring(n) for n in e.actions], copy.deepcopy(e.scene), e.created_count,
            e.molecules, copy.deepcopy(e.sources), copy.deepcopy(getattr(e, '_plugin_labels', {})))


def fails(e, name, args, path=None):
    before = snapshot(e)
    try:
        e.call(name, args)
    except ValueError as exc:
        assert snapshot(e) == before, (name, 'partial mutation')
        assert {'stage', 'suggestion', 'atoms', 'rings', 'stereocenters'} <= set(exc.diagnostics)
        if path:
            assert exc.diagnostics['path'] == path, exc.diagnostics
        return exc.diagnostics
    raise AssertionError('expected failure: ' + name)


def box(e, ref):
    return dp._select_one(e, ref)[1]


def center(b):
    return ((b[0] + b[2]) / 2, (b[1] + b[3]) / 2)


def main():
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / 'composition-results'
    output.mkdir(parents=True, exist_ok=True)
    e = engine()
    for mode in ('fast', 'rdkit'):
        for desired in (14.4, 22.5):
            normalized = ai.DrawingTools({'selected': 0, 'bond_length': desired})
            result = normalized.call('draw_smiles', {'smiles': 'c1ccccc1', 'layout': mode})
            atoms = {o['ref']: o for o in normalized.scene if o['kind'] == 'atom'}
            lengths = sorted(math.hypot(atoms[o['begin']]['x'] - atoms[o['end']]['x'], atoms[o['begin']]['y'] - atoms[o['end']]['y']) for o in normalized.scene if o['kind'] == 'bond')
            assert abs((lengths[2] + lengths[3]) / 2 - desired) < 1e-5
            assert identity(one_molecule(normalized.actions[-1].text)) == identity(Chem.MolFromSmiles('c1ccccc1'))
    print('PASS RDKit/CoordGen obey document bond length at 14.4 and 22.5 pt')
    specs = [{'spec': 'smiles:CCO', 'label': '1a'}, {'spec': 'smiles:c1ccccc1', 'label': 'long compound label'},
             {'spec': 'smiles:CC(=O)O', 'label': '化合物三'}, {'spec': 'smiles:C[C@H](O)C(=O)O', 'label': '4'}]
    grid = e.call('draw_compounds', {'compounds': specs, 'columns': 2})
    centers = [center(row['bounds']) for row in grid['compounds']]
    assert abs(centers[0][0] - centers[2][0]) < 1e-4
    assert abs(centers[1][0] - centers[3][0]) < 1e-4
    assert abs(centers[0][1] - centers[1][1]) < 1e-4
    assert abs(centers[2][1] - centers[3][1]) < 1e-4
    assert not dp._overlaps(dp._top_groups(e))
    assert grid['coordinate_system'] == 'insertion-relative-pt'
    for row in grid['compounds']:
        assert row['bounds'] == dp._rounded_bounds(box(e, row['ref']))
    before = snapshot(e)
    below = e.call('draw_compounds', {'compounds': [{'spec': 'smiles:CC'}], 'x': grid['bounds'][0], 'y': grid['bounds'][1]})
    assert grid['gap']==12
    assert below['avoided_existing'] and below['bounds'][1] >= grid['bounds'][3] + 15.99
    assert e.scene[:len(before[1])] == before[1]
    fails(e, 'draw_compounds', {'compounds': [specs[0], {'spec': 'smiles:INVALID'}]}, '$.compounds[1].spec')
    fails(e, 'draw_compounds', {'compounds': [{'spec': 'smiles:O', 'lable': 'bad'}]}, '$.compounds[0].lable')
    fails(e, 'draw_scheme', {'steps': [{'reactants': [False], 'products': ['smiles:O']}]}, '$.steps[0].reactants[0]')
    fails(e, 'draw_smiles', {'smiles': 'CC', 'x': float('nan')}, '$.x')
    fails(e, 'draw_compounds', {'compounds': specs, 'columns': True}, '$.columns')
    for layout in ('row', 'column', 'grid'):
        trial = engine().call('draw_compounds', {'compounds': [{'spec': r['spec']} for r in specs], 'layout': layout, 'numbering': False})
        assert not any(r['label_ref'] for r in trial['compounds'])
    print('PASS batch grid alignment, labels, existing-content avoidance, nested validation and full rollback')

    # Move labelled objects without distorting internal molecular geometry.
    e = engine()
    row = e.call('draw_compounds', {'compounds': specs[:2], 'layout': 'row', 'gap': 100})
    first, second = row['compounds']
    anchor_before = copy.deepcopy(e.select({'target': first['ref']}))
    label_before = box(e, second['label_ref'])[:]
    points_before = copy.deepcopy(e.select({'target': second['ref']}))
    moved = e.call('place_relative', {'target': second['ref'], 'anchor': first['ref'], 'position': 'below', 'gap': 40})
    assert abs(moved['bounds'][1] - moved['anchor_bounds'][3] - 40) < 1e-4
    assert e.select({'target': first['ref']}) == anchor_before
    assert abs(box(e, second['label_ref'])[1] - label_before[1] - moved['dy']) < 1e-4
    for old, new in zip(points_before, e.select({'target': second['ref']})):
        assert abs(new['x'] - old['x'] - moved['dx']) < 1e-4
        assert abs(new['y'] - old['y'] - moved['dy']) < 1e-4
    for position in ('left', 'right', 'above', 'below'):
        for align in ('start', 'center', 'end'):
            e.call('place_relative', {'target': second['ref'], 'anchor': first['ref'], 'position': position, 'align': align, 'gap': 100})
    obstacle = e.call('draw_shape', {'shape': 'rectangle', 'x': 150, 'y': -100, 'x2': 250, 'y2': 100})
    moved = e.call('place_relative', {'target': second['ref'], 'anchor': first['ref'], 'position': 'right', 'gap': 100})
    assert moved['avoided_obstacles'] > 0
    assert moved['bounds'][0] >= box(e, obstacle['ref'])[2] + 7.99
    fails(e, 'place_relative', {'target': first['ref'], 'anchor': first['ref'], 'position': 'right'})
    print('PASS all relative directions/alignment, label movement, obstacle avoidance, preserved molecule geometry')

    # Both early and late connector failures must leave no arrow or caption.
    e = engine()
    pair = e.call('draw_compounds', {'compounds': specs[:2], 'layout': 'row', 'gap': 140})
    a, b = [r['ref'] for r in pair['compounds']]
    connector = e.call('connect_objects', {'source': a, 'target': b, 'label': 'step 1', 'style': 'equilibrium'})
    assert connector['arrow_endpoints'][0][0] > box(e, a)[2]
    assert connector['arrow_endpoints'][1][0] < box(e, b)[0]
    assert not dp._overlaps(dp._top_groups(e))
    fails(e, 'connect_objects', {'source': a, 'target': b})  # existing connector is an obstacle
    e.call('delete_objects', {'refs': connector['refs']})
    fails(e, 'connect_objects', {'source': a, 'target': b, 'label': 'far too wide ' * 12})
    e.call('place_relative', {'target': b, 'anchor': a, 'position': 'right', 'gap': 10})
    fails(e, 'connect_objects', {'source': a, 'target': b})
    for position in ('above', 'below', 'left', 'right'):
        e.call('place_relative', {'target': b, 'anchor': a, 'position': position, 'gap': 100})
        arrow = e.call('connect_objects', {'source': a, 'target': b})
        e.call('delete_objects', {'target': arrow['ref']})
    print('PASS boundary connectors, all directions, path collision and late caption failure rollback')

    # Bounds used by subsequent model calls must reflect copy and text edits.
    text = e.call('add_text', {'text': 'x', 'x': 0, 'y': 300})
    changed = e.call('edit_text', {'target': text['ref'], 'text': 'very long label\n第二行', 'size': 20})
    assert changed['bounds'][2] > text['bounds'][2] + 100 and changed['bounds'][3] > 300
    copied = e.call('duplicate_objects', {'target': text['ref'], 'dx': 200, 'dy': 40})
    assert copied['bounds'] == box(e, copied['ref'])
    assert copied['bounds'][0] == changed['bounds'][0] + 200
    print('PASS refreshed text/multiline bounds and copied-object bounds')

    # Produce the exact action stream used for a native editable-canvas check.
    e = engine()
    grid = e.call('draw_compounds', {'compounds': specs, 'columns': 2, 'gap': 100})
    a, b = [r['ref'] for r in grid['compounds'][:2]]
    e.call('place_relative', {'target': b, 'anchor': a, 'position': 'right', 'gap': 140})
    e.call('connect_objects', {'source': a, 'target': b, 'label': 'step 1'})
    for node in e.actions:
        if node.tag == 'molecule':
            parsed = one_molecule(node.text, max_atoms=400)
            assert parsed.GetNumAtoms() > 0
    result = ET.Element('result', {'ok': 'true'})
    actions = ET.SubElement(result, 'actions'); actions.extend(e.actions)
    expected = {kind: sum(o['kind'] == kind for o in e.scene) for kind in ('atom', 'bond', 'text', 'arrow', 'path')}
    ET.SubElement(result, 'expected', {k: str(v) for k, v in expected.items()})
    ET.ElementTree(result).write(output / 'drawing-tools-plan.xml', encoding='utf-8', xml_declaration=True)
    report = e.call('layout_report', {'include_ascii': False})
    (output / 'report.json').write_text(json.dumps({'grid': grid, 'report': report, 'expected': expected}, ensure_ascii=False, indent=2), encoding='utf-8')
    assert not report['overlaps'], report['overlaps']
    print('PASS native fixture:', expected)

    # Exercise the real model-tool exchange without a service or a real key.
    request = ET.Element('request')
    for key, value in {'key': 'offline-test', 'endpoint': 'https://example.invalid',
                       'model': 'offline-model', 'prompt': 'Draw two numbered structures'}.items():
        ET.SubElement(request, key).text = value
    plugins = ET.SubElement(request, 'plugins')
    ET.SubElement(plugins, 'plugin', {'module': 'ai_plugins', 'entry': 'install_plugin', 'version': '1.2.0'})
    exchanges = []
    original_request = ai.request_json
    original_key = getattr(ai, '_ACTIVE_API_KEY', '')
    def mock_request(url, payload=None, key=None, timeout=55):
        assert url.endswith('/chat/completions')
        exchanges.append(copy.deepcopy(payload))
        names = {t['function']['name'] for t in payload['tools']}
        assert {'draw_compounds', 'place_relative', 'connect_objects'} <= names
        if len(exchanges) == 1:
            calls = [{'id': 'bad', 'type': 'function', 'function': {'name': 'draw_compounds', 'arguments': json.dumps({'compounds': [{'spec': 'smiles:CC', 'typo': 1}]})}}]
        elif len(exchanges) == 2:
            error = json.loads(payload['messages'][-1]['content'])
            assert error['staged'] is False and error['diagnostics']['path'] == '$.compounds[0].typo'
            calls = [{'id': 'good', 'type': 'function', 'function': {'name': 'draw_compounds', 'arguments': json.dumps({'compounds': specs[:2], 'layout': 'row'})}}]
        else:
            return {'choices': [{'message': {'role': 'assistant', 'content': 'Complete'}}]}
        return {'choices': [{'message': {'role': 'assistant', 'content': None, 'tool_calls': calls}}]}
    try:
        ai.request_json = mock_request
        result = ai.run(request)
        assert len(result.find('actions').findall('molecule')) == 2
        assert result.findtext('message') == 'Complete' and len(exchanges) == 3
    finally:
        ai.request_json = original_request
        ai._ACTIVE_API_KEY = original_key
    print('PASS actual model-tool protocol, structured error feedback and corrected retry (offline)')


if __name__ == '__main__':
    main()
