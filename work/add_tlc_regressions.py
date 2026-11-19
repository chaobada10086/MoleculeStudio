from pathlib import Path
p=Path('studio/tlc_independent_checks.inc');s=p.read_text(encoding='utf-8');needle='    CheckDlgButton(popup,207,BST_CHECKED);';i=s.index(needle)
s=s[:i]+'''    // Exercise off-column preview dragging at both vertical boundaries.
    {auto draft=d->figure;auto selected=d->spotId;auto lane=d->laneId;
     auto between=d->point(board,d->figure.lanes[2].u+.04,.99);d->dragPoint(int(between.X),int(between.Y));
     auto moved=r62::experimental::findSpot(d->figure,selected);passed&=moved&&moved->laneId==d->figure.lanes[2].id&&std::abs(moved->u-d->figure.lanes[2].u)<1e-8&&std::abs(studio_tlc::rf(d->figure,*moved))<1e-8;
     auto above=d->point(board,d->figure.lanes[2].u,-.99);d->dragPoint(int(above.X),int(above.Y));moved=r62::experimental::findSpot(d->figure,selected);passed&=std::abs(studio_tlc::rf(d->figure,*moved)-1)<1e-8;
     d->figure=draft;d->spotId=selected;d->laneId=lane;d->refresh();}
    CheckDlgButton(popup,234,BST_CHECKED);SetDlgItemTextW(popup,232,L"30");SetDlgItemTextW(popup,233,L"70");
    spot=r62::experimental::findSpot(d->figure,d->spotId);passed&=spot&&std::abs(spot->tailLengthV-.6)<1e-8&&std::abs(spot->tailOpacity-.7)<1e-8;
'''+s[i:];p.write_text(s,encoding='utf-8')
p=Path('studio/tlc_checks.inc');s=p.read_text(encoding='utf-8');s=s.replace('    s.selected.clear();auto saved=s.save', '''    bool visibleTail=false;for(const auto& primitive:s.scene.primitives())if(primitive.source==figureRef(plate)&&primitive.z==15&&primitive.style.fill.enabled&&(primitive.style.fill.rgba&255u)>40u)visibleTail=true;require(visibleTail,"tail has visible canvas geometry in active renderer");
    s.selected.clear();auto saved=s.save''');s=s.replace('require(direct.gesture(PointerPhase::Down,from).ok()&&direct.gesture(PointerPhase::Move,to).ok()&&direct.gesture(PointerPhase::Up,to).ok(),"drag existing spot");','''require(direct.gesture(PointerPhase::Down,from).ok()&&direct.gesture(PointerPhase::Move,to).ok(),"preview spot drag");auto ghost=direct.overlay();bool snapped=false;for(const auto& item:ghost.scene().primitives())if(std::abs((item.bounds.min.x+item.bounds.max.x)*.5-bounds.pointAt(board.lanes[2].u,0).x)<1e-6)snapped=true;require(snapped,"drag preview snaps to column");require(direct.gesture(PointerPhase::Up,to).ok(),"commit spot drag");''');p.write_text(s,encoding='utf-8')
