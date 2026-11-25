#pragma once
#include "font_families.hpp"

#include "i18n.hpp"
#include "shortcuts.hpp"
#include "next/core/document.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <sstream>

namespace linux_caption {
using namespace r62::core;

inline uint16_t shortcutFlag(const GdkEventKey& event) {
    using namespace linux_shortcuts;
    const auto value = chord(event.keyval, event.state);
    const auto* binding = current().match(value, Scope::Text);
    if (!binding) binding = current().match(value, Scope::Window);
    if (!binding) return 0;
    switch (binding->action) {
        case Action::Bold: return kBold;
        case Action::Italic: return kItalic;
        case Action::Underline: return kUnderline;
        case Action::TextScript: return binding->argument;
        default: return 0;
    }
}

inline std::vector<uint16_t> formulaScripts(const char* text, int from, int to) {
    std::vector<uint16_t> scripts;
    scripts.reserve(to-from);
    auto* p = g_utf8_offset_to_pointer(text, from);
    bool afterAtom = false, rIndex = false, inDigits = false;
    for (int i = from; i < to; ++i, p = g_utf8_next_char(p)) {
        const auto ch = g_utf8_get_char(p);
        if (ch >= '0' && ch <= '9') {
            if (!inDigits) {
                const auto* previous = g_utf8_find_prev_char(text, p);
                const auto* before = previous ? g_utf8_find_prev_char(text, previous) : nullptr;
                rIndex = previous && *previous == 'R' && (!before || !g_unichar_isalpha(g_utf8_get_char(before)));
            }
            scripts.push_back(afterAtom ? (rIndex ? kSuperscript : kSubscript) : 0);
            inDigits = true;
        } else {
            scripts.push_back(0);
            afterAtom = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == ')' || ch == ']';
            inDigits = false;
        }
    }
    return scripts;
}

struct Content {
    std::string text;
    std::vector<TextRun> runs;
    std::vector<uint32_t> colors;
};

class Editor {
    struct Style {
        TextRun run;
        uint32_t color = 255;

        bool operator==(const Style& other) const {
            return run.fontName == other.run.fontName && run.size == other.run.size &&
                run.flags == other.run.flags && color == other.color;
        }
    };
    struct Span {
        Style style;
        GtkTextTag* tag;
    };
    struct Range {
        int start, end;
        Style style;
    };

