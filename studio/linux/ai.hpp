#pragma once

#include "ai_document.hpp"
#include "config.hpp"
#include "ocsr.hpp"
#include "worker.hpp"
#include <deque>

namespace linux_ai {
using linux_i18n::tr;
struct AttachmentPicker {
    GtkWindow* owner;
    GtkWidget* listing;
    GtkWidget* clear;
    std::vector<std::string> files;

    void refresh() const {
        std::string names;
        for (const auto& path : files) {
            if (!names.empty()) names += "、";
            auto* name = g_filename_display_basename(path.c_str());
            names += name;
            g_free(name);
        }
        gtk_label_set_text(GTK_LABEL(listing), names.c_str());
        gtk_widget_set_tooltip_text(listing, names.empty() ? nullptr : names.c_str());
        gtk_widget_set_sensitive(clear, !files.empty());
    }
};

struct Panel {
    std::string endpoint = "https://api.deepseek.com";
    std::string model = "auto";
    std::string key;
    std::string preset;
    bool advanced = false;
    std::vector<std::string> offeredModels;
    std::deque<std::pair<std::string,std::string>> history;

    Panel() {
        linux_config::Preferences settings("ai.ini");
        const auto address = settings.text("Assistant", "Endpoint");
        const auto configured = settings.text("Assistant", "Model");
        if (!address.empty()) endpoint = address;
        if (!configured.empty()) model = configured;
        preset = settings.text("Assistant", "Preset");
        advanced = settings.flag("Assistant", "Advanced", false);
    }

    static std::string textOf(GtkWidget* view) {
        auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        const std::unique_ptr<gchar, decltype(&g_free)> value(
            gtk_text_buffer_get_text(buffer, &start, &end, FALSE), g_free);
        return value ? value.get() : "";
    }

