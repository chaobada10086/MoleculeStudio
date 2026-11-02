"""Shared normalized vector shapes; no raster images or generated code execution."""
import math

def polygon(n, radius=.5, inner=None):
    count = n * 2 if inner else n
    return [[.5 + (inner if inner and i % 2 else radius) * math.cos(-math.pi/2 + i*2*math.pi/count),
             .5 + (inner if inner and i % 2 else radius) * math.sin(-math.pi/2 + i*2*math.pi/count)] for i in range(count)]

def rounded():
    return [[cx + .15*math.cos(a*math.pi/180), cy + .15*math.sin(a*math.pi/180)]
            for cx,cy,start in [(.85,.15,-90),(.85,.85,0),(.15,.85,90),(.15,.15,180)] for a in range(start,start+91,15)]

# key, Chinese title, category, normalized outline
SHAPES = [
 ('rectangle','矩形','基本形状',[[0,0],[1,0],[1,1],[0,1]]),
 ('rounded_rectangle','圆角矩形','基本形状',rounded()),
 ('ellipse','椭圆','基本形状',polygon(64)),
 ('triangle','三角形','基本形状',[[.5,0],[1,1],[0,1]]),
 ('diamond','菱形 / 判断','基本形状',[[.5,0],[1,.5],[.5,1],[0,.5]]),
 ('pentagon','五边形','基本形状',polygon(5)),
 ('hexagon','六边形','基本形状',polygon(6)),
 ('octagon','八边形','基本形状',polygon(8)),
 ('parallelogram','平行四边形 / 数据','流程图',[[.2,0],[1,0],[.8,1],[0,1]]),
 ('trapezoid','梯形','基本形状',[[.2,0],[.8,0],[1,1],[0,1]]),
 ('star5','五角星','星与标注',polygon(5,inner=.22)),
 ('star8','八角星','星与标注',polygon(8,inner=.3)),
 ('cross','十字形','基本形状',[[.35,0],[.65,0],[.65,.35],[1,.35],[1,.65],[.65,.65],[.65,1],[.35,1],[.35,.65],[0,.65],[0,.35],[.35,.35]]),
 ('right_arrow','向右箭头','块箭头',[[0,.25],[.65,.25],[.65,0],[1,.5],[.65,1],[.65,.75],[0,.75]]),
 ('left_arrow','向左箭头','块箭头',[[1,.25],[.35,.25],[.35,0],[0,.5],[.35,1],[.35,.75],[1,.75]]),
 ('up_arrow','向上箭头','块箭头',[[.25,1],[.25,.35],[0,.35],[.5,0],[1,.35],[.75,.35],[.75,1]]),
 ('down_arrow','向下箭头','块箭头',[[.25,0],[.25,.65],[0,.65],[.5,1],[1,.65],[.75,.65],[.75,0]]),
 ('double_arrow','双向箭头','块箭头',[[0,.5],[.25,0],[.25,.3],[.75,.3],[.75,0],[1,.5],[.75,1],[.75,.7],[.25,.7],[.25,1]]),
 ('chevron','燕尾箭头','块箭头',[[0,0],[.7,0],[1,.5],[.7,1],[0,1],[.3,.5]]),
 ('document','文档','流程图',[[0,0],[1,0],[1,.85]]+[[i/20,.85+.1*math.sin(i/20*2*math.pi)] for i in range(19,-1,-1)]),
 ('callout','对话标注','星与标注',[[0,0],[1,0],[1,.75],[.4,.75],[.2,1],[.2,.75],[0,.75]]),
 ('banner','旗帜','星与标注',[[0,0],[1,0],[.8,.5],[1,1],[0,1]]),
]
CATALOG = {s[0]: s for s in SHAPES}

def outline(kind, x, y, width, height):
    return [[x+px*width,y+py*height] for px,py in CATALOG[kind][3]]

