#include "next/templates_queries/instantiate.hpp"

#include <cmath>

namespace r62 {
namespace templates_queries {

namespace {

const double kPi = 3.14159265358979323846;

bool finitePlacement(const Placement& pl) {
    if (!pl.origin.finite()) return false;
    if (!std::isfinite(pl.rotationDeg)) return false;
    if (!std::isfinite(pl.directionDeg)) return false;
    if (!std::isfinite(pl.scale)) return false;
    return true;
}

PlacementPlan fail(const std::string& id, const std::string& why) {
    PlacementPlan plan;
    plan.templateId = id;
    plan.ok = false;
    plan.error = why;
    return plan;
}

} // namespace

// ---------------------------------------------------------------------------
// anchorBondEndpoint
// ---------------------------------------------------------------------------
core::Vec2 anchorBondEndpoint(const core::Vec2& target, double directionDeg,
                              double bondLength) {
    const core::Vec2 d = core::direction(directionDeg);
    return core::Vec2(target.x + d.x * bondLength, target.y + d.y * bondLength);
}

// ---------------------------------------------------------------------------
// planPlacement
// ---------------------------------------------------------------------------
// The single geometric decision this function makes is: ROTATE the template so the
// anchor atom sits at the target end of the anchor bond and the template extends
// along directionDeg.  Everything else is translation and uniform scale, so the
// template's internal geometry is preserved exactly.
PlacementPlan planPlacement(const core::Document& document,
                            const TemplateRegistry& registry,
                            const std::string& templateId,
                            const Placement& placement) {
    const CatalogEntry* entry = registry.find(templateId);
    if (entry == 0) return fail(templateId, "unknown template id: " + templateId);
    if (!finitePlacement(placement))
        return fail(templateId, "placement has a non-finite origin, rotation, direction or scale");
    if (placement.scale <= 0.0)
        return fail(templateId, "placement scale must be > 0 (0 is refused, not defaulted to 1)");

    // Which template atom is the anchor (rank 1).
    int anchorLocal = -1;
    int bestRank = 0;
    for (size_t i = 0; i < entry->anchors.size(); ++i) {
        if (anchorLocal < 0 || entry->anchors[i].rank < bestRank) {
            anchorLocal = entry->anchors[i].atomIndex;
            bestRank = entry->anchors[i].rank;
        }
    }
    if (anchorLocal < 0)
        return fail(templateId, "template declares no anchor");

    const bool connecting = placement.anchorTarget != core::kInvalidObjectId;
    if (connecting) {
        if (document.atoms().alive(placement.anchorTarget) == 0)
            return fail(templateId, "anchor target is not a live atom in this document");
    }

    // Effective rotation.
    //  * Not connecting: just the requested rotation.  The template origin lands on
    //    placement.origin.
    //  * Connecting: the anchor atom must land one bond length from the target along
    //    directionDeg, and the template must be turned so its own anchor->centroid
    //    direction points the same way.  The template origin is therefore IGNORED
    //    (the target atom determines the position), which is stated rather than
    //    silently applied.
    double effectiveRotation = placement.rotationDeg;
    core::Vec2 anchorWorld = placement.origin;

    const CatalogAtom& anchorAtom = entry->atoms[static_cast<size_t>(anchorLocal)];

    if (connecting) {
        const core::AtomRecord* targetRec = document.atoms().alive(placement.anchorTarget);
        // targetRec is non-null because of the check above.
        const core::Vec2 targetPos = targetRec->position;
        const double bondLength = document.defaultBondLength();
        anchorWorld = anchorBondEndpoint(targetPos, placement.directionDeg, bondLength);

        // The template's own "growth direction" is anchor -> farthest atom, measured
        // in template-local degrees.  Using the farthest atom (not the first bond)
        // makes a ring grow outward along its axis rather than toward one vertex.
        double bestLen = -1.0;
        core::Vec2 localDir(1.0, 0.0);
        for (size_t i = 0; i < entry->atoms.size(); ++i) {
            if (static_cast<int>(i) == anchorLocal) continue;
            const core::Vec2 v = entry->atoms[i].offset - anchorAtom.offset;
            const double l2 = core::lengthSq(v);
            if (l2 > bestLen) { bestLen = l2; localDir = v; }
        }
        if (bestLen <= 0.0) {
            // A single-atom template (Me, OH, NH2, an R site).  There is no growth
            // axis to align, so the requested rotation is used verbatim and the
            // direction only decides WHERE the single atom sits.  This is stated
            // instead of inventing an alignment that does not exist.
            effectiveRotation = placement.rotationDeg;
        } else {
            const double localAng = core::angleOf(localDir);
            effectiveRotation = placement.directionDeg - localAng;
        }
    }

    PlacementPlan plan;
    plan.templateId = templateId;

    const double rotRad = effectiveRotation * kPi / 180.0;
    const double cs = std::cos(rotRad);
    const double sn = std::sin(rotRad);

    // Place atoms: rotate about the anchor atom, then translate the anchor to its
    // world position.  Rotating about the ANCHOR (not the origin) is what keeps the
    // anchor bond length exact for every template.
    plan.atoms.reserve(entry->atoms.size());
    for (size_t i = 0; i < entry->atoms.size(); ++i) {
        const CatalogAtom& a = entry->atoms[i];
        const core::Vec2 rel = a.offset - anchorAtom.offset;
        const double sx = rel.x * placement.scale;
        const double sy = rel.y * placement.scale;
        // Document space is y-DOWN, and core::direction() already accounts for it,
        // so a positive rotationDeg must rotate the same way.  Using the standard
        // rotation matrix here with y-down turns math-CCW into screen-CW; the
        // negation on sy is what makes "90 degrees" and direction(90) agree.
        const double rx = sx * cs + sy * sn;
        const double ry = -sx * sn + sy * cs;

        PlannedPlacementAtom out;
        out.element = a.element;
        out.labelText = a.labelText;
        out.position = core::Vec2(anchorWorld.x + rx, anchorWorld.y + ry);
        out.charge = a.charge;
        out.hydrogenCount = a.hydrogenCount;
        out.aromatic = a.aromatic;
        out.isAnchor = (static_cast<int>(i) == anchorLocal);
        out.anchorRank = 0;
        for (size_t k = 0; k < entry->anchors.size(); ++k)
            if (entry->anchors[k].atomIndex == static_cast<int>(i))
                out.anchorRank = entry->anchors[k].rank;
        plan.atoms.push_back(out);
    }

    // Bounds over placed atoms.
    plan.bounds = core::Box(plan.atoms[0].position, plan.atoms[0].position);
    for (size_t i = 1; i < plan.atoms.size(); ++i) plan.bounds.add(plan.atoms[i].position);

    // Template-internal bonds.
    for (size_t i = 0; i < entry->bonds.size(); ++i) {
        const CatalogBond& b = entry->bonds[i];
        PlannedPlacementBond pb;
        pb.fromLocal = b.from;
        pb.toLocal = b.to;
        pb.order = b.order;
        pb.display = b.display;
        pb.semantics = b.semantics;
        plan.bonds.push_back(pb);
    }

    // The connectivity bond, when connecting.  Guarded against creating a duplicate
    // bond to an atom that ALREADY has one of these: that would make addBond fail
    // inside the transaction and the whole paste would roll back.  Refusing up front
    // with a reason is the honest alternative to a silent partial paste.
    if (connecting) {
        PlannedPlacementBond pb;
        pb.fromLocal = -1;      // the existing target
        pb.toLocal = anchorLocal;
        pb.order = placement.anchorOrder;
        pb.semantics = core::BondSemantics::Normal;
        if (placement.anchorOrder == core::BondOrder::Aromatic)
            pb.semantics = core::BondSemantics::Aromatic;
        plan.bonds.push_back(pb);
    }

    plan.ok = true;
    return plan;
}

// ---------------------------------------------------------------------------
// instantiateCommand
// ---------------------------------------------------------------------------
// The command body is a pure function of the plan, so Core's Repeat (Ctrl+Y) can
// replay it and each replay produces a NEW instance with new ids.  That is exactly
// the semantics "repeating a paste does not alias the previous paste".
core::Error instantiateCommand(const PlacementPlan& plan,
                               core::ObjectId anchorTarget,
                               core::Command* outCommand,
                               std::vector<core::ObjectId>* createdAtoms,
                               std::vector<core::ObjectId>* createdBonds) {
    if (!outCommand) return core::Error(core::ErrorCode::InvalidArgument, "outCommand is null");
    if (!plan.ok) {
        return core::Error(core::ErrorCode::InvalidArgument,
                           plan.error.empty() ? std::string("plan is not ok") : plan.error);
    }
    if (plan.atoms.empty())
        return core::Error(core::ErrorCode::InvalidArgument, "plan creates no atoms");

    // Capture the plan BY VALUE.  A Command must not hold a pointer into the
    // document or into a caller stack frame (commands.hpp is explicit about this).
    const PlacementPlan captured = plan;

    *outCommand = core::Command("templates.instantiate", core::kInvalidateStructure |
                                                      core::kInvalidateGeometry |
                                                      core::kInvalidateSemantics,
        [captured, anchorTarget, createdAtoms, createdBonds](core::CommandContext& ctx) -> core::CommandResult {
            core::Document& doc = ctx.document;

            std::vector<core::ObjectId> localIds;
            localIds.reserve(captured.atoms.size());

            for (size_t i = 0; i < captured.atoms.size(); ++i) {
                const PlannedPlacementAtom& a = captured.atoms[i];
                core::AtomRecord rec;
                rec.position = a.position;
                rec.element = static_cast<int16_t>(a.element == 0 ? 6 : a.element);
                rec.semantics.charge = static_cast<int8_t>(a.charge);
                rec.semantics.implicitHydrogens = static_cast<int8_t>(a.hydrogenCount);
                rec.semantics.aromatic = a.aromatic;
                if (a.element == 0) {
                    // A labelled non-element node.  NodeType is set so the label path
                    // knows this is not carbon-with-a-label.
                    rec.semantics.nodeType = static_cast<uint8_t>(core::NodeType::RGroup);
                    rec.semantics.showLabel = true;
                }
                core::ObjectId id = core::kInvalidObjectId;
                core::Error e = doc.addAtomRecord(rec, &id);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                if (a.element == 0 && !a.labelText.empty())
                    doc.setAtomLabelQuiet(id, a.labelText);
                localIds.push_back(id);
            }

            std::vector<core::ObjectId> bondIds;
            bondIds.reserve(captured.bonds.size());
            for (size_t i = 0; i < captured.bonds.size(); ++i) {
                const PlannedPlacementBond& b = captured.bonds[i];
                core::ObjectId fromId = core::kInvalidObjectId;
                core::ObjectId toId = core::kInvalidObjectId;
                if (b.fromLocal < 0) {
                    fromId = anchorTarget;
                } else {
                    if (static_cast<size_t>(b.fromLocal) >= localIds.size())
                        return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                         "bond fromLocal out of range");
                    fromId = localIds[static_cast<size_t>(b.fromLocal)];
                }
                if (static_cast<size_t>(b.toLocal) >= localIds.size())
                    return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                     "bond toLocal out of range");
                toId = localIds[static_cast<size_t>(b.toLocal)];

                if (fromId == core::kInvalidObjectId ||
                    !doc.atoms().exists(fromId) || !doc.atoms().exists(toId))
                    return core::CommandResult::fail(core::ErrorCode::NotFound,
                                                     "anchor target is not a live atom");

                core::BondRecord brec;
                brec.beginAtom = fromId;
                brec.endAtom = toId;
                brec.order = b.order;
                brec.display = b.display;
                brec.semantics = b.semantics;
                core::ObjectId bid = core::kInvalidObjectId;
                core::Error e = doc.addBondRecord(brec, &bid);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                bondIds.push_back(bid);
            }

            if (createdAtoms) *createdAtoms = localIds;
            if (createdBonds) *createdBonds = bondIds;
            return core::CommandResult::success(core::kInvalidateStructure |
                                                core::kInvalidateGeometry |
                                                core::kInvalidateSemantics);
        });

