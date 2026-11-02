#include "next/advanced/fragments.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace r62 {
namespace advanced {

namespace {

core::Vec2 centroidOf(const core::Document& doc,
                      const std::vector<core::ObjectId>& atoms) {
    core::Vec2 sum(0.0, 0.0);
    size_t n = 0;
    for (size_t i = 0; i < atoms.size(); ++i) {
        const core::AtomRecord* a = doc.atoms().alive(atoms[i]);
        if (!a) continue;
        sum = sum + a->position;
        ++n;
    }
    if (n == 0) return core::Vec2(0.0, 0.0);
    return core::Vec2(sum.x / static_cast<double>(n), sum.y / static_cast<double>(n));
}

int orderValue(core::BondOrder o) {
    switch (o) {
        case core::BondOrder::Single: return 1;
        case core::BondOrder::Double: return 2;
        case core::BondOrder::Triple: return 3;
        case core::BondOrder::Quadruple: return 4;
        default: return 1;   // aromatic/query count as one, and the formula says so
    }
}

std::vector<std::vector<core::ObjectId> > componentsExcluding(
    const core::Document& doc, const std::vector<core::ObjectId>& atoms,
    const std::set<core::ObjectId>& removed) {
    std::map<core::ObjectId, size_t> index;
    for (size_t i = 0; i < atoms.size(); ++i) index[atoms[i]] = i;
    std::vector<size_t> parent(atoms.size());
    for (size_t i = 0; i < atoms.size(); ++i) parent[i] = i;
    struct UF {
        std::vector<size_t>& p;
        explicit UF(std::vector<size_t>& pp) : p(pp) {}
        size_t find(size_t x) { while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; } return x; }
        void unite(size_t a, size_t b) { a = find(a); b = find(b); if (a != b) p[a] = b; }
    } uf(parent);

    const std::vector<core::BondRecord>& bonds = doc.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i) {
        const core::BondRecord& b = bonds[i];
        if (!b.isAlive()) continue;
        if (removed.count(b.id)) continue;
        std::map<core::ObjectId, size_t>::const_iterator ia = index.find(b.beginAtom);
        std::map<core::ObjectId, size_t>::const_iterator ib = index.find(b.endAtom);
        if (ia == index.end() || ib == index.end()) continue;
        uf.unite(ia->second, ib->second);
    }
    std::map<size_t, std::vector<core::ObjectId> > groups;
    for (size_t i = 0; i < atoms.size(); ++i) groups[uf.find(i)].push_back(atoms[i]);
    std::vector<std::vector<core::ObjectId> > out;
    for (std::map<size_t, std::vector<core::ObjectId> >::iterator it = groups.begin();
         it != groups.end(); ++it) {
        std::sort(it->second.begin(), it->second.end());
        out.push_back(it->second);
    }
    std::sort(out.begin(), out.end(),
              [](const std::vector<core::ObjectId>& x, const std::vector<core::ObjectId>& y) {
                  return x.front() < y.front();
              });
    return out;
}

double cross2(const core::Vec2& a, const core::Vec2& b) {
    return a.x * b.y - a.y * b.x;
}

bool segmentsProperlyCross(const core::Vec2& p, const core::Vec2& p2,
                           const core::Vec2& q, const core::Vec2& q2) {
    const core::Vec2 r = p2 - p;
    const core::Vec2 s = q2 - q;
    const double denom = cross2(r, s);
    if (std::fabs(denom) < 1e-12) return false;
    const core::Vec2 qp = q - p;
    const double t = cross2(qp, s) / denom;
    const double u = cross2(qp, r) / denom;
    return t > 0.0 && t < 1.0 && u > 0.0 && u < 1.0;
}

std::string elementCountText(int element, size_t count) {
    char buf[64];
    if (count == 1) std::snprintf(buf, sizeof(buf), "%s", core::elementSymbol(element));
    else std::snprintf(buf, sizeof(buf), "%s%llu", core::elementSymbol(element),
                       static_cast<unsigned long long>(count));
    return std::string(buf);
}

} // namespace

