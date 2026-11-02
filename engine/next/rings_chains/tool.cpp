// =============================================================================
// re_round62_viewer/next/rings_chains/tool.cpp
//
// T08 - core::Tool implementations for the ring and chain tools.
//
// NO KEYBOARD HANDLING. handleChar/handleKey are intentionally not overridden,
// so a character key falls through to T04's keymap instead of being swallowed.
// The only key state this file reads is the MODIFIER word that core::
// PointerState already carries, and it is used for exactly the documented Alt
// behaviour (release the fixed length/angle constraints, GUIDE pp.92-93).
//
// BOND STYLE COMES FROM T07. Ring and chain edges are written as plain single
// bonds unless the preset says otherwise, and the style axes live in T07's
// BondAxes. This module does not invent a second style vocabulary.
// =============================================================================

#include "next/rings_chains/tool.hpp"
#include "next/rings_chains/internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace r62 {
namespace rings_chains {

// -----------------------------------------------------------------------------
// RingTool
// -----------------------------------------------------------------------------
RingTool::RingTool()
    : id_("r62.tool.ring"), preset_(RingPresetId::Cyclohexane),
      pointer_(0, 0), pressed_(false), haveSecondAnchor_(false),
      secondAnchorBond_(core::kInvalidObjectId), anchorCaptured_(false),
      anchorAtom_(core::kInvalidObjectId), anchorBond_(core::kInvalidObjectId),
      anchorBond2_(core::kInvalidObjectId) {}

void RingTool::resetPerfCounters() { perf_.reset(); }

void RingTool::setHotspotsFrom(core::ToolHost& host, core::HitContext& hits,
                               const core::Vec2& documentPoint, double radiusDevice) {
    policy_.anchorBond = core::kInvalidObjectId;
    policy_.anchorBond2 = core::kInvalidObjectId;
    policy_.anchorAtom = core::kInvalidObjectId;
    // PRIORITY: an ATOM endpoint beats the BOND body it belongs to. An atom sits
    // exactly on every bond it terminates, so testing the bond first would make
    // "grow a ring from this atom" unreachable on any bonded atom, which is
    // almost all of them. The bond is only taken when no atom is within the
    // capture radius, i.e. the pointer is on the middle of an edge.
    // The atom hotspot must stay small enough that the MIDDLE of a bond is
    // still a bond.  With the full snap radius a 14.4 pt bond midpoint is
    // inside both endpoint circles, so an atom always won and fusion on a
    // benzene edge was unreachable at normal zoom.  Keep a generous bond
    // radius, but reserve atom capture for the immediate neighbourhood of an
    // atom; the original ChemDraw ring tool uses the same distinction.
    const double atomRadius = std::max(2.5, radiusDevice * 0.45);
    const core::ObjectId atom = hits.nearestAtom(documentPoint, atomRadius);
    if (atom != core::kInvalidObjectId) {
        policy_.anchorAtom = atom;
        return;
    }
    const core::ObjectId bond = hits.nearestBond(documentPoint, std::max(2.5, radiusDevice * 0.70));
    if (bond != core::kInvalidObjectId) policy_.anchorBond = bond;
    if (preset_ != RingPresetId::Benzene) return;

    // Peri/bay fusion.  A free 120-degree sector at a three-connected carbon is
    // the inside of a bay.  Completing the hexagon through that sector shares
    // both existing edges (A-B and B-C), which is exactly what turns naphthalene
    // into phenalene.  Existing-ring pairs produce a completion that lands on
    // existing atoms and are rejected by the overlap test below.
    const core::Document& doc = host.document();
    const double L = doc.defaultBondLength() > 0 ? doc.defaultBondLength() : 14.4;
    core::ObjectId bestB = core::kInvalidObjectId, bestA = core::kInvalidObjectId, bestC = core::kInvalidObjectId;
    double bestScore = 1e100;
    for (const auto& b : doc.atoms().records()) {
        if (!b.isAlive() || b.element != 6) continue;
        const std::vector<core::ObjectId>& incident = doc.incidentBonds(b.id);
        if (incident.size() != 3) continue;
        for (size_t i = 0; i < incident.size(); ++i) {
            const core::BondRecord* bi = doc.bonds().alive(incident[i]);
            if (!bi) continue;
            const core::ObjectId ai = bi->beginAtom == b.id ? bi->endAtom : bi->beginAtom;
            const core::AtomRecord* a = doc.atoms().alive(ai);
            if (!a) continue;
            for (size_t j = i + 1; j < incident.size(); ++j) {
                const core::BondRecord* bj = doc.bonds().alive(incident[j]);
                if (!bj) continue;
                if (bond != core::kInvalidObjectId && incident[i] != bond && incident[j] != bond) continue;
                const core::ObjectId ci = bj->beginAtom == b.id ? bj->endAtom : bj->beginAtom;
                const core::AtomRecord* c = doc.atoms().alive(ci);
                if (!c || ci == ai) continue;
                const core::Vec2 ba = a->position - b.position;
                const core::Vec2 bc = c->position - b.position;
                const double la = core::length(ba), lc = core::length(bc);
                if (la < L * 0.55 || lc < L * 0.55) continue;
                if (std::abs(la-lc) > std::max(la,lc)*0.10 || std::abs(core::dot(ba,bc)/(la*lc)+0.5)>0.12) continue;
                const core::Vec2 o = a->position + c->position - b.position;
                const core::Vec2 v = a->position - o;
                auto rotate = [](core::Vec2 q, double degrees) {
                    const double r = degrees * 3.14159265358979323846 / 180.0;
                    const double cs = std::cos(r), sn = std::sin(r);
                    return core::Vec2(q.x * cs - q.y * sn, q.x * sn + q.y * cs);
                };
                const core::Vec2 vb = b.position-o;
                const double turn = (v.x*vb.y-v.y*vb.x)>0 ? -60.0 : 60.0;
                const core::Vec2 fresh[3] = { o + rotate(v, turn), o + rotate(v, 2*turn), o + rotate(v, 3*turn) };
                // Reuse additional boundary vertices; reject completed rings.
                int occupied = 0;
                for (const core::Vec2& q : fresh) {
                    for (const auto& other : doc.atoms().records())
                        if (other.isAlive() && core::length(q-other.position) < la*.08) { ++occupied; break; }
                }
                if (occupied == 3) continue;
                const double score = core::length(documentPoint - o);
                if (score < (bond != core::kInvalidObjectId ? L*1.6 : L*0.65) && score < bestScore) {
                    bestScore = score; bestB = b.id; bestA = ai; bestC = ci;
                }
            }
        }
    }
    if (bestB != core::kInvalidObjectId) {
        policy_.anchorAtom = bestB;
        // Recover the two bond ids from the chosen pair.
        const std::vector<core::ObjectId>& incident = doc.incidentBonds(bestB);
        for (core::ObjectId id : incident) {
            const core::BondRecord* br = doc.bonds().alive(id);
            if (!br) continue;
            if ((br->beginAtom == bestA && br->endAtom == bestB) ||
                (br->endAtom == bestA && br->beginAtom == bestB)) policy_.anchorBond = id;
            if ((br->beginAtom == bestC && br->endAtom == bestB) ||
                (br->endAtom == bestC && br->beginAtom == bestB)) policy_.anchorBond2 = id;
        }
        if (policy_.anchorBond == core::kInvalidObjectId || policy_.anchorBond2 == core::kInvalidObjectId) {
            policy_.anchorAtom = core::kInvalidObjectId;
            policy_.anchorBond = policy_.anchorBond2 = core::kInvalidObjectId;
        }
    }
}

core::Error RingTool::resolve(core::ToolHost& host, const core::PointerState& pointer) {
    const core::Document& doc = host.document();
    policy_.modifiers = pointer.modifiers;
    policy_.bondLength = 0.0;
    policy_.fixedLength = host.fixedLength();
    policy_.fixedAngles = host.fixedAngles();
    policy_.angleStepDegrees = host.angleStepDegrees();

    // Alt on a RING placement releases the fixed-angle constraint by letting
    // the caller supply a rotation derived from the drag. GUIDE proves Alt
    // releases the constraints for a bond drag; applying the same release to a
    // ring rotation is OUR extension and is recorded as such in RESULTS.md.
    const bool alt = (pointer.modifiers & core::kModAlt) != 0u;
    if (alt && policy_.anchorAtom != core::kInvalidObjectId) {
        const core::AtomRecord* a = doc.atoms().alive(policy_.anchorAtom);
        if (a) {
            policy_.rotationDeg = core::angleOf(pointer.position - a->position);
            policy_.rotationGiven = true;
        }
    }

    // The pointer direction, used as the fallback for the no-neighbour case.
    if (policy_.anchorAtom != core::kInvalidObjectId) {
        const core::AtomRecord* a = doc.atoms().alive(policy_.anchorAtom);
        if (a) policy_.pointerDirectionDeg =
            core::angleOf(pointer.position - a->position);
    } else if (policy_.anchorBond != core::kInvalidObjectId) {
        const core::BondRecord* b = doc.bonds().alive(policy_.anchorBond);
        if (b) {
            const core::AtomRecord* a0 = doc.atoms().alive(b->beginAtom);
            const core::AtomRecord* a1 = doc.atoms().alive(b->endAtom);
            if (a0 && a1) policy_.pointerDirectionDeg =
                core::angleOf(pointer.position - (a0->position + a1->position) * 0.5);
        }
    }

    const RingRequest request = ringRequestFor(doc, preset_, pointer.position, policy_);
    ++perf_.placementsResolved;
    return resolveRingPlacement(doc, request, &placement_);
}

core::CommandResult RingTool::beginGesture(core::ToolHost& host,
                                           const core::PointerState& pointer,
                                           core::HitContext& hits) {
    pointer_ = pointer.position;
    pressed_ = true;
    haveSecondAnchor_ = false;
    secondAnchorBond_ = core::kInvalidObjectId;
    // THE ANCHOR IS CAPTURED ONCE, at PointerDown. That is the whole point of a
    // drag gesture: the user presses on an atom or a bond to say "grow from
    // HERE", and then drags away to choose the direction. Re-resolving the
    // hotspot on every move would lose the anchor the moment the pointer left
    // the 10 px capture radius, which is exactly the class of bug the T04
    // hysteresis was added to avoid.
    setHotspotsFrom(host, hits, pointer.position, host.snapRadiusDevice());
    anchorCaptured_ = (policy_.anchorAtom != core::kInvalidObjectId ||
                       policy_.anchorBond != core::kInvalidObjectId);
    anchorAtom_ = policy_.anchorAtom;
    anchorBond_ = policy_.anchorBond;
    anchorBond2_ = policy_.anchorBond2;
    // The hotspot was resolved by the platform and cached by updateGesture;
    // beginGesture refreshes it because the user may have moved without a move
    // event reaching us (the T04 stale-hotspot rule).
    const core::Error e = resolve(host, pointer);
    if (!e.ok()) {
        host.setStatus(e.message);
        status_ = e.message;
    } else {
        status_ = std::string("ring ") + ringTopologyClassName(placement_.geometry.topology);
    }
    return core::CommandResult::noChange();
}

core::CommandResult RingTool::updateGesture(core::ToolHost& host,
                                            const core::PointerState& pointer,
                                            core::HitContext& hits) {
    if (!pressed_) return core::CommandResult::noChange();
    pointer_ = pointer.position;
    // Hold the anchor captured at PointerDown. Only a gesture that STARTED on
    // empty canvas keeps looking, so a user who pressed on blank space and then
    // dragged onto an atom still gets a grow/fuse.
    if (anchorCaptured_) {
        policy_.anchorAtom = anchorAtom_;
        policy_.anchorBond = anchorBond_;
        policy_.anchorBond2 = anchorBond2_;
    } else {
        setHotspotsFrom(host, hits, pointer.position, host.snapRadiusDevice());
    }
    const core::Error e = resolve(host, pointer);

    // Publish the PREVIEW. It contains geometry derived from the SAME placement
    // value the commit will consume, so the picture and the result cannot
    // diverge. The document is NOT touched and NOTHING is added to it, so a
    // cancelled preview leaves no trace.
    core::PreviewScene& preview = host.preview();
    preview.clear();
    if (e.ok() && placement_.valid) {
        // Existing atoms the ring will reuse are HIDDEN from the preview so the
        // ghost does not draw them twice.
        for (size_t i = 0; i < placement_.reuseAtom.size(); ++i) {
            if (placement_.reuseAtom[i] == core::kInvalidObjectId) continue;
            preview.mutableHidden().push_back(
                core::ObjectRef(core::ObjectKind::Atom, placement_.reuseAtom[i]));
        }
        for (size_t e2 = 0; e2 < placement_.geometry.bonds.size(); ++e2) {
            if (placement_.reuseBond[e2] != core::kInvalidObjectId) continue;
            core::PreviewItem item;
            item.primitive = core::PreviewPrimitive::Segment;
            item.points.push_back(
                placement_.worldPosition[static_cast<size_t>(placement_.geometry.bonds[e2].from)]);
            item.points.push_back(
                placement_.worldPosition[static_cast<size_t>(placement_.geometry.bonds[e2].to)]);
            preview.add(item);
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "preview: %d new atom(s), %d new bond(s)",
                      placement_.createdAtomCount, placement_.createdBondCount);
        status_ = buf;
    } else {
        status_ = e.ok() ? placement_.rejectReason : e.message;
    }
    ++perf_.previewsPublished;
    return core::CommandResult::noChange();
}

core::CommandResult RingTool::endGesture(core::ToolHost& host,
                                         const core::PointerState& pointer,
                                         core::HitContext& hits) {
    if (!pressed_) return core::CommandResult::noChange();
    pressed_ = false;
    pointer_ = pointer.position;
    if (anchorCaptured_) {
        policy_.anchorAtom = anchorAtom_;
        policy_.anchorBond = anchorBond_;
        policy_.anchorBond2 = anchorBond2_;
    } else {
        setHotspotsFrom(host, hits, pointer.position, host.snapRadiusDevice());
    }
    // Re-resolve at the release position, then commit THAT value. A caller that
    // released without moving gets the same value the preview showed.
    const core::Error e = resolve(host, pointer);
    host.preview().clear();
    if (!e.ok() || !placement_.valid) {
        std::string why = e.ok() ? placement_.rejectReason : e.message;
        if (why.empty()) why = "ring placement is not valid";
        host.setStatus(why);
        status_ = why;
        return core::CommandResult::fail(core::ErrorCode::InvalidGeometry, why);
    }

    std::vector<core::ObjectId> atoms;
    std::vector<core::ObjectId> bonds;
    std::string notice;
    const core::Command cmd = commands::createRingCommand(placement_, &atoms, &bonds, &notice);
    const core::Error submitted = host.submit(cmd, "rings_chains.ring.create");
    if (!submitted.ok()) {
        host.setStatus(submitted.message);
        status_ = submitted.message;
        return core::CommandResult::fail(submitted.code, submitted.message);
    }
    committed_ = placement_;
    ++perf_.commits;
    status_ = notice.empty() ? "ring placed" : notice;
    if(!notice.empty())host.setStatus(notice);
    return core::CommandResult::success(core::kInvalidateStructure |
                                        core::kInvalidateGeometry |
                                        core::kInvalidateSemantics);
}

core::CommandResult RingTool::cancelGesture(core::ToolHost& host) {
    pressed_ = false;
    anchorCaptured_ = false;
    anchorAtom_ = core::kInvalidObjectId;
    anchorBond_ = core::kInvalidObjectId;
    anchorBond2_ = core::kInvalidObjectId;
    placement_ = RingPlacement();
    host.preview().clear();
    status_ = "cancelled";
    return core::CommandResult::success(core::kInvalidateSelection);
}

// -----------------------------------------------------------------------------
// ChainTool
// -----------------------------------------------------------------------------
ChainTool::ChainTool()
    : id_("r62.tool.chain"), preset_(ChainPresetId::Snaking),
      origin_(0, 0), pointer_(0, 0), pressed_(false) {}

void ChainTool::setPreset(ChainPresetId preset) {
    preset_ = preset;
    policy_.chainAtomCount = 1;
}

void ChainTool::resetPerfCounters() { perf_.reset(); }

core::Error ChainTool::resolve(core::ToolHost& host, const core::PointerState& pointer) {
    const core::Document& doc = host.document();
    policy_.modifiers = pointer.modifiers;
    policy_.fixedLength = host.fixedLength();
    policy_.fixedAngles = host.fixedAngles();
    policy_.angleStepDegrees = host.angleStepDegrees();

    // The chain length follows the drag distance divided by the bond length when
    // the user drags a long way; one atom per bond length is the natural
    // mapping and is OUR choice (the guide does not state the mapping).
    double span = core::length(pointer.position - origin_);
    if (!policy_.anchorAtom && !policy_.fixedLength) span = 0.0;
    const double bondLength = detail::resolvedBondLength(doc, policy_.bondLength);
    int count = 1;
    if (bondLength > 0.0 && std::isfinite(bondLength) && span > 0.0) {
        count = static_cast<int>(std::floor(span / bondLength));
        if (count < 1) count = 1;
        if (count > 64) count = 64;   // preview guard; the plan has its own cap
    }
    policy_.chainAtomCount = count;

    ChainRequest request = chainRequestFor(doc, origin_, pointer.position, policy_);
    request.options.snaking = (preset_ == ChainPresetId::Snaking);
    if (policy_.anchorAtom != core::kInvalidObjectId) {
        const core::AtomRecord* a = doc.atoms().alive(policy_.anchorAtom);
        if (a) request.origin = a->position;
    }
    ++perf_.plansResolved;
    return resolveChainPlan(doc, request, &plan_);
}

core::CommandResult ChainTool::beginGesture(core::ToolHost& host,
                                            const core::PointerState& pointer,
                                            core::HitContext& hits) {
    pressed_ = true;
    origin_ = pointer.position;
    policy_.anchorAtom = core::kInvalidObjectId;
    policy_.anchorBond = core::kInvalidObjectId;
    const core::ObjectId atom = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    if (atom != core::kInvalidObjectId) {
        policy_.anchorAtom = atom;
        const core::AtomRecord* a = host.document().atoms().alive(atom);
        if (a) origin_ = a->position;
    }
    return core::CommandResult::noChange();
}

core::CommandResult ChainTool::updateGesture(core::ToolHost& host,
                                             const core::PointerState& pointer,
                                             core::HitContext& hits) {
    (void)hits;
    if (!pressed_) return core::CommandResult::noChange();
    pointer_ = pointer.position;
    const core::Error e = resolve(host, pointer);

    core::PreviewScene& preview = host.preview();
    preview.clear();
    if (e.ok() && plan_.valid) {
        core::Vec2 cursor = plan_.origin;
        for (size_t i = 0; i < plan_.atoms.size(); ++i) {
            core::PreviewItem item;
            item.primitive = core::PreviewPrimitive::Segment;
            item.points.push_back(cursor);
            item.points.push_back(plan_.atoms[i].position);
            preview.add(item);
            cursor = plan_.atoms[i].position;
        }
        status_ = "chain preview";
    } else {
        status_ = e.ok() ? plan_.rejectReason : e.message;
    }
    ++perf_.previewsPublished;
    return core::CommandResult::noChange();
}

core::CommandResult ChainTool::endGesture(core::ToolHost& host,
                                          const core::PointerState& pointer,
                                          core::HitContext& hits) {
    (void)hits;
    if (!pressed_) return core::CommandResult::noChange();
    pressed_ = false;
    pointer_ = pointer.position;
    const core::Error e = resolve(host, pointer);
    host.preview().clear();
    if (!e.ok() || !plan_.valid) {
        std::string why = e.ok() ? plan_.rejectReason : e.message;
        if (why.empty()) why = "chain plan is not valid";
        host.setStatus(why);
        status_ = why;
        return core::CommandResult::fail(core::ErrorCode::InvalidGeometry, why);
    }
    std::vector<core::ObjectId> atoms;
    std::vector<core::ObjectId> bonds;
    const core::Command cmd = commands::createChainCommand(plan_, &atoms, &bonds);
    const core::Error submitted = host.submit(cmd, "rings_chains.chain.create");
    if (!submitted.ok()) {
        host.setStatus(submitted.message);
        status_ = submitted.message;
        return core::CommandResult::fail(submitted.code, submitted.message);
    }
    committed_ = plan_;
    ++perf_.commits;
    status_ = "chain placed";
    return core::CommandResult::success(core::kInvalidateStructure |
                                        core::kInvalidateGeometry);
}

core::CommandResult ChainTool::cancelGesture(core::ToolHost& host) {
    pressed_ = false;
    plan_ = ChainPlan();
    host.preview().clear();
    status_ = "cancelled";
    return core::CommandResult::success(core::kInvalidateSelection);
}

// -----------------------------------------------------------------------------
// Registration
// -----------------------------------------------------------------------------
core::Error registerTools(core::ToolRegistry& registry,
                          RingTool* ringTool, ChainTool* chainTool,
                          RegistryResult* out) {
    if (out) *out = RegistryResult();
    if (!ringTool || !chainTool)
        return core::Error(core::ErrorCode::InvalidArgument, "tool instances must not be null");

    const std::vector<RingPresetSpec>& rings = ringPresets();
    for (size_t i = 0; i < rings.size(); ++i) {
        core::ToolPreset p;
        p.id            = rings[i].toolId;
        p.behavior      = core::ToolBehavior::PlaceRings;
        p.target        = core::PresetTarget::Atom;
        p.displayName   = rings[i].displayName;
        p.category      = rings[i].category;
        p.sourceCommand = rings[i].command;
        p.optionKey     = rings[i].toolId;
        p.invalidation  = core::kInvalidateStructure | core::kInvalidateGeometry |
                          core::kInvalidateSemantics;
        p.implemented   = rings[i].implemented;
        uint32_t assigned = 0;
        const core::Error e = registry.registerPreset(p, &assigned);
        if (!e.ok()) return e;
        if (out) ++out->presetsRegistered;
    }
    const std::vector<ChainPresetSpec>& chains = chainPresets();
    for (size_t i = 0; i < chains.size(); ++i) {
        core::ToolPreset p;
        p.id            = chains[i].toolId;
        p.behavior      = core::ToolBehavior::PlaceChain;
        p.target        = core::PresetTarget::Atom;
        p.displayName   = chains[i].displayName;
        p.category      = chains[i].category;
        p.sourceCommand = chains[i].command;
        p.optionKey     = chains[i].toolId;
        p.invalidation  = core::kInvalidateStructure | core::kInvalidateGeometry |
                          core::kInvalidateSemantics;
        p.implemented   = chains[i].implemented;
        uint32_t assigned = 0;
        const core::Error e = registry.registerPreset(p, &assigned);
        if (!e.ok()) return e;
        if (out) ++out->presetsRegistered;
    }

    core::Error e = registry.registerTool(core::ToolBehavior::PlaceRings, ringTool);
    if (!e.ok()) return e;
    if (out) ++out->toolsRegistered;
    e = registry.registerTool(core::ToolBehavior::PlaceChain, chainTool);
    if (!e.ok()) return e;
    if (out) ++out->toolsRegistered;

    uint32_t next = core::platform_command::ToolBand;
    for (size_t i = 0; i < rings.size(); ++i) {
        const core::Error be = registry.bindPlatformCommand(rings[i].toolId, next++);
        if (!be.ok()) return be;
    }
    for (size_t i = 0; i < chains.size(); ++i) {
        const core::Error be = registry.bindPlatformCommand(chains[i].toolId, next++);
        if (!be.ok()) return be;
    }
    if (out) out->actionsRegistered = actionBindings().size();
    return core::Error::success();
}

// -----------------------------------------------------------------------------
// actionBindings - DATA, not a switch
// -----------------------------------------------------------------------------
// The action vocabulary is T04's. The key that reaches it is T04's business.
//
// DELIBERATELY ABSENT, and why (this is the honest part):
//   * generic-canvas 6 / 7 / 4 / 5 / 8 / 9 -> the verified excerpt says the
//     GENERIC table does NOT list 6 and explicitly says not to confirm or enable
//     "empty canvas 6 = six ring". Those sizes therefore arrive by TOOLBAR
//     command only, and this table does not claim a key for them.
//   * bond-hotspot a / z / 4..8 / 9 / 0 ARE verified and ARE listed; they fuse
//     onto the hotspot BOND, which core::ActionRequest already carries.
//   * atom-hotspot ring sprouting (v / u / 6 / 7) is T06's chemistry plus this
//     module's direction rule and is NOT claimed as a key binding here, because
//     the review of that path lives in T06 and the direction heuristic below is
//     explicitly PROJECT.
const std::vector<ActionBinding>& actionBindings() {
    static std::vector<ActionBinding> table;
    if (!table.empty()) return table;

    struct Row {
        interaction::HotkeyAction action;
        const char* sourceCommand;
        RingPresetId ring;
        ChainPresetId chain;
        bool isChain;
        const char* note;
    };
    static const Row rows[] = {
        { interaction::HotkeyAction::FuseBenzene, "chooseRingTool_Benzene",
          RingPresetId::Benzene, ChainPresetId::Acyclic, false,
          "bond hotspot a: fuse a benzene on the hot bond" },
        { interaction::HotkeyAction::FuseCyclopentadiene,
          "chooseRingTool_Cyclopentadiene",
          RingPresetId::Cyclopentadiene, ChainPresetId::Acyclic, false,
          "bond hotspot z: fuse a cyclopentadiene on the hot bond" },
        { interaction::HotkeyAction::FuseRing, "",
          RingPresetId::Cyclopentane, ChainPresetId::Acyclic, false,
          "bond hotspot 4/5/6/7/8: fused ring of that size (binding parameter)" },
        { interaction::HotkeyAction::ChairRingOrientation, "",
          RingPresetId::Chair1, ChainPresetId::Acyclic, false,
          "bond hotspot 9/0: the two chair orientations (binding parameter)" },
        { interaction::HotkeyAction::ActivateBenzeneTool, "chooseRingTool_Benzene",
          RingPresetId::Benzene, ChainPresetId::Acyclic, false,
          "generic j activates the benzene tool" },
        { interaction::HotkeyAction::ActivateCyclopentadieneTool,
          "chooseRingTool_Cyclopentadiene",
          RingPresetId::Cyclopentadiene, ChainPresetId::Acyclic, false,
          "generic Shift+j activates the cyclopentadiene tool" },
        { interaction::HotkeyAction::ActivateChainTool,
          "chooseAlkaneTool_Snaking_Chain",
          RingPresetId::Cyclohexane, ChainPresetId::Snaking, true,
          "generic Shift+x activates the (snaking) chain tool" }
    };
    const size_t n = sizeof(rows) / sizeof(rows[0]);
    for (size_t i = 0; i < n; ++i) {
        ActionBinding b;
        b.action = rows[i].action;
        b.sourceCommand = rows[i].sourceCommand;
        b.ring = rows[i].ring;
        b.chain = rows[i].chain;
        b.isChain = rows[i].isChain;
        b.note = rows[i].note;
        table.push_back(b);
    }
    return table;
}

const ActionBinding* bindingFor(interaction::HotkeyAction action) {
    const std::vector<ActionBinding>& t = actionBindings();
    for (size_t i = 0; i < t.size(); ++i) if (t[i].action == action) return &t[i];
    return 0;
}

// -----------------------------------------------------------------------------
// handleAction - the T04 seam. This function NEVER inspects a keystroke.
// -----------------------------------------------------------------------------
interaction::RouterOutcome handleAction(void* userData,
                                        const interaction::ActionRequest& request) {
    ActionState* state = static_cast<ActionState*>(userData);
    if (!state || !state->ringTool || !state->chainTool)
        return interaction::RouterOutcome::Unsupported;
    if (!bindingFor(request.action)) return interaction::RouterOutcome::NotHandled;

    using interaction::HotkeyAction;
    switch (request.action) {
        case HotkeyAction::FuseBenzene:
            state->ringTool->setPreset(RingPresetId::Benzene);
            state->ringMode = true;
            state->lastStatus = "benzene fuse armed on the hot bond";
            return interaction::RouterOutcome::Handled;
        case HotkeyAction::FuseCyclopentadiene:
            state->ringTool->setPreset(RingPresetId::Cyclopentadiene);
            state->ringMode = true;
            state->lastStatus = "cyclopentadiene fuse armed on the hot bond";
            return interaction::RouterOutcome::Handled;
        case HotkeyAction::FuseRing: {
            const int size = request.binding ? request.binding->parameter : 0;
            RingPresetId preset;
            switch (size) {
                case 3: preset = RingPresetId::Cyclopropane; break;
                case 4: preset = RingPresetId::Cyclobutane; break;
                case 5: preset = RingPresetId::Cyclopentane; break;
                case 6: preset = RingPresetId::Cyclohexane; break;
                case 7: preset = RingPresetId::Cycloheptane; break;
                case 8: preset = RingPresetId::Cyclooctane; break;
                default:
                    state->lastStatus = "fused ring size is not a 3..8 ring";
                    return interaction::RouterOutcome::Unsupported;
            }
            state->ringTool->setPreset(preset);
            state->ringMode = true;
            state->lastStatus = "fused ring armed on the hot bond";
            return interaction::RouterOutcome::Handled;
        }
        case HotkeyAction::ChairRingOrientation: {
            const int which = request.binding ? request.binding->parameter : 1;
            state->ringTool->setPreset(which == 2 ? RingPresetId::Chair2
                                                  : RingPresetId::Chair1);
            state->ringMode = true;
            state->lastStatus = "chair orientation armed";
            return interaction::RouterOutcome::Handled;
        }
        case HotkeyAction::ActivateBenzeneTool:
            state->ringTool->setPreset(RingPresetId::Benzene);
            state->ringMode = true;
            state->lastStatus = "benzene tool";
            return interaction::RouterOutcome::Handled;
        case HotkeyAction::ActivateCyclopentadieneTool:
            state->ringTool->setPreset(RingPresetId::Cyclopentadiene);
            state->ringMode = true;
            state->lastStatus = "cyclopentadiene tool";
            return interaction::RouterOutcome::Handled;
        case HotkeyAction::ActivateChainTool:
            state->chainTool->setPreset(ChainPresetId::Snaking);
            state->ringMode = false;
            state->lastStatus = "chain tool";
            return interaction::RouterOutcome::Handled;
        default:
            break;
    }
    return interaction::RouterOutcome::NotHandled;
}

size_t registerActionHandlers(interaction::CommandRouter& router, ActionState* state) {
    const std::vector<ActionBinding>& t2 = actionBindings();
    size_t n = 0;
    for (size_t i = 0; i < t2.size(); ++i) {
        router.registerHandler(t2[i].action, &handleAction, state);
        ++n;
    }
    return n;
}

} // namespace rings_chains
} // namespace r62