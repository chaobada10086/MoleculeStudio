import math
import xml.etree.ElementTree as ET


def cycle_schema():
    line = {'type':'string','minLength':1,'maxLength':120}
    return {'title':line,'coupling':{'type':'object','description':'辅环到紧邻前一主环的交换支线，两端填转变的从1起步号。','properties':{key:{'type':'integer','minimum':1,'maximum':12} for key in ('parent_edge','couple_edge')},'required':['parent_edge','couple_edge'],'additionalProperties':False},'nodes':{'type':'array','minItems':3,'maxItems':12,'items':{
        'type':'object','properties':{'smiles':line,'metal':{'type':'string','maxLength':2},'oxidation':{'type':'string','maxLength':5},
        'ligands':{'type':'array','maxItems':6,'items':{'type':'string','minLength':1,'maxLength':18}},
        'incoming':{'type':'string','maxLength':24},'outgoing':{'type':'string','maxLength':24},
        'incoming_smiles':line,'outgoing_smiles':line,'step':line},'required':['step'],'additionalProperties':False}}}


def draw_cycle(engine,args,origin=None,rotation=0,transfer_edge=None):
    from chemistry_open import Chem
    from ai_plugins.draw_plugin import _auto_origin,_group_bounds,_add_text,_text_width
    prior=[obj['native_cycle_box'] for obj in engine.scene if 'native_cycle_box' in obj]
    blank=not engine.scene
    left,top=_auto_origin(engine)
    if prior:
        if args.get('coupling'):
            left,top=prior[-1][2]+12,prior[-1][1]
        elif len(prior)%2:
            left,top=prior[-1][2]+36,prior[-1][1]
        else:
            left,top=prior[0][0],max(box[3] for box in prior)+36
    if origin is not None:
        left,top=origin
    anchors=[]
    groups=[]
    states=set()
    extent=0
    compact=all('metal' in node for node in args['nodes'])
    for node in args['nodes']:
        if ('metal' in node)==('smiles' in node):
            raise ValueError('每个节点选择 metal+oxidation+ligands 或完整 smiles，不能混用。')
        spec=node.get('smiles') or '['+node['metal']+']'
        if 'metal' in node and (not node.get('oxidation') or 'ligands' not in node or any(',' in value for value in node['ligands'])):
            raise ValueError('配合物必须给氧化态及配体列表，单个配体中不能含逗号。')
        mol=Chem.MolFromSmiles(spec)
        if mol is None or len(Chem.GetMolFrags(mol))!=1:
            raise ValueError('中间体必须是一个连通的 SMILES，游离试剂写入 step。')
        key=(Chem.MolToSmiles(mol),node.get('oxidation'),tuple(sorted(node.get('ligands',[]))))
        if key in states:
            raise ValueError('中间体重复，循环自动连回第一个节点。')
        states.add(key)
        drawn=engine.call('draw_smiles',dict(smiles=spec,x=left,y=top))
        members,box=_group_bounds(engine,drawn['ref'])
        atoms=[obj for obj in members if obj['kind']=='atom']
        anchors.append(atoms[0]['ref'])
        groups.append((members,box))
        extent=max(extent,math.hypot(box[2]-box[0],box[3]-box[1]))
    radius=max(75,(extent+25)/math.sin(math.pi/len(anchors)))
    if compact:
        radius=max(86,(max(_text_width(value,10) for node in args['nodes'] for value in node['ligands']+['M'])*.65+31)/math.sin(math.pi/len(anchors)))
    center_x,center_y=left+radius+extent/2+16,top+radius+extent/2+36
    margin=68 if compact else 0
    center_x+=margin
    center_y+=margin
    receipt=engine.call('arrange_catalytic_cycle',dict(refs=anchors,x=center_x,y=center_y,radius_x=radius,radius_y=radius))
    action=engine.actions[-1]
    if compact:action.set('planned_frame','true')
    if rotation:action.set('rotation',str(math.degrees(rotation)))
    if args.get('coupling'):
        previous=[item for item in engine.actions[:-1] if item.tag=='catalytic_cycle']
        link=args['coupling']
        if not previous or link['parent_edge']>len(previous[-1].findall('ref')) or link['couple_edge']>len(anchors):
            raise ValueError('耦合必须指向紧邻的上一环与当前环中存在的转变。')
        for key,value in link.items():action.set(key,str(value))
    for index,node in enumerate(args['nodes']):
        if 'metal' in node:
            ET.SubElement(action,'metal',index=str(index),element=node['metal'],oxidation=node['oxidation'],ligands=','.join(node['ligands']))
        angle=rotation+2*math.pi*(index+.5)/len(anchors)
        for field,role,offset in [('incoming','in',-.18),('outgoing','out',.18)]:
            if node.get(field) or node.get(field+'_smiles'):
                theta=angle+offset*(2.1 if index+1==transfer_edge else 1)
                arm=radius+(66 if node.get(field+'_smiles') else 48)
                px=center_x+arm*math.sin(theta)
                py=center_y-arm*math.cos(theta)
                attrs={}
                if node.get(field+'_smiles'):
                    if node.get(field):raise ValueError('同一支线请在文字和 SMILES 中二选一。')
                    partner_mol=Chem.MolFromSmiles(node[field+'_smiles'])
                    if partner_mol is None or len(Chem.GetMolFrags(partner_mol))!=1:
                        raise ValueError('结构支线必须是单个连通分子；盐或多种物质请用文字简式。')
                    partner=engine.call('draw_smiles',dict(smiles=node[field+'_smiles'],x=px,y=py))
                    members,_=_group_bounds(engine,partner['ref'])
                    attrs['partner_ref']=next(obj['ref'] for obj in members if obj['kind']=='atom')
                    partner_action=engine.actions.pop()
                    engine.actions.insert(list(engine.actions).index(action),partner_action)
                ET.SubElement(action,'branch',edge=str(index),role=role,label=node.get(field,''),x=str(px),y=str(py),**attrs)
    for index,(members,box) in enumerate(groups):
        angle=rotation+2*math.pi*index/len(groups)
        dx=center_x+radius*math.sin(angle)-(box[0]+box[2])/2
        dy=center_y-radius*math.cos(angle)-(box[1]+box[3])/2
        for obj in members:
            for field,delta in [('x',dx),('x2',dx),('y',dy),('y2',dy)]:
                obj[field]+=delta
    title=args['title'] if compact else args['title']+'（自顶部顺时针）'
    title_ref=_add_text(engine,title,left,top+12,12,'#115A50')['ref']
    bottom=center_y+radius+extent/2+22+margin
    for index,node in enumerate(args['nodes']):
        if compact:
            angle=rotation+2*math.pi*(index+.5)/len(anchors)
            value=node['step']
            if len(value)>16:raise ValueError('环边步骤名称限 16 字；进出物分别写 incoming/outgoing。')
            _add_text(engine,value,center_x+(radius-36)*math.sin(angle)-_text_width(value,9)/2,center_y-(radius-36)*math.cos(angle)+3,9)
        else:
            _add_text(engine,f"{index+1} → {(index+1)%len(anchors)+1}   "+node['step'],left,bottom+index*15,9)
    right=max(center_x+radius+extent/2+margin,left+_text_width(title,12),left+max(_text_width(node['step'],9)+60 for node in args['nodes']))
    box=[left,top,right,bottom+(0 if compact else len(anchors)*15)]
    next(obj for obj in engine.scene if obj['ref']==title_ref)['native_cycle_box']=box
    if prior or blank:
        width=max(float(engine.context.get('page_width_mm',210)), (max(item[2] for item in prior+[box])+64)*25.4/72)
        height=max(float(engine.context.get('page_height_mm',297)), (max(item[3] for item in prior+[box])+64)*25.4/72)
        engine.call('set_page_layout',dict(width_mm=width,height_mm=height))
    return {**receipt,'native_action':'catalytic_cycle','steps':len(anchors),'bounds':box,'clockwise':True,'rotation_deg':math.degrees(rotation)}


