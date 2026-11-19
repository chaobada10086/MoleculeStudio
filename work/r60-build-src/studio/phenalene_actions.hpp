#pragma once
// 1H-phenalene (C13H10): four fusion carbons, one peripheral CH2 (atom 7),
// eight aromatic CH.  This graph and its Kekule assignment match the
// RDKit/InChI structure; the central carbon is quaternary sp2 and carries
// one double bond, not an extra hydrogen.
ObjectId App::insertPhenalene(Vec2 center){
    const double L=s.doc.defaultBondLength()>0?s.doc.defaultBondLength():14.4;
    const double k=L/1.5;
    const Vec2 local[13]={
        Vec2(3.0,0.0),Vec2(2.25,-1.299),Vec2(0.75,-1.299),Vec2(0.0,-2.598),
        Vec2(-1.5,-2.598),Vec2(-2.25,-1.299),Vec2(-1.5,0.0),Vec2(-2.25,1.299),
        Vec2(-1.5,2.598),Vec2(0.0,2.598),Vec2(0.75,1.299),Vec2(2.25,1.299),
        Vec2(0.0,0.0)};
    std::vector<Vec2> points;points.reserve(13);
    for(const Vec2& p:local)points.push_back(center+p*k);
    struct E{int a,b;BondOrder order;};
    const E edges[15]={
        {0,1,BondOrder::Single},{1,2,BondOrder::Double},{2,3,BondOrder::Single},
        {3,4,BondOrder::Double},{4,5,BondOrder::Single},{5,6,BondOrder::Double},
        {6,7,BondOrder::Single},{7,8,BondOrder::Single},{8,9,BondOrder::Double},
        {9,10,BondOrder::Single},{10,11,BondOrder::Single},{10,12,BondOrder::Double},
        {11,0,BondOrder::Double},{12,2,BondOrder::Single},{12,6,BondOrder::Single}};
    auto created=std::make_shared<std::vector<ObjectId>>();
    auto error=s.submit(Command("插入 1H-菲烯",kInvalidateStructure|kInvalidateGeometry|kInvalidateSemantics|kInvalidateAppearance,
        [points,edges,created](CommandContext& c)->CommandResult{
            created->clear();created->reserve(points.size());
            for(Vec2 p:points){ObjectId id=kInvalidObjectId;auto e=c.document.addAtom(p,6,&id);if(!e.ok())return CommandResult::fail(e.code,e.message);created->push_back(id);}
            for(const E& edge:edges){
                BondRecord record;record.beginAtom=(*created)[size_t(edge.a)];record.endAtom=(*created)[size_t(edge.b)];
                record.order=edge.order;record.display=BondDisplay::Normal;record.semantics=BondSemantics::Normal;record.stereo=BondStereo::None;
                ObjectId id=kInvalidObjectId;auto e=c.document.addBondRecord(record,&id);if(!e.ok())return CommandResult::fail(e.code,e.message);
            }
            return CommandResult::success();
        }),"插入 1H-菲烯");
    if(!error.ok()){this->error(error);return kInvalidObjectId;}
    s.selected.clear();for(ObjectId id:*created)s.selected.push_back(ObjectRef(ObjectKind::Atom,id));
    s.refresh();status="已插入 1H-菲烯 · 可拖动或缩放";invalidate();return created->empty()?kInvalidObjectId:(*created)[0];
}
void App::cancelPhenalenePlacement(){if(!phenalenePlacing)return;phenalenePlacing=false;status="已取消放置 1H-菲烯";updateCanvasCursor();invalidate();}
