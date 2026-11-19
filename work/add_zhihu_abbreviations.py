"""Add unambiguous one-attachment groups from the supplied article PDF."""
import json
from pathlib import Path
from rdkit import Chem

root = Path(__file__).resolve().parents[1] / 'studio'
entries = {
    'An': ('[*]c1ccc(OC)cc1', '4-methoxyphenyl (p-anisyl)'),
    'Bip': ('[*]S(=O)(=O)c1ccc(-c2ccccc2)cc1', 'biphenyl-4-sulfonyl'),
    'BOB': ('[*]C(=O)CCCOCc1ccccc1', '4-benzyloxybutyryl'),
    'Chx': ('[*]C1CCCCC1', 'cyclohexyl; alias of Cy'),
    'Dcpm': ('[*]C(C1CC1)C1CC1', 'dicyclopropylmethyl'),
    'EOM': ('[*]COCC', 'ethoxymethyl'),
    'Hmb': ('[*]Cc1c(O)cc(OC)cc1', '2-hydroxy-4-methoxybenzyl'),
    'Nos': ('[*]S(=O)(=O)c1ccc([N+](=O)[O-])cc1', '4-nitrobenzenesulfonyl; distinct from ortho-Ns'),
    'PNZ': ('[*]C(=O)OCc1ccc([N+](=O)[O-])cc1', '4-nitrobenzyloxycarbonyl'),
    'Th': ('[*]c1cccs1', 'thiophen-2-yl'),
}
path = root / 'nicknames.src.json'
data = json.loads(path.read_text(encoding='utf-8'))
for abbreviation, (smiles, note) in entries.items():
    assert abbreviation not in data, abbreviation
    mol = Chem.MolFromSmiles(smiles)
    assert mol is not None and sum(a.GetAtomicNum() == 0 for a in mol.GetAtoms()) == 1, abbreviation
    data[abbreviation] = {'confidence': 'high', 'note': note + '; supplied Zhihu abbreviation table', 'smiles': smiles}
path.write_text(json.dumps(data, ensure_ascii=False, sort_keys=True, indent=2) + '\n', encoding='utf-8')
print(', '.join(entries))
