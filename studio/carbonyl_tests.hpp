#include "engine/atom_runs.hpp"
// Native font/geometry regressions and actual editing transactions.
{
    using namespace r62::integration;
    Session carbonyl;auto error=persistence::readCdxml(projectAcetoneXml(),carbonyl.doc);require(error.ok(),error.message.c_str());
    carbonyl.refresh();RingGeometry rings(carbonyl.doc);auto labels=molecular::labels(carbonyl.doc,carbonyl.glyphs,rings);
    ObjectId oxygen=kInvalidObjectId;
    for(const auto& a:carbonyl.doc.atoms().records())if(a.isAlive()&&a.element==8){oxygen=a.id;
        auto runs=studioatom::runs(carbonyl.doc,a.id);require(!runs.empty()&&(runs[0].flags&kFormula)&&!(runs[0].flags&(kSuperscript|kSubscript)),"CDXML face 96 means formula, never two script flags");
        const auto& layout=labels.at(a.id).layout;require(layout.glyphs.size()==1&&std::abs(layout.glyphs[0].size-10)<1e-8,"native oxygen remains full-sized");
        Vec2 baseline=layout.anchor+Vec2(0,layout.metrics.firstBaseline);require(baseline.finite(),"oxygen baseline is finite");
        require(layout.glyphs[0].contours[0].size()>80,"oxygen curves remain smooth at high zoom");
    }
    require(oxygen!=kInvalidObjectId,"carbonyl oxygen imported");
    for(const auto& b:carbonyl.doc.bonds().records())if(b.isAlive()&&b.order==BondOrder::Double){
        auto data=rings.build(b);molecular::clip(data,b,labels,molecular::setting(carbonyl.doc,"MarginWidth",1.6));require(data.shapes.size()==2,"carbonyl has two visible strokes");
        require(std::abs(length(data.shapes[0].points[0]-data.shapes[1].points[0])-2.592)<.01,"18 percent is the entire distance between double-bond strokes");
        for(auto& shape:data.shapes){require(shape.points.size()==2,"carbonyl clipped stroke endpoints");require(length(shape.points[1]-carbonyl.doc.atoms().get(oxygen)->position)>1.5,"oxygen label has positive clearance");}
    }
    auto digest=carbonyl.doc.digest();auto before=carbonyl.scene.bounds();auto refs=carbonyl.all();require(carbonyl.move(refs,Vec2(37,19)).ok(),"drag complete carbonyl");auto after=carbonyl.scene.bounds();require(length(after.min-before.min-Vec2(37,19))<.001,"drag keeps oxygen and clipped bonds aligned");require(carbonyl.undo().ok()&&carbonyl.doc.digest()==digest,"carbonyl drag undo");
    auto path=testOut+"/carbonyl_roundtrip.cdxml";require(carbonyl.save(path,true).ok(),"save carbonyl");Session reopened;require(reopened.open(path).ok(),"reopen carbonyl");require(reopened.doc.stats().aliveAtoms==4&&reopened.doc.stats().aliveBonds==3,"carbonyl connectivity preserved");
    Session drawn;ObjectId c,o,left,right,bond,single;drawn.doc.addAtom(Vec2(0,0),6,&c);drawn.doc.addAtom(Vec2(0,-14.4),8,&o);drawn.doc.addAtom(Vec2(-12.4708,7.2),6,&left);drawn.doc.addAtom(Vec2(12.4708,7.2),6,&right);BondRecord doubleBond;doubleBond.beginAtom=c;doubleBond.endAtom=o;doubleBond.order=BondOrder::Double;drawn.doc.addBondRecord(doubleBond,&bond);drawn.doc.addBond(c,left,&single);drawn.doc.addBond(c,right,&single);drawn.refresh();
    RingGeometry drawnRings(drawn.doc);auto fresh=molecular::labels(drawn.doc,drawn.glyphs,drawnRings);auto base=fresh.at(o).layout;Vec2 baseline=base.anchor+Vec2(0,base.metrics.firstBaseline);require(std::abs(baseline.y+14.4-3.904)<.001,"newly drawn oxygen uses native baseline too");
    // Partial drag rotates/lengthens C=O. The glyph remains upright, with one
    // shared clipped end on the two strokes. Undo restores the imported layout.
    auto original=drawn.doc.digest();require(drawn.move({ObjectRef(ObjectKind::Atom,o)},Vec2(10,3)).ok(),"partial carbonyl drag");require(drawn.undo().ok()&&drawn.doc.digest()==original,"partial carbonyl drag undo");
    for(int angle=0;angle<360;angle+=15){double radians=angle*3.141592653589793/180;Document document=drawn.doc;
        for(const auto& a:drawn.doc.atoms().records())if(a.isAlive())document.setAtomPosition(a.id,Vec2(a.position.x*std::cos(radians)-a.position.y*std::sin(radians),a.position.x*std::sin(radians)+a.position.y*std::cos(radians)));
        RingGeometry geometry(document);auto label=molecular::labels(document,drawn.glyphs,geometry);auto data=geometry.build(*document.bonds().get(bond));molecular::clip(data,*document.bonds().get(bond),label,1.59);
        require(data.shapes.size()==2,"rotated carbonyl strokes remain visible");Vec2 v=document.atoms().get(o)->position-document.atoms().get(c)->position;v=v*(1/length(v));require(std::abs(dot(data.shapes[0].points[1]-data.shapes[1].points[1],v))<.001,"rotated carbonyl ends align");
    }
    std::ofstream(testOut+"/carbonyl_tests.json")<<"{\"native_formula_face\":true,\"baseline\":true,\"smooth_oxygen\":true,\"native_spacing\":true,\"native_clearance\":true,\"drag_and_undo\":true,\"partial_drag\":true,\"rotations\":24,\"save_reload\":true}";
}
