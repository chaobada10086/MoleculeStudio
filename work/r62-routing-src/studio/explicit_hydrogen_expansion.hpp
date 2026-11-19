#pragma once
#include "engine/hydride_labels.hpp"
#include "engine/atom_runs.hpp"
#include "engine/sprout_layout.hpp"
#include "engine/mechanism_step.hpp"
#include "next/core/commands.hpp"
#include <memory>
#include <array>
#include <functional>
namespace studio { namespace explicit_hydrogens {
using namespace r62;using namespace core;
struct Plan {ObjectId atom;int element,count;};
inline bool plan(const Document& doc,ObjectId id,Plan* out){
    auto a=doc.atoms().alive(id);if(!a)return false;if(doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.compound.saved"))return false;const auto type=static_cast<NodeType>(a->semantics.nodeType);if(type!=NodeType::Element&&type!=NodeType::Nickname&&type!=NodeType::Fragment)return false;if(type==NodeType::Element&&a->element<=1)return false;int element=a->element;auto label=doc.atomLabel(id);if(studio::hydride_labels::legacyHydroxyl(*a,label))element=8;
    if(type!=NodeType::Element&&studio::hydride_labels::count(element,label)<1)for(int z=2;z<=118;++z)if(studio::hydride_labels::count(z,label)>0){element=z;break;}
    int textH=studio::hydride_labels::count(element,label);if(element==6&&label=="Me")textH=3;
    if(!label.empty()&&label!=elementSymbol(element)&&textH<0)return false;
    if(a->semantics.nodeType!=uint8_t(NodeType::Element)&&textH<1)return false;
    // Never turn an abbreviation with hidden heavy atoms into a bare element.
    for(auto key:{"studio.nickname.fragment","studio.compound.fragment"})if(auto raw=doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue(key)){
        tinyxml2::XMLDocument xml;if(xml.Parse(raw->c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement())return false;int heavy=0;bool valid=true;
        std::function<void(tinyxml2::XMLElement*)> scan=[&](tinyxml2::XMLElement* node){for(auto n=node->FirstChildElement();n;n=n->NextSiblingElement()){if(std::string(n->Name())=="n"){auto type=n->Attribute("NodeType");if(type&&std::string(type)=="ExternalConnectionPoint")continue;int e=n->IntAttribute("Element",6);if(e!=1){++heavy;valid&=e==element;}}scan(n);}};scan(xml.RootElement());if(!valid||heavy!=1)return false;
    }
    int count=textH>=0?textH:(a->semantics.implicitHydrogens>=0?int(a->semantics.implicitHydrogens):chemistry::refreshAtomDerived(doc,id,{}).newHydrogenCount);
    if(count<1||count>8)return false;if(out)*out={id,element,count};return true;
}
inline Command command(const std::vector<Plan>& plans,const std::shared_ptr<std::vector<ObjectRef>>& made){
    return Command("显式氢展开",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance|kInvalidateSemantics,[plans,made](CommandContext& ctx){auto& doc=ctx.document;made->clear();
        for(auto p:plans){auto a=doc.atoms().alive(p.atom);if(!a)return CommandResult::fail(ErrorCode::NotFound,"所选含氢原子已不存在");auto position=a->position;auto sem=a->semantics;
            std::vector<ObjectId> incident=doc.incidentBonds(p.atom);std::array<Vec2,4> points{};std::array<int,4> depths{};double originalVolume=0;
            bool stereo=p.count==1&&incident.size()==3;for(size_t i=0;i<incident.size()&&i<3;++i){auto b=doc.bonds().alive(incident[i]);auto other=b->beginAtom==p.atom?b->endAtom:b->beginAtom;points[i]=doc.atoms().alive(other)->position;depths[i]=b->beginAtom==p.atom?mechanism::step_detail::wedgeDepth(*b):0;if(doc.atoms().alive(other)->element==1)stereo=false;}
            points[3]=position;if(stereo)originalVolume=mechanism::step_detail::tetrahedralVolume(points,depths);stereo&=std::abs(originalVolume)>1e-6;
            auto e=doc.setElement(p.atom,p.element);sem.nodeType=uint8_t(NodeType::Element);sem.implicitHydrogens=0;sem.showLabel=true;if(e.ok())e=doc.setAtomSemantics(p.atom,sem);if(e.ok())e=doc.setAtomLabel(p.atom,elementSymbol(p.element));if(!e.ok())return CommandResult::fail(e.code,e.message);studioatom::set(doc,p.atom,{});
            auto& bag=doc.extensions(ObjectRef(ObjectKind::Atom,p.atom));for(auto key:{"studio.nickname.fragment","studio.compound.fragment","studio.label.text","pst.attr.MoleculeStudioLiteralLabel","cdxml.attr.r62ChemLabel","pst.attr.NumHydrogens","studio.atom.z","pst.attr.xyz"})bag.erase(key);bag.set("studio.hydrogens.manual","1");
            for(int i=0;i<p.count;++i){auto direction=sprout_layout::resolve(doc,p.atom);if(!direction.ok)return CommandResult::fail(ErrorCode::InvalidGeometry,"氢原子附近空间不足，请先拉开结构后再展开");ObjectId hydrogen,bond;e=doc.addAtom(position+core::direction(direction.directionDeg)*doc.defaultBondLength(),1,&hydrogen);if(e.ok()){auto h=doc.atoms().alive(hydrogen)->semantics;h.implicitHydrogens=0;h.showLabel=true;e=doc.setAtomSemantics(hydrogen,h);}if(e.ok())e=doc.addBond(p.atom,hydrogen,&bond);if(!e.ok())return CommandResult::fail(e.code,e.message);
                if(stereo){points[3]=doc.atoms().alive(hydrogen)->position;auto projected=mechanism::step_detail::tetrahedralVolume(points,depths);if(projected*originalVolume<=1e-6){std::array<int,4> single{};single[3]=1;auto volume=mechanism::step_detail::tetrahedralVolume(points,single);if(std::abs(volume)<1e-6)return CommandResult::fail(ErrorCode::InvalidGeometry,"无法保持该原子的立体构型，请调整附近位置后重试");for(auto id:incident){auto b=doc.bonds().alive(id);if(b->beginAtom==p.atom&&mechanism::step_detail::wedgeDepth(*b)){doc.setBondDisplay(id,BondDisplay::Normal);doc.setBondStereo(id,BondStereo::None);}}e=doc.setBondDisplay(bond,volume*originalVolume>0?BondDisplay::Wedge:BondDisplay::WedgeHash);if(!e.ok())return CommandResult::fail(e.code,e.message);}}
                made->emplace_back(ObjectKind::Atom,hydrogen);made->emplace_back(ObjectKind::Bond,bond);
            }
        }
        return CommandResult::success();
    });
}
} }
