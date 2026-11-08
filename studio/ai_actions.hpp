#pragma once
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
    context->SetAttribute("application_version","0.95");context->SetAttribute("ai_tools_revision","2026-10-29");context->SetAttribute("native_ring_variants",112);context->SetAttribute("text_size",s.doc.defaultTextSize());
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
    struct Operation {std::string kind,style,text,handle,font;aiobjects::Selector selector;std::vector<Vec2> points;Vec2 from,to;double angle=0,factor=1,gap=30;bool closed=false,filled=false,bold=false,italic=false,hasBold=false,hasItalic=false,hasText=false;uint32_t fill=0;double width=0,size=10;uint32_t color=0;bool setWidth=false,setColor=false;std::shared_ptr<Session> molecule;double pageWidth=0,pageHeight=0;int pageAcross=1,pageDown=1;};std::vector<Operation> plan;
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
            p.style=e->Attribute("style")?e->Attribute("style"):"";if(p.style!="forward"&&p.style!="equilibrium"&&p.style!="retrosynthetic"&&p.style!="both")throw std::runtime_error("未知 AI 箭头");
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
        for(const auto& p:plan){Error e;std::vector<ObjectRef> refs;ObjectId id=kInvalidObjectId;
            if(p.kind=="page"){studioio::Page page=studioio::Page::get(c.document);page.width=p.pageWidth;page.height=p.pageHeight;page.across=p.pageAcross;page.down=p.pageDown;if(!page.valid())return CommandResult::fail(ErrorCode::InvalidArgument,"纸张设置无效");page.store(c.document);*pageChanged=true;continue;}
            if(p.kind=="molecule")e=studioio::append(p.molecule->doc,c.document,p.molecule->all(),Vec2(),&refs);
            else if(p.kind=="draw_shape"){const char* command=p.style=="line"?"chooseDrawingTool_Line":p.style=="rectangle"?"chooseDrawingTool_Rectangle_Hollow":"chooseDrawingTool_Circle_Hollow";auto preset=graphics::shapePresetByCommand(command);if(!preset)return CommandResult::fail(ErrorCode::InvalidArgument,"未知图形样式");auto params=graphics::resolveShapePreset(preset->presetId,p.from,p.to,c.document.defaultLineWidth());e=graphics::addShape(c.document,params,c.document.defaultLineWidth(),&id);refs.push_back(ObjectRef(ObjectKind::Arrow,id));}
            else if(p.kind=="draw_arrow"){const char* command=p.style=="equilibrium"?"chooseArrowTool_Large_Equilibrium":p.style=="retrosynthetic"?"chooseArrowTool_Large_Retrosynthetic":p.style=="both"?"chooseArrowTool_Large_Resonance":"chooseArrowTool_Large_Solid";auto preset=arrows::presetByCommand(command);if(!preset)return CommandResult::fail(ErrorCode::InvalidArgument,"未知箭头样式");auto params=arrows::resolvePreset(preset->presetId,p.from,p.to,c.document.defaultLineWidth());e=arrows::addArrow(c.document,params,c.document.defaultLineWidth(),&id);refs.push_back(ObjectRef(ObjectKind::Arrow,id));}
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
            if(p.kind!="duplicate_objects")for(auto ref:refs){e=graphic_style::apply(c.document,ref,p.width,p.color,p.width>0,true);if(!e.ok())return fail(e);}inserted->insert(inserted->end(),refs.begin(),refs.end());aiobjects::created(registry,p.handle,refs,p.molecule?&p.molecule->doc:nullptr);
        }
        // Centre only the objects this turn created. Edits/moves applied to the user's existing
        // selection stay exactly where the AI asked for them; the whole turn moves together so a
        // molecule, its name and a reaction arrow keep their relative layout.
        {auto live=aiobjects::all(c.document);std::set<ObjectRef> alive(live.begin(),live.end());std::vector<ObjectRef> fresh;
         for(auto ref:*inserted)if(alive.count(ref))fresh.push_back(ref);
         if(!fresh.empty()){Box box=aiContentBounds(c.document,fresh,s.glyphs);
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
    invalidate();return status;
}
