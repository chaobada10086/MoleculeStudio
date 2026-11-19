#ifndef R62_NEXT_ADVANCED_ARRANGE_HPP
#define R62_NEXT_ADVANCED_ARRANGE_HPP
// =============================================================================
// next/advanced/arrange.hpp  --  T17 structure / reaction arrangement
//
// The "structure cleanup" family in ChemDraw has three separate jobs and this
// module keeps them separate:
//
//   1. GEOMETRY cleanup   (cleanup.hpp)  - move atoms within a structure.
//   2. COMPONENT layout   (here)         - place whole disconnected fragments
//                                          relative to each other.
//   3. REACTION layout    (here)         - when arrow objects exist, place the
//                                          components that read as reactants and
//                                          products on the correct side, with a
//                                          gap for the arrow.
//
// Nothing here is chemistry. It never invents a reagent, never writes a caption
// and never reorders a reaction: it moves rigid components and leaves every
// element, bond axis, label and annotation untouched.
// =============================================================================

#include "next/advanced/common.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace advanced {

struct ArrangeOptions {
    bool       reactionAware;   // use arrow objects to split left/right groups
    bool       vertical;        // stack components vertically instead of a row
    bool       keepAnchored;    // a component with a lock/anchor stays put
    double     gap;             // 0 == 2.0 * document bond length
    core::Vec2 origin;          // the baseline the first free component starts at
    ArrangeOptions()
        : reactionAware(true), vertical(false), keepAnchored(true), gap(0.0),
          origin(0.0, 0.0) {}
};

struct ArrangePlan {
    bool        valid;
    core::Error error;
    core::DocumentId documentId;
    uint64_t    revision;
    std::vector<core::ObjectRef> objects;   // atoms that move
    std::vector<core::Vec2>      deltas;    // delta per object
    std::vector<core::ObjectId>  components; // first atom of each component, in layout order
    int         anchoredComponentCount;
    int         arrowCount;
    std::string note;
    ArrangePlan() : valid(false), documentId(0), revision(0),
                    anchoredComponentCount(0), arrowCount(0) {}
};

core::Error planArrange(const LayoutInput& input, const ArrangeOptions& options,
                        ArrangePlan* out);
core::Error planArrange(const core::Document& doc, const ArrangeOptions& options,
                        ArrangePlan* out);

core::Command applyArrangeCommand(const ArrangePlan& plan, bool enforceFreshPlan = true);

std::string describeArrangePlan(const ArrangePlan& plan);

} // namespace advanced
} // namespace r62

#endif // R62_NEXT_ADVANCED_ARRANGE_HPP