    GtkWidget* view_ = gtk_text_view_new();
    GtkTextBuffer* buffer_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view_));
    GtkTextMark* pendingMark_ = nullptr;
    int savedStart_ = -1, savedEnd_ = -1;
    GtkFontChooser* font_ = nullptr;
    GtkColorChooser* color_ = nullptr;
    Style fallback_, pending_;
    std::string family_;
    Content original_;
    GtkWidget* controls_ = nullptr;
    double displayScale_ = 1.;
    double points_ = 0;
    std::list<Span> styles_;
    bool hasColors_ = false, styled_ = false;

    static GdkRGBA unpack(uint32_t color) {
        return {(color >> 24) / 255., ((color >> 16) & 255) / 255.,
                ((color >> 8) & 255) / 255., (color & 255) / 255.};
    }

    static uint32_t pack(const GdkRGBA& color) {
        const auto byte = [](double value) { return uint32_t(std::lround(value * 255)); };
        return (byte(color.red) << 24) | (byte(color.green) << 16) | (byte(color.blue) << 8) | byte(color.alpha);
    }

    GtkTextTag* tagFor(const Style& style) {
        for (const auto& span : styles_)
            if (span.style == style) return span.tag;
        const auto& run = style.run;
        const auto family = linux_font::families(run.fontName.empty() ? family_ : run.fontName);
        const auto points = run.size > 0 ? run.size : points_;
        const auto foreground = unpack(style.color);
        const auto script = run.flags & kSubscript ? -1 : run.flags & kSuperscript ? 1 : 0;
        auto* tag = gtk_text_buffer_create_tag(buffer_, nullptr, "family", family.c_str(),
            "size-points", points * displayScale_, "weight", run.flags & kBold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
            "style", run.flags & kItalic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL,
            "underline", run.flags & kUnderline ? PANGO_UNDERLINE_SINGLE : PANGO_UNDERLINE_NONE,
            "foreground-rgba", &foreground, "scale", script ? .75 : 1.,
            "rise", int(points * displayScale_ * (script > 0 ? .33 : script < 0 ? -.22 : 0.) * PANGO_SCALE), nullptr);
        styles_.push_back({style, tag});
        g_object_set_data(G_OBJECT(tag), "molecule-caption-style", &styles_.back().style);
        return tag;
    }

    Style styleAt(const GtkTextIter& at) const {
        auto* tags = gtk_text_iter_get_tags(&at);
        const Style* style = nullptr;
        for (auto* row = tags; row; row = row->next)
            if (auto* found = static_cast<Style*>(g_object_get_data(G_OBJECT(row->data), "molecule-caption-style"))) {
                style = found;
                break;
            }
        g_slist_free(tags);
        return style ? *style : fallback_;
    }

    Style caretStyle(GtkTextIter at) const {
        if (pendingMark_) {
            GtkTextIter mark;
            gtk_text_buffer_get_iter_at_mark(buffer_, &mark, pendingMark_);
            if (gtk_text_iter_equal(&at, &mark)) return pending_;
        }
        if (gtk_text_iter_get_offset(&at) > 0) gtk_text_iter_backward_char(&at);
        return styleAt(at);
    }

    std::vector<Range> ranges(GtkTextIter at, const GtkTextIter& end) const {
        std::vector<Range> result;
        while (gtk_text_iter_compare(&at, &end) < 0) {
            GtkTextIter next = at;
            if (!gtk_text_iter_forward_to_tag_toggle(&next, nullptr) || gtk_text_iter_compare(&next, &end) > 0)
                next = end;
            result.push_back({gtk_text_iter_get_offset(&at), gtk_text_iter_get_offset(&next), styleAt(at)});
            at = next;
        }
        return result;
    }

    void assign(const GtkTextIter& start, const GtkTextIter& end, const Style& style) {
        gtk_text_buffer_remove_all_tags(buffer_, &start, &end);
        gtk_text_buffer_apply_tag(buffer_, tagFor(style), &start, &end);
    }

    void refreshControls() {
        GtkTextIter at, end;
        const bool selection = gtk_text_buffer_get_selection_bounds(buffer_, &at, &end);
        const auto style = selection ? styleAt(at) : caretStyle(at);
        const auto& family = style.run.fontName.empty() ? family_ : style.run.fontName;
        auto* description = pango_font_description_new();
        pango_font_description_set_family(description, family.c_str());
        pango_font_description_set_size(description, int((style.run.size > 0 ? style.run.size : points_) * PANGO_SCALE));
        gtk_font_chooser_set_font_desc(font_, description);
        pango_font_description_free(description);
        const auto rgba = unpack(style.color);
        gtk_color_chooser_set_rgba(color_, &rgba);
    }

    void update(const std::function<void(Style&)>& change) {
        GtkTextIter start, end;
        if (gtk_text_buffer_get_selection_bounds(buffer_, &start, &end)) {
            for (const auto& range : ranges(start, end)) {
                auto style = range.style;
                change(style);
                if (style == range.style) continue;
                GtkTextIter a; GtkTextIter b;
                gtk_text_buffer_get_iter_at_offset(buffer_, &a, range.start);
                gtk_text_buffer_get_iter_at_offset(buffer_, &b, range.end);
                assign(a, b, style);
                styled_ = true;
            }
        } else {
            const auto style = caretStyle(start);
            pending_ = style;
            change(pending_);
            if (pendingMark_) gtk_text_buffer_move_mark(buffer_, pendingMark_, &start);
            else pendingMark_ = gtk_text_buffer_create_mark(buffer_, nullptr, &start, FALSE);
        }
        refreshControls();
        gtk_widget_grab_focus(view_);
    }

    void applyFormula() {
        GtkTextIter start, end;
        if (!gtk_text_buffer_get_selection_bounds(buffer_, &start, &end)) gtk_text_buffer_get_bounds(buffer_, &start, &end);
        const int from = gtk_text_iter_get_offset(&start), to = gtk_text_iter_get_offset(&end);
        const auto scripts = formulaScripts(capture().text.c_str(), from, to);
        if (pendingMark_) {
            gtk_text_buffer_delete_mark(buffer_, pendingMark_);
            pendingMark_ = nullptr;
        }
        for (const auto& range : ranges(start, end)) {
            for (int at = range.start; at < range.end;) {
                const auto script = scripts[at-from];
                int stop = at+1;
                while (stop < range.end && scripts[stop-from] == script) ++stop;
                auto style = range.style;
                style.run.flags = (style.run.flags & ~(kFormula|kSubscript|kSuperscript)) | script;
                if (!(style == range.style)) {
                    GtkTextIter a; GtkTextIter b;
                    gtk_text_buffer_get_iter_at_offset(buffer_, &a, at);
                    gtk_text_buffer_get_iter_at_offset(buffer_, &b, stop);
                    assign(a, b, style);
                    styled_ = true;
                }
                at = stop;
            }
        }
        refreshControls();
        gtk_widget_grab_focus(view_);
    }

    void toggle(uint16_t flag) {
        if (flag == kFormula) { applyFormula(); return; }
        GtkTextIter start, end;
        bool active;
        if (gtk_text_buffer_get_selection_bounds(buffer_, &start, &end)) {
            const auto marked = ranges(start, end);
            active = std::all_of(marked.begin(), marked.end(), [flag](const Range& range) {
                return (range.style.run.flags & flag) != 0;
            });
        } else active = (caretStyle(start).run.flags & flag) != 0;
        update([=](Style& style) {
            const uint16_t clear = flag & (kSubscript | kSuperscript | kFormula) ? kSubscript | kSuperscript | kFormula : flag;
            style.run.flags &= ~clear;
            if (!active) style.run.flags |= flag;
        });
    }

