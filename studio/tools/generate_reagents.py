"""Build editable reagent templates and their shared library index."""
from pathlib import Path
import json
import math
import re
import xml.etree.ElementTree as ET
import sys
import copy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from rdkit import Chem
from rdkit.Chem import rdDepictor, rdMolDescriptors

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    "https://organicchemistrydata.org/hansreich/resources/named_reagents/",
    "https://organicchemistrydata.org/hansreich/resources/redox/",
    "https://www.acs.org/content/dam/acsorg/education/students/highschool/chemistryclubs/infographics/reductions-in-organic-chemistry.pdf",
    "https://orgsyn.org/Content/pdfs/procedures/cv5p0414.pdf",
    "https://organicchemistrydata.org/hansreich/resources/acronyms/",
    "https://www.sigmaaldrich.com/US/en/technical-documents/technical-article/chemistry-and-synthesis/peptide-synthesis/peptide-coupling-reagents-selection-guide",
]
INORGANIC = """
高锰酸钾|KMnO4|氧化剂|[K+].[O-][Mn](=O)(=O)=O
重铬酸钾|K2Cr2O7|氧化剂|[K+].[K+].[O-][Cr](=O)(=O)O[Cr](=O)(=O)[O-]
高碘酸钠|NaIO4|氧化剂|[Na+].[O-][I](=O)(=O)=O
高碘酸|HIO4|氧化剂|O[I](=O)(=O)=O
四氧化锇|OsO4|氧化剂|O=[Os](=O)(=O)=O
二氧化硒|SeO2|氧化剂|O=[Se]=O
三氧化铬|CrO3|氧化剂|O=[Cr](=O)=O
二氧化锰|MnO2|氧化剂|O=[Mn]=O
四氧化钌|RuO4|氧化剂|O=[Ru](=O)(=O)=O
过氧化氢|H2O2|氧化剂|OO
臭氧|O3|氧化剂|[O-][O+]=O
过硫酸钾|K2S2O8|氧化剂|[K+].[K+].[O-]S(=O)(=O)OOS(=O)(=O)[O-]
次氯酸钠|NaClO|氧化剂|[Na+].[O-]Cl
氯酸钠|NaClO3|氧化剂|[Na+].[O-]Cl(=O)=O
高氯酸钾|KClO4|氧化剂|[K+].[O-]Cl(=O)(=O)=O
溴酸钾|KBrO3|氧化剂|[K+].[O-]Br(=O)=O
氯化亚锡|SnCl2|还原剂|Cl[Sn]Cl
硼氢化钠|NaBH4|还原剂|[Na+].[BH4-]
硼氢化锂|LiBH4|还原剂|[Li+].[BH4-]
氢化铝锂|LiAlH4|还原剂|[Li+].[AlH4-]
氰基硼氢化钠|NaBH3CN|还原剂|[Na+].[BH3-]C#N
连二亚硫酸钠|Na2S2O4|还原剂|[Na+].[Na+].[O-]S(=O)S(=O)[O-]
碘化钐|SmI2|还原剂|[Sm+2].[I-].[I-]
氯化亚铬|CrCl2|还原剂|[Cr+2].[Cl-].[Cl-]
锌|Zn|还原剂|[Zn]
铁|Fe|还原剂|[Fe]
镁|Mg|还原剂|[Mg]
氢气|H2|还原剂|[H][H]
氢化钠|NaH|碱|[Na+].[H-]
氢化钾|KH|碱|[K+].[H-]
氢氧化钠|NaOH|碱|[Na+].[OH-]
氢氧化钾|KOH|碱|[K+].[OH-]
碳酸钾|K2CO3|碱|[K+].[K+].[O-]C(=O)[O-]
碳酸铯|Cs2CO3|碱|[Cs+].[Cs+].[O-]C(=O)[O-]
碳酸钠|Na2CO3|碱|[Na+].[Na+].[O-]C(=O)[O-]
碳酸氢钠|NaHCO3|碱|[Na+].OC(=O)[O-]
氨基钠|NaNH2|碱|[Na+].[NH2-]
盐酸|HCl|酸与路易斯酸|Cl
氢溴酸|HBr|酸与路易斯酸|Br
氢碘酸|HI|酸与路易斯酸|I
硫酸|H2SO4|酸与路易斯酸|OS(=O)(=O)O
硝酸|HNO3|酸与路易斯酸|O[N+](=O)[O-]
磷酸|H3PO4|酸与路易斯酸|OP(=O)(O)O
三氟化硼|BF3|酸与路易斯酸|FB(F)F
三氯化铝|AlCl3|酸与路易斯酸|Cl[Al](Cl)Cl
四氯化钛|TiCl4|酸与路易斯酸|Cl[Ti](Cl)(Cl)Cl
四氯化锡|SnCl4|酸与路易斯酸|Cl[Sn](Cl)(Cl)Cl
氯化锌|ZnCl2|酸与路易斯酸|Cl[Zn]Cl
三氯化铁|FeCl3|酸与路易斯酸|[Fe+3].[Cl-].[Cl-].[Cl-]
氯化亚铜|CuCl|金属盐与卤化剂|[Cu+].[Cl-]
碘化亚铜|CuI|金属盐与卤化剂|[Cu+].[I-]
氯化钯|PdCl2|金属盐与卤化剂|Cl[Pd]Cl
锇酸钾|K2OsO4|金属盐与卤化剂|[K+].[K+].[O-][Os](=O)(=O)[O-]
氯化铈|CeCl3|金属盐与卤化剂|[Ce+3].[Cl-].[Cl-].[Cl-]
硝酸银|AgNO3|金属盐与卤化剂|[Ag+].[O-][N+](=O)[O-]
叠氮化钠|NaN3|亲核盐|[Na+].[N-]=[N+]=[N-]
氰化钠|NaCN|亲核盐|[Na+].[C-]#N
碘化钠|NaI|亲核盐|[Na+].[I-]
氟化钾|KF|亲核盐|[K+].[F-]
氯化锂|LiCl|亲核盐|[Li+].[Cl-]
氯气|Cl2|金属盐与卤化剂|ClCl
溴|Br2|金属盐与卤化剂|BrBr
碘|I2|金属盐与卤化剂|II
亚硫酰氯|SOCl2|金属盐与卤化剂|O=S(Cl)Cl
三氯氧磷|POCl3|金属盐与卤化剂|O=P(Cl)(Cl)Cl
三溴化磷|PBr3|金属盐与卤化剂|BrP(Br)Br
五氯化磷|PCl5|金属盐与卤化剂|ClP(Cl)(Cl)(Cl)Cl
"""
ORGANIC = """
三苯基膦|PPh3 Ph3P|膦与偶联试剂|P(c1ccccc1)(c1ccccc1)c1ccccc1
偶氮二甲酸二乙酯|DEAD|膦与偶联试剂|CCOC(=O)N=NC(=O)OCC
偶氮二甲酸二异丙酯|DIAD|膦与偶联试剂|CC(C)OC(=O)N=NC(=O)OC(C)C
二环己基碳二亚胺|DCC|膦与偶联试剂|C1CCC(N=C=NC2CCCCC2)CC1
二异丙基碳二亚胺|DIC|膦与偶联试剂|CC(C)N=C=NC(C)C
碳二亚胺盐酸盐|EDCI EDC-HCl|膦与偶联试剂|CCN=C=NCCC[NH+](C)C.[Cl-]
羰基二咪唑|CDI|膦与偶联试剂|O=C(n1ccnc1)n1ccnc1
羟基苯并三唑|HOBt|膦与偶联试剂|On1nnc2ccccc21
三乙胺|Et3N|有机碱|CCN(CC)CC
二异丙基乙胺|DIPEA Hunig|有机碱|CCN(C(C)C)C(C)C
吡啶|Py|有机碱|n1ccccc1
二甲氨基吡啶|DMAP|有机碱|CN(C)c1ccncc1
二氮杂双环十一碳烯|DBU|有机碱|C1CCC2=NCCCN2CC1
二氮杂双环壬烯|DBN|有机碱|C1CC2=NCCCN2C1
二氮杂双环辛烷|DABCO|有机碱|C1CN2CCN1CC2
二异丙基氨基锂|LDA|有机碱|[Li+].CC(C)[N-]C(C)C
双三甲硅基氨基锂|LiHMDS|有机碱|[Li+].C[Si](C)(C)[N-][Si](C)(C)C
双三甲硅基氨基钠|NaHMDS|有机碱|[Na+].C[Si](C)(C)[N-][Si](C)(C)C
叔丁醇钾|KOtBu tBuOK|有机碱|[K+].CC(C)(C)[O-]
甲醇钠|NaOMe|有机碱|[Na+].C[O-]
乙醇钠|NaOEt|有机碱|[Na+].CC[O-]
正丁基锂|nBuLi|有机金属|[Li+].[CH2-]CCC
叔丁基锂|tBuLi|有机金属|[Li+].[C-](C)(C)C
甲基溴化镁|MeMgBr|有机金属|C[Mg]Br
苯基溴化镁|PhMgBr|有机金属|Br[Mg]c1ccccc1
二甲基铜锂|Me2CuLi Gilman|有机金属|[Li+].C[Cu-]C
二异丁基氢化铝|DIBAL-H|还原试剂|CC(C)C[AlH]CC(C)C
三乙基硅烷|Et3SiH|还原试剂|CC[SiH](CC)CC
三丁基氢化锡|Bu3SnH|还原试剂|CCCC[SnH](CCCC)CCCC
硼烷四氢呋喃|BH3-THF BH3.THF|还原试剂|[BH3].C1CCOC1
三乙酰氧基硼氢化钠|NaBH(OAc)3|还原试剂|[Na+].CC(=O)O[BH-](OC(C)=O)OC(C)=O
间氯过氧苯甲酸|mCPBA|氧化试剂|OOC(=O)c1cccc(Cl)c1
过乙酸|AcOOH|氧化试剂|CC(=O)OO
叔丁基过氧化氢|TBHP|氧化试剂|CC(C)(C)OO
二甲基二环氧乙烷|DMDO|氧化试剂|CC1(C)OO1
四甲基哌啶氧自由基|TEMPO|氧化试剂|[O]N1C(C)(C)CCCC1(C)C
N甲基吗啉氧化物|NMO|氧化试剂|C[N+]1([O-])CCOCC1
二氯二氰基苯醌|DDQ|氧化试剂|N#CC1=C(C#N)C(=O)C(Cl)=C(Cl)C1=O
苯碘二乙酸酯|PIDA|氧化试剂|CC(=O)OI(c1ccccc1)OC(C)=O
苯碘双三氟乙酸酯|PIFA|氧化试剂|O=C(OI(c1ccccc1)OC(=O)C(F)(F)F)C(F)(F)F
氯铬酸吡啶盐|PCC|氧化试剂|[nH+]1ccccc1.[O-][Cr](=O)(=O)Cl
重铬酸吡啶盐|PDC|氧化试剂|[nH+]1ccccc1.[nH+]1ccccc1.[O-][Cr](=O)(=O)O[Cr](=O)(=O)[O-]
N溴代丁二酰亚胺|NBS|卤化与保护|O=C1CCC(=O)N1Br
N氯代丁二酰亚胺|NCS|卤化与保护|O=C1CCC(=O)N1Cl
N碘代丁二酰亚胺|NIS|卤化与保护|O=C1CCC(=O)N1I
二乙氨基三氟化硫|DAST|卤化与保护|CCN(CC)S(F)(F)F
对甲苯磺酰氯|TsCl|卤化与保护|Cc1ccc(S(=O)(=O)Cl)cc1
甲磺酰氯|MsCl|卤化与保护|CS(=O)(=O)Cl
三氟甲磺酸酐|Tf2O|卤化与保护|O=S(=O)(OS(=O)(=O)C(F)(F)F)C(F)(F)F
三甲基氯硅烷|TMSCl|卤化与保护|C[Si](C)(C)Cl
叔丁基二甲基氯硅烷|TBSCl TBDMSCl|卤化与保护|CC(C)(C)[Si](C)(C)Cl
二碳酸二叔丁酯|Boc2O|卤化与保护|CC(C)(C)OC(=O)OC(=O)OC(C)(C)C
四丁基氟化铵|TBAF|卤化与保护|CCCC[N+](CCCC)(CCCC)CCCC.[F-]
三氟乙酸|TFA|有机酸|O=C(O)C(F)(F)F
对甲苯磺酸|TsOH pTSA|有机酸|Cc1ccc(S(=O)(=O)O)cc1
乙酸|AcOH|有机酸|CC(=O)O
偶氮二异丁腈|AIBN|自由基与碳源|CC(C)(C#N)N=NC(C)(C)C#N
过氧化苯甲酰|BPO|自由基与碳源|O=C(OOC(=O)c1ccccc1)c1ccccc1
重氮甲烷|CH2N2|自由基与碳源|C=[N+]=[N-]
重氮乙酸乙酯|EDA|自由基与碳源|CCOC(=O)C=[N+]=[N-]
三甲基硅基重氮甲烷|TMSCHN2|自由基与碳源|C[Si](C)(C)C=[N+]=[N-]
四溴化碳|CBr4|自由基与碳源|BrC(Br)(Br)Br
三溴甲烷|CHBr3|自由基与碳源|BrC(Br)Br
三氯甲烷|CHCl3|自由基与碳源|ClC(Cl)Cl
"""

