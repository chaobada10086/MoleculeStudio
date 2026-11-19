from pathlib import Path
r=Path(__file__).resolve().parents[1]
def edit(name,old,new,count=1):
 p=r/name;s=p.read_text(encoding='utf-8');assert s.count(old)==count,(name,s.count(old));p.write_text(s.replace(old,new),encoding='utf-8',newline='\r\n')
edit('studio/tlc_preferences.hpp','int lanes=4,spots=1;','int lanes=3,spots=1;')
edit('studio/studio.cpp','L"TLC",L"Lanes",4,path.c_str()','L"TLC",L"Lanes",3,path.c_str()')
edit('studio/studio.cpp','bool tlcSpotMenu(Vec2 point,POINT screen);','bool tlcSpotMenu(Vec2 point,POINT screen);\n    bool tlcClickPending=false;Vec2 tlcClickPoint;uint32_t tlcClickModifiers=0;uint64_t tlcClickDocument=0,tlcClickSerial=0;size_t tlcClickTool=0;\n    void cancelTlcClick();void commitTlcClick();')
edit('studio/studio.cpp','const auto result=s.gesture(PointerPhase::Up,s.view().toDocument(pixel),pointerModifiers);','''Error result;
            if(s.tlcSpotting){
                // A first release may belong to a double-click. Deposit only
                // after Windows' double-click interval has elapsed.
                s.tlcSpotting=false;a->tlcClickPending=true;a->tlcClickPoint=s.view().toDocument(pixel);
                a->tlcClickModifiers=pointerModifiers;a->tlcClickDocument=s.doc.id();a->tlcClickSerial=s.serial;a->tlcClickTool=s.activeIndex;
                SetTimer(h,19,GetDoubleClickTime(),nullptr);
            }else result=s.gesture(PointerPhase::Up,s.view().toDocument(pixel),pointerModifiers);''')
edit('studio/studio.cpp','case WM_LBUTTONDBLCLK:{a->cancelTableDrag();','case WM_LBUTTONDBLCLK:{a->cancelTlcClick();s.cancel();a->down=false;if(GetCapture()==h)ReleaseCapture();a->cancelTableDrag();')
edit('studio/studio.cpp','case WM_TIMER:if(wp==7){','case WM_TIMER:if(wp==19){a->commitTlcClick();return 0;}if(wp==7){')
# Flush a preceding independent click before a new normal press. Windows sends
# DBLCLK instead of DOWN for the second press of a double-click.
edit('studio/studio.cpp','case WM_LBUTTONDOWN:','case WM_LBUTTONDOWN:a->commitTlcClick();')
edit('studio/studio.cpp','a->selectedElectronMarker={};a->radicalPlacing=false;a->cancelTableDrag();s.cancel();','a->cancelTlcClick();a->selectedElectronMarker={};a->radicalPlacing=false;a->cancelTableDrag();s.cancel();')
edit('studio/tlc_actions.hpp','void App::tlcMenu(){','''void App::cancelTlcClick(){tlcClickPending=false;if(canvas)KillTimer(canvas,19);}
void App::commitTlcClick(){
    if(!tlcClickPending)return;
    cancelTlcClick();
    if(s.doc.id()!=tlcClickDocument||s.serial!=tlcClickSerial||s.activeIndex!=tlcClickTool||
       s.entries[s.activeIndex].command!="chooseTLCPlateTool")return;
    error(s.gesture(PointerPhase::Down,tlcClickPoint,tlcClickModifiers));
    error(s.gesture(PointerPhase::Up,tlcClickPoint,tlcClickModifiers));invalidate();
}
void App::tlcMenu(){''')
print('TLC double-click and three-dot defaults updated')
