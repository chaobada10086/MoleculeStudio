#pragma once
static void checkMechanismPageFlow(App& app,const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("page flow: ")+why);};
    Document d;studioio::Page page;page.width=500;page.height=140;page.x=0;page.y=0;page.store(d);
    ObjectId source,blocker,product;d.addAtom(Vec2(420,80),8,&source);d.addAtom(Vec2(40,112),8,&blocker);d.addAtom(Vec2(700,80),8,&product);
    auto p=mechanism::nextStepReactionArrow(d,532,55);ObjectId arrow;arrows::addArrow(d,p,.6,&arrow);
    ObjectId caption;d.addText((p.begin+p.end)*.5+Vec2(0,-12),10,"-H", &caption);
    const ObjectRef captionRef(ObjectKind::Text,caption);
    d.extensions(captionRef).set(studio_mechanism_products::arrowSideKey,"1");
    const auto captionBefore=d.texts().alive(caption)->position;
    std::vector<ObjectRef> made{{ObjectKind::Atom,product},{ObjectKind::Arrow,arrow},captionRef},existing{{ObjectKind::Atom,source},{ObjectKind::Atom,blocker}};
    std::vector<ObjectRef> row{{ObjectKind::Atom,source}};bool wrapped=false;
    require(studio_mechanism_page_flow::alignStep(d,d,made).ok(),"center the connector before placing the row");
    require(studio_mechanism_page_flow::place(d,made,existing,row,&wrapped).ok()&&wrapped,"overflow creates a lower row");
    studio_mechanism_page_flow::InkBounds ink(d);
    auto bounds=ink.bounds({{ObjectKind::Atom,product}});auto occupied=ink.bounds({{ObjectKind::Atom,blocker}});
    require(std::abs(bounds.min.x-24)<1e-6&&bounds.min.y>occupied.max.y+30,"lower row starts at left margin and skips occupied content");
    auto actual=studioio::Page::get(d);require(actual.across==1&&actual.down>1,"page grows down rather than right");
    require(d.atoms().alive(source)->position==Vec2(420,80)&&d.atoms().alive(blocker)->position==Vec2(40,112),"existing drawing is stationary");
    arrows::ArrowParams moved;bool ok=false;arrows::readArrow(d,arrow,&moved,&ok);const auto arrowDelta=moved.begin-p.begin;
    require(ok&&moved.begin.y==80&&moved.end.y==80&&moved.begin.x>420&&moved.end.x<500,
            "reaction arrow stays centered on the previous row");
    require(std::abs(length(moved.end-moved.begin)-2*d.defaultBondLength())<1e-8,"reaction arrow follows the two-bond proportion");
    require(length(d.texts().alive(caption)->position-captionBefore-arrowDelta)<1e-8,"caption follows arrow on the previous row");
    auto once=d.digest();wrapped=true;require(studio_mechanism_page_flow::place(d,made,existing,row,&wrapped).ok()&&!wrapped&&d.digest()==once,"placing a fitting row again is stable");
    app.s.cancel();app.s.doc=d;app.s.history.clear();app.s.selected.clear();app.s.refresh();app.s.fit();app.invalidate();app.screenshot(dir+"/page-flow.png");require(app.s.save(dir+"/page-flow.cdxml",true).ok(),"save vertical page layout");Session reopened;require(reopened.open(dir+"/page-flow.cdxml").ok()&&studioio::Page::get(reopened.doc).across==1&&studioio::Page::get(reopened.doc).down==actual.down,"vertical layout persists");
    std::ofstream(dir+"/page-flow-checks.json")<<"{\"wrap_left\":true,\"skip_occupied_row\":true,\"grow_down\":true,\"existing_fixed\":true,\"arrow_on_previous_row\":true,\"caption_follows_arrow\":true,\"roundtrip\":true}";
}
