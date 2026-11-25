#pragma once
#include "ai_document.hpp"
using studioai::aiContentBounds;
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
    const studioai::DrawContext context{aiDocument,aiSerial,aiSelection,aiOrigin,aiWidth,aiColor};
    auto result=studioai::apply(s,bytes,context,
        [this](CommandContext& command, const std::vector<ObjectRef>& selection, int mode, int style,
               bool keepLossStructures, std::vector<ObjectRef>& refs){
            auto step=planMechanismStep(command.document,selection,aiobjects::all(command.document),mode,style,
                                        false,keepLossStructures);
            auto result=step.command.execute(command);
            if(result.ok())refs=*step.created;
            return result;
        });
    if(!result.empty()){
        status=result;
        if(h)invalidate();
    }
    return result;
}
