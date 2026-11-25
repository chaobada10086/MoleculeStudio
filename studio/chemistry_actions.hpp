#ifndef STUDIO_CHEMISTRY_ACTIONS_HPP
#define STUDIO_CHEMISTRY_ACTIONS_HPP
#include "text_tidy.hpp"
#include "chemistry_clean.hpp"
#include "stereo_annotation.hpp"
std::vector<ObjectRef> App::chemicalScope(){
    return studio_chemistry::scope(s.doc, s.selected);
}
using studio_stereo::isProjectionGroupName;
bool App::prepareStereoLabels(std::string* output){
    std::string input;
    auto result=studio_stereo::writeInput(s,input);
    if(!result.ok()){*output=result.message;return false;}
    if(input!=stereoCacheInput){
        stereoCacheInput=input;
        stereoCacheOk=chemservice::run(h,stereoCacheInput,"text/xml","text/x-moleculestudio-stereo","stereolabels",&stereoCacheOutput);
    }
    *output=stereoCacheOutput;
    return stereoCacheOk;
}
bool App::canLabelConfiguration(){std::string result;if(!prepareStereoLabels(&result))return false;return result.find("\natom\t")!=std::string::npos||result.find("\nbond\t")!=std::string::npos||result.find("\nprojection\t")!=std::string::npos;}
bool App::chemistryAction(const std::string& op,int){
    if(op=="undoTextTidy"){
        std::vector<ObjectRef> refs;for(auto ref:studioio::expanded(s.doc,s.selected))
            if((ref.kind==ObjectKind::Text&&s.doc.texts().alive(ref.id))||(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id)&&!s.doc.atomLabel(ref.id).empty())){
                bool work=s.doc.extensions(ref).findValue(textbackup::key())!=nullptr;
                const auto runs=ref.kind==ObjectKind::Text?s.doc.runs(ref.id):studioatom::runs(s.doc,ref.id);
                for(const auto& run:runs)work|=(run.flags&(kSubscript|kSuperscript|kFormula))!=0;
                if(work)refs.push_back(ref);
            }
        if(refs.empty()){status="所选文字没有可撤销的自动排版";invalidate();return true;}
        auto e=s.submit(Command("撤销文字整理",kInvalidateAppearance|kInvalidateGeometry|kInvalidateSemantics,[refs](CommandContext& c){
            for(auto ref:refs){auto e=textbackup::restore(c.document,ref);if(!e.ok())return CommandResult::fail(e.code,e.message);}return CommandResult::success();
        }),"撤销文字整理");error(e);if(e.ok())status="已恢复所选文字整理前的内容和排版 · 可撤销";invalidate();return true;
    }
    if(op=="textTidy"){
        // One command, one undo entry. The spelling/typography passes are the
        // same chemcompletion helpers the inline editor commits with, applied
        // to labels and captions that already exist.
        std::vector<ObjectId> atoms,texts;std::set<ObjectId> atomSeen,textSeen;
        for(auto ref:chemicalScope())if(ref.kind==ObjectKind::Atom&&atomSeen.insert(ref.id).second)atoms.push_back(ref.id);
        if(s.selected.empty()){for(auto& t:s.doc.texts().records())if(t.isAlive()&&textSeen.insert(t.id).second)texts.push_back(t.id);}
        else{
            for(auto ref:studioio::expanded(s.doc,s.selected))if(ref.kind==ObjectKind::Text&&textSeen.insert(ref.id).second)texts.push_back(ref.id);
            // Selecting a structure should also reach captions that sit inside
            // its box: users select the drawing, not every caption separately.
            Box scope=studioio::selectionBounds(s);
            if(!scope.empty()&&!atoms.empty()){
                double margin=std::max(12.0,std::max(scope.max.x-scope.min.x,scope.max.y-scope.min.y)*.05);
                scope.add(Vec2(scope.min.x-margin,scope.min.y-margin));
                scope.add(Vec2(scope.max.x+margin,scope.max.y+margin));
                for(auto& t:s.doc.texts().records())if(t.isAlive()&&!textSeen.count(t.id)){
                    Box b=selection::objectBounds(s.doc,ObjectRef(ObjectKind::Text,t.id));
                    if(b.empty())continue;
                    if(b.max.x<scope.min.x||b.min.x>scope.max.x||b.max.y<scope.min.y||b.min.y>scope.max.y)continue;
                    textSeen.insert(t.id);texts.push_back(t.id);
                }
            }
        }
        if(atoms.empty()&&texts.empty()){status="请先选择要整理的结构或文字";invalidate();return true;}
        // Only hand the command real work: an all-no-op command is rejected by
        // the history, which would turn a second click into an error message.
        std::vector<ObjectId> workAtoms,workTexts;
        for(auto id:atoms)if(texttidy::needsTidyAtom(s.doc,id))workAtoms.push_back(id);
        for(auto id:texts)if(texttidy::needsTidyText(s.doc,id))workTexts.push_back(id);
        if(workAtoms.empty()&&workTexts.empty()){status="所选文字已经是整理后的格式";invalidate();return true;}
        struct TidyCount{int atoms=0,texts=0;};
        auto count=std::make_shared<TidyCount>();
        error(s.submit(Command("文字整理",kInvalidateStructure|kInvalidateAppearance|kInvalidateGeometry,[workAtoms,workTexts,count](CommandContext& c){
            for(auto id:workAtoms){bool changed=false;auto e=texttidy::tidyAtom(c.document,id,&changed);if(!e.ok())return CommandResult::fail(e.code,e.message);if(changed)++count->atoms;}
            for(auto id:workTexts){bool changed=false;auto e=texttidy::tidyText(c.document,id,&changed);if(!e.ok())return CommandResult::fail(e.code,e.message);if(changed)++count->texts;}
            return CommandResult::success();
        }),"文字整理"));
        if(count->atoms||count->texts){
            std::string detail;
            if(count->atoms)detail+="原子标签 "+std::to_string(count->atoms);
            if(count->texts)detail+=std::string(detail.empty()?"":"、")+"文字 "+std::to_string(count->texts);
            status="已整理 "+std::to_string(count->atoms+count->texts)+" 处文字（"+detail+"） · Ctrl+Z 可撤销";
        }else status="所选文字已经是整理后的格式";
        invalidate();return true;
    }
    if(op=="stereoLabels"){
        std::string output;
        const auto document=s.doc.id();
        const auto serial=s.serial;
        status="正在标注构型…（Esc 取消）";invalidate(false);UpdateWindow(h);
        if(!prepareStereoLabels(&output)){status=output;invalidate();return true;}
        if(s.doc.id()!=document||s.serial!=serial){
            status="等待期间文档已更改，请重新标注。";invalidate();return true;
        }
        auto result=studio_stereo::apply(s,output);
        if(result.ok())status=s.status;
        else error(result);
        invalidate();return true;
    }
    if(op=="clean3d"){conformer3D();return true;}
    if(op=="nickname"){std::string label;if(!ChemistryDialog::show(h,true,&label))return true;label=chemcompletion::normalize(label);if(chemcompletion::formulaLayout())label=chemcompletion::hyphenatedGroup(label);s.input.refreshHotspot(&s);auto spot=s.input.hotspot();ObjectId id=spot.kind==interaction::HotspotKind::Atom?spot.object.id:kInvalidObjectId;if(id==kInvalidObjectId&&s.selected.size()==1&&s.selected[0].kind==ObjectKind::Atom)id=s.selected[0].id;error(nicknames::insert(s,label,id));return true;}
    if(op=="properties"){s.input.refreshHotspot(&s);auto spot=s.input.hotspot();if(spot.kind==interaction::HotspotKind::Atom)openText(kInvalidObjectId,spot.object.id);else if(spot.kind==interaction::HotspotKind::Bond){double order=1;if(NumberDialog::show(h,L"化学键属性",L"键级（1、2 或 3）",1,&order,1,3))error(s.submit(chemistry::setBondOrderWithRefreshCommand(spot.object.id,order<1.5?BondOrder::Single:order<2.5?BondOrder::Double:BondOrder::Triple),"化学键属性"));}else if(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Text)openText(s.selected[0].id);else execute(Button("","页面设置","","@page"));return true;}
    bool importing=op=="nameToStructure"||op=="pasteSMILES"||op=="pasteMol"||op=="pasteHELM";
    bool copying=op=="copySMILES"||op=="copyMol"||op=="copyMol3000"||op=="copyHELM";
    if (op == "clean" || op == "cleanReaction") {
        studio_chemistry::CleanupRequest request;
        auto result = studio_chemistry::prepareCleanup(s.doc, s.selected, op == "cleanReaction", request);
        if (!result.ok()) { error(result); return true; }
        status = "正在处理化学结构…（Esc 取消）";
        invalidate(false);
        UpdateWindow(h);
        const auto serial = s.serial;
        const auto document = s.doc.id();
        std::string output;
        if (!chemservice::run(h, request.xml, "text/xml", "text/xml", "clean", &output)) {
            status = output;
            invalidate(false);
            return true;
        }
        if (s.serial != serial || s.doc.id() != document) {
            error({ErrorCode::InvalidArgument, "等待期间文档已更改，请重新整理。"});
            return true;
        }
        Command command;
        result = studio_chemistry::prepareCleanupCommand(s.doc, request, output, command);
        if (result.ok()) result = s.submit(command, "整理结构");
        error(result);
        if (result.ok()) status = studio_chemistry::cleanupStatus(request);
        return true;
    }
    if(!importing&&!copying&&op!="structureToName"&&op!="expandNickname"&&op!="nmr1h"&&op!="nmr13c"&&op!="nmr")return false;
    std::string input,output,inMime="text/xml",outMime="text/xml",action="convert";
    std::vector<ObjectRef> refs;
    Box oldBounds(Vec2(1e100,1e100),Vec2(-1e100,-1e100));
    if(importing){if(op=="nameToStructure"){input=pendingName;pendingName.clear();if(input.empty())for(auto r:s.selected)if(r.kind==ObjectKind::Text){if(!input.empty())input+=char(32);input+=fullText(s.doc,r.id);}if(input.empty()&&!ChemistryDialog::show(h,false,&input))return true;inMime="chemical/x-name";}else{if(!OpenClipboard(canvas)){status="无法打开剪贴板";return true;}auto mem=GetClipboardData(CF_UNICODETEXT);if(mem){auto p=(wchar_t*)GlobalLock(mem);if(p){input=U(p);GlobalUnlock(mem);}}CloseClipboard();inMime=op=="pasteSMILES"?"chemical/x-daylight-smiles":op=="pasteHELM"?"chemical/x-helm":"chemical/x-mdl-molfile";}if(input.empty()){status="剪贴板中没有可转换的文本";return true;}}
    else {
        refs = chemicalScope();
        if (refs.empty()) { status = "请先绘制或选中结构"; return true; }
        Document subset;
        auto result = studioio::append(s.doc, subset, refs, Vec2(), nullptr);
        if (!result.ok()) { error(result); return true; }
        for (auto ref : refs)
            if (ref.kind == ObjectKind::Atom) oldBounds.add(s.doc.atoms().alive(ref.id)->position);
        std::ostringstream xml;
        result = persistence::writeCdxml(subset, xml);
        if (!result.ok()) { error(result); return true; }
        input = xml.str();
        if (op == "nmr1h" || op == "nmr13c") { outMime = "text/x-moleculestudio-nmr"; action = op; }
        else if (op == "structureToName") outMime = "chemical/x-name";
        else if (copying) outMime = op=="copySMILES"?"chemical/x-daylight-smiles":op=="copyHELM"?"chemical/x-helm":op=="copyMol3000"?"chemical/x-mdl-molfile-v3000":"chemical/x-mdl-molfile";
        else action = "expand";
    }
    if(op=="nmr"||op=="nmr1h"||op=="nmr13c"){openNmrTab(input);status="核磁预测已在独立标签页打开；原结构可继续编辑";return true;}
    bool localExpanded=false;
    if(op=="expandNickname"&&!std::getenv("MOLECULE_STUDIO_EXPAND_LEGACY")){
        try{output=nicknameexpand::expand(input);localExpanded=true;}
        catch(const std::exception& e){status=e.what();invalidate(false);return true;}
    }
    if(!localExpanded){status="正在处理化学结构…（Esc 取消）";invalidate(false);UpdateWindow(h);if(!chemservice::run(h,input,inMime,outMime,action,&output)){status=output;invalidate(false);if((op=="nameToStructure"||op=="structureToName"||op=="nmr1h"||op=="nmr13c")&&output!="已取消化学服务")studio_i18n::messageBox(h,W(output).c_str(),L"化学计算未完成",MB_OK|MB_ICONINFORMATION);return true;}}

    if(copying){if(OpenClipboard(canvas)){EmptyClipboard();auto text=W(output);error(studioio::put(CF_UNICODETEXT,text.c_str(),(text.size()+1)*sizeof(wchar_t)));CloseClipboard();status="已复制 "+op.substr(4);}else status="无法写入剪贴板";return true;}
    if(op=="structureToName"){
        Vec2 p(oldBounds.min.x,oldBounds.max.y+22);
        auto id=std::make_shared<ObjectId>();
        const std::vector<ObjectRef> linkedRefs=refs;
        error(s.submit(Command("系统命名",kInvalidateAppearance|kInvalidateGeometry,[p,output,id,linkedRefs](CommandContext& c){
            auto e=studioNaming::createCaption(c.document,output,p,linkedRefs,id.get());
            return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
        }),"系统命名"));
        s.selected={ObjectRef(ObjectKind::Text,*id)};status=output;return true;
    }
    auto generated=std::make_shared<Session>();auto e=persistence::readCdxml(output,generated->doc);if(!e.ok()){error(e);return true;}generated->refresh();
    if(localExpanded&&!studio::validation::overValentAtoms(generated->doc).empty()){status="展开结果含异常价态，已保留原结构";invalidate(false);return true;}
    if(op=="expandNickname"&&generated->doc.atoms().size()==0){status="本地缩写展开未返回结构，已保留原结构";invalidate();return true;}
    // Parsed names and pasted formats enter as a single undoable insertion.
    double factor=op=="expandNickname"?1.:s.doc.defaultBondLength()/std::max(.1,generated->doc.defaultBondLength());Box bounds;for(auto& a:generated->doc.atoms().records())if(a.isAlive())bounds.add(a.position);Vec2 from=bounds.empty()?Vec2():(bounds.min+bounds.max)*.5,to=s.view().center;if(!s.selected.empty()){auto b=studioio::selectionBounds(s);if(!b.empty())to=Vec2((b.min.x+b.max.x)*.5,b.max.y+55);}if(op=="expandNickname")to=from;std::map<ObjectId,double> depths;for(auto& a:generated->doc.atoms().records())if(a.isAlive()){auto& bag=generated->doc.extensions(ObjectRef(ObjectKind::Atom,a.id));if(auto z=bag.findValue("studio.atom.z"))depths[a.id]=atof(z->c_str())*factor;bag.erase("studio.atom.z");bag.erase("pst.attr.xyz");}for(auto& a:generated->doc.atoms().records())if(a.isAlive())generated->doc.setAtomPosition(a.id,to+(a.position-from)*factor);for(auto z:depths)generated->doc.extensions(ObjectRef(ObjectKind::Atom,z.first)).set("studio.atom.z",std::to_string(z.second));auto all=generated->all();auto inserted=std::make_shared<std::vector<ObjectRef>>();auto removed=op=="expandNickname"?refs:std::vector<ObjectRef>();e=s.submit(Command(op=="expandNickname"?"展开缩写":"名称/格式转结构",kInvalidateStructure|kInvalidateGeometry,[generated,all,inserted,removed](CommandContext& c){for(auto r:removed)if(r.kind!=ObjectKind::Atom)c.document.removeObject(r);for(auto r:removed)if(r.kind==ObjectKind::Atom)c.document.removeAtom(r.id);inserted->clear();auto e=studioio::append(generated->doc,c.document,all,Vec2(),inserted.get());return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"导入化学结构");error(e);if(e.ok()){s.selected=*inserted;status=op=="expandNickname"?"已展开缩写":"已生成结构";}return true;
}
#endif

#include "functional_group_actions.hpp"
void App::collapseGroups(){
    if(catalyticFold(false))return;
    expandPending=false;
    if(textEditor.active&&!finishText(true))return;
    auto result=studio_group_actions::collapse(s);
    if(result.ok())status=s.status;
    else error(result);
    invalidate(result.ok());
}

void App::expandGroups(){
    if(catalyticFold(true))return;
    const bool repeated=expandPending&&expandDocument==s.doc.id()&&expandDigest==s.doc.digest()&&expandSelection==s.selected;
    expandPending=false;
    studio_group_actions::NextExpansion next;
    auto result=studio_group_actions::expand(s,repeated,[this](const std::string& input,std::string& output){
        return chemservice::run(h,input,"text/xml","text/xml","clean",&output);
    },next);
    if(!result.ok()){error(result);invalidate(false);return;}
    if(next==studio_group_actions::NextExpansion::CleanSelection){chemistryAction("clean");invalidate();return;}
    if(next==studio_group_actions::NextExpansion::AllowRepeatCleanup){
        expandPending=true;expandDocument=s.doc.id();expandDigest=s.doc.digest();expandSelection=s.selected;
    }
    status=s.status;
    invalidate();
}