def records():
    result = []
    for inorganic, data in [(True, INORGANIC), (False, ORGANIC)]:
        for line in data.strip().splitlines():
            title, alias, category, smiles = line.split('|')
            result.append(dict(title=title, alias=alias, category=category, smiles=smiles, inorganic=inorganic))
    extra = ROOT/'reagents_extra.json'
    if extra.exists():
        result.extend(json.loads(extra.read_text(encoding='utf-8')))
    additions=ROOT/'library_additions.json'
    if additions.exists():
        used={alias.lower().replace('-','') for r in result for alias in r['alias'].split()}
        for r in json.loads(additions.read_text(encoding='utf-8')):
            aliases=[a for a in r['alias'].split() if a.lower().replace('-','') not in used]
            if not aliases:continue
            r['alias']=' '.join(aliases);result.append(r);used.update(a.lower().replace('-','') for a in aliases)
    expansion=ROOT/'template_expansion_202611.json'
    if expansion.exists():
        result.extend(json.loads(expansion.read_text(encoding='utf-8')))
    photos=ROOT/'photosensitizers_202611.json'
    if photos.exists():result.extend(json.loads(photos.read_text(encoding='utf-8')))
    aliases = {'LiAlH4': 'LiAlH4 LAH', 'DIBAL-H': 'DIBAL-H DIBAL DIBAH',
               'LiHMDS': 'LiHMDS LHMDS', 'Ph3P': 'Ph3P PPh3',
               'Et3N': 'Et3N NEt3', 'mCPBA': 'mCPBA MCPBA'}
    for record in result:
        for key, replacement in aliases.items():
            if key in record['alias'].split():
                record['alias'] = ' '.join(dict.fromkeys((record['alias']+' '+replacement).split()))
    alkoxides=ROOT/'metal_alkoxides.json'
    if alkoxides.exists():result.extend(json.loads(alkoxides.read_text(encoding='utf-8')))
    return result