def anchor(node, kind, direction):
    """Intersect the cardinal centre ray with the actual polygon border."""
    x,y,x2,y2=node['bounds'];cx=(x+x2)/2;cy=(y+y2)/2
    ux,uy=direction;points=outline(kind,x,y,x2-x,y2-y);hits=[]
    for a,b in zip(points,points[1:]+points[:1]):
        vx=b[0]-a[0];vy=b[1]-a[1];den=ux*vy-uy*vx
        if abs(den)<1e-10:continue
        ax=a[0]-cx;ay=a[1]-cy;t=(ax*vy-ay*vx)/den;s=(ax*uy-ay*ux)/den
        if t>=0 and -.00001<=s<=1.00001:hits.append(t)
    if not hits:raise ValueError('图案没有可连接的边界')
    t=min(hits);return [cx+ux*t,cy+uy*t]

def register(plugin):
    num={'type':'number'}; string={'type':'string'}
    props={'shape':{'type':'string','enum':list(CATALOG)},'x':num,'y':num,'width':num,'height':num,
           'text':string,'size':num,'fill':string,'color':string,'text_color':string,'line_width':num}
    node={'type':'object','properties':{'id':string,**props},'required':['id','text'],'additionalProperties':False}
    edge={'type':'object','properties':{'source':string,'target':string,'label':string},'required':['source','target'],'additionalProperties':False}
    plugin.PLUGIN_TOOLS.extend([
        plugin._tool('draw_autoshape','绘制 PPT 风格可编辑图案，22 种形状，可填色、设置边框和居中文字。宽高会自动扩大容纳文字；未给坐标时避开已有内容。',props,['shape']),
        plugin._tool('draw_diagram','一次绘制可编辑流程图或步骤图。nodes 按输入顺序紧凑排列，edges 通过节点 id 连接，支持跨节点外侧折线路由。自动容纳文字、统一节点尺寸、避开原有画布。',
            {'nodes':{'type':'array','items':node,'minItems':1,'maxItems':24},'edges':{'type':'array','items':edge,'maxItems':48},
             'direction':{'type':'string','enum':['row','column']},'gap':num,'x':num,'y':num},['nodes'])])
    plugin.PLUGIN_NAMES.update(('draw_autoshape','draw_diagram'))
    plugin.PLUGIN_SYSTEM += '\n画非化学图案用 draw_autoshape；流程图、步骤图优先 draw_diagram，一次给齐节点和边。不要用 SMILES 画图标。连接线是普通可编辑对象，移动节点后需重画连接；图形和文字独立可编辑。'
    plugin._HANDLERS.update({'draw_autoshape':lambda e,a:draw(plugin,e,a),'draw_diagram':lambda e,a:diagram(plugin,e,a)})

def dimensions(a):
    from ai_scene import text_extent
    size=float(a.get('size',10)); w=float(a.get('width',96)); h=float(a.get('height',52))
    if not 6<=size<=48 or not 12<=w<=1000 or not 12<=h<=1000:
        raise ValueError('字号须为 6–48，宽高须为 12–1000 pt')
    text=a.get('text','')
    if len(text)>500: raise ValueError('图内文字最多 500 字；长文请分成多个节点')
    tw,th=text_extent(text,size)
    return max(w,tw*2+24),max(h,th*2+20),size

def draw(p,e,a):
    w,h,size=dimensions(a); x=float(a.get('x',0)); y=float(a.get('y',0))
    if 'y' not in a:
        b=p._document_bounds(e)
        if b: y=b[3]+16
    line=float(a.get('line_width',1))
    if not .1<=line<=12: raise ValueError('边框线宽须为 0.1–12 pt')
    fill=a.get('fill','#E6F2EF'); fill=None if fill=='none' else fill
    shape=p._add_path(e,outline(a.get('shape','rounded_rectangle'),x,y,w,h),True,fill,a.get('color','#167D8D'),line)
    label=None
    if a.get('text'):
        from ai_scene import text_extent
        tw,th=text_extent(a['text'],size)
        label=p._add_text(e,a['text'],x+(w-tw)/2,y+(h-th)/2+size*.8,size,a.get('text_color','#24343B'))['ref']
        p._label_map(e)[label]=shape['ref']
        for obj in e.scene:
            if p._group_key(obj)==label:obj['layout_parent']=shape['ref']
    return {'staged':True,'ref':shape['ref'],'text_ref':label,'bounds':[x,y,x+w,y+h],
            'refs':[shape['ref']]+([label] if label else [])}

