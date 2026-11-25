#pragma once

#include "config.hpp"
#include "i18n.hpp"
#include "keymap_data.hpp"
#include <gtk/gtk.h>
#include <algorithm>
#include <iterator>
#include <map>
#include "shortcut_zh.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace linux_shortcuts {
enum class Action {
    New, Open, Close, Save, SaveAs, ExportCdxml, Print, ExportPdf, Undo, Redo, Repeat, SelectAll, Copy, CopyPng, Cut, Paste, Delete,
    EditLabel, EditTable, InsertTable, Fit, FitPage, ActualSize, ZoomIn, ZoomOut, FixedLength, FixedAngles, PageSetup,
    NudgeHorizontal, NudgeVertical, Group, Ungroup, Align, Distribute, Bold, Italic, Underline, TextScript, TextTypography, TextAlign, Perspective,
    Merge, Rotate, RotateStep, PreciseTransform, Flip, Stack, Formula, Conformer, Format, CopyChemicalText, PasteChemicalText, Templates, SaveTemplate, OpenTemplate, ImportTemplate, ExportTemplate,
    NameToStructure, StructureToName, Cleanup, CleanReaction,
    Expand, Collapse, CustomGroups, Stereo, Nmr, Recognize, Assistant, MechanismStep, MechanismReagent,
    MechanismState, MechanismPlacement, TransitionState, ReactiveCenter, AttachBaseHydrogen, Catalytic,
    GraphicFormat,
    Language, Settings, DrawingStyles, Tool,
    ExportSkill, CheckUpdates, EditHoveredAtom, ClearHoveredLabel, Hotspot, Calculator, Skin
};

enum class Scope { Window, Canvas, Atom, Bond, Text };

struct Shortcut {
    std::string id;
    std::string label;
    Action action;
    std::string initial;
    int argument = 0;
    const char* tool = nullptr;
    Scope scope = Scope::Window;
};