const char* fragmentOperationName(FragmentOperation op) {
    switch (op) {
        case FragmentOperation::MassFragmentation: return "MassFragmentation";
        case FragmentOperation::Synthesis:         return "Synthesis";
        case FragmentOperation::Retrosynthesis:    return "Retrosynthesis";
        case FragmentOperation::ManualCut:         return "ManualCut";
        case FragmentOperation::ManualConnect:     return "ManualConnect";
    }
    return "?";
}

std::vector<core::ObjectId> bondsCrossedByLine(const core::Document& doc,
                                               const core::Vec2& a, const core::Vec2& b) {
    std::vector<core::ObjectId> out;
    if (!a.finite() || !b.finite()) return out;
    if (core::length(b - a) < 1e-9) return out;
    const std::vector<core::BondRecord>& bonds = doc.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i) {
        const core::BondRecord& bond = bonds[i];
        if (!bond.isAlive()) continue;
        const core::AtomRecord* ba = doc.atoms().alive(bond.beginAtom);
        const core::AtomRecord* bb = doc.atoms().alive(bond.endAtom);
        if (!ba || !bb) continue;
        if (segmentsProperlyCross(a, b, ba->position, bb->position)) out.push_back(bond.id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool ringBondOnly(const core::Document& doc, const std::vector<core::ObjectId>& bonds) {
    if (bonds.empty()) return false;
    for (size_t i = 0; i < bonds.size(); ++i) {
        const core::BondRecord* b = doc.bonds().alive(bonds[i]);
        if (!b) return false;
        std::vector<core::ObjectId> queue;
        std::set<core::ObjectId> seen;
        queue.push_back(b->beginAtom);
        seen.insert(b->beginAtom);
        bool connected = false;
        while (!queue.empty() && !connected) {
            const core::ObjectId u = queue.back();
            queue.pop_back();
            const std::vector<core::ObjectId>& incident = doc.incidentBonds(u);
            for (size_t k = 0; k < incident.size(); ++k) {
                if (incident[k] == bonds[i]) continue;
                const core::BondRecord* e = doc.bonds().alive(incident[k]);
                if (!e) continue;
                const core::ObjectId v = e->beginAtom == u ? e->endAtom : e->beginAtom;
                if (v == b->endAtom) { connected = true; break; }
                if (seen.insert(v).second) queue.push_back(v);
            }
        }
        if (!connected) return false;
    }
    return true;
}

uint8_t homolyticRadicalFor(core::BondOrder order) {
    switch (order) {
        case core::BondOrder::Single: return static_cast<uint8_t>(core::Radical::Doublet);
        case core::BondOrder::Double: return static_cast<uint8_t>(core::Radical::Triplet);
        case core::BondOrder::Triple: return static_cast<uint8_t>(core::Radical::Triplet);
        default: return static_cast<uint8_t>(core::Radical::None);
    }
}

std::string pieceFormula(const core::Document& doc,
                         const std::vector<core::ObjectId>& atoms,
                         chemistry::MassResult* massOut, bool allowNaturalWeight,
                         const std::vector<FragmentRadical>* plannedRadicals) {
    std::set<core::ObjectId> member(atoms.begin(), atoms.end());
    std::map<core::ObjectId, int> radicalElectrons;
    if (plannedRadicals) {
        for (size_t i = 0; i < plannedRadicals->size(); ++i) {
            const uint8_t rad = (*plannedRadicals)[i].radical;
            const int electrons = rad == static_cast<uint8_t>(core::Radical::Triplet) ? 2
                                : rad == static_cast<uint8_t>(core::Radical::Doublet) ? 1 : 0;
            if (electrons > 0) radicalElectrons[(*plannedRadicals)[i].atom] += electrons;
        }
    }
    std::map<int, size_t> counts;
    std::vector<chemistry::MassAtom> massAtoms;
    bool unresolved = false;

    for (size_t i = 0; i < atoms.size(); ++i) {
        const core::AtomRecord* a = doc.atoms().alive(atoms[i]);
        if (!a) continue;
        int bondOrderSum = 0;
        int aromaticBonds = 0;
        const std::vector<core::ObjectId>& incident = doc.incidentBonds(atoms[i]);
        for (size_t k = 0; k < incident.size(); ++k) {
            const core::BondRecord* b = doc.bonds().alive(incident[k]);
            if (!b) continue;
            const core::ObjectId other = b->beginAtom == atoms[i] ? b->endAtom : b->beginAtom;
            if (!member.count(other)) continue;
            bondOrderSum += orderValue(b->order);
            if (b->semantics == core::BondSemantics::Aromatic ||
                b->order == core::BondOrder::Aromatic) ++aromaticBonds;
        }
        counts[a->element] += 1;
        int h = 0;
        if (a->element != 1) {
            const chemistry::HydrogenResult hr =
                chemistry::implicitHydrogensSimple(a->element, a->semantics.charge,
                                                   bondOrderSum, aromaticBonds > 0);
            if (hr.valid && hr.count >= 0) {
                h = hr.count;
                std::map<core::ObjectId, int>::const_iterator re = radicalElectrons.find(atoms[i]);
                if (re != radicalElectrons.end()) {
                    h -= re->second;             // homolytic: one fewer H per unpaired electron
                    if (h < 0) h = 0;
                }
                counts[1] += static_cast<size_t>(h);
            } else {
                unresolved = true;
            }
        }
        massAtoms.push_back(chemistry::MassAtom(a->element, a->semantics.isotope, h));
    }

    std::string formula;
    std::map<int, size_t>::const_iterator c = counts.find(6);
    if (c != counts.end()) formula += elementCountText(6, c->second);
    c = counts.find(1);
    if (c != counts.end()) formula += elementCountText(1, c->second);
    std::vector<std::pair<std::string, size_t> > rest;
    for (std::map<int, size_t>::const_iterator it = counts.begin(); it != counts.end(); ++it) {
        if (it->first == 1 || it->first == 6) continue;
        rest.push_back(std::make_pair(std::string(core::elementSymbol(it->first)), it->second));
    }
    std::sort(rest.begin(), rest.end());
    for (size_t i = 0; i < rest.size(); ++i) {
        char buf[64];
        if (rest[i].second == 1) std::snprintf(buf, sizeof(buf), "%s", rest[i].first.c_str());
        else std::snprintf(buf, sizeof(buf), "%s%llu", rest[i].first.c_str(),
                           static_cast<unsigned long long>(rest[i].second));
        formula += buf;
    }
    if (unresolved) formula += "?";

    if (massOut) {
        *massOut = chemistry::estimateMass(massAtoms, allowNaturalWeight);
        if (unresolved) massOut->ok = false;
    }
    return formula;
}

// ---------------------------------------------------------------------------
// FragmentService
// ---------------------------------------------------------------------------
const char* FragmentService::providerName() const {
    return native_ ? native_->providerName() : "builtin";
}

FragmentCapability FragmentService::capability(FragmentOperation op) const {
    FragmentCapability cap;
    cap.op = op;
    cap.provider = providerName();
    const bool nativeVerified = native_ && native_->verifiedFor(op);
    switch (op) {
        case FragmentOperation::ManualCut:
            cap.provenance = Provenance::OwnImplemented;
            cap.sourceCommand = "";
            cap.note = "explicit bond ids removed; connected components recomputed";
            break;
        case FragmentOperation::ManualConnect:
            cap.provenance = Provenance::OwnImplemented;
            cap.sourceCommand = "";
            cap.note = "explicit atom pairs joined; no reaction inference";
            break;
        case FragmentOperation::MassFragmentation:
            cap.provenance = Provenance::OwnImplemented;
            cap.sourceCommand = "chooseFragmentTool_MassFragmentation";
            cap.note = "user-specified crossings; homolytic radicals; T06 formula and "
                       "estimate mass per side. Not predictive.";
            break;
        case FragmentOperation::Synthesis:
            cap.sourceCommand = "chooseFragmentTool_Synthesis";
            if (nativeVerified) {
                cap.provenance = Provenance::NativeBacked;
                cap.note = "executed on the host through a verified native port";
            } else {
                cap.provenance = Provenance::Unsupported;
                cap.note = "reactant inference requires reaction rules; use ManualConnect "
                           "for an explicit bond add. No verified native port.";
            }
            break;
        case FragmentOperation::Retrosynthesis:
            cap.sourceCommand = "chooseFragmentTool_Retrosynthesis";
            if (nativeVerified) {
                cap.provenance = Provenance::NativeBacked;
                cap.note = "executed on the host through a verified native port";
            } else {
                cap.provenance = Provenance::Unsupported;
                cap.note = "retrosynthetic path search requires reaction rules; use "
                           "ManualCut for an explicit bond break. No verified native port.";
            }
            break;
    }
    return cap;
}

std::vector<FragmentCapability> FragmentService::capabilities() const {
    std::vector<FragmentCapability> out;
    for (int i = 0; i < 5; ++i)
        out.push_back(capability(static_cast<FragmentOperation>(i)));
    return out;
}

core::Error FragmentService::plan(const core::Document& doc,
                                  const FragmentRequest& request,
                                  FragmentPlan* out) const {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null FragmentPlan");
    *out = FragmentPlan();
    out->documentId = doc.id();
    out->revision   = doc.revision();
    out->keepLine   = request.keepLine;

    FragmentCapability cap = capability(request.op);
    out->provenance = cap.provenance;
    out->provider   = cap.provider;

    if (request.op == FragmentOperation::Synthesis ||
        request.op == FragmentOperation::Retrosynthesis) {
        if (cap.provenance == Provenance::NativeBacked && native_) {
            NativeFragmentOutcome outcome;
            core::Error e = native_->execute(request.op, request, &outcome);
            if (!e.ok()) {
                out->error = e;
                out->note = std::string("native port failed: ") + e.message;
                return e;
            }
            out->nativeApplied = outcome.applied;
            out->valid = outcome.applied;
            out->note = std::string("native: ") + outcome.description;
            if (!outcome.applied)
                out->error = core::Error(core::ErrorCode::Unsupported,
                                         "native port declined the operation");
            return core::Error::success();
        }
        out->error = core::Error(core::ErrorCode::Unsupported, cap.note);
        out->note = std::string("Unsupported: ") + cap.note;
        return out->error;
    }

    if (request.op == FragmentOperation::ManualConnect) {
        if (request.connectFrom.size() != request.connectTo.size() ||
            request.connectFrom.empty()) {
            out->error = core::Error(core::ErrorCode::InvalidArgument,
                                     "connectFrom/connectTo must be equal-sized and non-empty");
            return out->error;
        }
        for (size_t i = 0; i < request.connectFrom.size(); ++i) {
            const core::ObjectId a = request.connectFrom[i];
            const core::ObjectId b = request.connectTo[i];
            if (!doc.atoms().alive(a) || !doc.atoms().alive(b)) {
                out->error = core::Error(core::ErrorCode::NotAlive, "connect endpoint is not alive");
                return out->error;
            }
            if (a == b) {
                out->error = core::Error(core::ErrorCode::SelfBond, "cannot connect an atom to itself");
                return out->error;
            }
            if (doc.bondBetween(a, b) != core::kInvalidObjectId) {
                out->error = core::Error(core::ErrorCode::DuplicateBond,
                                         "the two atoms are already bonded");
                return out->error;
            }
            out->bondsToAdd.push_back(BondAdd(a, b, request.connectOrder));
        }
        out->valid = true;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "connect: %llu explicit bond(s) added",
                      static_cast<unsigned long long>(out->bondsToAdd.size()));
        out->note = buf;
        return core::Error::success();
    }

    std::vector<core::ObjectId> cuts = request.cutBonds;
    if (request.op == FragmentOperation::MassFragmentation && cuts.empty()) {
        cuts = bondsCrossedByLine(doc, request.lineStart, request.lineEnd);
        out->crossedBondCount = static_cast<int>(cuts.size());
    } else {
        out->crossedBondCount = static_cast<int>(cuts.size());
    }
    if (cuts.empty()) {
        out->error = core::Error(core::ErrorCode::InvalidArgument,
                                 "no bond was crossed or listed; nothing to break");
        out->note = "no bond crossed";
        return out->error;
    }
    for (size_t i = 0; i < cuts.size(); ++i) {
        const core::BondRecord* b = doc.bonds().alive(cuts[i]);
        if (!b) {
            out->error = core::Error(core::ErrorCode::NotAlive, "cut bond is not alive");
            return out->error;
        }
        out->bondsToRemove.push_back(cuts[i]);
    }
    std::sort(out->bondsToRemove.begin(), out->bondsToRemove.end());
    out->bondsToRemove.erase(std::unique(out->bondsToRemove.begin(), out->bondsToRemove.end()),
                             out->bondsToRemove.end());

    out->ringBondOnly = ringBondOnly(doc, out->bondsToRemove);

    if (request.homolytic) {
        std::map<core::ObjectId, int> unpaired;
        for (size_t i = 0; i < out->bondsToRemove.size(); ++i) {
            const core::BondRecord* b = doc.bonds().alive(out->bondsToRemove[i]);
            if (!b) continue;
            const uint8_t rad = homolyticRadicalFor(b->order);
            const int electrons = rad == static_cast<uint8_t>(core::Radical::Triplet) ? 2
                                : rad == static_cast<uint8_t>(core::Radical::Doublet) ? 1 : 0;
            if (electrons == 0) continue;
            unpaired[b->beginAtom] += electrons;
            unpaired[b->endAtom] += electrons;
        }
        for (std::map<core::ObjectId, int>::const_iterator it = unpaired.begin();
             it != unpaired.end(); ++it) {
            const uint8_t rad = it->second >= 2 ? static_cast<uint8_t>(core::Radical::Triplet)
                                                : static_cast<uint8_t>(core::Radical::Doublet);
            out->radicals.push_back(FragmentRadical(it->first, rad));
        }
    }

    std::vector<core::ObjectId> allAtoms;
    const std::vector<core::AtomRecord>& records = doc.atoms().records();
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].isAlive()) allAtoms.push_back(records[i].id);
    std::set<core::ObjectId> removed(out->bondsToRemove.begin(), out->bondsToRemove.end());
    std::vector<std::vector<core::ObjectId> > comps =
        componentsExcluding(doc, allAtoms, removed);

    const core::Vec2 lineDir = request.lineEnd - request.lineStart;
    for (size_t c = 0; c < comps.size(); ++c) {
        FragmentPiece piece;
        piece.atoms = comps[c];
        piece.formula = pieceFormula(doc, comps[c], &piece.mass, true, &out->radicals);
        if (core::length(lineDir) > 1e-9) {
            const core::Vec2 centre = centroidOf(doc, comps[c]);
            const double side = cross2(lineDir, centre - request.lineStart);
            piece.side = side >= 0.0 ? 0 : 1;
        }
        out->pieces.push_back(piece);
    }
    for (int s = 0; s < 2; ++s) {
        std::vector<core::ObjectId> members;
        for (size_t c = 0; c < comps.size(); ++c)
            if (out->pieces[c].side == s)
                members.insert(members.end(), comps[c].begin(), comps[c].end());
        if (members.empty()) continue;
        out->sides[s].formula =
            pieceFormula(doc, members, &out->sides[s].mass, true, &out->radicals);
        out->sides[s].pieceCount = static_cast<int>(members.size());
    }
    if (out->ringBondOnly)
        out->note = "only a ring bond was crossed: one formula/mass pair per the guide";
    else
        out->note = "explicit cut: components recomputed, homolytic radicals recorded";
    out->valid = true;
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------
core::Command applyFragmentPlanCommand(const FragmentPlan& plan, bool enforceFreshPlan) {
    FragmentPlan copy = plan;
    return core::Command("advanced.fragment.apply",
                         core::kInvalidateStructure | core::kInvalidateSemantics |
                         core::kInvalidateGeometry,
        [copy, enforceFreshPlan](core::CommandContext& ctx) -> core::CommandResult {
            core::Document& doc = ctx.document;
            if (!copy.valid)
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                 "fragment plan is not valid");
            if (copy.nativeApplied)
                return core::CommandResult::noChange();
            if (doc.id() != copy.documentId)
                return core::CommandResult::fail(core::ErrorCode::DocumentMismatch,
                                                 "fragment plan belongs to another document");
            if (enforceFreshPlan && doc.revision() != copy.revision)
                return core::CommandResult::fail(core::ErrorCode::RevisionConflict,
                                                 "fragment plan is stale");
            for (size_t i = 0; i < copy.bondsToRemove.size(); ++i) {
                core::Error e = doc.removeBond(copy.bondsToRemove[i]);
                if (!e.ok() && e.code != core::ErrorCode::NotAlive)
                    return core::CommandResult::fail(e.code, e.message);
            }
            for (size_t i = 0; i < copy.bondsToAdd.size(); ++i) {
                core::ObjectId id = core::kInvalidObjectId;
                core::Error e = doc.addBond(copy.bondsToAdd[i].a, copy.bondsToAdd[i].b, &id);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
                if (copy.bondsToAdd[i].order != core::BondOrder::Single) {
                    core::Error eo = doc.setBondOrder(id, copy.bondsToAdd[i].order);
                    if (!eo.ok()) return core::CommandResult::fail(eo.code, eo.message);
                }
            }
            for (size_t i = 0; i < copy.radicals.size(); ++i) {
                core::AtomRecord rec;
                if (!doc.readAtom(copy.radicals[i].atom, &rec).ok()) continue;
                rec.semantics.radical = copy.radicals[i].radical;
                core::Error e = doc.setAtomSemantics(copy.radicals[i].atom, rec.semantics);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            }
            return core::CommandResult::success(core::kInvalidateStructure |
                                                core::kInvalidateSemantics |
                                                core::kInvalidateGeometry);
        });
}