    void showMessage(GtkWindow* owner, const char* title, const std::string& message) {
        auto* dialog = gtk_dialog_new_with_buttons(tr(title), owner, GTK_DIALOG_MODAL,
                                                   tr("关闭"), GTK_RESPONSE_CLOSE, nullptr);
        auto* view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), message.c_str(), -1);
        auto* scroller = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(scroller, 540, 210);
        gtk_container_add(GTK_CONTAINER(scroller), view);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroller);
        gtk_widget_show_all(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    struct SettingsFields {
        GtkEntry *endpointInput, *modelInput, *keyInput;
        GtkWidget *preferences, *presetInput, *advancedMode;

        SettingsFields(GtkBox* content, const Panel& panel) {
            auto entry = [&](const char* label, const std::string& value) {
                auto* field = gtk_entry_new();
                auto* heading = gtk_label_new(tr(label));
                gtk_label_set_xalign(GTK_LABEL(heading), 0);
                gtk_box_pack_start(content, heading, FALSE, FALSE, 2);
                gtk_box_pack_start(content, field, FALSE, FALSE, 2);
                gtk_entry_set_text(GTK_ENTRY(field), value.c_str());
                return GTK_ENTRY(field);
            };
            endpointInput = entry("HTTPS API 地址", panel.endpoint);
            auto* modelName = gtk_label_new(tr("模型"));
            gtk_label_set_xalign(GTK_LABEL(modelName), 0);
            gtk_box_pack_start(content, modelName, FALSE, FALSE, 2);
            auto* models = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new_with_entry());
            for (const auto& id : panel.offeredModels) gtk_combo_box_text_append_text(models, id.c_str());
            modelInput = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(models)));
            gtk_entry_set_text(modelInput, panel.model.c_str());
            gtk_box_pack_start(content, GTK_WIDGET(models), FALSE, FALSE, 2);
            keyInput = entry("API Key（本次运行）", panel.key);
            gtk_entry_set_visibility(keyInput, FALSE);
            preferences = gtk_expander_new(tr("绘图预设"));
            presetInput = gtk_text_view_new();
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(presetInput), GTK_WRAP_WORD_CHAR);
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(presetInput)), panel.preset.c_str(), -1);
            auto* presetScroll = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_widget_set_size_request(presetScroll, 450, 85);
            gtk_container_add(GTK_CONTAINER(presetScroll), presetInput);
            gtk_container_add(GTK_CONTAINER(preferences), presetScroll);
            gtk_box_pack_start(content, preferences, FALSE, FALSE, 3);
            advancedMode = gtk_check_button_new_with_label(tr("高级绘图"));
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(advancedMode), panel.advanced);
            gtk_box_pack_start(content, advancedMode, FALSE, FALSE, 4);
        }

        void apply(Panel& panel) const {
            const std::string nextEndpoint = gtk_entry_get_text(endpointInput);
            const std::string nextModel = gtk_entry_get_text(modelInput);
            const std::string nextKey = gtk_entry_get_text(keyInput);
            if (panel.endpoint != nextEndpoint || panel.model != nextModel || panel.key != nextKey) panel.history.clear();
            if (panel.endpoint != nextEndpoint || panel.key != nextKey) panel.offeredModels.clear();
            panel.endpoint = nextEndpoint;
            panel.model = nextModel;
            panel.key = nextKey;
            panel.preset = textOf(presetInput);
            panel.advanced = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(advancedMode));
        }
    };

    std::string saveSettings() const {
        std::string error;
        linux_config::Preferences("ai.ini").replaceGroup("Assistant", {{"Endpoint", endpoint},
            {"Model", model}, {"Preset", preset}, {"Advanced", advanced ? "true" : "false"}}, &error);
        return error;
    }

    std::string request(GtkWindow* owner, Session& session, const std::string& script,
                        const studioai::MechanismStep& mechanismStep) {
        auto* dialog = gtk_dialog_new_with_buttons(tr("AI 绘图"), owner, GTK_DIALOG_MODAL,
                                                   tr("取消"), GTK_RESPONSE_CANCEL, tr("测试连接"), 1, tr("发送并绘图"), 2, nullptr);
        auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
        SettingsFields fields(content, *this);
        auto* prompt = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(prompt), GTK_WRAP_WORD_CHAR);
        gtk_widget_set_size_request(prompt, 530, 120);
        auto* scope = gtk_check_button_new_with_label(tr("随请求发送当前文档的绘图对象"));
        auto* heading = gtk_label_new(tr("绘图要求"));
        gtk_label_set_xalign(GTK_LABEL(heading), 0);
        gtk_box_pack_start(content, heading, FALSE, FALSE, 3);
        gtk_box_pack_start(content, prompt, TRUE, TRUE, 3);
        auto* attachments = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        auto* addFile = gtk_button_new_with_label(tr("选择图片或 PDF…"));
        AttachmentPicker picker{GTK_WINDOW(dialog), gtk_label_new(""), gtk_button_new_with_label(tr("清空附件")), {}};
        gtk_label_set_ellipsize(GTK_LABEL(picker.listing), PANGO_ELLIPSIZE_END);
        gtk_label_set_xalign(GTK_LABEL(picker.listing), 0);
        gtk_box_pack_start(GTK_BOX(attachments), addFile, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(attachments), picker.clear, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(attachments), picker.listing, TRUE, TRUE, 0);
        gtk_box_pack_start(content, attachments, FALSE, FALSE, 3);
        picker.refresh();
        const bool attachmentRuntime = g_file_test(linux_ocsr::interpreter, G_FILE_TEST_IS_EXECUTABLE);
        gtk_widget_set_sensitive(addFile, attachmentRuntime);
        gtk_widget_set_tooltip_text(addFile, tr(attachmentRuntime ?
            "所选附件会随本轮绘图请求发送给当前 AI 服务" : "需要安装 moleculestudio-ocsr"));
        g_signal_connect(addFile, "clicked", G_CALLBACK(+[](GtkButton*, AttachmentPicker* picker) {
            for (auto& path : linux_ocsr::pickFiles(picker->owner, true))
                if (std::find(picker->files.begin(), picker->files.end(), path) == picker->files.end())
                    picker->files.push_back(std::move(path));
            picker->refresh();
        }), &picker);
        g_signal_connect(picker.clear, "clicked", G_CALLBACK(+[](GtkButton*, AttachmentPicker* picker) {
            picker->files.clear();
            picker->refresh();
        }), &picker);
        gtk_box_pack_start(content, scope, FALSE, FALSE, 5);
        gtk_widget_show_all(dialog);
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response != 1 && response != 2) { gtk_widget_destroy(dialog); return {}; }
        fields.apply(*this);
        const auto promptText = textOf(prompt);
        const bool sendScene = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope));
        gtk_widget_destroy(dialog);
        if (key.empty()) return "请输入 API Key";
        if (response == 2 && promptText.empty()) return "请输入绘图要求";
        if (g_utf8_strlen(preset.c_str(), -1) > 6000) return "预设提示词不得超过 6000 字";

        studioai::DrawContext state{session.doc.id(), session.serial, {}, session.view().center,
            session.graphicsWidth > 0 ? session.graphicsWidth : session.doc.defaultLineWidth(), session.graphicsColor};
        if (sendScene && response == 2) {
            state.selection = session.selected;
            if (!state.selection.empty()) {
                const auto bounds = studioio::selectionBounds(session);
                if (!bounds.empty()) state.origin = Vec2((bounds.min.x+bounds.max.x)*.5, bounds.max.y+45);
            }
        }
        tinyxml2::XMLDocument xml;
        auto* root = xml.NewElement("request");
        root->SetAttribute("mode", response == 1 ? "test" : "draw");
        const bool advancedDrawing = response == 2 && advanced;
        if (advancedDrawing) root->SetAttribute("advanced", "1");
        xml.InsertEndChild(root);
        auto field = [&](const char* name, const std::string& text){
            auto* child = xml.NewElement(name);
            child->SetText(text.c_str());
            root->InsertEndChild(child);
        };
        field("endpoint", endpoint);
        field("model", model);
        field("key", key);
        const bool sendAttachments = response == 2 && !picker.files.empty();
        if (response == 2) {
            field("prompt", promptText);
            if (!preset.empty()) field("preset", preset);
            for (const auto& path : picker.files) {
                auto* file = xml.NewElement("image");
                file->SetAttribute("kind", "upload");
                file->SetAttribute("path", path.c_str());
                root->InsertEndChild(file);
            }
            auto* context = xml.NewElement("context");
            root->InsertEndChild(context);
            context->SetAttribute("text_size", session.doc.defaultTextSize());
            context->SetAttribute("font", session.doc.defaultFont().c_str());
            context->SetAttribute("bond_length", session.doc.defaultBondLength());
            context->SetAttribute("line_width", state.width);
            context->SetAttribute("color", aiobjects::hexColor(state.color).c_str());
            context->SetAttribute("selected", int(state.selection.size()));
            context->SetAttribute("atoms", sendScene ? int(session.doc.stats().aliveAtoms) : 0);
            context->SetAttribute("bonds", sendScene ? int(session.doc.stats().aliveBonds) : 0);
            if (sendScene) aiobjects::snapshot(session.doc, state.selection, state.origin, xml, root);
            auto* messages = xml.NewElement("history");
            root->InsertEndChild(messages);
            for (const auto& item : history) {
                auto* message = xml.NewElement("message");
                message->SetAttribute("role", item.first.c_str());
                message->SetText(item.second.c_str());
                messages->InsertEndChild(message);
            }
        }
        tinyxml2::XMLPrinter data;
        xml.Print(&data);
        std::string failure;
        const auto* interpreter = sendAttachments ? linux_ocsr::interpreter : "/usr/bin/python3";
        const auto bytes = linux_worker::run(owner, "正在等待 AI 服务…", {interpreter, script}, data.CStr(), failure,
            advancedDrawing ? 1830 : 180);
        if (!failure.empty()) return failure;
        if (bytes.empty()) return {};
        tinyxml2::XMLDocument result;
        if (result.Parse(bytes.c_str()) != tinyxml2::XML_SUCCESS || !result.RootElement()) return "无法读取 AI 服务返回";
        auto* node = result.RootElement()->FirstChildElement("message");
        const std::string reply = node && node->GetText() ? node->GetText() : "";
        if (!result.RootElement()->BoolAttribute("ok")) return reply.empty() ? "AI 执行失败" : reply;
        if (const auto* list = result.RootElement()->FirstChildElement("models")) {
            offeredModels.clear();
            for (auto* option = list->FirstChildElement("model"); option; option = option->NextSiblingElement("model"))
                if (const auto* id = option->Attribute("id")) offeredModels.emplace_back(id);
        }
        const auto configurationFailure = saveSettings();
        if (response == 2) {
            const auto applied = studioai::apply(session, bytes, state, mechanismStep);
            if (!applied.empty()) session.status = applied;
            if (!sendAttachments) {
                history.push_back({"user", promptText});
                history.push_back({"assistant", reply});
                while (history.size() > 10) history.pop_front();
            }
        }
        if (!reply.empty()) showMessage(owner, response == 1 ? "连接测试" : "AI 绘图结果", reply);
        if (!configurationFailure.empty()) session.status = configurationFailure;
        return {};
    }
};
}
