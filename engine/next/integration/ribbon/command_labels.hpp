#pragma once
#include <string>
#include <vector>
namespace r62 { namespace integration { namespace ribbon {
inline std::string lower(std::string s){for(size_t i=0;i<s.size();++i)if(s[i]>='A'&&s[i]<='Z')s[i]=char(s[i]-'A'+'a');return s;}

struct Tok { const char* en; const char* zh; };
inline std::string tokenZh(const std::string& t){
    static constexpr Tok kTokens[] = {
    {"Selection","选择"},{"Lasso","套索"},{"Marquee","框选"},{"Editting","编辑"},{"Eraser","橡皮擦"},
    {"Bond","键"},{"MultipleBonds","多重键"},{"Ring","环"},{"Alkane","碳链"},{"Chain","链"},
    {"Arrow","箭头"},{"Drawing","图形"},{"Bracket","括号"},{"Symbol","符号"},{"PushElectron","电子推移"},
    {"PushRadical","自由基推移"},{"Push","推移"},{"Orbital","轨道"},{"Bio","生物"},{"GenericShape","图形"},
    {"Query","查询"},{"Pen","曲线"},{"Table","表格"},{"Template","模板"},{"TLCPlate","薄层色谱板"},
    {"GEPPlate","凝胶电泳板"},{"Fragment","片段"},{"Perspective","透视"},{"Structure","结构"},
    {"Text","文字"},{"View","视图"},{"File","文件"},
    {"Single","单"},{"Double","双"},{"Triple","三"},{"Quadruple","四"},{"Aromatic","芳香"},
    {"Any","任意"},{"Dative","配位"},{"Unspecified","未指定"},{"Solid","实心"},{"Bold","粗"},
    {"Dashed","虚线"},{"Dash","虚线"},{"Dotted","点线"},{"Wavy","波浪"},{"Hashed","哈希"},
    {"Wedged","楔形"},{"Wedge","楔形"},{"Hollow","空心"},{"Normal","普通"},{"Or","或"},
    {"Hydrogen","氢"},
    {"Large","大"},{"Medium","中"},{"Small","小"},{"Left","左"},{"Right","右"},{"Center","居中"},
    {"OneSided","半"},{"Resonance","共振"},{"Equilibrium","平衡"},{"Unbalanced","不等长"},
    {"Retrosynthetic","逆合成"},{"NoGo","禁止"},{"Cross","叉"},{"Hash","斜线"},{"Eliptical","椭圆"},
    {"CW","顺时针"},{"CCW","逆时针"},{"Rectangle","矩形"},{"Oval","椭圆"},{"Circle","圆"},
    {"RoundRect","圆角矩形"},{"Line","直线"},{"Arc","圆弧"},{"Shaded","阴影"},{"Shadowed","投影"},
    {"Square","方形"},{"Parenthesis","圆括号"},{"CurlyBrackets","花括号"},{"CurlyBracket","花括号"},
    {"SquareBrackets","方括号"},{"SquareBracket","方括号"},{"Dagger","匕首"},
    {"SingleDagger","单匕首"},{"DoubleDagger","双匕首"},
    {"Plus","加号"},{"Minus","减号"},{"RadicalCation","正离子自由基"},{"RadicalAnion","负离子自由基"},
    {"DoubleRadical","双自由基"},{"Radical","自由基"},{"HDot","氢点"},{"HDash","氢短划线"},
    {"Attachment","连接点"},{"Diamond","菱形"},{"Star","星形"},{"PolymerBeadAttachmentPoint","聚合物珠连接点"},
    {"Cyclopropane","三元环"},{"Cyclobutane","四元环"},{"Cyclopentane","五元环"},{"Cyclohexane","六元环"},
    {"Cycloheptane","七元环"},{"Cyclooctane","八元环"},{"Benzene","苯环"},{"Cyclopentadiene","环戊二烯"},
    {"Chair","椅式"},{"Acyclic","直链"},{"Snaking","蛇形"},{"DrawCurve","绘制曲线"},{"EditCurve","编辑曲线"},
    {"p","p轨道"},{"s","s轨道"},{"Sigma","σ轨道"},{"spLobe","sp单叶"},{"sp3","sp3杂化"},
    {"dxy","dxy轨道"},{"dz2","dz2轨道"},{"Open","轮廓"},
    {"Substrate","底物"},{"Enzyme","酶"},{"Kinase","激酶"},{"GProtein","G蛋白"},{"Alpha","α"},
    {"Beta","β"},{"Gamma","γ"},{"Receptor","受体"},{"IonChannel","离子通道"},
    {"Immunoglobin","免疫球蛋白"},{"NuclearPore","核孔"},{"HelixProtein","螺旋蛋白"},{"DNA","DNA"},
    {"tRNA","tRNA"},{"mRNA","mRNA"},{"Membrane","膜"},{"Ellipse","椭圆"},{"Micelle","胶束"},
    {"Cloud","云"},{"Ball","球"},{"Triangle","三角"},{"Hexagon","六边形"},{"Octagon","八边形"},
    {"Mitochondrion","线粒体"},{"Golgi","高尔基体"},{"Er","内质网"},{"ER","内质网"},
    {"Centriole","中心粒"},{"RibosomeA","核糖体A"},{"RibosomeB","核糖体B"},{"Ribosome","核糖体"},
    {"Virus","病毒"},{"HIV","HIV"},{"Bacterium","细菌"},{"Bacteriophage","噬菌体"},
    {"Paramecium","草履虫"},{"Amoeba","变形虫"},{"PlasmidMap","质粒图谱"},
    {"ReactionAtomAtomMap","反应原子映射"},{"AlternativeGroup","替代基团"},{"FreeSite","自由位点"},
    {"AbsoluteStereochemistry","绝对立体化学"},{"RacemicStereochemistry","外消旋立体化学"},
    {"RelativeStereochemistry","相对立体化学"},{"MassFragmentation","质谱碎片"},
    {"Synthesis","合成"},{"Retrosynthesis","逆合成"}
    };
    const auto key = lower(t);
    for(const auto& token : kTokens)if(key==lower(token.en))return token.zh;
    return std::string();
}

inline std::vector<std::string> tokens(const std::string& s){
    std::vector<std::string> out;std::string cur;
    for(size_t i=0;i<s.size();++i){
        char c=s[i];
        if(c=='_'||c=='-'||c==' '){if(!cur.empty()){out.push_back(cur);cur.clear();}continue;}
        if(c>='0'&&c<='9'){if(!cur.empty()){out.push_back(cur);cur.clear();}out.push_back(std::string(1,c));continue;}
        bool upper=c>='A'&&c<='Z';
        if(upper&&!cur.empty()&&!(cur.size()==1&&cur[0]>='0'&&cur[0]<='9')){
            out.push_back(cur);cur.clear();
        }
        cur.push_back(c);
    }
    if(!cur.empty())out.push_back(cur);
    return out;
}

inline std::string zhCategory(const std::string& c){
    if(c.find("BioDraw")!=std::string::npos)return "生物";
    if(c.find("选择")!=std::string::npos)return "选择";
    if(c.find("化学键")!=std::string::npos)return "化学键";
    if(c.find("环结构")!=std::string::npos)return "环";
    if(c.find("碳链")!=std::string::npos)return "链";
    if(c.find("文字")!=std::string::npos)return "文字";
    if(c.find("箭头")!=std::string::npos)return "箭头";
    if(c.find("几何图形")!=std::string::npos)return "图形";
    if(c.find("自由曲线")!=std::string::npos)return "曲线";
    if(c.find("括号")!=std::string::npos)return "括号";
    if(c.find("符号")!=std::string::npos)return "符号";
    if(c.find("轨道")!=std::string::npos)return "轨道";
    if(c.find("查询")!=std::string::npos)return "查询";
    if(c.find("片段")!=std::string::npos)return "片段";
    if(c.find("实验")!=std::string::npos)return "实验图示";
    if(c.find("透视")!=std::string::npos)return "透视";
    if(c.find("表格")!=std::string::npos)return "表格";
    if(c.find("模板")!=std::string::npos)return "模板";
    return c;
}

struct Override { const char* command; const char* zh; };
inline std::string zhLabelForCommand(const std::string& command,const std::string& category){
    static constexpr Override kOverrides[] = {
    {"chooseSelectionTool_Marquee","框选选择"},{"chooseSelectionTool_Lasso","套索选择"},
    {"chooseEdittingTool_Eraser","橡皮擦"},{"chooseEdittingTool_Text","文字/原子标签"},
    {"chooseBondTool_Solid","单键"},{"chooseBondTool_Bold","粗单键"},{"chooseBondTool_Dashed","虚线单键"},
    {"chooseBondTool_Hashed","哈希键"},{"chooseBondTool_Hashed_Wedged","虚楔键"},
    {"chooseBondTool_Wedged","楔形键"},{"chooseBondTool_Hollow_Wedged","空心楔键"},
    {"chooseBondTool_Wavy","波浪键"},{"chooseBondTool_DottedHydrogen","氢键(点线)"},
    {"chooseMultipleBondsTool_DoubleBond","双键"},{"chooseMultipleBondsTool_TripleBond","三键"},
    {"chooseMultipleBondsTool_QuadrupleBond","四键"},{"chooseMultipleBondsTool_AnyBond","任意键"},
    {"chooseMultipleBondsTool_BoldDoubleBond","粗双键"},{"chooseMultipleBondsTool_DashedDoubleBond","虚线双键"},
    {"chooseMultipleBondsTool_DoubleDashedDoubleBond","双虚线双键"},
    {"chooseMultipleBondsTool_SingleOrDoubleBond","单或双键"},{"chooseMultipleBondsTool_DoubleEitherBond","双键(或)"},
    {"chooseMultipleBondsTool_DativeBond","配位键"},
    {"chooseMultipleBondsTool_SingleOrAromaticBond","单或芳香键"},
    {"chooseMultipleBondsTool_DoubleOrAromaticBond","双或芳香键"},
    {"chooseRingTool_Benzene","苯环"},{"chooseRingTool_Cyclopentane","五元环"},
    {"chooseRingTool_Cyclohexane","六元环"},{"chooseRingTool_Cyclopentadiene","环戊二烯"},
    {"chooseRingTool_Chair_Cyclohexane1","椅式六元环 1"},{"chooseRingTool_Chair_Cyclohexane2","椅式六元环 2"},
    {"chooseAlkaneTool_Acyclic_Chain","直链碳链"},{"chooseAlkaneTool_Snaking_Chain","蛇形碳链"},
    {"chooseArrowTool_Large_Solid","实心箭头"},{"chooseArrowTool_Large_Equilibrium","平衡箭头"},
    {"chooseArrowTool_Large_Resonance","共振箭头"},{"chooseArrowTool_Large_Retrosynthetic","逆合成箭头"},
    {"choosePenTool_DrawCurve","绘制曲线"},{"choosePenTool_EditCurve","编辑曲线"},
    {"chooseTableTool","表格"},{"chooseTemplateTool","模板库"},{"chooseStructurePerspectiveTool","结构透视"},
    {"choosePushElectronTool","电子推移"},{"choosePushRadicalTool","自由基推移"},
    {"chooseTLCPlateTool","薄层色谱板"},{"chooseGEPPlateTool","凝胶电泳板"}
    };
    for(const auto& entry : kOverrides)if(command==entry.command)return entry.zh;
    std::string body=command;
    if(body.size()>6&&body.compare(0,6,"choose")==0)body=body.substr(6);
    size_t us=body.find('_');
    std::string tool=us==std::string::npos?body:body.substr(0,us);
    std::string rest=us==std::string::npos?std::string():body.substr(us+1);
    bool bio=category.find("BioDraw")!=std::string::npos;
    std::vector<std::string> tks=tokens(rest.empty()?tool:rest);
    if(bio&&!tks.empty()&&(lower(tks[0])=="bio"||lower(tks[0])=="generic"||lower(tks[0])=="shape"))tks.erase(tks.begin());
    std::string out;
    for(size_t i=0;i<tks.size();++i){std::string z=tokenZh(tks[i]);out+=z.empty()?tks[i]:z;}
    if(out.empty())out=tool.empty()?command:tool;
    return out;
}

}}}
