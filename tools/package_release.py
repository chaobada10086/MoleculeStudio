"""Package three pre-audited staging directories. Never package a live installation."""
from pathlib import Path
import argparse,zipfile,json,hashlib,concurrent.futures
from audit_release import audit

def digest(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def package(staging,out,version,kind):
 base=staging/kind/'MoleculeStudio';result=audit(base)
 if result['findings']:raise RuntimeError(json.dumps(result))
 files=sorted(p for p in base.rglob('*') if p.is_file() and p.name!=f'MANIFEST-{kind}.json')
 manifest={'version':version,'package':kind,'files':{p.relative_to(base).as_posix():{'bytes':p.stat().st_size,'sha256':digest(p)} for p in files}}
 mp=base/f'MANIFEST-{kind}.json';mp.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8');files.append(mp)
 suffix={'Core':'Core-win64','AI':'AI-addon-win64','OCSR':'OCSR-addon-win64'}[kind];target=out/f'MoleculeStudio-{version}-{suffix}.zip'
 with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=1,allowZip64=True) as z:
  for p in files:
   stored=p.name.endswith(('.zip','.gz','.xz','.bz2','.7z'))
   z.write(p,'MoleculeStudio/'+p.relative_to(base).as_posix(),compress_type=zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED)
 with zipfile.ZipFile(target) as z:
  bad=z.testzip()
  if bad:raise RuntimeError('CRC failure: '+bad)
  assert len(z.namelist())==len(files)
 return {'file':target.name,'bytes':target.stat().st_size,'sha256':digest(target),'entries':len(files)}
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--staging',type=Path,required=True);a.add_argument('--output',type=Path,required=True);a.add_argument('--version',default='0.95');args=a.parse_args();args.output.mkdir(parents=True,exist_ok=True)
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:rows=list(pool.map(lambda k:package(args.staging,args.output,args.version,k),['Core','AI','OCSR']))
 report=args.output.parent/'RELEASE_CHECKSUMS.json';report.write_text(json.dumps(rows,indent=2),encoding='utf8');print(json.dumps(rows,indent=2))