def diagram(p,e,a):
    nodes=a['nodes']; ids=[n['id'] for n in nodes]
    if any(not i or len(i)>80 for i in ids) or len(set(ids))!=len(ids): raise ValueError('节点 id 不能为空或重复，最长 80 字')
    edges=a.get('edges',[{'source':ids[i],'target':ids[i+1]} for i in range(len(ids)-1)])
    if any(edge['source'] not in ids or edge['target'] not in ids or edge['source']==edge['target'] for edge in edges):
        raise ValueError('连线两端必须是存在的不同节点 id')
    if len({(edge['source'],edge['target']) for edge in edges})!=len(edges):raise ValueError('请合并同一对节点的重复连线和标签')
    if any('x' in n or 'y' in n for n in nodes): raise ValueError('draw_diagram 自动排版，请在顶层设置 x/y，不要为节点设置坐标')
    widths,heights,_=zip(*(dimensions(n) for n in nodes)); w=max(widths);h=max(heights)
    row=a.get('direction','row')=='row';gap=float(a.get('gap',32))
    if not 16<=gap<=500: raise ValueError('流程图间距须为 16–500 pt')
    labels=[edge.get('label','') for edge in edges]
    if any(len(t)>100 for t in labels): raise ValueError('连线标签最长 100 字')
    if row: gap=max(gap,max((p._text_width(t,9)+16 for t in labels),default=0))
    else: gap=max(gap,32 if any(labels) else 16)
    x=float(a.get('x',0));y=float(a.get('y',0)); old=p._document_bounds(e)
    if old: y=max(y,old[3]+32+(24+len(edges)*20 if row else 0))
    out={};refs=[]
    for i,n in enumerate(nodes):
        item=draw(p,e,{**{k:v for k,v in n.items() if k!='id'},'shape':n.get('shape','rounded_rectangle'),
                      'x':x+i*(w+gap) if row else x,'y':y if row else y+i*(h+gap),'width':w,'height':h})
        out[n['id']]=item;refs.extend(item['refs'])
    routes=[]
    for index,edge in enumerate(edges):
        si=ids.index(edge['source']);ti=ids.index(edge['target']);b=out[edge['source']]['bounds'];c=out[edge['target']]['bounds']
        def at(i,d):return anchor(out[ids[i]],nodes[i].get('shape','rounded_rectangle'),d)
        if ti-si==1:
            if row: start=at(si,(1,0));end=at(ti,(-1,0))
            else: start=at(si,(0,1));end=at(ti,(0,-1))
            points=[start,end]
        elif row:
            lane=y-24-index*20;start=at(si,(0,-1));end=at(ti,(0,-1));points=[start,[start[0],lane],[end[0],lane],end]
        else:
            lane=x+w+24+index*20;start=at(si,(1,0));end=at(ti,(1,0));points=[start,[lane,start[1]],[lane,end[1]],end]
        shaft=p._add_path(e,points,color='#50656D',width=1)['ref'];refs.append(shaft)
        dx=end[0]-points[-2][0];dy=end[1]-points[-2][1];length=math.hypot(dx,dy);ux=dx/length;uy=dy/length
        head=p._add_path(e,[end,[end[0]-6*ux+3*uy,end[1]-6*uy-3*ux],[end[0]-6*ux-3*uy,end[1]-6*uy+3*ux]],True,'#50656D','#50656D',.5)['ref'];refs.append(head)
        for obj in e.scene:
            if p._group_key(obj) in (shaft,head):obj['layout_connector']=True
        text=edge.get('label',''); label=None
        if text:
            mid1,mid2=(points[0],points[1]) if len(points)==2 else (points[1],points[2])
            tx=(mid1[0]+mid2[0])/2;ty=(mid1[1]+mid2[1])/2
            label=p._add_text(e,text,tx-p._text_width(text,9)/2 if row else tx+6,ty-6 if row else ty,9,'#50656D')['ref'];refs.append(label)
        routes.append({**edge,'points':points,'refs':[shaft,head]+([label] if label else [])})
    return {'staged':True,'nodes':out,'edges':routes,'refs':refs,'bounds':p._bounds_for_handles(e,refs),'gap':gap,
            'warning':'连接线为独立对象；移动节点后请重新连接。'}
