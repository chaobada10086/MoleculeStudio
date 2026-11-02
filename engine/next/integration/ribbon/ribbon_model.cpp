#include "ribbon_model.hpp"
#include "next/interaction/router.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <cstring>

namespace r62 { namespace integration {
#include "next/integration/source_tools.hpp"
namespace ribbon {

using namespace r62::interaction;

const char* dispName(Disp d){
    switch(d){case Disp::Implemented:return "Implemented";case Disp::Partial:return "Partial";
    case Disp::Unsupported:return "Unsupported";case Disp::NativeBacked:return "Native-backed";
    default:return "Unverified";}
}

static std::string lower(std::string s){for(size_t i=0;i<s.size();++i)if(s[i]>='A'&&s[i]<='Z')s[i]=char(s[i]-'A'+'a');return s;}

struct Tok { const char* en; const char* zh; };
static const Tok kTokens[] = {
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
static const size_t kTokCount = sizeof(kTokens)/sizeof(kTokens[0]);

static std::string tokenZh(const std::string& t){
    for(size_t i=0;i<kTokCount;++i)if(lower(t)==lower(kTokens[i].en))return kTokens[i].zh;
    return std::string();
}

static std::vector<std::string> tokens(const std::string& s){
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

std::string zhCategory(const std::string& c){
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
static const Override kOverrides[] = {
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
static const size_t kOverrideCount = sizeof(kOverrides)/sizeof(kOverrides[0]);

std::string zhLabelForCommand(const std::string& command,const std::string& category){
    for(size_t i=0;i<kOverrideCount;++i)if(command==kOverrides[i].command)return kOverrides[i].zh;
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

static bool has(const std::string& s,const char* p){return s.find(p)!=std::string::npos;}

int iconForCommand(const std::string& c,int* variant){
    if(variant)*variant=0;
    if(has(c,"Marquee"))return IconCursor;
    if(has(c,"Lasso"))return IconLasso;
    if(has(c,"Eraser"))return IconEraser;
    if(has(c,"chooseBondTool_Solid"))return IconBond;
    if(has(c,"Hashed_Wedged"))return IconBondHash;
    if(has(c,"Hollow_Wedged"))return IconBondHollowWedge;
    if(has(c,"Wedged"))return IconBondWedge;
    if(has(c,"Hashed"))return IconBondHash;
    if(has(c,"Bold"))return IconBondBold;
    if(has(c,"Dashed")||has(c,"Dash"))return IconBondDashed;
    if(has(c,"Dotted")||has(c,"Hydrogen"))return IconBondDotted;
    if(has(c,"Wavy"))return IconBondWavy;
    if(has(c,"Dative"))return IconBondDative;
    if(has(c,"AnyBond"))return IconBondAny;
    if(has(c,"Quadruple"))return IconBondQuad;
    if(has(c,"Triple"))return IconBondTriple;
    if(has(c,"DoubleBond")||has(c,"DoubleEither")||has(c,"DoubleOr")||has(c,"SingleOrDouble")||has(c,"BoldDouble")||has(c,"DashedDouble")||has(c,"DoubleDashed"))return IconBondDouble;
    if(has(c,"chooseBondTool")||has(c,"MultipleBonds"))return IconBond;
    if(has(c,"RingTool")){
        if(has(c,"Benzene"))return IconRingAromatic;
        if(has(c,"Cyclopentadiene"))return IconRingDiene;
        if(has(c,"Chair"))return IconChair;
        if(has(c,"Cyclopropane")){if(variant)*variant=3;return IconRing3;}
        if(has(c,"Cyclobutane")){if(variant)*variant=4;return IconRing3;}
        if(has(c,"Cyclopentane")){if(variant)*variant=5;return IconRing3;}
        if(has(c,"Cyclohexane")){if(variant)*variant=6;return IconRing3;}
        if(has(c,"Cycloheptane")){if(variant)*variant=7;return IconRing3;}
        if(has(c,"Cyclooctane")){if(variant)*variant=8;return IconRing3;}
        return IconRing6;
    }
    if(has(c,"AlkaneTool"))return has(c,"Snaking")?IconChainSnake:IconChain;
    if(has(c,"EdittingTool_Text"))return IconText;
    if(has(c,"ArrowTool")){
        if(has(c,"Hollow"))return IconArrowHollow;
        if(has(c,"Equilibrium"))return IconArrowEquilibrium;
        if(has(c,"Unbalanced"))return IconArrowUnbalanced;
        if(has(c,"Resonance"))return IconArrowResonance;
        if(has(c,"NoGo"))return IconArrowNoGo;
        if(has(c,"Retrosynthetic"))return IconArrowRetro;
        if(has(c,"CW")||has(c,"CCW")||has(c,"270")||has(c,"180")||has(c,"120")||has(c,"90"))return IconArrowArc;
        if(has(c,"Small"))return IconArrowSmall;
        return IconArrowLarge;
    }
    if(has(c,"DrawingTool")){
        if(has(c,"Rectangle"))return IconRect;
        if(has(c,"RoundRect"))return IconRoundRect;
        if(has(c,"Oval"))return IconOval;
        if(has(c,"Circle"))return IconCircle;
        if(has(c,"Line"))return IconLine;
        if(has(c,"Arc")){if(has(c,"90")&&!has(c,"270")){if(variant)*variant=90;}else if(has(c,"120")){if(variant)*variant=120;}else if(has(c,"180")){if(variant)*variant=180;}else{if(variant)*variant=270;}return IconArc;}
        return IconRect;
    }
    if(has(c,"PenTool"))return has(c,"EditCurve")?IconEditCurve:IconCurve;
    if(has(c,"BracketTool")){
        if(has(c,"Dagger"))return IconDagger;
        if(has(c,"Curly"))return IconBracketCurly;
        if(has(c,"Parenthesis"))return IconBracketParen;
        return IconBracketSquare;
    }
    if(has(c,"SymbolTool")){
        if(has(c,"CirclePlus"))return IconPlus;
        if(has(c,"CircleMinus"))return IconMinus;
        if(has(c,"Plus"))return IconPlus;
        if(has(c,"Minus"))return IconMinus;
        if(has(c,"DoubleRadical"))return IconDoubleRadical;
        if(has(c,"RadicalCation"))return IconRadicalCation;
        if(has(c,"RadicalAnion"))return IconRadicalAnion;
        if(has(c,"Radical"))return IconRadical;
        if(has(c,"HDot"))return IconHDot;
        if(has(c,"HDash"))return IconHDash;
        if(has(c,"Attachment"))return IconAttachment;
        return IconRadical;
    }
    if(has(c,"PushElectron"))return IconPushElectron;
    if(has(c,"PushRadical"))return IconRadical;
    if(has(c,"Push"))return IconPushElectron;
    if(has(c,"OrbitalTool")){
        if(has(c,"spLobe"))return IconOrbitalSpLobe;
        if(has(c,"sp3"))return IconOrbitalSp3;
        if(has(c,"Sigma"))return IconOrbitalSigma;
        if(has(c,"dxy")||has(c,"dz2"))return IconOrbitalD;
        if(has(c,"p_"))return IconOrbitalP;
        return IconOrbitalS;
    }
    if(has(c,"QueryTool")){
        if(has(c,"Map"))return IconMap;
        if(has(c,"Stereochemistry"))return has(c,"Absolute")?IconStereoAbs:has(c,"Racemic")?IconStereoRac:IconStereoRel;
        return IconQuery;
    }
    if(has(c,"FragmentTool"))return IconFragment;
    if(has(c,"Perspective"))return IconPerspective;
    if(has(c,"Table"))return IconTable;
    if(has(c,"Template"))return IconTemplate;
    if(has(c,"PlateTool"))return IconPlate;
    if(has(c,"BioTool")||has(c,"GenericShape")){
        if(has(c,"Enzyme")||has(c,"Kinase"))return IconBioEnzyme;
        if(has(c,"GProtein"))return IconBioGProtein;
        if(has(c,"Receptor"))return IconBioReceptor;
        if(has(c,"IonChannel"))return IconBioIonChannel;
        if(has(c,"Immunoglobin"))return IconBioImmunoglobin;
        if(has(c,"NuclearPore"))return IconBioNuclearPore;
        if(has(c,"DNA"))return IconBioDNA;
        if(has(c,"RNA"))return IconBioRNA;
        if(has(c,"Membrane"))return IconBioMembrane;
        if(has(c,"Mitochondrion")||has(c,"Golgi")||has(c,"Centriole")||has(c,"ER"))return IconBioOrganelle;
        if(has(c,"Ribosome"))return IconBioRibosome;
        if(has(c,"Virus")||has(c,"HIV")||has(c,"Bacteriophage"))return IconBioVirus;
        if(has(c,"Bacterium")||has(c,"Paramecium")||has(c,"Amoeba"))return IconBioBacterium;
        if(has(c,"Plasmid"))return IconBioPlasmid;
        if(has(c,"Helix"))return IconBioProtein;
        if(has(c,"Cloud"))return IconBioCloud;
        return IconBioShape;
    }
    return IconBioShape;
}

static Item mk(const char* id,const char* label,ItemKind kind,SourceKind src,Disp d,int icon){
    Item it;it.id=id;it.label=label;it.kind=kind;it.source=src;it.disp=d;it.icon=icon;
    it.checkable=(kind==ItemKind::Toggle);
    return it;
}
static Item cmdItem(const char* command,const char* category,ItemKind kind=ItemKind::Small){
    Item it;int v=0;
    it.id=std::string("cmd:")+command;
    it.label=zhLabelForCommand(command,category);
    it.sublabel=command;
    it.command=command;it.owner="T19";it.kind=kind;it.source=SourceKind::SourceCommand;
    it.icon=iconForCommand(command,&v);it.iconVariant=v;it.disp=Disp::Partial;
    it.tooltip=it.label+"\n来源命令: "+command+"\n分类: "+category+"\n经统一 CommandRouter，拖到画布落笔；一手势一事务。";
    return it;
}
static Item actItem(const char* id,const char* label,HotkeyAction a,const char* hotkey,const char* tip,int icon,Disp d=Disp::Implemented){
    Item it=mk(id,label,ItemKind::Small,SourceKind::Action,d,icon);
    it.action=int(a);it.hotkey=hotkey?hotkey:"";it.tooltip=std::string(label)+(hotkey&&*hotkey?std::string("  (")+hotkey+")":"")+std::string("\n")+tip;
    return it;
}
static Item fileItem(const char* id,const char* label,int op,const char* hotkey,int icon){
    Item it=mk(id,label,ItemKind::Large,SourceKind::FileOp,Disp::Implemented,icon);
    it.value=op;it.hotkey=hotkey?hotkey:"";it.tooltip=std::string(label)+"\n经 T04 CommandRouter 的文件动作；T19 适配层执行。";
    return it;
}
static Item propItem(const char* id,const char* label,const char* key,int value,const char* hotkey,const char* tip,int icon,ItemKind kind=ItemKind::Small,Disp d=Disp::Implemented){
    Item it=mk(id,label,kind,SourceKind::PropertyOp,d,icon);
    it.command=key;it.value=value;it.hotkey=hotkey?hotkey:"";
    it.tooltip=std::string(label)+(hotkey&&*hotkey?std::string("  (")+hotkey+")":"")+std::string("\n")+tip;
    return it;
}
static Item unsupItem(const char* id,const char* label,const char* reason,int icon){
    Item it=mk(id,label,ItemKind::Small,SourceKind::Unsupported,Disp::Unsupported,icon);
    it.reason=reason;it.tooltip=std::string(label)+"\n未接通: "+reason;
    return it;
}
static Item gallery(const char* id,const char* label,const char* category,int icon,size_t* count){
    Item g=mk(id,label,ItemKind::Gallery,SourceKind::Unsupported,Disp::Partial,icon);
    for(size_t i=0;i<sizeof(sourceTools)/sizeof(sourceTools[0]);++i)
        if(std::string(sourceTools[i].category)==category){g.children.push_back(cmdItem(sourceTools[i].command,sourceTools[i].category));if(count)++*count;}
    g.tooltip=std::string(label)+"：展开全部 "+std::to_string((long long)g.children.size())+" 个该分类的工具/样式预设。";
    return g;
}
static Item galleryOf(const char* id,const char* label,int icon,const std::vector<const char*>& commands,const std::string& category){
    Item g=mk(id,label,ItemKind::Gallery,SourceKind::Unsupported,Disp::Partial,icon);
    for(size_t i=0;i<commands.size();++i)g.children.push_back(cmdItem(commands[i],category.c_str()));
    g.tooltip=std::string(label)+"：展开全部 "+std::to_string((long long)g.children.size())+" 个预设。";
    return g;
}
static const char* kBio="BioDraw 生物图示";
static const char* kArrowCat="箭头";
static const char* kShapeCat="几何图形";
static const char* kBondCat="化学键";
static const char* kRingCat="环结构";
static const char* kChainCat="碳链";
static const char* kBracketCat="括号与匕首标记";
static const char* kSymbolCat="化学符号与电子推移";
static const char* kOrbCat="原子轨道示意图";
static const char* kQueryCat="查询结构与立体标记";
static const char* kSelCat="选择与擦除";
static const char* kTextCat="文字与原子标签入口";
static const char* kTableCat="表格";
static const char* kTmplCat="模板入口";
static const char* kPerspCat="结构透视";
static const char* kExpCat="实验图示";
static const char* kCurveCat="自由曲线";

static std::vector<const char*> bioMatching(const char* a){
    std::vector<const char*> v;
    for(size_t i=0;i<sizeof(sourceTools)/sizeof(sourceTools[0]);++i){
        std::string c=sourceTools[i].command;
        if(std::string(sourceTools[i].category)!=kBio)continue;
        if(c.find(a)!=std::string::npos)v.push_back(sourceTools[i].command);
    }
    return v;
}

const Model& ribbonModel(){
    static Model m;static bool built=false;if(built)return m;built=true;
    size_t total=0;
    {
        Tab t;t.title="开始";
        Group g;g.title="文件";
        g.items.push_back(fileItem("file.new","新建",1,"Ctrl+N",IconNew));
        g.items.push_back(fileItem("file.open","打开副本",2,"Ctrl+O",IconOpen));
        g.items.push_back(fileItem("file.save","另存 T19",3,"Ctrl+S",IconSave));
        g.items.push_back(fileItem("file.export","导出 CDXML",4,"",IconExport));
        t.groups.push_back(g);
        Group c;c.title="剪贴板";
        c.items.push_back(actItem("clip.cut","剪切",HotkeyAction::EditCut,"Ctrl+X","T04 表已登记；本装配未接通剪贴板传输，将明确返回 Unsupported。",IconCut,Disp::Unsupported));
        c.items.push_back(actItem("clip.copy","复制",HotkeyAction::EditCopy,"Ctrl+C","T04 表已登记；本装配未接通剪贴板传输，将明确返回 Unsupported。",IconCopy,Disp::Unsupported));
        c.items.push_back(actItem("clip.paste","粘贴",HotkeyAction::EditPaste,"Ctrl+V","T04 表已登记；本装配未接通剪贴板传输，将明确返回 Unsupported。",IconPaste,Disp::Unsupported));
        c.items.push_back(actItem("edit.delete","删除",HotkeyAction::EditDelete,"Delete","删除选中对象；T01 removeObject 对部分注释返回 NotImplemented，失败整笔回滚。",IconDelete));
        t.groups.push_back(c);
        Group h;h.title="历史";
        h.items.push_back(actItem("edit.undo","撤销",HotkeyAction::Undo,"Ctrl+Z","精确 digest 回滚；一手势一事务。",IconUndo));
        h.items.push_back(actItem("edit.redo","重做",HotkeyAction::Redo,"Ctrl+Shift+Z","重做最后撤销的事务。",IconRedo));
        h.items.push_back(actItem("edit.repeat","重复",HotkeyAction::RepeatLastAction,"Ctrl+Y","重做最后一个命令（不是重做 Undo）。",IconRepeat));
        t.groups.push_back(h);
        Group s;s.title="选择";
        s.items.push_back(cmdItem("chooseSelectionTool_Marquee",kSelCat,ItemKind::Large));
        s.items.push_back(cmdItem("chooseSelectionTool_Lasso",kSelCat,ItemKind::Large));
        s.items.push_back(cmdItem("chooseEdittingTool_Eraser",kSelCat));
        s.items.push_back(actItem("sel.all","全选",HotkeyAction::EditSelectAll,"Ctrl+A","选中当前文档全部存活对象。",IconSelectAll));
        s.items.push_back(actItem("sel.cancel","取消手势",HotkeyAction::CancelGesture,"Esc","取消当前手势/预览，清除热点。",IconDelete));
        t.groups.push_back(s);
        Group q;q.title="常用工具";
        q.items.push_back(cmdItem("chooseBondTool_Solid",kBondCat,ItemKind::Large));
        q.items.push_back(cmdItem("chooseRingTool_Benzene",kRingCat,ItemKind::Large));
        q.items.push_back(cmdItem("chooseEdittingTool_Text",kTextCat,ItemKind::Large));
        q.items.push_back(cmdItem("chooseArrowTool_Large_Solid",kArrowCat,ItemKind::Large));
        t.groups.push_back(q);
        Group v;v.title="视图";
        v.items.push_back(actItem("view.actual","实际大小",HotkeyAction::ZoomActualSize,"F5","缩放 100%。",IconZoomActual));
        v.items.push_back(actItem("view.fit","全图",HotkeyAction::ZoomFit,"F6","缩放到全部对象。",IconZoomFit));
        v.items.push_back(actItem("view.zin","放大",HotkeyAction::ZoomIn,"F7","放大 25%。",IconZoomIn));
        v.items.push_back(actItem("view.zout","缩小",HotkeyAction::ZoomOut,"F8","缩小 20%。",IconZoomOut));
        t.groups.push_back(v);
        Group con;con.title="约束";
        Item fl=actItem("con.fixedlen","固定键长",HotkeyAction::ToggleFixedLength,"Ctrl+L","切换固定键长；Alt 临时释放。",IconLineWidth);
        fl.kind=ItemKind::Toggle;fl.checkable=true;
        Item fa=actItem("con.fixedangle","固定角度",HotkeyAction::ToggleFixedAngles,"Ctrl+E","切换 15° 固定角；Alt 临时释放。",IconArc);
        fa.kind=ItemKind::Toggle;fa.checkable=true;
        con.items.push_back(fl);con.items.push_back(fa);
        t.groups.push_back(con);
        Group pp;pp.title="属性";
        pp.items.push_back(propItem("prop.panel","属性面板","panel.props",0,"","切换右侧属性编辑区的显示。",IconPanel,ItemKind::Toggle));
        pp.items.push_back(propItem("prop.bond","键属性","panel.focus.bond",0,"","将属性区切换到键属性。",IconPropBond));
        pp.items.push_back(propItem("prop.atom","原子属性","panel.focus.atom",0,"","将属性区切换到原子属性。",IconPropAtom));
        pp.items.push_back(propItem("prop.text","文字属性","panel.focus.text",0,"","将属性区切换到文字属性。",IconPropText));
        pp.items.push_back(propItem("prop.widthminus","线宽 −","style.width.delta",-1,"","选中对象线宽减 0.5pt（下限 0.5）。",IconLineWidth));
        pp.items.push_back(propItem("prop.widthplus","线宽 +","style.width.delta",1,"","选中对象线宽加 0.5pt（上限 8）。",IconLineWidth));
        pp.items.push_back(propItem("prop.color","线条颜色…","style.color",0,"","为选中对象选择线条颜色（提交为一次事务）。",IconColor));
        pp.items.push_back(propItem("prop.fill","填充色…","style.fill",0,"","为选中对象选择填充色（未建模填充时明确拒绝）。",IconFill));
        t.groups.push_back(pp);
        m.tabs.push_back(t);
    }
    {
        Tab t;t.title="结构";
        Group b;b.title="化学键";
        b.items.push_back(cmdItem("chooseBondTool_Solid",kBondCat,ItemKind::Large));
        {Item sp=cmdItem("chooseMultipleBondsTool_DoubleBond",kBondCat,ItemKind::Split);sp.children.push_back(cmdItem("chooseMultipleBondsTool_DoubleBond",kBondCat));sp.children.push_back(cmdItem("chooseMultipleBondsTool_TripleBond",kBondCat));sp.children.push_back(cmdItem("chooseMultipleBondsTool_QuadrupleBond",kBondCat));sp.children.push_back(cmdItem("chooseMultipleBondsTool_BoldDoubleBond",kBondCat));sp.children.push_back(cmdItem("chooseMultipleBondsTool_DashedDoubleBond",kBondCat));b.items.push_back(sp);}
        b.items.push_back(cmdItem("chooseMultipleBondsTool_TripleBond",kBondCat,ItemKind::Large));
        b.items.push_back(cmdItem("chooseBondTool_Wedged",kBondCat));
        b.items.push_back(cmdItem("chooseBondTool_Hashed_Wedged",kBondCat));
        b.items.push_back(cmdItem("chooseBondTool_Hollow_Wedged",kBondCat));
        b.items.push_back(cmdItem("chooseBondTool_Bold",kBondCat));
        b.items.push_back(cmdItem("chooseBondTool_Dashed",kBondCat));
        b.items.push_back(cmdItem("chooseBondTool_Wavy",kBondCat));
        b.items.push_back(cmdItem("chooseMultipleBondsTool_DativeBond",kBondCat));
        b.items.push_back(cmdItem("chooseMultipleBondsTool_AnyBond",kBondCat));
        {size_t n=0;Item gg=gallery("gal.bonds","全部键型",kBondCat,IconBond,&n);total+=n;b.items.push_back(gg);}
        t.groups.push_back(b);
        Group r;r.title="环";
        r.items.push_back(cmdItem("chooseRingTool_Benzene",kRingCat,ItemKind::Large));
        r.items.push_back(cmdItem("chooseRingTool_Cyclohexane",kRingCat,ItemKind::Large));
        r.items.push_back(cmdItem("chooseRingTool_Cyclopentane",kRingCat,ItemKind::Large));
        r.items.push_back(cmdItem("chooseRingTool_Cyclopentadiene",kRingCat));
        r.items.push_back(cmdItem("chooseRingTool_Cycloheptane",kRingCat));
        {size_t n=0;Item gg=gallery("gal.rings","全部环",kRingCat,IconRing6,&n);total+=n;r.items.push_back(gg);}
        t.groups.push_back(r);
        Group ch;ch.title="链";
        ch.items.push_back(cmdItem("chooseAlkaneTool_Acyclic_Chain",kChainCat,ItemKind::Large));
        ch.items.push_back(cmdItem("chooseAlkaneTool_Snaking_Chain",kChainCat,ItemKind::Large));
        t.groups.push_back(ch);
        Group a;a.title="原子标签";
        a.items.push_back(propItem("atom.C","C 碳","atom.element",6,"c","把选中原子设为碳。",IconPropAtom));
        a.items.push_back(propItem("atom.N","N 氮","atom.element",7,"n","把选中原子设为氮。",IconPropAtom));
        a.items.push_back(propItem("atom.O","O 氧","atom.element",8,"o","把选中原子设为氧。",IconPropAtom));
        a.items.push_back(propItem("atom.S","S 硫","atom.element",16,"s","把选中原子设为硫。",IconPropAtom));
        a.items.push_back(propItem("atom.P","P 磷","atom.element",15,"p","把选中原子设为磷。",IconPropAtom));
        a.items.push_back(propItem("atom.F","F 氟","atom.element",9,"f","把选中原子设为氟。",IconPropAtom));
        a.items.push_back(propItem("atom.Cl","Cl 氯","atom.element",17,"l","把选中原子设为氯。",IconPropAtom));
        a.items.push_back(propItem("atom.Br","Br 溴","atom.element",35,"b","把选中原子设为溴。",IconPropAtom));
        a.items.push_back(propItem("atom.I","I 碘","atom.element",53,"i","把选中原子设为碘。",IconPropAtom));
        a.items.push_back(propItem("atom.B","B 硼","atom.element",5,"B","把选中原子设为硼。",IconPropAtom));
        a.items.push_back(propItem("atom.H","H 氢","atom.element",1,"h","把选中原子设为氢。",IconPropAtom));
        a.items.push_back(propItem("atom.chgminus","电荷 −","atom.charge.delta",-1,"-","原子形式电荷减 1。",IconMinus));
        a.items.push_back(propItem("atom.chgplus","电荷 +","atom.charge.delta",1,"+","原子形式电荷加 1。",IconPlus));
        a.items.push_back(propItem("atom.hcount","氢数循环","atom.h.cycle",0,"","在默认/0/1/2/3/4 间循环显式氢数。",IconHDot));
        a.items.push_back(propItem("atom.isotope","同位素循环","atom.isotope.cycle",0,"","在 0/13/14/15/2/18 间循环同位素。",IconQuery));
        a.items.push_back(propItem("atom.radical","自由基循环","atom.radical.cycle",0,"","在无/单/双自由基间循环。",IconRadical));
        a.items.push_back(actItem("atom.editlabel","编辑标签",HotkeyAction::EditAtomLabel,"Enter","打开标签编辑控件；原子热点 Enter。",IconText));
        a.items.push_back(actItem("atom.clearlabel","清除标签",HotkeyAction::ClearAtomLabel,"Backspace","清除原子标签（不删除原子）。",IconDelete));
        t.groups.push_back(a);
        Group bp;bp.title="键属性";
        bp.items.push_back(propItem("bond.single","单键","bond.order",1,"1","把选中键设为单键。",IconBond));
        bp.items.push_back(propItem("bond.double","双键","bond.order",2,"2","把选中键设为双键。",IconBondDouble));
        bp.items.push_back(propItem("bond.triple","三键","bond.order",3,"3","把选中键设为三键。",IconBondTriple));
        bp.items.push_back(propItem("bond.quad","四键","bond.order",4,"","把选中键设为四键。",IconBondQuad));
        bp.items.push_back(propItem("bond.aromatic","芳香键","bond.order",5,"","把选中键设为芳香键级。",IconRingAromatic));
        bp.items.push_back(propItem("bond.side.sym","双键对称","bond.side",0,"","双线对称分布（默认）。",IconBondDouble));
        bp.items.push_back(propItem("bond.side.left","双键偏左","bond.side",1,"l","双键偏左。",IconBondDouble));
        bp.items.push_back(propItem("bond.side.right","双键偏右","bond.side",2,"r","双键偏右。",IconBondDouble));
        bp.items.push_back(propItem("bond.side.center","双键居中","bond.side",3,"c","一条线在轴线上的居中双键。",IconBondDouble));
        bp.items.push_back(propItem("bond.wedge","楔形","bond.display",1,"w","立体楔形显示。",IconBondWedge));
        bp.items.push_back(propItem("bond.hash","虚楔","bond.display",2,"h","立体虚楔显示。",IconBondHash));
        bp.items.push_back(propItem("bond.dash","虚线","bond.display",6,"d","虚线键显示。",IconBondDashed));
        bp.items.push_back(propItem("bond.bold","粗键","bond.display",5,"b","粗键显示。",IconBondBold));
        bp.items.push_back(propItem("bond.wavy","波浪","bond.display",8,"y","波浪键显示。",IconBondWavy));
        bp.items.push_back(propItem("bond.sem.normal","普通语义","bond.semantics",0,"","键语义普通。",IconBond));
        bp.items.push_back(propItem("bond.sem.aromatic","芳香语义","bond.semantics",1,"","键语义芳香。",IconRingAromatic));
        bp.items.push_back(propItem("bond.sem.dative","配位语义","bond.semantics",2,"","键语义配位。",IconBondDative));
        bp.items.push_back(propItem("bond.stereo.up","上立体","bond.stereo",1,"","键立体 Up。",IconBondWedge));
        bp.items.push_back(propItem("bond.stereo.down","下立体","bond.stereo",2,"","键立体 Down。",IconBondHash));
        bp.items.push_back(propItem("bond.stereo.none","清除立体","bond.stereo",0,"","清除键立体。",IconDelete));
        t.groups.push_back(bp);
        Group st;st.title="样式";
        st.items.push_back(propItem("style.widthminus","线宽 −","style.width.delta",-1,"","线宽减 0.5pt。",IconLineWidth));
        st.items.push_back(propItem("style.widthplus","线宽 +","style.width.delta",1,"","线宽加 0.5pt。",IconLineWidth));
        st.items.push_back(propItem("style.color","颜色…","style.color",0,"","选择线条颜色。",IconColor));
        st.items.push_back(propItem("style.fill","填充色…","style.fill",0,"","选择填充色。",IconFill));
        st.items.push_back(propItem("style.reset","复位样式","style.reset",0,"","把选中对象恢复文档默认线宽/颜色。",IconDelete));
        t.groups.push_back(st);
        m.tabs.push_back(t);
    }
    {
        Tab t;t.title="绘图";
        Group ar;ar.title="箭头";
        ar.items.push_back(cmdItem("chooseArrowTool_Large_Solid",kArrowCat,ItemKind::Large));
        ar.items.push_back(cmdItem("chooseArrowTool_Large_Equilibrium",kArrowCat));
        ar.items.push_back(cmdItem("chooseArrowTool_Large_Resonance",kArrowCat));
        ar.items.push_back(cmdItem("chooseArrowTool_Large_NoGo_Cross",kArrowCat));
        ar.items.push_back(cmdItem("chooseArrowTool_Large_Retrosynthetic",kArrowCat));
        ar.items.push_back(cmdItem("chooseArrowTool_Medium_OneSided_Left",kArrowCat));
        {size_t n=0;Item gg=gallery("gal.arrows","全部箭头",kArrowCat,IconArrowLarge,&n);total+=n;ar.items.push_back(gg);}
        t.groups.push_back(ar);
        Group cv;cv.title="曲线";
        cv.items.push_back(cmdItem("choosePenTool_DrawCurve",kCurveCat,ItemKind::Large));
        cv.items.push_back(cmdItem("choosePenTool_EditCurve",kCurveCat,ItemKind::Large));
        t.groups.push_back(cv);
        Group sh;sh.title="图形";
        sh.items.push_back(cmdItem("chooseDrawingTool_Rectangle_Hollow",kShapeCat,ItemKind::Large));
        sh.items.push_back(cmdItem("chooseDrawingTool_Oval_Hollow",kShapeCat));
        sh.items.push_back(cmdItem("chooseDrawingTool_Circle_Hollow",kShapeCat));
        sh.items.push_back(cmdItem("chooseDrawingTool_RoundRect_Hollow",kShapeCat));
        sh.items.push_back(cmdItem("chooseDrawingTool_Line",kShapeCat));
        sh.items.push_back(cmdItem("chooseDrawingTool_Arc_180",kShapeCat));
        {size_t n=0;Item gg=gallery("gal.shapes","全部图形",kShapeCat,IconRect,&n);total+=n;sh.items.push_back(gg);}
        t.groups.push_back(sh);
        Group br;br.title="括号";
        br.items.push_back(cmdItem("chooseBracketTool_SquareBrackets",kBracketCat,ItemKind::Large));
        br.items.push_back(cmdItem("chooseBracketTool_Parenthesis",kBracketCat));
        br.items.push_back(cmdItem("chooseBracketTool_CurlyBrackets",kBracketCat));
        br.items.push_back(cmdItem("chooseBracketTool_SingleDagger",kBracketCat));
        br.items.push_back(cmdItem("chooseBracketTool_DoubleDagger",kBracketCat));
        {size_t n=0;Item gg=gallery("gal.brackets","全部括号",kBracketCat,IconBracketSquare,&n);total+=n;br.items.push_back(gg);}
        t.groups.push_back(br);
        Group sy;sy.title="符号";
        sy.items.push_back(cmdItem("chooseSymbolTool_Plus",kSymbolCat,ItemKind::Large));
        sy.items.push_back(cmdItem("chooseSymbolTool_Minus",kSymbolCat,ItemKind::Large));
        sy.items.push_back(cmdItem("chooseSymbolTool_Radical",kSymbolCat));
        sy.items.push_back(cmdItem("chooseSymbolTool_DoubleRadical",kSymbolCat));
        sy.items.push_back(cmdItem("choosePushElectronTool",kSymbolCat));
        sy.items.push_back(cmdItem("chooseSymbolTool_HDot",kSymbolCat));
        sy.items.push_back(cmdItem("chooseSymbolTool_Attachment_Diamond",kSymbolCat));
        {size_t n=0;Item gg=gallery("gal.symbols","全部符号",kSymbolCat,IconRadical,&n);total+=n;sy.items.push_back(gg);}
        t.groups.push_back(sy);
        Group ex;ex.title="图示与表格";
        ex.items.push_back(cmdItem("chooseTableTool",kTableCat,ItemKind::Large));
        ex.items.push_back(cmdItem("chooseTLCPlateTool",kExpCat,ItemKind::Large));
        ex.items.push_back(cmdItem("chooseGEPPlateTool",kExpCat,ItemKind::Large));
        t.groups.push_back(ex);
        Group orb;orb.title="轨道";
        orb.items.push_back(cmdItem("chooseOrbitalTool_p_Shaded",kOrbCat,ItemKind::Large));
        orb.items.push_back(cmdItem("chooseOrbitalTool_s_Open",kOrbCat));
        orb.items.push_back(cmdItem("chooseOrbitalTool_Sigma_Open",kOrbCat));
        orb.items.push_back(cmdItem("chooseOrbitalTool_sp3_Shaded",kOrbCat));
        {size_t n=0;Item gg=gallery("gal.orbitals","全部轨道",kOrbCat,IconOrbitalP,&n);total+=n;orb.items.push_back(gg);}
        t.groups.push_back(orb);
        m.tabs.push_back(t);
    }
    {
        Tab t;t.title="文字";
        Group tx;tx.title="文字";
        tx.items.push_back(cmdItem("chooseEdittingTool_Text",kTextCat,ItemKind::Large));
        tx.items.push_back(propItem("text.edit","编辑文字","text.edit",0,"","双击文字或在属性区编辑；提交为一次事务。",IconText));
        tx.items.push_back(propItem("text.bold","粗体","text.flags.bold",0,"","切换选中文字粗体标记。",IconTextBold));
        tx.items.push_back(propItem("text.italic","斜体","text.flags.italic",0,"","切换选中文字斜体标记。",IconTextItalic));
        tx.items.push_back(propItem("text.underline","下划线","text.flags.underline",0,"","切换选中文字下划线标记。",IconTextUnderline));
        tx.items.push_back(propItem("text.sub","下标","text.flags.sub",0,"","切换下标标记。",IconTextSub));
        tx.items.push_back(propItem("text.sup","上标","text.flags.sup",0,"","切换上标标记。",IconTextSup));
        t.groups.push_back(tx);
        Group sz;sz.title="字号";
        {Item gg=mk("gal.size","字号",ItemKind::Gallery,SourceKind::PropertyOp,Disp::Implemented,IconText);const int sizes[]={8,9,10,11,12,14,16,18,24,36,48,72};for(int i=0;i<12;++i){Item ci=propItem("text.size","字号","text.size",sizes[i],"","把选中文字字号设为该点数。",IconText);ci.sublabel=std::to_string(sizes[i])+" pt";gg.children.push_back(ci);}sz.items.push_back(gg);}
        t.groups.push_back(sz);
        Group al;al.title="对齐";
        al.items.push_back(propItem("text.align.left","左对齐","text.align",0,"","文字段落左对齐。",IconAlignLeft));
        al.items.push_back(propItem("text.align.center","居中","text.align",1,"","文字段落居中。",IconAlignCenterH));
        al.items.push_back(propItem("text.align.right","右对齐","text.align",2,"","文字段落右对齐。",IconAlignRight));
        al.items.push_back(propItem("text.color","文字颜色…","text.color",0,"","选择文字颜色。",IconColor));
        t.groups.push_back(al);
        Group tf;tf.title="字体";
        tf.items.push_back(propItem("text.font.default","默认字体","text.font.default",0,"","恢复文档默认字体。",IconText));
        tf.items.push_back(propItem("text.font.mono","等宽字体","text.font.mono",0,"","使用 Consolas 等宽字体。",IconText));
        t.groups.push_back(tf);
        m.tabs.push_back(t);
    }
    {
        Tab t;t.title="科学";
        Group or2;or2.title="轨道";
        {size_t n=0;Item gg=gallery("gal.orbitals2","全部轨道",kOrbCat,IconOrbitalP,&n);total+=n;or2.items.push_back(gg);}
        or2.items.push_back(cmdItem("chooseOrbitalTool_p_Shaded",kOrbCat));
        or2.items.push_back(cmdItem("chooseOrbitalTool_s_Open",kOrbCat));
        or2.items.push_back(cmdItem("chooseOrbitalTool_Sigma_Shaded",kOrbCat));
        t.groups.push_back(or2);
        Group q;q.title="查询与立体";
        {size_t n=0;Item gg=gallery("gal.query","查询/立体标记",kQueryCat,IconQuery,&n);total+=n;q.items.push_back(gg);}
        q.items.push_back(unsupItem("q.map","反应原子映射","T13 查询服务已独立交付，总装交互尚未接通。",IconMap));
        q.items.push_back(unsupItem("q.freesite","自由位点","T13 查询服务已独立交付，总装交互尚未接通。",IconQuery));
        t.groups.push_back(q);
        Group fr;fr.title="片段";
        {size_t n=0;Item gg=gallery("gal.frag","全部片段工具","片段操作",IconFragment,&n);total+=n;fr.items.push_back(gg);}
        fr.items.push_back(cmdItem("chooseFragmentTool_MassFragmentation","片段操作",ItemKind::Large));
        fr.items.push_back(cmdItem("chooseFragmentTool_Synthesis","片段操作"));
        fr.items.push_back(cmdItem("chooseFragmentTool_Retrosynthesis","片段操作"));
        t.groups.push_back(fr);
        Group pe;pe.title="透视";
        pe.items.push_back(cmdItem("chooseStructurePerspectiveTool",kPerspCat,ItemKind::Large));
        t.groups.push_back(pe);
        Group tp;tp.title="模板";
        tp.items.push_back(cmdItem("chooseTemplateTool",kTmplCat,ItemKind::Large));
        tp.items.push_back(unsupItem("tpl.library","模板库浏览","本机模板库内容未逐项导入；入口保留且明确拒绝。",IconTemplate));
        t.groups.push_back(tp);
        Group hm;hm.title="HELM 生物聚合物";
        hm.items.push_back(unsupItem("helm.seq","HELM 序列输入","T15 编解码已独立交付，总装尚未接通序列编辑 UI。",IconLink));
        hm.items.push_back(unsupItem("helm.monomer","插入/替换单体","T15 服务未接入统一 CommandRouter 交互。",IconLink));
        hm.items.push_back(unsupItem("helm.connect","连接/交联","T15 服务未接入统一 CommandRouter 交互。",IconLink));
        hm.items.push_back(unsupItem("helm.complement","互补链/发夹","T15 服务未接入统一 CommandRouter 交互。",IconLink));
        t.groups.push_back(hm);
        m.tabs.push_back(t);
    }
    {
        Tab t;t.title="生物";
        struct BG{const char* title;const char* pats;int icon;};
        static const BG bg[]={
            {"酶","Enzyme|Kinase",IconBioEnzyme},
            {"G 蛋白","GProtein",IconBioGProtein},
            {"受体","Receptor",IconBioReceptor},
            {"离子通道","IonChannel",IconBioIonChannel},
            {"免疫球蛋白","Immunoglobin",IconBioImmunoglobin},
            {"核孔","NuclearPore",IconBioNuclearPore},
            {"核酸","DNA|tRNA|mRNA",IconBioRNA},
            {"膜","Membrane",IconBioMembrane},
            {"细胞器","Golgi|Mitochondrion|ER|Er|Centriole",IconBioOrganelle},
            {"核糖体","Ribosome",IconBioRibosome},
            {"微生物","Virus|HIV|Bacterium|Bacteriophage|Paramecium|Amoeba",IconBioVirus},
            {"其他图形","Cloud|Star|Circle|Ball|Triangle|Rectangle|Hexagon|Octagon",IconBioShape}
        };
        for(size_t i=0;i<sizeof(bg)/sizeof(bg[0]);++i){
            std::vector<std::string> pats;{std::string s=bg[i].pats;size_t pos=0;while(pos<=s.size()){size_t q=s.find('|',pos);if(q==std::string::npos)q=s.size();pats.push_back(s.substr(pos,q-pos));pos=q+1;}}
            std::vector<const char*> cs;
            for(size_t k=0;k<sizeof(sourceTools)/sizeof(sourceTools[0]);++k){
                std::string c=sourceTools[k].command;
                if(std::string(sourceTools[k].category)!=kBio)continue;
                for(size_t j=0;j<pats.size();++j)if(c.find(pats[j])!=std::string::npos){cs.push_back(sourceTools[k].command);break;}
            }
            Group g;g.title=bg[i].title;
            std::string id=std::string("gal.bio.")+bg[i].title;
            Item gal=galleryOf(id.c_str(),(std::string("全部")+bg[i].title).c_str(),bg[i].icon,cs,kBio);
            total+=cs.size();
            g.items.push_back(gal);
            t.groups.push_back(g);
        }
        Group g2;g2.title="蛋白与质粒";
        {Item pg=mk("gal.bio.protein","螺旋蛋白",ItemKind::Gallery,SourceKind::Unsupported,Disp::Partial,IconBioProtein);
         std::vector<const char*> cs=bioMatching("HelixProtein");for(size_t k=0;k<cs.size();++k){pg.children.push_back(cmdItem(cs[k],kBio));++total;}
         pg.tooltip="螺旋蛋白图形预设。";g2.items.push_back(pg);}
        {Item pl=mk("gal.bio.plasmid","质粒图谱",ItemKind::Gallery,SourceKind::Unsupported,Disp::Partial,IconBioPlasmid);
         std::vector<const char*> cs=bioMatching("Plasmid");for(size_t k=0;k<cs.size();++k){pl.children.push_back(cmdItem(cs[k],kBio));++total;}
         pl.tooltip="质粒图谱预设。";g2.items.push_back(pl);}
        t.groups.push_back(g2);
        m.tabs.push_back(t);
    }
    {
        Tab t;t.title="布局";
        Group al;al.title="对齐";
        const char* aln[6]={"左对齐","水平居中","右对齐","顶对齐","垂直居中","底对齐"};
        const int alni[6]={IconAlignLeft,IconAlignCenterH,IconAlignRight,IconAlignTop,IconAlignCenterV,IconAlignBottom};
        for(int i=0;i<6;++i)al.items.push_back(propItem((std::string("sel.align.")+std::to_string(i)).c_str(),aln[i],"select.align",i,"","按选区边界对齐；至少需要一个可测对象。",alni[i]));
        t.groups.push_back(al);
        Group di;di.title="分布";
        const char* dis[4]={"水平中心等距","垂直中心等距","水平间隙等距","垂直间隙等距"};
        for(int i=0;i<4;++i)di.items.push_back(propItem((std::string("sel.dist.")+std::to_string(i)).c_str(),dis[i],"select.distribute",i,"","分布需要至少三个可测对象，否则明确报告不适用。",i<2?IconDistributeH:IconDistributeV));
        t.groups.push_back(di);
        Group zo;zo.title="层级";
        const char* zn[4]={"置于顶层","置于底层","上移一层","下移一层"};
        const int zi[4]={IconFront,IconBack,IconForward,IconBackward};
        for(int i=0;i<4;++i)zo.items.push_back(propItem((std::string("sel.z.")+std::to_string(i)).c_str(),zn[i],"select.zorder",i,"","T01 无逐对象 z；仅对分组记录生效，未分组时明确报告 unsupported。",zi[i]));
        t.groups.push_back(zo);
        Group gr;gr.title="分组";
        gr.items.push_back(propItem("sel.group","编组","select.group",0,"","把选中对象放入一个 Group 记录。",IconGroup));
        gr.items.push_back(propItem("sel.ungroup","解组","select.ungroup",0,"","解散选中的分组记录，成员保留。",IconUngroup));
        t.groups.push_back(gr);
        Group tr;tr.title="变换";
        tr.items.push_back(propItem("sel.rotl","逆时针 90°","select.rotate",90,"","绕选区中心旋转。",IconRotateL));
        tr.items.push_back(propItem("sel.rotr","顺时针 90°","select.rotate",-90,"","绕选区中心旋转。",IconRotateR));
        tr.items.push_back(propItem("sel.fliph","水平翻转","select.flip",0,"","绕选区中心垂直轴翻转。",IconFlipH));
        tr.items.push_back(propItem("sel.flipv","垂直翻转","select.flip",1,"","绕选区中心水平轴翻转。",IconFlipV));
        tr.items.push_back(propItem("sel.scaleup","放大 10%","select.scale",10,"","统一切向缩放。",IconZoomIn));
        tr.items.push_back(propItem("sel.scaledown","缩小 10%","select.scale",-10,"","统一切向缩放。",IconZoomOut));
        t.groups.push_back(tr);
        Group mg;mg.title="合并";
        mg.items.push_back(propItem("sel.merge","结构连接/合并","select.merge",0,"Ctrl+J","按最大距离合并重叠原子的结构（T05 merge）。",IconMerge));
        t.groups.push_back(mg);
        m.tabs.push_back(t);
    }
    {
        Tab t;t.title="视图";
        Group z;z.title="缩放";
        z.items.push_back(actItem("view.actual2","实际大小",HotkeyAction::ZoomActualSize,"F5","缩放 100%。",IconZoomActual));
        z.items.push_back(actItem("view.fit2","全图",HotkeyAction::ZoomFit,"F6","缩放到全部对象。",IconZoomFit));
        z.items.push_back(propItem("view.z100","100%","view.scale",100,"","把视图缩放设为 100%。",IconZoomActual));
        z.items.push_back(propItem("view.z150","150%","view.scale",150,"","把视图缩放设为 150%。",IconZoomIn));
        z.items.push_back(propItem("view.z200","200%","view.scale",200,"","把视图缩放设为 200%。",IconZoomIn));
        t.groups.push_back(z);
        Group pn;pn.title="面板";
        pn.items.push_back(propItem("panel.props2","属性面板","panel.props",0,"","切换右侧属性编辑区。",IconPanel,ItemKind::Toggle));
        pn.items.push_back(propItem("panel.stats","文档状态","panel.stats",0,"","切换底部状态行的统计显示。",IconPanel,ItemKind::Toggle));
        t.groups.push_back(pn);
        Group rq;rq.title="渲染质量";
        {Item q=mk("view.ssaa","2× SSAA 已强制",ItemKind::Label,SourceKind::Unsupported,Disp::Implemented,IconGrid);
         q.tooltip="GL-compat-fbo2x：缺少 2× SSAA 时程序拒绝启动，不以降低画质换取性能。";rq.items.push_back(q);}
        {Item dpi=mk("view.dpi","DPI 跟随系统",ItemKind::Label,SourceKind::Unsupported,Disp::Implemented,IconGrid);
         dpi.tooltip="Ribbon 与画布共用系统 DPI 缩放；鼠标坐标映射不变。";rq.items.push_back(dpi);}
        t.groups.push_back(rq);
        Group hs;hs.title="宿主";
        hs.items.push_back(unsupItem("host.bind","连接 ChemDraw 宿主","窗口标题始终标注 HOST UNBOUND；未通过文档身份/原生编辑/Undo/保存门禁前不得声称为宿主集成。",IconHost));
        t.groups.push_back(hs);
        m.tabs.push_back(t);
    }
    (void)total;
    return m;
}

}}}
