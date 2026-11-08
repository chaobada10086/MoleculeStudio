#ifndef STUDIO_CHEMISTRY_ACTIONS_HPP
#define STUDIO_CHEMISTRY_ACTIONS_HPP
#include "text_tidy.hpp"
#include "engine/name_link.hpp"
#include "next/selection/grouping.hpp"
// Re-anchor every connected component separately. A clean service may normalize
// the document origin and arrange disconnected fragments; neither translation
// should move an ordinary structure cleanup away from its original location.
inline std::vector<Vec2> anchorCleanComponents(const Document& document,const std::vector<ObjectId>& ids,const std::vector<Vec2>& cleaned,double factor){
    std::map<ObjectId,size_t> index;for(size_t i=0;i<ids.size();++i)index[ids[i]]=i;
    std::vector<std::vector<size_t>> edges(ids.size());for(const auto& bond:document.bonds().records())if(bond.isAlive()){auto a=index.find(bond.beginAtom),b=index.find(bond.endAtom);if(a!=index.end()&&b!=index.end()){edges[a->second].push_back(b->second);edges[b->second].push_back(a->second);}}
    std::vector<Vec2> positions(ids.size());std::vector<bool> seen(ids.size(),false);
    for(size_t root=0;root<ids.size();++root)if(!seen[root]){std::vector<size_t> component(1,root);seen[root]=true;Vec2 old=document.atoms().get(ids[root])->position;Box before(old,old),after(cleaned[root],cleaned[root]);
        for(size_t k=0;k<component.size();++k){size_t i=component[k];before.add(document.atoms().get(ids[i])->position);after.add(cleaned[i]);for(auto next:edges[i])if(!seen[next]){seen[next]=true;component.push_back(next);}}
        Vec2 oldCenter=(before.min+before.max)*.5,newCenter=(after.min+after.max)*.5;for(auto i:component)positions[i]=oldCenter+(cleaned[i]-newCenter)*factor;
    }return positions;
}
// ---------------------------------------------------------------------------
// Structure-to-name caption
// ---------------------------------------------------------------------------
// The caption is bound to the structure it names: the pair becomes one
// GroupRecord, and the caption carries a digest of the linked structure so a
// later edit can raise the red "structure changed" frame. One Command = one
// Ctrl+Z, including the group and the link. Running it again REPLACES its own
// previous caption instead of stacking a second one.
namespace studioNaming {
inline Error createCaption(r62::core::Document& document,const std::string& caption,const r62::core::Vec2& position,
                           const std::vector<r62::core::ObjectRef>& linkedRefs,r62::core::ObjectId* textId){
    using namespace r62::core;
    // Captions this command owns (found by their structure link) and the naming
    // groups they live in. A group the user made themselves is left alone.
    const std::vector<ObjectId> previous=namelink::linkedTextsForStructure(document,linkedRefs);
    std::vector<ObjectId> previousGroups;
    for(size_t k=0;k<previous.size();++k){
        const ObjectId owner=namelink::owningGroup(document,ObjectRef(ObjectKind::Text,previous[k]));
        if(owner==kInvalidObjectId)continue;
        const GroupRecord* group=document.groups().alive(owner);
        if(!group||group->label!=namelink::groupLabel())continue;
        if(std::find(previousGroups.begin(),previousGroups.end(),owner)==previousGroups.end())previousGroups.push_back(owner);
    }
    for(size_t k=0;k<previousGroups.size();++k){r62::selection::GroupReport report;r62::selection::ungroup(document,std::vector<ObjectId>(1,previousGroups[k]),false,&report);}
    for(size_t k=0;k<previous.size();++k)if(document.texts().alive(previous[k]))document.removeObject(ObjectRef(ObjectKind::Text,previous[k]));
    ObjectId fallbackId=kInvalidObjectId;
    ObjectId* created=textId?textId:&fallbackId;
    Error e=document.addText(position,12,caption,created);
    if(!e.ok())return e;
    auto format=document.texts().get(*created)->format;format.fontName="Arial";
    e=document.setTextFormat(*created,format);
    if(!e.ok())return e;
    // Bubble existing user groups instead of stealing their members: this group
    // nests the user group and puts the caption beside it.
    std::vector<ObjectRef> groupRefs=namelink::topLevelMembers(document,linkedRefs);
    groupRefs.push_back(ObjectRef(ObjectKind::Text,*created));
    r62::selection::createGroup(document,groupRefs,namelink::groupLabel(),0);
    namelink::setLink(document,*created,linkedRefs,namelink::structureDigestText(document,linkedRefs));
    return Error();
}
}
std::vector<ObjectRef> App::chemicalScope(){std::set<ObjectId> atoms;auto refs=studioio::expanded(s.doc,s.selected);for(auto r:refs)if(r.kind==ObjectKind::Atom)atoms.insert(r.id);else if(r.kind==ObjectKind::Bond){auto b=s.doc.bonds().alive(r.id);if(b){atoms.insert(b->beginAtom);atoms.insert(b->endAtom);}}if(s.selected.empty())for(auto& a:s.doc.atoms().records())if(a.isAlive())atoms.insert(a.id);bool changed=true;while(changed){changed=false;for(auto& b:s.doc.bonds().records())if(b.isAlive()&&(atoms.count(b.beginAtom)||atoms.count(b.endAtom))){changed|=atoms.insert(b.beginAtom).second;changed|=atoms.insert(b.endAtom).second;}}std::vector<ObjectRef> out;for(auto a:atoms)out.push_back(ObjectRef(ObjectKind::Atom,a));for(auto& b:s.doc.bonds().records())if(b.isAlive()&&atoms.count(b.beginAtom)&&atoms.count(b.endAtom))out.push_back(ObjectRef(ObjectKind::Bond,b.id));return out;}
bool App::chemistryAction(const std::string& op,int){
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
            if(!scope.empty()){
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
        // Every label is an ordinary text object inserted by ONE command, so a
        // single Ctrl+Z removes the whole annotation. Labels are tagged with a
        // document extension key, so running again replaces the previous set
        // instead of stacking a second copy on top of it.
        auto refs=chemicalScope();if(refs.empty()){status="请先绘制或选中结构";invalidate();return true;}
        std::shared_ptr<Session> subset(new Session());
        auto e=studioio::append(s.doc,subset->doc,refs,Vec2(),nullptr);if(!e.ok()){error(e);return true;}
        std::ostringstream xml;e=persistence::writeCdxml(subset->doc,xml);if(!e.ok()){error(e);return true;}
        std::string output;
        status="正在标注构型…（Esc 取消）";invalidate(false);UpdateWindow(h);
        if(!chemservice::run(h,xml.str(),"text/xml","text/x-moleculestudio-stereo","stereolabels",&output)){status=output;invalidate();return true;}
        struct StereoLabel{bool bond;std::string text;Vec2 center;};
        std::vector<StereoLabel> labels;int skipped=0;
        std::istringstream stream(output);std::string line;
        while(std::getline(stream,line)){
            if(line.empty())continue;
            std::vector<std::string> parts;size_t from=0;
            for(size_t i=0;i<=line.size();++i)if(i==line.size()||line[i]=='\t'){parts.push_back(line.substr(from,i-from));from=i+1;}
            if(parts.size()==2&&parts[0]=="skipped"){skipped=std::atoi(parts[1].c_str());continue;}
            if(parts.size()<5||(parts[0]!="atom"&&parts[0]!="bond"))continue;
            // The ids come from the subset CDXML (append() clears source ids), so
            // they only identify the centre inside the service; the label is
            // placed by coordinates, which are the document's own.
            StereoLabel label;label.bond=parts[0]=="bond";label.text=parts[2];
            label.center=Vec2(std::atof(parts[3].c_str()),std::atof(parts[4].c_str()));
            if(!label.center.finite())continue;
            labels.push_back(label);
        }
        if(labels.empty()){status=skipped?("选中结构中没有可标注的 R/S/E/Z 构型（"+std::to_string(skipped)+" 处构型未定）"):"选中结构中没有手性中心或可指认的 E/Z 双键";invalidate();return true;}
        double size=s.doc.defaultTextSize()>0?s.doc.defaultTextSize():10;
        error(s.submit(Command("构型标注",kInvalidateAppearance|kInvalidateGeometry,[labels,size](CommandContext& c){
            std::vector<ObjectId> previous;
            for(auto& t:c.document.texts().records())if(t.isAlive()){
                if(c.document.extensions(ObjectRef(ObjectKind::Text,t.id)).findValue("studio.stereo.label")){previous.push_back(t.id);continue;}
                // A reopened document loses the extension marker, so a plain
                // "(R)" caption standing exactly where this run wants to write
                // is treated as this command's own label and recycled.
                std::string plain=fullText(c.document,t.id);
                if(plain!="(R)"&&plain!="(S)"&&plain!="(E)"&&plain!="(Z)")continue;
                double w=size*.60*double(plain.size()),h=size*1.05;Vec2 at(t.position.x+w*.5,t.position.y+h*.5);
                for(auto& label:labels)if(core::length(at-label.center)<9){previous.push_back(t.id);break;}
            }
            for(auto id:previous)c.document.removeObject(ObjectRef(ObjectKind::Text,id));
            for(auto& label:labels){
                // The service places the label centre; CDXML text is anchored at
                // its top-left, so offset by the measured box.
                // Conventional caption: upright parentheses around the CIP letter.
                std::string caption="("+label.text+")";
                double width=size*.60*double(caption.size()),height=size*1.05;
                ObjectId id;auto e=c.document.addText(Vec2(label.center.x-width*.5,label.center.y-height*.5),size,caption,&id);
                if(!e.ok())return CommandResult::fail(e.code,e.message);
                TextFormat format;format.fontName=c.document.defaultFont();format.size=size;
                e=c.document.setTextFormat(id,format);if(!e.ok())return CommandResult::fail(e.code,e.message);
                c.document.extensions(ObjectRef(ObjectKind::Text,id)).set("studio.stereo.label",label.bond?"bond":"atom");
            }
            return CommandResult::success();
        }),"构型标注"));
        int r=0,ss=0,ee=0,zz=0;
        for(auto& label:labels)if(label.text=="R")++r;else if(label.text=="S")++ss;else if(label.text=="E")++ee;else if(label.text=="Z")++zz;
        std::string summary="已标注 "+std::to_string(labels.size())+" 处构型";
        if(r||ss||ee||zz){std::string detail;if(r)detail+=std::string(detail.empty()?"":"、")+"R "+std::to_string(r);if(ss)detail+=std::string(detail.empty()?"":"、")+"S "+std::to_string(ss);if(ee)detail+=std::string(detail.empty()?"":"、")+"E "+std::to_string(ee);if(zz)detail+=std::string(detail.empty()?"":"、")+"Z "+std::to_string(zz);summary+="（"+detail+"）";}
        if(skipped)summary+="；"+std::to_string(skipped)+" 处构型未定已跳过";
        status=summary+" · 可按 Ctrl+Z 撤销";invalidate();return true;
    }
    if(op=="clean3d"){conformer3D();return true;}
    if(op=="nickname"){std::string label;if(!ChemistryDialog::show(h,true,&label))return true;label=chemcompletion::normalize(label);s.input.refreshHotspot(&s);auto spot=s.input.hotspot();ObjectId id=spot.kind==interaction::HotspotKind::Atom?spot.object.id:kInvalidObjectId;if(id==kInvalidObjectId&&s.selected.size()==1&&s.selected[0].kind==ObjectKind::Atom)id=s.selected[0].id;if(id!=kInvalidObjectId)error(s.submit(nicknames::labelCommand(id,label),"插入缩写"));else{auto p=s.view().center;auto fresh=std::make_shared<ObjectId>();error(s.submit(Command("插入缩写",kInvalidateStructure|kInvalidateAppearance,[p,label,fresh](CommandContext& c){auto e=c.document.addAtom(p,6,fresh.get());if(e.ok())e=nicknames::apply(c.document,*fresh,label);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"插入缩写"));s.selected={ObjectRef(ObjectKind::Atom,*fresh)};}return true;}
    if(op=="properties"){s.input.refreshHotspot(&s);auto spot=s.input.hotspot();if(spot.kind==interaction::HotspotKind::Atom)openText(kInvalidObjectId,spot.object.id);else if(spot.kind==interaction::HotspotKind::Bond){double order=1;if(NumberDialog::show(h,L"化学键属性",L"键级（1、2 或 3）",1,&order,1,3))error(s.submit(chemistry::setBondOrderWithRefreshCommand(spot.object.id,order<1.5?BondOrder::Single:order<2.5?BondOrder::Double:BondOrder::Triple),"化学键属性"));}else if(s.selected.size()==1&&s.selected[0].kind==ObjectKind::Text)openText(s.selected[0].id);else execute(Button("","页面设置","","@page"));return true;}
    bool importing=op=="nameToStructure"||op=="pasteSMILES"||op=="pasteMol"||op=="pasteHELM";
    bool copying=op=="copySMILES"||op=="copyMol"||op=="copyMol3000"||op=="copyHELM";
    bool cleaning=op=="clean"||op=="clean3d"||op=="cleanReaction";
    if(!importing&&!copying&&!cleaning&&op!="structureToName"&&op!="expandNickname"&&op!="nmr1h"&&op!="nmr13c"&&op!="nmr")return false;
    std::string input,output,inMime="text/xml",outMime="text/xml",action="convert";std::vector<ObjectRef> refs;std::vector<ObjectId> originals;std::vector<int> fileIds;Box oldBounds(Vec2(1e100,1e100),Vec2(-1e100,-1e100));std::shared_ptr<Session> subset(new Session());
    if(importing){if(op=="nameToStructure"){input=pendingName;pendingName.clear();if(input.empty())for(auto r:s.selected)if(r.kind==ObjectKind::Text){if(!input.empty())input+=char(32);input+=fullText(s.doc,r.id);}if(input.empty()&&!ChemistryDialog::show(h,false,&input))return true;inMime="chemical/x-name";}else{if(!OpenClipboard(canvas)){status="无法打开剪贴板";return true;}auto mem=GetClipboardData(CF_UNICODETEXT);if(mem){auto p=(wchar_t*)GlobalLock(mem);if(p){input=U(p);GlobalUnlock(mem);}}CloseClipboard();inMime=op=="pasteSMILES"?"chemical/x-daylight-smiles":op=="pasteHELM"?"chemical/x-helm":"chemical/x-mdl-molfile";}if(input.empty()){status="剪贴板中没有可转换的文本";return true;}}
    else{refs=chemicalScope();if(refs.empty()){status="请先绘制或选中结构";return true;}auto e=studioio::append(s.doc,subset->doc,refs,Vec2(),nullptr);if(!e.ok()){error(e);return true;}for(auto r:refs)if(r.kind==ObjectKind::Atom){originals.push_back(r.id);oldBounds.add(s.doc.atoms().get(r.id)->position);}if(cleaning)for(auto& a:subset->doc.atoms().records())if(a.isAlive()){auto& bag=subset->doc.extensions(ObjectRef(ObjectKind::Atom,a.id));bag.erase("studio.atom.z");bag.erase("pst.attr.xyz");}std::ostringstream xml;e=persistence::writeCdxml(subset->doc,xml);if(!e.ok()){error(e);return true;}input=xml.str();tinyxml2::XMLDocument doc;doc.Parse(input.c_str());auto page=doc.RootElement()->FirstChildElement("page");for(auto f=page->FirstChildElement("fragment");f;f=f->NextSiblingElement("fragment"))for(auto a=f->FirstChildElement("n");a;a=a->NextSiblingElement("n"))fileIds.push_back(a->IntAttribute("id"));
        if(op=="nmr1h"||op=="nmr13c"){outMime="text/x-moleculestudio-nmr";action=op;}else if(op=="structureToName")outMime="chemical/x-name";else if(copying)outMime=op=="copySMILES"?"chemical/x-daylight-smiles":op=="copyHELM"?"chemical/x-helm":op=="copyMol3000"?"chemical/x-mdl-molfile-v3000":"chemical/x-mdl-molfile";else if(cleaning)action=op=="clean3d"?"clean3d":"clean";else action="expand";
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
    struct CleanStereoUpdate{ObjectId id,a,b;BondDisplay display;BondStereo stereo;};std::vector<CleanStereoUpdate> cleanStereo;std::vector<std::pair<ObjectId,std::string>> cleanFragments;
    if(cleaning){std::map<int,ObjectId> sourceAtoms;for(size_t i=0;i<originals.size()&&i<fileIds.size();++i)sourceAtoms[fileIds[i]]=originals[i];for(const auto& atom:generated->doc.atoms().records())if(atom.isAlive()&&sourceAtoms.count(atom.sourceId)){auto raw=static_cast<const Document&>(generated->doc).extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("studio.nickname.fragment");auto original=sourceAtoms[atom.sourceId];if(raw&&static_cast<const Document&>(s.doc).extensions(ObjectRef(ObjectKind::Atom,original)).findValue("studio.nickname.fragment"))cleanFragments.push_back({original,*raw});}for(const auto& bond:generated->doc.bonds().records())if(bond.isAlive()){int a=generated->doc.atoms().get(bond.beginAtom)->sourceId,b=generated->doc.atoms().get(bond.endAtom)->sourceId;if(!sourceAtoms.count(a)||!sourceAtoms.count(b))continue;auto from=sourceAtoms[a],to=sourceAtoms[b];for(const auto& old:s.doc.bonds().records())if(old.isAlive()&&((old.beginAtom==from&&old.endAtom==to)||(old.beginAtom==to&&old.endAtom==from))){bool wedge=old.display==BondDisplay::Wedge||old.display==BondDisplay::WedgeHash||bond.display==BondDisplay::Wedge||bond.display==BondDisplay::WedgeHash;if(wedge)cleanStereo.push_back({old.id,from,to,bond.display,bond.stereo});break;}}}
    if(cleaning){if(originals.size()!=fileIds.size()){status="整理结果的原子映射不完整，已保留原结构";return true;}std::map<int,Vec2> returned;for(auto& a:generated->doc.atoms().records())if(a.isAlive()){returned[a.sourceId]=a.position;}if(returned.size()!=originals.size()){status="整理结果改变了原子数量，已保留原结构";return true;}double factor=op=="expandNickname"?1.:s.doc.defaultBondLength()/std::max(.1,generated->doc.defaultBondLength());std::vector<Vec2> positions;for(auto id:fileIds){auto it=returned.find(id);if(it==returned.end()){status="整理结果缺少原子映射，已保留原结构";return true;}positions.push_back(it->second);}positions=anchorCleanComponents(s.doc,originals,positions,factor);error(s.submit(Command("整理结构",kInvalidateGeometry,[originals,positions,op,oldBounds,cleanStereo,cleanFragments](CommandContext& c){for(size_t i=0;i<originals.size();i++){auto e=c.document.setAtomPosition(originals[i],positions[i]);if(!e.ok())return CommandResult::fail(e.code,e.message);auto& bag=c.document.extensions(ObjectRef(ObjectKind::Atom,originals[i]));bag.erase("studio.atom.z");bag.erase("pst.attr.xyz");}for(auto fragment:cleanFragments)c.document.extensions(ObjectRef(ObjectKind::Atom,fragment.first)).set("studio.nickname.fragment",fragment.second);for(auto bond:cleanStereo){auto e=c.document.setBondEndpoints(bond.id,bond.a,bond.b);if(e.ok())e=c.document.setBondDisplay(bond.id,bond.display);if(e.ok())e=c.document.setBondStereo(bond.id,bond.stereo);if(!e.ok())return CommandResult::fail(e.code,e.message);}if(op=="cleanReaction"){advanced::LayoutInput input;auto e=advanced::captureLayoutInput(c.document,&input);if(!e.ok())return CommandResult::fail(e.code,e.message);std::set<ObjectId> chosen(originals.begin(),originals.end());input.atoms.erase(std::remove_if(input.atoms.begin(),input.atoms.end(),[&](const advanced::LayoutAtom& a){return !chosen.count(a.id);}),input.atoms.end());input.bonds.erase(std::remove_if(input.bonds.begin(),input.bonds.end(),[&](const advanced::LayoutBond& b){return !chosen.count(b.a)||!chosen.count(b.b);}),input.bonds.end());advanced::ArrangeOptions options;options.origin=Vec2(oldBounds.min.x,(oldBounds.min.y+oldBounds.max.y)*.5);advanced::ArrangePlan plan;e=advanced::planArrange(input,options,&plan);if(!e.ok())return CommandResult::fail(e.code,e.message);return advanced::applyArrangeCommand(plan,false).execute(c);}return CommandResult::success();}),"整理结构"));status=op=="clean3d"?"已按三维构象整理绘图":"已用 RDKit 整理结构，保留原子、键级、立体化学和标注";return true;}
    // Parsed names and pasted formats enter as a single undoable insertion.
    double factor=op=="expandNickname"?1.:s.doc.defaultBondLength()/std::max(.1,generated->doc.defaultBondLength());Box bounds;for(auto& a:generated->doc.atoms().records())if(a.isAlive())bounds.add(a.position);Vec2 from=bounds.empty()?Vec2():(bounds.min+bounds.max)*.5,to=s.view().center;if(!s.selected.empty()){auto b=studioio::selectionBounds(s);if(!b.empty())to=Vec2((b.min.x+b.max.x)*.5,b.max.y+55);}if(op=="expandNickname")to=from;std::map<ObjectId,double> depths;for(auto& a:generated->doc.atoms().records())if(a.isAlive()){auto& bag=generated->doc.extensions(ObjectRef(ObjectKind::Atom,a.id));if(auto z=bag.findValue("studio.atom.z"))depths[a.id]=atof(z->c_str())*factor;bag.erase("studio.atom.z");bag.erase("pst.attr.xyz");}for(auto& a:generated->doc.atoms().records())if(a.isAlive())generated->doc.setAtomPosition(a.id,to+(a.position-from)*factor);for(auto z:depths)generated->doc.extensions(ObjectRef(ObjectKind::Atom,z.first)).set("studio.atom.z",std::to_string(z.second));auto all=generated->all();auto inserted=std::make_shared<std::vector<ObjectRef>>();auto removed=op=="expandNickname"?refs:std::vector<ObjectRef>();e=s.submit(Command(op=="expandNickname"?"展开缩写":"名称/格式转结构",kInvalidateStructure|kInvalidateGeometry,[generated,all,inserted,removed](CommandContext& c){for(auto r:removed)if(r.kind!=ObjectKind::Atom)c.document.removeObject(r);for(auto r:removed)if(r.kind==ObjectKind::Atom)c.document.removeAtom(r.id);inserted->clear();auto e=studioio::append(generated->doc,c.document,all,Vec2(),inserted.get());return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"导入化学结构");error(e);if(e.ok()){s.selected=*inserted;status=op=="expandNickname"?"已展开缩写":"已生成结构";}return true;
}
#endif

#include "functional_groups.hpp"
void App::collapseGroups(){
 expandPending=false;if(textEditor.active&&!finishText(true))return;
 try{auto chosen=functional_groups::selection(s);if(chosen.empty()){status="请先选中要收起的结构";invalidate(false);return;}auto matches=functional_groups::findMatches(s.doc,chosen);
 if(matches.empty()){status="选区内没有与库定义及连接关系匹配的官能团";invalidate(false);return;}
 auto e=s.submit(Command("官能团收起",kInvalidateStructure|kInvalidateGeometry|kInvalidateSemantics,[matches](CommandContext& c){try{for(const auto& m:matches)functional_groups::collapse(c.document,m);return CommandResult::success();}catch(const std::exception& e){return CommandResult::fail(ErrorCode::InvalidArgument,e.what());}}),"官能团收起");error(e);
 if(e.ok()){s.selected.clear();for(const auto& m:matches)s.selected.push_back(ObjectRef(ObjectKind::Atom,m.anchor));status="已收起 "+std::to_string(matches.size())+" 个官能团 · 可撤销";}invalidate();
 }catch(const std::exception& e){status=e.what();invalidate(false);}
}
void App::expandGroups(){
 if(expandPending&&expandDocument==s.doc.id()&&expandDigest==s.doc.digest()&&expandSelection==s.selected){expandPending=false;chemistryAction("clean");invalidate();return;}
 expandPending=false;if(s.selected.empty()){status="请先选中要展开的结构";invalidate(false);return;}
 try{std::vector<functional_groups::Expansion> plans;for(auto id:functional_groups::selection(s))if(!functional_groups::definition(s.doc,id).empty())plans.push_back(functional_groups::prepareExpansion(s.doc,id));
 if(plans.empty()){status="选中结构没有可展开的官能团缩写";invalidate(false);return;}
 auto inserted=std::make_shared<std::vector<ObjectRef>>();auto e=s.submit(Command("官能团展开",kInvalidateStructure|kInvalidateGeometry|kInvalidateSemantics,[plans,inserted](CommandContext& c){try{inserted->clear();for(const auto& p:plans)functional_groups::applyExpansion(c.document,p,*inserted);return CommandResult::success();}catch(const std::exception& e){return CommandResult::fail(ErrorCode::InvalidArgument,e.what());}}),"官能团展开");error(e);
 if(e.ok()){s.selected=*inserted;expandPending=true;expandDocument=s.doc.id();expandDigest=s.doc.digest();expandSelection=s.selected;status="所选官能团已展开 · 直接再点一次可整理结构";}invalidate();
 }catch(const std::exception& e){status=e.what();invalidate(false);}
}