inline const std::vector<Shortcut>& catalog() {
    static const auto entries = [] {
        std::vector<Shortcut> result = {
            {"new", "新建", Action::New, "<Control>n"},
            {"open", "打开", Action::Open, "<Control>o"},
            {"close", "关闭文档", Action::Close, "<Control>w"},
            {"save", "保存", Action::Save, "<Control>s"},
            {"saveAs", "另存为", Action::SaveAs, "<Control><Shift>s"},
            {"exportCdxml", "导出 CDXML…", Action::ExportCdxml},
            {"print", "打印", Action::Print, "<Control>p"},
            {"exportPdf", "导出 PDF", Action::ExportPdf},
            {"undo", "撤销", Action::Undo, "<Control>z"},
            {"redo", "重做", Action::Redo, "<Control><Shift>z"},
            {"redoAlternate", "重做（备用）", Action::Redo, "<Control>y"},
            {"repeat", "重复上一步", Action::Repeat},
            {"selectAll", "全选", Action::SelectAll, "<Control>a"},
            {"copy", "复制", Action::Copy, "<Control>c"},
            {"copyPng", "复制为 PNG", Action::CopyPng},
            {"cut", "剪切", Action::Cut, "<Control>x"},
            {"paste", "粘贴", Action::Paste, "<Control>v"},
            {"delete", "删除", Action::Delete, "Delete"},
            {"editLabel", "编辑文字、原子标签或表格", Action::EditLabel, ""},
            {"editTable", "编辑表格", Action::EditTable},
            {"insertTable", "插入表格", Action::InsertTable},
            {"insertThreeLineTable", "插入三线表", Action::InsertTable, "", 1},
            {"fit", "适应窗口", Action::Fit, "F6"},
            {"fitPage", "适合页面", Action::FitPage},
            {"actualSize", "实际大小", Action::ActualSize, "F5"},
            {"zoomIn", "放大", Action::ZoomIn, "F7"},
            {"zoomOut", "缩小", Action::ZoomOut, "F8"},
            {"perspective", "结构透视", Action::Perspective},
            {"fixedLength", "固定键长", Action::FixedLength, "<Control>l"},
            {"fixedAngles", "固定键角", Action::FixedAngles, "<Control>e"},
            {"nudgeLeft", "向左微移 1 磅", Action::NudgeHorizontal, "Left", -1},
            {"nudgeRight", "向右微移 1 磅", Action::NudgeHorizontal, "Right", 1},
            {"nudgeUp", "向上微移 1 磅", Action::NudgeVertical, "Up", -1},
            {"nudgeDown", "向下微移 1 磅", Action::NudgeVertical, "Down", 1},
            {"nudgeLeft10", "向左微移 10 磅", Action::NudgeHorizontal, "<Shift>Left", -10},
            {"nudgeRight10", "向右微移 10 磅", Action::NudgeHorizontal, "<Shift>Right", 10},
            {"nudgeUp10", "向上微移 10 磅", Action::NudgeVertical, "<Shift>Up", -10},
            {"nudgeDown10", "向下微移 10 磅", Action::NudgeVertical, "<Shift>Down", 10},
            {"group", "编组", Action::Group, "<Control>g"},
            {"ungroup", "解组", Action::Ungroup, "<Control><Shift>g"},
            {"alignLeft", "左边缘对齐", Action::Align, "<Control><Alt><Shift>l", 0},
            {"alignHorizontal", "水平居中对齐", Action::Align, "<Control><Alt><Shift>c", 1},
            {"alignRight", "右边缘对齐", Action::Align, "<Control><Alt><Shift>r", 2},
            {"alignTop", "顶部对齐", Action::Align, "<Control><Alt><Shift>t", 3},
            {"alignVertical", "垂直居中对齐", Action::Align, "<Control><Alt><Shift>m", 4},
            {"alignBottom", "底部对齐", Action::Align, "<Control><Alt><Shift>b", 5},
            {"distributeHorizontal", "水平分布", Action::Distribute, "<Control><Alt><Shift>h", 0},
            {"distributeVertical", "垂直分布", Action::Distribute, "<Control><Alt><Shift>v", 1},
            {"mergeSelection", "合并", Action::Merge},
            {"rotateSelection", "旋转 90°", Action::Rotate},
            {"rotateLeft1", "逆时针旋转 1°", Action::RotateStep, "<Alt>Left", -1},
            {"rotateRight1", "顺时针旋转 1°", Action::RotateStep, "<Alt>Right", 1},
            {"rotateLeft15", "逆时针旋转 15°", Action::RotateStep, "<Alt>Up", -15},
            {"rotateRight15", "顺时针旋转 15°", Action::RotateStep, "<Alt>Down", 15},
            {"rotateDegrees", "旋转对象", Action::PreciseTransform},
            {"scalePercent", "缩放对象", Action::PreciseTransform, "", 1},
            {"flipHorizontal", "水平翻转", Action::Flip, "", 0},
            {"flipVertical", "垂直翻转", Action::Flip, "", 1},
            {"stackFront", "置于顶层", Action::Stack, "F2", 1},
            {"stackBack", "置于底层", Action::Stack, "F3", 0},
            {"bold", "加粗", Action::Bold, "<Control>b"},
            {"italic", "斜体", Action::Italic, "<Control>i"},
            {"underline", "下划线", Action::Underline, "<Control>u"},
            {"textFormula", "化学式格式", Action::TextScript, "<Control>f", r62::core::kFormula},
            {"textSubscript", "下标", Action::TextScript, "F9", r62::core::kSubscript, nullptr, Scope::Text},
            {"textSuperscript", "上标", Action::TextScript, "F10", r62::core::kSuperscript},
            {"textFont", "字体", Action::TextTypography, "", 0},
            {"textSizeUp", "增大字号", Action::TextTypography, "", 1},
            {"textSizeDown", "减小字号", Action::TextTypography, "", -1},
            {"textColor", "文字颜色", Action::TextTypography, "", 2},
            {"textLeft", "文字左对齐", Action::TextAlign, "<Control><Shift>l", 0},
            {"textCenter", "文字居中", Action::TextAlign, "<Control><Shift>c", 1},
            {"textRight", "文字右对齐", Action::TextAlign, "<Control><Shift>r", 2},
            {"textJustify", "两端对齐", Action::TextAlign, "", 3},
            {"formula", "分子式", Action::Formula},
            {"calculator", "计算器", Action::Calculator},
            {"skin", "皮肤", Action::Skin},
            {"conformer", "三维整理", Action::Conformer, "<Control><Shift>d"},
            {"format", "格式转换", Action::Format},
            {"copyAsSMILES", "复制为 SMILES", Action::CopyChemicalText, "<Control><Alt>c", 0},
            {"copyAsHELM", "复制为 HELM", Action::CopyChemicalText, "<Control><Alt>e", 1},
            {"copyAsMol", "复制为 Mol", Action::CopyChemicalText, "<Control><Alt><Shift>o", 2},
            {"copyAsMolV3000", "复制为 Mol V3000", Action::CopyChemicalText, "<Control><Alt>o", 3},
            {"pasteAsSMILES", "从 SMILES 粘贴", Action::PasteChemicalText, "<Control><Alt>p", 0},
            {"pasteAsHELM", "从 HELM 粘贴", Action::PasteChemicalText, "<Control><Shift>e", 1},
            {"pasteAsMol", "从 Mol 粘贴", Action::PasteChemicalText, "<Control><Alt><Shift>p", 2},
            {"templates", "模板", Action::Templates},
            {"saveTemplate", "添加自定义模板", Action::SaveTemplate},
            {"openTemplate", "我的模板", Action::OpenTemplate},
            {"importTemplate", "导入自定义模板", Action::ImportTemplate},
            {"exportTemplate", "导出自定义模板", Action::ExportTemplate},
            {"nameToStructure", "命名转结构", Action::NameToStructure, "<Control><Shift>n"},
            {"structureToName", "结构转命名", Action::StructureToName, "<Control><Alt>n"},
            {"cleanup", "二维结构整理", Action::Cleanup, "<Control><Shift>k"},
            {"cleanReaction", "反应整理", Action::CleanReaction, "<Control><Shift>x"},
            {"expand", "官能团展开", Action::Expand},
            {"collapse", "官能团收起", Action::Collapse},
            {"customGroups", "自定义官能团", Action::CustomGroups},
            {"stereo", "构型标注", Action::Stereo},
            {"nmr", "预测核磁", Action::Nmr},
            {"recognize", "图片转结构", Action::Recognize, "F9", 0, nullptr, Scope::Canvas},
            {"assistant", "AI 绘图", Action::Assistant},
            {"mechanismStep", "生成机理下一步", Action::MechanismStep},
            {"mechanismTransfer", "下一步 + 质子转移", Action::MechanismStep, "", 1},
            {"mechanismDeprotonate", "下一步 − 质子", Action::MechanismStep, "", 2},
            {"mechanismBase", "插入 B⁻", Action::MechanismReagent, "", 0},
            {"mechanismNucleophile", "插入 Nu⁻", Action::MechanismReagent, "", 1},
            {"mechanismProton", "插入 H⁺", Action::MechanismReagent, "", 2},
            {"catalyticClockwise", "顺时针循环", Action::Catalytic, "", 0},
            {"catalyticCounterclockwise", "逆时针循环", Action::Catalytic, "", 1},
            {"catalyticReagent", "加入试剂", Action::Catalytic, "", 2},
            {"catalyticProduct", "放出产物", Action::Catalytic, "", 3},
            {"catalyticMetal", "金属与配体", Action::Catalytic, "", 4},
            {"catalyticOutline", "添加催化循环路径", Action::Catalytic, "", 5},
            {"positiveCharge", "正电荷", Action::MechanismState, "", 1},
            {"negativeCharge", "负电荷", Action::MechanismState, "", -1},
            {"circledPositiveCharge", "圈正电荷", Action::MechanismState, "", 3},
            {"circledNegativeCharge", "圈负电荷", Action::MechanismState, "", -3},
            {"lonePair", "插入孤对电子", Action::MechanismState},
            {"radical", "插入自由基", Action::MechanismState, "", 2},
            {"mechanismEquivalence", "等同符号 ≡", Action::MechanismPlacement, "", 1},
            {"partialPositive", "标注 δ+", Action::MechanismPlacement, "", 2},
            {"partialNegative", "标注 δ−", Action::MechanismPlacement, "", 3},
            {"virtualBond", "待成键", Action::MechanismPlacement, "", 4},
            {"transitionState", "过渡态 [ ]‡", Action::TransitionState},
            {"allene", "插入连烯", Action::ReactiveCenter, "", 1},
            {"carbene", "插入卡宾", Action::ReactiveCenter, "", 0},
            {"attachBaseHydrogen", "添加 B–H 化学键", Action::AttachBaseHydrogen},
            {"language", "界面语言", Action::Language},
            {"shortcuts", "快捷键", Action::Settings},
            {"exportSkill", "另存 AI 绘图 Skill", Action::ExportSkill},
            {"checkUpdates", "检查更新", Action::CheckUpdates},
            {"drawingStyles", "绘图预设", Action::DrawingStyles},
            {"graphicsWidth", "线宽", Action::GraphicFormat, "", 0},
            {"graphicsColor", "绘图颜色", Action::GraphicFormat, "", 1},
            {"shapeFill", "图案填充", Action::GraphicFormat, "", 2},
            {"pageSetup", "页面与排版", Action::PageSetup},
            {"marqueeTool", "框选工具", Action::Tool, "space", 0, "chooseSelectionTool_Marquee"},
            {"singleTool", "单键工具", Action::Tool, "1", 0, "chooseBondTool_Solid"},
            {"solidTool", "单键工具（备用）", Action::Tool, "x", 0, "chooseBondTool_Solid"},
            {"doubleTool", "双键工具", Action::Tool, "2", 0, "chooseMultipleBondsTool_DoubleBond"},
            {"tripleTool", "三键工具", Action::Tool, "3", 0, "chooseMultipleBondsTool_TripleBond"},
            {"pentagonTool", "五元环工具", Action::Tool, "5", 0, "chooseRingTool_Cyclopentane"},
            {"hexagonTool", "六元环工具", Action::Tool, "6", 0, "chooseRingTool_Cyclohexane"},
            {"chainTool", "碳链工具", Action::Tool, "<Shift>x", 0, "chooseAlkaneTool_Snaking_Chain"},
            {"benzeneTool", "苯环", Action::Tool, "j", 0, "chooseRingTool_Benzene"},
            {"textTool", "文字工具", Action::Tool, "t", 0, "chooseEdittingTool_Text"},
            {"tableTool", "绘制表格", Action::Tool, "", 0, "chooseTableTool"},
            {"arrowTool", "实心箭头", Action::Tool, "e", 0, "chooseArrowTool_Large_Solid"},
            {"mechanismArrowTool", "机理箭头", Action::Tool, "", 0, "chooseArrowTool_90_CW"},
            {"fishhookTool", "机理鱼钩箭头", Action::Tool, "", 0, "chooseArrowTool_MechanismFishhook"},
            {"editHoveredAtom", "编辑原子标签", Action::EditHoveredAtom, "Return", 0, nullptr, Scope::Atom},
            {"clearHoveredLabel", "清除原子标签", Action::ClearHoveredLabel, "BackSpace", 0, nullptr, Scope::Atom}
        };
        for (auto& item : result)
            if (item.action == Action::Tool) item.scope = Scope::Canvas;
        for (size_t i = 0; i < std::size(keymap::hotkeys); ++i) {
            const auto& source = keymap::hotkeys[i];
            const std::string kind = source.object;
            if (kind == "Generic") continue;
            const std::string operation = source.command;
            const bool hydrogen = operation == "LABELTEXT" && std::string(source.value) == "H";
            auto label = hydrogen ? std::string("氧/氮循环氢数；其他原子改为氢") : shortcutChinese(source);
            const guint key = gdk_unicode_to_keyval(static_cast<unsigned char>(source.key[0]));
            const guint lower = gdk_keyval_to_lower(key);
            const auto modifiers = key == lower ? GdkModifierType(0) : GDK_SHIFT_MASK;
            gchar* accelerator = gtk_accelerator_name(lower, modifiers);
            result.push_back({"hotspot." + kind + "." + source.key, std::move(label), Action::Hotspot,
                accelerator, int(i), nullptr, kind == "Atom" ? Scope::Atom : Scope::Bond});
            g_free(accelerator);
        }
        return result;
    }();
    return entries;
}

