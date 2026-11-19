from pathlib import Path
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8')
needle='a->insertionPreviewTool=s.activeIndex;a->insertionPreviewPending=true;'
replacement=needle+'''
        if(a->down&&dynamic_cast<arrows::ArrowTool*>(s.entries[s.activeIndex].tool.get())){
            a->insertionPreviewPending=false;a->error(s.gesture(PointerPhase::Move,a->insertionPreviewPoint,pointerModifiers));
            SetCursor(LoadCursorW(0,MAKEINTRESOURCEW(32512)));InvalidateRect(h,0,FALSE);
            if(a->now()-a->lastMovePaintMs>=8.0)UpdateWindow(h);return 0;
        }'''
assert s.count(needle)==1;s=s.replace(needle,replacement);p.write_text(s,encoding='utf-8')
