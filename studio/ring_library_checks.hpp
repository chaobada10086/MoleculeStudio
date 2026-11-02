#pragma once
static bool ringCheckMenuSeen=false;
static void CALLBACK closeRingCheckMenu(HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND dialog=FindWindowW(L"MoleculeStudioRingLibrary",nullptr);DWORD process=0;if(dialog)GetWindowThreadProcessId(dialog,&process);
    if(dialog&&process==GetCurrentProcessId()){ringCheckMenuSeen=true;KillTimer(owner,timer);PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
}
void App::ringLibraryCheck(const std::string& dir){
    auto require=[](bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);};
    require(ring_library::parentCount==129&&ring_library::variantCount==131,"complete parent and variant counts");std::map<std::string,int> counts;
    for(const auto& p:ring_library::parents){++counts[p.category];require(p.firstVariant>=0&&p.firstVariant+p.variantCount<=ring_library::variantCount,"parent variant range");}
    int expected[]={35,12,6,5,6,6,20,20};for(int i=0;i<8;++i)require(counts[ring_library::categories[i]]==expected[i],"category counts match reviewed list");
    {std::set<std::wstring> keys;for(int i=0;i<ring_library::variantCount;++i){auto key=ringVariantKey(i);require(keys.insert(key).second,"unique persistent choice keys");for(auto c:key)require(c<128,"persistent choice keys survive ANSI and Unicode INI files");}}
    std::ofstream manifest(dir+"/native-catalog.tsv");manifest<<"variant\tparent\tform\tformula\tatoms\tbonds\n";
    for(int i=0;i<ring_library::variantCount;++i){
        s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());double L=i%2?22.5:14.4;s.doc.setDefaultBondLength(L);s.refresh();const auto& v=ring_library::variants[i];const auto& p=ring_library::parents[v.parent];
        auto before=s.doc.digest();ObjectId first=insertRingTemplate(i,Vec2(110,100));require(first!=kInvalidObjectId,std::string("insert ")+p.english);
        require(s.doc.stats().aliveAtoms==size_t(v.atomCount)&&s.doc.stats().aliveBonds==size_t(v.bondCount),std::string("graph size ")+p.english);
        auto f=studio::analyze(s.doc,s.selected);require(f.valid&&f.plain==v.formula,std::string("formula ")+p.english+": "+f.plain+" != "+v.formula+" "+f.note);
        for(int a=0;a<v.atomCount;++a){const auto& source=ring_library::atoms[v.firstAtom+a];auto atom=s.doc.atoms().get(ObjectId(a));require(length(atom->position-(Vec2(110,100)+Vec2(source.x,source.y)*L))<1e-7,"template coordinates and document bond length");for(int b=0;b<a;++b)require(length(atom->position-s.doc.atoms().get(ObjectId(b))->position)>.15*L,"distinct atom positions");}
        auto after=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==before,"one undo removes whole ring");require(s.undo(true).ok()&&s.doc.digest()==after,"redo restores exact ring");
        std::ostringstream filename;filename<<dir<<"/catalog_"<<std::setw(3)<<std::setfill('0')<<i<<".cdxml";require(s.save(filename.str(),true).ok(),"export template");Session back;require(back.open(filename.str()).ok(),"reopen template");auto formula=studio::analyze(back.doc,back.all());require(formula.valid&&formula.plain==v.formula,"template formula survives export/import");
        manifest<<i<<'\t'<<p.english<<'\t'<<v.title<<'\t'<<v.formula<<'\t'<<v.atomCount<<'\t'<<v.bondCount<<'\n';
    }
    int crownCount=0;for(const auto& p:ring_library::parents)if(std::string(p.category)=="冠醚"){++crownCount;const auto& v=ring_library::variants[p.firstVariant];int count=0;for(int i=0;i<v.atomCount;++i)if(ring_library::atoms[v.firstAtom+i].element==8)++count;int n=count*3;auto point=[&](int i){auto a=ring_library::atoms[v.firstAtom+(i+n)%n];return Vec2(a.x,a.y);};for(int i=0;i<n;++i)if(i%3){auto a=point(i-1)-point(i),b=point(i+1)-point(i);require(std::abs(length(a)-1)<1e-5&&std::abs(length(b)-1)<1e-5&&std::abs(a.x*b.x+a.y*b.y+.5)<1e-5,"crown chain-carbon corners are 120-degree zigzags");}}require(crownCount==5,"all five non-benzannulated crowns redrawn");
    require(RingLibraryPicker::matches(35,L"BCP"),"English alias search");require(RingLibraryPicker::matches(2,L"萘"),"Chinese search");require(RingLibraryPicker::matches(19,L"TETRACENE"),"case insensitive English search");
    s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();ringPreferencesLoaded=true;ringVariant=36;ringRecent.clear();auto before=s.doc.digest();
    beginRingPlacement();require(ringPlacing&&s.doc.digest()==before,"choosing a ring arms placement without document changes");SendMessageW(canvas,WM_KEYDOWN,VK_ESCAPE,0);require(!ringPlacing&&s.doc.digest()==before,"Escape cancels placement without changes");
    beginRingPlacement();auto point=s.view().toDevice(Vec2(100,100));SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(point.x),int(point.y)));SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(int(point.x),int(point.y)));require(s.doc.stats().aliveAtoms==5&&s.doc.stats().aliveBonds==6,"real canvas click inserts BCP");require(!ringPlacing&&ringRecent.size()==1&&ringRecent[0]==36,"successful placement records most recent variant");
    require(s.undo().ok()&&s.doc.digest()==before,"canvas placement is one undo");
    switchTab(5);layout();Button ringButton("","","","");bool found=false;for(auto& g:visibleGroups())for(auto& b:g.items)if(b.cmd=="@otherRings"){ringButton=b;found=true;}require(found,"replacement toolbar button present");auto location=MAKELPARAM(int((ringButton.r.X+ringButton.r.Width*.5)*scale),int((ringButton.r.Y+ringButton.r.Height*.5)*scale));
    ringCheckMenuSeen=false;SetTimer(h,9,30,closeRingCheckMenu);SendMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,location);SendMessageW(h,WM_LBUTTONUP,0,location);KillTimer(h,9);require(ringCheckMenuSeen&&!ringPlacing&&s.doc.digest()==before&&IsWindowEnabled(GetAncestor(h,GA_ROOT)),"single click opens real modal menu and cancel preserves document");
    ringCheckMenuSeen=false;SetTimer(h,9,30,closeRingCheckMenu);SendMessageW(h,WM_RBUTTONUP,0,location);KillTimer(h,9);require(ringCheckMenuSeen&&!ringPlacing&&IsWindowEnabled(GetAncestor(h,GA_ROOT)),"right click opens real modal menu and cancel restores owner");
    ringCheckMenuSeen=false;SetTimer(h,9,30,closeRingCheckMenu);SendMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,location);require(ringHoldPending,"press starts long-press timer");SendMessageW(h,WM_TIMER,4,0);KillTimer(h,9);require(ringCheckMenuSeen,"long-press timer opens real menu");SendMessageW(h,WM_LBUTTONUP,0,location);require(!ringPlacing&&s.doc.digest()==before,"long-press release does not invoke short click after cancel");
    std::ofstream(dir+"/ring-library-checks.json")<<"{\"parents\":129,\"variants\":131,\"categories\":10,\"crown_zigzag_layouts\":5,\"all_formulas\":true,\"distinct_positions\":true,\"bond_length_scaling\":true,\"all_native_exports\":true,\"undo_redo\":true,\"canvas_click\":true,\"cancel\":true,\"recent_choice\":true,\"bilingual_alias_search\":true,\"single_click_modal\":true,\"right_click_modal\":true,\"long_press_timer_modal\":true,\"release_no_extra_click\":true}";
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}

