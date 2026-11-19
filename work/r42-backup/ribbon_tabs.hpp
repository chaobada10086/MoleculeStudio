#pragma once
Group makeProjectionTools(){Group group;group.name="立体投影";group.items={Button("projection.fischer","费歇尔投影","fischer","@projection",0),Button("projection.sawhorse","锯架式","sawhorse","@projection",1),Button("projection.newman","纽曼投影","newman","@projection",2)};return group;}

std::vector<Group> makeTableTools(){std::vector<Group> groups;auto group=[&](const char* name){groups.push_back(Group());groups.back().name=name;};auto add=[&](const char* name,const char* icon,const char* op){groups.back().items.push_back(Button(std::string("table.")+op,name,icon,std::string("@table:")+op));};
    group("表格");add("插入表格","table","insert");add("插入三线表","threeLines","insertThree");add("编辑内容","pen","edit");add("选择全表","select","selectAll");add("删除表格","erase","deleteTable");
    group("行与列");add("上方插入行","rowPlus","rowAbove");add("下方插入行","rowPlus","rowBelow");add("左侧插入列","colPlus","colLeft");add("右侧插入列","colPlus","colRight");add("删除行","cut","deleteRows");add("删除列","cut","deleteCols");
    group("合并与拆分");add("合并单元格","mergeCells","merge");add("拆分单元格","splitCells","split");add("清空内容","erase","clear");
    group("单元格大小");add("行间距…","rowPlus","rowHeight");add("列宽…","colPlus","colWidth");add("平均分布行","distV","equalRows");add("平均分布列","distH","equalCols");add("适应内容","fit","autoFit");
    group("对齐与文字");add("左对齐","textalign0","left");add("水平居中","textalign1","center");add("右对齐","textalign2","right");add("顶部对齐","align3","top");add("垂直居中","align4","middle");add("底部对齐","align5","bottom");add("字号…","font","fontSize");add("加粗","boldText","bold");
    group("表格样式");add("三线表","threeLines","three");add("所有边框","table","grid");add("仅外框","rect","outer");add("仅横线","threeLines","horizontal");add("无边框","dottedRect","none");add("边框线宽…","bold","borderWidth");return groups;}
std::vector<Group> makeAITools(){Group group;group.name="AI 绘图工作台";group.items={Button("ai.open","开始 AI 绘图","aiChat","@ai"),Button("ai.settings","AI 设置","apiKey","@aiSettings")};return {group};}

// Expanded views share the same commands as Home. Tab switches only alter the
// presentation; the document, selection and active drawing tool stay in place.
std::vector<Group> makeExpandedTools(bool text){
    auto home=makeTools();std::vector<Group> result;
    auto group=[&](const char* name){result.push_back(Group());result.back().name=name;};
    auto copy=[&](size_t source,const std::string& command,int arg=0,const char* label=nullptr){
        for(auto b:home[source].items)if(b.cmd==command&&b.arg==arg){b.id=(text?"text.":"graphics.")+b.id;if(label)b.label=label;result.back().items.push_back(b);return;}
    };
    if(text){
        group("文字与字体");copy(4,"chooseEdittingTool_Text",0,"插入文字");copy(4,"@font",0,"选择字体");copy(4,"@color",0,"文字颜色");copy(4,"@size",1,"增大字号");copy(4,"@size",-1,"减小字号");
        group("字体样式");copy(4,"@flag",kBold);copy(4,"@flag",kItalic);copy(4,"@flag",kUnderline);
        group("化学式与脚标");copy(4,"@flag",kFormula);copy(4,"@flag",kSubscript);copy(4,"@flag",kSuperscript);
        // Its own card: the compact ribbon only shows the first three items of
        // a card, and this action must stay visible without opening a menu.
        group("文字整理");result.back().items.push_back(Button("text.tidy","文字整理","formula","@textTidy"));result.back().items.push_back(Button("text.autoLayout","自动排版","formula","@autoTextLayout"));
        group("段落对齐");copy(4,"@textalign",0);copy(4,"@textalign",1);copy(4,"@textalign",2);copy(4,"@textalign",3);
        group("剪贴板与撤销");copy(0,"@copy");copy(0,"@cut");copy(0,"@paste");copy(0,"@undo");copy(0,"@redo");
    }else{
        group("图案");result.back().items.push_back(Button("graphics.shapes","插入图案","rect","@shapes"));result.back().items.push_back(Button("graphics.tlc","插入TLC板","table","chooseTLCPlateTool"));result.back().items.push_back(Button("graphics.fill","图案填充","shapeFill","@shapeFill"));result.back().items.push_back(Button("graphics.text","插入文字","font","chooseEdittingTool_Text"));
        group("基本图形");result.back().items.push_back(Button("graphics.ellipse","椭圆","oval","chooseDrawingTool_Oval_Hollow"));for(const char* command:{"chooseDrawingTool_Line","chooseDrawingTool_Circle_Hollow","chooseDrawingTool_Rectangle_Hollow","chooseDrawingTool_RoundRect_Hollow","chooseDrawingTool_Rectangle_Dashed","chooseDrawingTool_RoundRect_Dashed","choosePenTool_DrawCurve","choosePenTool_EditCurve"})copy(3,command);
        group("箭头样式");result.back().items.push_back(Button("graphics.arrowStyle","选择箭头样式","arrow","@arrows"));
        group("括号与标注");result.back().items.push_back(Button("graphics.radical","自由基","radical","chooseSymbolTool_Radical"));for(const char* command:{"chooseBracketTool_SquareBrackets","chooseBracketTool_Parenthesis"})copy(3,command);
        group("排列与变换");for(auto b:home[5].items)if(b.enabled){b.id="graphics."+b.id;result.back().items.push_back(b);}
        group("线宽与颜色");result.back().items.push_back(Button("graphics.width","线宽","bold","@graphicsWidth"));result.back().items.push_back(Button("graphics.color","颜色","color","@graphicsColor"));

    }
    return result;
}





