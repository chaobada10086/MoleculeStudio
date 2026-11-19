// =============================================================================
// re_round62_viewer/next/bonds/edit.cpp
//
// T07 - bond edit commands. See edit.hpp for the contract.
//
// Every command body is a small lambda over core::CommandContext. It uses ONLY
// Document mutators, so the recording session sees every touched id and one undo
// step restores the exact prior state.
// =============================================================================

#include "next/bonds/edit.hpp"

#include "next/core/document.hpp"

#include <sstream>

namespace r62 {
namespace bonds {

const char* const kSideExtensionKey = "user.bonds.doubleSide";

namespace {

core::Error requireBond(core::Document& doc, core::ObjectId id) {
    const core::BondRecord* b = doc.bonds().get(id);
    if (!b) return core::Error(core::ErrorCode::OutOfRange, "bond id out of range");
    if (!b->isAlive()) return core::Error(core::ErrorCode::NotAlive, "bond is deleted");
    return core::Error::success();
}

std::string nameFor(const char* verb, size_t count) {
    std::ostringstream os;
    os << "bond." << verb << "[" << count << "]";
    return os.str();
}

// Write the side placement AND notify the recording session. Document::extensions()
// (non-const) deliberately does not notify, because most callers mutate without a
// transaction open; a command MUST notify or undo cannot restore the old value.
void setSideOnBag(core::Document& doc, core::ObjectId bondId, DoubleSide side) {
    const core::ObjectRef ref(core::ObjectKind::Bond, bondId);
    doc.extensions(ref).set(kSideExtensionKey, doubleSideName(side));
    core::DocumentSession* session = core::documentSession(&doc);
    if (session) session->noteExtensions(ref, false);
}

// core::CommandResult has no Error constructor (its factories are success /
// noChange / fail), so one adapter keeps every call site uniform.
core::CommandResult fromError(const core::Error& e) {
    core::CommandResult r;
    r.error = e;
    r.invalidation = core::kInvalidateNone;
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Side placement persistence
// ---------------------------------------------------------------------------
DoubleSide sideOf(const core::ExtensionBag& bag) {
    const std::string* v = bag.findValue(kSideExtensionKey);
    if (!v) return DoubleSide::Symmetric;
    if (*v == "left") return DoubleSide::Left;
    if (*v == "right") return DoubleSide::Right;
    if (*v == "centered") return DoubleSide::Centered;
    return DoubleSide::Symmetric;
}

// ---------------------------------------------------------------------------
// addBondCommand
// ---------------------------------------------------------------------------
core::Command addBondCommand(core::ObjectId begin, core::ObjectId end,
                             const BondAxes& axes, core::ObjectId* outBondId) {
    const BondAxes axesCopy = axes;
    core::ObjectId* out = outBondId;
    return core::Command("bond.add",
        core::kInvalidateStructure | core::kInvalidateSemantics,
        [begin, end, axesCopy, out](core::CommandContext& ctx) -> core::CommandResult {
            core::Document& doc = ctx.document;
            const core::Error v = validateAxes(axesCopy);
            if (!v.ok()) return fromError(v);
            if (begin == end)
                return core::CommandResult::fail(core::ErrorCode::SelfBond,
                                                 "a bond cannot connect an atom to itself");
            if (!doc.atoms().exists(begin) || !doc.atoms().exists(end))
                return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                 "bond endpoint atom is missing or deleted");
            if (doc.bondBetween(begin, end) != core::kInvalidObjectId)
                return core::CommandResult::fail(core::ErrorCode::DuplicateBond,
                                                 "a bond already connects these atoms");
            core::BondRecord rec;
            rec.beginAtom = begin;
            rec.endAtom = end;
            rec.order = axesCopy.order;
            rec.display = axesCopy.display;
            rec.semantics = axesCopy.semantics;
            rec.stereo = axesCopy.stereo;
            core::ObjectId id = core::kInvalidObjectId;
            core::Error r = doc.addBondRecord(rec, &id);
            if (!r.ok()) return fromError(r);
            if (axesCopy.side != DoubleSide::Symmetric)
                setSideOnBag(doc, id, axesCopy.side);
            if (out) *out = id;
            return core::CommandResult::success();
        });
}

// ---------------------------------------------------------------------------
// addBondFromSnapCommand
// ---------------------------------------------------------------------------
// Both endpoints are final. A free endpoint creates a carbon (or the requested
// element); a joined endpoint reuses the existing atom at its STORED coordinate.
// Everything happens in one command, so one undo removes the bond and any atom
// it introduced - the "one user action, one history entry" rule.
core::Command addBondFromSnapCommand(const BondEndpoint& origin,
                                     const interaction::SnapResult& snap,
                                     const BondAxes& axes,
                                     int newAtomElement,
                                     core::ObjectId* outBondId) {
    const BondEndpoint originCopy = origin;
    const BondAxes axesCopy = axes;
    const interaction::SnapResult endCopy = snap;
    const int element = (newAtomElement >= 1 && newAtomElement <= 118) ? newAtomElement : 6;
    core::ObjectId* out = outBondId;
    return core::Command("bond.addFromSnap",
        core::kInvalidateStructure | core::kInvalidateSemantics | core::kInvalidateGeometry,
        [originCopy, endCopy, axesCopy, element, out](
            core::CommandContext& ctx) -> core::CommandResult {
            core::Document& doc = ctx.document;
            if (!endCopy.valid)
                return core::CommandResult::fail(core::ErrorCode::InvalidGeometry,
                                                 "snap result is invalid; nothing to commit");
            const core::Error v = validateAxes(axesCopy);
            if (!v.ok()) return fromError(v);

            core::ObjectId beginId = core::kInvalidObjectId;
            if (originCopy.joined && originCopy.target.kind == core::ObjectKind::Atom) {
                const core::AtomRecord* a = doc.atoms().alive(originCopy.target.id);
                if (!a) return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                         "origin atom is deleted");
                beginId = originCopy.target.id;
            } else {
                core::Error r = doc.addAtom(originCopy.point, element, &beginId);
                if (!r.ok()) return fromError(r);
            }

            core::ObjectId endId = core::kInvalidObjectId;
            if (endCopy.joined && endCopy.target.kind == core::ObjectKind::Atom) {
                const core::AtomRecord* b = doc.atoms().alive(endCopy.target.id);
                if (!b) return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                         "snap target atom is deleted");
                endId = endCopy.target.id;
                // A join must use the atom stored coordinate. A different point
                // means the snap was re-resolved, which is the defect this API
                // is shaped to prevent.
                if (!(b->position == endCopy.endpoint))
                    return core::CommandResult::fail(core::ErrorCode::InvalidGeometry,
                        "snap endpoint disagrees with the joined atom coordinate");
            } else {
                core::Error r = doc.addAtom(endCopy.endpoint, element, &endId);
                if (!r.ok()) return fromError(r);
            }

            if (beginId == endId)
                return core::CommandResult::fail(core::ErrorCode::SelfBond,
                                                 "resolved endpoints are the same atom");
            // Two DISTINCT atoms can still be at an identical coordinate (a
            // zero-length drag that created a second atom). That is a degenerate
            // bond and must be refused rather than stored as an invisible edge.
            {
                const core::AtomRecord* ab = ctx.document.atoms().alive(beginId);
                const core::AtomRecord* ae = ctx.document.atoms().alive(endId);
                if (ab && ae && ab->position == ae->position)
                    return core::CommandResult::fail(core::ErrorCode::InvalidGeometry,
                                                     "zero-length bond: endpoints coincide");
            }
            if (doc.bondBetween(beginId, endId) != core::kInvalidObjectId)
                return core::CommandResult::fail(core::ErrorCode::DuplicateBond,
                                                 "a bond already connects these atoms");

            core::BondRecord rec;
            rec.beginAtom = beginId;
            rec.endAtom = endId;
            rec.order = axesCopy.order;
            rec.display = axesCopy.display;
            rec.semantics = axesCopy.semantics;
            rec.stereo = axesCopy.stereo;
            core::ObjectId id = core::kInvalidObjectId;
            core::Error r = doc.addBondRecord(rec, &id);
            if (!r.ok()) return fromError(r);
            if (axesCopy.side != DoubleSide::Symmetric)
                setSideOnBag(doc, id, axesCopy.side);
            if (out) *out = id;
            return core::CommandResult::success();
        });
}

// ---------------------------------------------------------------------------
// Axis setters
// ---------------------------------------------------------------------------
core::Command setBondOrderCommand(const std::vector<core::ObjectId>& bonds,
                                  core::BondOrder order) {
    const std::vector<core::ObjectId> ids = bonds;
    const core::BondOrder o = order;
    return core::Command(nameFor("setOrder", ids.size()), core::kInvalidateSemantics,
        [ids, o](core::CommandContext& ctx) -> core::CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                core::Error r = requireBond(ctx.document, ids[i]);
                if (!r.ok()) return fromError(r);
                r = ctx.document.setBondOrder(ids[i], o);
                if (!r.ok()) return fromError(r);
            }
            return core::CommandResult::success(core::kInvalidateSemantics);
        });
}

