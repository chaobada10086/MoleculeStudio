#pragma once

#include "i18n.hpp"
#include <gtk/gtk.h>
#include <string>
#include <vector>

namespace linux_worker {
inline std::string run(GtkWindow* owner, const char* title, const std::vector<std::string>& arguments,
                       const std::string& input, std::string& failure, guint seconds,
                       bool protocolErrors = false, bool* wasCancelled = nullptr) {
    if (wasCancelled) *wasCancelled = false;
    std::vector<const gchar*> argv;
    for (const auto& argument : arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    GError* error = nullptr;
    auto* process = g_subprocess_newv(argv.data(), GSubprocessFlags(G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE), &error);
    if (!process) {
        failure = error->message;
        g_clear_error(&error);
        return {};
    }
    auto* dialog = owner ? gtk_message_dialog_new(owner, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
        GTK_BUTTONS_NONE, "%s", linux_i18n::tr(title)) : nullptr;
    if (dialog) gtk_dialog_add_button(GTK_DIALOG(dialog), linux_i18n::tr("取消"), GTK_RESPONSE_CANCEL);
    struct Worker {
        GtkDialog* dialog;
        gchar* result = nullptr;
        gchar* diagnostic = nullptr;
        GError* error = nullptr;
        bool finished = false;
    } job{dialog ? GTK_DIALOG(dialog) : nullptr};
    // Commands using the worker without a dialog may be inside a document transaction.
    // Dispatch only this worker's events while waiting for its result.
    auto* context = dialog ? nullptr : g_main_context_new();
    if (context) g_main_context_push_thread_default(context);
    g_subprocess_communicate_utf8_async(process, input.c_str(), nullptr,
        +[](GObject* source, GAsyncResult* async, gpointer context){
            auto& job = *static_cast<Worker*>(context);
            g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), async, &job.result, &job.diagnostic, &job.error);
            job.finished = true;
            if (job.dialog) gtk_dialog_response(job.dialog, GTK_RESPONSE_OK);
        }, &job);
    auto* timeout = g_timeout_source_new_seconds(seconds);
    g_source_set_callback(timeout, +[](gpointer process) -> gboolean {
        g_subprocess_force_exit(G_SUBPROCESS(process));
        return G_SOURCE_REMOVE;
    }, process, nullptr);
    g_source_attach(timeout, context);
    bool cancelled = false;
    if (dialog) {
        gtk_widget_show_all(dialog);
        cancelled = gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK;
    } else {
        while (!job.finished) g_main_context_iteration(context, TRUE);
        g_main_context_pop_thread_default(context);
    }
    if (cancelled && !job.finished) {
        g_subprocess_force_exit(process);
        while (!job.finished) g_main_context_iteration(nullptr, TRUE);
    }
    g_source_destroy(timeout);
    g_source_unref(timeout);
    if (context) g_main_context_unref(context);
    if (wasCancelled) *wasCancelled = cancelled;
    std::string output;
    if (!cancelled) {
        if (!job.error && job.result && (g_subprocess_get_successful(process) || (protocolErrors && *job.result)))
            output = job.result;
        else failure = job.error ? job.error->message :
                       job.diagnostic && *job.diagnostic ? job.diagnostic : "计算未完成或已超时";
    }
    if (dialog) gtk_widget_destroy(dialog);
    g_clear_error(&job.error);
    g_free(job.result);
    g_free(job.diagnostic);
    g_object_unref(process);
    return output;
}
}
