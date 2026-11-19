"""Add two single-attachment alkyl groups checked against PDF pages 24 and 27."""
import json
from pathlib import Path

from rdkit import Chem

path = Path(__file__).resolve().parents[1] / "studio" / "nicknames.src.json"
data = json.loads(path.read_text(encoding="utf-8"))
entries = {
    "Sia": ("[*]C(C)C(C)C", "1,2-dimethylpropyl (secondary isoamyl)"),
    "thexyl": ("[*]C(C)(C)C(C)C", "1,1,2-trimethylpropyl"),
}
for abbreviation, (smiles, note) in entries.items():
    assert abbreviation not in data
    mol = Chem.MolFromSmiles(smiles)
    assert mol is not None and sum(atom.GetAtomicNum() == 0 for atom in mol.GetAtoms()) == 1
    data[abbreviation] = {
        "confidence": "high",
        "note": note + "; visually checked against supplied PDF",
        "smiles": smiles,
    }
path.write_text(json.dumps(data, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8")
