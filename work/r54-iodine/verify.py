from pathlib import Path
import subprocess as sp
import xml.etree.ElementTree as ET
import hashlib,json
d=Path(__file__).resolve().parent
out=d.parents[1]/"out/Studio-r54"
exe=next(out.glob("*r54.exe"))
def run(*args):
 r=sp.run([str(exe),*map(str,args)],capture_output=True)
 if r.returncode:raise RuntimeError(r.stderr.decode("utf8","replace"))
def inspect(path,label):
 scene=d/(label+".xml")
 run("--ai-scene",path,scene)
 return list(ET.parse(scene).iter("object"))
styles=[]
for style in range(4):
 f=d/("product-"+str(style)+".cdxml")
 run("--ai-apply",d/"original.r62doc",d/("style-"+str(style)+".xml"),f)
 objects=inspect(f,"product-scene-"+str(style))
 atoms=[o for o in objects if o.get("kind")=="atom"]
 bonds=[o for o in objects if o.get("kind")=="bond"]
 assert len(atoms)==14 and len(bonds)==13
 c,= [o for o in atoms if o.get("element")=="C" and o.get("charge")=="1"]
 i,= [o for o in atoms if o.get("element")=="I" and o.get("charge")=="-1"]
 assert all(i.get("ref") not in (b.get("begin"),b.get("end")) for b in bonds)
 orders=sorted(int(b.get("order")) for b in bonds if c.get("ref") in (b.get("begin"),b.get("end")))
 assert orders==[1,2]
 assert all(o.get("charge")=="0" for o in atoms if o not in [c,i])
 xml=ET.parse(f)
 node,=[n for n in xml.iter("n") if n.get("Charge")=="1"]
 assert node.get("NumHydrogens")=="0"
 ar=[o.get("MoleculeStudioArrow") for o in xml.iter() if o.get("MoleculeStudioArrow") and not o.get("MoleculeStudioElectronSource")]
 assert len(ar)==1
 styles.append(ar[0])
 print("PASS style",style,"6 ring bonds retained, C+ (0 H), free I-, charge conserved")
assert len(set(styles))==4
manifest=json.loads((out/"manifest.json").read_text())
assert hashlib.sha256(exe.read_bytes()).hexdigest()==manifest["exe_sha256"]
