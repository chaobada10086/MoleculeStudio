#pragma once

// The shell owns the tab strip; each stack child owns an independent editor.
struct DocumentHost {
    GtkWidget* shell = nullptr;
    GtkWidget* stack = nullptr;
    GtkWidget* strip = nullptr;
    GtkWidget* scroll = nullptr;
    GtkWidget* info = nullptr;
    GtkWidget* formula = nullptr;
    GtkWidget* mass = nullptr;
    guint infoIdle = 0;
    uint64_t infoDocument = 0, infoSerial = 0;
    std::vector<ObjectRef> infoSelection;
    std::vector<Window*> documents;
    Window* active = nullptr;
    bool tabbed = false;
    ~DocumentHost() { if (infoIdle) g_source_remove(infoIdle); }
};

static DocumentHost* hostFor(GtkWidget* shell) {
    return shell ? static_cast<DocumentHost*>(g_object_get_data(G_OBJECT(shell), "document-host")) : nullptr;
}

static std::string formulaWithSubscripts(const std::string& plain) {
    static const char* sub[] = {"₀","₁","₂","₃","₄","₅","₆","₇","₈","₉"};
    static const char* super[] = {"⁰","¹","²","³","⁴","⁵","⁶","⁷","⁸","⁹"};
    std::string result;
    bool isotope = false, charge = false;
    for (char ch : plain) {
        if (ch == '[') isotope = true;
        if (ch == ']') isotope = false;
        if (ch == '(') charge = true;
        if (!charge && ch >= '0' && ch <= '9') result += (isotope ? super : sub)[ch-'0'];
        else result += ch;
    }
    return result;
}

static void queueSelectionInfo(Window& doc) {
    auto* host = hostFor(doc.window);
    if (!host || !host->info || host->active != &doc || host->infoIdle) return;
    host->infoIdle = g_idle_add(+[](gpointer data) -> gboolean {
        auto* h = static_cast<DocumentHost*>(data);
        h->infoIdle = 0;
        auto* current = h->active;
        if (!current || current->spectrumPage || current->session.selected.empty()) {
            gtk_widget_hide(h->info);
            return G_SOURCE_REMOVE;
        }
        auto& s = current->session;
        if (h->infoDocument != s.doc.id() || h->infoSerial != s.serial || h->infoSelection != s.selected) {
            const auto info = studio::analyze(s.doc, s.selected);
            const auto formula = info.valid ? formulaWithSubscripts(info.plain) : std::string(tr("分子式")) + " —";
            const auto mass = info.valid ? info.mass + " g/mol" : std::string(tr("分子量")) + " —";
            gtk_label_set_text(GTK_LABEL(h->formula), formula.c_str());
            gtk_label_set_text(GTK_LABEL(h->mass), mass.c_str());
            const auto hint = std::string(tr("计算器")) + " · " + formula + " · " + mass;
            gtk_widget_set_tooltip_text(h->info, hint.c_str());
            atk_object_set_name(gtk_widget_get_accessible(h->info), hint.c_str());
            h->infoDocument = s.doc.id();
            h->infoSerial = s.serial;
            h->infoSelection = s.selected;
        }
        gtk_widget_show(h->info);
        return G_SOURCE_REMOVE;
    }, host);
}

static void addSelectionInfo(DocumentHost& host, GtkHeaderBar* header) {
    host.info = gtk_button_new();
    gtk_widget_set_no_show_all(host.info, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(host.info), "selection-info");
    auto* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    host.formula = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(host.formula), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(host.formula), 14);
    host.mass = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(box), host.formula, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), host.mass, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(host.info), box);
    gtk_widget_show_all(box);
    gtk_header_bar_pack_end(header, host.info);
    g_signal_connect(host.info, "clicked", G_CALLBACK(+[](GtkButton*, DocumentHost* h) {
        if (auto* doc = h->active) {
            doc->finishCanvasText();
            linux_calculator::show(GTK_WINDOW(h->shell), doc->session);
            doc->report({});
        }
    }), &host);
}

static std::vector<Window*> documentsOf(GtkApplication* app) {
    std::vector<Window*> result;
    for (auto* row = app ? gtk_application_get_windows(app) : nullptr; row; row = row->next) {
        if (auto* host = hostFor(GTK_WIDGET(row->data)))
            result.insert(result.end(), host->documents.begin(), host->documents.end());
    }
    return result;
}

