#pragma once
static void checkCalculator(App& app,const std::string& dir){
    auto require=[](bool ok,const char* reason){if(!ok)throw std::runtime_error(std::string("calculator: ")+reason);};
    double grams=0,mol=0,value=0;int cases=0;
    require(yieldcalc::convert(.1,78.114,true,grams,mol)&&std::abs(mol-.00128018024937962)<1e-12,"benzene 100mg reference");++cases;
    require(yieldcalc::convert(.002,78.114,false,grams,mol)&&std::abs(grams-.156228)<1e-12,"2mmol benzene reference");++cases;
    for(int unit=0;unit<4;++unit){double raw=1.25/yieldcalc::massFactor(unit);require(yieldcalc::convert(raw*yieldcalc::massFactor(unit),78.114,true,grams,mol)&&std::abs(grams-1.25)<1e-12,"mass unit conversions");++cases;}
    require(yieldcalc::amountUnit(.09999)==1&&yieldcalc::amountUnit(.1)==2&&yieldcalc::amountUnit(.101)==2,"100mmol display threshold");++cases;
    require(yieldcalc::percent(.088,.1,1,1,value)&&std::abs(value-88)<1e-10,"88 percent");++cases;
    require(yieldcalc::percent(.088,.1,2,1,value)&&std::abs(value-44)<1e-10,"product stoichiometry");++cases;
    require(yieldcalc::percent(.088,.1,1,2,value)&&std::abs(value-176)<1e-10,"starting stoichiometry without yield cap");++cases;
    require(!yieldcalc::percent(1,0,1,1,value)&&!yieldcalc::convert(1,0,true,grams,mol),"zero denominators");++cases;
    for(auto text:{L"-1",L"nan",L"inf",L"1x",L"1e999",L""}){require(!yieldcalc::number(text,value),"invalid input rejected");++cases;}
    require(yieldcalc::number(L"1.25e-3",value)&&std::abs(value-.00125)<1e-12,"scientific notation");++cases;
    auto& s=app.s;s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());
    s.doc.setDefaultBondLength(24);s.doc.setDefaultTextSize(10);s.doc.setDefaultFont("Arial");
    std::vector<ObjectId> roots;
    for(int side=0;side<2;++side){std::vector<ObjectId> atoms;for(int i=0;i<6;++i){ObjectId id;double angle=i*6.283185307179586/6;s.doc.addAtom(Vec2(side*230+24*std::cos(angle),24*std::sin(angle)),6,&id);atoms.push_back(id);}
        for(int i=0;i<6;++i){ObjectId id;s.doc.addBond(atoms[i],atoms[(i+1)%6],&id);if(i%2==0)s.doc.setBondOrder(id,BondOrder::Double);}roots.push_back(atoms[0]);
        if(side){ObjectId methyl;s.doc.addAtom(Vec2(278,0),6,&methyl);s.doc.addBond(atoms[0],methyl,nullptr);}}
    auto arrow=arrows::resolvePreset(0,Vec2(80,0),Vec2(170,0),.6);ObjectId arrowId;arrows::addArrow(s.doc,arrow,.6,&arrowId);
    s.refresh();s.selected=s.all();s.fit();auto rows=yieldcalc::collect(s,s.selected);
    require(rows.size()==2&&rows[0].formula.plain=="C6H6"&&rows[1].formula.plain=="C7H8"&&rows[0].role==0&&rows[1].role==2,"reaction components and roles");
    require(MassCalculatorDialog::number(rows[0].formula.exactMass,4)==L"78.0470","neutral exact mass copied independently of adduct");
    require(yieldcalc::collect(s,{ObjectRef(ObjectKind::Atom,roots[0])}).size()==1&&yieldcalc::collect(s,{}).empty(),"whole molecule from one selected atom, empty scope stays empty");
    rows[0].valid=rows[1].valid=true;rows[0].grams=.2;rows[0].mol=.2/rows[0].formula.averageMass;rows[1].grams=.1;rows[1].mol=.1/rows[1].formula.averageMass;rows[1].yield=88;rows[1].yieldValid=true;
    require(yieldcalc::label(rows[1])=="100.0 mg\nYield = 88 %","requested two-line annotation");
    auto before=s.doc.digest();auto cursor=s.history.cursor();require(yieldcalc::insert(s,rows,0).ok(),"insert reaction annotations");auto after=s.doc.digest();
    require(s.doc.stats().aliveTexts==2&&s.history.cursor()==cursor+1,"one transaction for all compounds");
    for(const auto& t:s.doc.texts().records())if(t.isAlive())require(t.format.align==1&&t.position.y>24,"captions centred below each compound");
    require(s.undo().ok()&&s.doc.digest()==before&&s.undo(true).ok()&&s.doc.digest()==after,"annotations undo redo atomically");
    require(yieldcalc::insert(s,rows,0).ok()&&s.doc.stats().aliveTexts==2,"repeat insertion updates captions");
    for(const char* ext:{"r62doc","cdxml"}){auto path=dir+"/calculator-annotations."+ext;require(s.save(path,std::string(ext)=="cdxml").ok(),"save annotations");Session reopened;require(reopened.open(path).ok()&&reopened.doc.stats().aliveTexts==2,"reopen editable captions");bool found=false;for(const auto& t:reopened.doc.texts().records())if(t.isAlive()&&reopened.doc.plainText(t.id)=="100.0 mg\nYield = 88 %")found=true;require(found,"two-line text survives save");}
    while(s.history.cursor()>cursor)require(s.undo().ok(),"undo caption fixture");require(s.doc.digest()==before,"restore fixture for UI test");s.selected=s.all();s.save(dir+"/calculator-reaction-fixture.cdxml",true);
    CalculatorDialog dialog;SetPropW(app.h,L"calculator.test.dir",(HANDLE)&dir);
    SetTimer(app.h,9191,50,[](HWND owner,UINT,UINT_PTR timer,DWORD){
        HWND popup=GetWindow(owner,GW_ENABLEDPOPUP);if(popup==owner||!GetDlgItem(popup,700))return;KillTimer(owner,timer);
        auto d=(CalculatorDialog*)GetWindowLongPtrW(popup,DWLP_USER);auto output=(const std::string*)GetPropW(owner,L"calculator.test.dir");
        try{
            auto ok=[](bool state,const char* reason){if(!state)throw std::runtime_error(reason);};
            wchar_t title[32]={};GetWindowTextW(popup,title,32);ok(std::wstring(title)==L"计算器"&&SendMessageW(d->tabs,TCM_GETCURSEL,0,0)==0,"title and default yield tab");
            ok(d->fields.size()==2&&d->keyButtons.size()==16&&IsWindowVisible(d->keyButtons[0]),"compound cards and default keyboard");
            ok(!GetDlgItem(d->yieldPage,601),"redundant starting-compound selector removed");RECT keyRect;GetWindowRect(d->keyButtons[0],&keyRect);ok(keyRect.bottom-keyRect.top>=d->px(74),"yield keyboard has larger keys");
            auto& product=d->rows[1];auto& f=d->fields[1];
            SetWindowTextW(f.mass,L"1.25 - 0.20 + 1e-2");SendMessageW(f.mass,WM_KEYDOWN,VK_RETURN,0);
            ok(product.valid&&std::abs(product.grams-1.06)<1e-10,"mass arithmetic Enter");
            SetFocus(f.mass);SetWindowTextW(f.mass,L"12.6-12.5");SetFocus(f.amount);
            ok(product.valid&&std::abs(product.grams-.1)<1e-10,"mass subtraction on blur");
            SetWindowTextW(d->fields[0].mass,L"0.200");SetWindowTextW(f.mass,L"0.100");
            ok(product.valid&&product.yieldValid&&std::abs(product.yield-100*product.mol/d->rows[0].mol)<1e-8,"single substrate supplies the starting amount");
            auto initial=yieldcalc::compact((.1/product.formula.averageMass)/.88*1000,14);SetWindowTextW(d->start,initial.c_str());
            ok(product.yieldValid&&std::abs(product.yield-88)<1e-8,"manual starting amount produces 88 percent");
            SendMessageW(d->startUnit,CB_SETCURSEL,1,0);d->command(603,CBN_SELCHANGE);ok(std::abs(product.yield-88)<1e-7,"starting unit switch preserves physical amount");
            SetWindowTextW(f.coefficient,L"2");ok(std::abs(product.yield-44)<1e-8,"UI stoichiometric coefficient");SetWindowTextW(f.coefficient,L"1");
            SendMessageW(f.massUnit,CB_SETCURSEL,1,0);d->command(1022,CBN_SELCHANGE);ok(std::abs(product.grams-.1)<1e-12&&std::abs(std::wcstod(CalculatorDialog::text(f.mass).c_str(),nullptr)-100)<1e-6,"mass unit preserves quantity");
            SetWindowTextW(f.mass,L"-1");ok(!product.valid&&!product.yieldValid&&CalculatorDialog::text(f.amount).empty(),"invalid input clears stale results");
            SetWindowTextW(f.mass,L"100");
            SendMessageW(f.amountUnit,CB_SETCURSEL,2,0);d->command(1025,CBN_SELCHANGE);SetWindowTextW(f.amount,L"0.002");ok(product.valid&&std::abs(product.grams-.002*product.formula.averageMass)<1e-12,"amount to mass UI");
            SendMessageW(f.amountUnit,CB_SETCURSEL,0,0);d->command(1025,CBN_SELCHANGE);
            SetWindowTextW(f.mass,yieldcalc::compact(product.formula.averageMass*101,12).c_str());ok(d->fields[1].autoUnit==2,"automatic mol above 100mmol");
            SetWindowTextW(f.mass,L"100");ok(d->fields[1].autoUnit==1,"automatic mmol below 100mmol");
            SetFocus(f.mass);SendMessageW(f.mass,EM_SETSEL,0,-1);d->key(8);ok(CalculatorDialog::text(f.mass)==L"1","soft keyboard input");d->key(3);ok(!product.valid,"keyboard backspace clears derived result");SetWindowTextW(f.mass,L"100");
            auto notify=[&](HWND tabs,HWND target,int index){SendMessageW(tabs,TCM_SETCURSEL,index,0);NMHDR event={};event.hwndFrom=tabs;event.code=TCN_SELCHANGE;SendMessageW(target,WM_NOTIFY,0,(LPARAM)&event);};
            notify(d->tabs,popup,1);ok(IsWindowVisible(d->massPage)&&!IsWindowVisible(d->yieldPage),"mass and yield are sibling tabs");
            for(auto key:d->keyButtons)ok(!IsWindowVisible(key),"mass tab has no keyboard");ok(IsWindowVisible(d->massPreview)&&d->massThumbnail,"mass tab has selected structure preview");
            ok(Header_GetItemCount(ListView_GetHeader(d->mass.list))==4,"average m/z column removed");
            for(int id:{504,505,507})ok(CalculatorDialog::text(GetDlgItem(d->massPage,id))==L"复制","all mass copy buttons named copy");
            ok(d->mass.copyValue(507)==MassCalculatorDialog::number(d->rows[0].formula.averageMass,4)&&d->mass.copyValue(505)==L"78.0470"&&d->mass.copyValue(504)==L"79.0542","copy selects average neutral, exact neutral or selected adduct value");
            ok(ListView_GetItemCount(d->mass.list)==4,"four common positive ions");for(auto index:d->mass.visible)ok(MassCalculatorDialog::adducts()[index].charge>0,"positive-only page");
            ok(d->mass.visible==std::vector<size_t>({1,2,3,4}),"common positive H, Na, K and NH4 adducts");
            SendMessageW(d->massPage,WM_COMMAND,506,0);ok(ListView_GetItemCount(d->mass.list)==10,"expand positive ions independently");
            notify(d->mass.filterBox,d->massPage,1);ok(ListView_GetItemCount(d->mass.list)==4,"four common negative ions");for(auto index:d->mass.visible)ok(MassCalculatorDialog::adducts()[index].charge<0,"negative-only page");
            ok(d->mass.visible==std::vector<size_t>({11,12,13,14}),"common negative deprotonated, Cl, formate and acetate ions");
            SendMessageW(d->massPage,WM_COMMAND,506,0);ok(ListView_GetItemCount(d->mass.list)==8,"expand all negative ions");
            ok(IsWindowEnabled(GetDlgItem(d->massPage,505)),"exact mass copy button enabled");
            SendMessageW(popup,WM_COMMAND,710,0);ok(d->mass.formula.plain=="C7H8","previous structure wraps");SendMessageW(popup,WM_COMMAND,711,0);ok(d->mass.formula.plain=="C6H6","next structure wraps");SendMessageW(popup,WM_COMMAND,711,0);ok(d->mass.formula.plain=="C7H8"&&d->massThumbnail->source==d->rows[1].preview,"structure preview and mass switch together");
            notify(d->tabs,popup,0);ok(std::abs(product.yield-88)<1e-7,"yield tab values survive tab switching");
            SendMessageW(popup,WM_COMMAND,703,0);ok(d->session->doc.stats().aliveTexts==2,"one click annotates reaction");
            std::ofstream(*output+"/calculator-ui-checks.json")<<"{\"default_yield_tab\":true,\"default_keyboard\":true,\"separate_compound_cards\":true,\"bidirectional_conversion\":true,\"unit_changes_preserve_quantity\":true,\"invalid_input_clears_results\":true,\"stoichiometry\":true,\"automatic_mmol_mol\":true,\"soft_keyboard\":true,\"mass_tabs_positive_negative\":true,\"four_common_ions\":true,\"expand_ions\":true,\"exact_mass_copy_button\":true,\"one_click_annotations\":true}";
            SetPropW(owner,L"calculator.test.ok",(HANDLE)1);
        }catch(const std::exception& error){std::ofstream(*output+"/calculator-ui-failure.txt")<<error.what();SetPropW(owner,L"calculator.test.ok",(HANDLE)2);}
        PostMessageW(popup,WM_COMMAND,IDCANCEL,0);
    });
    dialog.show(app.h,s);RemovePropW(app.h,L"calculator.test.dir");require((size_t)RemovePropW(app.h,L"calculator.test.ok")==1,"UI checks; see calculator-ui-failure.txt");
    for(int count:{0,1,6}){
        Session source;
        if(count==1)require(studioio::append(s.doc,source.doc,rows[0].refs,Vec2(),nullptr).ok(),"single-molecule scope fixture");
        if(count==6){std::vector<ObjectRef> refs;for(auto row:rows)refs.insert(refs.end(),row.refs.begin(),row.refs.end());
            for(int copy=0;copy<3;++copy)require(studioio::append(s.doc,source.doc,refs,Vec2(copy*420,0),nullptr).ok(),"six-compound scope fixture");}
        source.refresh();source.selected=source.all();SetPropW(app.h,L"calculator.scope.count",(HANDLE)(size_t(count)+1));
        SetTimer(app.h,9192,50,[](HWND owner,UINT,UINT_PTR timer,DWORD){
            HWND popup=GetWindow(owner,GW_ENABLEDPOPUP);if(popup==owner||!GetDlgItem(popup,700))return;KillTimer(owner,timer);
            auto d=(CalculatorDialog*)GetWindowLongPtrW(popup,DWLP_USER);int count=int((size_t)GetPropW(owner,L"calculator.scope.count"))-1;bool passed=d&&d->fields.size()==size_t(count);
            if(passed&&count==0)passed=!IsWindowEnabled(d->insertButton);
            if(passed&&count==1){auto& row=d->rows[0];auto& f=d->fields[0];SetWindowTextW(f.amount,L"2");passed=row.valid&&std::abs(row.grams-.156228)<1e-8;
                SetWindowTextW(d->start,L"1");passed&=row.yieldValid&&std::abs(row.yield-200)<1e-8;
                SetWindowTextW(d->start,L"0");passed&=!row.yieldValid;
                SetWindowTextW(d->start,L"1");SetWindowTextW(f.amount,L"0");passed&=row.valid&&row.yieldValid&&row.yield==0;}
            if(passed&&count==6){d->ensureVisible(5);RECT box;GetWindowRect(d->fields[5].mass,&box);POINT origin={box.left,box.top};ScreenToClient(d->cards,&origin);passed=d->offset>0&&origin.x>=0&&origin.x<d->px(730);SetWindowTextW(d->fields[5].mass,L"1");passed&=d->rows[5].valid&&!d->rows[4].valid;}
            SetPropW(owner,L"calculator.scope.ok",(HANDLE)(passed?1:2));PostMessageW(popup,WM_COMMAND,IDCANCEL,0);
        });
        CalculatorDialog scoped;scoped.show(app.h,source);RemovePropW(app.h,L"calculator.scope.count");require((size_t)RemovePropW(app.h,L"calculator.scope.ok")==1,"empty, single molecule and six-compound scrolling UI");
    }
    s.selected=s.all();s.fit();app.screenshot(dir+"/calculator-annotations.png");s.save(dir+"/calculator-ui-inserted.cdxml",true);
    std::ofstream(dir+"/calculator-math-checks.json")<<"{\"numeric_reference_cases\":"<<cases<<",\"reaction_components\":true,\"centered_two_line_annotations\":true,\"single_undo\":true,\"repeat_updates\":true,\"native_and_cdxml_roundtrip\":true,\"empty_single_and_six_compounds\":true,\"neutral_exact_mass\":true}";
}
