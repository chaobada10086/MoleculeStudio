#pragma once

namespace linux_nmr {
// A pane owns its result. The async callback owns the job until its pipes close.
struct Prediction {
    GtkWidget* pane;
    GtkWidget* status;
    GSubprocess* process;
    guint timeout = 0;
    gulong destroySignal = 0;

    static void start(GtkWidget* pane, const std::string& action, const std::string& input) {
        auto* status = gtk_label_new(tr("正在预测核磁位移…"));
        gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
        gtk_box_pack_start(GTK_BOX(pane), status, FALSE, FALSE, 8);
        GError* error = nullptr;
        auto flags = GSubprocessFlags(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
        auto* proc = g_subprocess_new(flags, &error, "/usr/bin/python3", conversionScript().c_str(),
                                      "text/xml", "text/x-moleculestudio-nmr", action.c_str(), nullptr);
        if (!proc) {
            gtk_label_set_text(GTK_LABEL(status), error->message);
            g_clear_error(&error);
            return;
        }
        auto* job = new Prediction{pane, status, proc};
        job->timeout = g_timeout_add_seconds(90, +[](gpointer data) -> gboolean {
            auto* job = static_cast<Prediction*>(data);
            job->timeout = 0;
            g_subprocess_force_exit(job->process);
            return G_SOURCE_REMOVE;
        }, job);
        job->destroySignal = g_signal_connect(pane, "destroy", G_CALLBACK(+[](GtkWidget*, Prediction* job) {
            job->pane = nullptr;
            g_subprocess_force_exit(job->process);
        }), job);
        g_subprocess_communicate_utf8_async(proc, input.c_str(), nullptr, +[](GObject*, GAsyncResult* res, gpointer data) {
            std::unique_ptr<Prediction> job(static_cast<Prediction*>(data));
            gchar *output = nullptr, *diagnostic = nullptr;
            GError* error = nullptr;
            const bool read = g_subprocess_communicate_utf8_finish(job->process, res, &output, &diagnostic, &error);
            if (job->timeout) g_source_remove(job->timeout);
            if (job->pane) {
                g_signal_handler_disconnect(job->pane, job->destroySignal);
                auto result = std::make_unique<Spectrum>();
                if (read && g_subprocess_get_successful(job->process) && output && result->read(output)) {
                    gtk_widget_destroy(job->status);
                    populate(GTK_BOX(job->pane), *result);
                    auto* copy = gtk_button_new_with_label(tr("复制峰表"));
                    gtk_widget_set_halign(copy, GTK_ALIGN_END);
                    gtk_box_pack_end(GTK_BOX(job->pane), copy, FALSE, FALSE, 4);
                    g_signal_connect(copy, "clicked", G_CALLBACK(+[](GtkButton*, Spectrum* spec) { copyPeaks(*spec); }), result.get());
                    g_object_set_data_full(G_OBJECT(job->pane), "spectrum", result.release(),
                        +[](gpointer p) { delete static_cast<Spectrum*>(p); });
                    gtk_widget_show_all(job->pane);
                } else {
                    const char* message = error ? error->message : diagnostic && *diagnostic ? diagnostic : tr("核磁预测失败");
                    gtk_label_set_text(GTK_LABEL(job->status), message);
                }
            }
            g_clear_error(&error);
            g_free(output);
            g_free(diagnostic);
            g_object_unref(job->process);
        }, job);
    }
};

struct PredictionPage {
    std::string snapshot;
    GtkWidget* book;

    void refresh(const char* solvent) {
        const int active = gtk_notebook_get_current_page(GTK_NOTEBOOK(book));
        while (auto* pane = gtk_notebook_get_nth_page(GTK_NOTEBOOK(book), 0)) gtk_widget_destroy(pane);
        for (auto nucleus : {std::make_pair("nmr1h","¹H"), {"nmr13c","¹³C"}, {"nmr19f","¹⁹F"}}) {
            auto* pane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_notebook_append_page(GTK_NOTEBOOK(book), pane, gtk_label_new(nucleus.second));
            Prediction::start(pane, std::string(nucleus.first) + ";solvent=" + solvent, snapshot);
        }
        gtk_widget_show_all(book);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(book), std::max(0, active));
    }
};
}

static bool openSpectrumTab(Window& source, const std::string& snapshot) {
    auto* host = hostFor(source.window);
    if (!host || !host->tabbed) return false;
    auto* doc = new Window();
    doc->spectrumPage = true;
    doc->window = source.window;
    doc->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    g_object_set_data_full(G_OBJECT(doc->page), "document-window", doc, +[](gpointer p) { delete static_cast<Window*>(p); });
    gtk_container_add(GTK_CONTAINER(host->stack), doc->page);
    addDocumentTab(*host, *doc);
    gchar* basename = source.session.sourcePath.empty() ? g_strdup(tr("未命名结构")) : g_filename_display_basename(source.session.sourcePath.c_str());
    const std::string name = std::string(tr("预测核磁")) + " · " + basename;
    g_free(basename);
    titleDocument(*doc, name + " — MoleculeStudio", name.c_str());
    auto* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(doc->page), toolbar, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_label_new(tr("溶剂")), FALSE, FALSE, 6);
    auto* solvent = linux_nmr::solventChooser();
    gtk_box_pack_start(GTK_BOX(toolbar), solvent, FALSE, FALSE, 0);
    auto* split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(doc->page), split, TRUE, TRUE, 0);
    doc->canvas = gtk_drawing_area_new();
    gtk_widget_set_can_focus(doc->canvas, TRUE);
    gtk_paned_pack1(GTK_PANED(split), doc->canvas, TRUE, TRUE);
    gtk_paned_set_position(GTK_PANED(split), 300);
    auto loaded = doc->session.openBytes(snapshot, "");
    doc->session.dirty = false;
    if (loaded.ok()) g_signal_connect(doc->canvas, "draw", G_CALLBACK((+[](GtkWidget* widget, cairo_t* cr, Window* doc) -> gboolean {
        auto& s = doc->session;
        const auto width = gtk_widget_get_allocated_width(widget), height = gtk_widget_get_allocated_height(widget);
        if (width < 45 || height < 45) return FALSE;
        if (s.renderView.widthPx != width || s.renderView.heightPx != height) {
            s.renderView.widthPx = width;
            s.renderView.heightPx = height;
            auto view = s.view();
            view.viewportCenter = {width * .5, height * .5};
            s.setView(view);
            s.fit();
        }
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        linux_cairo::draw(cr, s.scene, s.renderView, s.measurer.get());
        return TRUE;
    })), doc);
    auto* book = gtk_notebook_new();
    gtk_paned_pack2(GTK_PANED(split), book, TRUE, TRUE);
    auto* page = new linux_nmr::PredictionPage{snapshot, book};
    g_object_set_data_full(G_OBJECT(doc->page), "prediction", page, +[](gpointer p) { delete static_cast<linux_nmr::PredictionPage*>(p); });
    g_signal_connect(solvent, "changed", G_CALLBACK(+[](GtkComboBox* choice, linux_nmr::PredictionPage* p) {
        p->refresh(gtk_combo_box_get_active_id(choice));
    }), page);
    page->refresh(gtk_combo_box_get_active_id(GTK_COMBO_BOX(solvent)));
    gtk_widget_show_all(doc->page);
    gtk_widget_show_all(doc->tab);
    selectDocument(*doc);
    return true;
}
