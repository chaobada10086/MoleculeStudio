#pragma once
#include "bio_catalog.hpp"
#include "bio_library_formulas.hpp"
static std::shared_ptr<Session> readBioTemplate(size_t index){
    const auto& entry=bioComponents().at(index);wchar_t exe[32768]={};GetModuleFileNameW(0,exe,32768);std::wstring path=exe;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\bio_templates\\"+W(entry.file);
    auto source=std::make_shared<Session>();auto opened=source->open(U(path));if(!opened.ok())throw std::runtime_error(entry.title+"："+opened.message);return source;
}
static void scaleBioTemplate(Session& source,double bondLength){
    double factor=bondLength/source.doc.defaultBondLength();selection::TransformOptions options;options.textSizePolicy=selection::TextSizePolicy::ScaleWithGeometry;
    selection::scaleObjects(source.doc,source.all(),Vec2(),factor,options);
    double labelSize=integration::molecular::setting(source.doc,"LabelSize",10);
    for(const auto& atom:source.doc.atoms().records())if(atom.isAlive()){
        auto runs=studioatom::runs(source.doc,atom.id);for(auto& run:runs)run.size=(run.size>0?run.size:labelSize)*factor;if(!runs.empty())studioatom::set(source.doc,atom.id,runs);
        auto& ext=source.doc.extensions(ObjectRef(ObjectKind::Atom,atom.id));
        for(auto key:{"studio.label.offset","studio.atom.source.xy"})if(auto value=ext.findValue(key)){std::istringstream input(*value);Vec2 point;if(input>>point.x>>point.y){std::ostringstream output;output<<std::setprecision(15)<<point.x*factor<<' '<<point.y*factor;ext.set(key,output.str());}}
    }
    source.doc.documentExtensions().set("studio.style.LabelSize",std::to_string(labelSize*factor));source.doc.setDefaultTextSize(source.doc.defaultTextSize()*factor);source.doc.setDefaultBondLength(bondLength);source.refresh();
}
static TemplateLibraryModel bioLibraryModel(){
    TemplateLibraryModel model;model.title="生物分子";model.noun="分子";model.cue="搜索中文名、英文名、ATP、Ala、Haworth…";model.windowClass=L"MoleculeStudioBioLibrary";model.cardHeight=187;model.thumbnailHeight=127;
    std::map<std::string,int> groups;
    for(size_t i=0;i<bioComponents().size();++i){const auto& item=bioComponents()[i];auto separator=item.title.find(" · ");std::string title=item.title.substr(0,separator),form=separator==std::string::npos?"标准结构":item.title.substr(separator+4);auto key=item.file.substr(0,item.file.find('-'));
        if(!groups.count(key)){groups[key]=int(model.parents.size());std::string name=item.english;model.parents.push_back({title,name,item.file+" "+item.title,item.category,int(i),0});}
        int parent=groups[key];++model.parents[parent].variantCount;model.parents[parent].aliases+=" "+item.title+" "+item.file;
        model.variants.push_back({parent,form,item.formula});if(std::find(model.categories.begin(),model.categories.end(),item.category)==model.categories.end())model.categories.push_back(item.category);
    }
    model.load=[](int index){return readBioTemplate(size_t(index));};return model;
}
