#pragma once
#include "engine/catalytic.hpp"
#include "catalytic_dialog.hpp"
void App::catalyticAction(int action){
    using namespace studio::catalytic;if(textEditor.active&&!finishText(true))return;
    auto refs=studioio::expanded(s.doc,s.selected);std::vector<ObjectId> nodes;std::set<ObjectId> visited;std::vector<ObjectId> arrowsSelected;
    for(auto ref:refs)if(ref.kind==ObjectKind::Atom&&!visited.count(ref.id)){auto part=component(s.doc,ref.id);for(auto x:part)if(x.kind==ObjectKind::Atom)visited.insert(x.id);nodes.push_back(ref.id);}else if(ref.kind==ObjectKind::Arrow)arrowsSelected.push_back(ref.id);
    if(action==0||action==1){
        if(!nodes.empty()&&(nodes.size()<3||nodes.size()>12)){status="请框选 3–12 个独立中间体；空白选区可创建循环骨架。";invalidate();return;}
        CycleFields f;f.cycle=true;f.clockwise=action==0;f.title=L"催化循环与金属配体";f.labels={L"步骤数（空白新建）",L"水平半径（pt）",L"垂直半径（pt）"};f.values={std::to_string(nodes.empty()?4:nodes.size()),"130","110"};
        graphics::ShapeParams inheritedFrame;bool inherited=catalyticShape!=kInvalidObjectId&&catalyticEllipse(ObjectRef(ObjectKind::Arrow,catalyticShape),&inheritedFrame);
        if(inherited){f.values[1]=std::to_string(length(inheritedFrame.frame.axisU));f.values[2]=std::to_string(length(inheritedFrame.frame.axisV));f.fixedSize=true;f.rotation=std::atan2(inheritedFrame.frame.axisU.y,inheritedFrame.frame.axisU.x);}
        if(!nodes.empty()){f.metal=CatalyticMetal::read(s.doc,nodes.front());for(auto id:nodes)f.nodeMetals.push_back(CatalyticMetal::read(s.doc,id));f.selectedSource=std::make_shared<Session>();for(auto id:nodes)studioio::append(s.doc,f.selectedSource->doc,component(s.doc,id),Vec2(),nullptr);}
        if(!f.show(h))return;
        double count,rx,ry;if(!DrawingSettings::parse(W(f.values[0]).c_str(),3,12,&count)||count!=std::floor(count)||!DrawingSettings::parse(W(f.values[1]).c_str(),1,2000,&rx)||!DrawingSettings::parse(W(f.values[2]).c_str(),1,2000,&ry))return;
        auto configuredNodes=nodes;
        Vec2 origin=s.view().center;if(!nodes.empty()){origin=Vec2();for(auto id:nodes)origin=origin+center(s.doc,id);origin=origin*(1./nodes.size());std::sort(nodes.begin(),nodes.end(),[&](ObjectId a,ObjectId b){auto angle=[&](ObjectId id){auto v=center(s.doc,id)-origin;double x=std::atan2(v.y,v.x)+1.570796326794897;if(x<0)x+=6.283185307179586;return x;};return angle(a)<angle(b);});}
        // Keep the directed chemical order after users move nodes or reverse the layout.
        if(!nodes.empty()){std::map<ObjectId,ObjectId> next;auto root=[&](ObjectId atom){for(auto id:nodes){auto part=component(s.doc,id);if(std::find(part.begin(),part.end(),ObjectRef(ObjectKind::Atom,atom))!=part.end())return id;}return kInvalidObjectId;};for(const auto& edge:s.doc.arrows().records())if(edge.isAlive()){auto mode=s.doc.extensions(ObjectRef(ObjectKind::Arrow,edge.id)).findValue(key());if(!mode||*mode!="cycle")continue;arrows::ArrowParams p;bool valid=false;arrows::readArrow(s.doc,edge.id,&p,&valid);if(valid){auto a=root(p.electronSource.id),b=root(p.electronTarget.id);if(a!=kInvalidObjectId&&b!=kInvalidObjectId)next[a]=b;}}if(next.size()==nodes.size()){std::vector<ObjectId> order;auto id=nodes.front();while(std::find(order.begin(),order.end(),id)==order.end()&&next.count(id)){order.push_back(id);id=next[id];}if(order.size()==nodes.size()&&id==order.front())nodes=order;}}
        if(inherited)origin=inheritedFrame.frame.centre;
        auto made=std::make_shared<std::vector<ObjectRef>>();auto metal=f.metal;auto configs=f.nodeMetals;auto edited=f.editedNodes;auto sourceShape=catalyticShape;double rotation=f.rotation;
        auto e=s.submit(Command("催化循环排版",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[nodes,origin,rx,ry,count,action,made,metal,configs,edited,configuredNodes,inherited,sourceShape,rotation](CommandContext& c)mutable{try{
            if(nodes.empty())for(int i=0;i<int(count);++i)nodes.push_back((i<int(configs.size())?configs[i]:metal).apply(c.document,kInvalidObjectId,origin+Vec2(i*40,0)));
            else for(int i:edited)if(i<int(configuredNodes.size()))configs[i].apply(c.document,configuredNodes[i],origin);
            auto e=arrange(c.document,nodes,origin,rx,ry,action==0,made.get(),inherited,rotation);if(!e.ok())return CommandResult::fail(e.code,e.message);
            if(inherited){e=c.document.removeObject(ObjectRef(ObjectKind::Arrow,sourceShape));if(!e.ok())return CommandResult::fail(e.code,e.message);}
            for(auto id:nodes){auto part=component(c.document,id);made->insert(made->end(),part.begin(),part.end());}return CommandResult::success();
            }catch(const std::exception& e){return CommandResult::fail(ErrorCode::InvalidArgument,e.what());}}),"催化循环排版");
        if(e.ok()){s.selected=*made;s.activateCommand("chooseSelectionTool_Marquee");}status=e.ok()?"已生成平滑循环路径；配合物可右键收起或展开，整个操作可撤销。":e.message;
    }else if(action==2||action==3){
        if(arrowsSelected.size()!=1||nodes.size()>1){status="请选择一根循环箭头，可同时选中一个独立试剂结构；只选箭头时会插入 R 占位基团。";invalidate();return;}
        auto edge=arrowsSelected[0];if(!s.doc.extensions(ObjectRef(ObjectKind::Arrow,edge)).findValue(key())){status="请选择由催化循环工具生成的箭头。";invalidate();return;}
        arrows::ArrowParams p;bool valid=false;arrows::readArrow(s.doc,edge,&p,&valid);if(!valid)return;
        for(auto id:nodes)if(component(s.doc,id)==component(s.doc,p.electronSource.id)||component(s.doc,id)==component(s.doc,p.electronTarget.id)){status="请选择循环外独立的试剂或产物。";invalidate();return;}
        auto spot=point(s.doc,ObjectRef(ObjectKind::Arrow,edge));auto d=p.end-p.begin;auto normal=Vec2(d.y,-d.x)*(1/std::max(1.,length(d)));Vec2 place=spot+normal*(action==2?65:85);
        auto made=std::make_shared<ObjectId>(kInvalidObjectId);auto e=s.submit(Command(action==2?"加入循环试剂":"放出循环产物",kInvalidateStructure|kInvalidateGeometry,[nodes,edge,place,action,made](CommandContext& c){ObjectId atom=nodes.empty()?kInvalidObjectId:nodes.front();Error e;if(atom==kInvalidObjectId){e=c.document.addAtom(place,6,&atom);if(!e.ok())return CommandResult::fail(e.code,e.message);auto sem=c.document.atoms().alive(atom)->semantics;sem.nodeType=uint8_t(NodeType::Query);sem.showLabel=true;sem.implicitHydrogens=0;c.document.setAtomSemantics(atom,sem);c.document.setAtomLabel(atom,"R");}
            auto a=ObjectRef(ObjectKind::Atom,atom),b=ObjectRef(ObjectKind::Arrow,edge);e=link(c.document,action==2?a:b,action==2?b:a,action==2?"in":"out",action==2?.3:-.3,made.get());return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"循环支线");if(e.ok())s.selected={ObjectRef(ObjectKind::Arrow,*made)};status=e.ok()?"支线已连接；R 可用普通原子编辑修改。拖动试剂或循环中间体，连接自动跟随。":e.message;
    }else if(action==4){
        ObjectId selected=kInvalidObjectId;for(auto r:refs)if(r.kind==ObjectKind::Atom){auto a=s.doc.atoms().alive(r.id);if(a&&studio::metal_labels::find(s.doc.atomLabel(r.id).empty()?elementSymbol(a->element):s.doc.atomLabel(r.id)).valid()){if(selected!=kInvalidObjectId){status="请只选择一个金属中心。";invalidate();return;}selected=r.id;}}
        if(!refs.empty()&&selected==kInvalidObjectId){status="请选择一个金属原子，或清空选区插入新配合物。";invalidate();return;}
        CycleFields f;f.title=L"金属中心与配体";f.metal=CatalyticMetal::read(s.doc,selected);if(!f.show(h))return;
        auto made=std::make_shared<ObjectId>(selected);auto location=s.view().center;auto cfg=f.metal;
        auto e=s.submit(Command("编辑金属配体",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[made,location,cfg](CommandContext& c){try{*made=cfg.apply(c.document,*made,location);return CommandResult::success();}catch(const std::exception& e){return CommandResult::fail(ErrorCode::InvalidArgument,e.what());}}),"金属配体");
        if(e.ok()){s.selected={ObjectRef(ObjectKind::Atom,*made)};s.activateCommand("chooseSelectionTool_Marquee");}status=e.ok()?"金属、配体与显示格式已更新；可右键收起或展开。":e.message;
    }updateInfo();invalidate();
}
bool App::catalyticEllipse(ObjectRef ref,graphics::ShapeParams* out){
    if(ref.kind!=ObjectKind::Arrow)return false;graphics::ShapeParams p;bool valid=false;auto e=graphics::readShape(s.doc,ref.id,&p,&valid);if(!e.ok()||!valid||(p.form!=graphics::ShapeForm::Circle&&p.form!=graphics::ShapeForm::Oval))return false;if(out)*out=p;return true;
}
bool App::catalyticFold(bool expand){
    Error result;
    if(!studio_group_actions::foldCatalytic(s,expand,result))return false;
    status=result.ok()?s.status:result.message;
    updateInfo();invalidate();return true;
}
