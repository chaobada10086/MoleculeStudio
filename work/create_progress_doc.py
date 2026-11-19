from pathlib import Path
from docx import Document
from docx.shared import Cm, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn

ROOT = Path(r"C:\Users\ChaobaDa\Documents\GitHub\MoleculeStudio")
OUTPUT = Path(r"C:\Users\ChaobaDa\Desktop\MoleculeStudio改动核对清单-20261116.docx")

# A verification point is intentionally smaller than a feature request.
sections = [
    ("模板和立体投影入口", [
        ("糖类费歇尔式改为生物分子模板的变体", "模板 → 生物分子，选择糖类，在左下角切换费歇尔式；同一糖的其他表示方式仍在原窗口。", "已实现"),
        ("移除糖类费歇尔式的单独按钮", "检查模板选项卡和图形选项卡，均不应出现独立的糖类费歇尔入口。", "已实现"),
        ("图形选项卡不再单列三种立体投影", "打开图形选项卡，核对没有费歇尔、锯架、纽曼三个独立按钮。", "已实现"),
        ("立体投影组移动到自定义官能团左侧", "打开模板选项卡，核对两个分组的左右顺序。", "已实现"),
        ("费歇尔式的空白键端点可以填写基团", "插入空白费歇尔式，双击端点输入 OH、H 或自定义文字。", "已实现"),
        ("锯架式的端点可以填写基团", "插入交叉式、重叠式各一次，双击末端编辑文字。", "已实现"),
        ("纽曼式的端点可以填写基团", "插入交错式、重叠式各一次，双击前后键端点编辑文字。", "已实现"),
        ("三种投影的骨架按普通原子和化学键直接编辑", "插入后用普通原子、化学键工具修改骨架，并检查 ChemDraw 往返；当前仍是矢量组合，未达到这一项。", "仍需处理"),
    ]),
    ("箭头菜单和几何", [
        ("箭头头部大小菜单移到最下方", "打开箭头样式菜单，最下方应是非机理箭头的五档大小。", "已实现"),
        ("箭头大小为小、较小、中、较大、大五档", "逐档绘制直线和弧线，观察头部逐级变化；机理箭头不随全局档位改变。", "已实现"),
        ("弧线箭头保留复制、粘贴、锁定弧度", "右键箭头复制弧度，在不同长度箭头上粘贴；锁定后只影响随后新画和调整的箭头。", "已实现"),
        ("弧线箭头增加复制、粘贴、锁定曲率", "用不同长度箭头检验半径保持；包括超过半圆的大弧，锁定后重启也应保存。", "已实现"),
        ("直线箭头允许粘贴弧度与曲率", "右键直线箭头粘贴所复制的值，直线应变为弧线，Ctrl+Z 可撤销。", "已实现"),
        ("选择工具可拖动直线箭头中点", "选直线箭头，抓住中点往侧面拖，松开后形成弧线；再次拖动可调整。", "已实现"),
        ("空心箭头杆不再穿过箭头头部", "用大档空心头放大查看，杆线止于箭头头部底边。", "已实现"),
        ("实心、半箭头及曲线头部与 ChemDraw 视觉对照", "分别保存 CDXML，在 ChemDraw 打开并放大比较端部；不同版本的像素级差异仍需人工核对。", "待人工核对"),
        ("绘制虚线箭头时预览与成品一致", "拖动过程中应持续显示虚线。", "原有修复，待人工复核"),
    ]),
    ("机理生成与界面收缩", [
        ("机理生成区只有一个生成下一步按钮", "打开机理绘制，原三个生成按钮应由一个按钮取代。", "已实现"),
        ("单向箭头生成模式", "点击生成下一步，在弹出菜单选单向箭头。", "已实现"),
        ("可逆箭头生成模式", "在同一菜单选可逆箭头，检查生成产物与箭头。", "已实现"),
        ("可逆趋势向左", "在同一菜单选趋势向左，核对两根箭头相对长度。", "已实现"),
        ("可逆趋势向右", "在同一菜单选趋势向右，核对两根箭头相对长度。", "已实现"),
        ("开始选项卡窄窗口自动收起", "把窗口宽度缩到约 1450 逻辑像素以下，文字与排列分组隐藏，常用工具显示图标及下拉入口。", "已实现"),
        ("图形选项卡窄窗口自动收起", "把窗口继续缩到约 1040 逻辑像素以下，分组改成图标与下拉入口。", "已实现"),
        ("其他常用页窄窗口自动收起", "约 1100 逻辑像素以下检查模板、文字、机理等页，确认常用工具仍可从分组菜单访问。", "已实现"),
        ("窄窗口收起与页签重叠的快捷按钮", "窗口宽约 900 逻辑像素时，右侧快捷按钮按剩余宽度收起；开始、图形、模板工具组保持一排。", "已通过回归"),
        ("较长翻译的页签宽度", "逐个切换界面语言和 Windows 缩放比，核对页签不重叠；尚未逐语种人工核对。", "待人工核对"),
    ]),
    ("官能团与文字", [
        ("未收录文字不自动归一化", "输入不在库中的缩写和任意文字，结束编辑后应保持原拼写及标点。", "已实现"),
        ("已有常用缩写不重复加入", "当前官能团目录有 383 个键名；Me、Et、Ph、Boc、TBS、CF3、MEM、SEM 等原本已经存在。", "已核对"),
        ("逐页审阅用户提供的知乎 PDF", "桌面《知乎缩写.pdf》共 29 页，为扫描件；29 页均已目视查看，并与现有官能团、试剂目录核对筛选。", "已核对"),
        ("新增 An", "4-甲氧基苯基，即文章中的对茴香基；检查连接点与甲氧基对位。", "已实现"),
        ("新增 Bip", "联苯-4-磺酰基；检查磺酰基连接点与联苯对位。", "已实现"),
        ("新增 BOB", "4-苄氧基丁酰基；检查酰基连接点及 4 位苄氧基。", "已实现"),
        ("新增 Chx", "环己基，作为原有 Cy 的别名；两者展开成相同结构。", "已实现"),
        ("新增 Dcpm", "二环丙基甲基；检查中心碳连两个环丙基。", "已实现"),
        ("新增 EOM", "乙氧基甲基；检查连接点—CH₂—O—CH₂CH₃。", "已实现"),
        ("新增 Hmb", "2-羟基-4-甲氧基苄基；核对两处取代位置。", "已实现"),
        ("新增 Nos", "对硝基苯磺酰基；与原有邻硝基苯磺酰基 Ns 分开。", "已实现"),
        ("新增 PNZ", "对硝基苄氧羰基；检查连接点在羰基一端。", "已实现"),
        ("新增 Th", "噻吩-2-基；检查连接位置紧邻硫原子。", "已实现"),
        ("新增 Sia", "1,2-二甲基丙基，即仲异戊基；检查外部连接点在带一个甲基的碳上。", "已实现"),
        ("新增 thexyl", "1,1,2-三甲基丙基；检查外部连接点在带两个甲基的碳上。", "已实现"),
        ("增加 18-Cr-6 与 AQN 别名", "分别指向原有的 18 冠 6、9,10-蒽醌试剂，不复制相同结构。", "已实现"),
        ("新增 DCU 试剂", "N,N′-二环己基脲，核对两个环己基与脲羰基。", "已实现"),
        ("新增 TMAO 试剂", "氧化三甲胺，核对氮正电荷与氧负电荷。", "已实现"),
        ("新增 TMU 试剂", "四甲基脲，核对羰基两侧各两个甲基。", "已实现"),
        ("同名异物缩写不建立自动映射", "PDF 中 PMP、TPP 各有两种解释；DCE、TEA 等也可能歧义，保留原样输入。", "已核对"),
        ("新增缩写必须有单义结构", "不为可能代表多种结构的缩写自动建立官能团或试剂映射。", "执行原则"),
    ]),
    ("TLC 画布操作", [
        ("直接拖动斑点", "选择工具点住一个斑点，可在板内移动到新位置或另一列，其他点不动。", "已实现"),
        ("Ctrl 拖动复制斑点", "按 Ctrl 拖动一个点，原点保持原位，目标处新增一点。", "已实现"),
        ("右键复制到左侧", "在非首列斑点右键，复制到左侧样品列。", "已实现"),
        ("右键复制到右侧", "在非末列斑点右键，复制到右侧样品列。", "已实现"),
        ("右键复制到全部", "在斑点右键，其他样品列各得到一个同高度副本。", "已实现"),
        ("悬停斑点按 Delete", "鼠标停在斑点上按 Delete，只删除该点，不删除整块 TLC 板。", "已实现"),
        ("双击进入编辑器不额外增加斑点", "记录点数，再双击一个已有点进入编辑器，点数应不变。", "已实现"),
        ("点样编辑器中拖动与 Ctrl 复制", "在弹窗的预览里拖动点；Ctrl 拖动应复制。", "已实现"),
        ("直径选择全部或当前斑点", "分别勾选两个方框、输入直径，核对应用范围。", "已实现"),
        ("拖尾开关与长度", "勾选拖尾并改变长度，观察斑点向原点方向拉伸的范围。", "已实现"),
        ("拖尾不透明度", "改变不透明度，尾迹应逐层减淡；参考了 TLC 拖尾照片和教学资料。", "已实现"),
        ("ChemDraw 原生 TLC 对象导出", "保存 CDXML，在 ChemDraw 打开，板、样品列和斑点应显示。", "已实现"),
        ("ChemDraw 26 斑点坐标单位", "5 pt 斑点写为 327680 固定点单位；ChemDraw 26 保存后 Studio 还原列数、点数和尺寸。", "已通过回归"),
        ("ChemDraw 26 实心斑点与拖尾", "原生 tlcspot 写入填充标志；在 ChemDraw 26 打开检查实心圆点及拖尾的显示。", "已实机核对"),
        ("Studio 私有元数据保留逐点属性", "未在 ChemDraw 修改原生 TLC 几何时，往返应保留斑点标识、颜色、透明度和拖尾参数。", "已通过回归"),
    ]),
]

