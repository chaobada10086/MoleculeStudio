#pragma once
#include "next/core/document.hpp"
#include "next/core/commands.hpp"
#include "next/chemistry/valence.hpp"
#include "mechanism_roles.hpp"
#include "ion_labels.hpp"
#include "molecular_render.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace r62 { namespace mechanism {
inline bool triphenylphosphineLabel(const std::string& label){
    return label=="PPh3"||label=="Ph3P"||label=="PPh₃"||label=="Ph₃P";
}
inline const text::GlyphPlacement* triphenylphosphineGlyph(const std::string& label,const text::RichLayout& layout){
    if(!triphenylphosphineLabel(label))return nullptr;
    const text::GlyphPlacement* phosphorus=nullptr;
    for(const auto& glyph:layout.glyphs)if(glyph.codepoint=='P'){
        phosphorus=&glyph;
        if(label.front()=='P'&&label[1]=='P')break;
    }
    return phosphorus;
}
inline core::Vec2 triphenylphosphineCenter(const std::string& label,const text::RichLayout* layout,core::Vec2 fallback){
    if(!layout)return fallback;
    const auto* phosphorus=triphenylphosphineGlyph(label,*layout);
    if(!phosphorus)return fallback;
    core::Box ink;
    bool found=false;
    for(const auto& contour:phosphorus->contours)for(auto point:contour){
        if(found)ink.add(point);else{ink=core::Box(point,point);found=true;}
    }
    return found?layout->anchor+(ink.min+ink.max)*.5:fallback;
}
inline const char* lonePairKey(){return "pst.attr.MoleculeStudioLonePairs";}
struct MainGroupPairModel {
    int electrons;
    std::array<int,4> neutralValences;
};
inline MainGroupPairModel mainGroupPairModel(int element){
    switch(element){
    case 6:case 14:case 32: return {4,{{4,0,0,0}}};  // C, Si, Ge
    case 50:case 82: return {4,{{2,4,0,0}}};        // Sn, Pb
    case 7: return {5,{{3,0,0,0}}};
    case 15:case 33:case 51:case 83: return {5,{{3,5,0,0}}};  // P, As, Sb, Bi
    case 8: return {6,{{2,0,0,0}}};
    case 16:case 34:case 52: return {6,{{2,4,6,0}}};  // S, Se, Te
    case 9: return {7,{{1,0,0,0}}};
    case 17:case 35:case 53:case 85: return {7,{{1,3,5,7}}};
    default: return {0,{{0,0,0,0}}};
    }
}
// Whole-molecule abbreviations have a donor inside the label, rather than
// at a carbon placeholder. Keep this separate from ion_labels: adding a lone
// pair must not rewrite EtOH as hydroxide or discard its carbon skeleton.
inline const studioions::Entry* compactLonePairGroup(const std::string& label){
    static const studioions::Entry groups[]={
        {"MeOH","meoh",8,1,1},{"EtOH","etoh",8,1,1},
        {"nPrOH","nproh",8,1,1},{"iPrOH","iproh",8,1,1},
        {"nBuOH","nbuoh",8,1,1},{"iBuOH","ibuoh",8,1,1},
        {"sBuOH","sbuoh",8,1,1},{"tBuOH","tbuoh",8,1,1},
        {"BnOH","bnoh phch2oh",8,1,1},{"PhOH","phoh",8,1,1},
        {"CF3CH2OH","cf3ch2oh",8,1,1},
        {"Et2O","et2o",8,0,2},
        {"MeNH2","menh2",7,2,1},{"EtNH2","etnh2",7,2,1},
        {"nPrNH2","nprnh2",7,2,1},{"iPrNH2","iprnh2",7,2,1},
        {"nBuNH2","nbunh2",7,2,1},{"tBuNH2","tbunh2",7,2,1},
        {"PhNH2","phnh2",7,2,1},{"BnNH2","bnnh2",7,2,1},
        {"Me2NH","me2nh hnme2",7,1,2},{"Et2NH","et2nh hnet2",7,1,2},
        {"Bn2NH","bn2nh",7,1,2},
        {"Me3N","me3n nme3",7,0,3},{"Et3N","et3n net3",7,0,3},
        {"Bu3N","bu3n nbu3",7,0,3},
        {"MeSH","mesh",16,1,1},{"EtSH","etsh",16,1,1},
        {"PhSH","phsh",16,1,1},{"Me2S","me2s",16,0,2},{"Et2S","et2s",16,0,2}
    };
    auto key=studioions::ascii(label);
    for(auto& ch:key)if(ch>='A'&&ch<='Z')ch=char(ch+32);
    for(const auto& group:groups)
        if((std::string(" ")+group.aliases+" ").find(" "+key+" ")!=std::string::npos)return &group;
    return nullptr;
}
inline const text::GlyphPlacement* compactLonePairGlyph(const std::string& label,const text::RichLayout& layout){
    const auto* group=compactLonePairGroup(label);
    if(!group)return nullptr;
    const auto symbol=core::elementSymbol(group->element);
    for(const auto& glyph:layout.glyphs)if(glyph.codepoint==uint32_t(symbol[0]))return &glyph;
    return nullptr;
}
inline core::Vec2 compactLonePairCenter(const std::string& label,const text::RichLayout& layout,core::Vec2 fallback){
    const auto* glyph=compactLonePairGlyph(label,layout);if(!glyph)return fallback;
    core::Box ink;bool found=false;
    for(const auto& contour:glyph->contours)for(auto point:contour){
        if(found)ink.add(point);else{ink=core::Box(point,point);found=true;}
    }
    return found?layout.anchor+(ink.min+ink.max)*.5:fallback;
}
// Older/compact OH, HO, NH2, OMe... nodes may still be stored as carbon
// nicknames. Resolve their actual attachment atom without expanding the group.
inline const studioions::Entry* lonePairGroup(const std::string& label){
    if(auto group=studioions::find(label))return group;
    if(auto group=compactLonePairGroup(label))return group;
    static const studioions::Entry diphenylphosphino={"PPh2","",15,0,2};
    if(studioions::ascii(label)=="PPh2")return &diphenylphosphino;
    static const studioions::Entry hydrides[]={
        {"H2O","",8,2,0},{"NH3","",7,3,0},{"H2S","",16,2,0},{"PH3","",15,3,0}
    };
    const auto normalized=studioions::ascii(label);
    for(const auto& group:hydrides)if(normalized==group.label)return &group;
    for(const auto& group:hydrides)if(normalized==std::string(r62::core::elementSymbol(group.element))+"H"+std::to_string(group.hydrogens))return &group;
    return nullptr;
}
inline core::AtomRecord lonePairHost(const core::Document& doc,const core::AtomRecord& original,int* hidden){
    auto atom=original;
    *hidden=studioions::hiddenValence(doc,atom.id);
    if(const auto* group=lonePairGroup(doc.atomLabel(atom.id))){
        atom.element=group->element;
        atom.semantics.nodeType=uint8_t(core::NodeType::Element);
        atom.semantics.implicitHydrogens=group->hydrogens;
        *hidden=group->hidden;
    }
    return atom;
}
inline int lonePairCapacity(const core::Document& doc,core::ObjectId id){
    const auto* original=doc.atoms().alive(id);
    if(!original)return 0;
    int hidden=0;const auto resolved=lonePairHost(doc,*original,&hidden);
    const auto* atom=&resolved;
    if(triphenylphosphineLabel(doc.atomLabel(id)))
        return !atom->semantics.charge&&!atom->semantics.radical&&doc.incidentBonds(id).empty()?1:0;
    const auto role=genericRole(doc,id);
    if(!role.empty())return genericRoleValence(role,atom->semantics.charge)==0?1:0;
    if(atom->semantics.nodeType!=uint8_t(core::NodeType::Element)||atom->semantics.aromatic)return 0;
    const auto model=mainGroupPairModel(atom->element);
    const int charge=atom->semantics.charge;
    if(!model.electrons||charge<-1||charge>1)return 0;
    int bonds=hidden;
    for(auto id:doc.incidentBonds(atom->id)){
        const auto* bond=doc.bonds().alive(id);
        if(!bond||bond->semantics!=core::BondSemantics::Normal||int(bond->order)<1||int(bond->order)>3)return 0;
        bonds+=int(bond->order);
    }
    const int radical=atom->semantics.radical==uint8_t(core::Radical::Doublet)?1:atom->semantics.radical?2:0;
    const int hydrogen=atom->semantics.implicitHydrogens;
    if(atom->element==6&&!charge&&atom->semantics.radical==uint8_t(core::Radical::Singlet))
        return bonds+std::max(0,hydrogen)==2?1:0;
    for(int neutral:model.neutralValences){
        if(!neutral)break;
        const int valence=neutral+(model.electrons==4?-std::abs(charge):charge)-radical;
        if(bonds>valence||(hydrogen>=0&&bonds+hydrogen>valence))continue;
        const int electrons=model.electrons-charge-valence-radical;
        return electrons>=0?electrons/2:0;
    }
    return 0;
}
inline core::ObjectId lonePairTarget(const core::Document& doc,core::ObjectId id){
    const auto* atom=doc.atoms().alive(id);
    if(!atom||atom->element!=1||atom->semantics.charge||atom->semantics.radical)return id;
    const auto& links=doc.incidentBonds(id);
    if(links.size()!=1)return id;
    const auto* bond=doc.bonds().alive(links.front());
    if(!bond||bond->order!=core::BondOrder::Single||bond->semantics!=core::BondSemantics::Normal)return id;
    const auto host=bond->beginAtom==id?bond->endAtom:bond->beginAtom;
    // Pulling a condensed H into an explicit bond selects the new H. The
    // attached donor remains the lone-pair host, regardless of H coordinates.
    // Resolve only through that exact bond; free H+/H and unrelated atoms stay
    // their own targets.
    return lonePairCapacity(doc,host)>0?host:id;
}
inline std::vector<core::ObjectId> lonePairTargets(const core::Document& doc,const std::vector<core::ObjectId>& ids){
    std::vector<core::ObjectId> result;
    for(auto id:ids)result.push_back(lonePairTarget(doc,id));
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
inline bool hasPrintedLonePair(const core::Document& doc,core::ObjectId id){
    auto atom=doc.atoms().alive(id);
    return atom&&!atom->semantics.charge&&doc.atomLabel(id)=="Nu:"&&genericRole(doc,id)=="nucleophile-neutral";
}
inline int lonePairCount(const core::Document& doc,core::ObjectId id){
    auto value=doc.extensions(core::ObjectRef(core::ObjectKind::Atom,id)).findValue(lonePairKey());
    if(value)return std::max(0,std::min(lonePairCapacity(doc,id),std::atoi(value->c_str())));
    auto role=genericRole(doc,id);
    if((role=="nucleophile"||role=="nucleophile-neutral")&&lonePairCapacity(doc,id)==1)return 1;
    return 0;
}
inline const char* lonePairPositionsKey(){return "pst.attr.MoleculeStudioLonePairPositions";}
inline std::map<int,core::Vec2> lonePairOffsets(const core::Document& doc,core::ObjectId id){
    std::map<int,core::Vec2> offsets;
    auto value=doc.extensions(core::ObjectRef(core::ObjectKind::Atom,id)).findValue(lonePairPositionsKey());
    if(value){
        std::istringstream in(*value);
        int index;core::Vec2 offset;
        while(in>>index>>offset.x>>offset.y)if(index>=0&&index<4&&offset.finite())offsets[index]=offset;
    }
    return offsets;
}
inline void setLonePairOffset(core::Document& doc,core::ObjectId id,int index,core::Vec2 offset){
    auto offsets=lonePairOffsets(doc,id);offsets[index]=offset;
    std::ostringstream out;out.precision(12);
    for(const auto& entry:offsets)out<<entry.first<<' '<<entry.second.x<<' '<<entry.second.y<<' ';
    doc.extensions(core::ObjectRef(core::ObjectKind::Atom,id)).set(lonePairPositionsKey(),out.str());
}
inline core::Vec2 lonePairGlyphCenter(const core::Document& doc,const core::AtomRecord& atom,const text::RichLayout* layout){
    if(!layout)return atom.position;auto label=doc.atomLabel(atom.id);if(triphenylphosphineLabel(label))return triphenylphosphineCenter(label,layout,atom.position);
    int hidden=0;auto host=lonePairHost(doc,atom,&hidden);std::string symbol=core::elementSymbol(host.element);double best=1e100;auto center=atom.position;
    for(size_t i=0;i+symbol.size()<=layout->glyphs.size();++i){bool matches=true;for(size_t j=0;j<symbol.size();++j)matches&=layout->glyphs[i+j].codepoint==uint32_t(symbol[j]);if(!matches)continue;core::Box box;bool ink=false;for(size_t j=0;j<symbol.size();++j)for(const auto& contour:layout->glyphs[i+j].contours)for(auto p:contour){p=p+layout->anchor;if(ink)box.add(p);else{box=core::Box(p,p);ink=true;}}if(ink){auto p=(box.min+box.max)*.5;double distance=core::length(p-atom.position);if(distance<best){best=distance;center=p;}}}
    return center;
}
inline core::Vec2 lonePairDragHost(const core::Document& doc,const core::AtomRecord& atom,const text::RichLayout* layout){
    const auto label=doc.atomLabel(atom.id);
    return compactLonePairGroup(label)||triphenylphosphineLabel(label)?lonePairGlyphCenter(doc,atom,layout):atom.position;
}
inline core::Vec2 lonePairPosition(const core::Document& doc,core::ObjectId id,int index,const text::RichLayout* labelLayout=nullptr){
    auto a=doc.atoms().alive(id);if(!a)return core::Vec2();
    using namespace integration::molecular;
    const double em=setting(doc,"LabelSize",10);
    const auto labelText=doc.atomLabel(id);
    const auto center=lonePairGlyphCenter(doc,*a,labelLayout);
    const auto offsets=lonePairOffsets(doc,id);
    auto moved=offsets.find(index);
    if(moved!=offsets.end())return lonePairDragHost(doc,*a,labelLayout)+boundedElectronOffset(moved->second*em,em);
    if(triphenylphosphineLabel(labelText))return center+core::Vec2(0,-em*.70);
    auto role=genericRole(doc,id);
    if(role=="nucleophile"||role=="nucleophile-neutral")return a->position+core::Vec2(em*.95,em*.12);
    double best=-1,angle=-1.5707963267948966;std::vector<double> used;
    for(auto bid:doc.incidentBonds(id)){auto b=doc.bonds().alive(bid);auto other=doc.atoms().alive(b->beginAtom==id?b->endAtom:b->beginAtom);if(other)used.push_back(std::atan2(other->position.y-a->position.y,other->position.x-a->position.x));}
    if(labelLayout)for(const auto& g:labelLayout->glyphs)if(g.codepoint=='H'||(compactLonePairGroup(labelText)&&&g!=compactLonePairGlyph(labelText,*labelLayout))){core::Box box;bool ink=false;for(const auto& contour:g.contours)for(auto p:contour){p=p+labelLayout->anchor;if(ink)box.add(p);else{box=core::Box(p,p);ink=true;}}if(ink){auto delta=(box.min+box.max)*.5-center;if(core::length(delta)>em*.2)used.push_back(std::atan2(delta.y,delta.x));}}
    for(int n=0;n<=index;++n){best=-1;for(int k=0;k<24;++k){double t=-1.5707963267948966+k*6.283185307179586/24,score=3.141592653589793;for(double u:used)score=std::min(score,std::acos(std::cos(t-u)));if(score>best+1e-8){best=score;angle=t;}}used.push_back(angle);}
    bool label=a->element!=6||a->semantics.showLabel||!doc.atomLabel(id).empty()||a->semantics.isotope;double radius=doc.defaultTextSize()*(label?.70:.48);
    return center+core::Vec2(std::cos(angle),std::sin(angle))*radius;
}
inline double lonePairDotRadius(double em){return std::max(.45,em*.075);}
inline std::array<core::Vec2,2> lonePairDotsAt(core::Vec2 center,core::Vec2 atom,double em){
    const auto radial=center-atom;
    const double length=core::length(radial);
    const auto tangent=length>1e-8?core::Vec2(-radial.y/length,radial.x/length):core::Vec2(1,0);
    const auto spacing=tangent*(lonePairDotRadius(em)*1.8);
    return {{center-spacing,center+spacing}};
}
inline std::array<core::Vec2,2> lonePairDots(const core::Document& doc,const core::AtomRecord& atom,int index,double em,
                                             const text::RichLayout* labelLayout=nullptr){
    const auto center=lonePairGlyphCenter(doc,atom,labelLayout);
    return lonePairDotsAt(lonePairPosition(doc,atom.id,index,labelLayout),center,em);
}
inline core::Command setLonePairCommand(const std::vector<core::ObjectId>& ids){return core::Command("添加原子孤对电子",core::kInvalidateAppearance|core::kInvalidateSemantics,[ids](core::CommandContext& c){
    if(ids.empty())return core::CommandResult::fail(core::ErrorCode::InvalidArgument,"请先选中原子。");
    const auto targets=lonePairTargets(c.document,ids);
    for(auto id:targets)if(lonePairCapacity(c.document,id)<1)return core::CommandResult::fail(core::ErrorCode::InvalidArgument,"所选原子当前价态没有可显示的孤对电子；请先设置正确电荷和键。");
    for(auto id:targets){
        auto& doc=c.document;int hidden=0;
        const auto host=lonePairHost(doc,*doc.atoms().alive(id),&hidden);
        auto e=doc.setElement(id,host.element);if(e.ok())e=doc.setAtomSemantics(id,host.semantics);
        if(!e.ok())return core::CommandResult::fail(e.code,e.message);
        auto& bag=doc.extensions(core::ObjectRef(core::ObjectKind::Atom,id));
        if(const auto* group=lonePairGroup(doc.atomLabel(id))){
            // Keep the authored label, typography and coordinates. Only repair
            // the chemical identity used by later arrow capture/replay.
            bag.set("studio.mechanism.compact.valence",std::to_string(hidden));
            if(hidden)bag.set("studio.mechanism.ion.base",group->label);
            else bag.erase("studio.nickname.fragment");
        }
        int count=std::min(lonePairCapacity(doc,id),lonePairCount(doc,id)+1);
        bag.set(lonePairKey(),std::to_string(count));
    }
    return core::CommandResult::success();});}
} }
