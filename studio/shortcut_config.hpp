#ifndef STUDIO_SHORTCUT_CONFIG_HPP
#define STUDIO_SHORTCUT_CONFIG_HPP
#include <functional>
#include "shortcut_legacy.hpp"
struct ShortcutRow {
    std::string id,label,command;int arg=0,scope=0;UINT key=0,defaults=0;uint32_t modifiers=0,defaultModifiers=0;
    interaction::HotkeyBinding chemistry;
};
struct ShortcutConfig;
inline void loadDefaultShortcuts(ShortcutConfig&);
struct ShortcutConfig {
    std::vector<ShortcutRow> rows;interaction::Keymap chemistry;std::wstring path;uint64_t generation=0;
    static std::wstring chord(UINT key,uint32_t modifiers){if(!key)return L"未分配";std::wstring s;if(modifiers&kModControl)s+=L"Ctrl+";if(modifiers&kModAlt)s+=L"Alt+";if(modifiers&kModShift)s+=L"Shift+";if(key==VK_SPACE)return s+L"Space";if(key==VK_DELETE)return s+L"Delete";if(key==VK_BACK)return s+L"Backspace";if(key==VK_RETURN)return s+L"Enter";if(key==VK_ESCAPE)return s+L"Esc";wchar_t name[80]={};UINT scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC);GetKeyNameTextW(LONG(scan<<16),name,80);return s+name;}
    void add(std::string id,std::string label,std::string command,UINT key,uint32_t modifiers=0,int arg=0,int scope=0){ShortcutRow r;r.id=id;r.label=label;r.command=command;r.arg=arg;r.scope=scope;r.key=r.defaults=key;r.modifiers=r.defaultModifiers=modifiers;rows.push_back(r);}
    ShortcutConfig(){const uint32_t C=kModControl,S=kModShift,A=kModAlt;
        add("new","新建","@new",'N',C);add("open","打开","@open",'O',C);add("save","保存","@save",'S',C);add("saveas","另存为","@saveas",'S',C|S);add("page","页面设置","@page",'P',C|S);add("close","关闭文档","@close",'W',C);
        add("undo","撤销","@undo",'Z',C);add("redo","重做","@redo",'Z',C|S);add("repeat","重复上一步","@repeat",'Y',C);add("cut","剪切","@cut",'X',C);add("copy","复制","@copy",'C',C);add("copyxml","复制为 CDXML","@copyxml",'D',C);add("paste","粘贴","@paste",'V',C);add("all","全选","@all",'A',C);add("delete","删除选中对象","@delete",VK_DELETE);add("backspace","删除选中对象","@delete",VK_BACK);
        add("actual","实际大小","@actual",VK_F5);add("fit","适合窗口","@fit",VK_F6);add("zoomIn","放大","@zoom",VK_F7,0,1);add("zoomOut","缩小","@zoom",VK_F8,0,-1);add("fixedLength","固定键长","@fixedLength",'L',C);add("fixedAngles","固定角度","@fixedAngles",'E',C);
        add("group","编组","@group",'G',C);add("ungroup","解组","@ungroup",'G',C|S);add("join","连接/合并","@merge",'J',C);add("flipH","水平翻转","@flip",'H',C|S,0);add("flipV","垂直翻转","@flip",'V',C|S,1);
        const char* labels[]={"左边缘对齐","水平居中对齐","右边缘对齐","顶部对齐","垂直居中对齐","底部对齐"};const char* ids[]={"alignL","alignC","alignR","alignT","alignM","alignB"};const char keys[]={'L','C','R','T','M','B'};for(int i=0;i<6;i++)add(ids[i],labels[i],"@align",keys[i],C|S|A,i);add("distH","水平分布","@distribute",'H',C|S|A,0);add("distV","垂直分布","@distribute",'V',C|S|A,1);
        add("bold","粗体","@flag",'B',C,kBold);add("italic","斜体","@flag",'I',C,kItalic);add("underline","下划线","@flag",'U',C,kUnderline);add("formula","化学式","@flag",'F',C,kFormula);add("plain","常规文字","@plain",'T',C);add("subscript","下标","@flag",VK_F9,0,kSubscript);add("superscript","上标","@flag",VK_F10,0,kSuperscript);
        add("textLeft","文字左对齐","@textalign",'L',C|S,0);add("textCenter","文字居中","@textalign",'C',C|S,1);add("textRight","文字右对齐","@textalign",'R',C|S,2);add("textJustify","文字两端对齐","@textalign",'J',C|S,3);
        for(int i=0;i<4;i++){UINT k=UINT(VK_LEFT+i);add("nudge"+std::to_string(i),"微移（1 磅）","@nudge",k,0,int(k));add("nudge10"+std::to_string(i),"微移（10 磅）","@nudge",k,S,int(k)+256);add("rotate"+std::to_string(i),"旋转（左右 1°，上下 15°）","@rotateStep",k,A,k==VK_LEFT?-1:k==VK_RIGHT?1:k==VK_UP?-15:15);}
        add("rotateDialog","旋转角度…","@rotateDialog",'R',C);add("scaleDialog","缩放对象…","@scaleDialog",'K',C);
        add("marquee","框选工具","chooseSelectionTool_Marquee",VK_SPACE,0,0,1);add("bond","单键工具","chooseBondTool_Solid",'X',0,0,1);add("chain","碳链工具","chooseAlkaneTool_Snaking_Chain",'X',S,0,1);add("text","文字工具","chooseEdittingTool_Text",'T',0,0,1);add("bracket","括号工具","chooseBracketTool_SquareBrackets",'T',S,0,1);add("arrow","箭头工具","@arrowTool",'E',0,0,1);add("symbol","符号工具","chooseSymbolTool_CirclePlus",'E',S,0,1);add("benzene","苯环工具","chooseRingTool_Benzene",'J',0,0,1);add("diene","二烯环工具","chooseRingTool_Cyclopentadiene",'J',S,0,1);add("tlc","薄层板工具","chooseTLCPlateTool",'G',0,0,1);add("orbital","轨道工具","chooseOrbitalTool_p_Shaded",'G',S,0,1);
        size_t n=0;for(auto b:interaction::Keymap::defaults().bindings())if(b.context!=interaction::HotspotContext::None){ShortcutRow r;r.id="hotspot"+std::to_string(n++);r.scope=b.context==interaction::HotspotContext::Atom?2:3;r.label=interaction::hotkeyActionName(b.action);if(!b.label.empty())r.label+=" · "+b.label;if(b.element)r.label+=" · "+std::string(core::elementSymbol(b.element));r.chemistry=b;r.key=b.virtualKey;r.modifiers=b.modifiers;if(b.character){SHORT v=VkKeyScanW(wchar_t(b.character));if(v!=-1){r.key=LOBYTE(v);r.modifiers|=(HIBYTE(v)&1)?S:0;}}r.defaults=r.key;r.defaultModifiers=r.modifiers;rows.push_back(r);}
        loadDefaultShortcuts(*this);
        wchar_t dir[32768]={};GetModuleFileNameW(nullptr,dir,32768);path=dir;path=path.substr(0,path.find_last_of(L"\\/"))+L"\\shortcuts.ini";load();
    }
    void rebuild(){++generation;chemistry=interaction::Keymap();for(auto& r:rows)if(r.scope>=2&&r.key){auto b=r.chemistry;b.virtualKey=r.key;b.character=0;b.modifiers=r.modifiers;chemistry.add(b);}}
    void reset(){for(auto& r:rows){r.key=r.defaults;r.modifiers=r.defaultModifiers;}rebuild();}
    bool conflict(size_t index,UINT key,uint32_t modifiers)const{if(!key)return false;auto& a=rows[index];for(size_t i=0;i<rows.size();i++){auto& b=rows[i];if((a.command=="@delete"&&b.scope>=2)||(b.command=="@delete"&&a.scope>=2))continue;if(i!=index&&key==b.key&&modifiers==b.modifiers&&(a.scope==b.scope||a.scope==0||b.scope==0))return true;}return false;}
    void load(){for(auto& r:rows){auto id=W(r.id),legacy=W(legacyShortcutId(r.id));UINT oldKey=GetPrivateProfileIntW(L"Keys",legacy.c_str(),r.defaults,path.c_str());uint32_t oldModifiers=GetPrivateProfileIntW(L"Modifiers",legacy.c_str(),r.defaultModifiers,path.c_str());r.key=GetPrivateProfileIntW(L"Keys",id.c_str(),oldKey,path.c_str());r.modifiers=GetPrivateProfileIntW(L"Modifiers",id.c_str(),oldModifiers,path.c_str());if(r.key>255||r.modifiers>7){r.key=r.defaults;r.modifiers=r.defaultModifiers;}}rebuild();}
    bool save()const{for(auto& r:rows){auto id=W(r.id),key=std::to_wstring(r.key),m=std::to_wstring(r.modifiers);if(!WritePrivateProfileStringW(L"Keys",id.c_str(),key.c_str(),path.c_str())||!WritePrivateProfileStringW(L"Modifiers",id.c_str(),m.c_str(),path.c_str()))return false;}return true;}
    const ShortcutRow* match(UINT key,uint32_t modifiers,int scope)const{for(auto& r:rows)if(r.scope==scope&&r.key&&r.key==key&&r.modifiers==modifiers)return &r;return nullptr;}
};
#include "shortcut_catalog.hpp"
#endif