static void titleDocument(Window& doc, const std::string& title, const char* name) {
    if (doc.shellTitle == title && doc.tabSourcePath == doc.session.sourcePath) return;
    doc.shellTitle = title;
    doc.tabSourcePath = doc.session.sourcePath;
    if (doc.tabLabel) {
        std::string text = name;
        const auto dot = text.rfind('.');
        if (dot != std::string::npos && dot > 0 && g_utf8_strlen(text.c_str() + dot, -1) <= 6)
            text.resize(dot);
        if (doc.session.dirty) text += "  *";
        gtk_label_set_text(GTK_LABEL(doc.tabLabel), text.c_str());
        std::string hint = name;
        if (!doc.session.sourcePath.empty()) hint += "\n" + doc.session.sourcePath;
        gtk_widget_set_tooltip_text(doc.tab, hint.c_str());
    }
    if (hostFor(doc.window)->active == &doc && g_strcmp0(gtk_window_get_title(GTK_WINDOW(doc.window)), title.c_str()))
        gtk_window_set_title(GTK_WINDOW(doc.window), title.c_str());
}

static void revealTab(Window& doc) {
    auto* host = hostFor(doc.window);
    if (!doc.tab || host->active != &doc) return;
    auto* adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(host->scroll));
    GtkAllocation rect{};
    gtk_widget_get_allocation(doc.tab, &rect);
    gtk_adjustment_clamp_page(adjustment, rect.x, rect.x + rect.width);
}

static void selectDocument(Window& doc) {
    auto* host = hostFor(doc.window);
    if (host->active != &doc) {
        if (host->active) {
            host->active->finishCanvasText();
            if (host->active->tab)
                gtk_style_context_remove_class(gtk_widget_get_style_context(host->active->tab), "current-document");
        }
        host->active = &doc;
        gtk_stack_set_visible_child(GTK_STACK(host->stack), doc.page);
        if (doc.tab) gtk_style_context_add_class(gtk_widget_get_style_context(doc.tab), "current-document");
    }
    gtk_window_set_title(GTK_WINDOW(doc.window), doc.shellTitle.c_str());
    queueSelectionInfo(doc);
    revealTab(doc);
    gtk_widget_grab_focus(doc.canvas);
}

static void closeDocument(Window& doc) {
    auto* host = hostFor(doc.window);
    if (host->documents.size() == 1) { gtk_window_close(GTK_WINDOW(doc.window)); return; }
    doc.finishCanvasText();
    if (!doc.canClose()) return;
    auto pos = std::find(host->documents.begin(), host->documents.end(), &doc);
    const auto index = size_t(pos - host->documents.begin());
    host->documents.erase(pos);
    const bool closingActive = host->active == &doc;
    if (closingActive) host->active = nullptr;
    auto* page = doc.page;
    gtk_widget_destroy(doc.tab);
    if (closingActive) selectDocument(*host->documents[std::min(index, host->documents.size() - 1)]);
    gtk_widget_destroy(page);
}

static void newDocumentWindow(GtkWidget* shell) {
    showDocument(gtk_window_get_application(GTK_WINDOW(shell)), nullptr, {}, nullptr, true);
}