def prepare_molecule(record):
    if record.get('spatial_layout') == 'fac-ppy':
        from spatial_template_layout import iridium_molecule
        return iridium_molecule()
    mol = Chem.MolFromSmiles(record['smiles'], sanitize=False)
    if mol is None:
        raise ValueError(record['title'])
    mol.UpdatePropertyCache(strict=False)
    coordination = {
        '14221-01-3': (46, 15, 4), '13965-03-2': (46, 15, 2),
        '246047-72-3': (44, 15, 1), '301224-40-8': (44, 8, 1),
        '64443-05-6': (29, 7, 4), '109-63-7': (5, 8, 1),
        '13292-87-0': (5, 16, 1),
    }
    if record.get('query') in coordination:
        metal, donor, count = coordination[record['query']]
        targets = [a.GetIdx() for a in mol.GetAtoms() if a.GetAtomicNum() == metal]
        donors = [a.GetIdx() for a in mol.GetAtoms() if a.GetAtomicNum() == donor]
        if len(targets) != 1 or len(donors) != count:
            raise ValueError(record['title']+': coordination identity mismatch')
        editable = Chem.RWMol(mol)
        for index in donors:
            editable.AddBond(index, targets[0], Chem.BondType.DATIVE)
        mol = editable.GetMol(); mol.UpdatePropertyCache(strict=False)
    Chem.GetSymmSSSR(mol)
    # Metal/hypervalent compounds need relaxed valence, but depiction still
    # requires hybridization. Missing this stage bends every nitrile and azide.
    Chem.SanitizeMol(mol, sanitizeOps=Chem.SanitizeFlags.SANITIZE_FINDRADICALS |
                     Chem.SanitizeFlags.SANITIZE_SETHYBRIDIZATION |
                     Chem.SanitizeFlags.SANITIZE_SETCONJUGATION)
    Chem.Kekulize(mol, clearAromaticFlags=True)
    hydride_centers = [a.GetIdx() for a in mol.GetAtoms()
                       if a.GetAtomicNum() in (5, 13, 14, 50) and a.GetTotalNumHs() > 0]
    if hydride_centers:
        mol = Chem.AddHs(mol, onlyOnAtoms=hydride_centers)
    rdDepictor.Compute2DCoords(mol, forceRDKit=True, useRingTemplates=True)
    standardize_carbon_angles(mol)
    if record.get('preserve_unspecified_stereo'):
        from photosensitizer_layout import audit,radial
        if any(audit(mol).values()):radial(mol)
    Chem.WedgeMolBonds(mol, mol.GetConformer())
    project_main_group_centers(mol)
    if record.get('preserve_unspecified_stereo'):
        from photosensitizer_layout import audit
        report=audit(mol)
        if any(report.values()):raise ValueError(record['title']+': full depiction contains overlapping or crossing geometry: '+str(report))
        mol.SetProp('studio.layout.audit',json.dumps(report))
    sys.path.insert(0,str(ROOT))
    from branched_alkyl_layout import molecule as project_alkyl
    project_alkyl(mol)
    return mol


