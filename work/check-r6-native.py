import struct,json
from pathlib import Path
def parse(path):
 b=Path(path).read_bytes();p=22;nodes=[]
 def walk(owner=None):
  nonlocal p
  while p<len(b):
   tag=int.from_bytes(b[p:p+2],'little');p+=2
   if not tag:return
   if tag&32768:
    p+=4;node={'tag':tag,'props':{}};nodes.append(node);walk(node)
   else:
    n=int.from_bytes(b[p:p+2],'little');p+=2
    if n==65535:n=int.from_bytes(b[p:p+4],'little');p+=4
    data=b[p:p+n];p+=n;owner['props'][tag]=data
 walk();return nodes
native=parse('work/r6-visual/tlc-native-border.cdx');ours=parse('work/r6-visual/tlc-border-native-flags.cdx')
for tag in (0x801d,0x801e,0x801f):assert sum(n['tag']==tag for n in native)==sum(n['tag']==tag for n in ours)
a=next(n for n in native if n['tag']==0x801d)['props'];b=next(n for n in ours if n['tag']==0x801d)['props']
for tag in (0x209,0x20a,0x20b,0x20c,0xaa0,0xaa1,0xaa2,0xaa3,0xaa4):assert a[tag]==b[tag],hex(tag)
for a,b in zip([n['props'] for n in native if n['tag']==0x801f],[n['props'] for n in ours if n['tag']==0x801f]):
 for tag in (0xab0,0x812,0x813,0xa08):assert a[tag]==b[tag],hex(tag)
 assert a.get(0xab1,b'\0'*4)==b.get(0xab1,b'\0'*4)
print('Native ChemDraw binary comparison passed: plate corners, flags, 6 lanes, 18 spots, dimensions and tail.')
