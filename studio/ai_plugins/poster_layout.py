import math


def poster_schema():
    short = {'type':'string','minLength':1,'maxLength':64}
    def series(item, minimum, maximum):
        return {'type':'array','items':item,'minItems':minimum,'maxItems':maximum}
    return {'title':short,'columns':{'type':'integer','minimum':1,'maximum':3},
            'panels':series({'type':'object','properties':{'title':short,
                'states':series(short,3,12),'steps':series(short,3,12),
                'rows':series(series(short,1,6),1,16)},'required':['title'],'additionalProperties':False},1,12)}


def draw_poster(engine, args):
    from ai_plugins.draw_plugin import _text_width, _auto_origin, _add_text
    panels = []
    blank = not engine.scene
    for panel_index,panel in enumerate(args['panels'],1):
        states, steps, rows = [panel.get(key,[]) for key in ('states','steps','rows')]
        if len(states)!=len(steps) or not (states or rows) or rows and len({len(row) for row in rows})!=1:
            raise ValueError(f'面板 {panel_index}（{panel["title"]}）：有 {len(states)} 个状态，{len(steps)} 条转变；两者必须相等。表格各行列数必须相同。')
        if len(set(states))!=len(states):
            raise ValueError('闭环自动连接回首节点，无须重复状态。')
        node_width = max([_text_width(state,9)+16 for state in states] or [18])
        radius = max(48, len(states)*13)
        horizontal = max(radius, node_width*max(1,len(states)/4))
        table = ([['步骤','催化物种','至下一状态的变化']]+[[str(i+1),state,steps[i]] for i,state in enumerate(states)]) if states else []
        tables = [data for data in (table,rows) if data]
        widths = [[max(_text_width(row[col],9) for row in data)+16 for col in range(len(data[0]))] for data in tables]
        width = max([2*horizontal+node_width+24,_text_width(panel['title'],12)+24]+[sum(cols)+24 for cols in widths])
        height = 42+(2*radius+28 if states else 0)+sum(19*len(data)+8 for data in tables)
        panels.append((panel,radius,tables,widths,width,height,horizontal,node_width))
    columns=min(args.get('columns',2),len(panels))
    left,top=_auto_origin(engine)
    refs=[]
    def label(value,x,y,size,color):
        refs.append(_add_text(engine,value,x,y,size,color)['ref'])
    def shape(kind,x,y,right,bottom,color):
        refs.append(engine.call('draw_shape',dict(shape=kind,x=x,y=y,x2=right,y2=bottom,color=color))['ref'])
    label(args['title'],left,top+16,16,'#124C59')
    y=top+34
    col_widths=[max(p[4] for p in panels[col::columns]) for col in range(columns)]
    for start in range(0,len(panels),columns):
        group=panels[start:start+columns]
        x=left
        for col,(panel,radius,tables,widths,width,height,horizontal,node_width) in enumerate(group):
            shape('line',x,y,x+col_widths[col],y,'#19877E')
            label(panel['title'],x+8,y+18,12,'#124C59')
            cursor=y+32
            states=panel.get('states',[])
            if states:
                points=[(x+col_widths[col]/2+horizontal*math.sin(2*math.pi*i/len(states)),cursor+radius+10-radius*math.cos(2*math.pi*i/len(states))) for i in range(len(states))]
                for index,(sx,sy) in enumerate(points):
                    tx,ty=points[(index+1)%len(points)]
                    distance=math.hypot(tx-sx,ty-sy)
                    dx,dy=(tx-sx)/distance,(ty-sy)/distance
                    clearance=min((node_width/2+3)/max(abs(dx),0.001),13/max(abs(dy),0.001))
                    refs.append(engine.call('draw_arrow',dict(style='forward',x=sx+dx*clearance,y=sy+dy*clearance,x2=tx-dx*clearance,y2=ty-dy*clearance))['ref'])
                    value=states[index]
                    label(value,sx-_text_width(value,9)/2,sy+3,9,'#124C59')
                cursor+=2*radius+28
            for data,cols in zip(tables,widths):
                for row_index,row in enumerate(data):
                    cell=x+6
                    for value,span in zip(row,cols):
                        label(value,cell,cursor+12,9,'#124C59' if not row_index else '#223238')
                        cell+=span
                    cursor+=19
                    shape('line',x+4,cursor-3,cell-4,cursor-3,'#C7D9DA')
                cursor+=8
            x+=col_widths[col]+20
        y+=max(p[5] for p in group)+16
    if blank:
        engine.call('set_page_layout',dict(width_mm=max(210,(sum(col_widths)+20*columns+64)*25.4/72),height_mm=max(210,(y-top+64)*25.4/72)))
    return {'staged':True,'panels':len(panels),'cycle_lengths':[len(p[0].get('states',[])) for p in panels],'refs':refs}