// Appearance only: this MUST NOT request kInvalidateGeometry, or a style change
// would rebuild the spatial index (core transaction.style_not_geometry rule).
core::Command setBondDisplayCommand(const std::vector<core::ObjectId>& bonds,
                                    core::BondDisplay display) {
    const std::vector<core::ObjectId> ids = bonds;
    const core::BondDisplay d = display;
    return core::Command(nameFor("setDisplay", ids.size()), core::kInvalidateAppearance,
        [ids, d](core::CommandContext& ctx) -> core::CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                core::Error r = requireBond(ctx.document, ids[i]);
                if (!r.ok()) return fromError(r);
                r = ctx.document.setBondDisplay(ids[i], d);
                if (!r.ok()) return fromError(r);
            }
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}

core::Command setBondSemanticsCommand(const std::vector<core::ObjectId>& bonds,
                                      core::BondSemantics semantics) {
    const std::vector<core::ObjectId> ids = bonds;
    const core::BondSemantics s = semantics;
    return core::Command(nameFor("setSemantics", ids.size()), core::kInvalidateSemantics,
        [ids, s](core::CommandContext& ctx) -> core::CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                core::Error r = requireBond(ctx.document, ids[i]);
                if (!r.ok()) return fromError(r);
                const core::BondRecord* b = ctx.document.bonds().alive(ids[i]);
                if (!b) return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                         "bond is deleted");
                // Refuse to create an illegal pair. Leaving Aromatic semantics
                // while the order is still Aromatic would silently turn an
                // aromatic bond into a plain double bond.
                if (b->order == core::BondOrder::Aromatic &&
                    s != core::BondSemantics::Aromatic &&
                    s != core::BondSemantics::Query) {
                    return core::CommandResult::fail(core::ErrorCode::InvalidSemantics,
                        "cannot drop aromatic semantics while the order axis is Aromatic; "
                        "change the order and the semantics together with setBondAxes");
                }
                r = ctx.document.setBondSemantics(ids[i], s);
                if (!r.ok()) return fromError(r);
            }
            return core::CommandResult::success(core::kInvalidateSemantics);
        });
}

