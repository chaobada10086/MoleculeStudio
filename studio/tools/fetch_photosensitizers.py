"""Explicit refresh of reviewed organic photosensitizer candidates."""
from pathlib import Path
import sys,json,concurrent.futures
sys.path.insert(0,str(Path(__file__).resolve().parent))
from fetch_template_expansion import fetch,ROOT
from rdkit import Chem
from rdkit.Chem import rdMolDescriptors
DATA='''
4CzIPN有机光催化剂|4CzIPN|有机光催化剂|1416881-52-1
4DPAIPN有机光催化剂|4DPAIPN|有机光催化剂|1846598-27-3
3DPAFIPN有机光催化剂|3DPAFIPN|有机光催化剂|3DPAFIPN
4CzTPN有机光催化剂|4CzTPN|有机光催化剂|4CzTPN
2CzPN有机光催化剂|2CzPN|有机光催化剂|2CzPN
5CzBN有机光催化剂|5CzBN|有机光催化剂|5CzBN
9-均三甲苯基-10-甲基吖啶鎓四氟硼酸盐|Mes-Acr-MeBF4 MesAcrBF4|吖啶鎓与吡喃鎓光催化剂|1442433-71-7
2,4,6-三苯基吡喃鎓四氟硼酸盐|TPP-BF4|吖啶鎓与吡喃鎓光催化剂|448-61-3
10-苯基吩噻嗪|N-Phenylphenothiazine PTH|吩噻嗪与吩噁嗪光催化剂|10-phenylphenothiazine
10-苯基吩噁嗪|N-Phenylphenoxazine PPO|吩噻嗪与吩噁嗪光催化剂|10-phenylphenoxazine
吩噻嗪|Phenothiazine|吩噻嗪与吩噁嗪光催化剂|92-84-2
10-甲基吩噻嗪|N-Methylphenothiazine|吩噻嗪与吩噁嗪光催化剂|1207-72-3
9,10-二氰基蒽|DCA 9,10-DCA|芳烃光敏剂|1217-45-4
9-氰基蒽|9-CNA 9-cyanoanthracene|芳烃光敏剂|1210-12-4
蒽|Anthracene|芳烃光敏剂|120-12-7
芘|Pyrene|芳烃光敏剂|129-00-0
苝|Perylene|芳烃光敏剂|198-55-0
9,10-双(苯乙炔基)蒽|BPEA|芳烃光敏剂|10075-85-1
硫杂蒽酮|Thioxanthone TX|芳香酮光敏剂|492-22-8
2-异丙基硫杂蒽酮|2-ITX|芳香酮光敏剂|5495-84-1
2,4-二乙基硫杂蒽酮|DETX|芳香酮光敏剂|82799-44-8
氧杂蒽酮|Xanthone|芳香酮光敏剂|90-47-1
9,10-蒽醌|Anthraquinone AQ|芳香酮光敏剂|84-65-1
2-乙基蒽醌|2-EAQ|芳香酮光敏剂|84-51-5
2-叔丁基蒽醌|2-tBuAQ|芳香酮光敏剂|84-47-9
联苯甲酰|Benzil|芳香酮光敏剂|134-81-6
4,4′-双二甲氨基二苯甲酮|Michlers-ketone|芳香酮光敏剂|90-94-8
玫瑰红二钠盐|RoseBengal-Na2 RB-Na2|染料光敏剂|632-69-9
亚甲基蓝氯化物|MethyleneBlue-Cl MB-Cl|染料光敏剂|61-73-4
罗丹明B氯化物|RhodamineB RhoB|染料光敏剂|81-88-9
罗丹明6G氯化物|Rhodamine6G R6G|染料光敏剂|989-38-8
曙红Y游离酸|EosinY-acid|染料光敏剂|15086-94-9
荧光素二钠盐|Fluorescein-Na2 Uranine|染料光敏剂|518-47-8
曙红B二钠盐|EosinB-Na2|染料光敏剂|548-24-3
9-芴酮|Fluorenone|芳香酮光敏剂|486-25-9
9,10-二苯基蒽|DPA-anthracene|芳烃光敏剂|1499-10-1
'''
if __name__=='__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(fetch,DATA.strip().splitlines()))
    good=[r for r in results if 'error' not in r];bad=[r for r in results if 'error' in r]
    for r in good:
        r['preserve_unspecified_stereo']=True
        if r['alias'].split()[0] in {'4CzIPN','4DPAIPN','3DPAFIPN','4CzTPN','2CzPN','5CzBN'}:r['compact_label']=r['alias'].split()[0]
    acr=next((r for r in good if r['alias'].startswith('Mes-Acr-MeBF4')),None)
    if acr:
        cation=next(part for part in acr['smiles'].split('.') if '[B-]' not in part)
        smiles=cation+'.[O-]Cl(=O)(=O)=O';mol=Chem.MolFromSmiles(smiles)
        good.append(dict(title='9-均三甲苯基-10-甲基吖啶鎓高氯酸盐',alias='Mes-Acr-MeClO4 MesAcrClO4',category='吖啶鎓与吡喃鎓光催化剂',smiles=smiles,formula=rdMolDescriptors.CalcMolFormula(mol),inchikey=Chem.MolToInchiKey(mol),inorganic=False,preserve_unspecified_stereo=True,source='https://www.tcichemicals.com/BE/en/sds/M1774_EU_6N.pdf',review_note='Acridinium cation verified against BF4 salt; counterion explicitly replaced with perchlorate.'))
    good.append(dict(title='二氧化碳自由基阴离子',alias='CO2RA CO2-radical-anion CO2.- CO2*- CO2•- CO2·- CO2•− CO2·− CO2-• CO2-· CO2-* CO₂•⁻ CO₂·⁻',category='自由基阴离子中间体',smiles='[O-][C]=O',formula='CO2-',radical_elements=[6],formal_charge=-1,ionic_intermediate=True,inorganic=True,preserve_unspecified_stereo=True,source='https://pubs.acs.org/doi/10.1021/jacs.1c04427',review_note='One Lewis resonance form, carbon doublet and one O formal negative charge; no hydrogens.'))
    (ROOT/'photosensitizers_202611.json').write_text(json.dumps(good,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (ROOT/'tools/photosensitizer_fetch_failures.json').write_text(json.dumps(bad,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('fetched',len(good),'failed',len(bad),flush=True)
    for r in bad:print(r,flush=True)
