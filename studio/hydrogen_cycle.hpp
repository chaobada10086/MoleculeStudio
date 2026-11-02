#pragma once
bool App::cycleAtomHydrogens(ObjectId id,bool initialize,int replacement){
    auto atom=s.doc.atoms().alive(id);if(!atom)return false;int target=replacement?replacement:atom->element;if(target!=7&&target!=8)return false;
    int charge=atom->semantics.charge;if(charge<-1||charge>1){status="当前价态暂不支持自动循环氢数";return true;}
    auto label=s.doc.atomLabel(id);std::string element=core::elementSymbol(target);
    bool prefix=false;size_t letters=0;if(!initialize&&!label.empty()&&label!=element&&!r62::integration::molecular::hydrideLabel(target,label,&prefix,&letters)){status="此原子使用自定义标签，先恢复为 O 或 N 再调整氢数";return true;}
    int sum=0,aromatic=0;std::vector<ObjectId> neighbors;
    for(auto bid:s.doc.incidentBonds(id)){auto bond=s.doc.bonds().alive(bid);if(!bond)continue;if(bond->semantics==BondSemantics::Hydrogen)continue;
        if(bond->semantics!=BondSemantics::Normal&&bond->semantics!=BondSemantics::Aromatic){status="查询键或特殊配位键的氢数请手动设置";return true;}
        if(bond->order==BondOrder::Aromatic||(bond->semantics==BondSemantics::Aromatic&&bond->order==BondOrder::Single)){++sum;++aromatic;}else if(bond->order>=BondOrder::Single&&bond->order<=BondOrder::Triple)sum+=int(bond->order);else {status="当前键型无法确定可连接的氢数";return true;}
        neighbors.push_back(bond->beginAtom==id?bond->endAtom:bond->beginAtom);
    }
    // Aromatic N in a five-membered donor ring can carry H. Pyridine-like
    // neutral N uses its remaining valence for the aromatic pi system.
    if(target==7&&aromatic>=2){bool five=false;if(neighbors.size()==2){std::set<ObjectId> visited{id,neighbors[0]};std::function<void(ObjectId,int)> walk=[&](ObjectId at,int left){if(at==neighbors[1]){if(left==0)five=true;return;}if(!left)return;for(auto bid:s.doc.incidentBonds(at)){auto b=s.doc.bonds().alive(bid);if(!b)continue;auto next=b->beginAtom==at?b->endAtom:b->beginAtom;if(visited.count(next))continue;visited.insert(next);walk(next,left-1);visited.erase(next);}};walk(neighbors[0],3);}if(!five||charge>0)++sum;}
    int limit=(target==8?2:3)+charge-int(atom->semantics.radical!=0);int maximum=std::max(0,limit-sum);
    if(sum>limit){status="当前原子已超价，不能继续增加氢";return true;}
    int current=atom->semantics.implicitHydrogens<0?maximum:atom->semantics.implicitHydrogens;int next=initialize?std::min(maximum,target==8?1:2):(current>=maximum?0:current+1);
    auto base=nicknames::labelCommand(id,element);
    auto result=s.submit(Command("循环氧氮氢数",kInvalidateStructure|kInvalidateSemantics|kInvalidateGeometry|kInvalidateAppearance,[id,next,element,initialize,base](CommandContext& c){if(initialize){auto result=base.execute(c);if(!result.ok())return result;}auto atom=c.document.atoms().alive(id);if(!atom)return CommandResult::fail(ErrorCode::NotFound,"原子不存在");auto sem=atom->semantics;sem.implicitHydrogens=next;sem.showLabel=true;auto e=c.document.setAtomSemantics(id,sem);if(e.ok())e=c.document.setAtomLabel(id,element);if(e.ok()){auto& bag=c.document.extensions(ObjectRef(ObjectKind::Atom,id));bag.set("studio.hydrogens.manual","1");bag.erase("studio.atom.runs");bag.erase("studio.label.text");}return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);}),"循环氧氮氢数");
    if(result.ok())status=element+"：氢数 "+std::to_string(next)+"（可用范围 0–"+std::to_string(maximum)+"） · 同元素键或 H 继续循环 · Ctrl+Z 撤销";else error(result);return true;
}

bool App::preferAtomHydride(ObjectId id,int element){
    auto atom=s.doc.atoms().alive(id);if(!atom||(element!=7&&element!=8))return false;
    bool first=false;size_t letters=0;auto label=s.doc.atomLabel(id);
    bool explicitCount=atom->element==element&&(s.doc.extensions(ObjectRef(ObjectKind::Atom,id)).findValue("studio.hydrogens.manual")||r62::integration::molecular::hydrideLabel(element,label,&first,&letters));
    return cycleAtomHydrogens(id,!explicitCount,element);
}