core::Command setBondStereoCommand(const std::vector<core::ObjectId>& bonds,
                                   core::BondStereo stereo) {
    const std::vector<core::ObjectId> ids = bonds;
    const core::BondStereo st = stereo;
    return core::Command(nameFor("setStereo", ids.size()), core::kInvalidateAppearance,
        [ids, st](core::CommandContext& ctx) -> core::CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                core::Error r = requireBond(ctx.document, ids[i]);
                if (!r.ok()) return fromError(r);
                r = ctx.document.setBondStereo(ids[i], st);
                if (!r.ok()) return fromError(r);
            }
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}

core::Command setBondSideCommand(const std::vector<core::ObjectId>& bonds,
                                 DoubleSide side) {
    const std::vector<core::ObjectId> ids = bonds;
    const DoubleSide sd = side;
    return core::Command(nameFor("setSide", ids.size()), core::kInvalidateAppearance,
        [ids, sd](core::CommandContext& ctx) -> core::CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                core::Error r = requireBond(ctx.document, ids[i]);
                if (!r.ok()) return fromError(r);
                setSideOnBag(ctx.document, ids[i], sd);
            }
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}

// ---------------------------------------------------------------------------
// Combined axes + preset application + reversal
// ---------------------------------------------------------------------------
core::Command setBondAxesCommand(const std::vector<core::ObjectId>& bonds,
                                 const BondAxes& axes) {
    const std::vector<core::ObjectId> ids = bonds;
    const BondAxes ax = axes;
    return core::Command(nameFor("setAxes", ids.size()),
        core::kInvalidateAppearance | core::kInvalidateSemantics,
        [ids, ax](core::CommandContext& ctx) -> core::CommandResult {
            const core::Error v = validateAxes(ax);
            if (!v.ok()) return fromError(v);
            // Apply order and semantics together: an aromatic order is only legal
            // once the semantics axis agrees, and the wrong sequence would fail
            // an intermediate validation.
            for (size_t i = 0; i < ids.size(); ++i) {
                core::Error r = requireBond(ctx.document, ids[i]);
                if (!r.ok()) return fromError(r);
                const core::BondRecord* b = ctx.document.bonds().alive(ids[i]);
                if (!b) return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                         "bond is deleted");
                if (b->order == core::BondOrder::Aromatic &&
                    ax.order != core::BondOrder::Aromatic) {
                    // Leaving aromatic order: relax the order first so the
                    // semantics change cannot leave an illegal combination.
                    r = ctx.document.setBondOrder(ids[i], ax.order);
                    if (!r.ok()) return fromError(r);
                }
                r = ctx.document.setBondSemantics(ids[i], ax.semantics);
                if (!r.ok()) return fromError(r);
                r = ctx.document.setBondOrder(ids[i], ax.order);
                if (!r.ok()) return fromError(r);
                r = ctx.document.setBondDisplay(ids[i], ax.display);
                if (!r.ok()) return fromError(r);
                r = ctx.document.setBondStereo(ids[i], ax.stereo);
                if (!r.ok()) return fromError(r);
                setSideOnBag(ctx.document, ids[i], ax.side);
            }
            return core::CommandResult::success(
                core::kInvalidateAppearance | core::kInvalidateSemantics);
        });
}