def standardize_carbon_angles(mol):
    conf = mol.GetConformer()
    for atom in mol.GetAtoms():
        bonds = list(atom.GetBonds())
        if atom.GetAtomicNum() != 6 or atom.IsInRing() or len(bonds) != 3 or sorted(b.GetBondTypeAsDouble() for b in bonds) != [1, 1, 2]:
            continue
        center = atom.GetIdx()
        branches = []
        for neighbor in atom.GetNeighbors():
            branch = [neighbor.GetIdx()]; seen = {center, neighbor.GetIdx()}
            for index in branch:
                for other in mol.GetAtomWithIdx(index).GetNeighbors():
                    if other.GetIdx() not in seen:
                        branch.append(other.GetIdx()); seen.add(other.GetIdx())
            branches.append(branch)
        branches.sort(key=len, reverse=True)
        original = [conf.GetAtomPosition(i) for i in range(mol.GetNumAtoms())]
        pivot = original[center]
        axis = original[branches[0][0]]-pivot
        frame = math.atan2(axis.y, axis.x)
        best = None
        for sense in (1, -1):
            trial = list(original)
            for branch, target in zip(branches[1:], (frame+sense*2*math.pi/3, frame-sense*2*math.pi/3)):
                offset = original[branch[0]]-pivot
                turn = target-math.atan2(offset.y, offset.x)
                for index in branch:
                    p = original[index]-pivot
                    trial[index] = type(p)(pivot.x+p.x*math.cos(turn)-p.y*math.sin(turn),
                                           pivot.y+p.x*math.sin(turn)+p.y*math.cos(turn), 0)
            cost = 0
            for i in range(len(trial)):
                for j in range(i):
                    if mol.GetBondBetweenAtoms(i, j) is not None: continue
                    distance = (trial[i]-trial[j]).Length()
                    cost += max(0, .7-distance)**2*10000
            cost += sum((a-b).LengthSq() for a, b in zip(trial, original))*.00001
            if best is None or cost < best[0]: best = (cost, trial)
        for index, position in enumerate(best[1]): conf.SetAtomPosition(index, position)


