#include "commands.hpp"

#include <algorithm>
#include <cmath>

// ===========================================================================
// Built-in commands. Every one of these is a value-typed, copyable Command so
// that Ctrl+Y (Repeat) can re-execute it later without the caller keeping any
// state alive.
//
// Each command declares its invalidation class so downstream caches can be
// selective: a style edit must not rebuild a BVH, and a view change is not a
// document edit at all.
// ===========================================================================
namespace r62 {
namespace core {
namespace commands {

namespace {

// --- helpers ---------------------------------------------------------------

template <typename RecordT, typename SlabT>
bool readRecord(const SlabT& slab, ObjectId id, RecordT* out) {
    const RecordT* record = slab.get(id);
    if (!record) return false;
    *out = *record;
    return true;
}

Error requireAtom(const Document& document, ObjectId id, const char* what) {
    if (!document.atoms().alive(id))
        return Error(ErrorCode::NotAlive, std::string(what) + ": atom does not exist");
    return Error::success();
}

Error requireBond(const Document& document, ObjectId id, const char* what) {
    if (!document.bonds().alive(id))
        return Error(ErrorCode::NotAlive, std::string(what) + ": bond does not exist");
    return Error::success();
}

Error requireText(const Document& document, ObjectId id, const char* what) {
    if (!document.texts().alive(id))
        return Error(ErrorCode::NotAlive, std::string(what) + ": text does not exist");
    return Error::success();
}

} // namespace

// ---------------------------------------------------------------------------
// Creation / removal
// ---------------------------------------------------------------------------
Command addAtom(const Vec2& position, int element, ObjectId* outId) {
    return Command("atom.add", kInvalidateStructure | kInvalidateGeometry | kInvalidateSemantics,
        [position, element, outId](CommandContext& ctx) -> CommandResult {
            ObjectId id = kInvalidObjectId;
            Error r = ctx.document.addAtom(position, element, &id);
            if (!r.ok()) return CommandResult::fail(r.code, r.message);
            if (outId) *outId = id;
            return CommandResult::success(kInvalidateStructure | kInvalidateGeometry |
                                          kInvalidateSemantics);
        });
}

Command addBond(ObjectId begin, ObjectId end, BondOrder order, ObjectId* outId) {
    return Command("bond.add", kInvalidateStructure | kInvalidateGeometry | kInvalidateSemantics,
        [begin, end, order, outId](CommandContext& ctx) -> CommandResult {
            BondRecord record;
            record.beginAtom = begin;
            record.endAtom = end;
            record.order = order;
            ObjectId id = kInvalidObjectId;
            Error r = ctx.document.addBondRecord(record, &id);
            if (!r.ok()) return CommandResult::fail(r.code, r.message);
            if (outId) *outId = id;
            return CommandResult::success(kInvalidateStructure | kInvalidateGeometry |
                                          kInvalidateSemantics);
        });
}

Command addText(const Vec2& position, double size, const std::string& utf8, ObjectId* outId) {
    return Command("text.add", kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
        [position, size, utf8, outId](CommandContext& ctx) -> CommandResult {
            ObjectId id = kInvalidObjectId;
            Error r = ctx.document.addText(position, size, utf8, &id);
            if (!r.ok()) return CommandResult::fail(r.code, r.message);
            if (outId) *outId = id;
            return CommandResult::success(kInvalidateStructure | kInvalidateGeometry |
                                          kInvalidateAppearance);
        });
}

Command deleteObjects(const std::vector<ObjectRef>& refs) {
    std::vector<ObjectRef> copy(refs);
    return Command("objects.delete",
                   kInvalidateStructure | kInvalidateGeometry | kInvalidateSelection,
        [copy](CommandContext& ctx) -> CommandResult {
            // Bonds first: deleting an atom cascades, and a stale bond ref would
            // otherwise report NotAlive as an error instead of being skipped.
            for (size_t pass = 0; pass < 2; ++pass) {
                for (size_t i = 0; i < copy.size(); ++i) {
                    const bool isBond = copy[i].kind == ObjectKind::Bond;
                    if ((pass == 0) != isBond) continue;
                    Error r = ctx.document.removeObject(copy[i]);
                    if (!r.ok() && r.code != ErrorCode::NotAlive &&
                        r.code != ErrorCode::OutOfRange)
                        return CommandResult::fail(r.code, r.message);
                }
            }
            return CommandResult::success(kInvalidateStructure | kInvalidateGeometry |
                                          kInvalidateSelection);
        });
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------
Command translateObjects(const std::vector<ObjectRef>& refs, const Vec2& delta) {
    std::vector<ObjectRef> copy(refs);
    return Command("objects.translate", kInvalidateGeometry | kInvalidateSelection,
        [copy, delta](CommandContext& ctx) -> CommandResult {
            if (!delta.finite())
                return CommandResult::fail(ErrorCode::InvalidGeometry, "Delta must be finite");
            for (size_t i = 0; i < copy.size(); ++i) {
                const ObjectRef& ref = copy[i];
                if (ref.kind == ObjectKind::Atom) {
                    const AtomRecord* atom = ctx.document.atoms().alive(ref.id);
                    if (!atom) continue;
                    Error r = ctx.document.setAtomPosition(
                        ref.id, atom->position + delta);
                    if (!r.ok()) return CommandResult::fail(r.code, r.message);
                } else if (ref.kind == ObjectKind::Text) {
                    const TextRecord* text = ctx.document.texts().alive(ref.id);
                    if (!text) continue;
                    Error r = ctx.document.setTextPosition(ref.id, text->position + delta);
                    if (!r.ok()) return CommandResult::fail(r.code, r.message);
                }
            }
            return CommandResult::success(kInvalidateGeometry | kInvalidateSelection);
        });
}

Command setAtomPositions(const std::vector<ObjectId>& atoms,
                         const std::vector<Vec2>& positions) {
    std::vector<ObjectId> ids(atoms);
    std::vector<Vec2> points(positions);
    return Command("atoms.place", kInvalidateGeometry,
        [ids, points](CommandContext& ctx) -> CommandResult {
            if (ids.size() != points.size())
                return CommandResult::fail(ErrorCode::InvalidArgument,
                                           "ids and positions must have the same size");
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = ctx.document.setAtomPosition(ids[i], points[i]);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateGeometry);
        });
}

Command rotateObjects(const std::vector<ObjectRef>& refs, const Vec2& pivot,
                      double degrees) {
    std::vector<ObjectRef> copy(refs);
    return Command("objects.rotate", kInvalidateGeometry | kInvalidateSelection,
        [copy, pivot, degrees](CommandContext& ctx) -> CommandResult {
            if (!pivot.finite() || !std::isfinite(degrees))
                return CommandResult::fail(ErrorCode::InvalidGeometry, "Rotation must be finite");
            const double radians = degrees * 3.14159265358979323846 / 180.0;
            const double c = std::cos(radians), s = std::sin(radians);
            for (size_t i = 0; i < copy.size(); ++i) {
                const ObjectRef& ref = copy[i];
                if (ref.kind != ObjectKind::Atom) continue;
                const AtomRecord* atom = ctx.document.atoms().alive(ref.id);
                if (!atom) continue;
                const double x = atom->position.x - pivot.x;
                const double y = atom->position.y - pivot.y;
                const Vec2 moved(pivot.x + x * c - y * s, pivot.y + x * s + y * c);
                Error r = ctx.document.setAtomPosition(ref.id, moved);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateGeometry | kInvalidateSelection);
        });
}

// ---------------------------------------------------------------------------
// Semantics / style
// ---------------------------------------------------------------------------
Command setElements(const std::vector<ObjectId>& atoms, int element) {
    std::vector<ObjectId> ids(atoms);
    return Command("atoms.element", kInvalidateAppearance | kInvalidateSemantics,
        [ids, element](CommandContext& ctx) -> CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = requireAtom(ctx.document, ids[i], "setElements");
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = ctx.document.setElement(ids[i], element);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateAppearance | kInvalidateSemantics);
        });
}

Command setAtomSemantics(const std::vector<ObjectId>& atoms, const AtomSemantics& s) {
    std::vector<ObjectId> ids(atoms);
    return Command("atoms.semantics", kInvalidateAppearance | kInvalidateSemantics,
        [ids, s](CommandContext& ctx) -> CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = requireAtom(ctx.document, ids[i], "setAtomSemantics");
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = ctx.document.setAtomSemantics(ids[i], s);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateAppearance | kInvalidateSemantics);
        });
}