// ---------------------------------------------------------------------------
// Tool
// ---------------------------------------------------------------------------
FragmentTool::FragmentTool()
    : op_(FragmentOperation::MassFragmentation), origin_(0.0, 0.0),
      current_(0.0, 0.0), pressed_(false) {
    id_ = "r62.tool.fragment.mass";
}

core::CommandResult FragmentTool::beginGesture(core::ToolHost& host,
                                               const core::PointerState& pointer,
                                               core::HitContext& hits) {
    (void)hits;
    pressed_ = true;
    origin_ = pointer.position;
    current_ = pointer.position;
    crossing_.clear();
    status_ = "fragment: drag across the bonds to break";
    host.setStatus(status_);
    return core::CommandResult::noChange();
}

core::CommandResult FragmentTool::updateGesture(core::ToolHost& host,
                                                const core::PointerState& pointer,
                                                core::HitContext& hits) {
    (void)hits;
    if (!pressed_) return core::CommandResult::noChange();
    current_ = pointer.position;
    crossing_ = bondsCrossedByLine(host.document(), origin_, current_);
    core::PreviewScene& preview = host.preview();
    preview.clear();
    for (size_t i = 0; i < crossing_.size(); ++i) {
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Highlight;
        item.subject = core::ObjectRef(core::ObjectKind::Bond, crossing_[i]);
        preview.add(item);
    }
    {
        core::PreviewItem line;
        line.primitive = core::PreviewPrimitive::Segment;
        line.points.push_back(origin_);
        line.points.push_back(current_);
        preview.add(line);
    }
    return core::CommandResult::success(core::kInvalidateView);
}

