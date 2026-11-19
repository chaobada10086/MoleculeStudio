#include "tool_registry.hpp"

#include <algorithm>
#include <cmath>

namespace r62 {
namespace core {

const char* behaviorName(ToolBehavior behavior) {
    switch (behavior) {
    case ToolBehavior::Select: return "Select";
    case ToolBehavior::Marquee: return "Marquee";
    case ToolBehavior::Lasso: return "Lasso";
    case ToolBehavior::Erase: return "Erase";
    case ToolBehavior::PlaceSingle: return "PlaceSingle";
    case ToolBehavior::PlaceRings: return "PlaceRings";
    case ToolBehavior::PlaceChain: return "PlaceChain";
    case ToolBehavior::DragBond: return "DragBond";
    case ToolBehavior::DrawCurve: return "DrawCurve";
    case ToolBehavior::EditCurve: return "EditCurve";
    case ToolBehavior::Text: return "Text";
    case ToolBehavior::Template: return "Template";
    case ToolBehavior::Table: return "Table";
    case ToolBehavior::Perspective: return "Perspective";
    }
    return "Unknown";
}

// ===========================================================================
// ToolRegistry
// ===========================================================================
ToolRegistry::ToolRegistry() : nextPresetId_(0) {}

void ToolRegistry::clear() {
    presets_.clear();
    byId_.clear();
    byCommand_.clear();
    tools_.clear();
    nextPresetId_ = 0;
}

Error ToolRegistry::registerPreset(const ToolPreset& preset, uint32_t* outPresetId) {
    if (preset.id.empty())
        return Error(ErrorCode::InvalidArgument, "A preset needs a namespaced string id");
    if (preset.id.find(".") == std::string::npos)
        return Error(ErrorCode::InvalidArgument,
                     "Preset id must be namespaced, e.g. r62.tool.bond.solid");
    if (byId_.find(preset.id) != byId_.end())
        return Error(ErrorCode::AlreadyExists, "Preset id is already registered: " + preset.id);
    ToolPreset stored = preset;
    stored.presetId = nextPresetId_++;
    presets_.push_back(stored);
    byId_[stored.id] = stored.presetId;
    if (outPresetId) *outPresetId = stored.presetId;
    return Error::success();
}

Error ToolRegistry::registerTool(ToolBehavior behavior, Tool* tool) {
    if (!tool) return Error(ErrorCode::InvalidArgument, "Tool must not be null");
    if (tools_.find(behavior) != tools_.end())
        return Error(ErrorCode::AlreadyExists,
                     std::string("A tool is already registered for behavior ") +
                     behaviorName(behavior));
    tools_[behavior] = tool;
    return Error::success();
}

Error ToolRegistry::bindPlatformCommand(const ToolId& id, uint32_t platformCommand) {
    std::map<ToolId, uint32_t>::const_iterator preset = byId_.find(id);
    if (preset == byId_.end())
        return Error(ErrorCode::NotFound, "No preset with that id: " + id);
    // Bands must be respected: a tool must not answer to an edit/file command id.
    if (platformCommand < platform_command::ToolBand ||
        platformCommand > platform_command::ToolBandEnd)
        return Error(ErrorCode::InvalidArgument,
                     "Tool commands must live in the ToolBand range");
    std::map<uint32_t, ToolId>::const_iterator existing = byCommand_.find(platformCommand);
    if (existing != byCommand_.end())
        return Error(ErrorCode::AlreadyExists,
                     "Platform command id is already bound: " + existing->second);
    byCommand_[platformCommand] = id;
    return Error::success();
}

const ToolPreset* ToolRegistry::preset(const ToolId& id) const {
    std::map<ToolId, uint32_t>::const_iterator it = byId_.find(id);
    return it == byId_.end() ? 0 : &presets_[it->second];
}

const ToolPreset* ToolRegistry::presetById(uint32_t presetId) const {
    return presetId < presets_.size() ? &presets_[presetId] : 0;
}

const ToolPreset* ToolRegistry::presetForCommand(uint32_t platformCommand) const {
    std::map<uint32_t, ToolId>::const_iterator it = byCommand_.find(platformCommand);
    return it == byCommand_.end() ? 0 : preset(it->second);
}

Tool* ToolRegistry::tool(ToolBehavior behavior) const {
    std::map<ToolBehavior, Tool*>::const_iterator it = tools_.find(behavior);
    return it == tools_.end() ? 0 : it->second;
}

Tool* ToolRegistry::toolFor(const ToolId& id) const {
    const ToolPreset* p = preset(id);
    return p ? tool(p->behavior) : 0;
}

const ToolPreset* ToolRegistry::resolvePlatformCommand(uint32_t command) const {
    return presetForCommand(command);
}

size_t ToolRegistry::implementedPresetCount() const {
    size_t n = 0;
    for (size_t i = 0; i < presets_.size(); ++i) if (presets_[i].implemented) ++n;
    return n;
}

std::vector<std::string> ToolRegistry::categories() const {
    std::set<std::string> unique;
    for (size_t i = 0; i < presets_.size(); ++i) unique.insert(presets_[i].category);
    return std::vector<std::string>(unique.begin(), unique.end());
}

size_t ToolRegistry::categoryCount() const { return categories().size(); }

// ===========================================================================
// Built-in tool ids
// ===========================================================================
namespace tool_ids {
const char* kSelect       = "r62.tool.select";
const char* kBondSolid    = "r62.tool.bond.solid";
const char* kRingBenzene  = "r62.tool.ring.benzene";
const char* kTextCaption  = "r62.tool.text.caption";
const char* kMarquee      = "r62.tool.select.marquee";
const char* kEraser       = "r62.tool.edit.eraser";
} // namespace tool_ids

// ===========================================================================
// SelectionTool
// ===========================================================================
namespace {
const ToolId kSelectionToolId = "r62.tool.select";
const ToolId kBondToolId1 = "r62.tool.bond.solid";
const ToolId kBondToolId2 = "r62.tool.bond.double";
const ToolId kBondToolId3 = "r62.tool.bond.triple";
const ToolId kRingToolId = "r62.tool.ring.benzene";
} // namespace

const ToolId& SelectionTool::id() const { return kSelectionToolId; }

CommandResult SelectionTool::beginGesture(ToolHost& host, const PointerState& pointer,
                                          HitContext& hits) {
    if (pointer.button != kButtonLeft)
        return CommandResult::noChange();
    const ObjectId atom = hits.nearestAtom(pointer.position, 8.0);
    const ObjectId bond = atom == kInvalidObjectId
                         ? hits.nearestBond(pointer.position, 6.0) : kInvalidObjectId;
    ObjectRef picked;
    if (atom != kInvalidObjectId) picked = ObjectRef(ObjectKind::Atom, atom);
    else if (bond != kInvalidObjectId) picked = ObjectRef(ObjectKind::Bond, bond);

    const bool additive = (pointer.modifiers & kModShift) != 0;
    if (!picked.valid()) {
        // Empty space: begin a marquee. The selection is only replaced on
        // release, so a stray click does not destroy the selection.
        dragSet_.clear();
        dragging_ = false;
        origin_ = pointer.position;
        delta_ = Vec2(0, 0);
        takeDragSet_ = false;
        return CommandResult::success(kInvalidateSelection);
    }
    const std::vector<ObjectRef>& selection = host.selection();
    const bool alreadySelected =
        std::find(selection.begin(), selection.end(), picked) != selection.end();
    if (!additive && !alreadySelected) {
        host.setSelection(std::vector<ObjectRef>(1, picked));
    } else if (additive && !alreadySelected) {
        host.addToSelection(std::vector<ObjectRef>(1, picked));
    }
    dragSet_ = host.selection();
    origin_ = pointer.position;
    delta_ = Vec2(0, 0);
    dragging_ = true;
    takeDragSet_ = true;
    return CommandResult::success(kInvalidateSelection);
}

CommandResult SelectionTool::updateGesture(ToolHost& host, const PointerState& pointer,
                                           HitContext& hits) {
    (void)hits;
    if (!dragging_) return CommandResult::noChange();
    delta_ = pointer.position - origin_;
    // The preview is built from the SAME delta the commit will apply, so the
    // preview cannot diverge from the result.
    PreviewScene& preview = host.preview();
    preview.clear();
    for (size_t i = 0; i < dragSet_.size(); ++i) {
        PreviewItem item;
        item.primitive = PreviewPrimitive::GlyphTransform;
        item.subject = dragSet_[i];
        item.points.push_back(delta_);
        preview.add(item);
        preview.mutableHidden().push_back(dragSet_[i]);
    }
    return CommandResult::success(kInvalidateNone);
}

CommandResult SelectionTool::endGesture(ToolHost& host, const PointerState& pointer,
                                        HitContext& hits) {
    (void)pointer;
    host.preview().clear();
    if (!dragging_) {
        // Marquee release.
        if (takeDragSet_) return CommandResult::noChange();
        Box box(origin_, pointer.position);
        if (box.min.x > box.max.x) std::swap(box.min.x, box.max.x);
        if (box.min.y > box.max.y) std::swap(box.min.y, box.max.y);
        if (box.width() > 0.0 || box.height() > 0.0) {
            std::vector<ObjectRef> found = hits.objectsInBox(box);
            host.setSelection(found);
        }
        return CommandResult::success(kInvalidateSelection);
    }
    dragging_ = false;
    if (delta_.x == 0.0 && delta_.y == 0.0) return CommandResult::noChange();
    // ONE transaction for the whole drag: R1 of commands.hpp.
    Error submitted = host.submit(commands::translateObjects(dragSet_, delta_),
                                  "select.drag");
    if (!submitted.ok()) {
        host.setStatus(std::string("Move failed: ") + errorName(submitted.code));
        return CommandResult::fail(submitted.code, submitted.message);
    }
    return CommandResult::success(kInvalidateGeometry | kInvalidateSelection);
}

CommandResult SelectionTool::cancelGesture(ToolHost& host) {
    host.preview().clear();
    dragging_ = false;
    dragSet_.clear();
    delta_ = Vec2(0, 0);
    return CommandResult::success(kInvalidateNone);
}

// ===========================================================================
// BondTool
// ===========================================================================
const ToolId& BondTool::id() const {
    switch (order_) {
    case BondOrder::Double: return kBondToolId2;
    case BondOrder::Triple: return kBondToolId3;
    default: return kBondToolId1;
    }
}

CommandResult BondTool::beginGesture(ToolHost& host, const PointerState& pointer,
                                     HitContext& hits) {
    (void)host;
    if (pointer.button != kButtonLeft) return CommandResult::noChange();
    pressed_ = true;
    // Snapping to an existing atom at press time means the new bond shares the
    // EXACT coordinates of that atom rather than creating a duplicate.
    startAtom_ = hits.nearestAtom(pointer.position, 10.0);
    startPoint_ = startAtom_ != kInvalidObjectId
                  ? hits.document().atoms().get(startAtom_)->position
                  : pointer.position;
    endPoint_ = startPoint_;
    targetAtom_ = kInvalidObjectId;
    return CommandResult::success(kInvalidateNone);
}

CommandResult BondTool::updateGesture(ToolHost& host, const PointerState& pointer,
                                      HitContext& hits) {
    if (!pressed_) return CommandResult::noChange();
    targetAtom_ = hits.nearestAtom(pointer.position, 10.0);
    if (targetAtom_ == startAtom_) targetAtom_ = kInvalidObjectId;
    endPoint_ = targetAtom_ != kInvalidObjectId
                ? hits.document().atoms().get(targetAtom_)->position
                : pointer.position;
    PreviewScene& preview = host.preview();
    preview.clear();
    PreviewItem item;
    item.primitive = PreviewPrimitive::Segment;
    item.points.push_back(startPoint_);
    item.points.push_back(endPoint_);
    preview.add(item);
    return CommandResult::success(kInvalidateNone);
}

CommandResult BondTool::endGesture(ToolHost& host, const PointerState& pointer,
                                   HitContext& hits) {
    (void)pointer;
    (void)hits;
    host.preview().clear();
    if (!pressed_) return CommandResult::noChange();
    pressed_ = false;
    if (std::fabs(endPoint_.x - startPoint_.x) < 1e-9 &&
        std::fabs(endPoint_.y - startPoint_.y) < 1e-9)
        return CommandResult::noChange();   // zero-length drag creates nothing

    // The command creates BOTH endpoints when they do not exist yet, then the
    // bond, all inside one transaction. Preview and commit use the same points.
    const Vec2 from = startPoint_;
    const Vec2 to = endPoint_;
    const ObjectId fromAtom = startAtom_;
    const ObjectId toAtom = targetAtom_;
    const BondOrder order = order_;
    Command command("bond.draw", kInvalidateStructure | kInvalidateGeometry | kInvalidateSemantics,
        [from, to, fromAtom, toAtom, order](CommandContext& ctx) -> CommandResult {
            Document& document = ctx.document;
            ObjectId a = fromAtom;
            ObjectId b = toAtom;
            if (a == kInvalidObjectId) {
                Error r = document.addAtom(from, 6, &a);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            if (b == kInvalidObjectId) {
                Error r = document.addAtom(to, 6, &b);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            BondRecord record;
            record.beginAtom = a;
            record.endAtom = b;
            record.order = order;
            Error r = document.addBondRecord(record, 0);
            if (!r.ok()) return CommandResult::fail(r.code, r.message);
            return CommandResult::success(kInvalidateStructure | kInvalidateGeometry |
                                          kInvalidateSemantics);
        });
    Error submitted = host.submit(command, "bond.draw");
    if (!submitted.ok()) {
        host.setStatus(std::string("Bond failed: ") + errorName(submitted.code));
        return CommandResult::fail(submitted.code, submitted.message);
    }
    return CommandResult::success(kInvalidateStructure | kInvalidateGeometry |
                                  kInvalidateSemantics);
}

CommandResult BondTool::cancelGesture(ToolHost& host) {
    host.preview().clear();
    pressed_ = false;
    startAtom_ = kInvalidObjectId;
    targetAtom_ = kInvalidObjectId;
    return CommandResult::success(kInvalidateNone);
}

// ===========================================================================
// PlaceRingTool
// ===========================================================================
const ToolId& PlaceRingTool::id() const { return kRingToolId; }

CommandResult PlaceRingTool::beginGesture(ToolHost& host, const PointerState& pointer,
                                          HitContext& hits) {
    anchor_ = hits.nearestAtom(pointer.position, 10.0);
    if (anchor_ != kInvalidObjectId)
        center_ = hits.document().atoms().get(anchor_)->position;
    else
        center_ = pointer.position;
    radius_ = host.document().defaultBondLength();
    PreviewScene& preview = host.preview();
    preview.clear();
    PreviewItem item;
    item.primitive = PreviewPrimitive::Circle;
    item.points.push_back(center_);
    item.points.push_back(Vec2(radius_, 0));
    preview.add(item);
    return CommandResult::success(kInvalidateNone);
}

CommandResult PlaceRingTool::updateGesture(ToolHost& host, const PointerState& pointer,
                                           HitContext& hits) {
    anchor_ = hits.nearestAtom(pointer.position, 10.0);
    if (anchor_ != kInvalidObjectId)
        center_ = hits.document().atoms().get(anchor_)->position;
    else
        center_ = pointer.position;
    PreviewScene& preview = host.preview();
    preview.clear();
    PreviewItem item;
    item.primitive = PreviewPrimitive::Circle;
    item.points.push_back(center_);
    item.points.push_back(Vec2(radius_, 0));
    preview.add(item);
    return CommandResult::success(kInvalidateNone);
}

CommandResult PlaceRingTool::endGesture(ToolHost& host, const PointerState& pointer,
                                        HitContext& hits) {
    (void)pointer;
    (void)hits;
    host.preview().clear();
    const Vec2 center = center_;
    const double radius = radius_;
    const ObjectId anchor = anchor_;
    Command command("ring.benzene", kInvalidateStructure | kInvalidateGeometry,
        [center, radius, anchor](CommandContext& ctx) -> CommandResult {
            Document& document = ctx.document;
            std::vector<ObjectId> ring;
            ring.reserve(6);
            for (int i = 0; i < 6; ++i) {
                const double t = i * 3.14159265358979323846 / 3.0;
                const Vec2 position(center.x + radius * std::cos(t),
                                    center.y + radius * std::sin(t));
                if (i == 0 && anchor != kInvalidObjectId) { ring.push_back(anchor); continue; }
                ObjectId id = kInvalidObjectId;
                Error r = document.addAtom(position, 6, &id);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
                ring.push_back(id);
            }
            for (int i = 0; i < 6; ++i) {
                BondRecord record;
                record.beginAtom = ring[i];
                record.endAtom = ring[(i + 1) % 6];
                record.order = BondOrder::Single;
                record.semantics = BondSemantics::Aromatic;
                Error r = document.addBondRecord(record, 0);
                if (!r.ok()) return CommandResult::fail(r.code, r.message);
            }
            return CommandResult::success(kInvalidateStructure | kInvalidateGeometry);
        });
    Error submitted = host.submit(command, "ring.benzene");
    if (!submitted.ok()) {
        host.setStatus(std::string("Ring failed: ") + errorName(submitted.code));
        return CommandResult::fail(submitted.code, submitted.message);
    }
    return CommandResult::success(kInvalidateStructure | kInvalidateGeometry);
}

CommandResult PlaceRingTool::cancelGesture(ToolHost& host) {
    host.preview().clear();
    return CommandResult::success(kInvalidateNone);
}

} // namespace core
} // namespace r62
