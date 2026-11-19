#pragma once
#include "diels_alder_checks.hpp"
#include "expansion_cleanup_checks.hpp"
#include "visible_cleanup_checks.hpp"
#include "explicit_hydrogen_expansion_checks.hpp"
#include "generated_step_cleanup_checks.hpp"
#include "generated_ring_cleanup_checks.hpp"
#include "branch_clean_checks.hpp"
#include "current_fishhook_checks.hpp"
#include "base_byproduct_checks.hpp"
#include "reciprocal_arrow_checks.hpp"
#include "page_flow_checks.hpp"
#include "mass_layout_checks.hpp"
#include "halogen_substitution_checks.hpp"
#include "generated_spacing_checks.hpp"
#include "fishhook_beta_checks.hpp"
#include "user_hydrazine_checks.hpp"
#include "mechanism_textbook_checks.hpp"
#include "reagent_mechanism_checks.hpp"
#include "expanded_reagent_checks.hpp"
#include "virtual_bond_checks.hpp"
#include "carbon_fragment_checks.hpp"
#include "methanol_mechanism_checks.hpp"
#include "fused_carbene_checks.hpp"
#include "mechanism_follow_checks.hpp"
#include "compact_reagent_checks.hpp"
// Acceptance fixture: acetone enolate + acetone -> aldolate, driven by three
// electron-pair arrows. Expected connectivity is asserted independently below.
void App::mechanismStepCheck(const std::string& dir){
    {
        Session manual;manual.doc.setDefaultBondLength(30);
        const char* command="chooseMultipleBondsTool_DoubleEitherBond";
        bool onHome=false;
        for(const auto& group:makeTools())for(const auto& item:group.items)
            if(group.name=="化学键"&&item.cmd==command)onHome=true;
        if(!onHome||!manual.activateCommand(command))throw std::runtime_error("missing Home crossed double bond");
        manual.gesture(PointerPhase::Down,{0,0});manual.gesture(PointerPhase::Move,{30,0});manual.gesture(PointerPhase::Up,{30,0});
        auto check=[](const Session& session){
            const auto* b=session.doc.bonds().alive(0);
            if(!b||b->order!=BondOrder::Double||b->stereo!=BondStereo::Either)throw std::runtime_error("manual crossed bond lost");
        };
        check(manual);const auto path=dir+"/manual-crossed.cdxml";
        if(!manual.save(path,true).ok())throw std::runtime_error("save manual crossed bond");
        Session reopened;if(!reopened.open(path).ok())throw std::runtime_error("open manual crossed bond");check(reopened);
    }
    // Replay the three manually drawn arrows of an allyl aryl ether. The
    // attacked terminus is closer on screen to the ipso carbon than to ortho.
    for(bool reversed:{false,true}){
        Document d;d.setDefaultBondLength(14.4);
        const Vec2 pixels[]={{312,594},{457,677},{457,843},{312,926},{168,843},{168,677},
                             {312,427},{457,343},{457,176},{601,93}};
        auto point=[](Vec2 pixel){return (pixel-Vec2(150,80))*(14.4/167);};
        std::array<ObjectId,10> ids;
        for(int i=0;i<10;++i)d.addAtom(point(pixels[i]),i==6?8:6,&ids[i]);
        auto bond=[&](int i,int j,int order){
            ObjectId id;d.addBond(ids[reversed?j:i],ids[reversed?i:j],&id);d.setBondOrder(id,BondOrder(order));
            return ObjectRef(ObjectKind::Bond,id);
        };
        auto aryl=bond(0,1,2);bond(1,2,1);bond(2,3,2);bond(3,4,1);bond(4,5,2);bond(5,0,1);
        auto co=bond(0,6,1),ether=bond(6,7,1),allyl=bond(7,8,1),alkene=bond(8,9,2);
        auto arrow=[&](ObjectRef from,ObjectRef to,Vec2 tail,Vec2 tip){
            auto p=arrows::resolvePreset(49,point(tail),point(tip),.6);
            p.head.length=2.9;p.head.width=1.6;
            if(to.kind==ObjectKind::Bond)p.arcSagitta*=-1.7;
            p.mechanism=true;p.electronSource=from;p.electronTarget=to;return p;
        };
        std::vector<arrows::ArrowParams> input={
            arrow(ether,co,{427,379},{329,548}),arrow(alkene,allyl,{570,164},{473,247}),
            arrow(aryl,ObjectRef(ObjectKind::Atom,ids[9]),{425,639},{615,reversed?171.:139.})};
        if(reversed)std::reverse(input.begin(),input.end());
        auto verify=[&](const Document& product,const std::vector<ObjectRef>& made){
            std::map<ObjectId,ObjectId> copies;
            for(auto ref:made)if(ref.kind==ObjectKind::Atom)copies[studio_mechanism_products::sourceAtom(product,ref.id)]=ref.id;
            for(int i=0;i<10;++i){
                auto id=copies.at(ids[i]);const auto& sem=product.atoms().alive(id)->semantics;
                if(sem.charge||sem.radical)throw std::runtime_error("aryl allyl rearrangement: charge or radical");
                for(int j=i+1;j<10;++j){
                    auto old=d.bonds().alive(d.bondBetween(ids[i],ids[j]));int order=old?int(old->order):0;
                    if((i==0&&j==1)||(i==8&&j==9)||(i==1&&j==9))order=1;
                    if((i==0&&j==6)||(i==7&&j==8))order=2;
                    if(i==6&&j==7)order=0;
                    auto b=product.bonds().alive(product.bondBetween(id,copies.at(ids[j])));
                    if((b?int(b->order):0)!=order)throw std::runtime_error("aryl allyl rearrangement: wrong product connectivity");
                }
            }
        };
        Document product;std::vector<mechanism::ElectronMove> moves;std::vector<ObjectRef> made;
        auto err=studio_drawn_step::prepare(d,{},input,product,moves,made);
        if(!err.ok())throw std::runtime_error("aryl allyl replay: "+err.message);
        verify(product,made);
        std::vector<ObjectRef> selected;
        for(auto& p:input){ObjectId id;arrows::addArrow(d,p,.6,&id);selected.emplace_back(ObjectKind::Arrow,id);}
        s.cancel();s.doc=d;s.history.clear();s.input.bindDocument(s.doc.id());s.refresh();s.selected=selected;generateMechanismStep();
        verify(s.doc,s.selected);
        if(!reversed&&!s.save(dir+"/allyl-aryl-step.cdxml",true).ok())throw std::runtime_error("save allyl aryl step");
    }
    checkAldolAnnulation(*this,dir);
    checkDielsAlder(*this,dir);
    // Four coupled arrows for an enolate adding to an enone. Reverse both
    // stored bond orientation and drawing order to exercise endpoint choice.
    for(bool reverseBonds:{false,true})for(bool reverseArrows:{false,true}){
        Document d;std::vector<ObjectId> atoms;
        const Vec2 positions[]={{0,0},{30,0},{60,0},{60,50},{30,50},{0,50},{0,80}};
        for(int i=0;i<7;++i){ObjectId id;d.addAtom(positions[i],i==0||i==6?8:6,&id);atoms.push_back(id);}
        auto sem=d.atoms().alive(atoms[0])->semantics;sem.charge=-1;d.setAtomSemantics(atoms[0],sem);
        auto atom=[&](int i){return ObjectRef(ObjectKind::Atom,atoms[i]);};
        auto bond=[&](int a,int b,BondOrder order){
            ObjectId id;d.addBond(atoms[reverseBonds?b:a],atoms[reverseBonds?a:b],&id);
            d.setBondOrder(id,order);return ObjectRef(ObjectKind::Bond,id);
        };
        auto oc=bond(0,1,BondOrder::Single),donor=bond(1,2,BondOrder::Double);
        auto alkene=bond(3,4,BondOrder::Double),link=bond(4,5,BondOrder::Single),co=bond(5,6,BondOrder::Double);
        auto arrow=[&](ObjectRef from,ObjectRef to){
            Vec2 start,end;arrows::mechanismAnchorPoint(d,from,&start);arrows::mechanismAnchorPoint(d,to,&end);
            auto p=arrows::resolvePreset(49,start,end,.6);p.mechanism=true;p.electronSource=from;p.electronTarget=to;return p;
        };
        std::vector<arrows::ArrowParams> inputs={arrow(atom(0),oc),arrow(donor,alkene),arrow(alkene,link),arrow(co,atom(6))};
        if(reverseArrows)std::reverse(inputs.begin(),inputs.end());
        Document product;std::vector<mechanism::ElectronMove> moves;std::vector<ObjectRef> made;
        auto result=studio_mechanism::prepareProduct(d,{},inputs,&product,&moves,&made);
        if(!result.ok())throw std::runtime_error("conjugate addition: "+result.message);
        std::map<ObjectId,ObjectId> copies;
        for(auto ref:made)if(ref.kind==ObjectKind::Atom)copies[studio_mechanism_products::sourceAtom(product,ref.id)]=ref.id;
        for(int i=0;i<7;++i){
            auto id=copies.at(atoms[i]);
            if(product.atoms().alive(id)->semantics.charge!=(i==6?-1:0))throw std::runtime_error("conjugate addition charge");
            for(int j=i+1;j<7;++j){
                const auto* edge=product.bonds().alive(product.bondBetween(id,copies.at(atoms[j])));
                int expected=j==i+1?(i==0||i==4?2:1):0;
                if((edge?int(edge->order):0)!=expected)throw std::runtime_error("conjugate addition connectivity");
            }
        }
    }
    checkExpansionCleanup(*this,dir);
    checkVisibleCleanup(*this,dir);
    checkExplicitHydrogenExpansion(*this,dir);
    checkMechanismLocalCleanup(*this,dir);
    checkGeneratedRingCleanup(*this,dir);
    checkBranchAndClean(*this,dir);
    checkBaseByproduct(*this,dir);
    checkReciprocalArrows(*this,dir);
    checkMechanismPageFlow(*this,dir);
    checkMultilineByproducts(*this,dir);
    checkCurrentFishhooks(*this,dir);
    checkMassLayout(*this,dir);
    checkHalogenSubstitution(*this,dir);
    checkGeneratedSpacing(*this,dir);
    checkFishhookBetaScission(dir);
    checkUserHydrazine(*this,dir);
    auto require=[&](bool value,const char* why){std::ofstream(dir+"/mechanism-step-progress.txt",std::ios::app)<<why<<": "<<(value?"pass":"FAIL")<<"\n";if(!value)throw std::runtime_error(why);};
    checkCarbonFragments(*this,dir);
    checkCompactReagents(*this,dir);
    checkMechanismFollow(*this,dir);
    checkFusedCarbeneMigration(*this,dir);
    checkMethanolMechanism(*this,dir);
    checkVirtualBonds(*this,dir);
    checkReagentMechanisms(*this,dir);
    checkExpandedReagents(*this,dir);
    for(bool reverseBonds:{false,true})for(bool reverseArrows:{false,true}){
        Document source;
        source.setDefaultBondLength(30);
        const Vec2 points[]={{-15,-26},{15,-26},{30,0},{15,26},{-15,26},{-30,0},{60,0}};
        std::vector<ObjectId> atoms,bonds;
        for(int i=0;i<7;++i){
            ObjectId id;source.addAtom(points[i],i==6?35:6,&id);atoms.push_back(id);
        }
        for(int i=0;i<7;++i){
            ObjectId id;
            const auto a=i<6?atoms[i]:atoms[2],b=i<6?atoms[(i+1)%6]:atoms[6];
            source.addBond(reverseBonds?b:a,reverseBonds?a:b,&id);bonds.push_back(id);
        }
        auto shift=arrows::resolvePreset(49,Vec2(0,-26),Vec2(26,0),.6);
        auto departure=arrows::resolvePreset(49,Vec2(45,0),Vec2(57,0),.6);
        shift.mechanism=departure.mechanism=true;
        shift.electronSource=ObjectRef(ObjectKind::Bond,bonds[0]);
        shift.electronTarget=ObjectRef(ObjectKind::Atom,atoms[2]);
        departure.electronSource=ObjectRef(ObjectKind::Bond,bonds[6]);
        departure.electronTarget=ObjectRef(ObjectKind::Atom,atoms[6]);
        std::vector<arrows::ArrowParams> arrowsIn={shift,departure};
        if(reverseArrows)std::reverse(arrowsIn.begin(),arrowsIn.end());
        auto checkProduct=[&](const Document& product,const std::vector<ObjectRef>& made){
            std::map<ObjectId,ObjectId> copied;
            for(auto ref:made)if(ref.kind==ObjectKind::Atom)
                copied[studio_mechanism_products::sourceAtom(product,ref.id)]=ref.id;
            require(copied.size()==7,"ring contraction preserves six carbons and bromide");
            for(size_t i=0;i<7;++i){
                const auto* atom=product.atoms().alive(copied.at(atoms[i]));
                require(atom->semantics.charge==(i==1?1:i==6?-1:0),"ring contraction leaves the cation on the exocyclic carbon");
                for(size_t j=i+1;j<7;++j){
                    const bool retained=j==i+1&&i!=0&&j<6;
                    const bool expected=retained||(i==0&&(j==2||j==5));
                    const auto* bond=functional_groups::bond(product,copied.at(atoms[i]),copied.at(atoms[j]));
                    require(expected?(bond&&bond->order==BondOrder::Single):!bond,
                            "ring contraction gives a five-membered ring and an exocyclic single bond");
                }
            }
        };
        Document prepared;std::vector<ObjectRef> products;std::vector<mechanism::ElectronMove> moves;
        const auto result=studio_mechanism::prepareProduct(source,{},arrowsIn,&prepared,&moves,&products);
        if(!result.ok())std::ofstream(dir+"/mechanism-step-progress.txt",std::ios::app)<<result.message<<"\n";
        require(result.ok(),"sigma migration and bromide departure resolve together");
        checkProduct(prepared,products);
        require(studio_drawn_step::prepare(source,{},arrowsIn,prepared,moves,products).ok(),"direct arrow replay handles ring contraction");
        checkProduct(prepared,products);
        if(!reverseBonds&&!reverseArrows){
            s.cancel();s.doc=source;s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
            for(auto arrow:arrowsIn)require(arrows::addArrow(s.doc,arrow,.6,nullptr).ok(),"add ring-contraction arrows");
            s.refresh();s.selected=s.all();
            const auto before=s.doc.digest();
            generateMechanismStep();
            require(s.doc.stats().aliveAtoms==14,"generate next step creates the ring-contraction product");
            checkProduct(s.doc,s.selected);
            const auto after=s.doc.digest();
            require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,
                    "ring contraction undoes and redoes in one step");
            require(s.save(dir+"/ring-contraction.cdxml",true).ok(),"save editable ring-contraction example");
            s.selected.clear();s.fit();invalidate();screenshot(dir+"/ring-contraction.png");
        }
    }
    for(double turn:{0.,.65,2.4})for(double mirror:{-1.,1.}){
        // A distorted exocyclic substituent on a cyclohexene, including an
        // explicit O–H group whose internal bond must rotate with its oxygen.
        Document d;
        std::map<ObjectId,Vec2> pose;
        std::map<ObjectId,AtomRecord> atoms;
        std::map<mechanism::step_detail::Edge,int> links;
        const Vec2 points[]={{20,0},{10,-17.3205080757},{-10,-17.3205080757},
                            {-20,0},{-10,17.3205080757},{10,17.3205080757},{30,17},{44,23}};
        for(size_t i=0;i<8;++i){
            auto p=points[i];p.x*=mirror;p=mechanism::step_detail::rotate(p,turn)+Vec2(95,77);
            ObjectId id;d.addAtom(p,i==6?8:i==7?1:6,&id);
            pose[id]=p;atoms[id]=*d.atoms().alive(id);
        }
        for(ObjectId i=0;i<6;++i)links[mechanism::step_detail::edge(i,(i+1)%6)]=i==0?2:1;
        links[{0,6}]=links[{6,7}]=1;
        const auto original=pose;
        size_t budget=100000;
        mechanism::step_detail::alignTrigonalBranches(links,atoms,{},20,budget,pose);
        const auto pi=pose[1]-pose[0],sigma=pose[6]-pose[0];
        require(std::abs(dot(pi,sigma)/(length(pi)*length(sigma))+.5)<1e-7,
                "automatic trigonal layout sets the single-to-double angle to 120 degrees");
        for(ObjectId i=0;i<6;++i)require(length(pose[i]-original.at(i))<1e-8,"trigonal adjustment preserves cyclic vertices");
        require(std::abs(length(pose[6]-pose[0])-length(original.at(6)-original.at(0)))<1e-8&&
                std::abs(length(pose[7]-pose[6])-length(original.at(7)-original.at(6)))<1e-8,
                "trigonal adjustment preserves branch bond lengths");
    }
    for(int mode:{0,1,2,3}){
        // The screenshot shows carboxylate attack on an alkoxyphosphonium
        // center bearing a solid methyl wedge and a compact ethyl ester.
        Document source;
        auto atom=[&](Vec2 p,int z,int charge=0){
            ObjectId id;require(source.addAtom(p,z,&id).ok(),"SN2 reactant atom");
            auto sem=source.atoms().alive(id)->semantics;
            sem.charge=charge;
            require(source.setAtomSemantics(id,sem).ok(),"SN2 reactant charge");
            return id;
        };
        auto link=[&](ObjectId a,ObjectId b){
            ObjectId id;require(source.addBond(a,b,&id).ok(),"SN2 reactant bond");return id;
        };
        auto A=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};
        auto B=[](ObjectId id){return ObjectRef(ObjectKind::Bond,id);};
        auto center=atom(Vec2(45,62),6),methyl=atom(Vec2(45,47),6),ester=atom(Vec2(58,70),6);
        auto leaving=atom(Vec2(32,70),8),phosphorus=atom(Vec2(19,62),15,1);
        auto nucleophile=atom(Vec2(102,34),8,-1),acyl=atom(Vec2(115,26),6),carbonyl=atom(Vec2(115,11),8);
        auto hydrogen=mode==2?atom(Vec2(44,77),1):center;
        require(source.setAtomLabel(ester,"CO2Et").ok()&&source.setAtomLabel(phosphorus,"Ph3P").ok(),
                "SN2 fixture uses the user's compact ligands");
        const auto methylLink=link(center,methyl),esterLink=link(center,ester),leavingLink=link(center,leaving);
        link(leaving,phosphorus);
        if(mode==3){
            const auto spectatorWedge=link(acyl,nucleophile);
            source.setBondDisplay(spectatorWedge,BondDisplay::Wedge);
            source.setBondStereo(spectatorWedge,BondStereo::Up);
            link(acyl,carbonyl);
        }else{
            link(nucleophile,acyl);source.setBondOrder(link(acyl,carbonyl),BondOrder::Double);
        }
        link(acyl,atom(Vec2(128,34),6));
        if(mode==2)link(center,hydrogen);
        const auto originalWedge=mode==2?leavingLink:methylLink;
        require(source.setBondDisplay(originalWedge,mode==2?BondDisplay::WedgeHash:BondDisplay::Wedge).ok()&&
                source.setBondStereo(originalWedge,mode==2?BondStereo::Down:BondStereo::Up).ok(),"SN2 fixture has directed tetrahedral stereo");
        const auto& visual=arrows::mechanismVisualBoxes(source).at(nucleophile);
        const auto start=(visual.charge.min+visual.charge.max)*.5;
        auto attack=arrows::resolvePreset(49,start,Vec2(47,62),.6);
        auto departure=arrows::resolvePreset(49,Vec2(38,65),Vec2(33,68),.6);
        attack.mechanism=departure.mechanism=true;
        attack.electronSource=A(nucleophile);attack.sourceVisualAnchor=2;
        if(mode!=0){
            attack.electronTarget=mode==1?B(esterLink):A(center);
            attack.targetVisualAnchor=mode==1?5:1;
            departure.electronSource=B(leavingLink);departure.electronTarget=A(leaving);
        }
        Document prepared;
        std::vector<ObjectRef> products;
        std::vector<mechanism::ElectronMove> moves;
        const auto before=source.digest();
        const auto result=studio_mechanism::prepareProduct(source,{}, {attack,departure},&prepared,&moves,&products);
        if(!result.ok())std::ofstream(dir+"/mechanism-step-progress.txt",std::ios::app)<<result.message<<"\n";
        require(result.ok()&&moves[0].target==A(center)&&moves[1].source==B(leavingLink),
                "SN2 detects attack and departure at the same carbon");
        require(source.digest()==before&&prepared.atomLabel(products.at(ester).id)=="CO2Et",
                "SN2 keeps the compact ester and original drawing");
        const auto product=[&](ObjectId id){return products.at(id).id;};
        require(!functional_groups::bond(prepared,product(center),product(leaving))&&
                functional_groups::bond(prepared,product(center),product(nucleophile))&&
                prepared.atoms().alive(product(phosphorus))->semantics.charge==1&&
                prepared.atoms().alive(product(leaving))->semantics.charge==-1&&
                prepared.atoms().alive(product(nucleophile))->semantics.charge==0,
                "SN2 exchanges the ligands with the correct formal charges");
        // The sign is the directed area of the three remaining ligands,
        // adjusted for which ligand was marked out of the plane.
        auto parity=[&](const Document& doc,ObjectId c,const std::array<ObjectId,4>& ligands){
            int stereo=0;
            for(size_t i=0;i<ligands.size();++i){
                if(ligands[i]==c)continue;
                const auto* bond=functional_groups::bond(doc,c,ligands[i]);
                if(!bond||bond->beginAtom!=c)continue;
                const auto depth=bond->display==BondDisplay::Wedge?1:bond->display==BondDisplay::WedgeHash?-1:0;
                if(!depth)continue;
                std::vector<Vec2> plane;
                for(size_t j=0;j<ligands.size();++j)if(j!=i)plane.push_back(doc.atoms().alive(ligands[j])->position);
                const auto u=plane[1]-plane[0],v=plane[2]-plane[0];
                const auto area=u.x*v.y-u.y*v.x;
                if(std::abs(area)>1e-6)stereo+=(area>0?1:-1)*(i%2?-1:1)*depth;
            }
            return stereo;
        };
        const auto originalParity=parity(source,center,{methyl,ester,leaving,hydrogen});
        const auto productParity=parity(prepared,product(center),{product(methyl),product(ester),product(nucleophile),product(hydrogen)});
        require(originalParity!=0&&productParity==-originalParity,"SN2 inverts geometric parity with the entering ligand in the departing slot");
        if(mode==3){
            const auto* spectator=functional_groups::bond(prepared,product(acyl),product(nucleophile));
            require(spectator&&spectator->beginAtom==product(acyl)&&spectator->display==BondDisplay::Wedge&&
                    spectator->stereo==BondStereo::Up,"SN2 preserves the stereocenter attached to the nucleophile");
        }
        if(mode==1){
            s.doc=source;s.refresh();
            for(const auto& arrow:{attack,departure})require(arrows::addArrow(s.doc,arrow,.6,nullptr).ok(),"add SN2 arrow to canvas");
            s.selected=s.all();
            generateMechanismStep();
            require(s.doc.stats().aliveAtoms==2*source.stats().aliveAtoms,"the editor generates the SN2 product");
            arrows::ArrowParams repaired;bool ok=false;
            require(arrows::readArrow(s.doc,0,&repaired,&ok).ok()&&ok&&repaired.electronTarget==A(center)&&
                    repaired.targetVisualAnchor==1,"saved attack binding targets the tetrahedral carbon");
        }
    }
    // Triphenylphosphine donation into the azo group: the short second arrow
    // takes the N=N pi pair to the distal nitrogen in the same step.
    for(bool bound:{false,true}){
        Document source;
        auto atom=[&](Vec2 point,int element){
            ObjectId id;require(source.addAtom(point,element,&id).ok(),"azo fixture atom");return id;
        };
        auto bond=[&](ObjectId a,ObjectId b){
            ObjectId id;require(source.addBond(a,b,&id).ok(),"azo fixture bond");return id;
        };
        auto atomRef=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};
        auto p=atom(Vec2(26,89),15),n1=atom(Vec2(83,52),7),n2=atom(Vec2(107,52),7);
        auto left=atom(Vec2(63,32),6),right=atom(Vec2(122,31),6);
        require(source.setAtomLabel(left,"EtO2C").ok()&&source.setAtomLabel(right,"CO2Et").ok(),"ester labels in both reading orders");
        auto sem=source.atoms().alive(p)->semantics;
        sem.nodeType=uint8_t(NodeType::Query);sem.showLabel=true;sem.implicitHydrogens=0;
        require(source.setAtomSemantics(p,sem).ok()&&source.setAtomLabel(p,"Ph3P").ok(),"compact phosphine donor");
        TextRun ph,three,phosphorus;
        ph.utf8="Ph";three.utf8="3";three.flags=kSubscript;phosphorus.utf8="P";
        studioatom::set(source,p,{ph,three,phosphorus});
        auto& attrs=source.extensions(atomRef(p));
        attrs.set("studio.label.text","Ph3P");attrs.set("studio.label.offset","-20 4");
        attrs.set(mechanism::lonePairKey(),"1");
        auto pi=bond(n1,n2);source.setBondOrder(pi,BondOrder::Double);bond(left,n1);bond(n2,right);
        const auto box=arrows::mechanismVisualBoxes(source).at(p).lonePairs.front();
        auto attack=arrows::resolvePreset(49,Vec2(box.max.x+1,(box.min.y+box.max.y)*.5),Vec2(81,59),.6);
        auto shift=arrows::resolvePreset(49,Vec2(95,56),Vec2(101,57),.6);
        attack.mechanism=shift.mechanism=true;
        if(bound){
            attack.electronSource=atomRef(p);attack.sourceVisualAnchor=3;
            attack.electronTarget=atomRef(n1);attack.targetVisualAnchor=1;
            shift.electronSource=ObjectRef(ObjectKind::Bond,pi);shift.electronTarget=atomRef(n2);
        }
        const auto digest=source.digest();
        Document prepared;std::vector<mechanism::ElectronMove> moves;std::vector<ObjectRef> products;size_t preserved=0;
        auto error=studio_mechanism::prepareProduct(source,{}, {attack,shift},&prepared,&moves,&products,&preserved);
        if(!error.ok())std::ofstream(dir+"/mechanism-step-progress.txt",std::ios::app)<<error.message<<"\n";
        require(error.ok(),bound?"bound phosphine/azo arrows generate the next step":"unbound short azo arrows resolve from the original drawing");
        require(moves[0].source==atomRef(p)&&moves[0].target==atomRef(n1)&&
                moves[1].source==ObjectRef(ObjectKind::Bond,pi)&&moves[1].target==atomRef(n2),
                "azo endpoints resolve to P, N=N and the two distinct nitrogens");
        require(source.digest()==digest&&preserved==2&&mechanism::lonePairCount(source,p)==1&&
                prepared.atomLabel(products.at(3).id)=="EtO2C"&&prepared.atomLabel(products.at(4).id)=="CO2Et",
                "both ester spellings remain compact in the product and the reactant stays intact");
        auto newP=products.at(0).id,newN1=products.at(1).id,newN2=products.at(2).id;
        require(prepared.atoms().alive(newP)->element==15&&prepared.atoms().alive(newP)->semantics.charge==1&&
                prepared.atoms().alive(newN1)->semantics.charge==0&&prepared.atoms().alive(newN2)->semantics.charge==-1&&
                prepared.atoms().alive(newN1)->semantics.implicitHydrogens==0&&prepared.atoms().alive(newN2)->semantics.implicitHydrogens==0,
                "P becomes positive and the distal N negative without adding hydrogen");
        const auto* pn=functional_groups::bond(prepared,newP,newN1);
        const auto* nn=functional_groups::bond(prepared,newN1,newN2);
        require(pn&&nn&&pn->order==BondOrder::Single&&nn->order==BondOrder::Single&&
                prepared.atomLabel(newP)=="Ph3P"&&studioatom::runs(prepared,newP).at(1).flags==kSubscript&&
                mechanism::lonePairCount(prepared,newP)==0,"product has P–N and N–N single bonds and a styled phosphonium label without the donated pair");
        if(bound){
            s.doc=source;
            for(const auto& arrow:{attack,shift})require(arrows::addArrow(s.doc,arrow,.6,nullptr).ok(),"add selected azo mechanism arrow");
            s.selected=s.all();generateMechanismStep();
            require(s.doc.stats().aliveAtoms==10&&s.selected.size()==10,"the editor command appends the compact azo product and selects it");
            ObjectId productP=kInvalidObjectId,productN=kInvalidObjectId,productProximal=kInvalidObjectId;
            for(auto ref:s.selected)if(ref.kind==ObjectKind::Atom){
                const auto* atom=s.doc.atoms().alive(ref.id);
                if(atom->element==15)productP=ref.id;
                if(atom->element==7){
                    if(atom->semantics.charge==-1)productN=ref.id;
                    else productProximal=ref.id;
                }
            }
            require(productP!=kInvalidObjectId&&productN!=kInvalidObjectId&&productProximal!=kInvalidObjectId,
                    "find the three generated reaction centers in the editor selection");
            s.selected.clear();
            s.activateCommand("chooseSelectionTool_Marquee");
            const auto nVisual=arrows::mechanismVisualBoxes(s.doc).at(productN);
            require(nVisual.hasCharge,"the generated nitrogen displays a charge glyph");
            const auto charge=nVisual.charge;
            const auto center=(charge.min+charge.max)*.5;
            const auto beforeDrag=s.doc.digest();
            const bool grabbed=beginElectronMarkerDrag(s.view().toDevice(center));
            if(!grabbed||electronDragAtom!=productN||electronDragSite!=2)
                std::ofstream(dir+"/mechanism-step-progress.txt",std::ios::app)
                    <<"charge hit: "<<grabbed<<" id "<<electronDragAtom<<" expected "<<productN
                    <<" site "<<electronDragSite<<" tool "<<s.entries[s.activeIndex].owner
                    <<" box "<<charge.min.x<<","<<charge.min.y<<" to "<<charge.max.x<<","<<charge.max.y<<"\n";
            require(grabbed&&electronDragAtom==productN&&electronDragSite==2,
                    "the generated nitrogen superscript can be grabbed");
            updateElectronMarkerDrag(s.view().toDevice(center+Vec2(60,60)),true);
            const auto moved=arrows::mechanismVisualBoxes(s.doc).at(productN).charge;
            require(length((moved.min+moved.max)*.5-center)>2&&s.doc.atoms().alive(productN)->semantics.charge==-1,
                    "dragging moves the generated negative marker and preserves the charge");
            require(s.undo().ok()&&s.doc.digest()==beforeDrag,"generated charge drag is one undo");

            const auto addAtom=[&](Vec2 position,int z){ObjectId id;require(s.doc.addAtom(position,z,&id).ok(),"continuation atom");return id;};
            const auto addBond=[&](ObjectId a,ObjectId b){ObjectId id;require(s.doc.addBond(a,b,&id).ok(),"continuation bond");return id;};
            auto neutral=s.doc.atoms().alive(productN)->semantics;
            neutral.charge=0;neutral.implicitHydrogens=1;
            s.doc.setAtomSemantics(productN,neutral);
            const auto pPos=s.doc.atoms().alive(productP)->position;
            auto hydroxyl=addAtom(pPos+Vec2(22,48),6),carbon=addAtom(pPos+Vec2(42,40),6);
            addBond(carbon,hydroxyl);
            require(nicknames::apply(s.doc,hydroxyl,"OH").ok(),"hand-typed OH nickname");
            s.refresh();s.fit();
            const auto hBox=arrows::mechanismVisualBoxes(s.doc).at(hydroxyl).attachedProton;
            const auto hCenter=(hBox.min+hBox.max)*.5;
            require(beginElectronMarkerDrag(s.view().toDevice(hCenter))&&electronDragAtom==hydroxyl&&
                    electronDragSite==attachedProtonDragSite,"a hand-drawn neutral OH exposes its H glyph for dragging");
            const auto hydrogen=ObjectId(s.doc.atoms().size());
            updateElectronMarkerDrag(s.view().toDevice(hCenter+Vec2(0,45)),true);
            require(s.doc.atoms().alive(hydrogen)&&s.doc.atoms().alive(hydrogen)->element==1&&
                    s.doc.atoms().alive(hydroxyl)->element==8&&s.doc.atoms().alive(hydroxyl)->semantics.implicitHydrogens==0&&
                    functional_groups::bond(s.doc,hydroxyl,hydrogen),"dragging the OH hydrogen creates one explicit O–H bond");
            s.doc.extensions(atomRef(hydroxyl)).set(mechanism::lonePairKey(),"1");
            s.refresh();s.fit();s.selected.clear();
            s.activateCommand("chooseArrowTool_90_CW");
            const auto& visual=arrows::mechanismVisualBoxes(s.doc);
            const auto pBody=visual.at(productP).body;
            const auto pCenter=(pBody.min+pBody.max)*.5;
            const auto oxygenPair=visual.at(hydroxyl).lonePairs.front();
            const auto pn=s.doc.incidentBonds(productP).front();
            const auto& pnStrokes=arrows::mechanismBondEnvelopes(s.doc).at(pn).strokes;
            require(!pnStrokes.empty(),"P–N has a visible stroke to start the return arrow");
            const auto& pnStroke=pnStrokes.front();
            const auto startPN=(pnStroke.begin+pnStroke.end)*.5;
            auto draw=[&](Vec2 from,Vec2 to,ObjectRef expectedFrom,ObjectRef expectedTo){
                const auto id=ObjectId(s.doc.arrows().size());
                s.gesture(PointerPhase::Down,from);s.gesture(PointerPhase::Move,to);s.gesture(PointerPhase::Up,to);
                arrows::ArrowParams actual;bool valid=false;
                require(arrows::readArrow(s.doc,id,&actual,&valid).ok()&&valid&&
                        actual.electronSource==expectedFrom&&actual.electronTarget==expectedTo,
                        "native continuation arrow binds to its exact chemical objects");
                return id;
            };
            const auto attackId=draw((oxygenPair.min+oxygenPair.max)*.5,pCenter,atomRef(hydroxyl),atomRef(productP));
            const auto splitId=draw(startPN,s.doc.atoms().alive(productProximal)->position,ObjectRef(ObjectKind::Bond,pn),atomRef(productProximal));
            s.selected={ObjectRef(ObjectKind::Arrow,attackId),ObjectRef(ObjectKind::Arrow,splitId)};
            const auto beforeStep=s.doc.atoms().size();
            generateMechanismStep();
            require(s.doc.atoms().size()==beforeStep+8,"compact phosphonium undergoes P–N to P–O substitution");
            require(functional_groups::bond(s.doc,ObjectId(beforeStep),ObjectId(beforeStep+5))&&
                    !functional_groups::bond(s.doc,ObjectId(beforeStep),ObjectId(beforeStep+1)),
                    "continuation replaces the P–N single bond with P–O");
        }
    }
    // Short radical addition drawn at a crowded C=O junction. Both the already
    // single-electron version and the repairable mixed version share the same graph.
    for(bool mixed:{false,true}){
        Document d;ObjectId c,o,methyl,radical,pi,adjacent;
        d.addAtom(Vec2(0,0),6,&c);d.addAtom(Vec2(-12.5,7.2),8,&o);
        d.addAtom(Vec2(12.5,7.2),6,&methyl);d.addAtom(Vec2(46,4),6,&radical);
        d.addBond(c,o,&pi);d.setBondOrder(pi,BondOrder::Double);d.addBond(c,methyl,&adjacent);
        auto sem=d.atoms().alive(radical)->semantics;sem.radical=uint8_t(Radical::Doublet);d.setAtomSemantics(radical,sem);
        auto atom=[](ObjectId id){return ObjectRef(ObjectKind::Atom,id);};
        auto bond=[](ObjectId id){return ObjectRef(ObjectKind::Bond,id);};
        auto attack=arrows::resolvePreset(57,arrows::mechanismRadicalPoints(d,radical).front(),Vec2(1.4,.8),.6);
        attack.mechanism=true;attack.electronSource=atom(radical);attack.sourceVisualAnchor=4;
        attack.electronTarget=bond(adjacent);attack.targetVisualAnchor=5;
        auto split=arrows::resolvePreset(mixed?49:57,Vec2(-6,4),Vec2(-12.5,8),.6);
        split.mechanism=true;split.electronSource=bond(pi);split.electronTarget=atom(o);
        std::vector<arrows::ArrowParams> raw={attack,split};
        auto distant=raw;distant[0].end=Vec2(10,5.8);bool changesCount=false;
        require(!studio_mechanism::correctCarbonylRadicalArrows(d,distant,changesCount),"a deliberate target farther down the neighboring bond is not retargeted");
        auto corrected=raw;
        require(studio_mechanism::correctCarbonylRadicalArrows(d,corrected,changesCount)&&changesCount==mixed&&
                corrected[0].electronTarget==atom(c)&&corrected[1].head.sides!=arrows::HeadSides::Both,
                "recognize only the local carbonyl radical correction");
        std::vector<mechanism::ElectronMove> moves;
        require(!mechanism::inferElectronMoves(d,raw,&moves).ok()&&mechanism::inferElectronMoves(d,corrected,&moves).ok(),
                "repaired arrows yield a valid single-electron step");
    }
    // Independent electron bookkeeping fixtures: coupling, homolysis and ionic capture.
    for(int mode=0;mode<7;++mode){
        Document d;ObjectId a,b,bond;d.addAtom(Vec2(0,0),6,&a);d.addAtom(Vec2(30,0),6,&b);
        auto sa=d.atoms().get(a)->semantics,sb=d.atoms().get(b)->semantics;
        sa.implicitHydrogens=sb.implicitHydrogens=3;
        if(mode==0||mode==3)sa.radical=sb.radical=uint8_t(Radical::Doublet);
        if(mode==1||mode==4)d.addBond(a,b,&bond);
        if(mode==2||mode==5){sa.charge=-1;sb.charge=1;}
        if(mode==6){sa.radical=uint8_t(Radical::Doublet);sb.charge=1;}
        d.setAtomSemantics(a,sa);d.setAtomSemantics(b,sb);
        ObjectRef ar(ObjectKind::Atom,a),br(ObjectKind::Atom,b);std::vector<mechanism::ElectronMove> mm;
        if(mode==1||mode==4)mm={{ObjectRef(ObjectKind::Bond,bond),ar,1},{ObjectRef(ObjectKind::Bond,bond),br,1}};
        else if(mode==2||mode==5||mode==6)mm={{ar,br,mode==2?2:1}};else mm={{ar,br,1},{br,ar,1}};
        if(mode==3||mode==4)mm.pop_back();auto digest=d.digest();std::vector<ObjectRef> out;auto e=mechanism::generateNextStep(d,mm,&out);
        if(mode==3){require(!e.ok()&&d.digest()==digest,"unpaired fishhook rejected atomically");continue;}
        require(e.ok(),mode==0?"two radicals couple":mode==1?"two fishhooks homolyse bond":mode==2?"anion captures carbocation":mode==4?"single homolysis hook leaves radicals at both ends":mode==5?"anion cation single electron transfer yields radicals":"radical cation single electron transfer moves radical");
        int nb=0,na=0;for(auto r:out)if(r.kind==ObjectKind::Bond)++nb;else if(r.kind==ObjectKind::Atom){++na;auto x=d.atoms().get(r.id);require(x->semantics.charge==(mode==6&&na==1?1:0)&&x->semantics.implicitHydrogens==3&&x->semantics.radical==((mode==1||mode==4||mode==5||(mode==6&&na==2))?uint8_t(Radical::Doublet):0),"product electrons and hydrogens conserved");}
        require(na==2&&nb==((mode==1||mode>=4)?0:1),"radical fixture product graph");
    }
    for(int mode=0;mode<6;++mode){
        Document d;
        auto atom=[&](int element,Vec2 p,bool radical=false){
            ObjectId id;require(d.addAtom(p,element,&id).ok(),"radical reaction atom");
            if(radical){auto sem=d.atoms().alive(id)->semantics;sem.radical=uint8_t(Radical::Doublet);d.setAtomSemantics(id,sem);}
            return ObjectRef(ObjectKind::Atom,id);
        };
        auto bond=[&](ObjectRef a,ObjectRef b,BondOrder order=BondOrder::Single){
            ObjectId id;require(d.addBond(a.id,b.id,&id).ok()&&d.setBondOrder(id,order).ok(),"radical reaction bond");
            return ObjectRef(ObjectKind::Bond,id);
        };
        const bool beta=mode==4,disproportionation=mode==5;
        auto a=atom(6,Vec2(-40,0),true),b=atom(6,Vec2(0,0),disproportionation);
        auto c=atom(mode==2?1:mode==3?17:6,Vec2(30,0));
        auto bc=bond(b,c,mode<2?BondOrder::Double:BondOrder::Single);
        std::vector<mechanism::ElectronMove> mm;
        ObjectId transferred=kInvalidObjectId;
        if(beta){
            auto ab=bond(a,b);
            mm={{a,ab,1},{bc,ab,1}};
        }else if(disproportionation){
            auto h=atom(1,Vec2(30,28));transferred=h.id;
            auto ch=bond(c,h);
            mm={{a,h,1},{b,bc,1},{ch,bc,1}};
        }else if(mode==0){
            mm={{a,bc,1},{bc,c,1}};  // Direct attack on the pi bond and the remaining electron.
        }else if(mode==1){
            mm={{a,b,1},{bc,a,1},{bc,c,1}}; // Three explicit hooks.
        }else{
            transferred=c.id;
            mm={{a,c,1},{bc,b,1}};   // H or halogen atom abstraction.
        }
        std::vector<ObjectRef> made;
        auto result=mechanism::generateNextStep(d,mm,&made);
        if(!result.ok())throw std::runtime_error("radical reaction "+std::to_string(mode)+": "+result.message);
        std::vector<ObjectId> atoms;
        std::map<std::pair<ObjectId,ObjectId>,int> bonds;
        int radicals=0,totalHydrogens=0;
        for(auto ref:made){
            if(ref.kind==ObjectKind::Atom){
                atoms.push_back(ref.id);auto sem=d.atoms().alive(ref.id)->semantics;
                radicals+=sem.radical==uint8_t(Radical::Doublet);
                totalHydrogens+=sem.implicitHydrogens+(d.atoms().alive(ref.id)->element==1);
                require(!sem.charge,"radical propagation preserves charge");
            }else if(ref.kind==ObjectKind::Bond){auto record=d.bonds().alive(ref.id);bonds[std::minmax(record->beginAtom,record->endAtom)]=int(record->order);}
        }
        require(atoms.size()==(disproportionation?4:3)&&radicals==(disproportionation?0:1),"radical reaction atom and spin balance");
        auto order=[&](int first,int second){return bonds[std::minmax(atoms[first],atoms[second])];};
        if(mode<2)require(order(0,1)==1&&order(1,2)==1&&d.atoms().alive(atoms[2])->semantics.radical&&totalHydrogens==7,"alkene addition moves the radical and reduces the pi bond");
        if(mode==2||mode==3)require(order(0,2)==1&&order(1,2)==0&&d.atoms().alive(atoms[1])->semantics.radical&&totalHydrogens==(mode==2?7:6),"H or halogen abstraction transfers the indicated atom");
        if(beta)require(order(0,1)==2&&order(1,2)==0&&d.atoms().alive(atoms[2])->semantics.radical&&totalHydrogens==7,"beta scission forms a double bond and a new radical");
        if(disproportionation)require(order(0,3)==1&&order(1,2)==2&&order(2,3)==0&&totalHydrogens==8,"disproportionation yields a saturated fragment and an alkene");
        if(transferred!=kInvalidObjectId)require(d.atoms().alive(transferred)!=nullptr,"transfer preserves the original reaction");
    }
    for(int mode=0;mode<3;++mode){
        Document d;ObjectId donor,left,right,proton,b;d.addAtom(Vec2(0,0),6,&donor);d.addAtom(Vec2(-30,0),6,&left);d.addAtom(Vec2(30,0),6,&right);d.addAtom(Vec2(0,50),1,&proton);
        d.addBond(donor,left,&b);d.setBondOrder(b,mode==0?BondOrder::Single:BondOrder::Double);d.addBond(donor,right,&b);d.setBondOrder(b,mode==0?BondOrder::Single:BondOrder::Double);auto hs=d.atoms().get(proton)->semantics;hs.charge=1;d.setAtomSemantics(proton,hs);
        auto before=d.digest();std::vector<ObjectRef> made;auto e=mechanism::generateNextStep(d,{{ObjectRef(ObjectKind::Atom,donor),ObjectRef(ObjectKind::Atom,proton),mode==2?1:2}},&made);
        require(!e.ok()&&d.digest()==before,mode==0?"saturated carbon cannot invent donor pair":mode==1?"two possible adjacent pi donors reject ambiguity":"single electron arrow never uses pair pi fallback");
    }
    {Document d;ObjectId a,b,x,y,pi;d.addAtom(Vec2(0,0),6,&a);d.addAtom(Vec2(30,0),6,&b);d.addAtom(Vec2(0,50),6,&x);d.addAtom(Vec2(30,50),6,&y);d.addBond(a,b,&pi);d.setBondOrder(pi,BondOrder::Double);for(auto id:{x,y}){auto sem=d.atoms().get(id)->semantics;sem.charge=1;d.setAtomSemantics(id,sem);}auto before=d.digest();std::vector<ObjectRef> made;auto e=mechanism::generateNextStep(d,{{ObjectRef(ObjectKind::Atom,a),ObjectRef(ObjectKind::Atom,x)},{ObjectRef(ObjectKind::Atom,b),ObjectRef(ObjectKind::Atom,y)}},&made);require(!e.ok()&&d.digest()==before,"same pi pair cannot be donated from both atom ends");}
    for(bool bondSource:{false,true}){Session gesture;ObjectId o,c,target,b;gesture.doc.addAtom(Vec2(0,0),8,&o);gesture.doc.addAtom(Vec2(14.4,0),6,&c);gesture.doc.addAtom(Vec2(28.8,14.4),8,&target);gesture.doc.addBond(o,c,&b);gesture.refresh();gesture.activateCommand("chooseArrowTool_90_CW");Vec2 start=bondSource?Vec2(7.2,0):Vec2(0,0),end=bondSource?Vec2(28.8,14.4):Vec2(7.2,0);gesture.gesture(PointerPhase::Down,start);gesture.gesture(PointerPhase::Move,end);gesture.gesture(PointerPhase::Up,end);arrows::ArrowParams p;bool ok=false;arrows::readArrow(gesture.doc,0,&p,&ok);require(ok&&p.mechanism&&p.electronSource==ObjectRef(bondSource?ObjectKind::Bond:ObjectKind::Atom,bondSource?b:o)&&p.electronTarget==ObjectRef(bondSource?ObjectKind::Atom:ObjectKind::Bond,bondSource?target:b),bondSource?"manual bond to atom captures exact semantic refs":"manual atom to bond captures exact semantic refs");}
    s.dirty=false;execute(Button("","新建","","@new"));s.graphicsWidth=0;s.graphicsColor=0x000000FFu;
    s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    ObjectId hole;s.doc.addAtom(Vec2(-100,-100),6,&hole);s.doc.removeAtom(hole);
    std::vector<ObjectId> atom,bond,arrow;
    const Vec2 positions[]={Vec2(30,45),Vec2(30,75),Vec2(4,90),Vec2(56,90),Vec2(160,45),Vec2(160,75),Vec2(134,90),Vec2(186,90)};
    for(int i=0;i<8;++i){ObjectId id;require(s.doc.addAtom(positions[i],i==0||i==4?8:6,&id).ok(),"fixture atom");atom.push_back(id);}
    auto oxygen=s.doc.atoms().get(atom[0])->semantics;oxygen.charge=-1;require(s.doc.setAtomSemantics(atom[0],oxygen).ok(),"enolate oxygen charge");
    const int ends[][3]={{0,1,1},{1,2,1},{1,3,2},{4,5,2},{5,6,1},{5,7,1}};
    for(const auto& e:ends){ObjectId id;require(s.doc.addBond(atom[e[0]],atom[e[1]],&id).ok(),"fixture bond");require(s.doc.setBondOrder(id,e[2]==2?BondOrder::Double:BondOrder::Single).ok(),"fixture bond order");bond.push_back(id);}
    auto A=[&](int n){return ObjectRef(ObjectKind::Atom,atom[n]);};
    auto B=[&](int n){return ObjectRef(ObjectKind::Bond,bond[n]);};
    const std::vector<mechanism::ElectronMove> moves={{A(0),B(0)},{B(2),A(5)},{B(3),A(4)}};
    for(size_t i=0;i<moves.size();++i){Vec2 from,to;require(arrows::mechanismAnchorPoint(s.doc,moves[i].source,&from)&&arrows::mechanismAnchorPoint(s.doc,moves[i].target,&to),"arrow anchors resolve");auto p=arrows::resolvePreset(49,from,to,.6);p.mechanism=true;p.electronSource=moves[i].source;p.electronTarget=moves[i].target;p.head.shape=arrows::ArrowHeadShape::Solid;p.head.length=3.2;p.head.width=2.24;if(i==1)p.arcSagitta=core::length(to-from)*.35;arrows::routeMechanism(s.doc,p,i!=1);arrows::clearMechanismLabels(s.doc,p);ObjectId id;require(arrows::addArrow(s.doc,p,.6,&id).ok(),"fixture semantic arrow");arrow.push_back(id);}
    s.refresh();require(s.save(dir+"/aldol-input.cdxml",true).ok(),"save input with noncontiguous atom IDs");
    {auto digest=s.doc.digest();s.selected={ObjectRef(ObjectKind::Arrow,arrow[1])};chooseArrowPreset(49);require(s.doc.digest()==digest,"choosing mechanism tool preserves selected arrow route and semantic references");s.selected.clear();}
    {Document copy;ObjectId dummy;copy.addAtom(Vec2(-100,-100),7,&dummy);copy.addAtom(Vec2(-130,-100),7,&dummy);std::vector<ObjectRef> inserted;Vec2 offset(20,30);require(studioio::append(s.doc,copy,s.all(),offset,&inserted).ok(),"append copies mechanism source objects");int count=0;for(const auto& record:copy.arrows().records())if(record.isAlive()){arrows::ArrowParams p;bool ok=false;arrows::readArrow(copy,record.id,&p,&ok);if(!p.mechanism)continue;Vec2 from,to;require(ok&&arrows::mechanismAnchorPoint(copy,p.electronSource,&from)&&arrows::mechanismAnchorPoint(copy,p.electronTarget,&to),"copied anchors resolve");bool matched=false;for(const auto& move:moves){Vec2 a,b;arrows::mechanismAnchorPoint(s.doc,move.source,&a);arrows::mechanismAnchorPoint(s.doc,move.target,&b);if(core::length(from-a-offset)<1e-7&&core::length(to-b-offset)<1e-7)matched=true;}require(matched,"copy remaps source and target to translated objects");++count;}require(count==3,"copy retains three semantic arrows");}
    std::vector<AtomRecord> originalAtoms;std::vector<BondRecord> originalBonds;
    for(auto id:atom)originalAtoms.push_back(*s.doc.atoms().get(id));for(auto id:bond)originalBonds.push_back(*s.doc.bonds().get(id));
    auto before=s.doc.digest();s.selected=s.all();generateMechanismStep();
    require(s.doc.stats().aliveAtoms==16&&s.doc.stats().aliveBonds==13,"one step adds eight atoms and seven product bonds");
    {auto page=studioio::Page::get(s.doc);auto bounds=selection::selectionBounds(s.doc,s.selected);require(page.valid()&&!bounds.empty()&&bounds.min.x>=page.x&&bounds.min.y>=page.y&&bounds.max.x<=page.x+page.width*page.across&&bounds.max.y<=page.y+page.height*page.down,"generated product fits expanded document pages");}
    std::vector<ObjectId> product;
    for(const auto& a:s.doc.atoms().records())if(a.isAlive()&&a.id>atom.back())product.push_back(a.id);
    require(product.size()==8,"product atom count");
    // The documented core API preserves ascending original atom order.
    auto order=[&](ObjectId a,ObjectId b){for(const auto& edge:s.doc.bonds().records())if(edge.isAlive()&&((edge.beginAtom==a&&edge.endAtom==b)||(edge.beginAtom==b&&edge.endAtom==a)))return int(edge.order);return 0;};
    require(order(product[0],product[1])==2&&order(product[1],product[3])==1&&order(product[3],product[5])==1&&order(product[4],product[5])==1,"aldol carbonyl reformation carbon addition and carbonyl opening");
    require(order(product[1],product[2])==1&&order(product[5],product[6])==1&&order(product[5],product[7])==1,"spectator methyl bonds preserved");
    int charge=0,hydrogens=0;const int expectedH[]={0,0,3,2,0,0,3,3};
    for(size_t i=0;i<8;++i){const auto* a=s.doc.atoms().get(product[i]);charge+=a->semantics.charge;int actualH=a->semantics.implicitHydrogens;if(actualH<0){int sum=0;for(auto id:s.doc.incidentBonds(a->id))sum+=int(s.doc.bonds().get(id)->order);actualH=(a->element==6?4-std::abs(int(a->semantics.charge)):2+int(a->semantics.charge))-sum;}hydrogens+=actualH;require(a->element==originalAtoms[i].element,"product element identity conserved");require(actualH==expectedH[i],"per-atom hydrogen count conserved");require(a->semantics.charge==(i==4?-1:0),"aldolate charge belongs only to former ketone oxygen");}
    require(charge==-1&&hydrogens==11,"product conserves C6H11O2 and net charge minus one");
    for(size_t i=0;i<atom.size();++i){auto a=s.doc.atoms().get(atom[i]);require(a->position==originalAtoms[i].position&&a->element==originalAtoms[i].element&&a->semantics==originalAtoms[i].semantics,"reactant atoms unchanged");}
    for(size_t i=0;i<bond.size();++i){auto b=s.doc.bonds().get(bond[i]);require(b->beginAtom==originalBonds[i].beginAtom&&b->endAtom==originalBonds[i].endAtom&&b->order==originalBonds[i].order,"reactant bonds unchanged");}
    double sourceRight=-1e30,sourceBottom=-1e30,productLeft=1e30,productTop=1e30;for(auto id:atom){auto p=s.doc.atoms().get(id)->position;sourceRight=std::max(sourceRight,p.x);sourceBottom=std::max(sourceBottom,p.y);}for(auto id:product){auto p=s.doc.atoms().get(id)->position;productLeft=std::min(productLeft,p.x);productTop=std::min(productTop,p.y);}require(productLeft>sourceRight+20||productTop>sourceBottom+20,"product stays separated to the right or in the next row");
    auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"entire generated step is one undo");require(s.undo(true).ok()&&s.doc.digest()==after,"redo restores exact generated step");
    require(s.save(dir+"/aldol-result.cdxml",true).ok(),"save editable result");s.selected.clear();switchTab(1);layout();s.fit();invalidate();screenshot(dir+"/aldol-result.png");
    require(s.undo().ok(),"restore source for negative cases");
    {Document bad=s.doc;auto sem=bad.atoms().get(atom[3])->semantics;sem.implicitHydrogens=3;bad.setAtomSemantics(atom[3],sem);auto digest=bad.digest();std::vector<ObjectRef> created;auto e=mechanism::generateNextStep(bad,moves,&created);require(!e.ok()&&bad.digest()==digest,"incorrect explicit hydrogen count rejected atomically");}
    {Document bad=s.doc;bad.setBondDisplay(bond[1],BondDisplay::Wedge);auto digest=bad.digest();std::vector<ObjectRef> created;auto e=mechanism::generateNextStep(bad,moves,&created);require(!e.ok()&&bad.digest()==digest,"reacting stereocenter is not silently flattened");}
    {Document spectator=s.doc;spectator.setElement(atom[2],16);std::vector<ObjectRef> created;auto e=mechanism::generateNextStep(spectator,moves,&created);require(e.ok(),"remote sulfur spectator supported");bool preserved=false;for(auto r:created)if(r.kind==ObjectKind::Atom){auto a=spectator.atoms().get(r.id);if(a->element==16)preserved=a->semantics.implicitHydrogens==-1&&a->semantics.charge==0;}require(preserved,"remote sulfur semantics preserved without invented hydrogens");}
    {Document spectator=s.doc;std::vector<ObjectId> ring;const Vec2 xy[]={Vec2(-45,80),Vec2(-60,106),Vec2(-90,106),Vec2(-105,80),Vec2(-90,54),Vec2(-60,54)};for(auto p:xy){ObjectId id;spectator.addAtom(p,6,&id);ring.push_back(id);}ObjectId bridge;spectator.addBond(atom[2],ring[0],&bridge);std::vector<ObjectId> aromatic;for(int i=0;i<6;++i){ObjectId id;spectator.addBond(ring[i],ring[(i+1)%6],&id);require(spectator.setBondSemantics(id,BondSemantics::Aromatic).ok()&&spectator.setBondOrder(id,BondOrder::Aromatic).ok(),"fixture has actual aromatic bond semantics and order");aromatic.push_back(id);}auto original=spectator;std::vector<ObjectRef> created;auto e=mechanism::generateNextStep(spectator,moves,&created);require(e.ok(),"remote phenyl spectator supported");int count=0;for(auto r:created)if(r.kind==ObjectKind::Bond&&spectator.bonds().get(r.id)->order==BondOrder::Aromatic&&spectator.bonds().get(r.id)->semantics==BondSemantics::Aromatic)++count;require(count==6,"aromatic spectator bonds preserved");auto digest=original.digest();std::vector<mechanism::ElectronMove> bad={{ObjectRef(ObjectKind::Bond,aromatic[0]),ObjectRef(ObjectKind::Atom,ring[0])}};e=mechanism::generateNextStep(original,bad,&created);require(!e.ok()&&original.digest()==digest,"direct aromatic electron step rejected conservatively");}
    {
        Document ring;std::array<ObjectId,4> ids;
        for(int i=0;i<4;++i){
            ring.addAtom(Vec2(i*26,(i%2)*15),i?6:8,&ids[i]);
            if(i)ring.addBond(ids[i-1],ids[i],nullptr);
        }
        for(int i:{0,3}){auto sem=ring.atoms().alive(ids[i])->semantics;sem.charge=i?1:-1;ring.setAtomSemantics(ids[i],sem);}
        std::vector<ObjectRef> created;
        auto e=mechanism::generateNextStep(ring,{{ObjectRef(ObjectKind::Atom,ids[0]),ObjectRef(ObjectKind::Atom,ids[3])}},&created);
        require(e.ok(),"extended chain closes as a regular four-membered ring");
        std::map<ObjectId,ObjectId> copies;
        for(auto ref:created)if(ref.kind==ObjectKind::Atom)copies[studio_mechanism_products::sourceAtom(ring,ref.id)]=ref.id;
        for(int i=0;i<4;++i){
            const auto a=copies.at(ids[i]),b=copies.at(ids[(i+1)%4]),c=copies.at(ids[(i+2)%4]);
            auto bond=ring.bonds().alive(ring.bondBetween(a,b));require(bond&&bond->order==BondOrder::Single,"oxetane connectivity");
            const auto ab=ring.atoms().alive(a)->position-ring.atoms().alive(b)->position,cb=ring.atoms().alive(c)->position-ring.atoms().alive(b)->position;
            require(std::abs(length(ab)-std::hypot(26.,15.))<1e-5&&std::abs(dot(ab,cb))<1e-5,"no stretched side or folded angle");
            require(ring.atoms().alive(a)->semantics.charge==0&&ring.atoms().alive(ids[i])->position==Vec2(i*26,(i%2)*15),"neutral product and untouched source");
        }
    }
    // Identical arrows would transfer the same electron pair twice.
    {auto bad=moves;bad.push_back(moves.front());auto digest=s.doc.digest();std::vector<ObjectRef> created;auto e=mechanism::generateNextStep(s.doc,bad,&created);require(!e.ok()&&s.doc.digest()==digest,"duplicate electron movement rejected atomically");}
    {auto digest=s.doc.digest();std::vector<ObjectRef> created;std::vector<mechanism::ElectronMove> bad={{ObjectRef(ObjectKind::Atom,999999),A(5)}};auto e=mechanism::generateNextStep(s.doc,bad,&created);require(!e.ok()&&s.doc.digest()==digest,"dead semantic reference rejected atomically");}
    {Document ambiguous;ObjectId a,b,proton,pi;ambiguous.addAtom(Vec2(0,0),6,&a);ambiguous.addAtom(Vec2(30,0),6,&b);ambiguous.addAtom(Vec2(70,0),1,&proton);ambiguous.addBond(a,b,&pi);ambiguous.setBondOrder(pi,BondOrder::Double);auto sem=ambiguous.atoms().get(proton)->semantics;sem.charge=1;ambiguous.setAtomSemantics(proton,sem);auto digest=ambiguous.digest();std::vector<ObjectRef> created;auto e=mechanism::generateNextStep(ambiguous,{{ObjectRef(ObjectKind::Bond,pi),ObjectRef(ObjectKind::Atom,proton)}},&created);require(!e.ok()&&ambiguous.digest()==digest,"ambiguous alkene terminus is not guessed");}
    {arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,arrow[0],&p,&ok);auto reverse=arrows::reversedArrow(p);require(ok&&reverse.mechanism&&reverse.electronSource==p.electronTarget&&reverse.electronTarget==p.electronSource,"arrow reversal swaps electron source and target");}
    {auto digest=s.doc.digest();s.selected.clear();generateMechanismStep();require(s.doc.digest()==digest,"empty selection does not use unrelated arrows");}
    {arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,arrow[0],&p,&ok);require(ok,"read fishhook fixture");p.head.sides=arrows::HeadSides::Left;arrows::writeArrow(s.doc,arrow[0],p,.6);auto digest=s.doc.digest();s.selected=s.all();generateMechanismStep();require(s.doc.stats().aliveAtoms==8&&s.doc.digest()==digest,"unpaired single electron arrow cannot invent an ionic aldol product");p.head.sides=arrows::HeadSides::Both;arrows::writeArrow(s.doc,arrow[0],p,.6);}
    // Reimport compacts slab IDs: anchors must follow the actual atoms and bonds.
    require(s.open(dir+"/aldol-input.cdxml").ok(),"reopen original semantic arrows");
    std::vector<ObjectId> loadedAtoms,loadedBonds;for(const auto& a:s.doc.atoms().records())if(a.isAlive())loadedAtoms.push_back(a.id);for(const auto& b:s.doc.bonds().records())if(b.isAlive())loadedBonds.push_back(b.id);
    require(loadedAtoms.size()==8&&loadedBonds.size()==6,"reopened source counts");
    std::vector<mechanism::ElectronMove> restoredMoves;for(const auto& a:s.doc.arrows().records())if(a.isAlive()){arrows::ArrowParams p;bool ok=false;arrows::readArrow(s.doc,a.id,&p,&ok);if(p.mechanism){require(ok&&arrows::mechanismEndpointsValid(s.doc,p),"roundtrip semantic references valid");restoredMoves.push_back({p.electronSource,p.electronTarget});}}
    require(restoredMoves.size()==3,"all three semantic arrows survive CDXML");
    s.selected=s.all();generateMechanismStep();require(s.doc.stats().aliveAtoms==16&&s.doc.stats().aliveBonds==13,"reopened arrows generate complete aldol product");
    const auto userTrial=dir+"/carbonyl-user-input.r62doc";
    if(std::ifstream(userTrial,std::ios::binary).good()){
        require(s.open(userTrial).ok(),"open exact user carbonyl trial");
        s.selected={ObjectRef(ObjectKind::Arrow,4),ObjectRef(ObjectKind::Arrow,5)};
        const auto before=s.doc.digest();
        generateMechanismStep();
        int atoms=0,bonds=0,radicals=0,hydrogens=0;
        for(auto ref:s.selected){
            if(ref.kind==ObjectKind::Atom){
                ++atoms;auto a=s.doc.atoms().alive(ref.id);
                require(!a->semantics.charge,"exact carbonyl product is neutral");
                if(a->semantics.radical){require(a->element==8,"new radical is on oxygen");++radicals;}
                hydrogens+=chemistry::refreshAtomDerived(s.doc,ref.id,{}).newHydrogenCount;
            }else if(ref.kind==ObjectKind::Bond){
                ++bonds;require(s.doc.bonds().alive(ref.id)->order==BondOrder::Single,"product carbonyl is a single bond");
            }
        }
        require(atoms==8&&bonds==7&&radicals==1&&hydrogens==15,"exact user trial produces C7H15O with one oxygen radical");
        arrows::ArrowParams split,attack;bool splitOk=false,attackOk=false;
        require(arrows::readArrow(s.doc,4,&split,&splitOk).ok()&&splitOk&&
                arrows::readArrow(s.doc,5,&attack,&attackOk).ok()&&attackOk&&
                split.head.sides!=arrows::HeadSides::Both&&attack.electronTarget==ObjectRef(ObjectKind::Atom,19),
                "exact trial stores the approved hook and carbonyl carbon binding");
        require(s.save(dir+"/carbonyl-user-result.r62doc",false).ok(),"save corrected user trial separately");
        require(s.undo().ok()&&s.doc.digest()==before,"one undo restores both original arrows and removes product");
    }
    checkTextbookMechanisms(*this,dir);
    std::ofstream(dir+"/mechanism-step-checks.json")<<"{\"aldol_graph\":true,\"charge\":true,\"hydrogens\":true,\"reactants_unchanged\":true,\"undo_redo\":true,\"atomic_rejection\":true,\"semantic_roundtrip\":true}";
    s.selected.clear();s.fit();invalidate();s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
