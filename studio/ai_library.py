"""The AI shares the current application's exact native ring coordinates."""
from functools import lru_cache
from pathlib import Path
import json,xml.etree.ElementTree as ET
@lru_cache(maxsize=1)
def catalog():
    data=json.loads(Path(__file__).with_name('ai_ring_catalog.json').read_text(encoding='utf-8'))
    if data.get('schema')!=1:raise ValueError('环库版本不匹配，请更新软件')
    return data

def search(args):
    query=args.get('query','');category=args.get('category','');limit=args.get('limit',30);offset=args.get('offset',0)
    if not isinstance(query,str) or len(query)>120 or not isinstance(category,str) or len(category)>100 or type(limit)!=int or not 1<=limit<=112 or type(offset)!=int or offset<0:raise ValueError('模板查询参数无效')
    entries=catalog()['variants'];q=query.casefold()
    matches=[e for e in entries if (not category or category==e['category']) and (not q or any(q in str(e[k]).casefold() for k in ('id','name','english','aliases','variant','formula')))]
    return dict(source='MoleculeStudio native ring library',parents=catalog()['parents'],total_variants=len(entries),matched=len(matches),categories=sorted({e['category'] for e in entries}),templates=[{k:e[k] for k in ('id','name','english','category','variant','formula')} for e in matches[offset:offset+limit]],next_offset=offset+limit if offset+limit<len(matches) else None)

def draw(engine,args,number,style):
    key=args['template'];entry=next((e for e in catalog()['variants'] if e['id']==key),None)
    if entry is None:raise ValueError('模板 ID 不存在；先用 search_drawing_templates 查询，不要猜测编号')
    length=float(engine.context.get('bond_length',14.4))
    if not 0.1<=length<=1000:raise ValueError('文档键长无效')
    root=ET.Element('CDXML',{'BondLength':str(length)})
    page=ET.SubElement(root,'page',{'id':'1'});fragment=ET.SubElement(page,'fragment',{'id':'2'})
    for i,(x,y,element,hydrogens,charge,label) in enumerate(entry['atoms']):
        attrs={'id':str(i+3),'p':f'{x*length:.12g} {y*length:.12g}','Element':str(element)}
        if charge:attrs['Charge']=str(charge)
        if element==16:attrs['NumHydrogens']=str(hydrogens)
        ET.SubElement(fragment,'n',attrs)
    for i,(a,b,order,wedge) in enumerate(entry['bonds']):
        attrs={'id':str(3+len(entry['atoms'])+i),'B':str(a+3),'E':str(b+3),'Order':str(order)}
        if wedge:attrs['Display']='WedgeBegin' if wedge==1 else 'WedgedHashBegin'
        ET.SubElement(fragment,'b',attrs)
    from ai_scene import bounds
    box=bounds(engine.scene);width=(max(a[0] for a in entry['atoms'])-min(a[0] for a in entry['atoms']))*length
    height=(max(a[1] for a in entry['atoms'])-min(a[1] for a in entry['atoms']))*length
    default_x=box[0]+width/2 if box else 0;default_y=box[3]+16+height/2 if box else 0
    attrs=style(args);attrs.update(x=str(number(args,'x',default_x)),y=str(number(args,'y',default_y)))
    node=ET.Element('molecule',attrs);node.text=ET.tostring(root,encoding='unicode')
    created=engine.record_creation(node);engine.actions.append(node);engine.molecules+=1
    return dict(staged=True,**created,template=key,name=entry['name'],variant=entry['variant'],formula=entry['formula'],layout='native-library',source='native ring library')