static gboolean documentKey(GtkWidget*, GdkEventKey* event, DocumentHost* host) {
    auto* doc = host->active;
    if (!doc) return FALSE;
    auto* focus = gtk_window_get_focus(GTK_WINDOW(host->shell));
    if (focus && (focus == doc->fontEntry || focus == doc->sizeEntry)) return FALSE;
    const auto key = gdk_keyval_to_lower(event->keyval);
    const auto mods = event->state & gtk_accelerator_get_default_mod_mask();
    if (host->tabbed && (mods == GDK_CONTROL_MASK || mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
        auto* app = gtk_window_get_application(GTK_WINDOW(host->shell));
        if (key == GDK_KEY_t && mods == GDK_CONTROL_MASK) { showDocument(app, nullptr, {}, host->shell); return TRUE; }
        if (key == GDK_KEY_n && (mods & GDK_SHIFT_MASK)) { newDocumentWindow(host->shell); return TRUE; }
        if (key == GDK_KEY_F4) { closeDocument(*doc); return TRUE; }
        auto& docs = host->documents;
        if (key == GDK_KEY_Tab || key == GDK_KEY_ISO_Left_Tab) {
            const int index = int(std::find(docs.begin(), docs.end(), doc) - docs.begin());
            const int n = int(docs.size());
            selectDocument(*docs[(index + ((mods & GDK_SHIFT_MASK) ? -1 : 1) + n) % n]);
            return TRUE;
        }
        if (key >= GDK_KEY_1 && key <= GDK_KEY_9 && mods == GDK_CONTROL_MASK) {
            const size_t index = key - GDK_KEY_1;
            if (index < docs.size()) selectDocument(*docs[index]);
            return TRUE;
        }
    }
    if (doc->captionEditor && gtk_window_get_focus(GTK_WINDOW(host->shell)) == doc->captionEditor->view() &&
        doc->captionEditor->formatKey(*event)) return TRUE;
    if (doc->atomEntry && gtk_window_get_focus(GTK_WINDOW(host->shell)) == doc->atomEntry &&
        doc->atomLabelRuns.formatKey(*event)) return TRUE;
    if (doc->atomEntry || doc->captionEditor) {
        if (!(event->state & GDK_CONTROL_MASK) || key != GDK_KEY_s) return FALSE;
        doc->finishCanvasText();
    }
    if (doc->spectrumPage) {
        const auto* binding = linux_shortcuts::current().match(linux_shortcuts::chord(event->keyval, event->state),
                                                               linux_shortcuts::Scope::Window);
        if (!binding) return FALSE;
        if (binding->action == linux_shortcuts::Action::Close) { closeDocument(*doc); return TRUE; }
        if (binding->action == linux_shortcuts::Action::Open) { chooseDocuments(doc); return TRUE; }
        if (binding->action == linux_shortcuts::Action::New) {
            showDocument(gtk_window_get_application(GTK_WINDOW(host->shell)), nullptr, {}, host->shell);
            return TRUE;
        }
        return FALSE;
    }
    return doc->handleKey(*event);
}

static void showDocumentList(GtkButton* button, DocumentHost* host) {
    auto* menu = gtk_menu_new();
    gtk_menu_attach_to_widget(GTK_MENU(menu), GTK_WIDGET(button), nullptr);
    for (auto* doc : host->documents) {
        auto* item = gtk_check_menu_item_new_with_label(gtk_label_get_text(GTK_LABEL(doc->tabLabel)));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), doc == host->active);
        gtk_widget_set_tooltip_text(item, doc->session.sourcePath.c_str());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_signal_connect_object(item, "activate", G_CALLBACK(+[](GtkMenuItem*, GtkWidget* page) {
            auto* doc = static_cast<Window*>(g_object_get_data(G_OBJECT(page), "document-window"));
            if (doc) selectDocument(*doc);
        }), G_OBJECT(doc->page), G_CONNECT_DEFAULT);
    }
    g_signal_connect(menu, "selection-done", G_CALLBACK(+[](GtkMenuShell* menu, gpointer) {
        gtk_widget_destroy(GTK_WIDGET(menu));
    }), nullptr);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(button), GDK_GRAVITY_SOUTH_EAST, GDK_GRAVITY_NORTH_EAST, nullptr);
}