core::CommandResult FragmentTool::endGesture(core::ToolHost& host,
                                             const core::PointerState& pointer,
                                             core::HitContext& hits) {
    (void)pointer; (void)hits;
    if (!pressed_) return core::CommandResult::noChange();
    pressed_ = false;
    core::PreviewScene& preview = host.preview();
    preview.clear();
    if (crossing_.empty()) {
        status_ = "fragment: no bond crossed";
        host.setStatus(status_);
        return core::CommandResult::noChange();
    }
    FragmentRequest request;
    request.op = op_;
    request.lineStart = origin_;
    request.lineEnd = current_;
    request.cutBonds = crossing_;
    request.homolytic = true;

    FragmentService service(0);
    FragmentPlan plan;
    core::Error pe = service.plan(host.document(), request, &plan);
    if (!pe.ok()) {
        status_ = std::string("fragment: ") + pe.message;
        host.setStatus(status_);
        return core::CommandResult::fail(pe.code, pe.message);
    }
    core::Error e = host.submit(applyFragmentPlanCommand(plan, true), "fragment.cut");
    if (!e.ok()) {
        status_ = std::string("fragment rejected: ") + e.message;
        host.setStatus(status_);
        return core::CommandResult::fail(e.code, e.message);
    }
    committed_ = plan.bondsToRemove;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "fragment: broke %llu bond(s), %llu component(s)",
                  static_cast<unsigned long long>(plan.bondsToRemove.size()),
                  static_cast<unsigned long long>(plan.pieces.size()));
    status_ = buf;
    host.setStatus(status_);
    return core::CommandResult::success(core::kInvalidateStructure |
                                        core::kInvalidateSemantics |
                                        core::kInvalidateGeometry);
}