doc = Document()
sec = doc.sections[0]
sec.page_height, sec.page_width = Cm(29.7), Cm(21)
sec.top_margin = Cm(1.9)
sec.bottom_margin = Cm(1.8)
sec.left_margin = Cm(2.1)
sec.right_margin = Cm(2.1)

styles = doc.styles
for name in ("Normal", "Title", "Heading 1", "Heading 2"):
    style = styles[name]
    style.font.name = "Microsoft YaHei"
    style._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
    style.font.color.rgb = RGBColor(0, 0, 0)
styles["Normal"].font.size = Pt(9)
styles["Normal"].paragraph_format.space_after = Pt(4)
styles["Normal"].paragraph_format.line_spacing = 1.2
styles["Title"].font.size = Pt(17)
styles["Title"].font.bold = True
title_ppr = styles["Title"]._element.pPr
for border in title_ppr.findall(qn("w:pBdr")):
    title_ppr.remove(border)
styles["Title"].paragraph_format.space_after = Pt(9)
styles["Heading 1"].font.size = Pt(12)
styles["Heading 1"].font.bold = True
styles["Heading 1"].paragraph_format.space_before = Pt(13)
styles["Heading 1"].paragraph_format.space_after = Pt(5)

doc.add_paragraph("MoleculeStudio 改动逐项核对清单", "Title")
p = doc.add_paragraph()
p.add_run("日期：").bold = True
p.add_run("2026 年 11 月 16 日    ")
p.add_run("测试版本：").bold = True
p.add_run("MoleculeStudio-Progress-20261116.exe")
doc.add_paragraph("这份清单对应本轮模板入口、箭头、机理生成、窄屏界面、文字处理和 TLC 修改。每一行都拆成可单独检查的操作。请在左侧方框自行打勾；右侧状态表示目前的实现或核对进度。")
doc.add_paragraph("程序位置：" + str(ROOT / "out" / "Studio" / "MoleculeStudio-Progress-20261116.exe"))

