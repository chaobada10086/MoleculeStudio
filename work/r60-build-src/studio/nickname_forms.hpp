#ifndef STUDIO_NICKNAME_FORMS_HPP
#define STUDIO_NICKNAME_FORMS_HPP
// Connection order for abbreviation labels.
//
// A group drawn as text must touch the bond with the atom that actually bonds
// to the skeleton: -OAc must never look like it is attached through the acetate
// carbon, and -CO2Me must not read as attached through the ester oxygen. ChemDraw
// solves this by writing the group in connection order, so the same abbreviation
// has a right-hand bond spelling (connection last) and a left-hand bond spelling
// (connection first): OAc <-> AcO, CO2Me <-> MeO2C, OMe <-> MeO.
//
// Table entries are {left, right}: "left" is the spelling whose connecting atom
// is the FIRST character (label sits to the right of the bond), "right" is the
// spelling whose connecting atom is the LAST character (label sits to the left).
// Both spellings are recognised, so a label typed in either order is oriented
// correctly on either side.  Only the drawn glyph order changes; the stored
// label text, the atom and its semantics are untouched.
#include <map>
#include <string>
namespace nicknameform {
struct Pair{const char* left;const char* right;};
inline const Pair pairs[]={
    // oxygen-linked groups
    {"OH","HO"},{"SH","HS"},{"OAc","AcO"},{"OMe","MeO"},{"OEt","EtO"},{"OPr","PrO"},
    {"OiPr","iPrO"},{"OBu","BuO"},{"OtBu","tBuO"},{"OiBu","iBuO"},{"OPh","PhO"},
    {"OBn","BnO"},{"OBz","BzO"},{"OMs","MsO"},{"OTs","TsO"},{"OTf","TfO"},
    {"OTES","TESO"},{"OTMS","TMSO"},{"OTBS","TBSO"},{"OTBDMS","TBDMSO"},
    {"OTIPS","TIPSO"},{"OTBDPS","TBDPSO"},{"OCF3","F3CO"},{"OCOMe","MeOCO"},
    // nitrogen-linked groups
    {"NH2","H2N"},{"NHMe","MeHN"},{"NHEt","EtHN"},{"NMe2","Me2N"},{"NEt2","Et2N"},
    {"NHAc","AcHN"},{"NHBoc","BocHN"},{"NHCbz","CbzHN"},{"NHFmoc","FmocHN"},
    {"NHTs","TsHN"},{"NHNH2","H2NHN"},{"NO2","O2N"},{"NO","ON"},{"CN","NC"},
    // sulfur / phosphorus / boron
    {"SMe","MeS"},{"SEt","EtS"},{"SCF3","F3CS"},{"SO2Me","MeO2S"},{"SO2Ph","PhO2S"},
    {"SO2CF3","F3CO2S"},{"SO3H","HO3S"},{"SOPh","PhOS"},{"SOCH3","H3COS"},
    {"SO2NH2","H2NO2S"},{"PO3H2","H2O3P"},{"B(OH)2","(HO)2B"},
    // carbonyls and haloalkyls
    {"CHO","OHC"},{"COOH","HOOC"},{"CO2H","HO2C"},{"CO2Me","MeO2C"},{"CO2Et","EtO2C"},
    {"COOMe","MeOOC"},{"COOEt","EtOOC"},{"COOtBu","tBuOOC"},{"COOCH3","H3COOC"},
    {"CONH2","H2NOC"},{"CONHMe","MeHNOC"},{"CONMe2","Me2NOC"},{"CON3","N3OC"},
    {"COCl","ClOC"},{"COBr","BrOC"},{"COCH3","H3COC"},{"COCF3","F3COC"},
    {"CF3","F3C"},{"CHF2","F2CH"},{"CH2F","FCH2"},{"CH2OH","HOCH2"},{"CH2NH2","H2NCH2"},
    // alkyl / aryl formulas
    {"CH3","H3C"},{"C2H5","H5C2"},{"C3H7","H7C3"},{"C4H9","H9C4"},{"C5H11","H11C5"},
    {"C6H13","H13C6"},{"C7H15","H15C7"},{"C8H17","H17C8"},{"C9H19","H19C9"},
    {"C10H21","H21C10"},{"C11H23","H23C11"},{"C12H25","H25C12"},{"C13H27","H27C13"},
    {"C14H29","H29C14"},{"C15H31","H31C15"},{"C16H33","H33C16"},{"C17H35","H35C17"},
    {"C18H37","H37C18"},{"C19H39","H39C19"},{"C20H41","H41C20"},{"C6H5","H5C6"},
};
struct Forms{std::string left,right;};
inline const std::map<std::string,Forms>& index(){
    static const std::map<std::string,Forms> values=[](){
        std::map<std::string,Forms> result;
        for(const auto& pair:pairs){
            Forms forms{pair.left,pair.right};
            if(result.find(forms.left)==result.end())result[forms.left]=forms;
            if(result.find(forms.right)==result.end())result[forms.right]=forms;
        }
        return result;
    }();
    return values;
}
// bondOnRight: the neighbouring atom sits to the right, so the label is drawn to
// the left of the bond and its connecting atom must be the last character.
inline std::string oriented(const std::string& label,bool bondOnRight){
    auto it=index().find(label);if(it==index().end())return label;
    return bondOnRight?it->second.right:it->second.left;
}
inline bool known(const std::string& label){return index().find(label)!=index().end();}
}  // namespace nicknameform
#endif