std::vector<Group> makeTemplateTools(){
    Group builtins;builtins.name="内置模板";builtins.items={Button("templates.rings","更多环","otherRings","@otherRings"),Button("templates.bio","生物分子","benzene","@bioComponents")};
    builtins.items.push_back(Button("templates.inorganic","常见无机试剂","inorganicReagents","@inorganicReagents"));builtins.items.push_back(Button("templates.organic","常见有机合成试剂","organicReagents","@organicReagents"));
    Group custom;custom.name="自定义模板";custom.items={Button("templates.mine","我的模板","benzene","@customTemplates"),Button("templates.add","添加自定义模板","rowPlus","@addCustomTemplate")};custom.items.back().enabled=false;
    Group files;files.name="模板文件";files.items={Button("templates.import","导入模板文件","templateImport","@importCustomTemplate"),Button("templates.export","导出模板文件","templateExport","@exportCustomTemplate")};Group functional;functional.name="自定义官能团";functional.items={Button("templates.functionalGroups","自定义官能团","benzene","@customGroups")};return {builtins,custom,makeProjectionTools(),functional,files};
}

std::vector<Group> makeMechanismTools(){
 Group arrows;arrows.name="机理箭头";arrows.items={Button("mechanism.pair","机理箭头","mechanismPair","@mechanism"),Button("mechanism.single","机理鱼钩箭头","mechanismHook","@mechanismFishhook"),Button("mechanism.equivalence","等同符号 ≡","equivalence","@mechanismEquivalence")};
 Group reagents;reagents.name="通用试剂";reagents.items={Button("mechanism.base","插入 B⁻","genericBase","@mechanismBase"),Button("mechanism.nu","插入 Nu⁻","genericNu","@mechanismNu"),Button("mechanism.proton","插入 H⁺","genericProton","@mechanismProton")};for(auto& button:reagents.items)button.iconOnly=true;
 reagents.items.push_back(Button("mechanism.inorganic","常见无机试剂","inorganicReagents","@inorganicReagents"));reagents.items.push_back(Button("mechanism.organic","常见有机合成试剂","organicReagents","@organicReagents"));
 reagents.items.push_back(Button("mechanism.ions","常见离子","ionLibrary","@commonIons"));
 Group charges;charges.name="原子电荷";charges.items={
     Button("mechanism.positive","正电荷","barePlus","@mechanismPositive"),
     Button("mechanism.positiveCircle","圈正电荷","plus","@mechanismPositive",1),
     Button("mechanism.negative","负电荷","bareMinus","@mechanismNegative"),
     Button("mechanism.negativeCircle","圈负电荷","minus","@mechanismNegative",1),
     Button("mechanism.partialNegative","标注 δ−","partialMinus","@partialCharge",-1),
     Button("mechanism.partialPositive","标注 δ+","partialPlus","@partialCharge",1)};
 Group step;step.name="电子状态与推演";step.items={Button("mechanism.lonePair","插入孤对电子","lonePair","@mechanismLonePair"),Button("mechanism.radical","插入自由基","radical","@mechanismRadical"),Button("mechanism.next","生成下一步","arrow","@mechanismNext")};
 for(auto& button:charges.items)button.iconOnly=true;
 Group centers;centers.name="活性中心";centers.items={Button("mechanism.allene","插入连烯","allene","@allene"),Button("mechanism.carbene","插入卡宾","lonePair","@carbene")};
 Group next;next.name="生成产物";next.items={step.items.back(),Button("mechanism.transfer","下一步 + 质子转移","arrow","@mechanismTransfer"),Button("mechanism.deprotonate","下一步 − 质子","arrow","@mechanismDeprotonate")};step.items.pop_back();
 Group guides;guides.name="机理辅助";guides.items={Button("mechanism.virtualBond","待成键","virtualBond","@virtualBond")};
 Group styles;styles.name="生成箭头";const char* names[]={"单向","可逆","可逆趋势向左","可逆趋势向右"};for(int i=0;i<4;++i){Button b("mechanism.generationStyle"+std::to_string(i),names[i],"arrow","@generationArrowStyle",i);b.iconOnly=true;styles.items.push_back(b);}
 Group cycle;cycle.name="催化循环";const char* cycleNames[]={"顺时针循环","逆时针循环","加入试剂","放出产物","金属与配体"};const char* icons[]={"oval","oval","arrow","arrow","inorganicReagents"};for(int i=0;i<5;++i)cycle.items.push_back(Button("catalytic."+std::to_string(i),cycleNames[i],icons[i],"@catalytic",i));
 return {cycle,arrows,guides,reagents,charges,centers,step,next,styles};
}
