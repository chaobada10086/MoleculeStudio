// Task 3 acceptance: 结构转命名 auto-groups the caption with its structure and
// raises a red frame + hover message once that structure changes.
{
    auto reset=[this](Session& session){
        session.cancel();session.doc=Document();drawingSettings.apply(session.doc);
        session.history.clear();session.selected.clear();session.sourcePath.clear();
        session.input.bindDocument(session.doc.id());session.refresh();
    };
    auto linkedCaption=[](const Session& session)->ObjectId{
        for(const auto& text:session.doc.texts().records())if(text.isAlive()&&namelink::hasLink(session.doc,text.id))return text.id;
        return kInvalidObjectId;
    };
    auto owningGroup=[](const Session& session,ObjectId text)->ObjectId{
        for(const auto& group:session.doc.groups().records())if(group.isAlive()&&std::find(group.members.begin(),group.members.end(),ObjectRef(ObjectKind::Text,text))!=group.members.end())return group.id;
        return kInvalidObjectId;
    };
    auto textBox=[](const Session& session,ObjectId text)->Box{
        Box bounds(Vec2(1,1),Vec2(-1,-1));
        for(const auto& primitive:session.scene.primitives())if(primitive.source==ObjectRef(ObjectKind::Text,text)&&!primitive.bounds.empty()){bounds.add(primitive.bounds.min);bounds.add(primitive.bounds.max);}
        if(bounds.empty()){const TextRecord* record=session.doc.texts().alive(text);if(record)bounds=Box(record->position,record->position+Vec2(24,12));}
        return bounds;
    };
    auto redFrames=[&](Session& session,Box* frame)->size_t{
        size_t count=0;Box bounds(Vec2(1,1),Vec2(-1,-1));
        auto overlay=session.overlay();
        for(const auto& primitive:overlay.scene().primitives()){
            if(!primitive.style.stroke.enabled||primitive.style.stroke.rgba!=0xE02020FF)continue;
            ++count;
            for(size_t k=0;k<primitive.points.size();++k){if(bounds.empty())bounds=Box(primitive.points[k],primitive.points[k]);else bounds.add(primitive.points[k]);}
        }
        if(frame)*frame=bounds;
        return count;
    };
    auto buildPair=[&](Session& session)->ObjectId{
        reset(session);
        ObjectId left,right,bond;
        require(session.doc.addAtom(Vec2(0,0),6,&left).ok(),"naming pair atom");
        require(session.doc.addAtom(Vec2(30,0),6,&right).ok(),"naming pair atom");
        require(session.doc.addBond(left,right,&bond).ok(),"naming pair bond");
        session.refresh();
        std::vector<ObjectRef> refs;
        refs.push_back(ObjectRef(ObjectKind::Atom,left));
        refs.push_back(ObjectRef(ObjectKind::Atom,right));
        refs.push_back(ObjectRef(ObjectKind::Bond,bond));
        auto id=std::make_shared<ObjectId>(kInvalidObjectId);
        auto submitted=session.submit(Command("命名结构测试",kInvalidateStructure|kInvalidateAppearance,[refs,id](CommandContext& context){
            auto e=studioNaming::createCaption(context.document,"ethane",Vec2(0,-45),refs,id.get());
            return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
        }),"命名结构测试");
        require(submitted.ok(),("create a linked naming pair: "+submitted.message).c_str());
        session.refresh();
        return *id;
    };

    // ---- End to end: the real 结构转命名 action on a drawn structure. --------
    reset(s);
    s.activateCommand("chooseRingTool_Benzene");s.gesture(PointerPhase::Down,Vec2(),0);s.gesture(PointerPhase::Up,Vec2(),0);
    require(s.doc.stats().aliveAtoms==6,"structure-to-name fixture benzene");
    s.selected=s.all();s.refresh();
    require(chemistryAction("structureToName"),"structure-to-name action runs");
    ObjectId caption=kInvalidObjectId;
    for(const auto& text:s.doc.texts().records())if(text.isAlive())caption=text.id;
    require(caption!=kInvalidObjectId,("structure-to-name inserts a caption: "+status).c_str());
    require(namelink::hasLink(s.doc,caption),"the caption stores its structure link");
    require(!namelink::stale(s.doc,caption),"a fresh caption is not stale");
    const ObjectId group=owningGroup(s,caption);
    require(group!=kInvalidObjectId,"the caption and its structure share a GroupRecord");
    require(!s.doc.groups().get(group)->members.empty()&&s.doc.groups().get(group)->members.size()>=7,"the naming group holds the six atoms and the caption");
    bool atomMember=false;for(size_t k=0;k<s.doc.groups().get(group)->members.size();++k)if(s.doc.groups().get(group)->members[k].kind==ObjectKind::Atom)atomMember=true;
    require(atomMember,"the naming group contains its structure");
    require(!selection::groupsContaining(s.doc,ObjectRef(ObjectKind::Text,caption)).empty(),"selecting the caption resolves to the naming group");

    // Moving the pair is not a structural change.
    const Vec2 captionPosition=s.doc.texts().get(caption)->position;
    const uint64_t digestBeforeMove=s.doc.digest();
    require(s.move({ObjectRef(ObjectKind::Group,group)},Vec2(10,5)).ok(),"move the naming group as one unit");
    require(core::length(s.doc.texts().get(caption)->position-captionPosition-Vec2(10,5))<1e-6,"the caption moves with the structure");
    require(!namelink::stale(s.doc,caption),"moving the pair keeps the link current");
    require(s.undo().ok()&&s.doc.digest()==digestBeforeMove,"the group move is one undo step");

    // ---- Editing the structure: red frame + hover message. ------------------
    ObjectId linkedAtom=kInvalidObjectId;
    for(size_t k=0;k<s.doc.groups().get(group)->members.size();++k)if(s.doc.groups().get(group)->members[k].kind==ObjectKind::Atom){linkedAtom=s.doc.groups().get(group)->members[k].id;break;}
    require(linkedAtom!=kInvalidObjectId,"the naming group exposes a structure atom");
    require(s.submit(commands::setElements({linkedAtom},7),"naming structure edit").ok(),"edit the linked structure");
    require(namelink::stale(s.doc,caption),"editing the structure marks the caption stale");
    s.refresh();
    const Box captionBox=textBox(s,caption);
    require(!captionBox.empty(),"the stale caption has a measured box");
    Box redFrame;
    require(redFrames(s,&redFrame)>=1,"a stale caption draws a red frame");
    require(redFrame.min.x<=captionBox.min.x+.01&&redFrame.max.x>=captionBox.max.x-.01&&redFrame.min.y<=captionBox.min.y+.01&&redFrame.max.y>=captionBox.max.y-.01,"the red frame encloses the caption");
    auto view=s.view();view.scale*=3;s.setView(view);
    Box zoomedFrame;
    require(redFrames(s,&zoomedFrame)>=1&&std::abs(zoomedFrame.min.x-redFrame.min.x)<1e-6&&std::abs(zoomedFrame.max.y-redFrame.max.y)<1e-6,"the red frame is document-space and follows zoom");
    view.scale/=3;s.setView(view);
    const Vec2 hoverPoint=(captionBox.min+captionBox.max)*.5;
    require(nameLinkTipAt(hoverPoint)==std::string(namelink::staleMessage()),"hovering the stale caption reports the change");
    updateNameLinkTip(hoverPoint);
    require(nameLinkTipText==W(namelink::staleMessage()),"the canvas tooltip is wired to the stale caption");
    updateNameLinkTip(Vec2(1e6,1e6));
    require(nameLinkTipText.empty(),"moving away hides the change message");
    require(s.undo().ok()&&!namelink::stale(s.doc,caption),"undo clears the stale state");
    require(nameLinkTipAt(hoverPoint).empty(),"no change message after undo");
    require(s.undo(true).ok()&&namelink::stale(s.doc,caption),"redo restores the stale state");
    require(s.undo().ok(),"undo the redo");

    // ---- Re-naming replaces the caption, so the red frame cannot linger. ----
    const std::vector<ObjectRef> currentRefs=namelink::linkRefs(s.doc,caption);
    require(s.submit(Command("重新命名测试",kInvalidateAppearance|kInvalidateGeometry,[currentRefs](CommandContext& context){
        auto e=studioNaming::createCaption(context.document,"benzene",Vec2(0,-60),currentRefs,0);
        return e.ok()?CommandResult::success():CommandResult::fail(e.code,e.message);
    }),"重新命名测试").ok(),"re-name the linked structure");
    s.refresh();
    const ObjectId renamed=linkedCaption(s);
    require(renamed!=kInvalidObjectId&&!namelink::stale(s.doc,renamed),"re-naming clears the stale state");
    require(s.doc.stats().aliveTexts==1,"re-naming replaces the caption instead of stacking a second one");
    require(redFrames(s,0)==0,"no red frame remains after re-naming");
    require(s.undo().ok(),"re-name undo");
    {const ObjectId restored=linkedCaption(s);require(restored!=kInvalidObjectId&&!namelink::stale(s.doc,restored),"undo restores the previous caption and its current link");}

    // ---- Deleting the group deletes the pair as one unit. -------------------
    ObjectId pairCaption=buildPair(s);
    ObjectId pairGroup=owningGroup(s,pairCaption);
    require(pairGroup!=kInvalidObjectId,"the pair group exists");
    require(s.submit(commands::deleteObjects({ObjectRef(ObjectKind::Group,pairGroup)}),"delete naming group").ok(),"delete the naming group");
    require(s.doc.stats().aliveAtoms==0&&s.doc.stats().aliveTexts==0,"deleting the naming group deletes the structure and caption");
    require(s.undo().ok()&&s.doc.stats().aliveAtoms==2&&s.doc.stats().aliveTexts==1,"deleting the naming group is one undo step");

    // ---- Persistence: native and CDXML reload keep the association. ---------
    pairCaption=linkedCaption(s);
    require(s.save(testOut+"/name_link.r62doc").ok(),"save the linked naming pair (native)");
    Session native;auto opened=native.open(testOut+"/name_link.r62doc");
    require(opened.ok(),("reload the linked naming pair (native): "+opened.message).c_str());
    const ObjectId nativeCaption=linkedCaption(native);
    require(nativeCaption!=kInvalidObjectId,"the structure link survives a native reload");
    require(!namelink::stale(native.doc,nativeCaption),"the reloaded link is current");
    require(owningGroup(native,nativeCaption)!=kInvalidObjectId,"the naming group survives a native reload");
    ObjectId nativeAtom=kInvalidObjectId;
    for(const auto& atom:native.doc.atoms().records())if(atom.isAlive()){nativeAtom=atom.id;break;}
    require(native.submit(commands::setElements({nativeAtom},8),"native link edit").ok()&&namelink::stale(native.doc,nativeCaption),"a structure edit after a native reload marks the caption stale");

    require(s.save(testOut+"/name_link.cdxml",true).ok(),"save the linked naming pair (CDXML)");
    Session cdxml;opened=cdxml.open(testOut+"/name_link.cdxml");
    require(opened.ok(),("reload the linked naming pair (CDXML): "+opened.message).c_str());
    const ObjectId cdxmlCaption=linkedCaption(cdxml);
    require(cdxmlCaption!=kInvalidObjectId,"the structure link survives a CDXML round trip");
    require(!namelink::stale(cdxml.doc,cdxmlCaption),"the CDXML link is current");
    require(owningGroup(cdxml,cdxmlCaption)!=kInvalidObjectId,"the naming group is rebuilt after a CDXML import");
    ObjectId cdxmlAtom=kInvalidObjectId;
    for(const auto& atom:cdxml.doc.atoms().records())if(atom.isAlive()){cdxmlAtom=atom.id;break;}
    require(cdxml.submit(commands::setElements({cdxmlAtom},8),"cdxml link edit").ok()&&namelink::stale(cdxml.doc,cdxmlCaption),"a structure edit after a CDXML import marks the caption stale");

    // ---- Ungrouping dissolves the association and clears the frame. ---------
    pairCaption=linkedCaption(s);pairGroup=owningGroup(s,pairCaption);
    require(pairGroup!=kInvalidObjectId,"pair group before ungroup");
    s.selected={ObjectRef(ObjectKind::Group,pairGroup)};
    require(arrangeAction("ungroup"),"ungroup the naming pair");
    require(!namelink::hasLink(s.doc,pairCaption),"ungrouping clears the structure link");
    require(!namelink::stale(s.doc,pairCaption),"no stale frame after ungrouping");
    require(nameLinkTipAt(textBox(s,pairCaption).min).empty(),"no hover message after ungrouping");
    require(s.save(testOut+"/name_link_ungrouped.r62doc").ok()&&!namelink::hasLink(s.doc,pairCaption),"the cleared link is what a later save stores");

    std::ofstream(testOut+"/name-link-checks.json")<<"{\"auto_group\":true,\"link_digest\":true,\"group_move\":true,\"red_frame\":true,\"tooltip\":true,\"zoom_stable\":true,\"undo_redo\":true,\"rename_replaces\":true,\"delete_group\":true,\"native_reload\":true,\"cdxml_reload\":true,\"ungroup_clears\":true}";
}