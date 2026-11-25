#pragma once

#include "next/arrows_curves/model.hpp"
#include <vector>

// The transient scene repeatedly reads the same arrow styles while the pointer moves.
// Endpoints from the annotation or attached atoms are still resolved by readArrow.
namespace r62::arrows {
class PreviewArrowCache {
    struct Entry {
        std::string source;
        ArrowParams params;
    };
    std::vector<Entry> slots_;
    inline static thread_local PreviewArrowCache* current_ = nullptr;
public:
    class Scope {
        PreviewArrowCache* previous_;
    public:
        explicit Scope(PreviewArrowCache& cache) : previous_(current_) { current_ = &cache; }
        ~Scope() { current_ = previous_; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
    void clear() { slots_.clear(); }
    static ArrowParams decode(core::ObjectId id, const std::string& text, bool* parsed) {
        if (!current_ || text.empty() || text.size() > 4096) return decodeArrowParams(text,parsed);
        auto& slots = current_->slots_;
        if (slots.empty()) slots.resize(256);
        auto& entry = slots[id % slots.size()];
        if (entry.source == text) {
            *parsed = true;
            return entry.params;
        }
        auto params = decodeArrowParams(text,parsed);
        if (*parsed) {
            entry.params = params;
            entry.source = text;
        }
        return params;
    }
};
}
