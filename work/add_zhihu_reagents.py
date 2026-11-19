"""Append independently checked single-compound reagent names from the supplied PDF."""
import json
from pathlib import Path
from rdkit import Chem
from rdkit.Chem import rdMolDescriptors

path = Path(__file__).resolve().parents[1] / 'studio/library_additions.json'
records = json.loads(path.read_text(encoding='utf-8'))
source = 'https://zhuanlan.zhihu.com/p/683966633'
new = [
    dict(title='N,N′-二环己基脲', alias='DCU', category='常用试剂及副产物',
         smiles='O=C(NC1CCCCC1)NC1CCCCC1', formula='C13H24N2O', inorganic=False, source=source),
    dict(title='氧化三甲胺', alias='TMAO', category='常用氧化剂',
         smiles='C[N+](C)(C)[O-]', formula='C3H9NO', inorganic=False, source=source),
    dict(title='四甲基脲', alias='TMU', category='常用试剂及溶剂',
         smiles='CN(C)C(=O)N(C)C', formula='C5H12N2O', inorganic=False, source=source),
]
existing = {alias for record in records for alias in record['alias'].split()}
for record in new:
    assert record['alias'] not in existing, record['alias']
    molecule = Chem.MolFromSmiles(record['smiles'])
    assert molecule and rdMolDescriptors.CalcMolFormula(molecule) == record['formula'], record['alias']
    records.append(record)
path.write_text(json.dumps(records, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(', '.join(record['alias'] for record in new))
