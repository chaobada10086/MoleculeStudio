#pragma once
#include "bio_catalog.hpp"
#include "bio_library_formulas.hpp"
#include "projection_templates.hpp"
static std::shared_ptr<Session> readBioTemplate(size_t index){
    const auto& entry=bioComponents().at(index);wchar_t exe[32768]={};GetModuleFileNameW(0,exe,32768);std::wstring path=exe;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\bio_templates\\"+W(entry.file);
    auto source=std::make_shared<Session>();auto opened=source->open(U(path));if(!opened.ok())throw std::runtime_error(entry.title+"："+opened.message);return source;
}
#include "bio_template_scale.hpp"
static TemplateLibraryModel bioLibraryModel(){
    TemplateLibraryModel model;model.title="生物分子";model.noun="分子";model.cue="搜索中文名、英文名、ATP、Ala、Haworth…";model.windowClass=L"MoleculeStudioBioLibrary";model.cardHeight=187;model.thumbnailHeight=127;
    std::map<std::string,int> groups;
    for(size_t i=0;i<bioComponents().size();++i){const auto& item=bioComponents()[i];auto separator=item.title.find(" · ");std::string title=item.title.substr(0,separator),form=separator==std::string::npos?"标准结构":item.title.substr(separator+4);auto key=item.file.substr(0,item.file.find('-'));
        if(!groups.count(key)){groups[key]=int(model.parents.size());std::string name=item.english;model.parents.push_back({title,name,item.file+" "+item.title,item.category,int(model.variants.size()),0});}
        int parent=groups[key];++model.parents[parent].variantCount;model.parents[parent].aliases+=" "+item.title+" "+item.file;
        model.variants.push_back({parent,form,item.formula});model.variants.back().sourceIndex=int(i);if(std::find(model.categories.begin(),model.categories.end(),item.category)==model.categories.end())model.categories.push_back(item.category);
        bool last=i+1==bioComponents().size()||bioComponents()[i+1].file.substr(0,bioComponents()[i+1].file.find('-'))!=key;
        if(last&&item.category=="糖类"&&title.find("脱氧")==std::string::npos){
            for(size_t sugar=0;sugar<stereo_projection::sugars().size();++sugar)if(title.find(stereo_projection::sugars()[sugar].title)!=std::string::npos){
                model.variants.push_back({parent,"费歇尔投影",item.formula});auto& v=model.variants.back();v.sugarIndex=int(sugar);v.sugarMirror=title.find("-L-")!=std::string::npos;++model.parents[parent].variantCount;break;
            }
        }
    }
    auto variants=model.variants;model.load=[variants](int index){const auto& v=variants.at(size_t(index));if(v.sugarIndex>=0){Session style;return stereo_projection::sugar(style.doc,v.sugarIndex,v.sugarMirror);}return readBioTemplate(size_t(v.sourceIndex));};return model;
}
