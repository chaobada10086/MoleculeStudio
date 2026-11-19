from pathlib import Path
r=Path(__file__).resolve().parents[1];p=r/'studio/mass_dialog.hpp';s=p.read_text(encoding='utf-8')
def edit(old,new,count=1):
 global s
 assert s.count(old)==count,(old[:70],s.count(old))
 s=s.replace(old,new)
edit('HWND list=0, theoretical=0, filterBox=0, copy=0;', 'HWND list=0, theoretical=0, filterBox=0, copy=0;bool embedded=false,showAll=false;')
edit('int filter=0;', 'int filter=1;')
edit('if(filter==2&&a.charge>0)continue;', 'if(filter==2&&a.charge>0)continue;\n            if(!showAll&&((a.charge>0&&i>3)||(a.charge<0&&i>13)))continue;')
edit('void copyResult(HWND h){','''static void copyText(HWND h,const std::wstring& text){
        if(!OpenClipboard(h))return;EmptyClipboard();
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
        if(memory){auto p=(wchar_t*)GlobalLock(memory);if(p){wcscpy(p,text.c_str());GlobalUnlock(memory);if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);}else GlobalFree(memory);}CloseClipboard();
    }
    void copyResult(HWND h){''')
edit('studio_i18n::setWindowText(h,L"质谱计算器 — Mass Calculator");', 'if(!d->embedded)studio_i18n::setWindowText(h,L"计算器");')
edit('SetWindowPos(h,0,0,0,px(720),px(520),SWP_NOMOVE|SWP_NOZORDER);', 'if(!d->embedded){SetWindowPos(h,0,0,0,px(750),px(550),SWP_NOMOVE|SWP_NOZORDER);')
edit('0,0,SWP_NOSIZE|SWP_NOZORDER);', '0,0,SWP_NOSIZE|SWP_NOZORDER);}')
edit('CreateFontW(-px(13)', 'CreateFontW(-px(15)',2)
edit('CreateFontW(-px(19)', 'CreateFontW(-px(22)')
edit('d->filterBox=control(L"COMBOBOX",L"",501,560,82,140,200,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);\n            for(auto s:{L"全部离子",L"正离子",L"负离子"})studio_i18n::sendMessage(d->filterBox,CB_ADDSTRING,0,(LPARAM)s);\n            studio_i18n::sendMessage(d->filterBox,CB_SETCURSEL,0,0);', '''d->filterBox=control(WC_TABCONTROLW,L"",501,20,82,440,28,WS_TABSTOP);
            for(int i=0;i<2;++i){TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=(LPWSTR)(i?L"负离子":L"正离子");SendMessageW(d->filterBox,TCM_INSERTITEMW,i,(LPARAM)&item);}
            control(L"BUTTON",L"显示全部",506,568,80,132,28,WS_TABSTOP);''')
edit('control(L"STATIC",L"常见加合物（选择一行查看理论 m/z）",0,20,84,300,22);','')
edit('control(L"BUTTON",L"关闭",IDCANCEL,590,414,110,32,WS_TABSTOP|BS_DEFPUSHBUTTON);', '''auto exactCopy=control(L"BUTTON",L"复制精确分子量",505,150,414,170,32,WS_TABSTOP);EnableWindow(exactCopy,d->formula.exactValid);
            if(!d->embedded)control(L"BUTTON",L"关闭",IDCANCEL,590,414,110,32,WS_TABSTOP|BS_DEFPUSHBUTTON);''')
edit('if(id==IDCANCEL||id==IDOK){EndDialog(h,0);return TRUE;}', 'if(id==IDCANCEL||id==IDOK){if(!d->embedded)EndDialog(h,0);return TRUE;}')
edit('if(id==501&&notification==CBN_SELCHANGE){d->filter=int(studio_i18n::sendMessage(d->filterBox,CB_GETCURSEL,0,0));d->populate(h);return TRUE;}', '''if(id==505){if(d->formula.exactValid)copyText(h,number(d->formula.exactMass,4));return TRUE;}
            if(id==506){d->showAll=!d->showAll;studio_i18n::setWindowText(GetDlgItem(h,506),d->showAll?L"只显示常用四项":L"显示全部");d->populate(h);return TRUE;}''')
edit('if(m==WM_NOTIFY&&((NMHDR*)l)->hwndFrom==d->list', '''if(m==WM_NOTIFY&&((NMHDR*)l)->hwndFrom==d->filterBox&&((NMHDR*)l)->code==TCN_SELCHANGE){d->filter=int(SendMessageW(d->filterBox,TCM_GETCURSEL,0,0))+1;d->populate(h);return TRUE;}
        if(m==WM_NOTIFY&&((NMHDR*)l)->hwndFrom==d->list''')
edit('if(m==WM_CLOSE){EndDialog(h,0);return TRUE;}', 'if(m==WM_CLOSE){if(!d->embedded)EndDialog(h,0);return TRUE;}')
edit('static void show(HWND owner,const Formula& value){', '''HWND createPage(HWND owner){
        embedded=true;struct Template{DLGTEMPLATE t;WORD menu,cls,title;}t={};
        t.t.style=WS_CHILD|DS_CONTROL;t.t.cx=720;t.t.cy=500;
        return CreateDialogIndirectParamW(GetModuleHandleW(0),&t.t,owner,proc,(LPARAM)this);
    }
    static void show(HWND owner,const Formula& value){''')
p.write_text(s,encoding='utf-8',newline='\r\n');print('mass page updated')
