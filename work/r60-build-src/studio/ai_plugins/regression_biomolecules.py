"""Offline structure identity, conformer semantics and native fixtures."""
import sys,copy,json
from pathlib import Path
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
from chemistry_open import Chem,identity,one_molecule,rdMolDescriptors
from ai_plugins import draw_plugin as p
from ai_plugins import biomolecules as bio
import ai_assistant as ai

def main():
    output=Path(sys.argv[1]);output.mkdir(parents=True,exist_ok=True);p.install_plugin(vars(ai))
    poses=0
    for key,item in bio.DATA.items():
        forms=['chair_4c1','chair_1c4','haworth','flat'] if key in bio.SUGARS and 'ribose' not in key else ['haworth','flat'] if key in bio.SUGARS else ['flat']
        for form in forms:
            mol,xml,actual,ring,subs=bio.cdxml(key,form);poses+=1
            assert identity(one_molecule(xml))==identity(mol),(key,form,'stereo changed')
            if 'formula' in item:assert rdMolDescriptors.CalcMolFormula(mol)==item['formula']
            if 'inchikey' in item:assert Chem.MolToInchiKey(mol)==item['inchikey'],(key,'database identity')
            if form=='chair_4c1' and key=='beta_d_glucose':assert all(s['orientation']=='equatorial' for s in subs)
            if form=='chair_1c4' and key=='beta_d_glucose':assert all(s['orientation']=='axial' for s in subs)
            if form=='chair_4c1' and key=='alpha_d_glucose':assert [s['carbon'] for s in subs if s['orientation']=='axial']==[1]
            if form=='chair_4c1' and key=='beta_d_mannose':assert [s['carbon'] for s in subs if s['orientation']=='axial']==[2]
            if form=='chair_4c1' and key=='beta_d_galactose':assert [s['carbon'] for s in subs if s['orientation']=='axial']==[4]
    e=ai.DrawingTools({'selected':0,'bond_length':22.5,'font':'Times New Roman'})
    samples=[('alpha_d_glucose','chair_4c1'),('beta_d_glucose','chair_4c1'),('beta_d_glucose','chair_1c4'),('beta_d_glucose','haworth'),('beta_d_galactose','chair_4c1'),('beta_d_mannose','chair_4c1'),('beta_d_ribose','haworth'),('beta_d_deoxyribose','haworth')]
    for i,(key,form) in enumerate(samples):
        r=e.call('draw_biomolecule',{'component':key,'conformation':form,'x':(i%4)*140,'y':(i//4)*150,'label':bio.DATA[key]['name']+'\n'+form})
        assert r['staged'] and r['label_ref'] and r['diagnostics']['stage']=='complete'
    root=ET.Element('result',ok='true');ET.SubElement(root,'actions').extend(e.actions);(output/'biosugars-plan.xml').write_text(ET.tostring(root,encoding='unicode'),encoding='utf-8')
    assert len(e.call('list_biomolecules',{})['components'])==39
    for args in [{'component':'beta_d_ribose','conformation':'chair_4c1'},{'component':'adenine','conformation':'haworth'},{'component':'peptide','sequence':'AZ'},{'component':'peptide','sequence':'ACD','label':'a'*201}]:
        before=([ET.tostring(n) for n in e.actions],copy.deepcopy(e.scene),e.created_count)
        try:e.call('draw_biomolecule',args)
        except ValueError as exc:assert exc.diagnostics['suggestion']
        else:raise AssertionError('invalid bio request accepted')
        assert before==([ET.tostring(n) for n in e.actions],e.scene,e.created_count),'partial structure after error'
    peptide=e.call('draw_biomolecule',{'component':'peptide','sequence':'ACDE','label':''})
    assert peptide['smiles']==identity(Chem.MolFromSequence('ACDE'))
    report={'components':39,'poses':poses,'database_identity':True,'stereo_roundtrip':True,'chair_flip_axial_equatorial':True,'alpha_beta':True,'peptide':True,'atomic_rollback':True}
    (output/'checks.json').write_text(json.dumps(report),encoding='utf-8');print(json.dumps(report))

if __name__=='__main__':main()
