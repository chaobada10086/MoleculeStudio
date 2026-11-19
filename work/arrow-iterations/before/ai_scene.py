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

MECHANISM_GUIDE = """完整多步机理优先一次调用 draw_scheme，普通步骤给 reactants、products、electron_moves。电子移动端点位于同一步反应物侧；步骤标题和条件随行排版。只有用户要求时另画总反应。双分子进攻时，将亲核体和亲电体同时列入 reactants，放在相邻位置；碱若参与弯箭头，必须画成可引用的分子，不能用条件或文字代替。质子转移需显式画出参与的 H。醇盐夺取羰基 α-H（如 Dieckmann 环化第一步）用 enolate 对象，reactants 依次放醇盐和底物的 smiles: 结构，省略 products 和 electron_moves。环化另起一步。已有图上补绘时，先用 inspect_document 读反应物的 atom、bond 引用及连接关系。
经典 PPh3/DEAD、醇和羧酸的 Mitsunobu 酯化优先调用 draw_mitsunobu，传入实际醇和酸的 SMILES，保留用户给出的手性。仅在醇碳是立体中心时标注构型反转。不同试剂或指定其他机理路径时用 draw_scheme。一般极性加成、酸碱反应、SN2 的弯箭头是双电子箭头；鱼钩只用于确有单电子转移的步骤。支链、芳环较多时给实际位点写 SMILES 原子映射号，例如 [O-:7]，在 electron_moves 引用 1:@7 或 2:@3-@4。核对给体、受体、离去键、形式电荷和产物是否相符。每行只用一个 title；已经画成结构的试剂无需再写在箭头上，条件只填用户或来源给出的信息。
小结构创建结果的 atom_refs 和 bond_refs 可直接使用。同一步协同的电子转移放入一次 draw_mechanism_step 的 moves。source 和 target 使用实际原子或键引用。target 也可使用待成键符号返回的引用。
DA [4+2] 环加成可直接画三支双电子箭头。对于 A=B–C=D 与 E=F，指定 A=B → B–C、C=D → E、E=F → A，在一次调用中生成 D–E 和 F–A。使用实际 bond/atom 引用即可。
electrons=1 绘制鱼钩，electrons=2 绘制双电子箭头。只画箭头用 generate=false。
已有箭头可通过 refs 参与推演。通用碱、亲核体和质子用 add_mechanism_reagent。
原子上的电子标记用 mark_electron_state。待成键、过渡态、部分电荷用 draw_mechanism_symbol。实际命名试剂先查 search_reagent_library。生成的产物在桌面应用动作时产生，下一轮 inspect_document 可读到实际产物。
"""

