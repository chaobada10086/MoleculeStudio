#pragma once

#include <gtk/gtk.h>

namespace linux_scrollbar {
// Native GTK steppers scroll a fraction of one step per animation frame.
// Canvas arrows and page clicks in the Windows editor advance on press.
class Press {
    GtkRange* range;
    guint timer = 0;
    int direction = 0;
    bool page = false;
    double position = 0;
    gint64 nextRepeat = 0;

    void advance() {
        if (page) {
            gint start, end;
            gtk_range_get_slider_range(range, &start, &end);
            if ((direction < 0 && position >= start) || (direction > 0 && position <= end))
                return;
        }
        auto* adj = gtk_range_get_adjustment(range);
        const double amount = page ? gtk_adjustment_get_page_increment(adj) : gtk_adjustment_get_step_increment(adj);
        gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + direction * amount);
    }
    void stop() {
        if (timer) g_source_remove(timer);
        timer = 0;
    }
    bool begin(const GdkEventButton& event) {
        if (event.button != GDK_BUTTON_PRIMARY || event.type != GDK_BUTTON_PRESS) return false;
        stop();
        auto* widget = GTK_WIDGET(range);
        const bool horizontal = gtk_orientable_get_orientation(GTK_ORIENTABLE(range)) == GTK_ORIENTATION_HORIZONTAL;
        const int length = horizontal ? gtk_widget_get_allocated_width(widget) : gtk_widget_get_allocated_height(widget);
        position = horizontal ? event.x : event.y;
        gint start, end;
        gtk_range_get_slider_range(range, &start, &end);
        if (position >= start && position < end) return false;
        direction = position < start ? -1 : 1;
        page = position >= 16 && position < length - 16;
        advance();
        nextRepeat = g_get_monotonic_time() + 400000;
        timer = g_timeout_add(50, +[](gpointer data) -> gboolean {
            auto* p = static_cast<Press*>(data);
            if (g_get_monotonic_time() >= p->nextRepeat) p->advance();
            return G_SOURCE_CONTINUE;
        }, this);
        return true;
    }
public:
    explicit Press(GtkRange* bar) : range(bar) {}
    ~Press() { stop(); }
    static void attach(GtkWidget* bar) {
        auto* press = new Press(GTK_RANGE(bar));
        g_object_set_data_full(G_OBJECT(bar), "canvas-scroll-press", press,
            +[](gpointer p) { delete static_cast<Press*>(p); });
        g_signal_connect(bar, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, Press* p) -> gboolean {
            return p->begin(*event);
        }), press);
        g_signal_connect(bar, "button-release-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, Press* p) -> gboolean {
            if (event->button == GDK_BUTTON_PRIMARY) p->stop();
            return FALSE;
        }), press);
        g_signal_connect(bar, "grab-broken-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, Press* p) -> gboolean {
            p->stop();
            return FALSE;
        }), press);
        g_signal_connect(bar, "unmap", G_CALLBACK(+[](GtkWidget*, Press* p) { p->stop(); }), press);
    }
};
}