Command setBondOrders(const std::vector<ObjectId>& bonds, BondOrder order) {
    std::vector<ObjectId> ids(bonds);
    return Command("bonds.order", kInvalidateAppearance | kInvalidateSemantics,
        [ids, order](CommandContext& ctx) -> CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = requireBond(ctx.document, ids[i], "setBondOrders");
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = ctx.document.setBondOrder(ids[i], order);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateAppearance | kInvalidateSemantics);
        });
}

Command setBondDisplay(const std::vector<ObjectId>& bonds, BondDisplay display) {
    std::vector<ObjectId> ids(bonds);
    return Command("bonds.display", kInvalidateAppearance | kInvalidateSemantics,
        [ids, display](CommandContext& ctx) -> CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = requireBond(ctx.document, ids[i], "setBondDisplay");
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = ctx.document.setBondDisplay(ids[i], display);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateAppearance | kInvalidateSemantics);
        });
}

Command setBondSemantics(const std::vector<ObjectId>& bonds, BondSemantics semantics) {
    std::vector<ObjectId> ids(bonds);
    return Command("bonds.semantics", kInvalidateSemantics,
        [ids, semantics](CommandContext& ctx) -> CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = requireBond(ctx.document, ids[i], "setBondSemantics");
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = ctx.document.setBondSemantics(ids[i], semantics);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateSemantics);
        });
}