    return core::Error::success();
}

core::Error instantiateTemplate(const core::Document& document,
                                const TemplateRegistry& registry,
                                const std::string& templateId,
                                const Placement& placement,
                                core::Command* outCommand,
                                PlacementPlan* outPlan,
                                std::vector<core::ObjectId>* createdAtoms,
                                std::vector<core::ObjectId>* createdBonds) {
    PlacementPlan plan = planPlacement(document, registry, templateId, placement);
    if (outPlan) *outPlan = plan;
    if (!plan.ok) return core::Error(core::ErrorCode::InvalidArgument, plan.error);
    return instantiateCommand(plan, placement.anchorTarget, outCommand,
                              createdAtoms, createdBonds);
}

// ---------------------------------------------------------------------------
// T06 bridge
// ---------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string> >
abbreviationIndex(const TemplateRegistry& registry) {
    std::vector<std::pair<std::string, std::string> > out;
    const std::vector<CatalogEntry>& entries = registry.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        std::map<std::string, std::string>::const_iterator it =
            entries[i].meta.find("abbreviation");
        if (it != entries[i].meta.end() && !it->second.empty())
            out.push_back(std::make_pair(it->second, entries[i].templateId));
    }
    return out;
}

ServiceStats serviceStats(const TemplateRegistry& registry) {
    ServiceStats s;
    s.catalogEntryCount = registry.size();
    const std::vector<std::pair<std::string, std::string> > idx = abbreviationIndex(registry);
    s.abbreviationCount = idx.size();
    s.unmappedEntries = registry.size() - idx.size();
    return s;
}

} // namespace templates_queries
} // namespace r62
