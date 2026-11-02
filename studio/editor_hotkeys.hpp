#ifndef STUDIO_EDITOR_HOTKEYS_HPP
#include "keymap_data.hpp"
#define STUDIO_EDITOR_HOTKEYS_HPP
#include "alkyne_sprout.hpp"
#include "engine/sprout_layout.hpp"
// A terminal carbonyl has two trigonal choices. growthAngle is the forward
// continuation of the existing chain (opposite its bond back to the parent).
// Prefer the side with room for
// the O and the whole C=O segment; a fixed +/- choice can land O on another atom.
inline double carbonylSproutAngle(const Document& doc,ObjectId carbon,double growthAngle){
    r62::sprout_layout::Environment obstacles(doc,carbon);
    auto env=rings_chains::buildSproutEnvironment(doc,carbon,25.);
    double choices[2]={growthAngle-60,growthAngle+60},best=1e300,angle=std::numeric_limits<double>::quiet_NaN();
    for(double candidate:choices){double score=0;if(obstacles.score(candidate,env.neighbourDirectionsDeg,&score)&&score<best-1e-8){best=score;angle=candidate;}}
    return angle;
}
void App::editorHotkey(int index){if(index<0||size_t(index)>=sizeof(keymap::hotkeys)/sizeof(keymap::hotkeys[0]))return;auto& key=keymap::hotkeys[index];std::string command=key.command,value=key.value;s.input.refreshHotspot(&s);auto spot=s.input.hotspot();ObjectId id=spot.object.id;bool atom=spot.kind==interaction::HotspotKind::Atom,bond=spot.kind==interaction::HotspotKind::Bond;
    if(command=="DIALOG"){execute(Button("","","",value=="CHOOSENICKNAME"?"@nickname":"@properties"));return;}
    if(command=="TOOLMODE"){const std::map<std::string,std::string> tools={{"BENZENE","chooseRingTool_Benzene"},{"CYCLOPENTADIENE","chooseRingTool_Cyclopentadiene"},{"SOLIDBOND","chooseBondTool_Solid"},{"ACYCLICCHAIN","chooseAlkaneTool_Acyclic_Chain"},{"ARROW",arrowCommand()},{"CHEMICALSYMBOLS","chooseSymbolTool_CirclePlus"},{"TEXT","chooseEdittingTool_Text"},{"BRACKET","chooseBracketTool_SquareBrackets"},{"TLCPLATE","chooseTLCPlateTool"},{"ORBITAL","chooseOrbitalTool_p_Shaded"},{"MARQUEE","chooseSelectionTool_Marquee"}};auto found=tools.find(value);if(found!=tools.end())execute(Button("",shortcutChinese(key),"",found->second));return;}
    if(command=="SELECT"){s.selected={spot.object};return;}
    if(atom){if(command=="LABELTEXT"){if(value=="O"||value=="N"){preferAtomHydride(id,value=="O"?8:7);return;}error(s.submit(nicknames::labelCommand(id,value),"原子快捷键"));return;}if(command=="CHARGE"){auto a=s.doc.atoms().get(id);error(s.submit(chemistry::setChargeCommand({id},a->semantics.charge+(value=="+"?1:-1)),"原子电荷"));return;}
        if(command=="ATOMNUMBER"){error(s.submit(Command("原子编号",kInvalidateAppearance,[id](CommandContext& c){auto ref=ObjectRef(ObjectKind::Atom,id);auto& ext=c.document.extensions(ref);auto prev=ext.findValue("studio.atom.number");if(prev)ext.erase("studio.atom.number");else ext.set("studio.atom.number",std::to_string(id+1));return CommandResult::success();}),"原子编号"));return;}
        if(command=="ATTACHMENTPOINT"){error(s.submit(Command("连接点",kInvalidateStructure|kInvalidateAppearance,[id](CommandContext& c){auto sem=c.document.atoms().get(id)->semantics;sem.nodeType=uint8_t(NodeType::ExternalConnection);sem.showLabel=true;auto e=c.document.setAtomSemantics(id,sem);if(e.ok())e=c.document.setAtomLabel(id,"*");return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"连接点"));return;}
        if(command=="SPROUT"){int recipe=atoi(value.c_str());error(s.submit(Command("添加结构片段",kInvalidateStructure|kInvalidateGeometry,[id,recipe](CommandContext& c){auto& d=c.document;auto a=d.atoms().alive(id);if(!a)return CommandResult::fail(ErrorCode::NotFound,"原子不存在");auto env=rings_chains::buildSproutEnvironment(d,id,25);auto dir=sprout_layout::resolve(d,id,-30);if(!dir.ok)return CommandResult::fail(ErrorCode::InvalidArgument,dir.rejectReason);double angle=dir.directionDeg,L=d.defaultBondLength();Error e;auto add=[&](ObjectId from,int element,double deg,BondOrder order=BondOrder::Single,BondDisplay display=BondDisplay::Normal){ObjectId to=kInvalidObjectId,b;e=d.addAtom(d.atoms().get(from)->position+core::direction(deg)*L,element,&to);if(e.ok())e=d.addBond(from,to,&b);if(e.ok())e=d.setBondOrder(b,order);if(e.ok())e=d.setBondDisplay(b,display);return to;};
            auto ring=[&](ObjectId anchor,rings_chains::RingPresetId preset){rings_chains::RingRequest r;r.preset=preset;r.mode=dir.neighbourCount>=2&&anchor==id?rings_chains::RingAttachMode::Spiro:rings_chains::RingAttachMode::GrowFromAtom;r.anchorAtom=anchor;r.pointer=d.atoms().get(anchor)->position+core::direction(angle)*L;r.bondLength=L;rings_chains::RingPlacement p;e=rings_chains::resolveRingPlacement(d,r,&p);if(e.ok()){auto result=rings_chains::commands::createRingCommand(p,0,0).execute(c);e=result.error;}};
            if(recipe==0||recipe==1||recipe==4||recipe==5)add(id,6,angle,BondOrder::Single,recipe==4?BondDisplay::Wedge:recipe==5?BondDisplay::WedgeHash:BondDisplay::Normal);
            else if(recipe==2){
                // ChemDraw's Atom/2 adds =O ON the hovered carbon. A terminal
                // carbon becomes CHO; it never inserts an extra carbon atom.
                double oxygenAngle=dir.neighbourCount==1?carbonylSproutAngle(d,id,env.neighbourDirectionsDeg[0]+180):angle;
                if(!std::isfinite(oxygenAngle))return CommandResult::fail(ErrorCode::InvalidArgument,"附近没有可安全添加羰基的方向，请拖动指定方向");
                add(id,8,oxygenAngle,BondOrder::Double);
            }
            else if(recipe==3||recipe==16||recipe==15){ObjectId anchor=id;if(recipe!=3||dir.neighbourCount>1)anchor=add(id,6,angle);if(e.ok())ring(anchor,recipe==15?rings_chains::RingPresetId::Cyclopentadiene:rings_chains::RingPresetId::Benzene);}
            else if(recipe==6||recipe==7||recipe==11||recipe==12)ring(id,recipe==6?rings_chains::RingPresetId::Cyclohexane:recipe==7?rings_chains::RingPresetId::Cyclopentane:recipe==11?rings_chains::RingPresetId::Cyclopropane:rings_chains::RingPresetId::Cyclobutane);
            else if(recipe==8)add(id,6,angle,BondOrder::Double);
            else if(recipe==9){add(id,6,angle-30);if(e.ok())add(id,6,angle+90);}
            // Alkyne sprout attaches the triple bond directly to the hovered
            // atom. The old two-step path left an extra CH2 (R-CH2-C≡CH);
            // ChemDraw's z command produces R-C≡CH (one new carbon, one H).
            // An sp carbon is linear: on a terminal atom continue exactly
            // opposite its existing bond instead of using the 120-degree
            // carbon-chain sprout direction.
            else if(recipe==10){double alkyneAngle=dir.neighbourCount==1?env.neighbourDirectionsDeg[0]+180.0:angle;add(id,6,alkyneAngle,BondOrder::Triple);}
            else if(recipe==13){auto carbon=add(id,6,angle);if(e.ok())for(int i=0;i<3&&e.ok();i++)add(carbon,6,angle-90+i*90);}
            else if(recipe==14){auto sulfur=add(id,16,angle);if(e.ok())add(sulfur,8,angle+90,BondOrder::Double);if(e.ok())add(sulfur,8,angle-90,BondOrder::Double);}
            return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
        }),"添加结构片段"));return;}
    }
    if(bond){if(command=="BONDORDER"){int order=atoi(value.c_str());error(s.submit(chemistry::setBondOrderWithRefreshCommand(id,order==1?BondOrder::Single:order==2?BondOrder::Double:BondOrder::Triple),"化学键级"));return;}
        if(command=="BONDDOUBLEPOSITION"){error(s.submit(bonds::setBondSideCommand({id},value=="LEFT"?bonds::DoubleSide::Left:value=="RIGHT"?bonds::DoubleSide::Right:bonds::DoubleSide::Centered),"双键位置"));return;}
        if(command=="BONDPOSITION"){s.selected={spot.object};execute(Button("","置于顶层","","@front"));return;}
        if(command=="BONDDISPLAY"){error(s.submit(Command("键显示",kInvalidateAppearance|kInvalidateStructure,[id,value](CommandContext& c){auto display=value=="DASHED"||value=="TAUTOMERIC"?BondDisplay::Dashed:value=="HASHED"?BondDisplay::Hash:value=="HASHEDWEDGED"?BondDisplay::WedgeHash:value=="BOLD"||value=="DOUBLEBOLD"?BondDisplay::Bold:value=="WEDGED"?BondDisplay::Wedge:BondDisplay::Wavy;auto e=c.document.setBondDisplay(id,display);if(e.ok()&&(value=="TAUTOMERIC"||value=="DOUBLEBOLD"))e=c.document.setBondOrder(id,BondOrder::Double);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"键显示"));return;}
        if(command=="FUSERING"){int n=atoi(value.c_str());rings_chains::RingRequest r;r.mode=rings_chains::RingAttachMode::FuseOnBond;r.anchorBond=id;r.pointer=spot.point;r.preset=n==1?rings_chains::RingPresetId::Cyclopentadiene:n==2?rings_chains::RingPresetId::Benzene:n==9?rings_chains::RingPresetId::Chair1:n==10?rings_chains::RingPresetId::Chair2:static_cast<rings_chains::RingPresetId>(n-3);rings_chains::RingPlacement p;auto e=rings_chains::resolveRingPlacement(s.doc,r,&p);if(e.ok())e=s.submit(rings_chains::commands::createRingCommand(p,0,0),"稠合环");error(e);return;}
    }
}
#endif
