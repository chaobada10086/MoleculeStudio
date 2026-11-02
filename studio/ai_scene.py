"""Document inspection and staged edits for the native, transactional AI bridge."""
import copy
import math
import re
import xml.etree.ElementTree as ET

def validate_xml_strings(value):
    """Reject strings that cannot survive the native XML transport."""
    if isinstance(value, str):
        if any(not (c in '\t\n\r' or '\x20' <= c <= '\ud7ff' or '\ue000' <= c <= '\ufffd' or '\U00010000' <= c <= '\U0010ffff') for c in value):
            raise ValueError('工具文字包含 XML 不支持的控制字符')
    elif isinstance(value, dict):
        for key, item in value.items():
            validate_xml_strings(key); validate_xml_strings(item)
    elif isinstance(value, (list, tuple)):
        for item in value: validate_xml_strings(item)

KINDS = ['all', 'atom', 'bond', 'text', 'arrow', 'path', 'bracket', 'orbital', 'symbol', 'bio', 'table', 'group', 'graphics']
SELECTOR = {
    'target': {'type': 'string', 'description': 'auto=有选区则选区，否则全文；document=全文；selection=选区；created=本轮新对象；也可用查询返回的 molecule:0、atom:3 或绘图返回的 new:1 等引用。'},
    'refs': {'type': 'array', 'items': {'type': 'string'}, 'description': '可选，多个对象/分子的引用，提供后代替 target。'},
    'kind': {'type': 'string', 'enum': KINDS, 'description': '对象类型；给氧原子改色用 atom，不要选 bond。'},
    'element': {'type': 'string', 'description': '仅匹配真实元素原子，例如 O、N、Cl；不包括缩写标签。'},
    'contains': {'type': 'string', 'description': '仅匹配包含此文字的文本对象。'},
}

def extra_tools(tool, STR, NUM, STYLE):
    return [
        tool('inspect_document', '查看当前画布及本轮已暂存对象，返回纸张尺寸和张数、引用、元素、坐标、包围盒、分子分组及匹配数量。编辑前用它确定对象；所有坐标相对本轮插入点。', {**SELECTOR, 'limit': {'type':'integer'}, 'offset': {'type':'integer'}}),
        tool('style_objects', '按范围/元素/类型/文本筛选后改色或线宽。只改匹配对象，不自动给相邻键改色。无需用户手选。', {**SELECTOR, **STYLE}),
        tool('move_objects', '平移匹配对象。移动整个分子请使用 molecule 引用，避免只拉动局部原子。', {**SELECTOR, 'dx':NUM, 'dy':NUM}, ['dx','dy']),
        tool('transform_objects', '以匹配对象的中心旋转和等比缩放；正角顺时针。保留化学连接与立体化学。', {**SELECTOR, 'angle':NUM, 'scale':NUM}),
        tool('duplicate_objects', '复制对象并平移，返回新引用；保留原对象与格式。复制分子请用 molecule 引用。', {**SELECTOR, 'dx':NUM, 'dy':NUM}, ['dx','dy']),
        tool('delete_objects', '删除明确要求移除的对象，可撤销。删除原子同时删除连接它的键。', SELECTOR),
        tool('edit_text', '修改现有文字内容、字号、字体、粗体、斜体和颜色；自动限于 text 类型，未指定属性保持原样。', {**SELECTOR, 'text':STR, 'size':NUM, 'font':STR, 'bold':{'type':'boolean'}, 'italic':{'type':'boolean'}, 'color':STR}),
        tool('arrange_objects', '按 refs 的顺序将独立分子/图形/文字横排或竖排，gap 为包围盒之间的间距；x,y 为第一项左边/顶部的位置和排列中心线。', {'refs':{'type':'array','items':STR}, 'direction':{'type':'string','enum':['row','column']}, 'gap':NUM,'x':NUM,'y':NUM}, ['refs']),
        tool('draw_path', '绘制折线或闭合多边形，可填色，用于自定义示意图、框线和图案。返回可继续编辑的引用。', {'points':{'type':'array','items':{'type':'array','items':NUM,'minItems':2,'maxItems':2},'minItems':2,'maxItems':128}, 'closed':{'type':'boolean'}, 'fill':STR, **STYLE}, ['points']),
    ]