for section_title, entries in sections:
    doc.add_heading(section_title, level=1)
    for title, check, status in entries:
        p = doc.add_paragraph()
        p.paragraph_format.keep_with_next = True
        p.paragraph_format.space_after = Pt(1)
        run = p.add_run("□  " + title)
        run.bold = True
        run.font.size = Pt(9.5)
        tail = p.add_run("   ［" + status + "］")
        tail.font.size = Pt(8.5)
        tail.font.color.rgb = RGBColor(65, 88, 84)
        q = doc.add_paragraph("核对：" + check)
        q.paragraph_format.left_indent = Cm(0.55)
        q.paragraph_format.space_after = Pt(6)

doc.add_heading("回归检查与交接", level=1)
checks = [
    "本次测试版已通过 --arc-check；报告见 work/test-progress-18-arc。",
    "本次测试版已通过 --table-check；报告见 work/test-progress-18-table，使用 ChemDraw 26 保存回来的 TLC 文件。",
    "本次测试版已通过 --template-library-check 与 --current-fixes-check；报告分别见 work/test-progress-18-library、work/test-progress-19-current。",
    "ChemDraw 实机：在 ChemDraw 26 打开 Studio 输出的 TLC；再另存为 ChemDraw XML 并在 Studio 打开。",
    "详细交接：docs/MOLECULESTUDIO_HANDOFF_20261116.txt。",
    "未完成项：三种投影骨架尚未成为普通原子键拓扑；箭头尖端和多语言窄屏布局仍需人工逐档核对。"
]
for item in checks:
    doc.add_paragraph("□  " + item)

footer = sec.footer.paragraphs[0]
footer.alignment = WD_ALIGN_PARAGRAPH.RIGHT
footer.add_run("MoleculeStudio 逐项核对  ·  ")
field = OxmlElement("w:fldSimple")
field.set(qn("w:instr"), "PAGE")
footer._p.append(field)

OUTPUT.parent.mkdir(parents=True, exist_ok=True)
doc.save(OUTPUT)
print(OUTPUT)