Command setBondStereo(const std::vector<ObjectId>& bonds, BondStereo stereo) {
    std::vector<ObjectId> ids(bonds);
    return Command("bonds.stereo", kInvalidateAppearance | kInvalidateSemantics,
        [ids, stereo](CommandContext& ctx) -> CommandResult {
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = requireBond(ctx.document, ids[i], "setBondStereo");
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            for (size_t i = 0; i < ids.size(); ++i) {
                Error r = ctx.document.setBondStereo(ids[i], stereo);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateAppearance | kInvalidateSemantics);
        });
}

Command setTextPlain(ObjectId text, const std::string& utf8) {
    return Command("text.set", kInvalidateAppearance | kInvalidateSemantics,
        [text, utf8](CommandContext& ctx) -> CommandResult {
            Error r = requireText(ctx.document, text, "setTextPlain");
            if (!r.ok()) return CommandResult::fail(r.code, r.message);
            r = ctx.document.setTextPlain(text, utf8);
            if (!r.ok()) return CommandResult::fail(r.code, r.message);
            return CommandResult::success(kInvalidateAppearance | kInvalidateSemantics);
        });
}

Command setTextRuns(ObjectId text, const std::vector<TextRun>& runs) {
    std::vector<TextRun> copy(runs);
    return Command("text.runs", kInvalidateAppearance | kInvalidateSemantics,
        [text, copy](CommandContext& ctx) -> CommandResult {
            Error r = requireText(ctx.document, text, "setTextRuns");
            if (!r.ok()) return CommandResult::fail(r.code, r.message);
            r = ctx.document.setTextRuns(text, copy);
            if (!r.ok()) return CommandResult::fail(r.code, r.message);
            return CommandResult::success(kInvalidateAppearance | kInvalidateSemantics);
        });
}

Command setLineStyle(const std::vector<ObjectRef>& refs, const LineStyle& style) {
    std::vector<ObjectRef> copy(refs);
    return Command("objects.linestyle", kInvalidateAppearance,
        [copy, style](CommandContext& ctx) -> CommandResult {
            for (size_t i = 0; i < copy.size(); ++i) {
                Error r = ctx.document.setLineStyle(copy[i], style);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateAppearance);
        });
}

