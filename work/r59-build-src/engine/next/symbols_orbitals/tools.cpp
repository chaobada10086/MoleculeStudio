#include "next/symbols_orbitals/tools.hpp"
#include "next/symbols_orbitals/push.hpp"

#include <cmath>
#include <cstdio>

namespace r62 {
namespace symbols {

namespace {

double dist(const core::Vec2& a, const core::Vec2& b) { return core::length(a - b); }

// The document point a symbol/orbital should be placed at: an exact existing atom
// coordinate when the pointer is over one, otherwise the raw pointer. Using the
// STORED coordinate (not the pointer) is what stops a new object from landing a
// fraction of a pixel off an atom it is nominally attached to.
core::Vec2 resolvePlacementOrigin(const core::Document& d, const core::Vec2& pointer,
                                  core::ObjectId nearestAtom) {
    if (nearestAtom != core::kInvalidObjectId) {
        const core::AtomRecord* a = d.atoms().alive(nearestAtom);
        if (a) return a->position;
    }
    return pointer;
}

} // namespace

// ===========================================================================
// registerSymbolsOrbitals
// ===========================================================================
core::Error registerSymbolsOrbitals(core::ToolRegistry* registry,
                                    uint32_t* outPresetIds, size_t* outCount) {
    if (!registry) return core::Error(core::ErrorCode::InvalidArgument, "no registry");
    size_t slot = 0;

    const size_t ns = symbolPresetCount();
    for (size_t i = 0; i < ns; ++i) {
        const SymbolPresetSpec& spec = symbolPresets()[i];
        // The two pushing commands live in BOTH tables: the symbol table keeps the
        // 16-command mapping complete (an inventory fact), while the push table
        // carries their real behavior. Registering the push command once, as a
        // DragBond tool, is what avoids a duplicate preset id.
        if (pushPresetByCommand(spec.sourceCommand)) continue;
        core::ToolPreset p;
        p.id = spec.toolId;
        p.behavior = core::ToolBehavior::PlaceSingle;
        p.target = core::PresetTarget::Symbol;
        p.displayName = spec.displayName;
        p.category = "chemical_symbols";
        p.sourceCommand = spec.sourceCommand;
        p.optionKey = spec.toolId;
        p.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry;
        p.implemented = spec.implemented;
        uint32_t assigned = core::kPresetNone;
        const core::Error e = registry->registerPreset(p, &assigned);
        if (!e.ok()) return e;
        if (registry->bindPlatformCommand(p.id,
                core::platform_command::ToolBand + assigned).code != core::ErrorCode::Ok) {
            // A collision is a real error, not something to ignore.
            return core::Error(core::ErrorCode::AlreadyExists, "platform command collision");
        }
        if (outPresetIds) outPresetIds[slot] = assigned;
        ++slot;
    }

    const size_t np = pushPresetCount();
    for (size_t i = 0; i < np; ++i) {
        const PushPresetSpec& spec = pushPresets()[i];
        core::ToolPreset p;
        p.id = spec.toolId;
        p.behavior = core::ToolBehavior::DragBond;
        p.target = core::PresetTarget::Arrow;   // a push IS an arrow object
        p.displayName = spec.displayName;
        p.category = "chemical_symbols";
        p.sourceCommand = spec.sourceCommand;
        p.optionKey = spec.toolId;
        p.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry;
        p.implemented = spec.implemented;
        uint32_t assigned = core::kPresetNone;
        const core::Error e = registry->registerPreset(p, &assigned);
        if (!e.ok()) return e;
        if (registry->bindPlatformCommand(p.id,
                core::platform_command::ToolBand + assigned).code != core::ErrorCode::Ok)
            return core::Error(core::ErrorCode::AlreadyExists, "platform command collision");
        if (outPresetIds) outPresetIds[slot] = assigned;
        ++slot;
    }

    const size_t no = orbitalPresetCount();
    for (size_t i = 0; i < no; ++i) {
        const OrbitalPresetSpec& spec = orbitalPresets()[i];
        core::ToolPreset p;
        p.id = spec.toolId;
        p.behavior = core::ToolBehavior::PlaceSingle;
        p.target = core::PresetTarget::Orbital;
        p.displayName = spec.displayName;
        p.category = "orbitals";
        p.sourceCommand = spec.sourceCommand;
        p.optionKey = spec.toolId;
        p.invalidation = core::kInvalidateStructure | core::kInvalidateGeometry;
        p.implemented = spec.implemented;
        uint32_t assigned = core::kPresetNone;
        const core::Error e = registry->registerPreset(p, &assigned);
        if (!e.ok()) return e;
        if (registry->bindPlatformCommand(p.id,
                core::platform_command::ToolBand + assigned).code != core::ErrorCode::Ok)
            return core::Error(core::ErrorCode::AlreadyExists, "platform command collision");
        if (outPresetIds) outPresetIds[slot] = assigned;
        ++slot;
    }

    if (outCount) *outCount = slot;
    return core::Error::success();
}

// ===========================================================================
// SymbolTool
// ===========================================================================
SymbolTool::SymbolTool(uint32_t presetId) : presetId_(presetId) {
    const SymbolPresetSpec* spec = symbolPresetById(presetId);
    id_ = spec ? std::string(spec->toolId) : std::string("r62.tool.symbol.unknown");
}

core::Invalidation SymbolTool::activate(core::ToolHost& host) {
    state_ = SymbolGestureState();
    previewGeometry_ = ShapeGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    const SymbolPresetSpec* spec = symbolPresetById(presetId_);
    if (spec && spec->meaning == SymbolMeaning::RadicalMono)
        host.setStatus("自由基：点击画布放置小黑点；切回选择工具后可拖动");
    return core::kInvalidateSelection;
}

core::CommandResult SymbolTool::beginGesture(core::ToolHost& host,
                                             const core::PointerState& pointer,
                                             core::HitContext& hits) {
    state_ = SymbolGestureState();
    previewGeometry_ = ShapeGeometry();
    state_.active = true;
    state_.presetId = presetId_;
    state_.pressPoint = pointer.position;

    const core::Document& doc = hits.document();
    SymbolParams p = resolveSymbolPreset(presetId_, pointer.position, doc.defaultTextSize());

    // [GUIDE p242] ALT+click with the lone-pair tool produces the BAR form.
    if (p.meaning == SymbolMeaning::LonePair && altRequestsBarLonePair(pointer.modifiers))
        p = lonePairBarVariant(p);

    // An atom under the pointer becomes the BIND TARGET - but nothing is bound yet.
    // The binding happens in the commit, inside the same transaction as the create.
    const core::ObjectId atom = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    // The radical dot is a free GRAPHIC object: like a text box, it can be placed
    // anywhere and dragged with the selection tool. It deliberately does not bind
    // to an atom, so it never fights an atom's own radical marker.
    state_.bindAtom = (p.meaning == SymbolMeaning::RadicalMono) ? core::kInvalidObjectId : atom;
    p.position = resolvePlacementOrigin(doc, pointer.position, state_.bindAtom);
    // A symbol with a Node/Centered anchor attaches AT the atom; a Free one stays
    // where the pointer is. This is the guide anchor rule applied as data.
    state_.pending = p;

    publishPreview(host, host.view());
    return core::CommandResult::noChange();
}

core::CommandResult SymbolTool::updateGesture(core::ToolHost& host,
                                              const core::PointerState& pointer,
                                              core::HitContext& hits) {
    if (!state_.active) return core::CommandResult::noChange();
    if (dist(pointer.position, state_.pressPoint) > 0.0) state_.moved = true;
    const core::Document& doc = hits.document();
    const core::ObjectId atom = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    state_.bindAtom = (state_.pending.meaning == SymbolMeaning::RadicalMono)
                          ? core::kInvalidObjectId : atom;
    state_.pending.position = resolvePlacementOrigin(doc, pointer.position, state_.bindAtom);
    publishPreview(host, host.view());
    return core::CommandResult::noChange();
}

core::CommandResult SymbolTool::endGesture(core::ToolHost& host,
                                           const core::PointerState& pointer,
                                           core::HitContext& hits) {
    (void)pointer; (void)hits;
    if (!state_.active) return core::CommandResult::noChange();
    const bool radical = state_.pending.meaning == SymbolMeaning::RadicalMono;
    const core::Error e = commit(host);
    const core::ObjectId atom = state_.bindAtom;
    state_ = SymbolGestureState();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    if (radical) {
        host.setStatus("自由基已放置；切回选择工具后可拖动");
    } else if (atom != core::kInvalidObjectId) {
        host.setStatus("symbol placed and bound to an atom");
    } else {
        host.setStatus("symbol placed (unbound: no atom under the pointer)");
    }
    return core::CommandResult::success();
}

core::CommandResult SymbolTool::cancelGesture(core::ToolHost& host) {
    state_ = SymbolGestureState();
    previewGeometry_ = ShapeGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    return core::CommandResult::noChange();
}

core::Error SymbolTool::commit(core::ToolHost& host) {
    const double lineWidth = host.document().defaultLineWidth();
    const SymbolParams payload = state_.pending;   // by value: the command must be copyable
    const core::ObjectId bindAtom = state_.bindAtom;
    const core::ObjectId target = state_.target;
    core::ObjectId created = core::kInvalidObjectId;
    const bool doBind = (bindAtom != core::kInvalidObjectId);

    if (target == core::kInvalidObjectId) {
        // ONE transaction covers create AND bind, so Undo restores both the new
        // object (removed) and the atom chemistry (reverted).
        core::Command cmd("symbols.add",
                          core::kInvalidateStructure | core::kInvalidateGeometry |
                          core::kInvalidateSemantics,
                          [payload, lineWidth, bindAtom, doBind, &created](core::CommandContext& ctx) {
                              core::ObjectId id = core::kInvalidObjectId;
                              const core::Error e = addSymbol(ctx.document, payload, lineWidth, &id);
                              if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                              created = id;
                              if (doBind) {
                                  BindOptions opt;
                                  const core::Error be =
                                      bindSymbolToAtom(ctx.document, id, bindAtom, opt, 0);
                                  if (!be.ok()) return core::CommandResult::fail(be.code, be.message);
                              }
                              return core::CommandResult::success();
                          });
        return host.submit(cmd, "symbols.add");
    }

    core::Command cmd("symbols.edit",
                      core::kInvalidateAppearance | core::kInvalidateGeometry |
                      core::kInvalidateSemantics,
                      [payload, lineWidth, target, bindAtom, doBind](core::CommandContext& ctx) {
                          const core::Error e = writeSymbol(ctx.document, target, payload, lineWidth);
                          if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                          if (doBind) {
                              SymbolBinding b;
                              BindOptions opt;
                              const core::Error be =
                                  bindSymbolToAtom(ctx.document, target, bindAtom, opt, &b);
                              if (!be.ok()) return core::CommandResult::fail(be.code, be.message);
                          }
                          return core::CommandResult::success();
                      });
    return host.submit(cmd, "symbols.edit");
}

void SymbolTool::publishPreview(core::ToolHost& host, const core::ViewTransform& view) {
    core::PreviewScene& preview = host.preview();
    preview.clear();
    preview.mutableHidden().clear();

    const core::Document& doc = host.document();
    const Tessellation tess;
    ShapeGeometry g;
    if (!buildSymbolGeometry(state_.pending, doc.defaultLineWidth(), tess, &g).ok()) {
        previewGeometry_ = ShapeGeometry();
        return;
    }
    previewGeometry_ = g;

    // The preview is built from the SAME ShapeGeometry the commit will write, so
    // "preview == commit" is structural rather than a discipline.
    for (size_t i = 0; i < g.outlines.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polygon;
        item.points = g.outlines[i];
        item.style.width = g.strokeWidth;
        preview.add(item);
    }
    for (size_t i = 0; i < g.strokes.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polyline;
        item.points = g.strokes[i];
        item.style.width = g.strokeWidth;
        preview.add(item);
    }
    for (size_t i = 0; i < g.dots.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polygon;
        item.points = g.dots[i];
        item.fillEnabled = 1;
        item.fillRGBA = 0x000000FFu;
        preview.add(item);
    }

    // Show WHERE the bind would land, so the user can see the difference between
    // "over an atom" (a chemistry edit) and "in empty space" (a pure graphic).
    if (state_.bindAtom != core::kInvalidObjectId) {
        const core::AtomRecord* a = doc.atoms().alive(state_.bindAtom);
        if (a) {
            core::PreviewItem hl;
            hl.primitive = core::PreviewPrimitive::Highlight;
            hl.subject = core::ObjectRef(core::ObjectKind::Atom, state_.bindAtom);
            hl.points.push_back(a->position);
            preview.add(hl);
        }
    }
    (void)view;
}
// ===========================================================================
// OrbitalTool
// ===========================================================================
OrbitalTool::OrbitalTool(uint32_t presetId) : presetId_(presetId) {
    const OrbitalPresetSpec* spec = orbitalPresetById(presetId);
    id_ = spec ? std::string(spec->toolId) : std::string("r62.tool.orbital.unknown");
}

core::Invalidation OrbitalTool::activate(core::ToolHost& host) {
    state_ = OrbitalGestureState();
    previewGeometry_ = ShapeGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    return core::kInvalidateSelection;
}

core::CommandResult OrbitalTool::beginGesture(core::ToolHost& host,
                                              const core::PointerState& pointer,
                                              core::HitContext& hits) {
    state_ = OrbitalGestureState();
    previewGeometry_ = ShapeGeometry();
    state_.active = true;
    state_.presetId = presetId_;
    state_.pressPoint = pointer.position;

    const core::Document& doc = hits.document();
    OrbitalParams p = resolveOrbitalPreset(presetId_, pointer.position,
                                           doc.defaultBondLength(), doc.defaultTextSize());
    // CENTERED shapes (s, sigma) are placed ON the atom; DIRECTED shapes (lobe, p,
    // sp3, d) place their NODE on the atom [GUIDE p240/p241]. Both reduce to "the
    // anchor is where the user clicked", because the anchor IS the centered point
    // for one family and the node for the other. The distinction is carried by
    // p.anchor and enforced by validateOrbitalParams.
    const core::ObjectId atom = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    p.position = resolvePlacementOrigin(doc, pointer.position, atom);
    state_.pending = p;
    publishPreview(host, host.view());
    return core::CommandResult::noChange();
}

core::CommandResult OrbitalTool::updateGesture(core::ToolHost& host,
                                               const core::PointerState& pointer,
                                               core::HitContext& hits) {
    if (!state_.active) return core::CommandResult::noChange();
    const core::Document& doc = hits.document();
    const core::Vec2 delta = pointer.position - state_.pressPoint;
    const double dragLen = core::length(delta);
    if (dragLen > 0.0) state_.moved = true;

    // [GUIDE p241] "Display the Info window to view the orbital length and angle
    // relative to the X-axis while you draw it." So the drag DOES define a length
    // and an angle: dragging away from the press point grows the orbital along the
    // drag direction. A click without a drag keeps the preset default.
    if (dragLen > 1e-9) {
        const double bond = doc.defaultBondLength();
        const double minLen = (bond > 0.0) ? bond * 0.35 : 5.0;
        state_.pending.length = (dragLen < minLen) ? minLen : dragLen;
        state_.pending.rotation = core::angleOf(delta);
        if (state_.pending.width > state_.pending.length * 1.6)
            state_.pending.width = state_.pending.length * 1.6;
    }
    const core::ObjectId atom = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    state_.pending.position = resolvePlacementOrigin(doc, pointer.position, atom);
    publishPreview(host, host.view());
    return core::CommandResult::noChange();
}

core::CommandResult OrbitalTool::endGesture(core::ToolHost& host,
                                            const core::PointerState& pointer,
                                            core::HitContext& hits) {
    (void)pointer; (void)hits;
    if (!state_.active) return core::CommandResult::noChange();
    const core::Error e = commit(host);
    state_ = OrbitalGestureState();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    host.setStatus("orbital placed");
    return core::CommandResult::success();
}

core::CommandResult OrbitalTool::cancelGesture(core::ToolHost& host) {
    state_ = OrbitalGestureState();
    previewGeometry_ = ShapeGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    return core::CommandResult::noChange();
}

core::Error OrbitalTool::commit(core::ToolHost& host) {
    const double lineWidth = host.document().defaultLineWidth();
    const double bondLength = host.document().defaultBondLength();
    const OrbitalParams payload = state_.pending;
    const core::ObjectId target = state_.target;

    if (target == core::kInvalidObjectId) {
        core::Command cmd("orbitals.add",
                          core::kInvalidateStructure | core::kInvalidateGeometry,
                          [payload, lineWidth](core::CommandContext& ctx) {
                              core::ObjectId id = core::kInvalidObjectId;
                              const core::Error e = addOrbital(ctx.document, payload, lineWidth, &id);
                              if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                              return core::CommandResult::success();
                          });
        return host.submit(cmd, "orbitals.add");
    }
    core::Command cmd("orbitals.edit",
                      core::kInvalidateAppearance | core::kInvalidateGeometry,
                      [payload, lineWidth, target](core::CommandContext& ctx) {
                          const core::Error e = writeOrbital(ctx.document, target, payload, lineWidth);
                          if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                          return core::CommandResult::success();
                      });
    return host.submit(cmd, "orbitals.edit");
    (void)bondLength;
}

void OrbitalTool::publishPreview(core::ToolHost& host, const core::ViewTransform& view) {
    core::PreviewScene& preview = host.preview();
    preview.clear();
    preview.mutableHidden().clear();

    const core::Document& doc = host.document();
    const Tessellation tess;
    ShapeGeometry g;
    if (!buildOrbitalGeometry(state_.pending, doc.defaultLineWidth(), tess, &g).ok()) {
        previewGeometry_ = ShapeGeometry();
        return;
    }
    previewGeometry_ = g;

    for (size_t i = 0; i < g.outlines.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polygon;
        item.points = g.outlines[i];
        item.style.width = g.strokeWidth;
        // Same authoritative paint predicate as the render bridge and the SVG
        // writer, so the drag PREVIEW matches the object that will be committed
        // (a Solid phase-inverted orbital is previewed solid, not as an outline).
        const bool paint = (i < g.outlinePaintFill.size())
                               ? g.outlinePaintFill[i]
                               : ((i < g.outlineFilled.size()) ? g.outlineFilled[i] : false);
        item.fillEnabled = paint ? 1 : 0;
        item.fillRGBA = 0x809EC5E8u;
        preview.add(item);
    }
    for (size_t i = 0; i < g.hatches.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polyline;
        item.points = g.hatches[i];
        item.style.width = g.strokeWidth * 0.6;
        preview.add(item);
    }
    for (size_t i = 0; i < g.strokes.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polyline;
        item.points = g.strokes[i];
        item.style.width = g.strokeWidth;
        preview.add(item);
    }
    (void)view;
}

// ===========================================================================
// PushTool
// ===========================================================================
PushTool::PushTool(uint32_t presetId) : presetId_(presetId), head_(PushHead::FullPair) {
    const PushPresetSpec* spec = pushPresetById(presetId);
    if (spec) {
        head_ = spec->head;
        id_ = std::string(spec->toolId);
    } else {
        id_ = std::string("r62.tool.push.electron");
    }
}

core::Invalidation PushTool::activate(core::ToolHost& host) {
    state_ = PushGestureState();
    previewGeometry_ = arrows::ArrowGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    return core::kInvalidateSelection;
}

core::CommandResult PushTool::beginGesture(core::ToolHost& host,
                                           const core::PointerState& pointer,
                                           core::HitContext& hits) {
    state_ = PushGestureState();
    previewGeometry_ = arrows::ArrowGeometry();
    state_.active = true;
    state_.presetId = presetId_;
    state_.pressPoint = pointer.position;

    const core::Document& doc = hits.document();
    PushParams p = resolvePushPreset(presetId_, pointer.position, pointer.position);
    p.head = head_;

    // [GUIDE p158] the SOURCE is an existing atom, bond or orbital. Resolve it from
    // the hit context so the trajectory starts ON the object rather than wherever
    // the pointer happened to be.
    const core::ObjectId atom = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    if (atom != core::kInvalidObjectId) {
        const core::AtomRecord* a = doc.atoms().alive(atom);
        if (a) {
            p.begin = a->position;
            p.sourceObject = core::ObjectRef(core::ObjectKind::Atom, atom);
            p.sourceKind = PushEndpoint::Atom;
        }
    }
    state_.pending = p;
    publishPreview(host, host.view());
    return core::CommandResult::noChange();
}

core::CommandResult PushTool::updateGesture(core::ToolHost& host,
                                            const core::PointerState& pointer,
                                            core::HitContext& hits) {
    if (!state_.active) return core::CommandResult::noChange();
    const core::Document& doc = hits.document();
    if (core::length(pointer.position - state_.pressPoint) > 0.0) state_.moved = true;

    state_.pending.end = pointer.position;
    state_.pending.targetObject = core::ObjectRef();
    state_.pending.targetKind = PushEndpoint::None;
    const core::ObjectId atom = hits.nearestAtom(pointer.position, host.snapRadiusDevice());
    if (atom != core::kInvalidObjectId) {
        const core::AtomRecord* a = doc.atoms().alive(atom);
        if (a) {
            // Snap the tip to the atom EXACTLY, so the mechanism arrow visibly
            // points at the atom and not at a nearby pixel.
            state_.pending.end = a->position;
            state_.pending.targetObject = core::ObjectRef(core::ObjectKind::Atom, atom);
            state_.pending.targetKind = PushEndpoint::Atom;
        }
    }
    publishPreview(host, host.view());
    return core::CommandResult::noChange();
}

core::CommandResult PushTool::endGesture(core::ToolHost& host,
                                         const core::PointerState& pointer,
                                         core::HitContext& hits) {
    (void)pointer; (void)hits;
    if (!state_.active) return core::CommandResult::noChange();
    const core::Error e = commit(host);
    state_ = PushGestureState();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
    host.setStatus(head_ == PushHead::FullPair ? "electron pair push drawn"
                                               : "single electron push drawn");
    return core::CommandResult::success();
}

core::CommandResult PushTool::cancelGesture(core::ToolHost& host) {
    state_ = PushGestureState();
    previewGeometry_ = arrows::ArrowGeometry();
    host.preview().clear();
    host.preview().mutableHidden().clear();
    return core::CommandResult::noChange();
}

core::Error PushTool::commit(core::ToolHost& host) {
    const double lineWidth = host.document().defaultLineWidth();
    PushParams payload = state_.pending;
    if (payload.end == payload.begin) {
        // A click without a drag has no direction. Give it the document bond
        // length along +x so the user sees a real arrow rather than an error.
        const double bond = host.document().defaultBondLength();
        payload.end = core::Vec2(payload.begin.x + (bond > 0.0 ? bond : 14.4), payload.begin.y);
    }
    core::Command cmd("push.add",
                      core::kInvalidateStructure | core::kInvalidateGeometry,
                      [payload, lineWidth](core::CommandContext& ctx) {
                          core::ObjectId id = core::kInvalidObjectId;
                          const core::Error e = addPush(ctx.document, payload, lineWidth, &id);
                          if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                          return core::CommandResult::success();
                      });
    return host.submit(cmd, "push.add");
}

void PushTool::publishPreview(core::ToolHost& host, const core::ViewTransform& view) {
    core::PreviewScene& preview = host.preview();
    preview.clear();
    preview.mutableHidden().clear();

    const core::Document& doc = host.document();
    arrows::Tessellation tess;
    arrows::ArrowGeometry g;
    if (!buildPushGeometry(state_.pending, doc.defaultLineWidth(), tess, &g).ok()) {
        previewGeometry_ = arrows::ArrowGeometry();
        return;
    }
    previewGeometry_ = g;

    // The preview reuses the T10 geometry verbatim - shafts, heads, markers - so a
    // push previews exactly as it will be committed.
    for (size_t s = 0; s < g.shafts.size(); ++s) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Polyline;
        item.points = g.shafts[s];
        item.style.width = g.strokeWidth;
        preview.add(item);
    }
    for (size_t i = 0; i < g.heads.size(); ++i) {
        const arrows::HeadGeometry& h = g.heads[i];
        if (!h.outline.empty()) {
            core::PreviewItem item;
            item.primitive = core::PreviewPrimitive::Polygon;
            item.points = h.outline;
            item.style.width = g.strokeWidth;
            item.fillEnabled = h.filled ? 1 : 0;
            item.fillRGBA = h.filled ? 0x000000FFu : 0u;
            preview.add(item);
        }
        for (size_t k = 0; k + 1 < h.strokes.size(); k += 2) {
            core::PreviewItem item;
            item.primitive = core::PreviewPrimitive::Segment;
            item.points.push_back(h.strokes[k]);
            item.points.push_back(h.strokes[k + 1]);
            item.style.width = g.strokeWidth;
            preview.add(item);
        }
    }

