import sys
from pathlib import Path
sys.path.insert(0,str(Path('out/Studio').resolve()))
from chemistry_open import one_molecule,identity,stereo_labels
xml='''<CDXML BondLength="20"><page id="1"><fragment id="2"><n id="3" p="0 0" MoleculeStudioProjection="0"/><n id="4" p="0 -20" NodeType="Nickname"><t><s>CHO</s></t></n><n id="5" p="0 20" NodeType="Nickname"><t><s>CH2OH</s></t></n><n id="6" p="-20 0" Element="1"/><n id="7" p="20 0" NodeType="Nickname"><t><s>OH</s></t></n><b id="8" B="3" E="4" Order="1" MoleculeStudioProjectionBegin="0 1 -1"/><b id="9" B="3" E="5" Order="1" MoleculeStudioProjectionBegin="0 -1 -1"/><b id="10" B="3" E="6" Order="1" MoleculeStudioProjectionBegin="-1 0 1"/><b id="11" B="3" E="7" Order="1" MoleculeStudioProjectionBegin="1 0 1"/></fragment></page></CDXML>'''
m=one_molecule(xml)
print(identity(m))
print([(a.GetIntProp('_STUDIO_CDX_ID'),a.GetProp('_CIPCode')) for a in m.GetAtoms() if a.HasProp('_CIPCode')])