def atlas_schema():
    return {'cycles':{'type':'array','minItems':1,'maxItems':12,'items':{
        'type':'object','properties':cycle_schema(),'required':['title','nodes'],'additionalProperties':False}}}


def draw_atlas(engine,args):
    from ai_plugins.draw_plugin import _auto_origin
    cycles=args['cycles']
    groups=[]
    for cycle in cycles:
        if cycle.get('coupling'):
            if not groups or len(groups[-1])!=1:raise ValueError('辅环必须紧接其主环。')
            groups[-1].append(cycle)
        else:
            groups.append([cycle])
    left,top=_auto_origin(engine)
    cursor=left
    row_bottom=top
    receipts=[]
    for index,group in enumerate(groups):
        if index and index%2==0:
            cursor,top=left,row_bottom+20
        for offset,cycle in enumerate(group):
            rotation=0
            if len(group)==2:
                link=group[1]['coupling']
                edge=link['couple_edge' if offset else 'parent_edge']
                rotation=(1.5 if offset else .5)*math.pi-2*math.pi*(edge-.5)/len(cycle['nodes'])
            receipt=draw_cycle(engine,cycle,(cursor,top),rotation,edge if len(group)==2 else None)
            cursor=receipt['bounds'][2]+(8 if len(group)==2 and not offset else 22)
            row_bottom=max(row_bottom,receipt['bounds'][3])
            receipts.append({'title':cycle['title'],'steps':receipt['steps'],'coupled':bool(offset)})
    return {'staged':True,'native_action':'catalytic_cycle','cycles':receipts}