inline const Shortcut& shortcut(const char* id) {
    for (const auto& item : catalog())
        if (item.id == id) return item;
    throw std::logic_error("Unknown GTK shortcut");
}

inline std::string commandName(const Shortcut& item) {
    auto label = linux_i18n::message(item.label);
    if (item.action != Action::Hotspot) return label;
    const auto& source = keymap::hotkeys[item.argument];
    if (std::string(source.command) == "LABELTEXT" && std::string(source.value) != "H")
        label += " · " + std::string(source.value);
    return label;
}

struct Chord {
    guint key = 0;
    GdkModifierType modifiers = GdkModifierType(0);
    bool operator==(const Chord& other) const { return key == other.key && modifiers == other.modifiers; }
};

inline Chord chord(guint key, guint modifiers) {
    constexpr guint allowed = GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK;
    if (key == GDK_KEY_KP_Add) key = GDK_KEY_plus;
    if (key == GDK_KEY_KP_Subtract) key = GDK_KEY_minus;
    if (key == GDK_KEY_KP_Enter) key = GDK_KEY_Return;
    // A punctuation keyval already includes the shift needed to type it.
    if (g_unichar_ispunct(gdk_keyval_to_unicode(key)) || key == GDK_KEY_plus)
        modifiers &= ~GDK_SHIFT_MASK;
    return {gdk_keyval_to_lower(key), GdkModifierType(modifiers & allowed)};
}

