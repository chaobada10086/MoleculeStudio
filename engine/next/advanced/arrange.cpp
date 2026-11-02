#include "next/advanced/arrange.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace r62 {
namespace advanced {

namespace {

struct Block {
    Component   component;
    core::Box   box;
    core::Vec2  centre;
    bool        hasAnchor;
    bool        anchored;
    bool        leftOfArrow;
    Block() : hasAnchor(false), anchored(false), leftOfArrow(true) {}
};

} // namespace

core::Error planArrange(const LayoutInput& input, const ArrangeOptions& options,
                        ArrangePlan* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null ArrangePlan");
    *out = ArrangePlan();
    out->documentId = input.documentId;
    out->revision   = input.revision;

    const double gap = options.gap > 0.0 ? options.gap : 2.0 * input.defaultBondLength;
    if (!std::isfinite(gap) || gap < 0.0)
        return core::Error(core::ErrorCode::InvalidArgument, "invalid gap");

    std::vector<Component> comps = atomComponents(input);
    std::vector<Block> blocks;
    for (size_t c = 0; c < comps.size(); ++c) {
        Block b;
        b.component = comps[c];
        b.box = componentBounds(input, comps[c]);
        b.centre = componentCentroid(input, comps[c]);
        for (size_t i = 0; i < comps[c].atoms.size(); ++i) {
            const LayoutAtom* a = findAtom(input, comps[c].atoms[i]);
            if (!a) continue;
            if (a->locked || a->anchor) { b.hasAnchor = true; break; }
        }
        b.anchored = b.hasAnchor && options.keepAnchored;
        blocks.push_back(b);
    }
    out->arrowCount = static_cast<int>(input.arrows.size());

    // --- figure out the two groups when a reaction arrow is present ---------
    double arrowX = 0.0;
    bool haveArrow = false;
    if (options.reactionAware && !input.arrows.empty()) {
        // Use the leftmost arrow's midpoint: that is the anchor of the scheme.
        double best = 0.0;
        bool first = true;
        for (size_t i = 0; i < input.arrows.size(); ++i) {
            const double mid = (input.arrows[i].a.x + input.arrows[i].b.x) * 0.5;
            if (first || mid < best) { best = mid; first = false; }
        }
        arrowX = best;
        haveArrow = true;
    }
    for (size_t i = 0; i < blocks.size(); ++i)
        blocks[i].leftOfArrow = !haveArrow || blocks[i].centre.x <= arrowX;

    // --- place --------------------------------------------------------------
    // Anchored blocks are reference points; the free blocks are packed around
    // them without ever being laid on top of an anchored block.
    std::vector<bool> placed(blocks.size(), false);
    std::set<size_t> occupiedX;

    auto nextFreeCursor = [&](double cursor) { (void)occupiedX; return cursor; };

    // Left group first (reading order), then right group, matching a scheme.
    for (int group = 0; group < 2; ++group) {
        double cursorY = input.atoms.empty() ? options.origin.y : options.origin.y;
        double cursorX = options.origin.x;
        // Start the right group after the arrow plus a gap.
        if (group == 1 && haveArrow) cursorX = arrowX + gap;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (placed[i]) continue;
            if (blocks[i].leftOfArrow != (group == 0)) continue;
            const Block& b = blocks[i];
            if (b.anchored) {
                placed[i] = true;
                // Keep the anchored block exactly where it is. A following free
                // block starts after it.
                cursorX = std::max(cursorX, b.box.max.x + gap);
                continue;
            }
            const double width  = b.box.max.x - b.box.min.x;
            const double height = b.box.max.y - b.box.min.y;
            const double targetX = options.vertical ? b.box.min.x
                                                    : cursorX;
            const double targetY = options.vertical
                ? cursorY
                : options.origin.y - height * 0.5;
            const core::Vec2 delta(targetX - b.box.min.x, targetY - b.box.min.y);
            for (size_t k = 0; k < b.component.atoms.size(); ++k) {
                out->objects.push_back(core::ObjectRef(core::ObjectKind::Atom,
                                                       b.component.atoms[k]));
                out->deltas.push_back(delta);
            }
            out->components.push_back(b.component.atoms.empty()
                                          ? core::kInvalidObjectId
                                          : b.component.atoms.front());
            if (options.vertical) cursorY = b.box.max.y + gap;
            else                  cursorX += width + gap;
            placed[i] = true;
        }
        (void)nextFreeCursor;
    }
    for (size_t i = 0; i < blocks.size(); ++i)
        if (blocks[i].anchored) ++out->anchoredComponentCount;

    {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "components=%llu moved=%llu anchored=%d arrows=%d %s",
                      static_cast<unsigned long long>(blocks.size()),
                      static_cast<unsigned long long>(out->components.size()),
                      out->anchoredComponentCount, out->arrowCount,
                      haveArrow ? "(reaction-aware)" : "(plain row)");
        out->note = buf;
    }
    out->valid = true;
    return core::Error::success();
}

core::Error planArrange(const core::Document& doc, const ArrangeOptions& options,
                        ArrangePlan* out) {
    LayoutInput input;
    core::Error e = captureLayoutInput(doc, &input);
    if (!e.ok()) return e;
    return planArrange(input, options, out);
}

core::Command applyArrangeCommand(const ArrangePlan& plan, bool enforceFreshPlan) {
    ArrangePlan copy = plan;
    return core::Command("advanced.arrange", core::kInvalidateGeometry,
        [copy, enforceFreshPlan](core::CommandContext& ctx) -> core::CommandResult {
            core::Document& doc = ctx.document;
            if (!copy.valid)
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                 "arrange plan is not valid");
            if (doc.id() != copy.documentId)
                return core::CommandResult::fail(core::ErrorCode::DocumentMismatch,
                                                 "arrange plan belongs to another document");
            if (enforceFreshPlan && doc.revision() != copy.revision)
                return core::CommandResult::fail(core::ErrorCode::RevisionConflict,
                                                 "arrange plan is stale");
            for (size_t i = 0; i < copy.objects.size(); ++i) {
                const core::AtomRecord* a = doc.atoms().alive(copy.objects[i].id);
                if (!a) continue;
                core::Error e = doc.setAtomPosition(copy.objects[i].id,
                                                    a->position + copy.deltas[i]);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            }
            return core::CommandResult::success(core::kInvalidateGeometry);
        });
}

std::string describeArrangePlan(const ArrangePlan& plan) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "arrange[valid=%s objects=%llu %s]",
                  plan.valid ? "yes" : "no",
                  static_cast<unsigned long long>(plan.objects.size()),
                  plan.note.c_str());
    return std::string(buf);
}

} // namespace advanced
} // namespace r62
