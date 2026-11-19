from PIL import Image,ImageDraw
from pathlib import Path
import xml.etree.ElementTree as E
root=E.parse('work/r43-curves.svg').getroot();im=Image.new('RGB',(1500,1050),'white');d=ImageDraw.Draw(im);ns={'s':'http://www.w3.org/2000/svg'}
for i,g in enumerate(root.findall('s:g',ns)):
 x,y=map(float,g.attrib['transform'][10:-1].split(','));d.text((x*1.5+15,y*1.5+10),str(i+1),fill='black')
 for p in g:
  pts=[tuple(map(float,q.split(','))) for q in p.attrib['points'].split()];pts=[((a+x)*1.5,(b+y)*1.5) for a,b in pts];d.line(pts,fill=p.attrib['stroke'],width=3)
im.save('work/r43-curves.png')