inline Chord parse(const std::string& value) {
    Chord parsed;
    gtk_accelerator_parse(value.c_str(), &parsed.key, &parsed.modifiers);
    return chord(parsed.key, parsed.modifiers);
}

inline std::string display(Chord binding) {
    if (!binding.key) return linux_i18n::tr("未分配");
    gchar* value = gtk_accelerator_get_label(binding.key, binding.modifiers);
    std::string result(value);
    g_free(value);
    return result;
}

inline bool assignable(const Shortcut& shortcut, Chord value) {
    if (!value.key) return true;
    if (value.key == GDK_KEY_Escape || value.key == GDK_KEY_Tab || value.key == GDK_KEY_ISO_Left_Tab)
        return false;
    const auto character = gdk_keyval_to_unicode(value.key);
    const bool modified = (value.modifiers & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK)) != 0;
    return value.key != GDK_KEY_VoidSymbol &&
        ((shortcut.scope != Scope::Window && shortcut.scope != Scope::Text) || !g_unichar_isprint(character) || modified);
}

class Keymap {
    std::vector<Chord> keys_;
public:
    Keymap() { restoreDefaults(); }

    void restoreDefaults() {
        keys_.clear();
        for (const auto& item : catalog()) keys_.push_back(parse(item.initial));
    }

