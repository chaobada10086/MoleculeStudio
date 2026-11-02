import argparse,json,re
from pathlib import Path
TOKEN=re.compile(rb'(?<![A-Za-z0-9])(?:sk-[A-Za-z0-9_-]{24,}|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|AKIA[0-9A-Z]{16})')
PEM=re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\s+[A-Za-z0-9+/=\r\n]{60,}-----END')
DENY={'.env','.npmrc','.pypirc','auth.json','credentials.json','interface.ini','drawing.ini','custom_groups.xml'}
PARTS={'__pycache__','OCSRCache','recognition_records','ChemistryJobs','sessions'}
def audit(root):
 findings=[];count=0
 for p in root.rglob('*'):
  if not p.is_file():continue
  rel=p.relative_to(root)
  if '.git' in rel.parts:continue
  count+=1
  if p.name in DENY or (PARTS.intersection(rel.parts) and 'node_modules' not in rel.parts and 'site-packages' not in rel.parts) or p.name.endswith(('.log','.pyc','.bak','.jsonl.zstd')):findings.append({'path':rel.as_posix(),'reason':'private/generated file'})
  if p.suffix.lower() in {'.py','.cpp','.hpp','.h','.inc','.json','.js','.mjs','.cjs','.txt','.md','.yml','.yaml','.ini','.cfg','.pem','.key','.xml'} and p.stat().st_size<16000000:
   b=p.read_bytes()
   for name,pattern in [('token-like value',TOKEN),('private key body',PEM)]:
    if pattern.search(b):findings.append({'path':rel.as_posix(),'reason':name})
 return {'files':count,'findings':findings}
if __name__=='__main__':
 a=argparse.ArgumentParser(description='Redacted secret/generated-file scan; not a security guarantee');a.add_argument('directory',type=Path);a.add_argument('--output',type=Path);args=a.parse_args();result=audit(args.directory.resolve());text=json.dumps(result,ensure_ascii=False,indent=2)
 if args.output:args.output.write_text(text,encoding='utf8')
 print(text);raise SystemExit(bool(result['findings']))
