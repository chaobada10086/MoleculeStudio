// =============================================================================
// re_round62_viewer/next/helm/layout.cpp
//
// T15 - sequence layout. View geometry only; see layout.hpp for the hard rule and
// for the guide pages each rule comes from.
// =============================================================================

#include "next/helm/layout.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace r62 {
namespace helm {

namespace {

bool isNucleic(const SequencePolymer& polymer) {
    return polymer.type == PolymerType::Rna;
}

// How many backbone slots a monomer consumes. A nucleobase is a BRANCH: it sits
// under its sugar and does not advance the main line, which is the guide rule
// 'ribose and phosphate form the main sequence and the base is positioned
// vertically under the ribose' (PDF page 194).
bool isBranchResidue(const MonomerInstance& instance) {
    return instance.definition.role == MonomerRole::Branch;
}

} // namespace

// ----------------------------------------------------------------------------
// Single polymer layout
// ----------------------------------------------------------------------------
void layoutPolymerFrom(const SequenceModel& model, size_t polymerIndex,
                       size_t fromPosition, const LayoutOptions& options,
                       const core::Vec2& origin, SequenceLayout* layout) {
    const SequencePolymer* polymer = model.polymer(polymerIndex);
    if (!polymer || !layout) return;
    if (options.residuePitch <= 0.0 || options.linePitch <= 0.0) return;

    // Re-walk from the start to know the column of `fromPosition`, but only WRITE
    // placements at or after it. Walking the prefix is O(prefix) arithmetic with
    // no allocation, and it is what keeps a local edit from rewriting the map for
    // every earlier residue.
    size_t column = 0;
    size_t line = 0;
    size_t lastBackboneColumn = 0;   // the column a branch shares with its backbone
    bool   placedBackbone = false;   // has any backbone residue been placed yet?
    for (size_t i = 0; i < polymer->monomers.size(); ++i) {
        const MonomerInstance& instance = polymer->monomers[i];
        if (!instance.alive) continue;
        if (instance.copyIndex < 0) continue;   // defensive; copyIndex is >= 0
        // A BRANCH does not consume a column and must sit UNDER THE COLUMN OF THE
        // BACKBONE RESIDUE IT FOLLOWS, not the next free column. Using the running
        // cursor offsets every nucleobase by one pitch, which looks plausible and is
        // wrong: the guide puts the base 'vertically under the ribose' (PDF p.194).
        // A branch shares its backbone's column. If NO backbone residue has been placed
        // yet, there is no column to share, and treating the whole polymer as one column
        // would stack a bare base sequence such as RNA1{A.U.G.C} on top of itself. A
        // branch then advances like a backbone instead.
        const bool branch = isBranchResidue(instance);
        const bool branchHasBackbone = branch && placedBackbone;
        const size_t slotColumn = branchHasBackbone ? lastBackboneColumn : column;
        if (i >= fromPosition) {
            MonomerLayout placement;
            placement.lineBreakBefore = (column == 0 && line > 0);
            if (isBranchResidue(instance)) {
                placement.position = core::Vec2(
                    origin.x + static_cast<double>(slotColumn) * options.residuePitch,
                    origin.y + static_cast<double>(line) * options.linePitch + options.branchDrop);
            } else {
                placement.position = core::Vec2(
                    origin.x + static_cast<double>(slotColumn) * options.residuePitch,
                    origin.y + static_cast<double>(line) * options.linePitch);
            }
            (*layout).placements[instance.id] = placement;
        }
        if (!branch) { lastBackboneColumn = slotColumn; placedBackbone = true; }
        // Advance the cursor. A branch does not consume a column WHEN it has a backbone
        // to sit under; otherwise it behaves like a backbone residue.
        if (!branchHasBackbone) {
            ++column;
            if (options.residuesPerLine != 0 && column >= options.residuesPerLine) {
                column = 0;
                ++line;
            }
        }
    }
}

void layoutPolymer(const SequenceModel& model, size_t polymerIndex,
                   const LayoutOptions& options, const core::Vec2& origin,
                   SequenceLayout* layout) {
    layoutPolymerFrom(model, polymerIndex, 0, options, origin, layout);
}

SequenceLayout layoutSequence(const SequenceModel& model, const LayoutOptions& options) {
    SequenceLayout layout;
    layout.residuePitch = options.residuePitch;
    layout.linePitch = options.linePitch;
    layout.computedFor = model.serial();
    for (size_t i = 0; i < model.polymerCount(); ++i) {
        const core::Vec2 origin(0.0, static_cast<double>(i) * options.polymerRowGap);
        layoutPolymer(model, i, options, origin, &layout);
    }
    return layout;
}

// ----------------------------------------------------------------------------
// Complementary strands
// ----------------------------------------------------------------------------
core::Error complementLayout(const SequenceModel& model, size_t senseIndex,
                            size_t antisenseIndex, const LayoutOptions& options,
                            SequenceLayout* layout, size_t* outPairsPlaced) {
    if (!layout) return core::Error(core::ErrorCode::InvalidArgument, "null layout");
    const SequencePolymer* sense = model.polymer(senseIndex);
    const SequencePolymer* antisense = model.polymer(antisenseIndex);
    if (!sense || !antisense)
        return core::Error(core::ErrorCode::OutOfRange, "polymer index out of range");
    if (senseIndex == antisenseIndex)
        return core::Error(core::ErrorCode::InvalidArgument,
                           "a polymer cannot be complementary to itself here; use a hairpin");
    if (!isNucleic(*sense) || !isNucleic(*antisense))
        return core::Error(core::ErrorCode::Unsupported,
                           "complementary layout applies to DNA/RNA strands only");

    // Collect the hydrogen connections that join these two polymers. A covalent
    // cross-link is NOT a base pair and must not move a strand.
    std::vector<std::pair<size_t, size_t> > pairs;   // (sense pos, antisense pos)
    for (size_t c = 0; c < model.connectionCount(); ++c) {
        const SequenceConnection* conn = model.connection(c);
        if (!conn || !conn->alive) continue;
        if (conn->kind != ConnectionKind::Hydrogen) continue;
        const bool forward = conn->from.polymer == senseIndex && conn->to.polymer == antisenseIndex;
        const bool reverse = conn->from.polymer == antisenseIndex && conn->to.polymer == senseIndex;
        if (!forward && !reverse) continue;
        const size_t sPos = forward ? conn->from.position : conn->to.position;
        const size_t aPos = forward ? conn->to.position : conn->from.position;
        pairs.push_back(std::make_pair(sPos, aPos));
    }
    if (pairs.empty())
        return core::Error(core::ErrorCode::Unsupported,
                           "no hydrogen bonds join these two strands, so there is nothing to align");
    std::sort(pairs.begin(), pairs.end());

    // Lay the sense strand out first, at the top.
    const core::Vec2 senseOrigin(0.0, 0.0);
    layoutPolymer(model, senseIndex, options, senseOrigin, layout);

    // Place the antisense residue of every pair directly BELOW its partner, and
    // interpolate the residues between pairs along the same column so the two
    // chains stay parallel. The antisense strand therefore runs in the opposite
    // direction, which is what makes the bases face each other.
    const double verticalGap = options.polymerRowGap;
    size_t placed = 0;
    for (size_t p = 0; p < pairs.size(); ++p) {
        const SequencePolymer* antisensePolymer = antisense;
        const MonomerInstance& senseResidue = sense->monomers[pairs[p].first];
        MonomerLayout sensePlacement;
        std::map<MonomerInstanceId, MonomerLayout>::const_iterator it =
            layout->placements.find(senseResidue.id);
        if (it == layout->placements.end()) continue;
        sensePlacement = it->second;

        // Walk outward from each pair so an unpaired neighbourhood still lands on
        // the same column. Positions are handled in a second pass below.
        const size_t aPos = pairs[p].second;
        if (aPos >= antisensePolymer->monomers.size()) continue;
        const MonomerInstance& antisenseResidue = antisensePolymer->monomers[aPos];
        if (!antisenseResidue.alive) continue;
        MonomerLayout placement;
        placement.position = core::Vec2(sensePlacement.position.x,
                                        sensePlacement.position.y + verticalGap);
        (*layout).placements[antisenseResidue.id] = placement;
        ++placed;
    }
   
    // Any antisense residue that is NOT part of a pair still needs a position, so
    // it is placed by extrapolating from its nearest pair along the antisense
    // direction (which is the reverse of the sense direction).
    for (size_t m = 0; m < antisense->monomers.size(); ++m) {
        const MonomerInstance& residue = antisense->monomers[m];
        if (!residue.alive) continue;
        if (layout->has(residue.id)) continue;
        // Find the nearest pair by |position difference| on the antisense index.
        size_t bestPair = 0;
        size_t bestDistance = static_cast<size_t>(-1);
        for (size_t p = 0; p < pairs.size(); ++p) {
            const size_t d = pairs[p].second > m ? pairs[p].second - m : m - pairs[p].second;
            if (d < bestDistance) { bestDistance = d; bestPair = p; }
        }
        const MonomerInstance& anchor = antisense->monomers[pairs[bestPair].second];
        std::map<MonomerInstanceId, MonomerLayout>::const_iterator anchorIt =
            layout->placements.find(anchor.id);
        if (anchorIt == layout->placements.end()) continue;
        const double delta = static_cast<double>(pairs[bestPair].second)
                             - static_cast<double>(m);
        MonomerLayout placement;
        placement.position = core::Vec2(
            anchorIt->second.position.x + delta * options.residuePitch,
            anchorIt->second.position.y);
        (*layout).placements[residue.id] = placement;
    }
    layout->residuePitch = options.residuePitch;
    layout->linePitch = options.linePitch;
    layout->computedFor = model.serial();
    if (outPairsPlaced) *outPairsPlaced = placed;
    return core::Error();
}

// ----------------------------------------------------------------------------
// Hairpin
// ----------------------------------------------------------------------------
HairpinDetection detectHairpin(const SequenceModel& model, size_t polymerIndex) {
    HairpinDetection detection;
    detection.polymerIndex = polymerIndex;
    const SequencePolymer* polymer = model.polymer(polymerIndex);
    if (!polymer) {
        detection.reason = "polymer index out of range";
        return detection;
    }
    if (!isNucleic(*polymer)) {
        detection.reason = "a hairpin is a DNA/RNA fold; this polymer is not nucleic";
        return detection;
    }
    std::vector<std::pair<size_t, size_t> > pairs;
    for (size_t c = 0; c < model.connectionCount(); ++c) {
        const SequenceConnection* conn = model.connection(c);
        if (!conn || !conn->alive) continue;
        if (conn->kind != ConnectionKind::Hydrogen) continue;
        if (conn->from.polymer != polymerIndex || conn->to.polymer != polymerIndex) continue;
        size_t a = conn->from.position;
        size_t b = conn->to.position;
        if (a == b) continue;
        if (a > b) std::swap(a, b);
        pairs.push_back(std::make_pair(a, b));
    }
    if (pairs.empty()) {
        detection.reason = "no intramolecular hydrogen bonds";
        return detection;
    }
    std::sort(pairs.begin(), pairs.end());
    // A hairpin's stem pairs are nested: (i,j) inside (i',j') implies i<i' and
    // j<j'. A crossing pair means a pseudoknot, which is a different shape and is
    // reported as such instead of being folded as a hairpin.
    for (size_t a = 0; a < pairs.size(); ++a) {
        for (size_t b = a + 1; b < pairs.size(); ++b) {
            const bool nested = pairs[a].first < pairs[b].first &&
                                pairs[b].second < pairs[a].second;
            if (!nested) {
                detection.reason = "base pairs cross; this is a pseudoknot, not a hairpin";
                return detection;
            }
        }
    }
    // The loop is the run between the innermost pair.
    const size_t innerFrom = pairs[pairs.size() - 1].first;
    const size_t innerTo = pairs[pairs.size() - 1].second;
    detection.isHairpin = true;
    detection.pairCount = pairs.size();
    detection.stemPairs = pairs;
    detection.loopStart = innerFrom + 1;
    detection.loopEnd = innerTo;   // exclusive: innerTo is paired to innerFrom
    return detection;
}

core::Error layoutHairpin(const SequenceModel& model, const HairpinDetection& detection,
                          const LayoutOptions& options, SequenceLayout* layout) {
    if (!layout) return core::Error(core::ErrorCode::InvalidArgument, "null layout");
    if (!detection.isHairpin)
        return core::Error(core::ErrorCode::Unsupported,
                           detection.reason.empty() ? "not a hairpin" : detection.reason);
    const SequencePolymer* polymer = model.polymer(detection.polymerIndex);
    if (!polymer) return core::Error(core::ErrorCode::OutOfRange, "polymer index out of range");

    // Fold: the 5' arm of the stem runs left to right on the top row, the 3' arm
    // runs right to left on the bottom row, and the loop bridges them at the
    // right-hand end (guide PDF page 195).
    const size_t pairCount = detection.stemPairs.size();
    for (size_t p = 0; p < pairCount; ++p) {
        const size_t leftPos = detection.stemPairs[p].first;
        const size_t rightPos = detection.stemPairs[p].second;
        if (leftPos >= polymer->monomers.size() || rightPos >= polymer->monomers.size()) continue;
        const MonomerInstance& leftResidue = polymer->monomers[leftPos];
        const MonomerInstance& rightResidue = polymer->monomers[rightPos];
        if (!leftResidue.alive || !rightResidue.alive) continue;
        MonomerLayout left;
        left.position = core::Vec2(static_cast<double>(p) * options.residuePitch, 0.0);
        (*layout).placements[leftResidue.id] = left;
        MonomerLayout right;
        right.position = core::Vec2(static_cast<double>(p) * options.residuePitch,
                                    options.polymerRowGap);
        (*layout).placements[rightResidue.id] = right;
    }
    // The loop arcs from the end of the 5' arm, around the right, back to the end
    // of the 3' arm.
    const size_t loopLength = detection.loopEnd > detection.loopStart
                                  ? detection.loopEnd - detection.loopStart : 0;
    const double loopRadius = static_cast<double>(loopLength + 1) * options.residuePitch / 2.0;
    const double loopCenterX = static_cast<double>(pairCount) * options.residuePitch
                              + loopRadius * 0.5;
    const double loopCenterY = options.polymerRowGap / 2.0;
    for (size_t i = 0; i < loopLength; ++i) {
        const size_t pos = detection.loopStart + i;
        if (pos >= polymer->monomers.size()) break;
        const MonomerInstance& residue = polymer->monomers[pos];
        if (!residue.alive) continue;
        const double theta = 3.14159265358979323846 *
                             (static_cast<double>(i) + 1.0)
                             / (static_cast<double>(loopLength) + 1.0);
        MonomerLayout placement;
        placement.position = core::Vec2(loopCenterX - loopRadius * std::cos(theta),
                                        loopCenterY - loopRadius * std::sin(theta) * 0.5);
        (*layout).placements[residue.id] = placement;
    }
    // Anything still unplaced (e.g. a residue outside the stem/loop span) gets a
    // straight-run position so the layout is total.
    for (size_t m = 0; m < polymer->monomers.size(); ++m) {
        const MonomerInstance& residue = polymer->monomers[m];
        if (!residue.alive) continue;
        if (layout->has(residue.id)) continue;
        MonomerLayout placement;
        placement.position = core::Vec2(static_cast<double>(m) * options.residuePitch,
                                        -options.polymerRowGap);
        (*layout).placements[residue.id] = placement;
    }
    layout->residuePitch = options.residuePitch;
    layout->linePitch = options.linePitch;
    layout->computedFor = model.serial();
    return core::Error();
}

// ----------------------------------------------------------------------------
// Bounds
// ----------------------------------------------------------------------------
core::Box layoutBounds(const SequenceLayout& layout, double radius) {
    core::Box box;
    bool first = true;
    for (std::map<MonomerInstanceId, MonomerLayout>::const_iterator it =
             layout.placements.begin(); it != layout.placements.end(); ++it) {
        const core::Vec2 p = it->second.position;
        if (first) {
            box.min = core::Vec2(p.x - radius, p.y - radius);
            box.max = core::Vec2(p.x + radius, p.y + radius);
            first = false;
            continue;
        }
        if (p.x - radius < box.min.x) box.min.x = p.x - radius;
        if (p.y - radius < box.min.y) box.min.y = p.y - radius;
        if (p.x + radius > box.max.x) box.max.x = p.x + radius;
        if (p.y + radius > box.max.y) box.max.y = p.y + radius;
    }
    return box;
}

} // namespace helm
} // namespace r62