// ---------------------------------------------------------------------------
// Style presets (the "275 tools are mostly style options" case)
// ---------------------------------------------------------------------------
// A preset is DATA: a string option id that resolves to one of the bounded
// algorithms. Applying it goes through the same transaction entry point as any
// other edit, so a preset cannot invent a second mutation path.
Command setStylePreset(const std::vector<ObjectRef>& refs, const std::string& optionId) {
    std::vector<ObjectRef> copy(refs);
    return Command("style.preset:" + optionId, kInvalidateAppearance | kInvalidateSemantics,
        [copy, optionId](CommandContext& ctx) -> CommandResult {
            if (optionId.empty())
                return CommandResult::fail(ErrorCode::InvalidArgument, "Empty style option id");
            for (size_t i = 0; i < copy.size(); ++i) {
                const ObjectRef& ref = copy[i];
                if (ref.kind == ObjectKind::Bond) {
                    BondDisplay display = BondDisplay::Normal;
                    BondSemantics semantics = BondSemantics::Normal;
                    bool haveDisplay = false, haveSemantics = false;
                    if (optionId == "r62.style.bond.wedge") { display = BondDisplay::Wedge; haveDisplay = true; }
                    else if (optionId == "r62.style.bond.hash") { display = BondDisplay::Hash; haveDisplay = true; }
                    else if (optionId == "r62.style.bond.hollowwedge") { display = BondDisplay::HollowWedge; haveDisplay = true; }
                    else if (optionId == "r62.style.bond.bold") { display = BondDisplay::Bold; haveDisplay = true; }
                    else if (optionId == "r62.style.bond.dashed") { display = BondDisplay::Dashed; haveDisplay = true; }
                    else if (optionId == "r62.style.bond.dotted") { display = BondDisplay::Dotted; haveDisplay = true; }
                    else if (optionId == "r62.style.bond.wavy") { display = BondDisplay::Wavy; haveDisplay = true; }
                    else if (optionId == "r62.style.bond.solid") { display = BondDisplay::Normal; haveDisplay = true; }
                    else if (optionId == "r62.style.bond.aromatic") {
                        semantics = BondSemantics::Aromatic; haveSemantics = true; }
                    else if (optionId == "r62.style.bond.dative") {
                        semantics = BondSemantics::Dative; haveSemantics = true; }
                    else if (optionId == "r62.style.bond.hydrogen") {
                        semantics = BondSemantics::Hydrogen; haveSemantics = true; }
                    else {
                        return CommandResult::fail(ErrorCode::Unsupported,
                                                   "Unknown bond style preset: " + optionId);
                    }
                    if (haveDisplay) {
                        Error r = ctx.document.setBondDisplay(ref.id, display);
                        if (!r.ok()) return CommandResult::fail(r.code, r.message);
                    }
                    if (haveSemantics) {
                        Error r = ctx.document.setBondSemantics(ref.id, semantics);
                        if (!r.ok()) return CommandResult::fail(r.code, r.message);
                    }
                } else if (ref.kind == ObjectKind::Atom) {
                    const AtomRecord* atom = ctx.document.atoms().alive(ref.id);
                    if (!atom) continue;
                    AtomSemantics semantics = atom->semantics;
                    if (optionId == "r62.style.atom.showlabel") semantics.showLabel = true;
                    else if (optionId == "r62.style.atom.hidelabel") semantics.showLabel = false;
                    else if (optionId == "r62.style.atom.aromatic") semantics.aromatic = true;
                    else if (optionId == "r62.style.atom.aliphatic") semantics.aromatic = false;
                    else {
                        return CommandResult::fail(ErrorCode::Unsupported,
                                                   "Unknown atom style preset: " + optionId);
                    }
                    Error r = ctx.document.setAtomSemantics(ref.id, semantics);
                    if (!r.ok()) return CommandResult::fail(r.code, r.message);
                } else {
                    return CommandResult::fail(ErrorCode::Unsupported,
                                               "Style presets are not implemented for this class");
                }
            }
            return CommandResult::success(kInvalidateAppearance | kInvalidateSemantics);
        });
}

// ---------------------------------------------------------------------------
// Extension payload
// ---------------------------------------------------------------------------
Command setDocumentExtension(const std::string& key, const std::string& value) {
    return Command("ext.document", kInvalidateStructure,
        [key, value](CommandContext& ctx) -> CommandResult {
            if (key.empty())
                return CommandResult::fail(ErrorCode::InvalidArgument, "Extension key is empty");
            ctx.document.documentExtensions().set(key, value);
            ctx.document.noteTouchedExtensions(ObjectRef(), true);
            return CommandResult::success(kInvalidateStructure);
        });
}

Command setObjectExtension(const ObjectRef& ref, const std::string& key,
                           const std::string& value) {
    return Command("ext.object", kInvalidateStructure,
        [ref, key, value](CommandContext& ctx) -> CommandResult {
            if (!ref.valid() || key.empty())
                return CommandResult::fail(ErrorCode::InvalidArgument,
                                           "Extension ref and key must be valid");
            ctx.document.extensions(ref).set(key, value);
            ctx.document.noteTouchedExtensions(ref, false);
            return CommandResult::success(kInvalidateStructure);
        });
}

} // namespace commands
} // namespace core
} // namespace r62