    // [GUIDE p158] "When you hover over the electron or radical pushing arrows, the
    // associated objects are highlighted in addition to the arrows." The same
    // highlight is shown DURING the gesture for the source and target that are set.
    if (state_.pending.sourceObject.valid()) {
        core::PreviewItem hl;
        hl.primitive = core::PreviewPrimitive::Highlight;
        hl.subject = state_.pending.sourceObject;
        hl.points.push_back(state_.pending.begin);
        preview.add(hl);
    }
    if (state_.pending.targetObject.valid()) {
        core::PreviewItem hl;
        hl.primitive = core::PreviewPrimitive::Highlight;
        hl.subject = state_.pending.targetObject;
        hl.points.push_back(state_.pending.end);
        preview.add(hl);
    }
    (void)view;
}

// ===========================================================================
// T04 candidates and handles
// ===========================================================================
core::Error appendSymbolCandidates(const core::Document& d, core::ObjectId symbolId,
                                   const core::ViewTransform& view,
                                   double documentLineWidth, const Tessellation& tess,
                                   std::vector<interaction::SnapCandidate>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output list");
    SymbolParams p;
    bool ok = false;
    const core::Error e = readSymbol(d, symbolId, &p, &ok);
    if (!e.ok()) return e;
    (void)ok;
    const core::ObjectRef ref(core::ObjectKind::Symbol, symbolId);
    // The anchor is a hotspot but NOT a legal bond join target: nothing should draw
    // a bond into a charge ring. eligible=false, tolerant=true is exactly that, and
    // the smaller radius stops a wide symbol from swallowing its neighbourhood.
    out->push_back(interaction::SnapCandidate(ref, p.position,
                                              interaction::CandidateKind::ControlHandle,
                                              false, true));
    const std::vector<Handle> handles = symbolHandles(p, documentLineWidth, tess);
    for (size_t i = 0; i < handles.size(); ++i)
        out->push_back(interaction::SnapCandidate(ref, handles[i].point,
                                                  interaction::CandidateKind::ControlHandle,
                                                  false, true));
    (void)view;
    return core::Error::success();
}

core::Error appendOrbitalCandidates(const core::Document& d, core::ObjectId orbitalId,
                                    const core::ViewTransform& view,
                                    double documentLineWidth, const Tessellation& tess,
                                    std::vector<interaction::SnapCandidate>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "no output list");
    OrbitalParams p;
    bool ok = false;
    const core::Error e = readOrbital(d, orbitalId, &p, &ok);
    if (!e.ok()) return e;
    (void)ok;
    const core::ObjectRef ref(core::ObjectKind::Orbital, orbitalId);
    // An orbital IS a legal push endpoint [GUIDE p158], so its anchor is ELIGIBLE.
    out->push_back(interaction::SnapCandidate(ref, p.position,
                                              interaction::CandidateKind::ControlHandle,
                                              true, false));
    const std::vector<Handle> handles = orbitalHandles(p, documentLineWidth, tess);
    for (size_t i = 0; i < handles.size(); ++i)
        out->push_back(interaction::SnapCandidate(ref, handles[i].point,
                                                  interaction::CandidateKind::ControlHandle,
                                                  false, true));
    (void)view;
    return core::Error::success();
}

core::Error appendSymbolHandlesOf(const core::Document& d, core::ObjectId symbolId,
                                  double documentLineWidth, const Tessellation& tess,
                                  std::vector<Handle>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    SymbolParams p;
    bool ok = false;
    const core::Error e = readSymbol(d, symbolId, &p, &ok);
    if (!e.ok()) return e;
    *out = symbolHandles(p, documentLineWidth, tess);
    return core::Error::success();
}

core::Error appendOrbitalHandlesOf(const core::Document& d, core::ObjectId orbitalId,
                                   double documentLineWidth, const Tessellation& tess,
                                   std::vector<Handle>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null out");
    OrbitalParams p;
    bool ok = false;
    const core::Error e = readOrbital(d, orbitalId, &p, &ok);
    if (!e.ok()) return e;
    *out = orbitalHandles(p, documentLineWidth, tess);
    return core::Error::success();
}

} // namespace symbols
} // namespace r62