public:
    void setTypography(const std::string& family, double points) {
        if (savedStart_ >= 0) {
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_offset(buffer_, &start, savedStart_);
            gtk_text_buffer_get_iter_at_offset(buffer_, &end, savedEnd_);
            gtk_text_buffer_select_range(buffer_, &start, &end);
        }
        update([&](Style& style) {
            if (!family.empty()) style.run.fontName = family;
            if (points > 0.) style.run.size = points;
        });
    }

    std::pair<std::string,double> typography() const {
        GtkTextIter start, end;
        const bool selected = gtk_text_buffer_get_selection_bounds(buffer_, &start, &end);
        const auto style = selected ? styleAt(start) : caretStyle(start);
        return {style.run.fontName.empty() ? family_ : style.run.fontName,
                style.run.size > 0. ? style.run.size : points_};
    }

    Editor(GtkWidget* content, const Document& document, ObjectId id, const std::string& typed, double scale, int width, const std::string& nextFamily, double nextSize) {
        auto* pango = gtk_widget_get_pango_context(view_);
        const double dpi = pango_cairo_context_get_resolution(pango);
        displayScale_ = scale * 72. / (dpi > 0 ? dpi : 96.);
        const auto* text = document.texts().alive(id);
        family_ = text && !text->format.fontName.empty() ? text->format.fontName : document.defaultFont();
        points_ = text && text->size > 0 ? text->size : document.defaultTextSize();
        if (text) {
            fallback_.run.flags = text->format.flags;
            fallback_.color = text->format.colorRGBA;
        } else {
            if (!nextFamily.empty()) family_ = nextFamily;
            if (nextSize > 0.) points_ = nextSize;
            fallback_.run.fontName = family_;
        }

        if (text) {
            const auto& extensions = document.extensions(ObjectRef(ObjectKind::Text, id));
            const auto* override = extensions.findValue("studio.color.override");
            if (override) {
                char* end = nullptr;
                auto color = g_ascii_strtoull(override->c_str(), &end, 10);
                if (*end == '\0' && end != override->c_str() && color <= G_MAXUINT32)
                    fallback_.color = uint32_t(color);
                else override = nullptr;
            }
            const auto* stored = extensions.findValue("studio.text.colors");
            std::istringstream colors(stored ? *stored : "");
            hasColors_ = stored && !stored->empty();
            for (const auto& original : document.runs(id)) {
                Style style = fallback_;
                style.run = original;
                style.run.flags |= text->format.flags;
                style.run.utf8.clear();
                uint32_t color;
                if (!override && colors >> color) style.color = color;
                GtkTextIter end;
                gtk_text_buffer_get_end_iter(buffer_, &end);
                gtk_text_buffer_insert_with_tags(buffer_, &end, original.utf8.c_str(), -1, tagFor(style), nullptr);
            }
            original_ = capture();
        }
        if (!typed.empty()) {
            gtk_text_buffer_set_text(buffer_, typed.c_str(), -1);
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(buffer_, &start, &end);
            assign(start, end, fallback_);
        }

        auto* tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        const std::pair<const char*, uint16_t> buttons[] = {{"粗体", kBold}, {"斜体", kItalic}, {"下划线", kUnderline},
            {"化学式", kFormula}, {"上标", kSuperscript}, {"下标", kSubscript}};
        for (const auto& entry : buttons) {
            auto* button = gtk_button_new_with_label(linux_i18n::tr(entry.first));
            gtk_widget_set_focus_on_click(button, FALSE);
            g_object_set_data(G_OBJECT(button), "caption-flag", GUINT_TO_POINTER(entry.second));
            g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkWidget* button, Editor* editor) {
                editor->toggle(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "caption-flag")));
            }), this);
            gtk_box_pack_start(GTK_BOX(tools), button, FALSE, FALSE, 0);
        }
        auto* font = gtk_font_button_new();
        font_ = GTK_FONT_CHOOSER(font);
        gtk_font_button_set_title(GTK_FONT_BUTTON(font), linux_i18n::tr("字体"));
        atk_object_set_name(gtk_widget_get_accessible(font), linux_i18n::tr("字体"));
        gtk_box_pack_start(GTK_BOX(tools), font, FALSE, FALSE, 0);
        g_signal_connect(font, "font-set", G_CALLBACK(+[](GtkFontChooser* widget, Editor* editor) {
            auto* description = gtk_font_chooser_get_font_desc(widget);
            if (!description) return;
            const auto* family = pango_font_description_get_family(description);
            const auto points = pango_font_description_get_size(description) / double(PANGO_SCALE);
            if (family && points > 0) editor->update([&](Style& style) {
                style.run.fontName = family;
                style.run.size = points;
            });
            pango_font_description_free(description);
        }), this);
        auto* color = gtk_color_button_new();
        color_ = GTK_COLOR_CHOOSER(color);
        gtk_color_button_set_title(GTK_COLOR_BUTTON(color), linux_i18n::tr("文字颜色"));
        atk_object_set_name(gtk_widget_get_accessible(color), linux_i18n::tr("文字颜色"));
        gtk_widget_set_tooltip_text(color, linux_i18n::tr("文字颜色"));
        gtk_box_pack_start(GTK_BOX(tools), color, FALSE, FALSE, 0);
        g_signal_connect(color, "color-set", G_CALLBACK(+[](GtkColorChooser* widget, Editor* editor) {
            GdkRGBA rgba;
            gtk_color_chooser_get_rgba(widget, &rgba);
            editor->update([&](Style& style) { style.color = pack(rgba); });
        }), this);
        auto* menu = gtk_menu_button_new();
        gtk_button_set_label(GTK_BUTTON(menu), linux_i18n::tr("文字格式"));
        auto* popover = gtk_popover_new(menu);
        gtk_container_add(GTK_CONTAINER(popover), tools);
        gtk_widget_show_all(tools);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu), popover);
        controls_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
        gtk_box_pack_start(GTK_BOX(controls_), menu, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(content), controls_, FALSE, FALSE, 0);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view_), GTK_WRAP_WORD_CHAR);
        const GtkJustification alignment[] = {GTK_JUSTIFY_LEFT, GTK_JUSTIFY_CENTER, GTK_JUSTIFY_RIGHT, GTK_JUSTIFY_FILL};
        if (text && text->format.align < G_N_ELEMENTS(alignment))
            gtk_text_view_set_justification(GTK_TEXT_VIEW(view_), alignment[text->format.align]);
        auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_size_request(scroll, width, -1);
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 24);
        gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 180);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
        gtk_container_add(GTK_CONTAINER(scroll), view_);
        gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
        g_signal_connect_after(buffer_, "insert-text", G_CALLBACK(+[](GtkTextBuffer*, GtkTextIter* at,
            gchar* text, gint length, Editor* editor) {
            GtkTextIter start = *at;
            gtk_text_iter_backward_chars(&start, gint(g_utf8_strlen(text, length)));
            auto style = editor->caretStyle(start);
            if (editor->pendingMark_) {
                GtkTextIter mark;
                gtk_text_buffer_get_iter_at_mark(editor->buffer_, &mark, editor->pendingMark_);
                if (gtk_text_iter_equal(&mark, at)) style = editor->pending_;
            }
            editor->assign(start, *at, style);
            editor->refreshControls();
        }), this);
        g_signal_connect_after(buffer_, "mark-set", G_CALLBACK(+[](GtkTextBuffer* buffer, GtkTextIter*, GtkTextMark* mark, Editor* editor) {
            if (mark == gtk_text_buffer_get_insert(buffer) || mark == gtk_text_buffer_get_selection_bound(buffer))
                editor->refreshControls();
        }), this);
        // Editing a ribbon entry can claim the X11 primary selection.
        // Remember the caption range before focus moves to the entry.
        g_signal_connect(view_, "focus-out-event", G_CALLBACK(+[](GtkWidget*, GdkEventFocus*, Editor* editor)->gboolean {
            GtkTextIter a; GtkTextIter b;
            const bool selected = gtk_text_buffer_get_selection_bounds(editor->buffer_, &a, &b);
            editor->savedStart_ = selected ? gtk_text_iter_get_offset(&a) : -1;
            editor->savedEnd_ = gtk_text_iter_get_offset(&b);
            return FALSE;
        }), this);
        g_signal_connect(view_, "focus-in-event", G_CALLBACK(+[](GtkWidget*, GdkEventFocus*, Editor* editor)->gboolean {
            editor->savedStart_ = -1;
            return FALSE;
        }), this);
        refreshControls();
    }

    bool formatKey(const GdkEventKey& event) {
        const auto flag = shortcutFlag(event);
        if (!flag) return false;
        toggle(flag);
        return true;
    }

    GtkWidget* controls() const { return controls_; }
    GtkWidget* view() const { return view_; }
    bool textChanged(const Content& content) const { return content.text != original_.text; }
    bool changed(const Content& content) const {
        if (textChanged(content)) return true;
        if (!styled_) return false;
        if (content.runs.size() != original_.runs.size() || content.colors != original_.colors) return true;
        return !std::equal(content.runs.begin(), content.runs.end(), original_.runs.begin(), [](const auto& a, const auto& b) {
            return a.utf8 == b.utf8 && a.fontName == b.fontName && a.size == b.size && a.flags == b.flags;
        });
    }

    Content capture() const {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer_, &start, &end);
        Content result;
        bool storeColors = hasColors_;
        for (const auto& range : ranges(start, end)) {
            GtkTextIter a; GtkTextIter b;
            gtk_text_buffer_get_iter_at_offset(buffer_, &a, range.start);
            gtk_text_buffer_get_iter_at_offset(buffer_, &b, range.end);
            auto run = range.style.run;
            gchar* text = gtk_text_buffer_get_text(buffer_, &a, &b, FALSE);
            run.utf8 = text;
            g_free(text);
            result.text += run.utf8;
            result.runs.push_back(std::move(run));
            result.colors.push_back(range.style.color);
            storeColors |= range.style.color != fallback_.color;
        }
        if (!storeColors) result.colors.clear();
        return result;
    }
};
}
