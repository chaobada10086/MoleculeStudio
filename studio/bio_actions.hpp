#pragma once
#include "bio_library.hpp"
void App::bioMenu(){
    if(textEditor.active&&!finishText(true))return;if(bioPlacing)cancelBioPlacement();loadBioPreferences();
    RingLibraryPicker picker;picker.model=bioLibraryModel();int chosen=picker.show(h,bioIndex,bioRecent);
    if(chosen>=0)try{bioSource=readBioTemplate(size_t(chosen));bioIndex=chosen;s.cancel();patternPlacing=false;ringPlacing=false;radicalPlacing=false;bioPlacing=true;status=bioComponents()[chosen].title+"：点击画布放置，Esc 取消";SetFocus(canvas);updateCanvasCursor();}catch(const std::exception& e){status=e.what();}
    invalidate();
}
void App::insertBioComponent(size_t index){
    try{bioSource=readBioTemplate(index);bioIndex=int(index);placeBioComponent(s.view().center);}catch(const std::exception& e){status=e.what();invalidate();}
}
void App::placeBioComponent(Vec2 center){
    if(customPlacement){placeCustomTemplate(center);return;}
    auto source=bioSource;bioPlacing=false;bioSource.reset();if(!source)return;
    const auto& entry=bioComponents().at(bioIndex);scaleBioTemplate(*source,s.doc.defaultBondLength());auto bounds=source->scene.bounds();auto delta=center-(bounds.min+bounds.max)*.5;
    auto created=std::make_shared<std::vector<ObjectRef>>();double width=s.graphicsWidth>0?s.graphicsWidth:s.doc.defaultLineWidth();auto color=s.graphicsColor;
    auto result=s.submit(Command("插入生物分子",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[source,created,delta,width,color](CommandContext& c){auto e=studioio::append(source->doc,c.document,source->all(),delta,created.get());if(!e.ok())return CommandResult::fail(e.code,e.message);for(auto ref:*created){e=graphic_style::apply(c.document,ref,width,color,true,true);if(!e.ok())return CommandResult::fail(e.code,e.message);}return CommandResult::success();}),"插入生物分子");
    if(!result.ok()){error(result);updateCanvasCursor();return;}
    s.activateCommand("chooseSelectionTool_Marquee");s.selected=*created;rememberBio(bioIndex);status=entry.title+" · 可编辑原子与化学键";updateCanvasCursor();invalidate();
}
void App::cancelBioPlacement(){customPlacement=false;bioPlacing=false;bioSource.reset();s.ghost.clear();status="已取消生物分子放置";updateCanvasCursor();invalidate();}
void App::loadBioPreferences(){
    if(bioPreferencesLoaded)return;bioPreferencesLoaded=true;auto path=interfacePath();wchar_t value[512]={};auto find=[&](){for(size_t i=0;i<bioComponents().size();++i)if(W(bioComponents()[i].file)==value)return int(i);return -1;};
    GetPrivateProfileStringW(L"BioLibrary",L"Last",L"",value,512,path.c_str());int last=find();if(last>=0)bioIndex=last;
    for(int n=0;n<12;++n){auto key=L"Recent"+std::to_wstring(n);GetPrivateProfileStringW(L"BioLibrary",key.c_str(),L"",value,512,path.c_str());int id=find();if(id>=0&&std::find(bioRecent.begin(),bioRecent.end(),id)==bioRecent.end())bioRecent.push_back(id);}
}
void App::rememberBio(int index){
    loadBioPreferences();bioIndex=index;bioRecent.erase(std::remove(bioRecent.begin(),bioRecent.end(),index),bioRecent.end());bioRecent.insert(bioRecent.begin(),index);if(bioRecent.size()>12)bioRecent.resize(12);if(testing)return;
    auto path=interfacePath();WritePrivateProfileStringW(L"BioLibrary",L"Last",W(bioComponents()[index].file).c_str(),path.c_str());
    for(int n=0;n<12;++n){auto key=L"Recent"+std::to_wstring(n);WritePrivateProfileStringW(L"BioLibrary",key.c_str(),n<int(bioRecent.size())?W(bioComponents()[bioRecent[n]].file).c_str():nullptr,path.c_str());}
}