core::CommandResult FragmentTool::cancelGesture(core::ToolHost& host) {
    pressed_ = false;
    crossing_.clear();
    host.preview().clear();
    status_ = "fragment cancelled";
    host.setStatus(status_);
    return core::CommandResult::noChange();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
core::Error registerFragmentTools(core::ToolRegistry& registry,
                                  FragmentTool* fragmentTool,
                                  PerspectiveTool* perspectiveTool,
                                  ToolRegistration* out) {
    if (out) *out = ToolRegistration();
    struct Row {
        const char* id;
        const char* command;
        const char* name;
        core::ToolBehavior behavior;
        core::PresetTarget target;
    };
    const Row rows[] = {
        { "r62.tool.fragment.mass", "chooseFragmentTool_MassFragmentation",
          "Mass Fragmentation", core::ToolBehavior::Select, core::PresetTarget::Selection },
        { "r62.tool.fragment.cut", "", "Cut selected bonds",
          core::ToolBehavior::Select, core::PresetTarget::Selection },
        { "r62.tool.fragment.connect", "", "Connect atoms",
          core::ToolBehavior::Select, core::PresetTarget::Selection },
        { "r62.tool.perspective.structure", "chooseStructurePerspectiveTool",
          "Structure Perspective", core::ToolBehavior::Perspective,
          core::PresetTarget::Selection }
    };
    const size_t rowCount = sizeof(rows) / sizeof(rows[0]);
    for (size_t i = 0; i < rowCount; ++i) {
        core::ToolPreset preset;
        preset.id = rows[i].id;
        preset.behavior = rows[i].behavior;
        preset.target = rows[i].target;
        preset.displayName = rows[i].name;
        preset.category = "fragments/perspective";
        preset.sourceCommand = rows[i].command;
        preset.implemented = true;
        preset.invalidation = core::kInvalidateView;
        uint32_t presetId = 0;
        core::Error e = registry.registerPreset(preset, &presetId);
        if (!e.ok()) return e;
        core::Error b = registry.bindPlatformCommand(rows[i].id,
            core::platform_command::ToolBand + presetId);
        if (!b.ok()) return b;
        if (out) out->presetsRegistered += 1;
    }
    if (fragmentTool && registry.tool(core::ToolBehavior::Select) == 0) {
        core::Error e = registry.registerTool(core::ToolBehavior::Select, fragmentTool);
        if (e.ok() && out) out->toolsRegistered += 1;
    }
    if (perspectiveTool) {
        core::Error e = registry.registerTool(core::ToolBehavior::Perspective, perspectiveTool);
        if (!e.ok()) return e;
        if (out) out->toolsRegistered += 1;
    }
    return core::Error::success();
}

} // namespace advanced
} // namespace r62