def bounds(objects):
    boxes = [(o.get('x',0),o.get('y',0),o.get('x2',o.get('x',0)),o.get('y2',o.get('y',0))) for o in objects]
    return [min(b[0] for b in boxes),min(b[1] for b in boxes),max(b[2] for b in boxes),max(b[3] for b in boxes)] if boxes else None

def text_extent(text, size):
    """Conservative layout estimate; the native renderer retains exact text."""
    lines = str(text).replace('\r\n', '\n').replace('\r', '\n').split('\n')
    width = max((sum(1.0 if ord(ch) > 0x2E80 else .72 for ch in line) for line in lines), default=0) * size
    return width, size * (1 + 1.3 * (len(lines) - 1))

class EditingTools:
    def load_scene(self, scene):
        self.scene=[];self.scene_truncated=False;self.created_count=0;self.document_total=0;self.selection_complete=True
        if scene is not None:
            self.scene_truncated=scene.get('truncated')=='true';self.document_total=int(scene.get('total',0));self.selection_complete=scene.get('selection_complete','true')=='true'
            for node in scene.findall('object'):
                item=dict(node.attrib)
                for key in ('x','y','x2','y2'):
                    item[key]=float(item.get(key,0))
                if 'size' in item:item['size']=float(item['size'])
                for key in ('bold','italic'):
                    if key in item:item[key]=item[key]=='true'
                item['selected']=item.get('selected')=='true'
                self.scene.append(item)

    def new_handle(self):
        self.created_count+=1
        return 'new:'+str(self.created_count)

    def refresh_bonds(self):
        atoms = {o['ref']: o for o in self.scene if o['kind'] == 'atom'}
        for bond in self.scene:
            if bond['kind'] != 'bond': continue
            a, b = atoms.get(bond.get('begin')), atoms.get(bond.get('end'))
            if a is not None and b is not None:
                bond.update(x=min(a['x'],b['x']), y=min(a['y'],b['y']),
                            x2=max(a['x'],b['x']), y2=max(a['y'],b['y']))

    def select(self, args):
        from chemistry_open import Chem
        target=args.get('target','auto');kind=args.get('kind','all');element=args.get('element','');contains=args.get('contains','')
        refs=args.get('refs',[])
        if not isinstance(target,str) or not target or len(target)>100 or kind not in KINDS or not isinstance(contains,str) or len(contains)>500:
            raise ValueError('对象筛选条件无效')
        if not isinstance(refs,list) or len(refs)>500 or any(not isinstance(r,str) or not re.fullmatch(r'[A-Za-z0-9:/_-]{1,100}',r) for r in refs):
            raise ValueError('对象引用无效；请使用 inspect_document 或绘图返回的引用')
        if element and (not isinstance(element,str) or element not in [Chem.GetPeriodicTable().GetElementSymbol(i) for i in range(1,119)]):
            raise ValueError('element 必须是正确的元素符号，例如 O')
        if target=='auto': target='selection' if self.context.get('selected') else 'document'
        targets=refs or [target]
        known={'document','selection','created'}|{o['ref'] for o in self.scene}|{o.get('owner','') for o in self.scene}|{o.get('molecule','') for o in self.scene}
        known.discard('')
        if any(t not in known for t in targets): raise ValueError('对象引用不存在，请先 inspect_document 查询')
        def matches(o):
            in_target=any(t=='document' or t=='selection' and o.get('selected') or t=='created' and o.get('owner') or t in (o['ref'],o.get('owner'),o.get('molecule')) for t in targets)
            in_kind=kind=='all' or kind==o['kind'] or kind=='graphics' and o['kind'] not in ('atom','bond','text','group')
            return in_target and in_kind and (not element or o['kind']=='atom' and o.get('element')==element and o.get('is_element',True) not in (False,'false')) and (not contains or o['kind']=='text' and contains in o.get('text',''))
        return [o for o in self.scene if matches(o)]

    def inspect(self,args):
        chosen=self.select(args)
        limit=args.get('limit',60);offset=args.get('offset',0)
        if type(limit)!=int or not 1<=limit<=150 or type(offset)!=int or offset<0: raise ValueError('limit 范围为 1–150，offset 为非负整数')
        groups={}
        for o in chosen:
            name=o.get('owner') or o.get('molecule') or o['ref']
            groups.setdefault(name,[]).append(o)
        summaries=[{'ref':name,'objects':len(items),'bounds':bounds(items),'elements':{e:sum(o.get('element')==e for o in items) for e in sorted({o.get('element') for o in items if o.get('element')})}} for name,items in groups.items()]
        page={'width_mm':float(self.context.get('page_width_mm',210)), 'height_mm':float(self.context.get('page_height_mm',297)),
              'across':int(self.context.get('page_across',1)), 'down':int(self.context.get('page_down',1))}
        return {'page':page,'matched':len(chosen),'bounds':bounds(chosen),'objects':chosen[offset:offset+limit],'groups':summaries[:100], 'next_offset':offset+limit if offset+limit<len(chosen) else None,'snapshot_truncated':self.scene_truncated,'document_total':self.document_total if self.scene_truncated else len(self.scene),'document_total_scope':'initial-document' if self.scene_truncated else 'staged-document','selection_complete':self.selection_complete,'matched_scope':'snapshot' if self.scene_truncated else 'document','note':'已包含本轮暂存操作；包围盒用于布局，文字宽度为估计值。大文档快照最多1500个对象、优先选区；matched只统计返回快照，不是全文数量。document筛选在宿主全文执行，未返回对象的坐标与布局不可推测。'}

    def record_creation(self,node):
        from chemistry_open import Chem
        handle=self.new_handle();node.set('handle',handle);objects=[]
        common={'owner':handle,'selected':False}
        if node.tag=='molecule':
            root=ET.fromstring(node.text);atoms=list(root.iter('n'));raw={a.get('id'):tuple(map(float,a.get('p','0 0').split())) for a in atoms}
            box=[min(p[0] for p in raw.values()),min(p[1] for p in raw.values()),max(p[0] for p in raw.values()),max(p[1] for p in raw.values())]
            factor=float(self.context.get('bond_length',14.4))/max(.1,float(root.get('BondLength',14.4)))
            points={i:((p[0]-(box[0]+box[2])/2)*factor+float(node.get('x',0)),(p[1]-(box[1]+box[3])/2)*factor+float(node.get('y',0))) for i,p in raw.items()}
            for a in atoms:
                x,y=points[a.get('id')];objects.append({**common,'ref':handle+'/atom:'+a.get('id'),'kind':'atom','element':Chem.GetPeriodicTable().GetElementSymbol(int(a.get('Element','6'))),'x':x,'y':y,'x2':x,'y2':y})
            for b in root.iter('b'):
                a,z=points[b.get('B')],points[b.get('E')];objects.append({**common,'ref':handle+'/bond:'+b.get('id'),'kind':'bond','begin':handle+'/atom:'+b.get('B'),'end':handle+'/atom:'+b.get('E'),'order':b.get('Order','1'),'x':min(a[0],z[0]),'y':min(a[1],z[1]),'x2':max(a[0],z[0]),'y2':max(a[1],z[1])})
        else:
            if node.tag=='draw_path':
                points=[(float(p.get('x')),float(p.get('y'))) for p in node.findall('point')];box=[min(p[0] for p in points),min(p[1] for p in points),max(p[0] for p in points),max(p[1] for p in points)]
            else:
                x,y=float(node.get('x',0)),float(node.get('y',0));x2,y2=float(node.get('x2',x)),float(node.get('y2',y));box=[min(x,x2),min(y,y2),max(x,x2),max(y,y2)]
            kind='text' if node.tag=='add_text' else 'path' if node.tag=='draw_path' else 'arrow'
            item={**common,'ref':handle,'kind':kind,**dict(zip(('x','y','x2','y2'),box))}
            if kind=='text':
                item['text']=node.text or '';item['size']=float(node.get('size',10));tw,th=text_extent(item['text'],item['size']);item['x2']=item['x']+tw;item['y']-=item['size'];item['y2']=item['y']+th
            objects=[item]
        for o in objects: o['color']=node.get('color',self.context.get('color','#000000'))
        self.scene.extend(objects)
        return {'ref':handle,'bounds':bounds(objects),'objects':len(objects)}

    def edit(self,name,args,number,style):
        args=dict(args)
        if name=='edit_text':args.setdefault('kind','text')
        chosen=self.select(args)
        if not chosen and not self.scene_truncated:raise ValueError('没有匹配对象，请查询范围或更换筛选条件；没有暂存任何修改')
        attrs={k:str(args[k]) for k in ('target','kind','element','contains') if k in args}
        attrs.update(style(args));node=ET.Element(name,attrs)
        for ref in args.get('refs',[]):ET.SubElement(node,'ref',{'id':ref})
        result={'staged':True,'matched':len(chosen),'snapshot_truncated':self.scene_truncated}
        if name=='style_objects':
            if not any(k in args for k in ('color','width')):raise ValueError('请指定颜色或线宽')
            for o in chosen:o.update(style(args))
        elif name in ('move_objects','duplicate_objects'):
            dx,dy=number(args,'dx'),number(args,'dy');node.set('dx',str(dx));node.set('dy',str(dy))
            moving=chosen
            # A bond transform also moves its endpoints, as in the native editor.
            endpoints={o[k] for o in chosen if o['kind']=='bond' for k in ('begin','end') if k in o}
            moving=list({o['ref']:o for o in chosen+[o for o in self.scene if o['ref'] in endpoints]}.values())
            if name=='duplicate_objects':
                # Native copy automatically includes bonds joining copied atoms.
                atomrefs={o['ref'] for o in moving if o['kind']=='atom'}
                moving=list({o['ref']:o for o in moving+[o for o in self.scene if o['kind']=='bond' and o.get('begin') in atomrefs and o.get('end') in atomrefs]}.values())
                order=['atom','text','bond','path','arrow','bracket','orbital','symbol','bio','table','group']
                moving=sorted(moving,key=lambda o:(order.index(o['kind']),int(o['ref'].split(':')[-1]) if o['ref'].split(':')[-1].isdigit() else 0))
                handle=self.new_handle();node.set('handle',handle);mapping={o['ref']:handle+'/object:'+str(i) for i,o in enumerate(moving)}
                moving=copy.deepcopy(moving)
                for o in moving:
                    o['ref']=mapping[o['ref']];o['owner']=handle;o['selected']=False;o.pop('molecule',None)
                    for k in ('begin','end'):
                        if k in o:o[k]=mapping.get(o[k],o[k])
                self.scene.extend(moving);result['ref']=handle
            for o in moving:
                for k,v in (('x',dx),('x2',dx),('y',dy),('y2',dy)):o[k]=o.get(k,0)+v
        elif name=='transform_objects':
            angle=number(args,'angle',0,-3600,3600);scale=number(args,'scale',1,.05,20);node.set('angle',str(angle));node.set('scale',str(scale))
            if chosen:
                b=bounds(chosen);cx,cy=(b[0]+b[2])/2,(b[1]+b[3])/2;co,si=math.cos(math.radians(angle)),math.sin(math.radians(angle))
                endpoints={o[k] for o in chosen if o['kind']=='bond' for k in ('begin','end') if k in o}
                moving=list({o['ref']:o for o in chosen+[o for o in self.scene if o['ref'] in endpoints]}.values())
                def transform(x,y):return (cx+scale*((x-cx)*co-(y-cy)*si),cy+scale*((x-cx)*si+(y-cy)*co))
                for o in moving:
                    corners=[(x,y) for x in (o['x'],o['x2']) for y in (o['y'],o['y2'])]
                    if o['kind']=='text':
                        # Native transforms preserve point size and rotate captions.
                        ax,ay=o['x'],o['y']+float(o.get('size',10));nx,ny=transform(ax,ay)
                        points=[(nx+(x-ax)*co-(y-ay)*si,ny+(x-ax)*si+(y-ay)*co) for x,y in corners]
                    else:points=[transform(x,y) for x,y in corners]
                    o.update(x=min(x for x,y in points),y=min(y for x,y in points),x2=max(x for x,y in points),y2=max(y for x,y in points))
        elif name=='delete_objects':
            dead={o['ref'] for o in chosen};self.scene=[o for o in self.scene if o['ref'] not in dead and not (o['kind']=='bond' and (o.get('begin') in dead or o.get('end') in dead))]
        elif name=='edit_text':
            if any(o['kind']!='text' for o in chosen):raise ValueError('edit_text 只接受文字对象')
            if not any(k in args for k in ('text','size','font','bold','italic','color')):raise ValueError('请指定文字属性')
            for k in ('text','font'):
                if k in args and (not isinstance(args[k],str) or len(args[k])>(2000 if k=='text' else 100)):raise ValueError('文字或字体名称过长')
            if 'font' in args and len(args['font'].encode('utf-8'))>200:raise ValueError('字体名称超过 200 UTF-8 字节')
            if 'size' in args:node.set('size',str(number(args,'size',10,4,96)))
            if 'text' in args:ET.SubElement(node,'text').text=args['text']
            if 'font' in args:node.set('font',args['font'])
            for k in ('bold','italic'):
                if k in args:
                    if type(args[k])!=bool:raise ValueError(k+' 必须为布尔值')
                    node.set(k,str(args[k]).lower())
            for o in chosen:
                baseline=o['y']+float(o.get('size',10))
                o.update({k:args[k] for k in ('text','font','size','bold','italic','color') if k in args})
                tw,th=text_extent(o.get('text',''),float(o.get('size',10)));o['y']=baseline-float(o.get('size',10));o['x2']=o['x']+tw;o['y2']=o['y']+th
        self.refresh_bonds()
        self.actions.append(node);result['bounds']=bounds(moving if name=='duplicate_objects' else [] if name=='delete_objects' else chosen);return result

    def arrange(self,args,number):
        refs=args['refs'];direction=args.get('direction','row')
        if not isinstance(refs,list) or not 2<=len(refs)<=100 or len(set(refs))!=len(refs) or direction not in ('row','column'):raise ValueError('排列需要 2–100 个不重复引用及 row/column 方向')
        groups=[self.select({'target':ref}) for ref in refs];flat=[o['ref'] for group in groups for o in group]
        if any(not group for group in groups) or len(flat)!=len(set(flat)):raise ValueError('排列范围为空或重叠，请使用独立的 molecule/new 引用')
        gap=number(args,'gap',30,0,500);x,y=number(args,'x'),number(args,'y')
        node=ET.Element('arrange_objects',{'direction':direction,'gap':str(gap),'x':str(x),'y':str(y)})
        for ref,group in zip(refs,groups):
            ET.SubElement(node,'ref',{'id':ref});b=bounds(group);dx,dy=(x-b[0],y-(b[1]+b[3])/2) if direction=='row' else (x-(b[0]+b[2])/2,y-b[1])
            for o in group:
                for k,v in (('x',dx),('x2',dx),('y',dy),('y2',dy)):o[k]+=v
            if direction=='row':x+=b[2]-b[0]+gap
            else:y+=b[3]-b[1]+gap
        self.refresh_bonds()
        self.actions.append(node);return {'staged':True,'groups':len(groups),'bounds':bounds([o for g in groups for o in g])}
