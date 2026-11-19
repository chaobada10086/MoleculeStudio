#include "engine/projection_molecule.hpp"
#include "engine/atom_runs.hpp"
#include "engine/metal_labels.hpp"
#include "nickname_forms.hpp"
#include "engine/hydride_labels.hpp"
#ifndef STUDIO_MOLECULAR_RENDER_HPP
#define STUDIO_MOLECULAR_RENDER_HPP
#include "next/chemistry/edit.hpp"
#include "next/integration/ring_geometry.hpp"
#include "next/text/render_bridge.hpp"
#include "third_party/tinyxml2/tinyxml2.h"
#include <map>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
namespace r62 { namespace integration { namespace molecular {
using namespace core;
inline const char* electronMarkerKey(bool radical){
    return radical?"pst.attr.MoleculeStudioRadicalPosition":"pst.attr.MoleculeStudioChargePosition";
}
inline bool attachedElectronMarker(const Document& doc,ObjectId id,bool radical){
    return doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue(electronMarkerKey(radical))!=nullptr;
}
inline bool circledElectronCharge(const Document& doc,ObjectId id){
    return doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("pst.attr.MoleculeStudioChargeCircle")!=nullptr;
}
inline double electronChargeCircleRadius(double em){return em*.34;}
inline Vec2 boundedElectronOffset(Vec2 offset,double em){
    double distance=length(offset);
    if(!std::isfinite(distance)||distance<1e-8)return Vec2(em*.55,-em*.55);
    return offset*(std::max(em*.55,std::min(em*1.4,distance))/distance);
}
inline Vec2 electronMarkerCenter(const Document& doc,const AtomRecord& atom,bool radical,double em,Vec2 fallback){
    auto value=doc.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue(electronMarkerKey(radical));
    if(value){std::istringstream in(*value);Vec2 offset;
        if(in>>offset.x>>offset.y)return atom.position+boundedElectronOffset(offset*em,em);
    }
    return fallback;
}
// Ordinary atomic/hydride labels choose the upper side away from bonds.
// Keep named compound captions in their authored connection-order convention.
inline bool automaticChargeOnLeft(const Document& doc,const AtomRecord& atom){
    const auto label=doc.atomLabel(atom.id);
    if(!label.empty()&&label!=elementSymbol(atom.element)&&studio::hydride_labels::count(atom.element,label)<0)return false;
    const Vec2 right(.7808688094,-.6246950476),left(-.7808688094,-.6246950476);
    double rightClear=2.,leftClear=2.;bool connected=false;
    for(auto id:doc.incidentBonds(atom.id))if(auto bond=doc.bonds().alive(id)){
        if(bond->semantics==BondSemantics::Hydrogen)continue;auto other=doc.atoms().alive(bond->beginAtom==atom.id?bond->endAtom:bond->beginAtom);if(!other)continue;
        auto direction=other->position-atom.position;double span=length(direction);if(span<1e-8)continue;direction=direction*(1./span);connected=true;
        rightClear=std::min(rightClear,1.-dot(direction,right));leftClear=std::min(leftClear,1.-dot(direction,left));
    }
    return connected&&leftClear>rightClear+.05;
}
struct Label {text::RichLayout layout;Box box;};
typedef std::map<ObjectId,Label> Labels;
inline void dockProjectionLabel(Label& label,const Document& doc,const AtomRecord& atom);
inline void placeAutomaticCharge(Label& placed,const Document& doc,const AtomRecord& atom,size_t run,double em){
    if(!automaticChargeOnLeft(doc,atom))return;
    Box body,charge;bool hasBody=false,hasCharge=false;
    for(const auto& glyph:placed.layout.glyphs)for(const auto& contour:glyph.contours)for(auto point:contour){point=point+placed.layout.anchor;
        if(glyph.runIndex==run){if(hasCharge)charge.add(point);else{charge=Box(point,point);hasCharge=true;}}
        else{if(hasBody)body.add(point);else{body=Box(point,point);hasBody=true;}}
    }
    if(!hasBody||!hasCharge)return;
    const double dx=body.min.x-std::max(.6,em*.10)-charge.max.x;
    for(auto& glyph:placed.layout.glyphs)if(glyph.runIndex==run){glyph.x+=dx;for(auto& contour:glyph.contours)for(auto& point:contour)point.x+=dx;}
    bool any=false;Box ink;for(const auto& glyph:placed.layout.glyphs)for(const auto& contour:glyph.contours)for(auto point:contour){point=point+placed.layout.anchor;if(any)ink.add(point);else{ink=Box(point,point);any=true;}}
    if(any){placed.box=ink;placed.layout.metrics.inkBox=ink;}
}

inline double setting(const Document& doc,const char* key,double fallback){
    auto value=doc.documentExtensions().findValue(std::string("studio.style.")+key);
    if(!value)return fallback;double n=std::atof(value->c_str());return std::isfinite(n)&&n>=0?n:fallback;
}
inline bool sourcePosition(const Document& doc,ObjectId id,Vec2* p){
    auto value=doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.atom.source.xy");
    if(!value)return false;std::istringstream in(*value);return bool(in>>p->x>>p->y);
}
inline int labelHydrogenCount(const Document& doc,const AtomRecord& atom){
    const auto& sem=atom.semantics;
    if(sem.implicitHydrogens>=0)return sem.implicitHydrogens;
    if(atom.element==6&&sem.nodeType==uint8_t(NodeType::Element)&&doc.incidentBonds(atom.id).empty()&&
       !doc.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("pst.attr.MoleculeStudioLiteralLabel"))
        return std::max(0,chemistry::refreshAtomDerived(doc,atom.id,{}).newHydrogenCount);
    if((atom.element!=7&&atom.element!=8)||sem.nodeType!=uint8_t(NodeType::Element)||
       sem.aromatic||sem.radical)return 0;
    const auto& incident=doc.incidentBonds(atom.id);
    if(incident.empty()||doc.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("pst.attr.MoleculeStudioLiteralLabel"))
        return 0;
    for(auto id:incident){
        const auto* bond=doc.bonds().alive(id);
        if(!bond||bond->semantics!=BondSemantics::Normal||
           bond->order<BondOrder::Single||bond->order>BondOrder::Triple)return 0;
    }
    const auto derived=chemistry::refreshAtomDerived(doc,atom.id,{});
    return derived.overCoordinated?0:std::max(0,derived.newHydrogenCount);
}
// Recognize elemental hydrides, including imported H2N / NH₂ spellings.
// This changes glyph placement only: labels, runs and atom semantics stay intact.
inline bool hydrideLabel(int element,const std::string& label,bool* hydrogenFirst,size_t* elementChars){
    if(studio::hydride_labels::count(element,label,hydrogenFirst)<0)return false;
    *elementChars=std::string(elementSymbol(element)).size();
    return true;
}
inline void orientHydride(Label& placed,const AtomRecord& atom,const Document& doc,const std::string& label,size_t baseGlyphs,Vec2 shift,double size,bool rightBlocked){
    bool first=false;size_t letters=0;if(!hydrideLabel(atom.element,label,&first,&letters))return;
    auto align=doc.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("studio.text.LabelAlignment");
    if(align&&(*align=="Above"||*align=="Below"))return;
    double rightScore=-1,leftScore=-1,aboveScore=-1,belowScore=-1;int neighbours=0;
    for(auto id:doc.incidentBonds(atom.id)){auto bond=doc.bonds().alive(id);if(!bond||bond->semantics==BondSemantics::Hydrogen)continue;auto other=doc.atoms().alive(bond->beginAtom==atom.id?bond->endAtom:bond->beginAtom);if(other){auto d=other->position-atom.position;double len=length(d);if(len>1e-9){d=d*(1/len);rightScore=std::max(rightScore,d.x);leftScore=std::max(leftScore,-d.x);aboveScore=std::max(aboveScore,-d.y);belowScore=std::max(belowScore,d.y);++neighbours;}}}
    bool left=rightBlocked||(rightScore>.5&&leftScore<rightScore-1e-6);
    // A secondary amine with bonds on both sides needs H above/below N.
    // Prefer normal horizontal formulas unless the vertical gap is clearer.
    bool vertical=neighbours>1&&std::min(aboveScore,belowScore)+.25<std::min(leftScore,rightScore),above=aboveScore<=belowScore;
    auto& gs=placed.layout.glyphs;size_t isotope=atom.semantics.isotope?std::to_string(atom.semantics.isotope).size():0;
    if(baseGlyphs<=letters||gs.size()<isotope+baseGlyphs)return;
    size_t e0=isotope+(first?baseGlyphs-letters:0),e1=e0+letters;
    size_t h0=isotope+(first?0:letters),h1=first?e0:isotope+baseGlyphs;
    double ex=gs[e0].x,ew=gs[e1-1].x+gs[e1-1].advance-ex,hx=gs[h0].x,hw=gs[h1-1].x+gs[h1-1].advance-hx;
    auto move=[&](size_t begin,size_t end,double dx){for(size_t i=begin;i<end;++i){gs[i].x+=dx;for(auto& contour:gs[i].contours)for(auto& p:contour)p.x+=dx;}};
    double iw=isotope?gs[isotope-1].x+gs[isotope-1].advance-gs[0].x:0;
    if(isotope)move(0,isotope,ex-iw-gs[0].x);
    move(h0,h1,(vertical?ex+(ew-hw)*.5:left?ex-iw-hw:ex+ew)-hx);
    if(vertical){double dy=gs[e0].y-gs[h0].y+(above?-1:1)*size*.95;for(size_t i=h0;i<h1;++i){gs[i].y+=dy;for(auto& contour:gs[i].contours)for(auto& p:contour)p.y+=dy;}}
    if(gs.size()>isotope+baseGlyphs){size_t charge=isotope+baseGlyphs;move(charge,gs.size(),ex+ew+((left||vertical)?0:hw)-gs[charge].x);}
    placed.layout.anchor.x=atom.position.x+shift.x-ex-ew*.5;
    bool ink=false;
    for(const auto& g:gs)for(const auto& contour:g.contours)for(auto p:contour){
        p=p+placed.layout.anchor;
        if(ink)placed.box.add(p);else{placed.box=Box(p,p);ink=true;}
    }
    if(!ink)placed.box=Box(atom.position+Vec2(-ew*.5,-size*.5),atom.position+Vec2(ew*.5,size*.5));
    placed.layout.metrics.inkBox=placed.box;
}
inline Labels labels(const Document& doc,text::GlyphCache& glyphs,const RingGeometry& rings,const std::set<ObjectId>* affected=nullptr){
    const double tStart=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    Labels result;std::string labelFont=doc.defaultFont();if(auto f=doc.documentExtensions().findValue("studio.style.LabelFontName"))labelFont=*f;
    std::set<int> representedCharges;
    for(const auto& symbol:doc.symbols().records())if(symbol.isAlive()){
        auto xml=doc.extensions(ObjectRef(ObjectKind::Symbol,symbol.id)).findValue("studio.cdxml.graphic");
        if(!xml)continue;tinyxml2::XMLDocument dom;if(dom.Parse(xml->c_str())!=tinyxml2::XML_SUCCESS)continue;
        for(auto rep=dom.RootElement()->FirstChildElement("represent");rep;rep=rep->NextSiblingElement("represent"))
            if(rep->Attribute("attribute","Charge"))representedCharges.insert(rep->IntAttribute("object"));
    }
    const double tSymbols=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::vector<size_t> degree(doc.atoms().size(),0);std::map<ObjectId,const BondRecord*> terminal,terminalAny;
    for(auto& b:doc.bonds().records())if(b.isAlive()){++degree[b.beginAtom];++degree[b.endAtom];}
    for(auto& b:doc.bonds().records())if(b.isAlive()){
        // Hidden formula bonds retain chemical connectivity, but have no
        // visible corridor that should push apart or reorient their labels.
        const auto& bag=doc.extensions(ObjectRef(ObjectKind::Bond,b.id));auto visible=bag.findValue("pst.attr.Visible");if(!visible)visible=bag.findValue("studio.visible");if(visible&&*visible=="no")continue;
        if(degree[b.beginAtom]==1)terminalAny[b.beginAtom]=&b;if(degree[b.endAtom]==1)terminalAny[b.endAtom]=&b;
    }
    for(auto& b:doc.bonds().records())if(b.isAlive()&&b.order==BondOrder::Double&&b.display==BondDisplay::Normal&&b.stereo==BondStereo::None){
        if(degree[b.beginAtom]==1)terminal[b.beginAtom]=&b;if(degree[b.endAtom]==1)terminal[b.endAtom]=&b;
    }
    std::multimap<double,const AtomRecord*> visibleLabels;
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()){
        const auto label=doc.atomLabel(atom.id);
        if(atom.element!=6||degree[atom.id]==0||atom.semantics.showLabel||atom.semantics.isotope||(!label.empty()&&label!="C"))
            visibleLabels.emplace(atom.position.x,&atom);
    }
    const double tBonds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for(auto& a:doc.atoms().records())if(a.isAlive()&&(!affected||affected->count(a.id)||degree[a.id]==0)){
        std::string label=doc.atomLabel(a.id);if(label.empty())label=elementSymbol(a.element);
        // Connected skeletal carbons omit their label. A detached carbon
        // fragment needs its C/H label as well as its charge or radical marker.
        if(a.element==6&&(label.empty()||label=="C")&&!a.semantics.showLabel&&!a.semantics.isotope&&std::abs(int(a.semantics.charge))<=1&&
           degree[a.id]!=0)continue;
        if(doc.extensions(ObjectRef(ObjectKind::Atom,a.id)).findValue("studio.hydrogens.manual")&&a.semantics.implicitHydrogens>=0){int h=a.semantics.implicitHydrogens;label=std::string(elementSymbol(a.element))+(h?"H"+(h>1?std::to_string(h):std::string()):std::string());}
        // Standard CDXML NumHydrogens is chemical semantics, not a manual label
        // override.  Heteroatoms imported with NumHydrogens must display their
        // hydrogens (OH, NH, NH2, NH4+) or the review picture would show a bare
        // O/N while the underlying molecule still has the hydrogens.  Carbon
        // stays skeletal UNLESS it carries an important isotope/charge; then
        // its hydrogen count is part of the label too (13CH4, CH3+), otherwise
        // the renderer would show 13C or C+ and silently hide H3/H4.  Alias /
        // nickname labels are intentionally untouched.
        else if(label==elementSymbol(a.element)&&(a.element>1||a.semantics.isotope||a.semantics.charge)){
            const int h=labelHydrogenCount(doc,a);
            if(h>0)label+=std::string("H")+(h>1?std::to_string(h):std::string());
        }
        if(label.empty())continue;
        auto runs=studioatom::runs(doc,a.id);std::string content;for(auto& r:runs)content+=r.utf8;
        if(content!=label)runs.clear();
        if(runs.empty()){
            TextRun r;r.flags=kFormula;r.utf8=label;r.size=setting(doc,"LabelSize",10);r.fontName=labelFont;
            // A hydride count is never a charge magnitude. Keep it explicitly
            // subscript when a separate superscript charge run follows (CH3-).
            bool first=false;size_t letters=0;
            const auto digit=label.find_first_of("0123456789");
            if(hydrideLabel(a.element,label,&first,&letters)&&digit!=std::string::npos){
                auto end=digit;while(end<label.size()&&label[end]>='0'&&label[end]<='9')++end;
                r.utf8=label.substr(0,digit);if(!r.utf8.empty())runs.push_back(r);
                r.utf8=label.substr(digit,end-digit);r.flags=kSubscript;runs.push_back(r);
                r.utf8=label.substr(end);r.flags=kFormula;if(!r.utf8.empty())runs.push_back(r);
            }else runs.push_back(r);
        }
        const bool metalAttached=studio::metal_labels::active(doc,a.id);
        if(metalAttached)runs=studio::metal_labels::scripted(runs);
        text::LayoutOptions opt;opt.triangulateGlyphs=false;opt.defaultFont=labelFont;opt.defaultSize=setting(doc,"LabelSize",10);
        text::RichLayout measured;auto err=text::layoutRichText(runs,Vec2(),opt,&glyphs,&measured);if(!err.ok()||measured.glyphs.empty())continue;
        const size_t baseGlyphs=measured.glyphs.size();
        double size=runs.front().size>0?runs.front().size:opt.defaultSize;
        double attachedWidth=measured.glyphs.front().advance;
        if(label.size()>1&&label[1]>='a'&&label[1]<='z'&&label.substr(0,2)!="Me"&&label.substr(0,2)!="Et"&&label.substr(0,2)!="Ph"&&measured.glyphs.size()>1)attachedWidth+=measured.glyphs[1].advance;const size_t measuredRuns=runs.size();
        double firstInkMin=1e100,lastInkMax=-1e100;
        for(const auto& contour:measured.glyphs.front().contours)for(auto p:contour)firstInkMin=std::min(firstInkMin,p.x);
        for(const auto& contour:measured.glyphs.back().contours)for(auto p:contour)lastInkMax=std::max(lastInkMax,p.x);
        const double leftwardLabelShift=firstInkMin<1e99&&lastInkMax>-1e99?lastInkMax+firstInkMin-attachedWidth:measured.glyphs.back().x+measured.glyphs.back().advance*.5-attachedWidth*.5;
        bool automaticCharge=false;
        if(a.semantics.charge&&!attachedElectronMarker(doc,a.id,false)&&!representedCharges.count(a.sourceId)&&label.find('+')==std::string::npos&&label.find('-')==std::string::npos&&label.find("−")==std::string::npos){
            TextRun r;r.fontName=opt.defaultFont;r.size=size;r.flags=kSuperscript;int charge=a.semantics.charge;
            r.utf8=(std::abs(charge)>1?std::to_string(std::abs(charge)):"")+(charge>0?std::string("+"):std::string("−"));runs.push_back(r);automaticCharge=true;
        }
        // Radical electrons are NOT a text superscript: the scene builder draws
        // filled dots beside the label from atom.semantics.radical, so the dot
        // follows the atom and stays round at every zoom. A text run here would
        // duplicate it and sit inside the element glyph.
        double isotopeWidth=0;
        if(a.semantics.isotope&&!(label[0]>='0'&&label[0]<='9')){TextRun r;r.utf8=std::to_string(a.semantics.isotope);r.size=size;r.fontName=opt.defaultFont;r.flags=kSuperscript;text::RichLayout isotope;text::layoutRichText({r},Vec2(),opt,&glyphs,&isotope);isotopeWidth=isotope.metrics.width;runs.insert(runs.begin(),r);}
        if(runs.size()!=measuredRuns)text::layoutRichText(runs,Vec2(),opt,&glyphs,&measured);
        Vec2 shift,oldShift;
        auto bond=terminal.find(a.id);
        if(bond!=terminal.end()){
            const auto& b=*bond->second;Vec2 start=doc.atoms().get(b.beginAtom)->position,end=doc.atoms().get(b.endAtom)->position;
            Vec2 v=end-start;double len=length(v);
            auto data=rings.build(b);
            if(len>1e-6&&data.shapes.size()==2){Vec2 n(-v.y/len,v.x/len);double offset=0;
                for(auto& shape:data.shapes)if(!shape.points.empty())offset+=dot(shape.points.front()-start,n)*.5;
                shift=n*offset;
                Vec2 sa,se;if(sourcePosition(doc,b.beginAtom,&sa)&&sourcePosition(doc,b.endAtom,&se)){
                    auto original=se-sa;double l=length(original);if(l>1e-6)oldShift=Vec2(-original.y/l,original.x/l)*(offset*l/len);
                }
            }
        }
        // Atom label anchors are BASELINES. RichLayout's input is the line-box
        // top: adding its firstBaseline a second time put O inside the C=O bond.
        // Arial's default ChemDraw atom baseline is 0.3904 em below the node;
        // imported labels retain their exact native baseline and font cell.
        Vec2 baseline=a.position+shift+Vec2(-attachedWidth*.5-isotopeWidth,size*.3904);
        const auto& ext=doc.extensions(ObjectRef(ObjectKind::Atom,a.id));
        auto saved=ext.findValue("studio.label.offset"),savedText=ext.findValue("studio.label.text");
        bool native=saved&&savedText&&*savedText==label;
        const bool literal=ext.findValue("pst.attr.MoleculeStudioLiteralLabel")!=nullptr;
        bool rightBlocked=false;
        auto connected=terminalAny.find(a.id);
        if(connected!=terminalAny.end()){
            const auto& b=*connected->second;
            auto other=doc.atoms().alive(b.beginAtom==a.id?b.endAtom:b.beginAtom);
            if(other){
                const auto direction=other->position-a.position;
                rightBlocked=direction.x>length(direction)*.5;
            }
        }
        const bool shortTerminalGroup=connected!=terminalAny.end()&&
            (label=="OH"||label=="HO"||label=="CN"||label=="NC");
        if((!native||shortTerminalGroup)&&!literal&&!rightBlocked&&baseGlyphs>1){
            const double rightEdge=a.position.x+measured.metrics.width+size*.35;
            for(auto it=visibleLabels.upper_bound(a.position.x);it!=visibleLabels.end()&&it->first<rightEdge;++it){
                if(std::abs(it->second->position.y-a.position.y)<size*.75){rightBlocked=true;break;}
            }
        }
        // Some old files save an automatic right-justified HO/NC label even
        // when its bond is vertical. Reflow it around the same attachment atom.
        if(native&&shortTerminalGroup&&!literal&&!rightBlocked){
            auto align=ext.findValue("studio.text.LabelAlignment");
            if(!align||(*align!="Above"&&*align!="Below"))native=false;
        }
        if(!native&&!literal&&rightBlocked&&measured.glyphs.size()>1){
            auto connected=terminalAny.find(a.id);
            if(connected!=terminalAny.end()){
                const auto& b=*connected->second;ObjectId other=b.beginAtom==a.id?b.endAtom:b.beginAtom;
                const auto* neighbour=doc.atoms().alive(other);
                // On the left, anchor the last character at the bond endpoint;
                // the rest of the abbreviation extends away from the bond.
                if(neighbour&&neighbour->position.x>a.position.x+1e-9){
                    Vec2 direction=neighbour->position-a.position;double lengthToNeighbour=length(direction);
                    // A slanted bond intersects glyphs above their horizontal
                    // centre. Compensate for that vertical approach so the
                    // two sides do not acquire visibly different clearances.
                    double slant=lengthToNeighbour>1e-9?std::abs(direction.y)/lengthToNeighbour:0;
                    baseline.x-=std::max(0.,leftwardLabelShift-(measured.glyphs.size()==2?size*.4*slant:0));
                }
            }
        }
        if(native){std::istringstream in(*saved);Vec2 offset;if(in>>offset.x>>offset.y)baseline=a.position+offset-oldShift+shift;
            auto justify=ext.findValue("studio.text.LabelJustification");if(!justify)justify=ext.findValue("studio.text.Justification");
            if(justify&&*justify=="Right")baseline.x-=measured.metrics.width;
            else if(justify&&*justify=="Center")baseline.x-=measured.metrics.width*.5;
        }
        // Seed the outward alignment; final spacing uses the actual glyph ink
        // after group orientation, hydrogens and charge placement are complete.
        if(doc.incidentBonds(a.id).size()==1){auto edge=doc.bonds().alive(doc.incidentBonds(a.id).front());auto other=edge->beginAtom==a.id?edge->endAtom:edge->beginAtom;
            if(projection_molecule::kind(doc,other)>=0&&projection_molecule::kind(doc,other)<3){auto ray=a.position-doc.atoms().alive(other)->position;double span=length(ray);if(span>1e-8){ray=ray*(1/span);baseline=a.position;baseline.y+=ray.y<-.65?0:ray.y>.65?size*.72:size*.32;baseline.x-=ray.x>.3?0:ray.x<-.3?measured.metrics.width:measured.metrics.width*.5;native=false;}}
        }
        Vec2 top=baseline-Vec2(0,measured.metrics.firstBaseline);
        Label placed;placed.layout=std::move(measured);placed.layout.anchor=top;
        if(!placed.layout.metrics.inkBox.empty()){placed.layout.metrics.inkBox.min=placed.layout.metrics.inkBox.min+top;placed.layout.metrics.inkBox.max=placed.layout.metrics.inkBox.max+top;}
        if(native){
            auto align=ext.findValue("studio.text.LabelAlignment");
            auto moveGlyph=[](text::GlyphPlacement& g,Vec2 delta){g.x+=delta.x;g.y+=delta.y;for(auto& contour:g.contours)for(auto& p:contour)p=p+delta;};
            auto& gs=placed.layout.glyphs;
            // CDXML stores chemical tokens in connection order. Right-facing
            // labels reverse tokens (CO2Me -> MeO2C), never individual letters.
            if(align&&*align=="Right"){
                std::vector<size_t> starts{0};
                for(size_t i=1;i<gs.size();++i)if(gs[i].codepoint>='A'&&gs[i].codepoint<='Z')starts.push_back(i);
                starts.push_back(gs.size());double cursor=0;
                for(size_t j=starts.size()-1;j>0;--j){size_t first=starts[j-1],end=starts[j];if(first==end)continue;double x0=gs[first].x,x1=gs[end-1].x+gs[end-1].advance;
                    for(size_t k=first;k<end;++k)moveGlyph(gs[k],Vec2(cursor-x0,0));cursor+=x1-x0;
                }
            }
            if(align&&(*align=="Above"||*align=="Below")&&gs.size()==2&&(label=="NH"||label=="PH")){
                double advance=gs[0].advance;double dy=size*.834;
                if(*align=="Above"){moveGlyph(gs[0],Vec2(0,dy));moveGlyph(gs[1],Vec2((advance-gs[1].advance)*.5-gs[1].x,0));}
                else moveGlyph(gs[1],Vec2((advance-gs[1].advance)*.5-gs[1].x,dy));
            }
        }
        // Abbreviations are drawn in CONNECTION ORDER: -OAc must touch the bond
        // with O, never with the acetate carbon, so the side the bond arrives
        // from picks which spelling of the group is laid out (OAc <-> AcO,
        // CO2Me <-> MeO2C). Only glyph order changes; the stored label text and
        // the atom semantics are untouched.
        if(!native&&!literal&&placed.layout.glyphs.size()==label.size()){
            auto form=nicknameform::oriented(label,rightBlocked);
            if(form!=label&&form.size()==label.size()){
                auto& gs=placed.layout.glyphs;
                // The two spellings must be a permutation of the same characters;
                // anything else (styling runs, odd input) keeps the typed order.
                std::vector<bool> probe(gs.size(),false);bool permutation=true;
                for(unsigned char ch:form){
                    size_t match=gs.size();
                    for(size_t k=0;k<gs.size();++k)if(!probe[k]&&gs[k].codepoint==ch){match=k;break;}
                    if(match==gs.size()){permutation=false;break;}
                    probe[match]=true;
                }
                if(permutation){
                    std::vector<bool> used(gs.size(),false);double cursor=0;
                    for(unsigned char ch:form){
                        size_t match=gs.size();
                        for(size_t k=0;k<gs.size();++k)if(!used[k]&&gs[k].codepoint==ch){match=k;break;}
                        if(match==gs.size())break;
                        double dx=cursor-gs[match].x;gs[match].x+=dx;for(auto& contour:gs[match].contours)for(auto& p:contour)p.x+=dx;
                        cursor+=gs[match].advance;used[match]=true;
                    }
                }
            }
        }
        placed.box=Box(baseline+Vec2(0,-.845*size),baseline+Vec2(placed.layout.metrics.width,.01*size));
        // Superscripts and styled label runs must also remain clear of bonds.
        for(auto& g:placed.layout.glyphs)for(auto& contour:g.contours)for(auto p:contour)placed.box.add(p+placed.layout.anchor);
        if(!literal)orientHydride(placed,a,doc,label,baseGlyphs,shift,size,rightBlocked);
        if(automaticCharge&&!literal)placeAutomaticCharge(placed,doc,a,runs.size()-1,size);
        // Bound the amount of bond covered by a new terminal label. This is
        // based on ink in the bond corridor, including unknown/free labels;
        // atom coordinates and explicit imported typography stay unchanged.
        bool hydrideFirst=false;size_t hydrideChars=0;
        // A bare element symbol (O, N, S, Cl, ...) is centred on its atom the
        // way ChemDraw does it, and imported files keep that exact native
        // baseline. Shifting it to bound bond coverage would leave a freshly
        // drawn O on a different baseline than the same atom re-imported, so
        // reserve the coverage shift for labels that carry extra text
        // (hydrogens, isotopes, charges, aliases) and genuinely hang over the
        // bond corridor.
        const bool bareElement=label==elementSymbol(a.element)&&!a.semantics.isotope&&!a.semantics.charge;
        if(!native&&!bareElement&&!hydrideLabel(a.element,label,&hydrideFirst,&hydrideChars)&&terminalAny.count(a.id)){
            const auto& b=*terminalAny.at(a.id);auto other=b.beginAtom==a.id?b.endAtom:b.beginAtom;
            Vec2 toward=doc.atoms().get(other)->position-a.position;double distance=length(toward);
            if(distance>1e-6){toward=toward*(1/distance);Vec2 normal(-toward.y,toward.x);double intrusion=-1e100;
                for(const auto& g:placed.layout.glyphs)for(const auto& contour:g.contours)for(auto p:contour){
                    Vec2 v=p+placed.layout.anchor-a.position;
                    if(std::abs(dot(v,normal))<=size*.22)intrusion=std::max(intrusion,dot(v,toward));
                }
                double limit=std::min(size*.25,distance*.2);
                if(intrusion>limit){Vec2 move=toward*(limit-intrusion);placed.layout.anchor=placed.layout.anchor+move;
                    placed.box.min=placed.box.min+move;placed.box.max=placed.box.max+move;
                    placed.layout.metrics.inkBox.min=placed.layout.metrics.inkBox.min+move;placed.layout.metrics.inkBox.max=placed.layout.metrics.inkBox.max+move;}
            }
        }
        // In compact organotin/organosilicon hydrides the attachment is Sn/Si,
        // not the middle of "Bu3". Keep the element's two glyphs on the actual
        // atom, so an exposed H bond and subsequent arrow capture dock there.
        if(!literal&&(a.element==50||a.element==14)&&label!=elementSymbol(a.element)){
            const auto& gs=placed.layout.glyphs;Box elementInk;bool found=false;
            for(size_t i=0;i+1<gs.size();++i)if(gs[i].codepoint=='S'&&gs[i+1].codepoint==(a.element==50?'n':'i')){
                for(size_t j=i;j<=i+1;++j)for(const auto& contour:gs[j].contours)for(auto point:contour){point=point+placed.layout.anchor;if(found)elementInk.add(point);else{elementInk=Box(point,point);found=true;}}
                break;
            }
            if(found){auto move=a.position-(elementInk.min+elementInk.max)*.5;placed.layout.anchor=placed.layout.anchor+move;placed.box.min=placed.box.min+move;placed.box.max=placed.box.max+move;placed.layout.metrics.inkBox.min=placed.layout.metrics.inkBox.min+move;placed.layout.metrics.inkBox.max=placed.layout.metrics.inkBox.max+move;}
        }
        if(metalAttached){
            const auto span=studio::metal_labels::find(label);size_t first=0;
            for(size_t i=0;i<span.start;++i)if((static_cast<unsigned char>(label[i])&0xC0)!=0x80)++first;
            auto& gs=placed.layout.glyphs;
            if(first+span.size<=baseGlyphs){
                std::vector<size_t> order;bool right=studio::metal_labels::bondOnRight(doc,a.id);
                auto metal=[&](){for(size_t i=first;i<first+span.size;++i)order.push_back(i);};
                if(!right)metal();for(size_t i=0;i<baseGlyphs;++i)if(i<first||i>=first+span.size)order.push_back(i);if(right)metal();
                double cursor=0;for(auto index:order){auto& glyph=gs[index];double dx=cursor-glyph.x;glyph.x+=dx;for(auto& contour:glyph.contours)for(auto& point:contour)point.x+=dx;cursor+=glyph.advance;}
                Box ink;bool found=false;for(size_t i=first;i<first+span.size;++i)for(const auto& contour:gs[i].contours)for(auto point:contour){point=point+placed.layout.anchor;if(found)ink.add(point);else {ink=Box(point,point);found=true;}}
                if(found){auto move=a.position+shift-(ink.min+ink.max)*.5;placed.layout.anchor=placed.layout.anchor+move;
                    Box bounds;bool any=false;for(const auto& glyph:gs)for(const auto& contour:glyph.contours)for(auto point:contour){point=point+placed.layout.anchor;if(any)bounds.add(point);else{bounds=Box(point,point);any=true;}}
                    if(any){placed.box=bounds;placed.layout.metrics.inkBox=bounds;}
                }
            }
        }
        dockProjectionLabel(placed,doc,a);
        result[a.id]=std::move(placed);
    }
    if(std::getenv("MOLECULE_STUDIO_PERF_TRACE")){const double tEnd=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();std::fprintf(stderr,"[labels] symbols=%.1f bonds=%.1f atoms=%.1f n=%zu affected=%zu\n",tSymbols-tStart,tBonds-tSymbols,tEnd-tBonds,result.size(),affected?affected->size():size_t(0));}
    return result;
}
// Some ChemDraw documents place an independent Me label over a terminal
// carbon. It remains a separate object, but the bond must stop at its ink.
inline Labels clippingLabels(const Document& doc,const Labels& labels){
    Labels result=labels;std::vector<size_t> degree(doc.atoms().size(),0);for(const auto& b:doc.bonds().records())if(b.isAlive()){++degree[b.beginAtom];++degree[b.endAtom];}
    double cell=std::max(8.,doc.defaultTextSize());typedef std::pair<int,int> Cell;std::map<Cell,std::vector<ObjectId>> grid;
    for(const auto& label:labels)if(degree[label.first]==0){auto p=doc.atoms().get(label.first)->position;grid[Cell(int(floor(p.x/cell)),int(floor(p.y/cell)))].push_back(label.first);}
    for(const auto& atom:doc.atoms().records())if(atom.isAlive()&&degree[atom.id]==1&&!labels.count(atom.id)){
        Cell key(int(floor(atom.position.x/cell)),int(floor(atom.position.y/cell)));double best=cell*.55;ObjectId nearest=kInvalidObjectId;
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy){auto bucket=grid.find(Cell(key.first+dx,key.second+dy));if(bucket==grid.end())continue;for(auto id:bucket->second){double distance=length(atom.position-doc.atoms().get(id)->position);if(distance<best){best=distance;nearest=id;}}}
        if(nearest!=kInvalidObjectId)result[atom.id]=labels.find(nearest)->second;
    }return result;
}
inline bool interval(Vec2 a,Vec2 b,const Box& box,double* lo,double* hi){
    double low=0,high=1;Vec2 d=b-a;
    for(int k=0;k<2;k++){double p=k?a.y:a.x,v=k?d.y:d.x,l=k?box.min.y:box.min.x,h=k?box.max.y:box.max.x;
        if(std::abs(v)<1e-10){if(p<l||p>h)return false;continue;}
        double x=(l-p)/v,y=(h-p)/v;if(x>y)std::swap(x,y);low=std::max(low,x);high=std::min(high,y);if(low>high)return false;
    }*lo=low;*hi=high;return true;
}
// Intersect a closed contour with a half-plane using signed distances.
inline std::vector<Vec2> retainHalfPlane(const std::vector<Vec2>& polygon,Vec2 origin,Vec2 normal,double boundary,bool minimum){
    std::vector<Vec2> result;if(polygon.empty())return result;
    auto distance=[&](Vec2 vertex){double value=dot(vertex-origin,normal)-boundary;return minimum?value:-value;};
    Vec2 previous=polygon.back();double before=distance(previous);
    for(Vec2 current:polygon){double after=distance(current);
        if((before>=0)!=(after>=0))result.push_back(previous+(current-previous)*(before/(before-after)));
        if(after>=0)result.push_back(current);
        previous=current;before=after;
    }
    return result;
}
inline void dockProjectionLabel(Label& label,const Document& doc,const AtomRecord& atom){
    const auto& edges=doc.incidentBonds(atom.id);if(edges.size()!=1)return;
    const auto edge=doc.bonds().alive(edges.front());const auto other=edge->beginAtom==atom.id?edge->endAtom:edge->beginAtom;
    const int kind=projection_molecule::kind(doc,other);if(kind<0||kind>2)return;
    auto ray=atom.position-doc.atoms().alive(other)->position;const double span=length(ray);if(span<1e-8)return;
    ray=ray*(1/span);const Vec2 normal(-ray.y,ray.x);
    const double gap=setting(doc,"MarginWidth",1.6),corridor=gap+doc.defaultLineWidth()*.5;
    const bool axial=std::abs(ray.x)<1e-6||std::abs(ray.y)<1e-6;
    // In eclipsed Newman views each close pair owns one side of its angular
    // bisector. This prevents Cl/F and longer groups from reading as one label.
    Vec2 splitOrigin,splitNormal;bool split=false;
    if(kind==2)for(auto axis:doc.incidentBonds(other)){
        auto b=doc.bonds().alive(axis);auto core=b->beginAtom==other?b->endAtom:b->beginAtom;
        if(projection_molecule::kind(doc,core)!=2)continue;
        for(auto spoke:doc.incidentBonds(core)){auto e=doc.bonds().alive(spoke);auto partner=e->beginAtom==core?e->endAtom:e->beginAtom;if(partner==other)continue;
            auto direction=doc.atoms().alive(partner)->position-doc.atoms().alive(core)->position;double n=length(direction);if(n<1e-8)continue;direction=direction*(1/n);
            if(dot(direction,ray)<.94)continue;auto middle=direction+ray;middle=middle*(1/length(middle));splitNormal=Vec2(-middle.y,middle.x);if(dot(splitNormal,ray)<0)splitNormal=splitNormal*-1;
            splitOrigin=(atom.position+doc.atoms().alive(partner)->position)*.5;split=true;break;
        }if(split)break;
    }
    for(int pass=0;pass<(split?4:1);++pass){
    if(split){double side=1e100;for(const auto& glyph:label.layout.glyphs)for(const auto& contour:glyph.contours)for(auto p:contour)side=std::min(side,dot(p+label.layout.anchor-splitOrigin,splitNormal));if(side<gap*.5)label.layout.anchor=label.layout.anchor+splitNormal*(gap*.5-side);}
    double first=1e100;
    for(const auto& glyph:label.layout.glyphs)for(const auto& contour:glyph.contours){
        std::vector<Vec2> ink;for(auto p:contour)ink.push_back(p+label.layout.anchor);
        if(!axial){ink=retainHalfPlane(ink,atom.position,normal,-corridor,true);ink=retainHalfPlane(ink,atom.position,normal,corridor,false);}
        for(auto p:ink)first=std::min(first,dot(p-atom.position,ray));
    }
    if(first==1e100)break;
    // Match bond clipping's clearance exactly, rather than adding a percentage
    // of bond length plus font side bearings. Atom positions stay unchanged.
    label.layout.anchor=label.layout.anchor+ray*(gap-first);
    }
    Box ink;for(const auto& glyph:label.layout.glyphs)for(const auto& contour:glyph.contours)for(auto p:contour)ink.add(p+label.layout.anchor);
    if(!ink.empty()){label.box=ink;label.layout.metrics.inkBox=ink;}
}
inline void clip(bonds::BondRenderData& data,const BondRecord& bond,const Labels& labels,double margin,
                 const std::set<ObjectRef>* moving=0,Vec2 delta=Vec2()){
    Vec2 vector=data.axisEnd-data.axisBegin;double size=length(vector);if(size<1e-8)return;
    Vec2 direction=vector*(1/size),normal(-direction.y,direction.x);
    double lower=-1e100,upper=1e100,center=0,strokeWidth=.6;int straight=0;
    for(const auto& shape:data.shapes)if(shape.kind==bonds::BondShapeKind::Stroke&&shape.points.size()==2){center+=dot(shape.points.front()-data.axisBegin,normal);strokeWidth=std::max(strokeWidth,shape.strokeWidth);++straight;}
    if(straight)center/=straight;
    ObjectId ends[2]={bond.beginAtom,bond.endAtom};
    for(int end=0;end<2;++end){
        auto label=labels.find(ends[end]);if(label==labels.end())continue;
        Vec2 anchor=label->second.layout.anchor;
        if(moving&&moving->count(ObjectRef(ObjectKind::Atom,ends[end])))anchor=anchor+delta;
        bool found=false;double support=end?1e100:-1e100;
        double corridor=margin+strokeWidth*.5;
        bool axial=std::abs(direction.x)<1e-6||std::abs(direction.y)<1e-6;
        for(const auto& glyph:label->second.layout.glyphs)for(const auto& contour:glyph.contours){
            std::vector<Vec2> polygon;polygon.reserve(contour.size());for(Vec2 vertex:contour)polygon.push_back(vertex+anchor);
            if(!axial){polygon=retainHalfPlane(polygon,data.axisBegin,normal,center-corridor,true);polygon=retainHalfPlane(polygon,data.axisBegin,normal,center+corridor,false);}
            for(Vec2 vertex:polygon){double along=dot(vertex-data.axisBegin,direction);support=end?std::min(support,along):std::max(support,along);found=true;}
        }
        if(found){if(end)upper=support-margin;else lower=support+margin;data.clippedByLabel=true;}
    }
    for(auto& shape:data.shapes){
        if(shape.kind==bonds::BondShapeKind::Stroke&&shape.points.size()==2){
            double begin=dot(shape.points[0]-data.axisBegin,direction),finish=dot(shape.points[1]-data.axisBegin,direction);
            double from=std::max(begin,lower),to=std::min(finish,upper);
            if(to<=from)shape.points.clear();
            else{shape.points[0]=shape.points[0]+direction*(from-begin);shape.points[1]=shape.points[1]+direction*(to-finish);}
        }else if(shape.kind==bonds::BondShapeKind::FilledPolygon&&data.clippedByLabel){
            shape.outline=retainHalfPlane(shape.outline,data.axisBegin,direction,lower,true);
            shape.outline=retainHalfPlane(shape.outline,data.axisBegin,direction,upper,false);
        }
    }
    data.shapes.erase(std::remove_if(data.shapes.begin(),data.shapes.end(),[](const bonds::BondShape& shape){return (shape.kind==bonds::BondShapeKind::Stroke&&shape.points.empty())||(shape.kind==bonds::BondShapeKind::FilledPolygon&&shape.outline.size()<3);}),data.shapes.end());
}

} } }
#endif
