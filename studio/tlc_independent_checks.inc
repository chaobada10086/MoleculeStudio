// Exercise the actual editing entry point with independent lane populations.
require(writeFigure(s.doc,plate,figure).ok(),"reset four-sample fixture");s.refresh();auto beforeEditor=s.doc.digest();
SetPropW(h,L"tlc.test.output",(HANDLE)&dir);
UINT_PTR timer=SetTimer(h,9183,80,[](HWND owner,UINT,UINT_PTR timer,DWORD){
    HWND popup=GetWindow(owner,GW_ENABLEDPOPUP);if(popup==owner||!GetDlgItem(popup,220))return;KillTimer(owner,timer);
    auto d=reinterpret_cast<TlcDialog*>(GetWindowLongPtrW(popup,DWLP_USER));bool passed=d&&d->figure.lanes.size()==4;
    if(!passed){SetPropW(owner,L"tlc.test.result",(HANDLE)2);PostMessageW(popup,WM_COMMAND,IDCANCEL,0);return;}
    auto choose=[&](int index){SendDlgItemMessageW(popup,201,CB_SETCURSEL,index,0);SendMessageW(popup,WM_COMMAND,MAKEWPARAM(201,CBN_SELCHANGE),(LPARAM)GetDlgItem(popup,201));};
    auto point=[&](int index){SendDlgItemMessageW(popup,220,LB_SETCURSEL,index,0);SendMessageW(popup,WM_COMMAND,MAKEWPARAM(220,LBN_SELCHANGE),(LPARAM)GetDlgItem(popup,220));};
    choose(0);SetDlgItemInt(popup,202,1,FALSE);SetDlgItemTextW(popup,223,L"0.20");auto stable=d->figure.spots.front();
    choose(1);SetDlgItemInt(popup,202,3,FALSE);point(0);SetDlgItemTextW(popup,223,L"0.40");point(1);SetDlgItemTextW(popup,223,L"0.65");point(2);SetDlgItemTextW(popup,223,L"0.85");
    passed&=studio_tlc::spotsIn(d->figure,d->figure.lanes[0].id).size()==1&&studio_tlc::spotsIn(d->figure,d->figure.lanes[1].id).size()==3;
    passed&=d->figure.spots.front().id==stable.id&&d->figure.spots.front().v==stable.v;
    // Out-of-range input cannot silently move a point. The user can recover.
    double old=r62::experimental::findSpot(d->figure,d->spotId)->v;SetDlgItemTextW(popup,223,L"1.2");passed&=!d->commitRf(false)&&r62::experimental::findSpot(d->figure,d->spotId)->v==old;SetDlgItemTextW(popup,223,L"0.85");
    choose(2);SetDlgItemInt(popup,202,0,FALSE);passed&=!IsWindowEnabled(GetDlgItem(popup,223));
    choose(3);SetDlgItemInt(popup,202,2,FALSE);point(0);SetDlgItemTextW(popup,223,L"0");point(1);SetDlgItemTextW(popup,223,L"1");
    // Drag a specific point in the preview and check Rf and other lanes.
    choose(1);point(1);auto spot=r62::experimental::findSpot(d->figure,d->spotId);auto board=d->boardRect();auto start=d->point(board,spot->u,spot->v);auto end=d->point(board,spot->u,d->figure.originV-.77*(d->figure.originV-d->figure.frontV));
    SendMessageW(d->preview,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(int(start.X),int(start.Y)));SendMessageW(d->preview,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(int(end.X),int(end.Y)));SendMessageW(d->preview,WM_LBUTTONUP,0,MAKELPARAM(int(end.X),int(end.Y)));
    spot=r62::experimental::findSpot(d->figure,d->spotId);passed&=std::abs(studio_tlc::rf(d->figure,*spot)-.77)<.005;
    passed&=d->figure.spots.front().v==stable.v;
    CheckDlgButton(popup,207,BST_CHECKED);SendMessageW(popup,WM_COMMAND,MAKEWPARAM(207,BN_CLICKED),(LPARAM)GetDlgItem(popup,207));SetDlgItemTextW(popup,203,L"6");
    auto output=static_cast<const std::string*>(GetPropW(owner,L"tlc.test.output"));UpdateWindow(popup);UpdateWindow(d->preview);
    {RECT r;GetClientRect(popup,&r);HDC screen=GetDC(popup),memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,r.right,r.bottom);auto old=SelectObject(memory,bitmap);PrintWindow(popup,memory,PW_CLIENTONLY);Bitmap picture(bitmap,0);UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);std::vector<uint8_t> storage(bytes);auto encoders=(ImageCodecInfo*)storage.data();GetImageEncoders(count,bytes,encoders);for(UINT i=0;i<count;++i)if(wcscmp(encoders[i].MimeType,L"image/png")==0){picture.Save(W(*output+"/tlc-independent-editor.png").c_str(),&encoders[i].Clsid,0);break;}SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(popup,screen);}
    SetPropW(owner,L"tlc.test.result",(HANDLE)(passed?1:2));PostMessageW(popup,WM_COMMAND,IDOK,0);
});
tlcSettings(plate);KillTimer(h,timer);require(GetPropW(h,L"tlc.test.result")==HANDLE(1),"independent counts, Rf input, bounds and preview drag via native controls");RemovePropW(h,L"tlc.test.result");RemovePropW(h,L"tlc.test.output");
PlateFigure independent;require(readFigure(s.doc,plate,&independent,0).ok(),"read edited board");auto checkIndependent=[&](const PlateFigure& board){int counts[]={1,3,0,2};require(board.lanes.size()==4&&board.spots.size()==6,"independent point population totals");for(int i=0;i<4;++i)require(studio_tlc::spotsIn(board,board.lanes[i].id).size()==size_t(counts[i]),"sample counts remain 1, 3, 0, 2");auto first=studio_tlc::spotsIn(board,board.lanes[0].id);require(std::abs(studio_tlc::rf(board,*findSpot(board,first[0]))-.2)<1e-8,"precise Rf input retained");auto second=studio_tlc::spotsIn(board,board.lanes[1].id);require(std::abs(studio_tlc::rf(board,*findSpot(board,second[1]))-.77)<.005,"dragged Rf retained");auto last=studio_tlc::spotsIn(board,board.lanes[3].id);require(std::abs(studio_tlc::rf(board,*findSpot(board,last[0])))<1e-8&&std::abs(studio_tlc::rf(board,*findSpot(board,last[1]))-1)<1e-8,"Rf endpoints zero and one retained");};
checkIndependent(independent);auto afterEditor=s.doc.digest();require(s.undo().ok()&&s.doc.digest()==beforeEditor,"whole editor applies in one undo");require(s.undo(true).ok()&&s.doc.digest()==afterEditor,"whole editor exact redo");
// Canceling a changed draft must not alter the document or undo history.
timer=SetTimer(h,9184,50,[](HWND owner,UINT,UINT_PTR timer,DWORD){HWND popup=GetWindow(owner,GW_ENABLEDPOPUP);if(popup==owner||!GetDlgItem(popup,220))return;KillTimer(owner,timer);SetDlgItemInt(popup,202,9,FALSE);SetDlgItemTextW(popup,223,L"0.123");PostMessageW(popup,WM_COMMAND,IDCANCEL,0);});tlcSettings(plate);KillTimer(h,timer);require(s.doc.digest()==afterEditor,"cancel editor preserves all original points and Rf");
require(s.save(dir+"/tlc-independent.r62doc",false).ok()&&s.save(dir+"/tlc-independent.cdxml",true).ok(),"save independent counts and Rf");
for(const char* suffix:{"r62doc","cdxml"}){Session reopened;require(reopened.open(dir+"/tlc-independent."+suffix).ok(),"reopen independent board");PlateFigure actual;require(readFigure(reopened.doc,0,&actual,0).ok(),"read reopened independent board");checkIndependent(actual);auto flag=reopened.doc.extensions(figureRef(0)).findValue("studio.tlc.showRf");require(flag&&*flag=="1","Rf label display survives save and reopen");}
// New boards use the customized sample pattern instead of flattening counts.
auto configured=studio_tlc::preferences();configured.customTemplate=true;configured.plateTemplate=independent;configured.diameter=6;studio_tlc::preferences()=configured;auto newBoard=studio_tlc::create(PlateFrame(Vec2(),Vec2(120,0),Vec2(0,160)));checkIndependent(newBoard);require(validateFigure(newBoard).ok(),"custom template valid at another board size");
std::ofstream(dir+"/tlc-independent-checks.json")<<"{\"independent_counts\":[1,3,0,2],\"rf_numeric\":true,\"rf_drag\":true,\"rf_bounds\":true,\"unrelated_points_preserved\":true,\"one_step_undo_redo\":true,\"cancel_preserves_document\":true,\"native_cdxml_roundtrip\":true,\"custom_template\":true}";
