#ifndef STUDIO_NICKNAMES_HPP
#define STUDIO_NICKNAMES_HPP
#include "chem_completion.hpp"
#include "chem_italic.hpp"
#include "engine/atom_runs.hpp"
#include "engine/compact_radicals.hpp"
#include "engine/diazo.hpp"
namespace nicknames {
inline Error applyWholeIon(Document& d,ObjectId id,const compounddata::Entry& ion){
    const auto* atom=d.atoms().alive(id);if(!atom)return Error(ErrorCode::NotFound,"原子不存在");
    auto sem=atom->semantics;sem.nodeType=uint8_t(NodeType::Nickname);sem.showLabel=true;sem.aromatic=false;
    sem.charge=compounddata::ionCharge(&ion);sem.radical=sem.isotope=0;sem.implicitHydrogens=0;
    auto e=d.setElement(id,6);if(e.ok())e=d.setAtomSemantics(id,sem);
    const auto body=compounddata::ionBody(ion);
    if(e.ok())e=d.setAtomLabel(id,body);
    TextRun base;base.utf8=body;base.flags=kFormula;
    studioatom::set(d,id,{base});
    auto& bag=d.extensions(ObjectRef(ObjectKind::Atom,id));
    for(auto key:{"studio.nickname.fragment","studio.mechanism.ion.base","studio.mechanism.compact.valence","studio.hydrogens.manual","studio.query.description","studio.label.text","studio.label.offset"})bag.erase(key);
    bag.set("studio.compound.fragment",ion.fragment);
    if(auto residue=nickdata::find(body))bag.set("studio.nickname.fragment",residue->fragment);
    return e;
}
inline Error apply(Document& d,ObjectId id,const std::string& label){if(studio::diazo::requested(label)&&studio::diazo::eligible(d,id))return studio::diazo::append(d,id,studio::diazo::axis(d,id),studio::diazo::externalValence(d,id));auto atom=d.atoms().alive(id);if(!atom)return Error(ErrorCode::NotFound,"原子不存在");if(studio::compact_radicals::find(label))return studio::compact_radicals::apply(d,id,label);auto n=nickdata::find(label);auto a=nickdata::findAlias(label);auto custom=customgroups::find(label);auto compound=compounddata::find(label);
    if(compounddata::polyatomicIon(compound)&&d.incidentBonds(id).empty())return applyWholeIon(d,id,*compound);
    if(compound&&(!n||d.incidentBonds(id).empty())){
        auto sem=atom->semantics;sem.nodeType=uint8_t(NodeType::Nickname);sem.showLabel=true;sem.aromatic=false;sem.isotope=0;sem.charge=sem.radical=0;sem.implicitHydrogens=0;
        const bool phosphine=std::string(compound->formula)=="C18H15P";if(phosphine)sem.nodeType=uint8_t(NodeType::Element);
        const auto* hydride=compounddata::hydride(label);
        auto e=d.setElement(id,hydride?hydride->element:phosphine?15:6);if(e.ok())e=d.setAtomSemantics(id,sem);if(e.ok())e=d.setAtomLabel(id,label);
        auto& ext=d.extensions(ObjectRef(ObjectKind::Atom,id));ext.erase("studio.nickname.fragment");ext.erase("studio.hydrogens.manual");ext.erase("studio.query.description");ext.set("studio.compound.fragment",compound->fragment);return e;
    }
    if(!n&&!a&&!custom)return Error(ErrorCode::Unsupported,"未知缩写");const auto oldLabel=d.atomLabel(id);auto sem=atom->semantics;sem.nodeType=uint8_t(NodeType::Nickname);sem.showLabel=true;sem.aromatic=false;sem.isotope=0;sem.implicitHydrogens=-1;auto e=d.setElement(id,6);if(e.ok())e=d.setAtomSemantics(id,sem);if(e.ok())e=d.setAtomLabel(id,label);auto& ext=d.extensions(ObjectRef(ObjectKind::Atom,id));ext.erase("studio.compound.fragment");ext.erase("studio.nickname.fragment");ext.erase("studio.hydrogens.manual");ext.erase("studio.query.description");if(n)ext.set("studio.nickname.fragment",n->fragment);else if(a)ext.set("studio.nickname.fragment",a->fragment);else ext.set("studio.nickname.fragment",custom->fragment);
    if(e.ok()&&chemcompletion::formulaLayout()&&label.find('-')!=std::string::npos&&(oldLabel!=label||studioatom::runs(d,id).empty())){
        const auto italic=chemitalic::maskUtf8(label);
        std::vector<TextRun> runs;
        for(size_t i=0;i<label.size()&&i<italic.size();++i){
            const uint16_t flags=italic[i]?kItalic:0;
            if(!runs.empty()&&runs.back().flags==flags)runs.back().utf8+=label[i];
            else {TextRun run;run.flags=flags;run.utf8=label.substr(i,1);runs.push_back(run);}
        }
        if(runs.size()>1)studioatom::set(d,id,runs);
    }
    return e;}
inline Command labelCommand(ObjectId id,const std::string& label){if(studio::diazo::requested(label))return Command("输入重氮基",kInvalidateStructure|kInvalidateGeometry|kInvalidateSemantics|kInvalidateAppearance,[id,label](CommandContext& c){auto e=apply(c.document,id,label);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);});if(!nickdata::find(label)&&!nickdata::findAlias(label)&&!customgroups::find(label)&&!compounddata::find(label)){auto base=chemistry::setLabelTextCommand(id,label,{},nullptr);return Command("原子标签",kInvalidateStructure|kInvalidateAppearance|kInvalidateSemantics,[id,base](CommandContext& c)mutable{auto result=base.execute(c);if(result.ok()){auto& bag=c.document.extensions(ObjectRef(ObjectKind::Atom,id));bag.erase("studio.nickname.fragment");bag.erase("studio.compound.fragment");bag.erase("studio.hydrogens.manual");bag.erase("studio.query.description");}return result;});}return Command("缩写标签",kInvalidateAppearance|kInvalidateSemantics,[id,label](CommandContext& c){auto e=apply(c.document,id,label);return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);});}
}
namespace customgroups {
inline void saveSelection(Session& s,const std::string& name){
 if(name.empty()||name.size()>24||name[0]<'A'||(name[0]>'Z'&&name[0]<'a')||name[0]>'z')throw std::runtime_error("缩写须以英文字母开头，最多24字符");for(char c:name)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))throw std::runtime_error("缩写仅支持字母、数字与连字符");
 auto k=chemcompletion::key(name);load();if(!readError().empty())throw std::runtime_error(readError());for(auto& e:entries())if(chemcompletion::key(e.name)==k)throw std::runtime_error("缩写与已有官能团冲突");if(chemcompletion::protectedKey(k)||find(name)||chemcompletion::state().custom.count(k))throw std::runtime_error("缩写与元素或已有自定义项冲突");for(auto& e:nickdata::nicknames)if(chemcompletion::key(e.name)==k)throw std::runtime_error("缩写与内置官能团冲突");
 auto refs=studioio::expanded(s.doc,s.selected);std::set<ObjectId> atoms;for(auto r:refs){if(r.kind==ObjectKind::Atom)atoms.insert(r.id);else if(r.kind==ObjectKind::Bond){auto b=s.doc.bonds().alive(r.id);if(b){atoms.insert(b->beginAtom);atoms.insert(b->endAtom);}}else throw std::runtime_error("请选择纯结构，不能包含文字或图形");}
 if(atoms.size()<2||atoms.size()>500)throw std::runtime_error("请框选完整官能团及一个末端R连接点");ObjectId marker=kInvalidObjectId;int count=0;
 for(auto id:atoms)if(s.doc.atomLabel(id)=="R"){marker=id;++count;}
 if(count!=1)throw std::runtime_error("选区须恰好包含一个标为R的原子");
 for(auto& b:s.doc.bonds().records())if(b.isAlive()&&(atoms.count(b.beginAtom)!=atoms.count(b.endAtom)))throw std::runtime_error("选区切断了化学键，请选中完整官能团");
 auto bonds=s.doc.incidentBonds(marker);if(bonds.size()!=1||s.doc.bonds().get(bonds[0])->order!=BondOrder::Single)throw std::runtime_error("R须在末端，仅以一根单键连接官能团");
 std::set<ObjectId> seen;std::vector<ObjectId> queue{marker};while(!queue.empty()){auto id=queue.back();queue.pop_back();if(!seen.insert(id).second)continue;for(auto bid:s.doc.incidentBonds(id)){auto b=s.doc.bonds().alive(bid);queue.push_back(b->beginAtom==id?b->endAtom:b->beginAtom);}}if(seen!=atoms)throw std::runtime_error("选区须为一个连通的官能团");
 std::vector<ObjectRef> complete;for(auto id:atoms)complete.push_back(ObjectRef(ObjectKind::Atom,id));for(auto& b:s.doc.bonds().records())if(b.isAlive()&&atoms.count(b.beginAtom)&&atoms.count(b.endAtom))complete.push_back(ObjectRef(ObjectKind::Bond,b.id));Document copy;auto err=studioio::append(s.doc,copy,complete,Vec2(),nullptr);if(!err.ok())throw std::runtime_error(err.message);
 std::ostringstream xml;err=persistence::writeCdxml(copy,xml);if(!err.ok())throw std::runtime_error(err.message);tinyxml2::XMLDocument doc;doc.Parse(xml.str().c_str());auto page=doc.RootElement()->FirstChildElement("page");auto f=page?page->FirstChildElement("fragment"):nullptr;if(!f)throw std::runtime_error("无法生成官能团结构");bool found=false;for(auto n=f->FirstChildElement("n");n;n=n->NextSiblingElement("n")){auto t=n->FirstChildElement("t");auto run=t?t->FirstChildElement("s"):nullptr;if(run&&run->GetText()&&std::string(run->GetText())=="R"){n->SetAttribute("NodeType","ExternalConnectionPoint");n->DeleteAttribute("Element");n->DeleteAttribute("NumHydrogens");n->DeleteChildren();found=true;}}if(!found)throw std::runtime_error("无法识别R连接点");tinyxml2::XMLPrinter printer;f->Accept(&printer);auto v=entries();v.push_back({name,printer.CStr()});persist(v);++chemcompletion::state().revision;
}
}
#endif
