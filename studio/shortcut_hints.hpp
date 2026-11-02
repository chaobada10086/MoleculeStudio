#ifndef STUDIO_SHORTCUT_HINTS_HPP
#include "keymap_data.hpp"
#define STUDIO_SHORTCUT_HINTS_HPP
// Resolve hints through the same global -> hotspot -> generic precedence as
// App::shortcut. The catalog describes actions, not just visually similar tools.
namespace shortcut_hints {
struct Entry {std::string key,badge,action;bool related;Entry(std::string k,std::string b,std::string a,bool r):key(k),badge(b),action(a),related(r){}};
struct Context {std::vector<Entry> entries;std::string text,badge;};
inline const keymap::Hotkey* catalog(const ShortcutRow& row){return row.command=="@defaultHotkey"&&row.arg>=0&&size_t(row.arg)<sizeof(keymap::hotkeys)/sizeof(keymap::hotkeys[0])?&keymap::hotkeys[row.arg]:nullptr;}
inline std::string keyText(const ShortcutRow& row,bool badge=false){
    if(row.key==VK_SPACE&&row.modifiers==0)return "空格";
    if(row.key>='A'&&row.key<='Z'&&!(row.modifiers&(kModControl|kModAlt))){char letter=char(row.key);if(!(row.modifiers&kModShift))letter=char(tolower(letter));return (!badge&&(row.modifiers&kModShift)?std::string("Shift+"):std::string())+letter;}
    if(auto h=catalog(row))if(row.key==row.defaults&&row.modifiers==row.defaultModifiers)return h->key;
    return U(ShortcutConfig::chord(row.key,row.modifiers));
}
inline const ShortcutRow* effective(const ShortcutConfig& config,UINT key,uint32_t modifiers,int scope){auto r=config.match(key,modifiers,0);if(!r)r=config.match(key,modifiers,scope);if(!r)r=config.match(key,modifiers,1);return r;}
inline std::string toolCommand(const std::string& value,const std::string& arrowCommand=std::string()){
    static const std::map<std::string,std::string> tools={{"BENZENE","chooseRingTool_Benzene"},{"CYCLOPENTADIENE","chooseRingTool_Cyclopentadiene"},{"SOLIDBOND","chooseBondTool_Solid"},{"ACYCLICCHAIN","chooseAlkaneTool_Acyclic_Chain"},{"CHEMICALSYMBOLS","chooseSymbolTool_CirclePlus"},{"TEXT","chooseEdittingTool_Text"},{"BRACKET","chooseBracketTool_SquareBrackets"},{"TLCPLATE","chooseTLCPlateTool"},{"ORBITAL","chooseOrbitalTool_p_Shaded"},{"MARQUEE","chooseSelectionTool_Marquee"}};
    // The arrow shortcut fires whichever preset the Graphics tab shows, so the
    // hint must be resolved against the live choice, not the catalog default.
    if(value=="ARROW")return arrowCommand.empty()?"chooseArrowTool_Large_Solid":arrowCommand;
    auto found=tools.find(value);return found==tools.end()?std::string():found->second;
}
inline std::string atomAction(const Button& button,const keymap::Hotkey& h){
    std::string command=h.command,value=h.value,b=button.cmd;
    if(command=="SPROUT"){
        if(b=="chooseBondTool_Solid"&&(value=="0"||value=="1"))return value=="0"?"添加单键（环状模式）":"添加单键（链状模式）";
        static const std::map<std::string,std::string> recipe={{"chooseMultipleBondsTool_DoubleBond","8"},{"chooseMultipleBondsTool_TripleBond","10"},{"chooseBondTool_Wedged","4"},{"chooseBondTool_Hashed_Wedged","5"},{"chooseRingTool_Cyclopropane","11"},{"chooseRingTool_Cyclobutane","12"},{"chooseRingTool_Cyclopentane","7"},{"chooseRingTool_Cyclohexane","6"},{"chooseRingTool_Benzene","3"},{"chooseRingTool_Cyclopentadiene","15"}};
        auto found=recipe.find(b);if(found!=recipe.end()&&found->second==value){if(value=="8")return "添加 =CH₂";if(value=="10")return "添加 C≡CH";return "添加"+button.label;}
        if(b=="chooseMultipleBondsTool_DoubleBond"&&value=="2")return "在原碳上添加 =O";
        if(b=="chooseRingTool_Benzene"&&value=="16")return "添加苯基";
    }
    if(command=="SELECT"&&b=="chooseSelectionTool_Marquee")return "选中捕捉原子";
    if(command=="CHARGE"&&((b=="chooseSymbolTool_CirclePlus"&&value=="+")||(b=="chooseSymbolTool_CircleMinus"&&value=="-")))return value=="+"?"原子电荷 +1":"原子电荷 −1";
    return "";
}
inline std::string bondAction(const Button& button,const keymap::Hotkey& h){
    std::string command=h.command,value=h.value,b=button.cmd;
    if(command=="BONDORDER"&&((b=="chooseBondTool_Solid"&&value=="1")||(b=="chooseMultipleBondsTool_DoubleBond"&&value=="2")||(b=="chooseMultipleBondsTool_TripleBond"&&value=="3")))return "改为"+button.label;
    if(command=="BONDDOUBLEPOSITION"&&b=="chooseMultipleBondsTool_DoubleBond")return value=="LEFT"?"双键靠左":value=="RIGHT"?"双键靠右":"双键居中";
    if(command=="BONDDISPLAY"){
        static const std::map<std::string,std::string> display={{"chooseBondTool_Dashed","DASHED"},{"chooseBondTool_Hashed","HASHED"},{"chooseBondTool_Hashed_Wedged","HASHEDWEDGED"},{"chooseBondTool_Bold","BOLD"},{"chooseBondTool_Wedged","WEDGED"},{"chooseBondTool_Wavy","WAVY"}};
        auto found=display.find(b);if(found!=display.end()&&found->second==value)return "改为"+button.label;
        if(b=="chooseBondTool_Dashed"&&value=="TAUTOMERIC")return "改为双虚线";
        if(b=="chooseBondTool_Bold"&&value=="DOUBLEBOLD")return "改为双粗键";
    }
    if(command=="FUSERING"){
        static const std::map<std::string,std::string> rings={{"chooseRingTool_Cyclopropane","3"},{"chooseRingTool_Cyclobutane","4"},{"chooseRingTool_Cyclopentane","5"},{"chooseRingTool_Cyclohexane","6"},{"chooseRingTool_Cycloheptane","7"},{"chooseRingTool_Cyclooctane","8"},{"chooseRingTool_Benzene","2"},{"chooseRingTool_Cyclopentadiene","1"},{"chooseRingTool_Chair_Cyclohexane1","9"},{"chooseRingTool_Chair_Cyclohexane2","10"}};
        auto found=rings.find(b);if(found!=rings.end()&&found->second==value)return "在该键上稠合"+button.label;
    }
    if(command=="SELECT"&&b=="chooseSelectionTool_Marquee")return "选中捕捉键";
    return "";
}
inline Context describe(const ShortcutConfig& config,const Button& button,int scope,const std::string& arrowCommand=std::string(),const std::string& arrowName=std::string()){
    Context result;
    for(const auto& row:config.rows){
        if(!row.key||effective(config,row.key,row.modifiers,scope)!=&row)continue;
        std::string action;bool related=false;
        bool argumentMatters=button.cmd=="@flag"||button.cmd=="@align"||button.cmd=="@distribute"||button.cmd=="@flip"||button.cmd=="@textalign"||button.cmd=="@size"||button.cmd=="@zoom"||button.cmd=="@rotate";
        if(row.command==button.cmd&&(!argumentMatters||row.arg==button.arg))action=button.label;
        if(auto h=catalog(row)){
            std::string command=h->command,value=h->value;
            if(command=="TOOLMODE"){
                std::string target=toolCommand(value,arrowCommand);
                if(target==button.cmd)action="选择"+button.label+"工具";
                if(button.cmd=="chooseAlkaneTool_Snaking_Chain"&&value=="ACYCLICCHAIN"){action="选择直链工具（曲折链请点图标）";related=true;}
                if(value=="ARROW"){
                    if(button.cmd=="@arrows"){action="当前箭头样式："+(arrowName.empty()?std::string("直箭头"):arrowName);related=true;}
                    else if(button.cmd=="chooseArrowTool_Large_Equilibrium"&&button.cmd!=arrowCommand){action="改按当前箭头样式（其他类型请点图标）";related=true;}
                }
            }
            if(row.scope==2)action=atomAction(button,*h);
            if(row.scope==3)action=bondAction(button,*h);
            if(command=="DIALOG"&&value=="CHOOSENICKNAME"&&button.cmd=="@nickname")action="打开缩写库";
        }
        if(!action.empty())result.entries.push_back({keyText(row),keyText(row,true),action,related});
    }
    // Put the main edit before double-bond alignment and related options.
    std::stable_sort(result.entries.begin(),result.entries.end(),[](const Entry& a,const Entry& b){return (a.action.find("双键")==0?1:0)<(b.action.find("双键")==0?1:0);});
    // Group aliases for one action, without losing case or custom key bindings.
    std::vector<std::pair<std::string,std::string>> groups;
    for(const auto& entry:result.entries){auto found=std::find_if(groups.begin(),groups.end(),[&](const std::pair<std::string,std::string>& group){return group.first==entry.action;});if(found==groups.end())groups.push_back({entry.action,entry.key});else found->second+=" / "+entry.key;
        if(!entry.related&&result.badge.empty())result.badge=entry.badge;
    }
    for(const auto& group:groups){if(!result.text.empty())result.text+="  ·  ";result.text+=group.second+"  "+group.first;}
    if(result.text.empty())result.text="无可用快捷键，点击图标";
    if(result.badge.empty()||result.badge.size()>9)result.badge=result.badge.empty()?"—":result.entries.front().badge;
    return result;
}
}
#endif