    static Keymap load() {
        Keymap loaded;
        linux_config::Preferences prefs("shortcuts.ini");
        std::vector<size_t> overrides;
        for (size_t i = 0; i < loaded.keys_.size(); ++i) {
            const auto stored = prefs.text("Keys", catalog()[i].id.c_str());
            if (stored.empty()) continue;
            if (stored == "none") { loaded.keys_[i] = {}; continue; }
            const auto value = parse(stored);
            if (value.key && assignable(catalog()[i], value)) {
                loaded.keys_[i] = value;
                overrides.push_back(i);
            }
        }
        // A new default may reuse a key already chosen by a user on an older version.
        for (const auto i : overrides)
            while (const auto* other = loaded.conflict(i, loaded.keys_[i]))
                loaded.keys_[size_t(other - catalog().data())] = {};
        return loaded;
    }

    bool save(std::string& failure) const {
        std::map<std::string, std::string> changed;
        for (size_t i = 0; i < keys_.size(); ++i) {
            const auto& item = catalog()[i];
            const auto key = keys_[i];
            if (key == parse(item.initial)) continue;
            if (!key.key) changed.emplace(item.id, "none");
            else {
                auto* stored = gtk_accelerator_name(key.key, key.modifiers);
                changed.emplace(item.id, stored);
                g_free(stored);
            }
        }
        return linux_config::Preferences("shortcuts.ini").replaceGroup("Keys", changed, &failure);
    }

    Chord at(size_t index) const { return keys_.at(index); }
    Chord binding(const Shortcut& item) const { return at(size_t(&item - catalog().data())); }
    void set(size_t index, Chord value) { keys_.at(index) = value; }

    const Shortcut* conflict(size_t index, Chord value) const {
        if (!value.key) return nullptr;
        const auto scope = catalog()[index].scope;
        for (size_t i = 0; i < keys_.size(); ++i) {
            const auto other = catalog()[i].scope;
            if (i != index && keys_[i] == value &&
                    (scope == other || scope == Scope::Window || other == Scope::Window))
                return &catalog()[i];
        }
        return nullptr;
    }

    const Shortcut* match(Chord value, Scope scope) const {
        if (!value.key) return nullptr;
        for (size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i] == value && catalog()[i].scope == scope)
                return &catalog()[i];
        return nullptr;
    }
};

inline Keymap& current() {
    static Keymap keys = Keymap::load();
    return keys;
}