def project_main_group_centers(mol):
    """Standard 2D projections, with depth shown instead of five planar spokes."""
    conf = mol.GetConformer()
    geometry = []
    for atom in mol.GetAtoms():
        bonds = list(atom.GetBonds())
        neighbors = list(atom.GetNeighbors())
        terminal = all(n.GetDegree() == 1 for n in neighbors)
        if not terminal or any(b.GetBondType() != Chem.BondType.SINGLE for b in bonds):
            continue
        z, count = atom.GetAtomicNum(), len(neighbors)
        if count == 5 and z in (15, 33, 51):
            positions = [(0, 1, None), (0, -1, None), (-1, 0, None),
                         (.75, .66, Chem.BondDir.BEGINWEDGE), (.75, -.66, Chem.BondDir.BEGINDASH)]
            kind = 'trigonal-bipyramidal'
        elif count == 6 and z in (15, 16, 33, 51):
            positions = [(0, 1, None), (0, -1, None), (-1, 0, None), (1, 0, None),
                         (.71, .71, Chem.BondDir.BEGINWEDGE), (-.71, -.71, Chem.BondDir.BEGINDASH)]
            kind = 'octahedral'
        elif count == 4 and z in (5, 6, 7, 13, 14, 15, 32):
            positions = [(-.866, .5, None), (.866, .5, None),
                         (-.707, -.707, Chem.BondDir.BEGINWEDGE), (.707, -.707, Chem.BondDir.BEGINDASH)]
            kind = 'tetrahedral'
        elif count == 3 and z in (15, 33, 51) and not atom.GetFormalCharge():
            positions = [(0, 1, None), (-.866, -.5, Chem.BondDir.BEGINWEDGE),
                         (.866, -.5, Chem.BondDir.BEGINDASH)]
            kind = 'trigonal-pyramidal'
        else:
            continue
        center = conf.GetAtomPosition(atom.GetIdx())
        for neighbor, (x, y, depth) in zip(neighbors, positions):
            scale = 1.5/math.hypot(x, y)
            conf.SetAtomPosition(neighbor.GetIdx(), (center.x+x*scale, center.y+y*scale, 0))
            bond = mol.GetBondBetweenAtoms(atom.GetIdx(), neighbor.GetIdx())
            # CDXML wedges are rooted at the central atom, not at a terminal
            # ligand selected by the SMILES traversal order.
            bond.SetIntProp('studio.root', atom.GetIdx())
            if depth is not None: bond.SetBondDir(depth)
        geometry.append(dict(kind=kind, atom=atom.GetIdx()))
    mol.SetProp('studio.projections', json.dumps(geometry))


