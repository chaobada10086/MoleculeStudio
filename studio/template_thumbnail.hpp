#pragma once
#include <functional>
// Build previews through Session, RingGeometry, glyph clipping and the canvas
// batcher. The picker never approximates chemical bonds with its own strokes.
struct TemplateThumbnail {
    std::shared_ptr<Session> source;
    render::Scene scene;render::View view;render::Batch batch;
    int width=0,height=0;double originalWidth=.6;std::map<ObjectRef,double> originalBondWidths;
    explicit TemplateThumbnail(std::shared_ptr<Session> value):source(value),originalWidth(value->doc.defaultLineWidth()){
        for(const auto& bond:source->doc.bonds().records())if(bond.isAlive()){auto ref=ObjectRef(ObjectKind::Bond,bond.id);if(auto width=source->doc.extensions(ref).findValue("studio.graphics.width"))originalBondWidths[ref]=std::atof(width->c_str());}
    }
    void prepare(int w,int h){
        if(width==w&&height==h)return;width=w;height=h;
        view=render::View();view.widthPx=w;view.heightPx=h;
        view.transform.dpiScale=1;view.transform.scale=1;
        view.transform.viewportCenter=Vec2(w*.5,h*.5);
        source->renderView=view;source->refresh();
        Box bounds=source->scene.bounds();
        if(bounds.empty()){scene.clear();batch=render::Batch();return;}
        view.transform.center=(bounds.min+bounds.max)*.5;
        view.transform.scale=std::min({(w-12)/std::max(1.,bounds.width()),(h-10)/std::max(1.,bounds.height()),23./source->doc.defaultBondLength()});
        // Imported biomolecule templates use a longer source bond length. Keep
        // their thin strokes legible after fitting a thumbnail, with the same
        // native stroke geometry and clipping as the canvas.
        source->doc.setDefaultLineWidth(std::max(originalWidth,.85/view.transform.scale));
        for(auto width:originalBondWidths)source->doc.extensions(width.first).set("studio.graphics.width",std::to_string(std::max(width.second,.85/view.transform.scale)));
        view.defaultStrokeWidth=source->doc.defaultLineWidth();view.defaultTextSize=source->doc.defaultTextSize();
        source->renderView=view;source->refresh();scene=source->scene;
        // Expanded atoms/bonds share source IDs; each contour needs its own key.
        for(auto& primitive:scene.mutablePrimitives())primitive.source=ObjectRef();
        render::Batcher batcher;render::BatchStats stats;
        batcher.sync(scene,view,source->measurer.get(),&stats,true);
        batcher.buildBatch(scene,view,false,&batch,&stats,true);
    }
    void paint(Graphics& g,RectF rect){
        prepare(std::max(16,int(rect.Width)),std::max(16,int(rect.Height)));
        auto saved=g.Save();g.SetClip(rect,CombineModeIntersect);g.TranslateTransform(rect.X,rect.Y);
        olepreview::fillBatch(g,batch,view.defaultStrokeWidth*view.transform.scale,&scene,&view);g.Restore(saved);
    }
};
struct TemplateLibraryParent {std::string title,english,aliases,category;int firstVariant=0,variantCount=0;TemplateLibraryParent(std::string t,std::string e,std::string a,std::string c,int f,int n):title(t),english(e),aliases(a),category(c),firstVariant(f),variantCount(n){}};
struct TemplateLibraryVariant {int parent=0;std::string title,formula;TemplateLibraryVariant(int p,std::string t,std::string f):parent(p),title(t),formula(f){}};
struct TemplateLibraryModel {
    std::string title="其他环",noun="母体",cue="搜索中文名、英文名、BCP、DABCO…";
    const wchar_t* windowClass=L"MoleculeStudioRingLibrary";int cardHeight=137,thumbnailHeight=77;
    std::vector<std::string> categories;
    std::vector<TemplateLibraryParent> parents;std::vector<TemplateLibraryVariant> variants;
    std::function<std::shared_ptr<Session>(int)> load;
    std::map<int,std::shared_ptr<TemplateThumbnail>> thumbnails;
    std::map<int,std::string> failures;
    bool matches(int parent,const std::wstring& query)const{
        const auto& p=parents.at(parent);auto lower=[](std::wstring t){for(auto& c:t)c=std::towlower(c);return t;};
        return lower(W(p.title+" "+p.english+" "+p.aliases)).find(lower(query))!=std::wstring::npos;
    }
    void paint(Graphics& g,int index,RectF rect){
        if(!thumbnails.count(index)&&!failures.count(index))try{auto session=load(index);if(!session)throw std::runtime_error("无法读取模板");thumbnails[index]=std::make_shared<TemplateThumbnail>(session);if(variants[index].formula.empty()){auto formula=studio::analyze(session->doc,session->all());if(formula.valid)variants[index].formula=formula.plain;}}catch(const std::exception& e){failures[index]=e.what();}
        auto found=thumbnails.find(index);if(found!=thumbnails.end())found->second->paint(g,rect);else txt(g,"模板无法读取",rect,11,muted,false,true);
    }
};
