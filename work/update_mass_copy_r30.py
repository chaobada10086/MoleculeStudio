from pathlib import Path
p=Path(__file__).resolve().parents[1]/'studio/mass_dialog.hpp';s=p.read_text(encoding='utf-8')
def edit(old,new,count=1):
 global s
 assert s.count(old)==count,(old[:60],s.count(old));s=s.replace(old,new)
edit('            set(3,number(mz(a,true),4));\n            set(4,a.charge>0?L"正离子":L"负离子");','            set(3,a.charge>0?L"正离子":L"负离子");')
edit('if(adduct<0){studio_i18n::setWindowText(theoretical,L"—");EnableWindow(copy,FALSE);return;}','if(adduct<0||!formula.exactValid){studio_i18n::setWindowText(theoretical,L"—");EnableWindow(copy,FALSE);return;}')
edit('double calculated=formula.exactValid?mz(a,false):mz(a,true);','double calculated=mz(a,false);')
start=s.index('    std::wstring summary(HWND h)const{');end=s.index('    static void copyText',start)
s=s[:start]+'''    std::wstring copyValue(int id)const{
        if(id==507)return formula.valid?number(formula.averageMass,4):L"";
        if(id==505)return formula.exactValid?number(formula.exactMass,4):L"";
        int index=selectedAdduct();return index>=0&&formula.exactValid?number(mz(adducts()[size_t(index)],false),4):L"";
    }
'''+s[end:]
start=s.index('    void copyResult(HWND h){');end=s.index('    static INT_PTR CALLBACK proc',start)
s=s[:start]+'''    void copyResult(HWND h,int id=504){auto value=copyValue(id);if(!value.empty())copyText(h,value);}
'''+s[end:]
edit('control(L"STATIC",avg.c_str(),0,112,46,170,22,0,d->monoFont);','control(L"EDIT",avg.c_str(),508,116,46,126,24,ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,d->monoFont);\n            auto averageCopy=control(L"BUTTON",L"复制",507,248,43,54,28,WS_TABSTOP);EnableWindow(averageCopy,d->formula.valid);')
edit('control(L"STATIC",L"单同位素质量",0,300,46,105,22);','control(L"STATIC",L"单同位素质量",0,322,46,105,22);')
edit('control(L"STATIC",exact.c_str(),0,410,46,150,22,0,d->monoFont);','control(L"EDIT",exact.c_str(),509,430,46,130,24,ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,d->monoFont);\n            auto exactCopy=control(L"BUTTON",L"复制",505,568,43,54,28,WS_TABSTOP);EnableWindow(exactCopy,d->formula.exactValid);')
edit('control(L"STATIC",L"不饱和度 DBE",0,570,46,100,22);','control(L"STATIC",L"DBE",0,632,46,40,22);')
edit('control(L"STATIC",number(d->formula.dbe,1).c_str(),0,672,46,48,22,0,d->monoFont);','control(L"STATIC",number(d->formula.dbe,1).c_str(),0,678,46,42,22,0,d->monoFont);')
edit('const wchar_t* cols[]={L"加合物",L"电荷",L"单同位素 m/z",L"平均 m/z",L"模式"};\n            int widths[]={180,55,135,135,75};\n            for(int i=0;i<5;i++)','const wchar_t* cols[]={L"加合物",L"电荷",L"精确 m/z",L"模式"};\n            int widths[]={210,65,225,140};\n            for(int i=0;i<4;i++)')
edit('control(L"STATIC",L"所选加合物理论 m/z",0,20,372,130,22);','control(L"STATIC",L"所选加合物精确 m/z",0,20,372,154,24);')
edit('d->theoretical=control(L"STATIC",L"—",0,154,372,160,22,0,d->monoFont);','d->theoretical=control(L"EDIT",L"—",510,180,372,140,24,ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,d->monoFont);')
edit('d->copy=control(L"BUTTON",L"复制结果",504,20,414,110,32,WS_TABSTOP);\n            auto exactCopy=control(L"BUTTON",L"复制精确分子量",505,150,414,170,32,WS_TABSTOP);EnableWindow(exactCopy,d->formula.exactValid);','d->copy=control(L"BUTTON",L"复制",504,338,369,62,30,WS_TABSTOP);')
edit('control(L"STATIC",L"单同位素 m/z 用于高分辨质谱；平均 m/z 用于低分辨或分子量核对。",0,20,462,680,22);','')
edit('if(id==505){if(d->formula.exactValid)copyText(h,number(d->formula.exactMass,4));return TRUE;}','if(id==505||id==507){d->copyResult(h,id);return TRUE;}')
p.write_text(s,encoding='utf-8',newline='\r\n');print('mass copy controls and exact-only adduct columns updated')