def angle(mol, center, first, second):
    conformer = mol.GetConformer()
    p = conformer.GetAtomPosition(center)
    a = conformer.GetAtomPosition(first)-p
    b = conformer.GetAtomPosition(second)-p
    return math.degrees(math.acos(max(-1., min(1., a.DotProduct(b)/a.Length()/b.Length()))))


def audit_geometry(mol):
    checks = json.loads(mol.GetProp('studio.projections')) if mol.HasProp('studio.projections') else []
    for atom in mol.GetAtoms():
        bonds = list(atom.GetBonds())
        orders = sorted(b.GetBondTypeAsDouble() for b in bonds)
        neighbors = [n.GetIdx() for n in atom.GetNeighbors()]
        if atom.GetAtomicNum() in (6, 7) and len(bonds) == 2 and (
                orders == [1, 3] or orders == [2, 2]):
            measured = angle(mol, atom.GetIdx(), *neighbors)
            checks.append(dict(kind='linear', atom=atom.GetIdx(), angle=measured))
            if abs(180-measured) > .1:
                raise ValueError(f'nonlinear sp center {atom.GetIdx()}: {measured:.3f}')
        # Acyclic trigonal carbons have three 120 degree directions. Ring
        # vertices and tetrahedral projections follow their own constraints.
        if atom.GetAtomicNum() == 6 and len(bonds) == 3 and orders == [1, 1, 2] and not atom.IsInRing():
            for i in range(3):
                measured = angle(mol, atom.GetIdx(), neighbors[i], neighbors[(i+1) % 3])
                checks.append(dict(kind='trigonal', atom=atom.GetIdx(), angle=measured))
                if abs(120-measured) > 1.:
                    raise ValueError(f'nontrigonal carbon {atom.GetIdx()}: {measured:.3f}')
    return checks