def mechanism_tools(tool, string, number):
    refs = {'type':'array', 'items':string, 'minItems':1, 'maxItems':100, 'uniqueItems':True}
    move = {'type':'object', 'required':['source','target'], 'additionalProperties':False,
            'properties':{'source':string, 'target':string, 'electrons':{'type':'integer','enum':[1,2]},
                          'bend':{'type':'number','minimum':-2,'maximum':2}, 'route':{'type':'boolean'}}}
    return [
        tool('arrange_catalytic_cycle','按顺序排列 3–12 个独立中间体并连接原生催化箭头。每个中间体指定一个 atom 引用。最终布局在桌面应用时计算。',
             {'refs':{'type':'array','items':string,'minItems':3,'maxItems':12},'x':number,'y':number,
              'radius_x':number,'radius_y':number,'clockwise':{'type':'boolean'}},['refs','x','y']),
        tool('search_reagent_library','按名称、别名、分子式或类别查找本机试剂库，返回可绘制的条目 ID。',
             {'query':string,'offset':{'type':'integer','minimum':0}},['query']),
        tool('draw_library_reagent','按搜索所得 ID 插入工具栏的试剂模板。返回可编辑分子引用。',
             {'id':string,'x':number,'y':number},['id','x','y']),
        tool('draw_mechanism_symbol','插入原生机理符号。virtual_bond 需要两个原子 refs；transition_state 需要被括起的结构 refs；部分电荷和等同符号需要 x、y。',
             {'symbol':{'type':'string','enum':['partial_positive','partial_negative','equivalence','virtual_bond','transition_state']},
              'refs':refs,'x':number,'y':number},['symbol']),
        tool('add_mechanism_reagent','插入有化学语义的试剂原子，返回可直接作为电子端点的 ref。',
             {'role':{'type':'string','enum':['base','nucleophile','nucleophile-neutral','proton']},
              'x':number,'y':number}, ['role','x','y']),
        tool('mark_electron_state','在指定原子上调用编辑器的孤对电子、自由基或离子工具。正负离子和自由基按工具栏规则处理氢数。',
             {'refs':refs,'circled':{'type':'boolean','description':'为正负电荷加圈'},'state':{'type':'string','enum':['lone_pair','radical','positive','negative']}}, ['refs','state']),
        tool('draw_mechanism_step','绘制原生机理箭头。已有结构上补一支或多支弯箭头：提交同一步的 moves，并传 generate=false。推演下一步时传 generate=true。省略 generate 时，补箭头请求默认只画箭头，其他请求沿用生成下一步；refs 可指定已有箭头及参与结构。产物引用在应用后的下一轮可读。',
             {'moves':{'type':'array','items':move,'maxItems':100},'refs':refs,'generate':{'type':'boolean'},
              'arrow_style':{'type':'string','enum':['forward','reversible','favor_left','favor_right']},
              'mode':{'type':'string','enum':['arrows','proton_transfer','deprotonate']}}),
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
    def crosses_reaction_arrow(self, start, end, divider):
        x,y=start
        x2,y2=end
        mid=(divider['x']+divider['x2'])/2
        if not min(x,x2)<mid<max(x,x2):
            return False
        crossing_y=y+(y2-y)*(mid-x)/(x2-x)
        return abs(crossing_y-(divider['y']+divider['y2'])/2)<=2*float(self.context.get('bond_length',14.4))

    def check_electron_span(self,x,y,x2,y2):
        for arrow in self.scene:
            if arrow.get('reaction_divider') and self.crosses_reaction_arrow((x,y),(x2,y2),arrow):
                raise ValueError('电子箭头横跨反应箭头。把两个反应伙伴都放在反应箭头左侧，再用原子或键引用连接反应中心。')

    def check_reaction_position(self,divider):
        sites={o['ref']:o for o in self.scene}
        for arrow in self.scene:
            source,target=sites.get(arrow.get('source')),sites.get(arrow.get('target'))
            if not arrow.get('mechanism') or not source or not target:
                continue
            if self.crosses_reaction_arrow(self.electron_center(source),self.electron_center(target),divider):
                raise ValueError('反应箭头放在了电子来源和受体之间。请将反应箭头置于两个反应伙伴的同一侧。')

    @staticmethod
    def electron_center(obj):
        return tuple((obj[k]+obj.get(k+'2',obj[k]))/2 for k in ('x','y'))

    def check_electron_scope(self, source, target):
        endpoints = {o['ref']:o for o in self.scene if o['ref'] in (source,target)}
        a,b=endpoints[source],endpoints[target]
        if source==target:
            raise ValueError('电子箭头起点和终点指向同一对象，请核对原子或键。')
        if getattr(self,'reactant_arrows_only',False) and any(
                obj.get('reaction_side') == 'products' for obj in (a,b)):
            raise ValueError('电子端点位于产物列。请在下一步反应物上绘制电子移动。')
        if a.get('reaction_row') and b.get('reaction_row'):
            if (a['reaction_row'],a['reaction_side'])!=(b['reaction_row'],b['reaction_side']):
                raise ValueError('电子箭头跨越了反应步骤或产物列。请选择同一步同侧的原子或键。')
            if getattr(self,'reactant_arrows_only',False) and a['reaction_side']=='products':
                raise ValueError('这两个端点在本行产物列。请从本行 reactants 选择电子来源和受体；若该产物参与下一步，请在下一行反应物上画。')
            return
        self.check_electron_span(*self.electron_center(a),*self.electron_center(b))

    def bind_electron_points(self, attrs, source=None, target=None):
        sites={o['ref']:o for o in self.scene if o['kind'] in ('atom','bond')}
        tolerance=.31*float(self.context.get('bond_length',14.4))
        def resolve(ref,x,y):
            if ref:
                if ref not in sites:
                    raise ValueError('电子箭头端点需要有效的原子或键引用。')
                return ref
            distances=sorted((math.dist((x,y),self.electron_center(obj)),key) for key,obj in sites.items())
            if not distances or distances[0][0]>tolerance:
                return None
            if len(distances)>1 and distances[1][0]-distances[0][0]<tolerance*.15:
                raise ValueError('电子端点靠近多个对象。请从 inspect_document 选择确切的原子或键引用。')
            return distances[0][1]
        x,y,x2,y2=(float(attrs[k]) for k in ('x','y','x2','y2'))
        source,target=resolve(source,x,y),resolve(target,x2,y2)
        if bool(source)!=bool(target):
            raise ValueError('电子箭头有一端落在空白处。请指定同一步的电子来源和受体；涉及氢转移时，先画出对应的氢。')
        if source:
            self.check_electron_scope(source,target)
            attrs.update(source_ref=source,target_ref=target)
            x,y=self.electron_center(sites[source])
            x2,y2=self.electron_center(sites[target])
            attrs.update(x=str(x),y=str(y),x2=str(x2),y2=str(y2))
        else:
            if getattr(self,'require_bound_arrows',False):
                raise ValueError('两个端点均未绑定。请用 inspect_document 查反应物的 atom 或 bond 引用，再传 source 和 target。')
            self.check_electron_span(x,y,x2,y2)

    def mechanism_action(self, name, args, number):
        objects = {obj['ref']:obj for obj in self.scene}
        if name == 'add_mechanism_reagent':
            role = args['role']
            if role not in ('base','nucleophile','nucleophile-neutral','proton'):
                raise ValueError('未知的机理试剂')
            x, y = number(args,'x'), number(args,'y')
            ref = self.new_handle()
            self.actions.append(ET.Element('mechanism_reagent', mode=role, handle=ref, x=str(x), y=str(y)))
            self.scene.append(dict(ref=ref, owner=ref, kind='atom', selected=False, x=x,y=y,x2=x,y2=y,
                                   role=role, label={'base':'B','proton':'H'}.get(role,'Nu'),
                                   element='H' if role=='proton' else 'C', is_element='true' if role=='proton' else 'false',
                                   charge=1 if role=='proton' else 0 if role.endswith('neutral') else -1))
            return {'staged':True, 'ref':ref}
        refs = args.get('refs', [])
        if not isinstance(refs,list) or len(refs)>100 or any(not isinstance(r,str) or r not in objects and
                not any(o.get('owner')==r or o.get('molecule')==r for o in self.scene) for r in refs) or len(set(refs))!=len(refs):
            raise ValueError('请提供本步真实且不重复的对象引用')
        if name == 'arrange_catalytic_cycle':
            if not 3<=len(refs)<=12 or any(objects.get(r,{}).get('kind')!='atom' for r in refs):
                raise ValueError('请选择 3–12 个独立中间体，每个指定一个原子引用')
            clockwise=args.get('clockwise',True)
            if type(clockwise) is not bool:raise ValueError('clockwise 应为布尔值')
            action=ET.Element('catalytic_cycle',x=str(number(args,'x')),y=str(number(args,'y')),
                              radius_x=str(number(args,'radius_x',85,10,2000)),radius_y=str(number(args,'radius_y',65,10,2000)),
                              clockwise=str(clockwise).lower())
            for ref in refs:ET.SubElement(action,'ref',id=ref)
            self.actions.append(action)
            return {'staged':True,'intermediates':refs,'native_layout_pending':True}
        if name == 'draw_mechanism_symbol':
            symbol=args['symbol']
            if symbol not in ('partial_positive','partial_negative','equivalence','virtual_bond','transition_state'):
                raise ValueError('未知的机理符号')
            anchored=symbol in ('virtual_bond','transition_state')
            if anchored:
                if not refs or 'x' in args or 'y' in args: raise ValueError('请指定用于定位的 refs')
                if symbol=='virtual_bond' and (len(refs)!=2 or any(objects.get(r,{}).get('kind')!='atom' for r in refs)):
                    raise ValueError('待成键需要两个不同原子')
                chosen=[o for o in self.scene if any(r in (o['ref'],o.get('owner'),o.get('molecule')) for r in refs)]
                box=bounds(chosen)
                if symbol=='transition_state':box=[box[0]-10,box[1]-10,box[2]+10,box[3]+10]
            else:
                if refs or 'x' not in args or 'y' not in args: raise ValueError('请指定符号的 x、y 坐标')
                x,y=number(args,'x'),number(args,'y')
                box=[x,y,x,y]
            ref=self.new_handle()
            action=ET.Element('mechanism_symbol',mode=symbol,handle=ref)
            if not anchored:action.attrib.update(x=str(x),y=str(y))
            for key in refs: ET.SubElement(action,'ref',id=key)
            kind={'transition_state':'bracket','virtual_bond':'arrow','equivalence':'group'}.get(symbol,'text')
            self.scene.append(dict(ref=ref,owner=ref,selected=False,kind=kind,symbol=symbol,**dict(zip(('x','y','x2','y2'),box))))
            self.actions.append(action)
            return {'staged':True,'ref':ref}
        if name == 'mark_electron_state':
            state = args['state']
            if state not in ('lone_pair','positive','negative','radical') or not refs or any(objects.get(r,{}).get('kind')!='atom' for r in refs):
                raise ValueError('电子标记需要原子引用和有效状态')
            circled=args.get('circled',False)
            if type(circled) is not bool or circled and state not in ('positive','negative'):
                raise ValueError('circled 只适用于正、负电荷')
            action = ET.Element('electron_state',mode=state,circled=str(circled).lower())
            for ref in refs:
                ET.SubElement(action,'ref',id=ref)
                objects[ref]['pending_electron_state']=state
            self.actions.append(action)
            return {'staged':True,'refs':refs}
        moves, mode = args.get('moves',[]), args.get('mode','arrows')
        generate = args.get('generate', not (moves and getattr(self,'annotate_arrows',False)))
        if type(generate) is not bool or mode not in ('arrows','proton_transfer','deprotonate'):
            raise ValueError('推演模式或 generate 参数无效')
        if not isinstance(moves,list) or len(moves)>100 or not (moves or generate and refs):
            raise ValueError('请提供同一步的电子移动，或指定要推演的对象')
        arrows = []
        seen_moves, rows = set(), set()
        for move in moves:
            if not isinstance(move,dict) or set(move)-{'source','target','bend','route','electrons'}:
                raise ValueError('电子移动参数无效')
            endpoints = [objects.get(move.get(key)) for key in ('source','target')]
            if any(not obj or obj['kind'] not in ('atom','bond') and not (index==1 and obj.get('symbol')=='virtual_bond') for index,obj in enumerate(endpoints)):
                raise ValueError('电子移动端点需要 inspect_document 返回的原子或键引用')
            pair = (move['source'], move['target'])
            if pair in seen_moves:
                raise ValueError('同一步中电子移动重复，请删除重复箭头。')
            seen_moves.add(pair)
            rows.update(obj['reaction_row'] for obj in endpoints if obj.get('reaction_row'))
            if len(rows) > 1:
                raise ValueError('一次机理调用只能包含同一步的电子移动，请按反应行分别调用。')
            self.check_electron_scope(move['source'],move['target'])
            electrons, route = move.get('electrons',2), move.get('route',True)
            if type(electrons) is not int or electrons not in (1,2) or type(route) is not bool:
                raise ValueError('electrons 应为 1 或 2，route 应为布尔值')
            points = [[(obj[k]+obj.get(k+'2',obj[k]))/2 for k in ('x','y')] for obj in endpoints]
            if points[0]==points[1]: raise ValueError('电子箭头端点重合')
            action = ET.Element('draw_arrow',style='mechanism',source_ref=move['source'],target_ref=move['target'],
                                x=str(points[0][0]),y=str(points[0][1]),x2=str(points[1][0]),y2=str(points[1][1]),
                                bend=str(number(move,'bend',.35,-2,2)),route=str(route).lower(),fishhook=str(electrons==1).lower())
            arrows.append(self.record_creation(action)['ref'])
            self.actions.append(action)
        if generate:
            styles=['forward','reversible','favor_left','favor_right']
            action = ET.Element('mechanism_step',mode=mode,arrow_style=str(styles.index(args.get('arrow_style','forward'))))
            for ref in refs+arrows: ET.SubElement(action,'ref',id=ref)
            self.actions.append(action)
        return {'staged':True,'arrow_refs':arrows,'generation_requested':generate}

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
        handle=self.new_handle();node.set('handle',handle);objects=[]
        common={'owner':handle,'selected':False}
        if node.tag=='molecule':
            from chemistry_open import Chem
            root=ET.fromstring(node.text);atoms=list(root.iter('n'));raw={a.get('id'):tuple(map(float,a.get('p','0 0').split())) for a in atoms}
            box=[min(p[0] for p in raw.values()),min(p[1] for p in raw.values()),max(p[0] for p in raw.values()),max(p[1] for p in raw.values())]
            factor=float(self.context.get('bond_length',14.4))/max(.1,float(root.get('BondLength',14.4)))
            points={i:((p[0]-(box[0]+box[2])/2)*factor+float(node.get('x',0)),(p[1]-(box[1]+box[3])/2)*factor+float(node.get('y',0))) for i,p in raw.items()}
            for a in atoms:
                x,y=points[a.get('id')];objects.append({**common,'ref':handle+'/atom:'+a.get('id'),'kind':'atom','charge':int(a.get('Charge','0')),'element':Chem.GetPeriodicTable().GetElementSymbol(int(a.get('Element','6'))),'x':x,'y':y,'x2':x,'y2':y})
            for b in root.iter('b'):
                a,z=points[b.get('B')],points[b.get('E')];objects.append({**common,'ref':handle+'/bond:'+b.get('id'),'kind':'bond','begin':handle+'/atom:'+b.get('B'),'end':handle+'/atom:'+b.get('E'),'order':b.get('Order','1'),'x':min(a[0],z[0]),'y':min(a[1],z[1]),'x2':max(a[0],z[0]),'y2':max(a[1],z[1])})
        else:
            if node.tag=='draw_path':
                points=[(float(p.get('x')),float(p.get('y'))) for p in node.findall('point')];box=[min(p[0] for p in points),min(p[1] for p in points),max(p[0] for p in points),max(p[1] for p in points)]
            else:
                x,y=float(node.get('x',0)),float(node.get('y',0));x2,y2=float(node.get('x2',x)),float(node.get('y2',y));box=[min(x,x2),min(y,y2),max(x,x2),max(y,y2)]
            kind='text' if node.tag=='add_text' else 'path' if node.tag=='draw_path' else 'arrow'
            item={**common,'ref':handle,'kind':kind,**dict(zip(('x','y','x2','y2'),box))}
            if node.tag=='draw_arrow' and node.get('style') in ('forward','equilibrium','retrosynthetic','both'):
                item['reaction_divider']=abs(x2-x)>=float(self.context.get('bond_length',14.4)) and abs(y2-y)<=.15*abs(x2-x)
            if node.get('style')=='mechanism':
                # Leave room for the curved shaft when placing labels and rows.
                bend=float(node.get('bend',.35))
                mx=(x+x2)/2-(y2-y)*bend
                my=(y+y2)/2+(x2-x)*bend
                item.update(x=min(x,x2,mx)-3,y=min(y,y2,my)-3,x2=max(x,x2,mx)+3,y2=max(y,y2,my)+3)
                item.update(mechanism=True, electrons=1 if node.get('fishhook')=='true' else 2)
                for key in ('source','target'):
                    if node.get(key+'_ref'): item[key]=node.get(key+'_ref')
            if kind=='text':
                item['text']=node.text or '';item['size']=float(node.get('size',10));tw,th=text_extent(item['text'],item['size']);item['x2']=item['x']+tw;item['y']-=item['size'];item['y2']=item['y']+th
            objects=[item]
        for o in objects: o['color']=node.get('color',self.context.get('color','#000000'))
        self.scene.extend(objects)
        result={'ref':handle,'bounds':bounds(objects),'objects':len(objects)}
        if node.tag=='molecule' and len(objects)<=40:
            result['atom_refs']=[{'ref':o['ref'],'element':o['element'],'charge':o['charge']} for o in objects if o['kind']=='atom']
            result['bond_refs']=[{'ref':o['ref'],'begin':o['begin'],'end':o['end'],'order':o['order']} for o in objects if o['kind']=='bond']
        return result

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