struct Editor {
    enum Column { Name, Key, Modifiers, Default, Index, Context, Count };
    Keymap draft;
    GtkListStore* list;
    GtkWidget* status;

    void refresh() const {
        static const char* scopes[] = {"全局", "画布", "原子", "化学键", "文字"};
        gtk_list_store_clear(list);
        for (size_t i = 0; i < catalog().size(); ++i) {
            const auto& item = catalog()[i];
            GtkTreeIter row;
            gtk_list_store_append(list, &row);
            const auto value = draft.at(i);
            gtk_list_store_set(list, &row, Name, commandName(item).c_str(), Key, value.key,
                Modifiers, value.modifiers, Default, display(parse(item.initial)).c_str(), Index, guint(i),
                Context, linux_i18n::tr(scopes[static_cast<int>(item.scope)]), -1);
        }
    }

    void update(const gchar* path, Chord value) {
        GtkTreeIter row;
        if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(list), &row, path)) return;
        guint index;
        gtk_tree_model_get(GTK_TREE_MODEL(list), &row, Index, &index, -1);
        if (!assignable(catalog()[index], value)) {
            gtk_label_set_text(GTK_LABEL(status), linux_i18n::tr("请使用组合键或功能键"));
            return;
        }
        if (const auto* other = draft.conflict(index, value)) {
            const auto message = std::string(linux_i18n::tr("快捷键已分配给")) + " " + commandName(*other);
            gtk_label_set_text(GTK_LABEL(status), message.c_str());
            return;
        }
        draft.set(index, value);
        gtk_list_store_set(list, &row, Key, value.key, Modifiers, value.modifiers, -1);
        gtk_label_set_text(GTK_LABEL(status), "");
    }
};

inline bool configure(GtkWindow* owner) {
    using linux_i18n::tr;
    auto* dialog = gtk_dialog_new_with_buttons(tr("设置 · 快捷键"), owner, GTK_DIALOG_MODAL,
        tr("取消"), GTK_RESPONSE_CANCEL, tr("恢复默认"), 1, tr("应用"), GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 620, 480);
    auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    auto* hint = gtk_label_new(tr("双击快捷键以修改，按退格键清除"));
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_box_pack_start(content, hint, FALSE, FALSE, 6);
    auto* list = gtk_list_store_new(Editor::Count, G_TYPE_STRING, G_TYPE_UINT, GDK_TYPE_MODIFIER_TYPE,
        G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
    auto* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list));
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
        tr("命令"), gtk_cell_renderer_text_new(), "text", Editor::Name, nullptr);
    auto* key = gtk_cell_renderer_accel_new();
    g_object_set(key, "editable", TRUE, "accel-mode", GTK_CELL_RENDERER_ACCEL_MODE_OTHER, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
        tr("快捷键"), key, "accel-key", Editor::Key, "accel-mods", Editor::Modifiers, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
        tr("默认"), gtk_cell_renderer_text_new(), "text", Editor::Default, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1,
        tr("生效范围"), gtk_cell_renderer_text_new(), "text", Editor::Context, nullptr);
    auto* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroller), tree);
    gtk_box_pack_start(content, scroller, TRUE, TRUE, 0);
    auto* status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(status), 0);
    gtk_box_pack_start(content, status, FALSE, FALSE, 6);
    Editor edit{current(), list, status};
    edit.refresh();
    g_signal_connect(key, "accel-edited", G_CALLBACK(+[](GtkCellRendererAccel*, gchar* path,
        guint value, GdkModifierType modifiers, guint, Editor* edit) {
            edit->update(path, chord(value, modifiers));
        }), &edit);
    g_signal_connect(key, "accel-cleared", G_CALLBACK(+[](GtkCellRendererAccel*, gchar* path, Editor* edit) {
        edit->update(path, {});
    }), &edit);
    gtk_widget_show_all(dialog);
    bool saved = false;
    while (true) {
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == 1) {
            edit.draft.restoreDefaults();
            edit.refresh();
            gtk_label_set_text(GTK_LABEL(status), "");
        } else if (response == GTK_RESPONSE_ACCEPT) {
            std::string failure;
            if (edit.draft.save(failure)) {
                current() = edit.draft;
                saved = true;
                break;
            }
            gtk_label_set_text(GTK_LABEL(status), linux_i18n::message(failure).c_str());
        } else break;
    }
    gtk_widget_destroy(dialog);
    g_object_unref(list);
    return saved;
}
}
