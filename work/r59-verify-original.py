from pathlib import Path
import itertools, subprocess, xml.etree.ElementTree as ET, hashlib, json
exe=next(Path('out/Studio-r59').glob('*r59.exe')).resolve()
source=Path('work/r59-live-michael.r62doc')
out=Path('work/r59-original-check');out.mkdir(exist_ok=True)
digest=lambda:hashlib.sha256(source.read_bytes()).hexdigest()
before=digest()
expected={(28,29):1,(29,30):1,(29,31):2,(31,32):1,(32,40):1,(33,34):2,(34,35):1,(34,40):1,(35,36):1,(36,37):1,(37,38):1,(38,39):2,(38,40):1}
for perm in itertools.permutations([20,22,23,24]):
    plan=ET.Element('result',ok='true'); step=ET.SubElement(ET.SubElement(plan,'actions'),'mechanism_step',mode='arrows')
    for i in perm:ET.SubElement(step,'ref',id=f'arrow:{i}')
    file=out/'step.xml';ET.ElementTree(plan).write(file)
    cdxml=out/'Michael-r59.cdxml'
    subprocess.run([exe,'--ai-apply',source,file,cdxml],check=True)
    root=ET.parse(cdxml)
    atoms={int(n.get('id')):n for n in root.iter('n') if int(n.get('id'))>=28}
    assert set(atoms)==set(range(28,41))
    edges={tuple(sorted((int(b.get('B')),int(b.get('E'))))):int(b.get('Order','1')) for b in root.iter('b') if int(b.get('B'))>=28 or int(b.get('E'))>=28}
    assert edges==expected,(perm,edges)
    for i,a in atoms.items():
        assert int(a.get('Element','6'))==(8 if i in (30,33,39) else 6)
        assert int(a.get('Charge','0'))==(-1 if i==30 else 0) and not a.get('Radical')
    for i,a in atoms.items():
        order=sum(n for ends,n in edges.items() if i in ends)
        if a.get('NumHydrogens') is not None:
            assert order+int(a.get('NumHydrogens'))==(1 if i==30 else 2 if i in (33,39) else 4)
subprocess.run([exe,'--ai-scene',cdxml,out/'reopened.xml'],check=True)
assert digest()==before
print('24 permutations: all 13 product atoms and 13 bonds checked, one O-, no radicals; CDXML reopened; original SHA256:',before)
