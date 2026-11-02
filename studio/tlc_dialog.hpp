#ifndef STUDIO_TLC_DIALOG_HPP
#define STUDIO_TLC_DIALOG_HPP
#include "tlc_preferences.hpp"
#include <cwctype>
struct TlcDialog {
    studio_tlc::Preferences value;
    r62::experimental::PlateFigure figure;
    r62::experimental::PlateFrame frame=r62::experimental::PlateFrame(r62::core::Vec2(),r62::core::Vec2(70,0),r62::core::Vec2(0,100));
    bool editing=false,updating=false,dragging=false,prepared=false,rfDirty=false;
    uint32_t laneId=0,spotId=0;HFONT font=0;double dpi=1;HWND hwnd=0,preview=0;
    static std::wstring wide(const std::string& text){int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),0,0);std::wstring result(n,L' ');if(n)MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),&result[0],n);return result;}
    static std::string utf8(const std::wstring& text){int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),0,0,0,0);std::string result(n,' ');if(n)WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),&result[0],n,0,0);return result;}
    std::wstring text(int id){int length=studio_i18n::getWindowTextLength(GetDlgItem(hwnd,id));std::vector<wchar_t> buffer(size_t(length)+1);studio_i18n::getDlgItemText(hwnd,id,buffer.data(),int(buffer.size()));return buffer.data();}
    void prepare(){if(!prepared){figure=studio_tlc::create(frame);prepared=true;}studio_tlc::assignUnowned(figure);if(!laneId&&!figure.lanes.empty())laneId=figure.lanes.front().id;if(auto spot=r62::experimental::findSpot(figure,spotId))laneId=spot->laneId;}
    void number(int id,double value,int decimals=2){wchar_t buffer[48];swprintf(buffer,48,L"%.*f",decimals,value);studio_i18n::setDlgItemText(hwnd,id,buffer);}
    bool readNumber(int id,double min,double max,double* result,bool report){auto input=text(id);wchar_t* end=0;double value=wcstod(input.c_str(),&end);while(end&&iswspace(*end))++end;
        if(input.empty()||end==input.c_str()||!end||*end||!std::isfinite(value)||value<min||value>max){if(report){studio_i18n::messageBox(hwnd,id==223?L"Rf 请输入 0–1 之间的数值，例如 0.35。":id==202?L"当前样品的斑点数请输入 0–30 的整数。":L"斑点直径请输入 1–16 磅。",L"薄层板",MB_OK|MB_ICONINFORMATION);SetFocus(GetDlgItem(hwnd,id));studio_i18n::sendDlgItemMessage(hwnd,id,EM_SETSEL,0,-1);}return false;}*result=value;return true;}
    bool commitRf(bool report){if(!rfDirty)return true;auto spot=r62::experimental::mutableSpot(&figure,spotId);if(!spot){rfDirty=false;return true;}double rf;if(!readNumber(223,0,1,&rf,report))return false;studio_tlc::setRf(figure,*spot,rf);rfDirty=false;return true;}
    void updateLaneName(){auto lane=r62::experimental::mutableLane(&figure,laneId);if(lane)lane->label=utf8(text(212));}
    bool commit(bool report=true){if(!commitRf(report))return false;double count,diameter;if(!readNumber(202,0,30,&count,report)||count!=std::floor(count)||!readNumber(203,1,16,&diameter,report))return false;
        updateLaneName();studio_tlc::setSpotCount(figure,frame,laneId,int(count),diameter);
        if(std::abs(value.diameter-diameter)>1e-8)for(auto& spot:figure.spots)studio_tlc::resizeSpots(spot,frame,diameter);value.diameter=diameter;
        figure.showLaneBorders=IsDlgButtonChecked(hwnd,204)==BST_CHECKED;figure.originDefined=IsDlgButtonChecked(hwnd,205)==BST_CHECKED;figure.frontDefined=IsDlgButtonChecked(hwnd,206)==BST_CHECKED;value.showRf=IsDlgButtonChecked(hwnd,207)==BST_CHECKED;return true;
    }
    void refresh(bool properties=true){updating=true;
        auto combo=GetDlgItem(hwnd,201);studio_i18n::sendMessage(combo,CB_RESETCONTENT,0,0);int selected=0;for(size_t i=0;i<figure.lanes.size();++i){const auto& lane=figure.lanes[i];auto count=studio_tlc::spotsIn(figure,lane.id).size();auto label=L"样品 "+std::to_wstring(i+1);if(!lane.label.empty())label+=L" · "+wide(lane.label);label+=L"（"+std::to_wstring(count)+L" 点）";studio_i18n::sendMessage(combo,CB_ADDSTRING,0,(LPARAM)label.c_str());if(lane.id==laneId)selected=int(i);}studio_i18n::sendMessage(combo,CB_SETCURSEL,selected,0);
        auto owned=studio_tlc::spotsIn(figure,laneId);uint32_t oldSpot=spotId;if(std::find(owned.begin(),owned.end(),spotId)==owned.end())spotId=owned.empty()?0:owned.front();
        studio_i18n::sendDlgItemMessage(hwnd,220,LB_RESETCONTENT,0,0);for(size_t i=0;i<owned.size();++i){auto spot=r62::experimental::findSpot(figure,owned[i]);wchar_t buffer[96];swprintf(buffer,96,L"斑点 %u     Rf %.3f",unsigned(i+1),studio_tlc::rf(figure,*spot));studio_i18n::sendDlgItemMessage(hwnd,220,LB_ADDSTRING,0,(LPARAM)buffer);if(owned[i]==spotId)studio_i18n::sendDlgItemMessage(hwnd,220,LB_SETCURSEL,i,0);}
        if(properties){auto lane=r62::experimental::findLane(figure,laneId);studio_i18n::setDlgItemText(hwnd,212,lane?wide(lane->label).c_str():L"");SetDlgItemInt(hwnd,202,UINT(owned.size()),FALSE);number(203,value.diameter,1);}
        if(properties||oldSpot!=spotId){if(auto spot=r62::experimental::findSpot(figure,spotId))number(223,studio_tlc::rf(figure,*spot),4);else studio_i18n::setDlgItemText(hwnd,223,L"");rfDirty=false;}
        for(int id:{223,224,225,226,222})EnableWindow(GetDlgItem(hwnd,id),spotId!=0);EnableWindow(GetDlgItem(hwnd,211),figure.lanes.size()>1);EnableWindow(GetDlgItem(hwnd,210),figure.lanes.size()<12);EnableWindow(GetDlgItem(hwnd,221),owned.size()<30);
        updating=false;InvalidateRect(preview,0,FALSE);
    }
    void countChanged(){double count;if(!readNumber(202,0,30,&count,false)||count!=std::floor(count))return;studio_tlc::setSpotCount(figure,frame,laneId,int(count),value.diameter);refresh(false);}
    Gdiplus::RectF boardRect(){RECT client;GetClientRect(preview,&client);float width=float(client.right),height=float(client.bottom);double ratio=frame.halfWidth()/std::max(1.,frame.halfHeight());float h=height-float(72*dpi),w=float(h*ratio);if(w>width-100*dpi){w=width-float(100*dpi);h=float(w/ratio);}return Gdiplus::RectF(float(20*dpi)+(width-float(100*dpi)-w)/2,float(26*dpi)+(height-float(72*dpi)-h)/2,w,h);}
    Gdiplus::PointF point(const Gdiplus::RectF& board,double u,double v){return Gdiplus::PointF(board.X+float((u+1)*board.Width*.5),board.Y+float((v+1)*board.Height*.5));}
    void paintPreview(HDC dc){using namespace Gdiplus;Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);g.Clear(Color(255,250,252,251));auto board=boardRect();SolidBrush white(Color(255,255,255,255)),ink(Color(255,40,48,48));Pen border(Color(255,50,62,61),float(1.2*dpi)),guide(Color(255,133,148,143),float(.8*dpi)),active(Color(255,23,117,109),float(1.5*dpi));Font font(L"Microsoft YaHei UI",float(11*dpi),FontStyleRegular,UnitPixel);StringFormat center;center.SetAlignment(StringAlignmentCenter);center.SetLineAlignment(StringAlignmentCenter);
        g.FillRectangle(&white,board);g.DrawRectangle(&border,board);if(figure.showLaneBorders)for(const auto& lane:figure.lanes){auto a=point(board,lane.u,-.92),b=point(board,lane.u,.92);g.DrawLine(&guide,a,b);}
        if(figure.originDefined){auto a=point(board,-1,figure.originV),b=point(board,1,figure.originV);g.DrawLine(&guide,a,b);}
        if(figure.frontDefined){guide.SetDashStyle(DashStyleDash);auto a=point(board,-1,figure.frontV),b=point(board,1,figure.frontV);g.DrawLine(&guide,a,b);}
        for(const auto& lane:figure.lanes){auto p=point(board,lane.u,1);std::wstring label=wide(lane.label);RectF box(p.X-float(35*dpi),p.Y+float(7*dpi),float(70*dpi),float(22*dpi));g.DrawString(label.c_str(),-1,&font,box,&center,&ink);}
        for(const auto& spot:figure.spots){auto p=point(board,spot.u,spot.v);float radius=float(std::max(3.*dpi,spot.radiusU*board.Width*.5));g.FillEllipse(&ink,p.X-radius,p.Y-radius,radius*2,radius*2);if(spot.id==spotId)g.DrawEllipse(&active,p.X-radius-float(4*dpi),p.Y-radius-float(4*dpi),radius*2+float(8*dpi),radius*2+float(8*dpi));
        }
        if(value.showRf){r62::experimental::PlateFrame upright(r62::core::Vec2(),r62::core::Vec2(frame.halfWidth(),0),r62::core::Vec2(0,frame.halfHeight()));
            double scale=board.Width/(2*frame.halfWidth());for(const auto& label:studio_tlc::rfLabels(figure,upright,10)){
                auto p=point(board,label.position.x/frame.halfWidth(),label.position.y/frame.halfHeight());
                Font rfFont(L"Arial",float(label.size*scale),FontStyleRegular,UnitPixel);auto text=wide(label.text);g.DrawString(text.c_str(),-1,&rfFont,p,&ink);
            }
        }
    }
    void dragPoint(int x,int y){(void)x;auto board=boardRect();auto spot=r62::experimental::mutableSpot(&figure,spotId);if(!spot||board.Height<=0)return;double v=2*(y-board.Y)/board.Height-1;studio_tlc::setRf(figure,*spot,(figure.originV-v)/(figure.originV-figure.frontV));updating=true;number(223,studio_tlc::rf(figure,*spot),4);rfDirty=false;updating=false;refresh(false);}
    static LRESULT CALLBACK previewProc(HWND h,UINT message,WPARAM w,LPARAM l){auto d=reinterpret_cast<TlcDialog*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(message==WM_NCCREATE){d=static_cast<TlcDialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)d);}if(!d)return DefWindowProcW(h,message,w,l);
        if(message==WM_ERASEBKGND)return 1;if(message==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);d->paintPreview(dc);EndPaint(h,&ps);return 0;}
        if(message==WM_LBUTTONDOWN){if(!d->commit())return 0;auto board=d->boardRect();int x=GET_X_LPARAM(l),y=GET_Y_LPARAM(l);double best=12*d->dpi;uint32_t spotId=0;bool added=false;for(const auto& spot:d->figure.spots){auto p=d->point(board,spot.u,spot.v);double distance=std::hypot(p.X-x,p.Y-y);if(distance<best){best=distance;spotId=spot.id;}}
            if(spotId){d->spotId=spotId;d->laneId=r62::experimental::findSpot(d->figure,spotId)->laneId;}else if(board.Contains(float(x),float(y))){double u=2*(x-board.X)/board.Width-1;auto lane=std::min_element(d->figure.lanes.begin(),d->figure.lanes.end(),[&](const r62::experimental::Lane& a,const r62::experimental::Lane& b){return std::abs(a.u-u)<std::abs(b.u-u);});if(lane!=d->figure.lanes.end()){d->laneId=lane->id;if(studio_tlc::spotsIn(d->figure,d->laneId).size()<30){d->spotId=studio_tlc::addSpot(d->figure,d->frame,d->laneId,d->value.diameter);added=true;}else return 0;}}else return 0;
            d->refresh();d->dragging=true;SetCapture(h);if(added)d->dragPoint(x,y);return 0;}
        if(message==WM_MOUSEMOVE&&d->dragging){d->dragPoint(GET_X_LPARAM(l),GET_Y_LPARAM(l));return 0;}
        if(message==WM_LBUTTONUP&&d->dragging){d->dragPoint(GET_X_LPARAM(l),GET_Y_LPARAM(l));d->dragging=false;ReleaseCapture();return 0;}if(message==WM_CAPTURECHANGED)d->dragging=false;
        return DefWindowProcW(h,message,w,l);
    }
    static INT_PTR CALLBACK proc(HWND h,UINT message,WPARAM w,LPARAM l){auto d=reinterpret_cast<TlcDialog*>(GetWindowLongPtrW(h,DWLP_USER));
        if(message==WM_INITDIALOG){d=reinterpret_cast<TlcDialog*>(l);SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)d);d->hwnd=h;d->prepare();studio_i18n::setWindowText(h,d->editing?L"薄层板 · 样品与斑点":L"新建薄层板 · 样品与斑点");d->dpi=GetDpiForWindow(h)/96.;auto px=[&](int n){return int(n*d->dpi);};SetWindowPos(h,0,0,0,px(760),px(560),SWP_NOMOVE|SWP_NOZORDER);
            RECT owner,rect;GetWindowRect(GetParent(h),&owner);GetWindowRect(h,&rect);SetWindowPos(h,0,owner.left+(owner.right-owner.left-(rect.right-rect.left))/2,owner.top+(owner.bottom-owner.top-(rect.bottom-rect.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
            d->font=CreateFontW(-px(14),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            auto control=[&](const wchar_t* cls,const wchar_t* label,int id,int x,int y,int width,int height,DWORD style=0){auto c=studio_i18n::createWindowEx(0,cls,label,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),h,(HMENU)(INT_PTR)id,0,0);studio_i18n::sendMessage(c,WM_SETFONT,(WPARAM)d->font,TRUE);return c;};
            control(L"STATIC",L"样品列",0,24,18,170,22);control(L"COMBOBOX",L"",201,24,46,176,240,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);control(L"BUTTON",L"添加",210,208,46,52,28,WS_TABSTOP);control(L"BUTTON",L"删除",211,266,46,52,28,WS_TABSTOP);
            control(L"STATIC",L"名称",0,24,90,58,24);control(L"EDIT",L"",212,90,88,228,27,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
            control(L"STATIC",L"当前样品斑点数",0,24,130,160,24);control(L"EDIT",L"",202,188,126,70,28,WS_BORDER|WS_TABSTOP|ES_NUMBER);control(L"STATIC",L"0–30",0,266,130,52,24);
            control(L"LISTBOX",L"",220,24,168,294,105,WS_BORDER|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT);control(L"BUTTON",L"添加斑点",221,24,282,140,29,WS_TABSTOP);control(L"BUTTON",L"删除所选",222,178,282,140,29,WS_TABSTOP);
            control(L"STATIC",L"Rf",0,24,328,60,24);control(L"EDIT",L"",223,90,324,100,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);control(L"BUTTON",L"设置 Rf",224,202,324,116,28,WS_TABSTOP);control(L"BUTTON",L"− 0.01",225,90,360,100,28,WS_TABSTOP);control(L"BUTTON",L"+ 0.01",226,202,360,116,28,WS_TABSTOP);
            control(L"STATIC",L"斑点直径（磅）",0,24,403,165,24);control(L"EDIT",L"",203,188,399,70,28,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
            WNDCLASSW cls={};cls.lpfnWndProc=previewProc;cls.hInstance=GetModuleHandleW(0);cls.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32649));cls.lpszClassName=L"MoleculeStudioTlcPreview";RegisterClassW(&cls);d->preview=studio_i18n::createWindowEx(WS_EX_CLIENTEDGE,cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE,px(350),px(22),px(370),px(354),h,(HMENU)208,GetModuleHandleW(0),d);
            control(L"STATIC",L"拖动斑点可调整 Rf，空白处单击可加点。\n每个样品的斑点数量与 Rf 独立设置。",0,350,387,370,44);
            control(L"BUTTON",L"列参考线",204,24,442,145,26,BS_AUTOCHECKBOX|WS_TABSTOP);control(L"BUTTON",L"起点线",205,188,442,120,26,BS_AUTOCHECKBOX|WS_TABSTOP);control(L"BUTTON",L"溶剂前沿",206,332,442,145,26,BS_AUTOCHECKBOX|WS_TABSTOP);control(L"BUTTON",L"显示 Rf",207,492,442,145,26,BS_AUTOCHECKBOX|WS_TABSTOP);
            control(L"STATIC",L"Rf 范围 0–1 · 修改后同步预览",0,24,487,350,24);control(L"BUTTON",d->editing?L"应用":L"保存为默认",IDOK,480,482,112,32,BS_DEFPUSHBUTTON|WS_TABSTOP);control(L"BUTTON",L"取消",IDCANCEL,606,482,114,32,WS_TABSTOP);
            CheckDlgButton(h,204,d->figure.showLaneBorders?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(h,205,d->figure.originDefined?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(h,206,d->figure.frontDefined?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(h,207,d->value.showRf?BST_CHECKED:BST_UNCHECKED);d->refresh();return TRUE;
        }
        if(!d)return FALSE;
        if(message==WM_COMMAND&&!d->updating){int id=LOWORD(w),notification=HIWORD(w);
            if(id==IDCANCEL){EndDialog(h,0);return TRUE;}
            if(id==223&&notification==EN_CHANGE){d->rfDirty=true;if(d->commitRf(false))d->refresh(false);return TRUE;}
            if(id==202&&notification==EN_CHANGE){d->countChanged();return TRUE;}
            if(id==212&&notification==EN_CHANGE){d->updateLaneName();InvalidateRect(d->preview,0,FALSE);return TRUE;}
            if(id==201&&notification==CBN_SELCHANGE){int index=int(studio_i18n::sendDlgItemMessage(h,201,CB_GETCURSEL,0,0));if(!d->commit()){d->refresh();return TRUE;}if(index>=0&&size_t(index)<d->figure.lanes.size()){d->laneId=d->figure.lanes[index].id;d->spotId=0;}d->refresh();return TRUE;}
            if(id==220&&notification==LBN_SELCHANGE){int index=int(studio_i18n::sendDlgItemMessage(h,220,LB_GETCURSEL,0,0));auto owned=studio_tlc::spotsIn(d->figure,d->laneId);if(!d->commit()){d->refresh();return TRUE;}if(index>=0&&size_t(index)<owned.size())d->spotId=owned[index];d->refresh();return TRUE;}
            if(id==210||id==211||id==221||id==222||id==224||id==225||id==226||id==IDOK||(id>=204&&id<=207&&notification==BN_CLICKED)){
                if(!d->commit())return TRUE;
                if(id==210){auto lane=studio_tlc::addLane(d->figure);if(lane){d->laneId=lane;d->spotId=0;}}
                if(id==211){studio_tlc::removeLane(d->figure,d->laneId);d->laneId=d->figure.lanes.front().id;d->spotId=0;}
                if(id==221){auto owned=studio_tlc::spotsIn(d->figure,d->laneId);if(owned.size()<30){studio_tlc::setSpotCount(d->figure,d->frame,d->laneId,int(owned.size()+1),d->value.diameter);d->spotId=d->figure.spots.back().id;}}
                if(id==222)d->figure.spots.erase(std::remove_if(d->figure.spots.begin(),d->figure.spots.end(),[&](const r62::experimental::Spot& spot){return spot.id==d->spotId;}),d->figure.spots.end());
                if(id==225||id==226)if(auto spot=r62::experimental::mutableSpot(&d->figure,d->spotId))studio_tlc::setRf(d->figure,*spot,studio_tlc::rf(d->figure,*spot)+(id==225?-.01:.01));
                if(id==IDOK){auto valid=r62::experimental::validateFigure(d->figure);if(!valid.ok()){studio_i18n::messageBox(h,wide(valid.message).c_str(),L"薄层板",MB_OK|MB_ICONINFORMATION);return TRUE;}d->value.lanes=int(d->figure.lanes.size());d->value.guides=d->figure.showLaneBorders;d->value.origin=d->figure.originDefined;d->value.front=d->figure.frontDefined;EndDialog(h,1);return TRUE;}d->refresh();return TRUE;
            }
        }
        if(message==WM_DESTROY){if(d->font)DeleteObject(d->font);return TRUE;}return FALSE;
    }
    bool show(HWND owner){struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;} t={};t.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;t.dialog.cx=500;t.dialog.cy=370;return DialogBoxIndirectParamW(GetModuleHandleW(0),&t.dialog,owner,proc,(LPARAM)this)==1;}
};
#endif
