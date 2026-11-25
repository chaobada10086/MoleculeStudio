#pragma once

#include "next/core/commands.hpp"
#include "text_tidy.hpp"
#include "caption_editor.hpp"

namespace linux_caption {
using namespace r62::core;

inline bool literal(const Document& document, ObjectId id) {
    if (id == kInvalidObjectId) return false;
    const auto* value = document.extensions(ObjectRef(ObjectKind::Text, id))
        .findValue("pst.attr.InterpretChemically");
    return value && *value == "no";
}

inline Error apply(Document& document, ObjectId id, const std::vector<TextRun>& runs,
                   const std::vector<uint32_t>& colors, bool automatic) {
    const ObjectRef ref(ObjectKind::Text, id);
    std::string plain;
    for (const auto& run : runs) plain += run.utf8;
    const auto formatted = automatic && !literal(document, id)
        ? texttidy::tidy(plain, true, runs, colors, false, true) : texttidy::Tidy{};
    const auto result = document.setTextRuns(id, formatted.changed ? formatted.runs : runs);
    if (!result.ok()) return result;
    auto& extensions = document.extensions(ref);
    extensions.erase(textbackup::key());
    if (formatted.changed)
        textbackup::save(document, ref, plain, runs, texttidy::writeColors(colors));
    if (!colors.empty()) {
        extensions.erase("studio.color.override");
        extensions.set("studio.text.colors", texttidy::writeColors(formatted.changed ? formatted.colors : colors));
        extensions.set("studio.text.colors.edited", "1");
    }
    return result;
}

inline Command edit(ObjectId id, Content content, bool automatic) {
    const ObjectRef ref(ObjectKind::Text, id);
    if (content.text.empty()) return commands::deleteObjects({ref});
    return Command("编辑文字", kInvalidateAppearance | kInvalidateGeometry,
        [id, content = std::move(content), automatic](CommandContext& context) {
            auto& document = context.document;
            auto error = apply(document, id, content.runs, content.colors, automatic);
            if (error.ok()) {
                auto format = document.texts().alive(id)->format;
                if (format.flags) {
                    format.flags = 0;
                    error = document.setTextFormat(id, format);
                }
            }
            return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
        });
}

inline Command add(Vec2 point, double size, Content content, bool automatic, ObjectId* created) {
    return Command("原位文字", kInvalidateStructure | kInvalidateAppearance | kInvalidateGeometry,
        [point, size, content = std::move(content), automatic, created](CommandContext& context) {
            auto& document = context.document;
            auto result = document.addText(point, size, content.text, created);
            if (result.ok()) result = apply(document, *created, content.runs, content.colors, automatic);
            return result.ok() ? CommandResult::success() : CommandResult::fail(result.code, result.message);
        });
}
}
