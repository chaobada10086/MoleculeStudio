#pragma once
#include "ai_mechanism.hpp"
#include "ai_objects.hpp"
namespace {
bool aiPluginIdentifier(const char* text, bool dots) {
    if(!text || !*text) return false;
    if(*text >= '0' && *text <= '9') return false;
    for(const char* p=text; *p; ++p) {
        bool ok=(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_';
        if(dots && *p=='.') ok=true;
        if(!ok) return false;
    }
    return std::string(text).size()<=120;
}
void aiAttachPlugin(tinyxml2::XMLDocument& xml, tinyxml2::XMLElement* request) {
    wchar_t exe[32768]={};
    DWORD length=GetModuleFileNameW(0,exe,DWORD(sizeof(exe)/sizeof(exe[0])));
    if(!length || length>=DWORD(sizeof(exe)/sizeof(exe[0]))) return;
    std::wstring path(exe,length);
    size_t slash=path.find_last_of(L"\\/");
    if(slash==std::wstring::npos) return;
    std::wstring manifest=path.substr(0,slash)+L"\\ai_plugins\\plugin.xml";
    tinyxml2::XMLDocument doc;
    FILE* file=_wfopen(manifest.c_str(),L"rb");
    if(!file) return;
    auto error=doc.LoadFile(file);
    fclose(file);
    if(error!=tinyxml2::XML_SUCCESS) return;
    auto pluginDoc=doc.RootElement();
    if(!pluginDoc || std::string(pluginDoc->Name())!="plugin") return;
    const char* module=pluginDoc->Attribute("module");
    const char* entry=pluginDoc->Attribute("entry");
    if(!aiPluginIdentifier(module,true) || !aiPluginIdentifier(entry,false)) return;
    std::string relative=pluginDoc->Attribute("path")?pluginDoc->Attribute("path"):"ai_plugins";
    if(relative.empty() || relative.size()>120 || relative.find("..")!=std::string::npos ||
       relative.find(':')!=std::string::npos || relative[0]=='/' || relative[0]=='\\') return;
    auto plugins=xml.NewElement("plugins");
    request->InsertEndChild(plugins);
    auto plugin=xml.NewElement("plugin");
    plugins->InsertEndChild(plugin);
    plugin->SetAttribute("name",pluginDoc->Attribute("name")?pluginDoc->Attribute("name"):"plugin");
    plugin->SetAttribute("version",pluginDoc->Attribute("version")?pluginDoc->Attribute("version"):"");
    plugin->SetAttribute("module",module);
    plugin->SetAttribute("entry",entry);
    plugin->SetAttribute("path",relative.c_str());
}
// Task 8: bounding box of exactly what this AI turn created, so a single translation can put
// the whole turn (molecule + caption + arrow + its text) at the visible canvas centre.
// Captions are measured with StudioTextBounds (arrange_actions.hpp) - the same layout engine
// that draws them - instead of falling back to the anchor point.
Box aiContentBounds(Document& document,const std::vector<ObjectRef>& refs,text::GlyphCache& glyphs){
    Box box(Vec2(1,1),Vec2(-1,-1));StudioTextBounds metrics(glyphs);
    for(const auto& ref:studioio::expanded(document,refs)){
        Box part;
        if(ref.kind==ObjectKind::Text&&document.texts().alive(ref.id)){if(!metrics.textInkBox(document,ref.id,&part)||part.empty())part=selection::objectBounds(document,ref);}
        else part=selection::objectBounds(document,ref);
        if(part.empty()||!selection::boxFinite(part))continue;
        if(box.empty())box=part;else{box.add(part.min);box.add(part.max);}
    }
    return box;
}
}
std::string App::aiContext(){
    if(textEditor.active)finishText(true);
    // Undo, deletes and imported documents can leave stale references in a selection.
    {auto refs=aiobjects::all(s.doc);std::set<ObjectRef> live(refs.begin(),refs.end());s.selected.erase(std::remove_if(s.selected.begin(),s.selected.end(),[&](ObjectRef ref){return !live.count(ref);}),s.selected.end());}
    aiDocument=s.doc.id();aiSerial=s.serial;aiSelection=s.selected;aiOrigin=s.view().center;aiWidth=s.graphicsWidth>0?s.graphicsWidth:s.doc.defaultLineWidth();aiColor=s.graphicsColor;
    if(!s.selected.empty()){auto bounds=studioio::selectionBounds(s);if(!bounds.empty())aiOrigin=Vec2((bounds.min.x+bounds.max.x)*.5,bounds.max.y+45);}
    tinyxml2::XMLDocument xml;auto root=xml.NewElement("request");xml.InsertEndChild(root);aiAttachPlugin(xml,root);auto context=xml.NewElement("context");root->InsertEndChild(context);
    context->SetAttribute("application_version","0.95");context->SetAttribute("ai_tools_revision","2026-11-18");context->SetAttribute("native_ring_variants",112);context->SetAttribute("text_size",s.doc.defaultTextSize());
    context->SetAttribute("selected",int(aiSelection.size()));context->SetAttribute("font",s.doc.defaultFont().c_str());context->SetAttribute("bond_length",s.doc.defaultBondLength());context->SetAttribute("line_width",aiWidth>0?aiWidth:s.doc.defaultLineWidth());context->SetAttribute("color",aiobjects::hexColor(aiColor).c_str());context->SetAttribute("atoms",int(s.doc.stats().aliveAtoms));context->SetAttribute("bonds",int(s.doc.stats().aliveBonds));
    auto page=studioio::Page::get(s.doc);
    context->SetAttribute("page_width_mm",page.width*25.4/72);
    context->SetAttribute("page_height_mm",page.height*25.4/72);
    context->SetAttribute("page_across",page.across);
    context->SetAttribute("page_down",page.down);
    std::map<std::string,int> kinds;for(auto ref:studioio::expanded(s.doc,aiSelection)){std::string kind=ref.kind==ObjectKind::Atom?"atoms":ref.kind==ObjectKind::Bond?"bonds":ref.kind==ObjectKind::Text?"texts":"graphics";++kinds[kind];}std::ostringstream scope;for(auto pair:kinds)scope<<pair.first<<":"<<pair.second<<" ";context->SetAttribute("selection_types",scope.str().c_str());
    if(!aiSelection.empty()){auto bounds=studioio::selectionBounds(s);if(!bounds.empty()){std::ostringstream box;box<<bounds.min.x-aiOrigin.x<<","<<bounds.min.y-aiOrigin.y<<","<<bounds.max.x-aiOrigin.x<<","<<bounds.max.y-aiOrigin.y;context->SetAttribute("selection_bounds_relative_to_origin",box.str().c_str());}}
    aiobjects::snapshot(s.doc,aiSelection,aiOrigin,xml,root);
    tinyxml2::XMLPrinter out;xml.Print(&out);return out.CStr();
}
void App::showAI(const std::string& prompt,bool settings){
    ai.context=[this](){return aiContext();};ai.apply=[this](const std::string& bytes){return applyAI(bytes);};ai.capture=[this](const std::wstring& path){screenshot(U(path),false,false);return true;};ai.openSettings=[this](){showSettings(1);};ai.dockChanged=[this](){if(IsWindow(h)&&canvas){layout();invalidate();}};
    if(settings){showSettings(1);return;}ai.show(h,prompt);
}
std::string App::applyAI(const std::string& bytes){
    tinyxml2::XMLDocument xml;if(xml.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement()||std::string(xml.RootElement()->Name())!="result"||!xml.RootElement()->BoolAttribute("ok"))throw std::runtime_error("AI 结果无效");
    auto actions=xml.RootElement()->FirstChildElement("actions");if(!actions||!actions->FirstChildElement())return "";
    if(s.doc.id()!=aiDocument||s.serial!=aiSerial)throw std::runtime_error("等待期间文档已更改，本轮绘图未应用。请按当前文档重新发送。");
    struct Operation {std::string kind,style,text,handle,font,sourceRef,targetRef;int arrowStyle=0;bool circled=false,clockwise=true;double radiusX=85,radiusY=65;aiobjects::Selector selector;std::vector<Vec2> points;Vec2 from,to;double angle=0,factor=1,gap=30,bend=.35,head=3.2;bool route=true,fishhook=false;bool closed=false,filled=false,bold=false,italic=false,hasBold=false,hasItalic=false,hasText=false;uint32_t fill=0;double width=0,size=10;uint32_t color=0;bool setWidth=false,setColor=false;std::shared_ptr<Session> molecule;double pageWidth=0,pageHeight=0;int pageAcross=1,pageDown=1;};std::vector<Operation> plan;
    auto scalar=[](const tinyxml2::XMLElement* e,const char* key,double fallback,double low,double high){auto value=e->Attribute(key);if(!value)return fallback;char* end=0;double n=std::strtod(value,&end);if(end==value||!end||*end||!std::isfinite(n)||n<low||n>high){std::ostringstream error;error<<"AI 数值参数无效："<<e->Name()<<"."<<key<<"="<<value<<"（允许范围 "<<low<<" 至 "<<high<<"）";throw std::runtime_error(error.str());}return n;};
    for(auto e=actions->FirstChildElement();e;e=e->NextSiblingElement()){
        Operation p;p.kind=e->Name();p.selector=aiobjects::selector(e);if(auto handle=e->Attribute("handle"))p.handle=handle;if(p.handle.size()>50)throw std::runtime_error("AI 对象句柄过长");p.width=aiWidth;p.color=aiColor;p.setWidth=e->Attribute("width")!=nullptr;p.setColor=e->Attribute("color")!=nullptr;
        // Page width is measured in points; it is not a stroke style.
        if(p.kind=="page")p.setWidth=false;
        if(p.setWidth)p.width=scalar(e,"width",0,.1,12);
        if(p.setColor){std::string color=e->Attribute("color");if(color.size()!=7||color[0]!='#'||color.find_first_not_of("0123456789abcdefABCDEF",1)!=std::string::npos)throw std::runtime_error("AI 颜色无效");p.color=(uint32_t(std::strtoul(color.c_str()+1,0,16))<<8)|255;}
        p.from=aiOrigin+Vec2(scalar(e,"x",0,-200000,200000),scalar(e,"y",0,-200000,200000));p.to=aiOrigin+Vec2(scalar(e,"x2",0,-200000,200000),scalar(e,"y2",0,-200000,200000));
        if(p.kind=="molecule"){
            p.molecule=std::make_shared<Session>();auto err=persistence::readCdxml(e->GetText()?e->GetText():"",p.molecule->doc);if(!err.ok())throw std::runtime_error("AI 结构导入失败："+err.message);
            auto& d=p.molecule->doc;if(!d.atoms().size()||d.atoms().size()>400||d.texts().size()||d.arrows().size()||d.paths().size()||d.symbols().size()||d.bioShapes().size()||d.orbitals().size()||d.tables().size()||d.brackets().size())throw std::runtime_error("AI 分子数据含有不支持的对象或超过原子数上限");
            Box bounds;for(const auto& a:d.atoms().records())if(a.isAlive())bounds.add(a.position);Vec2 center=(bounds.min+bounds.max)*.5;double factor=s.doc.defaultBondLength()/std::max(.1,d.defaultBondLength());
            for(const auto& a:d.atoms().records())if(a.isAlive())d.setAtomPosition(a.id,p.from+(a.position-center)*factor);
        }else if(p.kind=="draw_shape"){
            p.style=e->Attribute("shape")?e->Attribute("shape"):"";if(p.style!="line"&&p.style!="rectangle"&&p.style!="ellipse")throw std::runtime_error("未知 AI 图形");
        }else if(p.kind=="draw_arrow"){
            p.style=e->Attribute("style")?e->Attribute("style"):"";if(p.style!="forward"&&p.style!="equilibrium"&&p.style!="retrosynthetic"&&p.style!="both"&&p.style!="mechanism")throw std::runtime_error("未知 AI 箭头");
            p.bend=scalar(e,"bend",.35,-2,2);p.head=scalar(e,"head",3.2,1,24);p.route=!e->Attribute("route")||e->BoolAttribute("route");p.fishhook=e->BoolAttribute("fishhook");
            if(auto ref=e->Attribute("source_ref"))p.sourceRef=ref;
            if(auto ref=e->Attribute("target_ref"))p.targetRef=ref;
        }else if(p.kind=="catalytic_cycle"){
            if(p.selector.refs.size()<3||p.selector.refs.size()>12)throw std::runtime_error("催化循环需要 3–12 个独立中间体");
            p.radiusX=scalar(e,"radius_x",85,10,2000);p.radiusY=scalar(e,"radius_y",65,10,2000);
            p.clockwise=!e->Attribute("clockwise")||e->BoolAttribute("clockwise");
        }else if(p.kind=="mechanism_symbol"){
            p.style=e->Attribute("mode")?e->Attribute("mode"):"";
            static const std::set<std::string> symbols={"partial_positive","partial_negative","equivalence","virtual_bond","transition_state"};
            if(!symbols.count(p.style))throw std::runtime_error("机理符号无效");
        }else if(p.kind=="mechanism_step"||p.kind=="electron_state"||p.kind=="mechanism_reagent"){
            p.style=e->Attribute("mode")?e->Attribute("mode"):"";
            p.circled=e->BoolAttribute("circled");
            p.arrowStyle=int(scalar(e,"arrow_style",0,0,3));
            const std::set<std::string> modes=p.kind=="mechanism_step"?std::set<std::string>{"arrows","proton_transfer","deprotonate"}:
                p.kind=="electron_state"?std::set<std::string>{"positive","negative","radical","lone_pair"}:
                std::set<std::string>{"base","nucleophile","nucleophile-neutral","proton"};
            if(!modes.count(p.style)||(p.kind!="mechanism_reagent"&&p.selector.refs.empty()))throw std::runtime_error("机理操作需要有效模式及明确的对象引用");
        }else if(p.kind=="add_text"){
            p.text=e->GetText()?e->GetText():"";if(p.text.empty()||p.text.size()>8000)throw std::runtime_error("AI 文字为空或过长");p.size=scalar(e,"size",10,4,96);
        }else if(p.kind=="draw_path"){
            p.closed=e->BoolAttribute("closed");p.filled=e->Attribute("fill")!=nullptr;if(p.filled){std::string color=e->Attribute("fill");if(color.size()!=7||color[0]!='#'||color.find_first_not_of("0123456789abcdefABCDEF",1)!=std::string::npos)throw std::runtime_error("AI 填充颜色无效");p.fill=(uint32_t(std::strtoul(color.c_str()+1,0,16))<<8)|255;}
            for(auto point=e->FirstChildElement("point");point;point=point->NextSiblingElement("point")){if(p.points.size()>=128)throw std::runtime_error("AI 路径过长");p.points.push_back(aiOrigin+Vec2(scalar(point,"x",0,-200000,200000),scalar(point,"y",0,-200000,200000)));}if(p.points.size()<2)throw std::runtime_error("AI 路径至少需要两个点");
        }else if(p.kind=="style_objects"){
            if(!p.setColor&&!p.setWidth)throw std::runtime_error("未指定对象样式");
        }else if(p.kind=="move_objects"||p.kind=="duplicate_objects"){
            p.from=Vec2(scalar(e,"dx",0,-200000,200000),scalar(e,"dy",0,-200000,200000));
        }else if(p.kind=="transform_objects"){
            p.angle=scalar(e,"angle",0,-3600,3600);p.factor=scalar(e,"scale",1,.05,20);
        }else if(p.kind=="edit_text"){
            p.hasText=e->FirstChildElement("text")!=nullptr;if(p.hasText)p.text=e->FirstChildElement("text")->GetText()?e->FirstChildElement("text")->GetText():"";if(p.text.size()>8000)throw std::runtime_error("AI 文字过长");p.size=scalar(e,"size",0,4,96);if(auto f=e->Attribute("font"))p.font=f;if(p.font.size()>200)throw std::runtime_error("AI 字体名称过长");p.hasBold=e->Attribute("bold");p.bold=e->BoolAttribute("bold");p.hasItalic=e->Attribute("italic");p.italic=e->BoolAttribute("italic");
        }else if(p.kind=="arrange_objects"){
            p.style=e->Attribute("direction")?e->Attribute("direction"):"row";if(p.style!="row"&&p.style!="column")throw std::runtime_error("AI 排列方向无效");p.gap=scalar(e,"gap",30,0,500);if(p.selector.refs.size()<2)throw std::runtime_error("排列需要至少两个对象引用");
        }else if(p.kind=="delete_objects"){
        }else if(p.kind=="style_selection"||p.kind=="move_selection"){
            if(aiSelection.empty())throw std::runtime_error("请先选择要修改的对象");if(p.kind=="move_selection")p.from=Vec2(scalar(e,"dx",0,-200000,200000),scalar(e,"dy",0,-200000,200000));else if(!p.setWidth&&!p.setColor)throw std::runtime_error("未指定选中对象的样式");
        }else if(p.kind=="page"){
            p.pageWidth=scalar(e,"width",0,72,14400);p.pageHeight=scalar(e,"height",0,72,14400);double across=scalar(e,"across",1,1,20),down=scalar(e,"down",1,1,20);
            if(across!=int(across)||down!=int(down))throw std::runtime_error("AI 纸张张数无效");p.pageAcross=int(across);p.pageDown=int(down);
        }else throw std::runtime_error("AI 请求了未开放的绘图操作");
        if((p.kind=="draw_shape"||p.kind=="draw_arrow")&&p.from==p.to)throw std::runtime_error("AI 图形起终点相同");plan.push_back(std::move(p));
    }
    auto inserted=std::make_shared<std::vector<ObjectRef>>();auto selection=aiSelection;auto matched=std::make_shared<size_t>(0);auto pageChanged=std::make_shared<bool>(false);
    // Task 8: where the *current* viewport centre is, resolved at apply time (a docked pane or a
    // pan/zoom between request and reply no longer matters), and the final bounds of this turn.
    Vec2 aiTarget=s.view().toDocument(s.view().viewportCenter);auto centered=std::make_shared<Box>(Vec2(1,1),Vec2(-1,-1));
    auto err=s.submit(Command("AI 绘图",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[plan,selection,inserted,matched,pageChanged,this,aiTarget,centered](CommandContext& c){
        auto fail=[](Error e){return CommandResult::fail(e.code,e.message);};
        try{auto registry=aiobjects::registry(c.document,selection);
        bool anchored=false;
        auto exact=[&](const std::string& key,bool allowGuide=false){
            aiobjects::Selector selector;selector.target=key;
            auto refs=aiobjects::resolve(c.document,registry,selector);
            if(refs.size()!=1||(refs[0].kind!=ObjectKind::Atom&&refs[0].kind!=ObjectKind::Bond&&!(allowGuide&&virtual_bonds::isGuide(c.document,refs[0]))))
                throw std::runtime_error("无效的电子端点："+key);
            return refs[0];
        };
        for(const auto& p:plan){Error e;std::vector<ObjectRef> refs;ObjectId id=kInvalidObjectId;
            if(p.kind=="page"){studioio::Page page=studioio::Page::get(c.document);page.width=p.pageWidth;page.height=p.pageHeight;page.across=p.pageAcross;page.down=p.pageDown;if(!page.valid())return CommandResult::fail(ErrorCode::InvalidArgument,"纸张设置无效");page.store(c.document);*pageChanged=true;continue;}
            if(p.kind=="molecule")e=studioio::append(p.molecule->doc,c.document,p.molecule->all(),Vec2(),&refs);
            else if(p.kind=="catalytic_cycle"){
                std::vector<ObjectId> nodes;
                std::set<ObjectRef> members;
                for(const auto& key:p.selector.refs){
                    const auto ref=exact(key);
                    if(ref.kind!=ObjectKind::Atom||members.count(ref))throw std::runtime_error("每个催化中间体需要指定一个独立的原子引用");
                    const auto component=catalytic::component(c.document,ref.id);
                    members.insert(component.begin(),component.end());
                    nodes.push_back(ref.id);
                }
                e=catalytic::arrange(c.document,nodes,p.from,p.radiusX,p.radiusY,p.clockwise,&refs);
                anchored=true;
            }
            else if(p.kind=="mechanism_symbol"){
                ObjectKind kind=ObjectKind::Text;
                CommandResult result;
                if(p.style=="virtual_bond"){
                    if(p.selector.refs.size()!=2)throw std::runtime_error("待成键需要两个原子");
                    auto a=exact(p.selector.refs[0]),b=exact(p.selector.refs[1]);
                    if(a.kind!=ObjectKind::Atom||b.kind!=ObjectKind::Atom)throw std::runtime_error("待成键端点必须是原子");
                    e=virtual_bonds::add(c.document,a.id,b.id,&id);kind=ObjectKind::Arrow;anchored=true;
                }else if(p.style=="transition_state"){
                    if(p.selector.refs.empty())throw std::runtime_error("过渡态需要结构引用");
                    auto box=selection::selectionBounds(c.document,aiobjects::resolve(c.document,registry,p.selector));
                    if(box.empty())throw std::runtime_error("过渡态没有可框选的对象");
                    box.min=box.min-Vec2(10,10);box.max=box.max+Vec2(10,10);
                    result=transitionBracket(c.document,box,&id);kind=ObjectKind::Bracket;anchored=true;
                }else if(p.style=="equivalence"){
                    result=equivalence(c.document,p.from,p.width,p.color,id);kind=ObjectKind::Group;
                }else result=partialCharge(c.document,p.style=="partial_negative"?-1:1,p.from,id);
                if(!result.ok())return result;
                refs.emplace_back(kind,id);
            }
            else if(p.kind=="mechanism_step"){
                const auto chosen=aiobjects::resolve(c.document,registry,p.selector);
                auto step=planMechanismStep(c.document,chosen,aiobjects::all(c.document),p.style=="arrows"?0:p.style=="proton_transfer"?1:2,p.arrowStyle,false);
                auto result=step.command.execute(c);
                if(!result.ok())return result;
                refs=*step.created;anchored=true;
            }else if(p.kind=="electron_state"){
                std::vector<ObjectId> atoms;
                for(const auto& key:p.selector.refs){
                    auto ref=exact(key);
                    if(ref.kind!=ObjectKind::Atom)throw std::runtime_error("电子状态需要原子引用");
                    atoms.push_back(ref.id);
                }
                auto result=atomElectronStateCommand(atoms,p.style=="positive"?1:p.style=="negative"?-1:p.style=="radical"?2:0,p.circled).execute(c);
                if(!result.ok())return result;
            }else if(p.kind=="mechanism_reagent"){
                const bool proton=p.style=="proton";
                e=c.document.addAtom(p.from,proton?1:6,&id);
                if(e.ok()&&proton){
                    auto sem=c.document.atoms().alive(id)->semantics;
                    sem.charge=1;sem.implicitHydrogens=0;sem.showLabel=true;
                    e=c.document.setAtomSemantics(id,sem);
                }else if(e.ok()){
                    e=mechanism::setGenericRole(c.document,id,p.style);
                    if(e.ok()&&p.style.find("nucleophile")==0)c.document.extensions(ObjectRef(ObjectKind::Atom,id)).set(integration::molecular::electronMarkerKey(false),"auto");
                }
                refs.emplace_back(ObjectKind::Atom,id);
            }
            else if(p.kind=="draw_shape"){const char* command=p.style=="line"?"chooseDrawingTool_Line":p.style=="rectangle"?"chooseDrawingTool_Rectangle_Hollow":"chooseDrawingTool_Circle_Hollow";auto preset=graphics::shapePresetByCommand(command);if(!preset)return CommandResult::fail(ErrorCode::InvalidArgument,"未知图形样式");auto params=graphics::resolveShapePreset(preset->presetId,p.from,p.to,c.document.defaultLineWidth());e=graphics::addShape(c.document,params,c.document.defaultLineWidth(),&id);refs.push_back(ObjectRef(ObjectKind::Arrow,id));}
            else if(p.kind=="draw_arrow"){const char* command=p.style=="equilibrium"?"chooseArrowTool_Large_Equilibrium":p.style=="retrosynthetic"?"chooseArrowTool_Large_Retrosynthetic":p.style=="both"?"chooseArrowTool_Large_Resonance":"chooseArrowTool_Large_Solid";auto preset=arrows::presetByCommand(command);if(!preset)return CommandResult::fail(ErrorCode::InvalidArgument,"未知箭头样式");auto params=arrows::resolvePreset(p.style=="mechanism"?(p.fishhook?57:49):preset->presetId,p.from,p.to,c.document.defaultLineWidth());if(p.style=="mechanism"){
                params.family=p.route||std::fabs(p.bend)>1e-9?arrows::ArrowFamily::Arc:arrows::ArrowFamily::Straight;
                params.sweep=p.route||std::fabs(p.bend)>1e-9?arrows::ArcSweep::Free:arrows::ArcSweep::Straight;
                params.arcSagitta=core::length(p.to-p.from)*p.bend;
                params.head.shape=arrows::ArrowHeadShape::Solid;
                params.head.sides=p.fishhook?arrows::HeadSides::Left:arrows::HeadSides::Both;
                params.head.length=p.head;params.head.width=p.head*.7;
                params.mechanism=true;
                params.electronSource=p.sourceRef.empty()?arrows::mechanismAnchor(c.document,p.from,.5):exact(p.sourceRef);
                params.electronTarget=p.targetRef.empty()?arrows::mechanismAnchor(c.document,p.to,.5):exact(p.targetRef,true);
                for(auto ref:{params.electronSource,params.electronTarget})if(ref.valid()&&std::find(inserted->begin(),inserted->end(),ref)==inserted->end())anchored=true;
                if(!p.sourceRef.empty())arrows::mechanismAnchorPoint(c.document,params.electronSource,&params.begin);
                if(!p.targetRef.empty())arrows::mechanismAnchorPoint(c.document,params.electronTarget,&params.end);
                if(params.electronSource.valid()&&params.electronSource.kind==ObjectKind::Bond)params.sourceVisualAnchor=5;
                if(params.electronTarget.valid()){
                    if(params.electronTarget.kind==ObjectKind::Bond)params.targetVisualAnchor=5;
                    else if(params.electronTarget.kind==ObjectKind::Atom)params.targetVisualAnchor=1;
                }
                if(!p.sourceRef.empty()&&params.electronSource.kind==ObjectKind::Atom){
                    const auto id=params.electronSource.id;
                    const auto radicals=arrows::mechanismRadicalPoints(c.document,id);
                    const auto* pair=arrows::mechanismClosestLonePair(c.document,id,params.end);
                    if(p.fishhook&&!radicals.empty()){
                        params.sourceVisualAnchor=4;params.begin=radicals.front();
                    }else if(!p.fishhook&&pair){
                        params.sourceVisualAnchor=3;params.begin=(pair->min+pair->max)*.5;
                    }else if(c.document.atoms().alive(id)->semantics.charge<0){
                        const auto charge=arrows::mechanismChargeBox(c.document,*c.document.atoms().alive(id));
                        params.sourceVisualAnchor=2;params.begin=(charge.min+charge.max)*.5;
                    }
                }
                if(params.begin==params.end)throw std::runtime_error("电子箭头端点重合");
                params.arcSagitta=core::length(params.end-params.begin)*p.bend;
                arrows::routeMechanism(c.document,params,p.route);
                arrows::clearMechanismLabels(c.document,params);
            }e=arrows::addArrow(c.document,params,c.document.defaultLineWidth(),&id);refs.push_back(ObjectRef(ObjectKind::Arrow,id));}
            else if(p.kind=="add_text"){e=c.document.addText(p.from,p.size,p.text,&id);if(e.ok()){TextFormat f;f.fontName=c.document.defaultFont();f.colorRGBA=p.color;e=c.document.setTextFormat(id,f);}refs.push_back(ObjectRef(ObjectKind::Text,id));}
            else if(p.kind=="draw_path"){PathRecord path;path.points=p.points;path.closed=p.closed;path.fillEnabled=p.filled;path.fillRGBA=p.fill;path.style.width=p.width>0?p.width:c.document.defaultLineWidth();path.style.colorRGBA=p.color;e=c.document.addPath(path,&id);refs.push_back(ObjectRef(ObjectKind::Path,id));}
            else if(p.kind=="style_objects"||p.kind=="move_objects"||p.kind=="transform_objects"||p.kind=="delete_objects"||p.kind=="duplicate_objects"||p.kind=="edit_text"){
                auto chosen=aiobjects::resolve(c.document,registry,p.selector);if(chosen.empty())return CommandResult::fail(ErrorCode::NotFound,"没有找到符合条件的对象，本轮未修改画布");*matched+=chosen.size();
                if(p.kind=="style_objects"){for(auto ref:chosen){e=graphic_style::apply(c.document,ref,p.width,p.color,p.setWidth,p.setColor);if(!e.ok())break;}}
                else if(p.kind=="move_objects")e=r62::integration::translateDocumentObjects(c.document,chosen,p.from);
                else if(p.kind=="transform_objects"){auto b=selection::selectionBounds(c.document,chosen);auto pivot=(b.min+b.max)*.5;selection::TransformOptions options;selection::scaleObjects(c.document,chosen,pivot,p.factor,options);selection::rotateObjects(c.document,chosen,pivot,-p.angle,options);}
                else if(p.kind=="delete_objects"){auto result=commands::deleteObjects(chosen).execute(c);if(!result.ok())return result;}
                else if(p.kind=="duplicate_objects"){Session copy;e=studioio::append(c.document,copy.doc,chosen,Vec2(),nullptr);if(e.ok())e=studioio::append(copy.doc,c.document,copy.all(),p.from,&refs);}
                else{for(auto ref:chosen){if(ref.kind!=ObjectKind::Text)return CommandResult::fail(ErrorCode::InvalidArgument,"文字编辑只能作用于文字对象，请指定 kind=text");auto format=c.document.texts().get(ref.id)->format;if(!p.font.empty())format.fontName=p.font;if(p.hasBold)format.flags=p.bold?(format.flags|kBold):(format.flags&~kBold);if(p.hasItalic)format.flags=p.italic?(format.flags|kItalic):(format.flags&~kItalic);e=c.document.setTextFormat(ref.id,format);if(e.ok()&&p.size>0)e=c.document.setTextSize(ref.id,p.size);if(e.ok()&&p.hasText)e=c.document.setTextPlain(ref.id,p.text);if(e.ok()&&p.setColor)e=graphic_style::apply(c.document,ref,0,p.color,false,true);if(!e.ok())break;}}
            }
            else if(p.kind=="arrange_objects"){
                std::vector<std::vector<ObjectRef>> groups;std::vector<Box> boxes;std::set<ObjectRef> seen;
                for(auto target:p.selector.refs){aiobjects::Selector choose;choose.target=target;auto group=aiobjects::resolve(c.document,registry,choose);if(group.empty())return CommandResult::fail(ErrorCode::NotFound,"排列对象不存在");for(auto ref:group)if(!seen.insert(ref).second)return CommandResult::fail(ErrorCode::InvalidArgument,"排列对象范围重叠，请按独立分子或图形排列");groups.push_back(group);boxes.push_back(selection::selectionBounds(c.document,group));}
                double cursor=p.from.x;double vertical=p.from.y;
                for(size_t i=0;i<groups.size();++i){auto box=boxes[i];Vec2 delta=p.style=="row"?Vec2(cursor-box.min.x,vertical-(box.min.y+box.max.y)*.5):Vec2(cursor-(box.min.x+box.max.x)*.5,vertical-box.min.y);e=r62::integration::translateDocumentObjects(c.document,groups[i],delta);if(!e.ok())break;if(p.style=="row")cursor+=box.max.x-box.min.x+p.gap;else vertical+=box.max.y-box.min.y+p.gap;}*matched+=seen.size();
            }
            else if(p.kind=="move_selection")e=r62::integration::translateDocumentObjects(c.document,selection,p.from);
            else if(p.kind=="style_selection"){for(auto ref:studioio::expanded(c.document,selection)){e=graphic_style::apply(c.document,ref,p.width,p.color,p.setWidth,p.setColor);if(!e.ok())break;}}
            if(!e.ok())return fail(e);
            if(p.kind!="duplicate_objects"&&p.kind!="mechanism_step"&&p.kind!="mechanism_symbol"&&p.kind!="catalytic_cycle")for(auto ref:refs){e=graphic_style::apply(c.document,ref,p.width,p.color,p.width>0,true);if(!e.ok())return fail(e);}inserted->insert(inserted->end(),refs.begin(),refs.end());aiobjects::created(registry,p.handle,refs,p.molecule?&p.molecule->doc:nullptr);
        }
        // Centre only the objects this turn created. Edits/moves applied to the user's existing
        // selection stay exactly where the AI asked for them; the whole turn moves together so a
        // molecule, its name and a reaction arrow keep their relative layout.
        {auto live=aiobjects::all(c.document);std::set<ObjectRef> alive(live.begin(),live.end());std::vector<ObjectRef> fresh;
         for(auto ref:*inserted)if(alive.count(ref))fresh.push_back(ref);
         if(!anchored&&!fresh.empty()){Box box=aiContentBounds(c.document,fresh,s.glyphs);
             if(!box.empty()){Vec2 centre=(box.min+box.max)*.5,delta=aiTarget-centre;
                 if(std::abs(delta.x)>1e-9||std::abs(delta.y)>1e-9){Error moved=r62::integration::translateDocumentObjects(c.document,fresh,delta);if(!moved.ok())return fail(moved);}
                 *centered=aiContentBounds(c.document,fresh,s.glyphs);}}}
        return CommandResult::success();}catch(const std::exception& e){return CommandResult::fail(ErrorCode::InvalidArgument,e.what());}}),"AI 绘图");
    if(!err.ok())throw std::runtime_error(err.message);if(*pageChanged)fitPage();
    {auto live=aiobjects::all(s.doc);std::set<ObjectRef> alive(live.begin(),live.end());bool hadNew=!inserted->empty();auto filter=[&](std::vector<ObjectRef>& refs){refs.erase(std::remove_if(refs.begin(),refs.end(),[&](ObjectRef r){return !alive.count(r);}),refs.end());};filter(s.selected);filter(*inserted);if(hadNew)s.selected=*inserted;}
    status="AI 已完成 "+std::to_string(plan.size())+" 个绘图动作"+(*matched?"，编辑 "+std::to_string(*matched)+" 个对象":"")+" · Ctrl+Z 撤销";
    if(!inserted->empty()&&!*pageChanged&&!centered->empty()){
        // Never zoom in: a small molecule must not make the canvas jump. A result larger than the
        // viewport is only shrunk just enough to stay visible, about the viewport centre, so it is
        // still centred afterwards. (Choice per the task: keep centring, adapt only when needed.)
        auto view=s.view();double dpi=view.dpiScale>0?view.dpiScale:1.0;
        double availW=std::max(120.0,double(s.renderView.widthPx)-60.0),availH=std::max(120.0,double(s.renderView.heightPx)-60.0);
        double needW=centered->width()*dpi,needH=centered->height()*dpi,fit=view.scale;
        if(needW>1e-9)fit=std::min(fit,availW/needW);if(needH>1e-9)fit=std::min(fit,availH/needH);
        if(fit<view.scale){view.scale=std::max(0.02,fit);s.setView(view);}
    }
    if(h)invalidate();return status;
}