core::Command applyBondPresetCommand(const std::vector<core::ObjectId>& bonds,
                                     const std::string& presetIdOrCommand) {
    const BondPreset* p = presetForId(presetIdOrCommand);
    if (!p) p = presetForCommand(presetIdOrCommand);
    if (!p) {
        // An unknown preset must FAIL, not silently do nothing: a toolbar click
        // that quietly has no effect is the worst possible outcome.
        const std::string token = presetIdOrCommand;
        return core::Command("bond.applyPreset.unknown", core::kInvalidateNone,
            [token](core::CommandContext&) -> core::CommandResult {
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                 "unknown bond preset: " + token);
            });
    }
    return setBondAxesCommand(bonds, p->axes);
}

// Swapping begin/end is what flips a wedge/hash/dative. It changes ONLY the
// stored endpoint order; the coordinates and the geometry are untouched.
core::Command reverseBondDirectionCommand(const std::vector<core::ObjectId>& bonds) {
    const std::vector<core::ObjectId> ids = bonds;
    return core::Command(nameFor("reverse", ids.size()), core::kInvalidateAppearance,
        [ids](core::CommandContext& ctx) -> core::CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                core::Error r = requireBond(ctx.document, ids[i]);
                if (!r.ok()) return fromError(r);
                const core::BondRecord* b = ctx.document.bonds().alive(ids[i]);
                if (!b) return core::CommandResult::fail(core::ErrorCode::NotAlive,
                                                         "bond is deleted");
                r = ctx.document.setBondEndpoints(ids[i], b->endAtom, b->beginAtom);
                if (!r.ok()) return fromError(r);
            }
            return core::CommandResult::success(core::kInvalidateAppearance);
        });
}

// ---------------------------------------------------------------------------
// Guide-derived click behaviours
// ---------------------------------------------------------------------------
BondAxes nextClickAxes(const BondAxes& current) {
    BondAxes next = current;
    // GUIDE 2448-2449: the single bond tool click advances single -> double and
    // then repositions the two lines on later clicks.
    if (current.order == core::BondOrder::Single ||
        current.order == core::BondOrder::Unspecified) {
        next.order = core::BondOrder::Double;
        next.side = DoubleSide::Symmetric;
        return next;
    }
    // The crossed ("either") state must be tested FIRST. If the double-bond side
    // cycle ran first, a crossed double bond could never return to a single
    // bond, because its order is still Double.
    if (current.stereo == core::BondStereo::Either) {
        next.order = core::BondOrder::Single;
        next.stereo = core::BondStereo::None;
        next.side = DoubleSide::Symmetric;
        return next;
    }
    if (current.order == core::BondOrder::Double) {
        switch (current.side) {
            case DoubleSide::Symmetric: next.side = DoubleSide::Left; return next;
            case DoubleSide::Left:      next.side = DoubleSide::Right; return next;
            case DoubleSide::Right:     next.side = DoubleSide::Centered; return next;
            case DoubleSide::Centered:
                // Next click: unspecified configuration (crossed double bond).
                next.stereo = core::BondStereo::Either;
                next.side = DoubleSide::Symmetric;
                return next;
        }
    }
    return next;
}

BondAxes nextWavyClickAxes(const BondAxes& current) {
    BondAxes next = current;
    if (current.order == core::BondOrder::Single) {
        // GUIDE 2459-2461: wavy single -> double -> double either.
        next.order = core::BondOrder::Double;
        next.display = core::BondDisplay::Normal;
        next.side = DoubleSide::Symmetric;
        return next;
    }
    if (current.order == core::BondOrder::Double &&
        current.stereo != core::BondStereo::Either) {
        next.stereo = core::BondStereo::Either;
        return next;
    }
    return next;
}

bool isAppearanceOnly(const core::Command& command, const core::CommandContext& ctx) {
    (void)ctx;
    const core::Invalidation inv = command.invalidation();
    return (inv & core::kInvalidateGeometry) == 0 &&
           (inv & core::kInvalidateStructure) == 0;
}

} // namespace bonds
} // namespace r62