enum class DocumentButton { New, List, Close };
static GtkWidget* documentButton(DocumentButton type) {
    auto* button = gtk_button_new();
    auto* glyph = gtk_drawing_area_new();
    atk_object_set_role(gtk_widget_get_accessible(glyph), ATK_ROLE_REDUNDANT_OBJECT);
    gtk_widget_set_size_request(glyph, 16, 16);
    gtk_widget_set_halign(glyph, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(glyph, GTK_ALIGN_CENTER);
    g_signal_connect(glyph,"draw",G_CALLBACK(+[](GtkWidget* widget,cairo_t* cr,gpointer data)->gboolean {
        GdkRGBA ink{};
        gtk_style_context_get_color(gtk_widget_get_style_context(widget),gtk_widget_get_state_flags(widget),&ink);
        gdk_cairo_set_source_rgba(cr,&ink);
        const auto kind=static_cast<DocumentButton>(GPOINTER_TO_INT(data));
        cairo_set_line_width(cr,kind==DocumentButton::Close ? 1.4 : 1.5);
        if(kind==DocumentButton::New){
            cairo_move_to(cr,2,8); cairo_line_to(cr,14,8);
            cairo_move_to(cr,8,2); cairo_line_to(cr,8,14);
        }else if(kind==DocumentButton::List){
            cairo_move_to(cr,4,6); cairo_line_to(cr,8,10); cairo_line_to(cr,12,6);
        }else{
            cairo_move_to(cr,4,4); cairo_line_to(cr,12,12);
            cairo_move_to(cr,4,12); cairo_line_to(cr,12,4);
        }
        cairo_stroke(cr);
        return TRUE;
    }),GINT_TO_POINTER(static_cast<int>(type)));
    gtk_container_add(GTK_CONTAINER(button),glyph);
    return button;
}

static void bindDocumentMenu(GtkWidget* button, DocumentHost& host, Window* target = nullptr) {
    g_object_set_data(G_OBJECT(button), "document-menu-target", target);
    g_signal_connect(button, "button-press-event", G_CALLBACK(+[](GtkWidget* anchor, GdkEventButton* event, DocumentHost* host) -> gboolean {
        if (event->button != GDK_BUTTON_SECONDARY || event->type != GDK_BUTTON_PRESS) return FALSE;
        auto* target = static_cast<Window*>(g_object_get_data(G_OBJECT(anchor), "document-menu-target"));
        if (target) selectDocument(*target);
        auto* menu = gtk_menu_new();
        gtk_menu_attach_to_widget(GTK_MENU(menu), host->shell, nullptr);
        auto append = [&](const char* title, guint key, GdkModifierType mods, GCallback callback) {
            auto* item = gtk_menu_item_new_with_label(tr(title));
            gtk_accel_label_set_accel(GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(item))), key, mods);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            g_signal_connect_object(item, "activate", callback, G_OBJECT(host->shell), G_CONNECT_DEFAULT);
        };
        append("新建标签页", GDK_KEY_t, GDK_CONTROL_MASK, G_CALLBACK(+[](GtkMenuItem*, GtkWidget* shell) {
            showDocument(gtk_window_get_application(GTK_WINDOW(shell)), nullptr, {}, shell);
        }));
        append("新建窗口", GDK_KEY_n, GdkModifierType(GDK_CONTROL_MASK | GDK_SHIFT_MASK), G_CALLBACK(+[](GtkMenuItem*, GtkWidget* shell) {
            newDocumentWindow(shell);
        }));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        append("关闭当前标签页", GDK_KEY_w, GDK_CONTROL_MASK, G_CALLBACK(+[](GtkMenuItem*, GtkWidget* shell) {
            if (auto* doc = hostFor(shell)->active) closeDocument(*doc);
        }));
        g_signal_connect(menu, "selection-done", G_CALLBACK(+[](GtkMenuShell* menu, gpointer) {
            gtk_widget_destroy(GTK_WIDGET(menu));
        }), nullptr);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
        return TRUE;
    }), &host);
}

static void addDocumentTab(DocumentHost& host, Window& doc) {
    host.documents.push_back(&doc);
    if (!host.tabbed) return;
    doc.tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(doc.tab), "document-tab");
    auto* select = gtk_button_new();
    doc.tabLabel = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(doc.tabLabel), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(doc.tabLabel), 20);
    gtk_label_set_xalign(GTK_LABEL(doc.tabLabel), 0);
    gtk_widget_set_size_request(doc.tab, 120, -1);
    gtk_container_add(GTK_CONTAINER(select), doc.tabLabel);
    auto* close = documentButton(DocumentButton::Close);
    gtk_widget_set_tooltip_text(close, tr("关闭文档"));
    atk_object_set_name(gtk_widget_get_accessible(close), tr("关闭文档"));
    gtk_box_pack_start(GTK_BOX(doc.tab), select, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(doc.tab), close, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(host.strip), doc.tab, FALSE, FALSE, 0);
    bindDocumentMenu(select, host, &doc);
    bindDocumentMenu(close, host, &doc);
    g_signal_connect(select, "clicked", G_CALLBACK(+[](GtkButton*, Window* d){ selectDocument(*d); }), &doc);
    g_signal_connect(close, "clicked", G_CALLBACK(+[](GtkButton*, Window* d){ closeDocument(*d); }), &doc);
    g_signal_connect(doc.tab, "size-allocate", G_CALLBACK(+[](GtkWidget*, GtkAllocation*, Window* d){ revealTab(*d); }), &doc);
}
