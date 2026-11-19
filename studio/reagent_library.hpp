#pragma once
#include "reagent_catalog.hpp"
void App::ionMenu(){
    if(textEditor.active&&!finishText(true))return;
    HMENU menu=CreatePopupMenu(),negative=CreatePopupMenu(),positive=CreatePopupMenu();
    for(size_t i=0;i<studioions::entries().size();++i){
        const auto& entry=studioions::entries()[i];
        if(i<=24)studio_i18n::appendMenu(negative,MF_STRING,UINT_PTR(i+1),W(std::string(entry.label)+"⁻").c_str());
        if(i>=25||std::string(entry.label)=="H")studio_i18n::appendMenu(positive,MF_STRING,UINT_PTR(i+101),W(std::string(entry.label)+"⁺").c_str());
    }
    studio_i18n::appendMenu(menu,MF_POPUP,UINT_PTR(negative),L"常见负离子");
    studio_i18n::appendMenu(menu,MF_POPUP,UINT_PTR(positive),L"常见正离子");
    POINT at;GetCursorPos(&at);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,at.x,at.y,0,h,nullptr);DestroyMenu(menu);
    if(!choice)return;int q=choice>=101?1:-1,index=choice-(q==1?101:1);
    const auto entry=studioions::entries().at(size_t(index));const auto position=s.view().center;
    auto id=std::make_shared<ObjectId>(kInvalidObjectId);
    auto e=s.submit(Command("插入常见离子",kInvalidateStructure|kInvalidateSemantics|kInvalidateGeometry|kInvalidateAppearance,[entry,q,position,id](CommandContext& c){
        auto e=c.document.addAtom(position,entry.element,id.get());if(e.ok())e=studioions::apply(c.document,*id,entry,q);
        return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
    }),"插入常见离子");error(e);if(e.ok())s.selected={ObjectRef(ObjectKind::Atom,*id)};
    s.activateCommand("chooseSelectionTool_Marquee");updateInfo();invalidate();
}
static std::shared_ptr<Session> readReagentTemplate(int index){
    const auto& entry=reagentEntries().at(size_t(index));
    wchar_t exe[32768]={};GetModuleFileNameW(nullptr,exe,32768);
    std::wstring path=exe;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\reagent_templates\\"+W(entry.file);
    auto source=std::make_shared<Session>();
    auto result=source->open(U(path));
    if(!result.ok())throw std::runtime_error(entry.title+std::string("：")+result.message);
    return source;
}
static TemplateLibraryModel commonReagentModel(bool inorganic,std::vector<int>* sourceIndices=nullptr){
    TemplateLibraryModel model;model.title=inorganic?"常见无机试剂":"常见有机合成试剂";
    model.noun="试剂";model.cue="搜索中文名、化学式或缩写";model.windowClass=L"MoleculeStudioReagentLibrary";
    model.cardHeight=187;model.thumbnailHeight=127;
    std::vector<int> indices;
    for(size_t i=0;i<reagentEntries().size();++i){
        const auto& entry=reagentEntries()[i];if(entry.inorganic!=inorganic)continue;
        if(std::string(entry.category).compare(0,std::string("生化").size(),"生化")==0)continue;
        const int local=int(indices.size());indices.push_back(int(i));
        model.parents.emplace_back(entry.title,entry.alias,entry.alias,entry.category,local,1);
        model.variants.emplace_back(local,"结构式",entry.alias);
        if(std::find(model.categories.begin(),model.categories.end(),entry.category)==model.categories.end())model.categories.push_back(entry.category);
    }
    model.load=[indices](int index){return readReagentTemplate(indices.at(size_t(index)));};
    if(sourceIndices)*sourceIndices=indices;return model;
}
void App::reagentMenu(bool inorganic){
    if(textEditor.active&&!finishText(true))return;
    if(bioPlacing)cancelBioPlacement();
    std::vector<int> indices;auto model=commonReagentModel(inorganic,&indices);
    RingLibraryPicker picker;picker.model=std::move(model);
    shareTemplateSearch(picker,inorganic?2:3);
    const int chosen=picker.show(h,0,{});
    if(chosen<0)return;
    try{
        reagentIndex=indices.at(size_t(chosen));bioSource=readReagentTemplate(reagentIndex);
        s.cancel();patternPlacing=ringPlacing=radicalPlacing=false;bioPlacing=true;reagentPlacing=true;
        status=std::string(reagentEntries()[reagentIndex].title)+"：点击画布放置，Esc 取消";
        SetFocus(canvas);updateCanvasCursor();invalidate();
    }catch(const std::exception& e){status=e.what();invalidate();}
}
void App::placeReagent(Vec2 center){
    auto source=bioSource;bioSource.reset();bioPlacing=reagentPlacing=false;if(!source)return;
    scaleBioTemplate(*source,s.doc.defaultBondLength());
    const auto bounds=source->scene.bounds();const auto delta=center-(bounds.min+bounds.max)*.5;
    auto made=std::make_shared<std::vector<ObjectRef>>();
    error(s.submit(Command("插入常见试剂",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,
        [source,delta,made](CommandContext& c){
            auto e=studioio::append(source->doc,c.document,source->all(),delta,made.get());
            return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
        }),"插入常见试剂"));
    s.selected=*made;s.activateCommand("chooseSelectionTool_Marquee");updateInfo();updateCanvasCursor();invalidate();
}
