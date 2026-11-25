#ifndef STUDIO_EDITOR_HOTKEYS_HPP
#include "keymap_data.hpp"
#define STUDIO_EDITOR_HOTKEYS_HPP
#include "alkyne_sprout.hpp"
#include "engine/sprout_layout.hpp"
#include "hotspot_actions.hpp"
void App::editorHotkey(int index){if(index<0||size_t(index)>=sizeof(keymap::hotkeys)/sizeof(keymap::hotkeys[0]))return;auto& key=keymap::hotkeys[index];std::string command=key.command,value=key.value;s.input.refreshHotspot(&s);auto spot=s.input.hotspot();ObjectId id=spot.object.id;bool atom=spot.kind==interaction::HotspotKind::Atom,bond=spot.kind==interaction::HotspotKind::Bond;
    if(command!="SPROUT"||value!="1"||!atom)rapidSprout.clear();
    if(command=="CHARGE"){
        std::vector<ObjectId> ids;for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom&&s.doc.atoms().alive(ref.id))ids.push_back(ref.id);
        if(ids.empty()&&atom)ids.push_back(id);
        if(!ids.empty()){
            std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());int delta=value=="+"?1:-1;
            // B-minus is a reagent state, not an unbounded charge counter.
            ids.erase(std::remove_if(ids.begin(),ids.end(),[&](ObjectId id){auto a=s.doc.atoms().alive(id);return delta<0&&mechanism::genericRole(s.doc,id)=="base"&&a&&a->semantics.charge==-1;}),ids.end());
            if(ids.empty())return;
            auto e=s.submit(Command("原子电荷",kInvalidateStructure|kInvalidateAppearance|kInvalidateSemantics,[ids,delta](CommandContext& c){
                for(auto id:ids){auto a=c.document.atoms().alive(id);if(!a)continue;auto result=chemistry::setChargeCommand({id},int(a->semantics.charge)+delta).execute(c);if(!result.error.ok())return result;}
                return CommandResult::success();
            }),"原子电荷");error(e);if(e.ok())status="已更新原子电荷和隐含氢数";
        }
        return;
    }
    if(command=="DIALOG"){execute(Button("","","",value=="CHOOSENICKNAME"?"@nickname":"@properties"));return;}
    if(command=="TOOLMODE"){const std::map<std::string,std::string> tools={{"BENZENE","chooseRingTool_Benzene"},{"CYCLOPENTADIENE","chooseRingTool_Cyclopentadiene"},{"SOLIDBOND","chooseBondTool_Solid"},{"ACYCLICCHAIN","chooseAlkaneTool_Acyclic_Chain"},{"ARROW",arrowCommand()},{"CHEMICALSYMBOLS","chooseSymbolTool_CirclePlus"},{"TEXT","chooseEdittingTool_Text"},{"BRACKET","chooseBracketTool_SquareBrackets"},{"TLCPLATE","chooseTLCPlateTool"},{"ORBITAL","chooseOrbitalTool_p_Shaded"},{"MARQUEE","chooseSelectionTool_Marquee"}};auto found=tools.find(value);if(found!=tools.end())execute(Button("",shortcutChinese(key),"",found->second));return;}
    if(command=="SELECT"){s.selected={spot.object};return;}
    if(atom){if(command=="LABELTEXT"){
        if(value=="O"||value=="N"){preferAtomHydride(id,value=="O"?8:7);return;}
        error(s.submit(studio::hotspot_actions::label(id,value,*s.doc.atoms().alive(id)),"原子快捷键"));return;
    }if(command=="CHARGE"){auto a=s.doc.atoms().get(id);error(s.submit(chemistry::setChargeCommand({id},a->semantics.charge+(value=="+"?1:-1)),"原子电荷"));return;}
        if(command=="ATOMNUMBER"){error(s.submit(Command("原子编号",kInvalidateAppearance,[id](CommandContext& c){auto ref=ObjectRef(ObjectKind::Atom,id);auto& ext=c.document.extensions(ref);auto prev=ext.findValue("studio.atom.number");if(prev)ext.erase("studio.atom.number");else ext.set("studio.atom.number",std::to_string(id+1));return CommandResult::success();}),"原子编号"));return;}
        if(command=="ATTACHMENTPOINT"){error(s.submit(Command("连接点",kInvalidateStructure|kInvalidateAppearance,[id](CommandContext& c){auto sem=c.document.atoms().get(id)->semantics;sem.nodeType=uint8_t(NodeType::ExternalConnection);sem.showLabel=true;auto e=c.document.setAtomSemantics(id,sem);if(e.ok())e=c.document.setAtomLabel(id,"*");return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"连接点"));return;}
        if(command=="SPROUT"&&value=="1"){
            const uint64_t now=GetTickCount64();std::vector<ObjectId> recent;
            if(rapidSprout.continues(s.doc,id,now,s.serial,s.history.cursor()))recent=rapidSprout.tips;
            auto added=std::make_shared<studio::rapid_sprout::Addition>();
            auto e=s.submit(studio::rapid_sprout::command(id,recent,added),"添加结构片段");error(e);
            if(e.ok()){recent.push_back(added->atom);rapidSprout.record(s.doc,id,now,s.serial,s.history.cursor(),recent);if(added->arranged)status="已按连续添键整理支链角度";}
            else rapidSprout.clear();return;
        }
        if(command=="SPROUT"){error(s.submit(studio::hotspot_actions::sprout(id,atoi(value.c_str())),"添加结构片段"));return;}
    }
    if(bond){if(command=="BONDORDER"){
        const int order=atoi(value.c_str());
        const auto serial=s.serial;
        auto result=s.submit(chemistry::setBondOrderWithRefreshCommand(id,order==1?BondOrder::Single:order==2?BondOrder::Double:BondOrder::Triple),"化学键级");
        error(result);
        if(result.ok()&&s.serial!=serial&&pointerOverCanvas&&!down){
            if(auto tool=dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get())){
                insertionPreviewPending=false;
                tool->hoverPreview(s,s.view().toDocument(pointerPixel),mods());
            }
        }
        return;
    }
        if(command=="BONDDOUBLEPOSITION"){error(s.submit(bonds::setBondSideCommand({id},value=="LEFT"?bonds::DoubleSide::Left:value=="RIGHT"?bonds::DoubleSide::Right:bonds::DoubleSide::Centered),"双键位置"));return;}
        if(command=="BONDPOSITION"){s.selected={spot.object};execute(Button("","置于顶层","","@front"));return;}
        if(command=="BONDDISPLAY"){error(s.submit(studio::hotspot_actions::bondDisplay(id,value),"键显示"));return;}
        if(command=="FUSERING"){error(studio::hotspot_actions::fuse(s,id,spot.point,atoi(value.c_str())));return;}
    }
}
#endif
