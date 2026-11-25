#pragma once

#include "caption_editor.hpp"
#include "engine/atom_runs.hpp"
#include "text_tidy.hpp"

namespace linux_caption {
// GtkEntryBuffer reports character offsets, while TextRun stores UTF-8 bytes.
class AtomLabelRuns {
    std::vector<TextRun> source_;
    std::vector<uint32_t> colors_;
    std::vector<size_t> characters_;
    guint deletedAt_ = G_MAXUINT;
    size_t deletedStyle_ = 0;
    GtkEntry* entry_ = nullptr;
    guint pendingAt_ = G_MAXUINT;
    size_t pendingStyle_ = 0;
    bool explicitRuns_ = false;
    bool changed_ = false;

    size_t styleAt(guint at) const {
        if (at == pendingAt_) return pendingStyle_;
        if (at == deletedAt_) return deletedStyle_;
        return characters_.empty() ? 0 : characters_[at ? at-1 : 0];
    }

    size_t withFlags(size_t index, uint16_t flags) {
        const auto base = source_[index];
        const auto color = texttidy::colorAt(colors_, index);
        for (size_t i = 0; i < source_.size(); ++i)
            if (source_[i].flags == flags && source_[i].size == base.size && source_[i].fontName == base.fontName &&
                texttidy::colorAt(colors_, i) == color) return i;
        source_.push_back(base);
        source_.back().flags = flags;
        if (!colors_.empty()) colors_.push_back(color);
        return source_.size()-1;
    }

    void showStyles() const {
        auto* list = pango_attr_list_new();
        const auto* text = gtk_entry_get_text(entry_);
        const auto* ptr = text;
        for (size_t i = 0; i < characters_.size() && *ptr;) {
            const auto index = characters_[i];
            const auto start = ptr;
            do { ptr = g_utf8_next_char(ptr); ++i; }
            while (i < characters_.size() && characters_[i] == index && *ptr);
            const auto add = [&](PangoAttribute* attr) {
                attr->start_index = guint(start-text);
                attr->end_index = guint(ptr-text);
                pango_attr_list_insert(list, attr);
            };
            const auto flags = source_[index].flags;
            if (flags & kBold) add(pango_attr_weight_new(PANGO_WEIGHT_BOLD));
            if (flags & kItalic) add(pango_attr_style_new(PANGO_STYLE_ITALIC));
            if (flags & kUnderline) add(pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
            if (flags & (kSubscript|kSuperscript)) {
                add(pango_attr_scale_new(.75));
                add(pango_attr_rise_new((flags & kSuperscript ? 4 : -3)*PANGO_SCALE));
            }
        }
        gtk_entry_set_attributes(entry_, list);
        pango_attr_list_unref(list);
    }
public:
    bool changed() const { return changed_; }
    void bind(GtkEntry* entry, const Document& doc, ObjectId id) {
        source_ = studioatom::runs(doc, id);
        colors_ = texttidy::readColors(doc, {ObjectKind::Atom, id});
        entry_ = entry;
        changed_ = false;
        pendingAt_ = G_MAXUINT;
        characters_.clear();
        deletedAt_ = G_MAXUINT;
        std::string plain;
        for (size_t i = 0; i < source_.size(); ++i) {
            plain += source_[i].utf8;
            characters_.insert(characters_.end(), g_utf8_strlen(source_[i].utf8.c_str(), -1), i);
        }
        if (plain != gtk_entry_get_text(entry) || characters_.empty()) {
            source_ = {TextRun{}};
            colors_.clear();
            characters_.assign(gtk_entry_get_text_length(entry), 0);
            explicitRuns_ = false;
        } else explicitRuns_ = true;
        if (!colors_.empty()) colors_.resize(source_.size(), colors_.back());
        showStyles();
        auto* buffer = gtk_entry_get_buffer(entry);
        g_signal_connect(buffer, "deleted-text", G_CALLBACK(+[](GtkEntryBuffer*, guint at, guint count, AtomLabelRuns* self) {
            auto& indices = self->characters_;
            if (at >= indices.size() || !count) return;
            self->pendingAt_ = G_MAXUINT;
            self->deletedAt_ = at;
            self->deletedStyle_ = indices[at];
            indices.erase(indices.begin()+at, indices.begin()+at+std::min<size_t>(count, indices.size()-at));
            self->showStyles();
        }), this);
        g_signal_connect(buffer, "inserted-text", G_CALLBACK(+[](GtkEntryBuffer*, guint at, const gchar*, guint count, AtomLabelRuns* self) {
            auto& indices = self->characters_;
            if (at > indices.size()) return;
            const auto style = self->styleAt(at);
            self->pendingAt_ = G_MAXUINT;
            indices.insert(indices.begin()+at, count, style);
            self->deletedAt_ = G_MAXUINT;
            self->showStyles();
        }), this);
    }

    bool formatKey(const GdkEventKey& event) {
        const auto flag = shortcutFlag(event);
        if (!flag) return false;
        int from, to;
        const bool selected = gtk_editable_get_selection_bounds(GTK_EDITABLE(entry_), &from, &to);
        if (!selected) from = to = gtk_editable_get_position(GTK_EDITABLE(entry_));
        if (from < 0 || to < from || size_t(to) > characters_.size()) return false;
        if (flag == kFormula) {
            if (!selected) { from = 0; to = int(characters_.size()); }
            const auto scripts = formulaScripts(gtk_entry_get_text(entry_), from, to);
            for (int i = from; i < to; ++i) {
                const auto index = characters_[i];
                const auto flags = (source_[index].flags & ~(kFormula|kSubscript|kSuperscript)) | scripts[i-from];
                if (flags != source_[index].flags) {
                    characters_[i] = withFlags(index, flags);
                    changed_ = true;
                }
            }
            pendingAt_ = G_MAXUINT;
            explicitRuns_ = true;
            showStyles();
            return true;
        }
        const auto active = selected
            ? std::all_of(characters_.begin()+from, characters_.begin()+to,
                [&](size_t i){ return (source_[i].flags & flag) != 0; })
            : (source_[styleAt(from)].flags & flag) != 0;
        const auto mask = flag & (kSuperscript|kSubscript|kFormula) ? kSuperscript|kSubscript|kFormula : flag;
        const auto toggle = [&](size_t i){ return withFlags(i, (source_[i].flags & ~mask) | (active ? 0 : flag)); };
        if (selected) {
            for (int i = from; i < to; ++i) characters_[i] = toggle(characters_[i]);
            changed_ = true;
            pendingAt_ = G_MAXUINT;
            showStyles();
        } else {
            pendingStyle_ = toggle(styleAt(from));
            pendingAt_ = from;
        }
        explicitRuns_ = true;
        return true;
    }

    Content capture(const std::string& text) const {
        Content result;
        if (!explicitRuns_ || characters_.empty() || characters_.size() != size_t(g_utf8_strlen(text.c_str(), -1))) return result;
        const char* at = text.c_str();
        size_t previous = source_.size();
        for (const auto index : characters_) {
            if (index != previous) {
                result.runs.push_back(source_[index]);
                result.runs.back().utf8.clear();
                if (!colors_.empty()) result.colors.push_back(texttidy::colorAt(colors_, index));
                previous = index;
            }
            const auto* next = g_utf8_next_char(at);
            result.runs.back().utf8.append(at, next-at);
            at = next;
        }
        return result;
    }
};
}
