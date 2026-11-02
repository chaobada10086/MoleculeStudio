#pragma once
#include "keymap_data.hpp"
#include "next/bonds/geometry.hpp"
// Regression checks for the alkyne sprout and triple-bond rendering.
static void tripleBondCheck(App& app,const std::string& dir){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto& s=app.s;
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    ObjectId a,b,bond;
    require(s.doc.addAtom(Vec2(0,0),6,&a).ok(),"add first alkyne atom");
    require(s.doc.addAtom(Vec2(30,0),6,&b).ok(),"add terminal alkyne atom");
    require(s.doc.addBond(a,b,&bond).ok(),"add alkyne attachment bond");
    s.refresh();
    auto view=s.view();view.center=Vec2(15,0);view.scale=8;s.setView(view);
    interaction::InputEvent move;
    move.kind=interaction::InputKind::PointerMove;
    move.focus=interaction::InputFocus::Canvas;
    move.devicePosition=s.view().toDevice(Vec2(30,0));
    move.documentId=s.doc.id();
    move.viewGeneration=s.viewSerial;
    require(s.input.handle(move,&s),"hover terminal atom");
    require(s.input.hotspot().kind==interaction::HotspotKind::Atom&&s.input.hotspot().object.id==b,"hotspot is the terminal atom");
    size_t index=0;bool found=false;
    for(size_t i=0;i<sizeof(keymap::hotkeys)/sizeof(keymap::hotkeys[0]);++i){
        std::string command=keymap::hotkeys[i].command,value=keymap::hotkeys[i].value;
        if(command=="SPROUT"&&value=="10"){index=i;found=true;break;}
    }
    require(found,"alkyne hotkey exists");
    app.editorHotkey(int(index));
    require(s.doc.stats().aliveAtoms==3,"alkyne adds exactly one carbon");
    require(s.doc.stats().aliveBonds==2,"alkyne adds exactly one bond");
    ObjectId added=kInvalidObjectId,triple=kInvalidObjectId;
    for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()&&atom.id!=a&&atom.id!=b)added=atom.id;
    require(added!=kInvalidObjectId,"new alkyne carbon present");
    for(const auto& candidate:s.doc.bonds().records())if(candidate.isAlive()&&candidate.order==BondOrder::Triple)triple=candidate.id;
    require(triple!=kInvalidObjectId,"new bond is a triple bond");
    Vec2 in=s.doc.atoms().get(a)->position-s.doc.atoms().get(b)->position;
    Vec2 out=s.doc.atoms().get(added)->position-s.doc.atoms().get(b)->position;
    double cosine=dot(in,out)/(core::length(in)*core::length(out));
    require(cosine<-0.999999,"alkyne sprout is 180 degrees");

    // A bond sprouted from the triple-bond endpoint must continue at 180 too.
    size_t singleIndex=0;bool singleFound=false;
    for(size_t i=0;i<sizeof(keymap::hotkeys)/sizeof(keymap::hotkeys[0]);++i){
        std::string command=keymap::hotkeys[i].command,value=keymap::hotkeys[i].value;
        if(command=="SPROUT"&&value=="1"){singleIndex=i;singleFound=true;break;}
    }
    require(singleFound,"single-bond sprout hotkey exists");
    view=s.view();view.center=s.doc.atoms().get(added)->position;view.scale=8;s.setView(view);
    move.devicePosition=s.view().toDevice(s.doc.atoms().get(added)->position);
    move.documentId=s.doc.id();move.viewGeneration=s.viewSerial;
    require(s.input.handle(move,&s),"hover triple endpoint");
    require(s.input.hotspot().kind==interaction::HotspotKind::Atom&&s.input.hotspot().object.id==added,"hotspot is the triple endpoint");
    app.editorHotkey(int(singleIndex));
    require(s.doc.stats().aliveAtoms==4,"single bond adds exactly one carbon");
    require(s.doc.stats().aliveBonds==3,"single bond adds exactly one bond");
    ObjectId tail=kInvalidObjectId;
    for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()&&atom.id!=a&&atom.id!=b&&atom.id!=added)tail=atom.id;
    require(tail!=kInvalidObjectId,"new terminal carbon present");
    Vec2 incoming=s.doc.atoms().get(b)->position-s.doc.atoms().get(added)->position;
    Vec2 outgoing=s.doc.atoms().get(tail)->position-s.doc.atoms().get(added)->position;
    double tailCosine=dot(incoming,outgoing)/(core::length(incoming)*core::length(outgoing));
    require(tailCosine<-0.999999,"bond from triple endpoint is 180 degrees");

    // The toolbar single-bond tool must obey the same linear continuation.
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    ObjectId ta,tb,tbond;
    require(s.doc.addAtom(Vec2(0,0),6,&ta).ok(),"tool fixture atom A");
    require(s.doc.addAtom(Vec2(30,0),6,&tb).ok(),"tool fixture atom B");
    require(s.doc.addBond(ta,tb,&tbond).ok(),"tool fixture bond");
    require(s.doc.setBondOrder(tbond,BondOrder::Triple).ok(),"tool fixture triple bond");
    s.refresh();
    auto toolView=s.view();toolView.center=Vec2(15,0);toolView.scale=8;s.setView(toolView);
    require(s.activateCommand("chooseBondTool_Solid"),"activate single bond tool");
    s.gesture(PointerPhase::Down,Vec2(30,0),0);
    s.gesture(PointerPhase::Up,Vec2(30,0),0);
    require(s.doc.stats().aliveAtoms==3,"tool adds exactly one carbon");
    require(s.doc.stats().aliveBonds==2,"tool adds exactly one bond");
    ObjectId toolTail=kInvalidObjectId;
    for(const auto& atom:s.doc.atoms().records())if(atom.isAlive()&&atom.id!=ta&&atom.id!=tb)toolTail=atom.id;
    require(toolTail!=kInvalidObjectId,"tool terminal carbon present");
    Vec2 toolIn=s.doc.atoms().get(ta)->position-s.doc.atoms().get(tb)->position;
    Vec2 toolOut=s.doc.atoms().get(toolTail)->position-s.doc.atoms().get(tb)->position;
    double toolCosine=dot(toolIn,toolOut)/(core::length(toolIn)*core::length(toolOut));
    require(toolCosine<-0.999999,"tool bond from triple endpoint is 180 degrees");

    r62::bonds::BondGeometryOptions options;
    r62::bonds::BondAxes axes;
    axes.order=BondOrder::Triple;axes.display=BondDisplay::Normal;axes.side=r62::bonds::DoubleSide::Centered;
    auto geometry=r62::bonds::buildBondGeometry(Vec2(0,0),Vec2(100,0),axes,options);
    require(geometry.shapes.size()==3,"triple bond renders three strokes");
    double x0=geometry.shapes[0].points[0].x,x1=geometry.shapes[0].points[1].x;
    require(std::abs(x0)<1e-9&&std::abs(x1-100.0)<1e-9,"triple bond reaches both atom centres");
    for(const auto& shape:geometry.shapes){
        require(shape.points.size()==2,"triple stroke has two endpoints");
        require(std::abs(shape.points[0].x-x0)<1e-9&&std::abs(shape.points[1].x-x1)<1e-9,"triple bond ends are flush");
    }
    std::ofstream(dir+"/triple_bond_checks.json")<<"{\"one_new_carbon\":true,\"one_new_bond\":true,\"triple_order\":true,\"linear_180_degrees\":true,\"three_flush_strokes\":true,\"connected_to_atom_centres\":true,\"single_bond_from_triple_end_180\":true,\"bond_tool_from_triple_end_180\":true,\"cosine\":"<<cosine<<",\"tail_cosine\":"<<tailCosine<<",\"tool_cosine\":"<<toolCosine<<"}";
}