def main():
    folder = ROOT / "reagent_templates"
    folder.mkdir(exist_ok=True)
    entries = []
    audits = []
    for record in records():
            title, alias, category = (record[k] for k in ('title', 'alias', 'category'))
            mol = prepare_molecule(record)
            charge=sum(a.GetFormalCharge() for a in mol.GetAtoms())
            if charge != 0 and not ((record.get('bio') or record.get('ionic_intermediate')) and record.get('formal_charge')==charge):
                raise ValueError(title+': whole reagent must include its counterions')
            formula = rdMolDescriptors.CalcMolFormula(mol, separateIsotopes=True, abbreviateHIsotopes=False)
            composition = lambda value: sorted((element, int(count or 1)) for element, count in re.findall(r'([A-Z][a-z]?)(\d*)', value))
            if record.get('formula') and composition(formula) != composition(record['formula']):
                raise ValueError(title+': formula mismatch '+formula+' != '+record['formula'])
            try:
                checks = audit_geometry(mol)
            except ValueError as exc:
                raise ValueError(title+': '+str(exc)) from exc
            audits.append(dict(title=title, alias=alias, checks=checks))
            if mol.HasProp('studio.layout.audit'):checks.append(dict(kind='full-depiction-clearance',result=json.loads(mol.GetProp('studio.layout.audit'))))
            index = len(entries)
            filename = f"reagent-{index:03d}.cdxml"
            root = ET.Element("CDXML", BondLength="22", LabelSize="10", LineWidth="0.6")
            page = ET.SubElement(root, "page", id="1")
            frag = ET.SubElement(page, "fragment", id="2")
            conf = mol.GetConformer()
            unspecified=set()
            if record.get('preserve_unspecified_stereo'):
                reference=Chem.MolFromSmiles(record['smiles'])
                unspecified={info.centeredOn for info in Chem.FindPotentialStereo(reference) if info.type==Chem.StereoType.Bond_Double and info.specified==Chem.StereoSpecified.Unspecified}
            for atom in mol.GetAtoms():
                point = conf.GetAtomPosition(atom.GetIdx())
                attrs = dict(id=str(10 + atom.GetIdx()), p=f"{point.x*22/1.5:.9f} {-point.y*22/1.5:.9f}", Element=str(atom.GetAtomicNum()),
                             NumHydrogens=str(atom.GetTotalNumHs()))
                if atom.GetFormalCharge():
                    attrs["Charge"] = str(atom.GetFormalCharge())
                if atom.GetIsotope():
                    attrs["Isotope"] = str(atom.GetIsotope())
                # RDKit's organic valence heuristic assigns artificial radicals
                # to isolated metals. Only the explicit nitroxyl is a radical.
                if atom.GetAtomicNum() in [8]+record.get('radical_elements',[]) and atom.GetNumRadicalElectrons() == 1:
                    attrs["Radical"] = "Doublet"
                node=ET.SubElement(frag, "n", attrs)
                # CO2/CS2 and related heterocumulenes show the central carbon
                # explicitly, rather than the skeletal allene-centre dot.
                if atom.GetAtomicNum()==6 and atom.GetDegree()==2 and all(n.GetAtomicNum()!=6 for n in atom.GetNeighbors()) and all(b.GetBondType()==Chem.BondType.DOUBLE for b in atom.GetBonds()):
                    caption=ET.SubElement(node,'t');ET.SubElement(caption,'s').text='C'
            for bond in mol.GetBonds():
                order = 'dative' if bond.GetBondType() == Chem.BondType.DATIVE else str(int(bond.GetBondTypeAsDouble()))
                if order not in ('1', '2', '3', 'dative'):
                    raise ValueError(title+': unsupported bond '+str(bond.GetBondType()))
                attrs = dict(id=str(1000+bond.GetIdx()), B=str(10+bond.GetBeginAtomIdx()),
                             E=str(10+bond.GetEndAtomIdx()), Order=order)
                if bond.HasProp('studio.root') and bond.GetBeginAtomIdx() != bond.GetIntProp('studio.root'):
                    attrs['B'], attrs['E'] = attrs['E'], attrs['B']
                if bond.GetBondDir() == Chem.BondDir.BEGINWEDGE:
                    attrs['Display'] = 'WedgeBegin'
                elif bond.GetBondDir() == Chem.BondDir.BEGINDASH:
                    attrs['Display'] = 'WedgedHashBegin'
                if bond.GetIdx() in unspecified:attrs['Display']='Wavy'
                ET.SubElement(frag, 'b', attrs)
            from spatial_template_layout import apply as spatial_layout,selected as spatial_selected
            spatial_info=spatial_layout(root,mol,record)
            if spatial_info:checks.append(dict(kind='spatial-template',layout=spatial_info))
            from compact_reagent_depiction import compact
            full=copy.deepcopy(root);compact_info=None if spatial_selected(record) else compact(root,mol,record)
            from branched_alkyl_layout import xml_fragment,aibn_fragment
            xml_fragment(root.find('./page/fragment'));aibn_fragment(root.find('./page/fragment'))
            aibn_fragment(full.find('./page/fragment'))
            if compact_info:
                ET.ElementTree(full).write(folder / ('full-'+filename),encoding='utf-8',xml_declaration=True)
                checks.append(dict(kind='compact-depiction',layout=compact_info))
            ET.ElementTree(root).write(folder / filename, encoding="utf-8", xml_declaration=True)
            # Keep the full-depiction asset current for earlier catalog clients.
            if spatial_info and (folder / ('full-'+filename)).exists():
                ET.ElementTree(full).write(folder / ('full-'+filename),encoding='utf-8',xml_declaration=True)
            entry = dict(title=title, alias=alias, category=category, file=filename, inorganic=record['inorganic'],
                         formula=formula)
            if compact_info:entry.update(full_file='full-'+filename,compact=compact_info)
            for key in ('cid', 'source', 'inchikey'):
                if key in record: entry[key] = record[key]
            entries.append(entry)
    (ROOT / "reagents.json").write_text(json.dumps(dict(sources=SOURCES, reagents=entries), ensure_ascii=False, indent=2), encoding="utf-8")
    lines = ["#pragma once", "struct ReagentEntry {const char* title;const char* alias;const char* category;const char* file;bool inorganic;};",
             "static const std::vector<ReagentEntry>& reagentEntries(){static const std::vector<ReagentEntry> entries={"]
    for entry in entries:
        values = ",".join(json.dumps(entry[k],ensure_ascii=False) for k in ("title","alias","category","file"))
        lines.append("    {"+values+","+str(entry["inorganic"]).lower()+"},")
    lines += ["};return entries;}"]
    (ROOT / "reagent_catalog.hpp").write_text("\n".join(lines)+"\n", encoding="utf-8")
    (ROOT/'reagent_geometry_audit.json').write_text(json.dumps(audits, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"{sum(e['inorganic'] for e in entries)} inorganic and {sum(not e['inorganic'] for e in entries)} organic templates")
    from build_compound_catalog import build
    build()
    from generate_bio_expansion import build as build_bio
    build_bio()

if __name__ == "__main__":
    main()
