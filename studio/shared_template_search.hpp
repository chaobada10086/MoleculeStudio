#pragma once
struct SharedTemplateChoice {
    int kind,index;std::string title;std::wstring path;
    SharedTemplateChoice(int k,int i,std::string t,std::wstring p={}):kind(k),index(i),title(t),path(p){}
};
void App::shareTemplateSearch(RingLibraryPicker& picker,int origin){
    picker.localParents=int(picker.model.parents.size());
    picker.model.cue="搜索全部模板库：环、生物分子、试剂、自定义模板";
    auto choices=std::make_shared<std::map<int,SharedTemplateChoice>>();
    picker.expandSharedSearch=[this,&picker,origin,choices](){
        auto append=[&](int kind,TemplateLibraryModel source,const std::vector<int>& indices,const std::vector<custom_templates::Entry>& custom){
            if(kind==origin||source.variants.empty())return;
            auto& model=picker.model;const int parentOffset=int(model.parents.size()),offset=int(model.variants.size());
            for(auto parent:source.parents){parent.firstVariant+=offset;parent.english=source.title+" · "+parent.english;model.parents.push_back(parent);}
            for(size_t i=0;i<source.variants.size();++i){
                auto variant=source.variants[i];const auto title=source.parents.at(variant.parent).title;
                variant.parent+=parentOffset;model.variants.push_back(variant);
                choices->emplace(offset+int(i),SharedTemplateChoice(kind,indices.empty()?int(i):indices.at(i),title,custom.empty()?std::wstring():custom.at(i).path));
            }
            const auto previous=model.load,next=source.load;
            model.load=[previous,next,offset](int id){return id<offset?previous(id):next(id-offset);};
        };
        append(0,ringLibraryModel(),{},{});append(1,bioLibraryModel(),{},{});
        for(bool inorganic:{true,false}){
            std::vector<int> indices;auto source=commonReagentModel(inorganic,&indices);
            append(inorganic?2:3,std::move(source),indices,{});
        }
        auto custom=custom_templates::entries();append(4,custom_templates::modelFor(custom),{},custom);
    };
    picker.chooseSharedResult=[this,choices](int result){
        const auto found=choices->find(result);if(found==choices->end())return false;
        const auto choice=found->second;
        try{
            if(bioPlacing)cancelBioPlacement();s.cancel();patternPlacing=ringPlacing=radicalPlacing=false;customPlacement=reagentPlacing=false;
            if(choice.kind==0){loadRingPreferences();ringVariant=choice.index;beginRingPlacement();return true;}
            if(choice.kind==1){bioIndex=choice.index;bioSource=readBioTemplate(size_t(choice.index));}
            else if(choice.kind==2||choice.kind==3){reagentIndex=choice.index;bioSource=readReagentTemplate(choice.index);reagentPlacing=true;}
            else{bioSource=custom_templates::read(choice.path);customPlacement=true;customPlacementName=choice.title;}
            bioPlacing=true;status=choice.title+"：点击画布放置，Esc 取消";SetFocus(canvas);updateCanvasCursor();invalidate();
        }catch(const std::exception& error){status=error.what();invalidate();}
        return true;
    };
}
