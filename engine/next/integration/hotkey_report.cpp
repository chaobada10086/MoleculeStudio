#include "session.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "next/interaction/keymap.hpp"
#include "next/interaction/router.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
namespace r62 { namespace integration {
using namespace interaction;

static std::string vkName(uint32_t vk){
    if(vk>='A'&&vk<='Z')return std::string(1,char(vk));
    if(vk>='0'&&vk<='9')return std::string(1,char(vk));
    if(vk>=VK_F1&&vk<=VK_F24)return "F"+std::to_string(vk-VK_F1+1);
    switch(vk){
    case VK_LEFT:return "Left";case VK_RIGHT:return "Right";case VK_UP:return "Up";case VK_DOWN:return "Down";
    case VK_RETURN:return "Enter";case VK_ESCAPE:return "Escape";case VK_DELETE:return "Delete";
    case VK_BACK:return "Backspace";case VK_SPACE:return "Space";case VK_TAB:return "Tab";
    case VK_HOME:return "Home";case VK_END:return "End";case VK_PRIOR:return "PageUp";case VK_NEXT:return "PageDown";
    case VK_INSERT:return "Insert";case VK_ADD:return "NumpadPlus";case VK_SUBTRACT:return "NumpadMinus";
    case VK_MULTIPLY:return "NumpadMultiply";case VK_DIVIDE:return "NumpadDivide";case VK_DECIMAL:return "NumpadDecimal";
    case VK_OEM_PERIOD:return ".";case VK_OEM_COMMA:return ",";case VK_OEM_1:return ";";
    case VK_OEM_2:return "/";case VK_OEM_3:return "~";case VK_OEM_4:return "[";case VK_OEM_5:return "\\";
    case VK_OEM_6:return "]";case VK_OEM_7:return "'";case VK_OEM_PLUS:return "=";case VK_OEM_MINUS:return "-";
    default:break;
    }
    return "VK_"+std::to_string(vk);
}
static std::string charName(uint32_t ch){
    if(ch>=33&&ch<127)return std::string(1,char(ch));
    if(ch==32)return "Space";
    char buf[16];std::snprintf(buf,sizeof(buf),"U+%04X",ch);return buf;
}
static std::string modName(uint32_t m){
    std::string s;
    if(m&kModControl)s+="Ctrl+";
    if(m&kModShift)s+="Shift+";
    if(m&kModAlt)s+="Alt+";
    if(s.empty())return "-";
    s.resize(s.size()-1);
    return s;
}
static std::string q(const std::string& s){
    bool need=s.find(',')!=std::string::npos||s.find('"')!=std::string::npos||s.find('\n')!=std::string::npos;
    if(!need)return s;
    std::string o="\"";for(size_t i=0;i<s.size();++i){if(s[i]=='"')o+="\"\"";else o+=s[i];}o+="\"";return o;
}
static std::string evidenceFor(HotkeyAction a){
    switch(a){
    case HotkeyAction::SetAtomElement:return "tests/T19/integration/usability.cpp n/C/c element checks";
    case HotkeyAction::SelectMolecule:return "usability.cpp space selects connected molecule";
    case HotkeyAction::SetBondOrderSingle:case HotkeyAction::SetBondOrderDouble:case HotkeyAction::SetBondOrderTriple:
        return "usability.cpp bond hotspot 1/2/3";
    case HotkeyAction::ActivateBenzeneTool:return "usability.cpp generic j activates benzene";
    case HotkeyAction::FuseBenzene:return "usability.cpp aromatic fusion explicit refusal; saturated fusion reuse";
    case HotkeyAction::ToggleFixedLength:case HotkeyAction::ToggleFixedAngles:return "usability.cpp Ctrl+L / Ctrl+E toggle";
    case HotkeyAction::Undo:case HotkeyAction::Redo:return "acceptance.cpp / usability.cpp digest round trip";
    case HotkeyAction::EditSelectAll:return "acceptance.cpp mixed document select-all move";
    case HotkeyAction::ZoomFit:case HotkeyAction::ZoomActualSize:return "window menu / F5-F8 dispatch counters";
    default:return "";
    }
}
int dumpHotkeys(const std::string& path){
    std::ofstream o(path.c_str(),std::ios::binary);
    if(!o)return 1;
    o<<"\xEF\xBB\xBF";
    o<<"Key,Modifiers,CaseSensitive,Context,Action,Source,SourceCitation,ConflictRule,Implemented,TestEvidence,Notes\n";
    const std::string focusRule="Only dispatched while InputFocus==Canvas; TextEntry/IME/modal dialog consumes the key first (T04 input_state). ";
    const Keymap& km=Keymap::defaults();
    const std::vector<HotkeyBinding>& bs=km.bindings();
    for(size_t i=0;i<bs.size();++i){
        const HotkeyBinding& b=bs[i];
        std::string key=b.virtualKey?vkName(b.virtualKey):charName(b.character);
        std::string cs=(b.character>='A'&&b.character<='Z')?"uppercase":((b.character>='a'&&b.character<='z')?"lowercase":"n/a");
        std::string conflict;
        if(b.context==HotspotContext::Atom)conflict="Atom hotspot table wins over Bond/None; accelerator table resolved first.";
        else if(b.context==HotspotContext::Bond)conflict="Bond hotspot table wins over generic None; Atom wins over Bond.";
        else conflict="Generic no-hotspot table; any atom/bond hotspot key takes priority.";
        std::string notes;
        if(b.element)notes+="element="+std::to_string(b.element)+" ";
        if(!b.label.empty())notes+="label="+b.label+" ";
        if(b.parameter)notes+="parameter="+std::to_string(b.parameter)+" ";
        if(b.forbiddenModifiers)notes+="forbiddenModifiers="+modName(b.forbiddenModifiers)+" ";
        o<<q(key)<<','<<q(modName(b.modifiers))<<','<<cs<<','<<hotspotContextName(b.context)<<','
         <<q(hotkeyActionName(b.action))<<','<<hotkeySourceName(b.source)<<','<<q(hotkeySourceCitation(b.source))<<','
         <<q(conflict+focusRule)<<','<<(b.implemented?"Implemented":"Unsupported")<<','<<q(evidenceFor(b.action))<<','<<q(notes)<<"\n";
    }
    const std::vector<Accelerator>& as=defaultAccelerators();
    for(size_t i=0;i<as.size();++i){
        const Accelerator& a=as[i];
        o<<q(vkName(a.virtualKey))<<','<<q(modName(a.modifiers))<<",n/a,Global(accelerator),"
         <<q(hotkeyActionName(a.action))<<','<<hotkeySourceName(a.source)<<','<<q(hotkeySourceCitation(a.source))<<','
         <<q(std::string("Accelerator table resolved before hotspot keys. ")+focusRule)<<','<<(a.implemented?"Implemented":"Unsupported")<<','
         <<q(evidenceFor(a.action))<<','<<q("accelerator (not hotspot dependent)")<<"\n";
    }
    o.close();
    return 0;
}
} }
