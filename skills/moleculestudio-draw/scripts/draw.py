"""Generate editable CDXML with the installed Core chemistry engine. No LLM calls."""
import argparse
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET


def fields(obj, allowed, required=()):
    if not isinstance(obj, dict) or set(obj) - set(allowed) or set(required) - set(obj):
        raise ValueError('Unknown or missing fields; allowed: ' + ', '.join(allowed))


def number(obj, key, default=100, low=-200000, high=200000):
    value = obj.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError('Invalid number: ' + key)
    return value


def string(value):
    if not isinstance(value, str) or not value or len(value) > 6000:
        raise ValueError('Text must contain 1–6000 characters')
    if any(not (c in '\t\n\r' or '\x20' <= c <= '\ud7ff' or '\ue000' <= c <= '\ufffd' or '\U00010000' <= c <= '\U0010ffff') for c in value):
        raise ValueError('Invalid XML character')
    return value


def render(app, data):
    fields(data, ('molecules', 'texts', 'arrows'))
    for key in ('molecules', 'texts', 'arrows'):
        if not isinstance(data.get(key, []), list) or len(data.get(key, [])) > 100:
            raise ValueError(key + ' must be an array with at most 100 entries')
    if not any(data.values()):
        raise ValueError('Drawing is empty')
    if not (app / 'chemistry_open.py').is_file() or not (app / 'chemistry_runtime').is_dir():
        raise ValueError('--app must point to the installed Core directory')
    sys.path.insert(0, str(app))
    from chemistry_open import Chem, name_molecule, to_cdxml
    root = ET.Element('CDXML', BondLength='30', LabelFont='3', LabelSize='10', LineWidth='0.6', BondSpacing='18')
    ET.SubElement(ET.SubElement(root, 'fonttable'), 'font', id='3', charset='utf-8', name='Arial')
    page = ET.SubElement(root, 'page', id='1', BoundingBox='0 0 595.276 841.89', WidthPages='1', HeightPages='1')
    counter = 10

    def new_id():
        nonlocal counter
        counter += 1
        return str(counter)

    def text(value, x, y, size=10):
        node = ET.SubElement(page, 't', id=new_id(), p=f'{x:g} {y:g}')
        ET.SubElement(node, 's', font='3', size=str(size), face='0').text = string(value)

    boxes = []
    for item in data.get('molecules', []):
        fields(item, ('smiles', 'name', 'x', 'y', 'label'))
        if ('smiles' in item) == ('name' in item):
            raise ValueError('Provide exactly one of smiles or name')
        mol = Chem.MolFromSmiles(string(item['smiles'])) if 'smiles' in item else name_molecule(string(item['name']))
        if mol is None or not 1 <= mol.GetNumAtoms() <= 400:
            raise ValueError('Invalid molecule or atom count outside 1–400')
        fragment = ET.fromstring(to_cdxml(mol)).find('page/fragment')
        points = [tuple(map(float, node.get('p').split())) for node in fragment.iter('n')]
        left, top = min(p[0] for p in points), min(p[1] for p in points)
        right, bottom = max(p[0] for p in points), max(p[1] for p in points)
        dx = number(item, 'x') - (left + right) / 2
        dy = number(item, 'y') - (top + bottom) / 2
        ids = {node.get('id'): new_id() for node in fragment.iter() if node.get('id')}
        for node in fragment.iter():
            for key in ('id', 'B', 'E'):
                if key in node.attrib:
                    node.set(key, ids[node.get(key)])
            if 'p' in node.attrib:
                x, y = map(float, node.get('p').split())
                node.set('p', f'{x+dx:.8f} {y+dy:.8f}')
        page.append(fragment)
        boxes.append([left+dx, top+dy, right+dx, bottom+dy])
        if 'label' in item:
            text(item['label'], left+dx, bottom+dy+25)
    for item in data.get('texts', []):
        fields(item, ('text', 'x', 'y', 'size'), ('text',))
        text(item['text'], number(item, 'x'), number(item, 'y'), number(item, 'size', 10, 4, 96))
    for item in data.get('arrows', []):
        fields(item, ('x', 'y', 'x2', 'y2'), ('x', 'y', 'x2', 'y2'))
        coords = [number(item, key) for key in ('x', 'y', 'x2', 'y2')]
        if coords[:2] == coords[2:]:
            raise ValueError('Arrow endpoints must differ')
        ET.SubElement(page, 'arrow', id=new_id(), ArrowheadHead='Full', ArrowheadTail='None',
                      ArrowheadType='Solid', HeadSize='1200', ArrowheadWidth='300', LineWidth='0.6',
                      Tail3D=f'{coords[0]:g} {coords[1]:g} 0', Head3D=f'{coords[2]:g} {coords[3]:g} 0')
    return ET.tostring(root, encoding='utf-8', xml_declaration=True), boxes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--overwrite', action='store_true')
    args = parser.parse_args()
    temporary = None
    try:
        if args.output.suffix.lower() != '.cdxml':
            raise ValueError('Output must end in .cdxml')
        if args.output.resolve() == args.input.resolve():
            raise ValueError('Input and output must differ')
        if args.output.exists() and not args.overwrite:
            raise FileExistsError('Output exists; choose another path or use --overwrite')
        if args.input.stat().st_size > 2_000_000:
            raise ValueError('Input is too large')
        data = json.loads(args.input.read_text(encoding='utf-8-sig'))
        payload, boxes = render(args.app.resolve(), data)
        with tempfile.NamedTemporaryFile(dir=args.output.parent, suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(payload)
        if args.overwrite:
            os.replace(temporary, args.output)
        else:
            # Atomic no-clobber publication on the Windows target.
            os.link(temporary, args.output)
        print(json.dumps({'output': str(args.output.resolve()), 'molecules': len(boxes), 'bounds': boxes}, ensure_ascii=True))
        return 0
    except Exception as exc:
        print(json.dumps({'error': str(exc)}, ensure_ascii=True), file=sys.stderr)
        return 2
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    sys.exit(main())
