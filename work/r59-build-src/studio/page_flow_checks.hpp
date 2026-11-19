#pragma once
static void checkMechanismPageFlow(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("page flow: ")+why);};
    Document d;studioio::Page page;page.width=500;page.height=140;page.x=0;page.y=0;page.store(d);
    ObjectId source,blocker,product;d.addAtom(Vec2(430,80),8,&source);d.addAtom(Vec2(40,112),8,&blocker);d.addAtom(Vec2(700,80),8,&product);
    auto p=arrows::resolvePreset(8,Vec2(550,80),Vec2(650,80),.6);ObjectId arrow;arrows::addArrow(d,p,.6,&arrow);
    std::vector<ObjectRef> made{{ObjectKind::Atom,product},{ObjectKind::Arrow,arrow}},existing{{ObjectKind::Atom,source},{ObjectKind::Atom,blocker}};
    auto before=d.atoms().alive(product)->position;auto row=selection::selectionBounds(d,{{ObjectKind::Atom,source}});bool wrapped=false;
    require(studio_mechanism_page_flow::place(d,made,existing,row,&wrapped).ok()&&wrapped,"overflow creates a lower row");
    auto bounds=selection::selectionBounds(d,made);auto occupied=selection::selectionBounds(d,{{ObjectKind::Atom,blocker}});
    require(std::abs(bounds.min.x-24)<1e-6&&bounds.min.y>occupied.max.y+30,"lower row starts at left margin and skips occupied content");
    auto actual=studioio::Page::get(d);require(actual.across==1&&actual.down>1,"page grows down rather than right");
    require(d.atoms().alive(source)->position==Vec2(430,80)&&d.atoms().alive(blocker)->position==Vec2(40,112),"existing drawing is stationary");
    arrows::ArrowParams moved;bool ok=false;arrows::readArrow(d,arrow,&moved,&ok);auto delta=d.atoms().alive(product)->position-before;
    require(ok&&length(moved.begin-p.begin-delta)<1e-8&&length(moved.end-p.end-delta)<1e-8,"reaction arrow and product move together");
    auto once=d.digest();wrapped=true;require(studio_mechanism_page_flow::place(d,made,existing,row,&wrapped).ok()&&!wrapped&&d.digest()==once,"placing a fitting row again is stable");
    app.s.cancel();app.s.doc=d;app.s.history.clear();app.s.selected.clear();app.s.refresh();app.s.fit();app.invalidate();app.screenshot(dir+"/page-flow.png");require(app.s.save(dir+"/page-flow.cdxml",true).ok(),"save vertical page layout");Session reopened;require(reopened.open(dir+"/page-flow.cdxml").ok()&&studioio::Page::get(reopened.doc).across==1&&studioio::Page::get(reopened.doc).down==actual.down,"vertical layout persists");
    std::ofstream(dir+"/page-flow-checks.json")<<"{\"wrap_left\":true,\"skip_occupied_row\":true,\"grow_down\":true,\"existing_fixed\":true,\"arrow_product_rigid\":true,\"roundtrip\":true}";
}
