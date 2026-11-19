// =============================================================================
// re_round62_viewer/next/rings_chains/commands.cpp
//
// T08 - THE ONLY MUTATION ENTRY POINTS this module offers.
//
// Every function returns ONE core::Command. Submitting it through
// core::CommandLog::execute makes the whole ring / chain / sprout ONE
// transaction and therefore ONE undo step. The rules this file enforces:
//
//   R1  one user action == one atomic transaction == one undo step
//   R2  a refused or failed command leaves the document EXACTLY as it was
//       (core::Transaction restores from its own captured baseline, so a bug in
//        this file cannot corrupt the document by computing a wrong inverse)
//   R5  a reused id is never renumbered and a duplicate edge is never created
//
// The command re-validates the cluster against the LIVE document before it
// writes anything. That is why a placement resolved before an undo cannot be
// committed afterwards: the document revision it recorded no longer matches.
// =============================================================================

#include "next/rings_chains/internal.hpp"

#include <cmath>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace r62 {
namespace rings_chains {
namespace commands {

namespace {

// ChemDraw's KekulizationHandler::AutoKekulize rearranges the whole aromatic
// component after a ring edit.  A fused ring at a bridge carbon cannot be
// committed with the preset's fixed alternation: adding the new sigma bond
// would leave the bridge carbon with five bonds unless an existing double bond
// moves somewhere else in the fused system.  Find a perfect matching on the
// carbon skeleton of the connected aromatic component; matched edges are the
// Kekule double bonds, every other C-C edge is single.  This is deliberately a
// graph operation, not a preset pattern, so naphthalene, anthracene and
// phenanthrene all fall out of the same code.
struct KekuleEdge {
    int a, b;
    core::ObjectId existing;
    int cluster;
    bool active;
};

bool autoKekulizeFusedBenzene(core::Document& document, Cluster& cluster, core::ObjectId saturatedAtom, bool* complete) {
    *complete=false;
    std::vector<core::ObjectId> nodeIds;
    std::map<core::ObjectId, int> existingNode;
    std::vector<int> clusterNode(cluster.atoms.size(), -1);

    auto addExisting = [&](core::ObjectId id) -> int {
        std::map<core::ObjectId, int>::iterator found = existingNode.find(id);
        if (found != existingNode.end()) return found->second;
        const core::AtomRecord* atom = document.atoms().alive(id);
        if (!atom || atom->element != 6) return -1;
        const int index = static_cast<int>(nodeIds.size());
        existingNode[id] = index;
        nodeIds.push_back(id);
        return index;
    };

    for (size_t i = 0; i < cluster.atoms.size(); ++i) {
        if (cluster.atoms[i].reuse != core::kInvalidObjectId) {
            const int index = addExisting(cluster.atoms[i].reuse);
            if (index < 0) return false;
            clusterNode[i] = index;
        } else {
            if (cluster.atoms[i].element != 6) return false;
            const int index = static_cast<int>(nodeIds.size());
            clusterNode[i] = index;
            nodeIds.push_back(core::kInvalidObjectId);
        }
    }

    std::vector<KekuleEdge> edges;
    std::set<core::ObjectId> seenBonds;
    std::queue<int> pending;
    std::vector<bool> queued(nodeIds.size(), false);
    for (size_t i = 0; i < cluster.atoms.size(); ++i) {
        if (cluster.atoms[i].reuse == core::kInvalidObjectId) continue;
        const int index = clusterNode[i];
        if (index >= 0 && !queued[static_cast<size_t>(index)]) {
            queued[static_cast<size_t>(index)] = true;
            pending.push(index);
        }
    }
    while (!pending.empty()) {
        const int index = pending.front();
        pending.pop();
        const core::ObjectId id = nodeIds[static_cast<size_t>(index)];
        if (id == core::kInvalidObjectId) continue;
        const std::vector<core::ObjectId>& incident = document.incidentBonds(id);
        for (size_t k = 0; k < incident.size(); ++k) {
            const core::ObjectId bondId = incident[k];
            if (seenBonds.count(bondId)) continue;
            const core::BondRecord* bond = document.bonds().alive(bondId);
            if (!bond) continue;
            const int a = addExisting(bond->beginAtom);
            const int b = addExisting(bond->endAtom);
            if (a < 0 || b < 0) continue;
            queued.resize(nodeIds.size(), false);
            seenBonds.insert(bondId);
            KekuleEdge edge;
            edge.a = a; edge.b = b; edge.existing = bondId; edge.cluster = -1; edge.active = true;
            edges.push_back(edge);
            if (!queued[static_cast<size_t>(a)]) { queued[static_cast<size_t>(a)] = true; pending.push(a); }
            if (!queued[static_cast<size_t>(b)]) { queued[static_cast<size_t>(b)] = true; pending.push(b); }
        }
    }

    for (size_t e = 0; e < cluster.bonds.size(); ++e) {
        if (cluster.bonds[e].reuse != core::kInvalidObjectId) continue;
        const int a = clusterNode[static_cast<size_t>(cluster.bonds[e].fromAtom)];
        const int b = clusterNode[static_cast<size_t>(cluster.bonds[e].toAtom)];
        if (a < 0 || b < 0) return false;
        bool duplicate = false;
        for (size_t k = 0; k < edges.size(); ++k) {
            if ((edges[k].a == a && edges[k].b == b) ||
                (edges[k].a == b && edges[k].b == a)) { duplicate = true; break; }
        }
        if (duplicate) continue;
        KekuleEdge edge;
        edge.a = a; edge.b = b; edge.existing = core::kInvalidObjectId;
        edge.cluster = static_cast<int>(e); edge.active = true;
        edges.push_back(edge);
    }

    // Terminal carbons (methyl etc.) are not part of the Kekule core.  Remove
    // them and their bond from the matching graph; their existing order stays.
    bool pruned = true;
    while (pruned) {
        pruned = false;
        std::vector<int> degree(nodeIds.size(), 0);
        for (size_t e = 0; e < edges.size(); ++e)
            if (edges[e].active) { ++degree[static_cast<size_t>(edges[e].a)]; ++degree[static_cast<size_t>(edges[e].b)]; }
        for (size_t i = 0; i < degree.size(); ++i) {
            if (degree[i] > 0 && degree[i] < 2) {
                for (size_t e = 0; e < edges.size(); ++e)
                    if (edges[e].active && (edges[e].a == static_cast<int>(i) || edges[e].b == static_cast<int>(i)))
                        edges[e].active = false;
                pruned = true;
            }
        }
    }

    std::vector<int> local(nodeIds.size(), -1);
    int count = 0;
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        bool live = false;
        for (size_t e = 0; e < edges.size(); ++e)
            if (edges[e].active && (edges[e].a == static_cast<int>(i) || edges[e].b == static_cast<int>(i))) { live = true; break; }
        if (live) local[i] = count++;
    }
    if (count < 4) return false;
    std::vector<int> degree(static_cast<size_t>(count), 0);
    for (size_t e = 0; e < edges.size(); ++e) if (edges[e].active) {
        const int a = local[static_cast<size_t>(edges[e].a)], b = local[static_cast<size_t>(edges[e].b)];
        if (a >= 0 && b >= 0) { ++degree[static_cast<size_t>(a)]; ++degree[static_cast<size_t>(b)]; }
    }

    // Benzenoid skeletons are bipartite. Augmenting paths find a maximum
    // matching without an exponential search or a 62-atom bitmask limit.
    std::vector<std::vector<int>> adjacent(static_cast<size_t>(count));
    for (int preferred=1;preferred>=0;--preferred) for (const auto& edge:edges) {
        if (!edge.active) continue;
        const auto* old=document.bonds().alive(edge.existing);
        const bool wasDouble=old && old->order==core::BondOrder::Double;
        if (wasDouble!=(preferred!=0)) continue;
        const int a=local[edge.a],b=local[edge.b];
        if(a<0 || b<0)continue;
        adjacent[a].push_back(b);adjacent[b].push_back(a);
    }
    std::vector<int> colour(count,-1),match(count,-1);
    for(int root=0;root<count;++root)if(colour[root]<0){
        std::queue<int> q;q.push(root);colour[root]=0;
        while(!q.empty()){
            int a=q.front();q.pop();
            for(int b:adjacent[a]){
                if(colour[b]<0){colour[b]=1-colour[a];q.push(b);}
                else if(colour[b]==colour[a])return false;
            }
        }
    }
    auto solve=[&](int excluded)->bool {
        std::fill(match.begin(),match.end(),-1);
        int pairs=0;
        std::vector<unsigned char> seen(count,0);
        std::function<bool(int)> augment=[&](int a)->bool {
            for(int b:adjacent[a]){
                if(b==excluded || seen[b])continue;
                seen[b]=1;
                if(match[b]<0 || augment(match[b])){match[a]=b;match[b]=a;return true;}
            }
            return false;
        };
        for(int a=0;a<count;++a)if(colour[a]==0 && a!=excluded){
            std::fill(seen.begin(),seen.end(),0);
            if(augment(a))++pairs;
        }
        return pairs*2==count-(excluded>=0?1:0);
    };
    // Always try the entire skeleton FIRST. A bay bridgehead is not a
    // mandatory saturated carbon when the completed system has a Kekule form.
    bool solvedMatch=solve(-1);
    *complete=solvedMatch;
    if(!solvedMatch && (count&1)){
        auto found=existingNode.find(saturatedAtom);
        if(found!=existingNode.end()){
            int candidate=local[found->second];
            if(candidate>=0)solvedMatch=solve(candidate);
        }
        for(int u=0;u<count && !solvedMatch;++u)
            if(degree[u]==2)solvedMatch=solve(u);
    }
    if (!solvedMatch) return false;

    for (size_t e = 0; e < edges.size(); ++e) {
        if (!edges[e].active) continue;
        const int a = local[static_cast<size_t>(edges[e].a)];
        const int b = local[static_cast<size_t>(edges[e].b)];
        if (a < 0 || b < 0) continue;
        const core::BondOrder order = (match[static_cast<size_t>(a)] == b)
            ? core::BondOrder::Double : core::BondOrder::Single;
        if (edges[e].existing != core::kInvalidObjectId) {
            const core::BondRecord* current = document.bonds().alive(edges[e].existing);
            if (current && current->order != order) {
                if (!document.setBondOrder(edges[e].existing, order).ok()) return false;
            }
        } else if (edges[e].cluster >= 0) {
            cluster.bonds[static_cast<size_t>(edges[e].cluster)].order = order;
        }
    }
    return true;
}

// The cluster commit body. Runs inside the transaction the CommandLog opened.
core::CommandResult runCluster(core::CommandContext& ctx,
                               const Cluster& cluster,
                               std::vector<core::ObjectId>* outAtoms,
                               std::vector<core::ObjectId>* outBonds) {
    if (outAtoms) outAtoms->clear();
    if (outBonds) outBonds->clear();

    std::string detail;
    const FuseReject reject = validateCluster(ctx.document, cluster, &detail);
    if (reject != FuseReject::None) {
        const core::ErrorCode code =
            (reject == FuseReject::DuplicateEdge) ? core::ErrorCode::DuplicateBond :
            (reject == FuseReject::SelfBond)      ? core::ErrorCode::SelfBond :
            (reject == FuseReject::DocumentMismatch) ? core::ErrorCode::DocumentMismatch :
            (reject == FuseReject::OverValence)   ? core::ErrorCode::InvalidSemantics :
            (reject == FuseReject::AnchorMissing) ? core::ErrorCode::NotFound :
                                                    core::ErrorCode::InvalidGeometry;
        return core::CommandResult::fail(code, detail);
    }

    // 1. Resolve every cluster atom to a document id. A reused atom keeps its
    //    id AND its stored position: a ring must never move the atom it fuses to.
    std::vector<core::ObjectId> resolved(cluster.atoms.size(), core::kInvalidObjectId);
    for (size_t i = 0; i < cluster.atoms.size(); ++i) {
        if (cluster.atoms[i].reuse != core::kInvalidObjectId) {
            resolved[i] = cluster.atoms[i].reuse;
            continue;
        }
        core::ObjectId created = core::kInvalidObjectId;
        const core::Error e = ctx.document.addAtom(cluster.atoms[i].position,
                                                   cluster.atoms[i].element, &created);
        if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
        resolved[i] = created;
        if (outAtoms) outAtoms->push_back(created);
    }

    // 2. Edges. A reused edge is recorded and NOT re-created; a new edge is
    //    created with the record's order and display. Aromatic semantics is
    //    applied here so benzene is never stored as a plain Kekule ring with no
    //    indication that it is aromatic.
    for (size_t e = 0; e < cluster.bonds.size(); ++e) {
        const ClusterBond& cb = cluster.bonds[e];
        const core::ObjectId a = resolved[static_cast<size_t>(cb.fromAtom)];
        const core::ObjectId b = resolved[static_cast<size_t>(cb.toAtom)];
        if (a == core::kInvalidObjectId || b == core::kInvalidObjectId || a == b)
            return core::CommandResult::fail(core::ErrorCode::InvalidGeometry,
                                             "edge endpoint did not resolve");
        if (cb.reuse != core::kInvalidObjectId) {
            if (outBonds) outBonds->push_back(cb.reuse);
            continue;
        }
        const core::ObjectId existing = ctx.document.bondBetween(a, b);
        if (existing != core::kInvalidObjectId) {
            // The edge appeared between validation and here. Refuse; the
            // transaction rolls back and nothing is left half-built.
            return core::CommandResult::fail(core::ErrorCode::DuplicateBond,
                                             "an edge between these atoms already exists");
        }
        core::BondRecord rec;
        rec.beginAtom = a;
        rec.endAtom = b;
        rec.order = cb.order;
        rec.display = cb.display;
        rec.semantics = core::BondSemantics::Normal;
        rec.stereo = core::BondStereo::None;
        core::ObjectId bid = core::kInvalidObjectId;
        const core::Error be = ctx.document.addBondRecord(rec, &bid);
        if (!be.ok()) return core::CommandResult::fail(be.code, be.message);
        if (outBonds) outBonds->push_back(bid);
    }

    // The document gained structure: the geometry AND the topological index are
    // stale. Appearance too, because atom labels appear on a new carbon.
    return core::CommandResult::success(core::kInvalidateStructure |
                                        core::kInvalidateGeometry |
                                        core::kInvalidateSemantics |
                                        core::kInvalidateAppearance);
}

} // namespace

// -----------------------------------------------------------------------------
// createClusterCommand
// -----------------------------------------------------------------------------
core::Command createClusterCommand(const Cluster& cluster,
                                   std::vector<core::ObjectId>* outAtoms,
                                   std::vector<core::ObjectId>* outBonds) {
    // Copy everything: a Command is replayed verbatim by Ctrl+Y (Repeat), so it
    // must not capture a pointer into the caller's stack.
    Cluster copy(cluster);
    std::vector<core::ObjectId>* atoms = outAtoms;
    std::vector<core::ObjectId>* bonds = outBonds;
    return core::Command("rings_chains.cluster.create",
                         core::kInvalidateStructure | core::kInvalidateGeometry |
                         core::kInvalidateSemantics | core::kInvalidateAppearance,
        [copy, atoms, bonds](core::CommandContext& ctx) -> core::CommandResult {
            return runCluster(ctx, copy, atoms, bonds);
        });
}

core::Command createRingCommand(const RingPlacement& placement,
                                std::vector<core::ObjectId>* outAllAtoms,
                                std::vector<core::ObjectId>* outAllBonds, std::string* notice) {
    if(notice)notice->clear();
    Cluster copy = clusterFromRingPlacement(placement);
    const bool pathFusion = placement.reuseBond.size() > 1 &&
                            placement.reuseBond[1] != core::kInvalidObjectId;
    if (pathFusion) for (size_t e = 0; e < copy.bonds.size(); ++e)
        if (copy.bonds[e].reuse == core::kInvalidObjectId) copy.bonds[e].order = core::BondOrder::Single;
    const core::ObjectId saturatedAtom=(pathFusion && placement.reusedAtomCount==3)?placement.reuseAtom[1]:core::kInvalidObjectId;
    const bool aromatic = (placement.geometry.topology == RingTopologyClass::Aromatic) &&
                          (placement.preset == RingPresetId::Benzene);
    const bool fusedBenzene = placement.preset == RingPresetId::Benzene &&
                              !placement.reuseBond.empty() &&
                              placement.reuseBond[0] != core::kInvalidObjectId;
    std::vector<core::ObjectId>* atomsOut = outAllAtoms;
    std::vector<core::ObjectId>* bondsOut = outAllBonds;

    // Every ring atom that is NEW is created by runCluster(); the ones that are
    // REUSED keep their ids. To report the WHOLE ring membership the command
    // needs both, so it records the created ids by index and fills the reused
    // ones from the cluster itself.
    return core::Command("rings_chains.ring.create",
                         core::kInvalidateStructure | core::kInvalidateGeometry |
                         core::kInvalidateSemantics | core::kInvalidateAppearance,
        [copy, aromatic, fusedBenzene, saturatedAtom, atomsOut, bondsOut, notice](core::CommandContext& ctx) -> core::CommandResult {
            std::vector<core::ObjectId> created;
            std::vector<core::ObjectId> reusedThenCreated;
            Cluster working = copy;
            if (fusedBenzene) {
                bool complete=false;
                if(autoKekulizeFusedBenzene(ctx.document, working, saturatedAtom, &complete)) {
                    working.documentRevision=ctx.document.revision();
                    if(notice && !complete)*notice=u8"已补环；此骨架不能完整分配交替双键，仍有非共轭碳。";
                } else {
                    return core::CommandResult::fail(core::ErrorCode::InvalidGeometry,
                        u8"此骨架无法自动完成苯环双键分配；请检查连接、原子类型，或使用普通环工具。" );
                }
            }
            core::CommandResult r = runCluster(ctx, working, &created, &reusedThenCreated);
            if (!r.ok()) return r;
            // Split the edge output back into (reused, created) in cluster edge
            // order. runCluster emits the reused id for an edge it did not have
            // to create, so counting the reused edges first recovers the split
            // exactly.
            std::vector<core::ObjectId> createdBonds;
            {
                size_t reusedEdges = 0;
                for (size_t e = 0; e < copy.bonds.size(); ++e)
                    if (copy.bonds[e].reuse != core::kInvalidObjectId) ++reusedEdges;
                for (size_t k = reusedEdges; k < reusedThenCreated.size(); ++k)
                    createdBonds.push_back(reusedThenCreated[k]);
            }

            // runCluster pushes created atom ids in cluster index order, so they
            // can be consumed in order to rebuild the per-index mapping.
            std::vector<core::ObjectId> byIndex(copy.atoms.size(), core::kInvalidObjectId);
            size_t next = 0;
            for (size_t i = 0; i < copy.atoms.size(); ++i) {
                if (copy.atoms[i].reuse != core::kInvalidObjectId) {
                    byIndex[i] = copy.atoms[i].reuse;
                } else {
                    if (next < created.size()) byIndex[i] = created[next++];
                }
            }

            // Aromatic semantics: written on the NEW edges of a benzene ring
            // through the public setter, so a persistence layer can tell an
            // aromatic ring from a plain Kekule drawing.
            if (aromatic) {
                for (size_t e = 0; e < createdBonds.size(); ++e) {
                    const core::BondRecord* br = ctx.document.bonds().alive(createdBonds[e]);
                    if (!br) continue;
                    if (br->order != core::BondOrder::Double) continue;
                    ctx.document.setBondSemantics(createdBonds[e],
                                                  core::BondSemantics::Aromatic);
                }
            }

            if (atomsOut) *atomsOut = byIndex;
            if (bondsOut) {
                // The complete edge set of the ring: reused edges first (in edge
                // order) then the created ones, reconstructed per edge index.
                std::vector<core::ObjectId> byEdge(copy.bonds.size(), core::kInvalidObjectId);
                size_t bnext = 0;
                for (size_t e = 0; e < copy.bonds.size(); ++e) {
                    if (copy.bonds[e].reuse != core::kInvalidObjectId) {
                        byEdge[e] = copy.bonds[e].reuse;
                    } else {
                        if (bnext < createdBonds.size()) byEdge[e] = createdBonds[bnext++];
                    }
                }
                *bondsOut = byEdge;
            }
            return r;
        });
}

core::Command createChainCommand(const ChainPlan& plan,
                                 std::vector<core::ObjectId>* outAllAtoms,
                                 std::vector<core::ObjectId>* outAllBonds) {
    // The cluster's atom 0 is the ORIGIN, which is either the caller's existing
    // anchor (reused, so it is not reported as new) or a brand-new atom when the
    // chain started on empty canvas. Both cases must be handled: the caller of
    // createChainCommand asks for the CHAIN's own atoms, which is what the plan
    // lists, so the origin slot is reported separately.
    Cluster cluster = clusterFromChainPlan(plan);
    const bool originIsNew = (plan.originAtom == core::kInvalidObjectId);
    Cluster copy(cluster);
    std::vector<core::ObjectId>* atomsOut = outAllAtoms;
    std::vector<core::ObjectId>* bondsOut = outAllBonds;
    return core::Command("rings_chains.chain.create",
                         core::kInvalidateStructure | core::kInvalidateGeometry |
                         core::kInvalidateSemantics | core::kInvalidateAppearance,
        [copy, originIsNew, atomsOut, bondsOut](core::CommandContext& ctx) -> core::CommandResult {
            std::vector<core::ObjectId> createdAtoms;
            std::vector<core::ObjectId> allBonds;
            core::CommandResult r = runCluster(ctx, copy, &createdAtoms, &allBonds);
            if (!r.ok()) return r;
            if (atomsOut) {
                atomsOut->clear();
                // When the origin was a free point it became cluster atom 0 and
                // was created too; the chain's atoms are everything after it.
                const size_t skip = originIsNew ? 1 : 0;
                for (size_t k = skip; k < createdAtoms.size(); ++k)
                    atomsOut->push_back(createdAtoms[k]);
            }
            if (bondsOut) *bondsOut = allBonds;
            return r;
        });
}

// -----------------------------------------------------------------------------
// reverseChainCommand
// -----------------------------------------------------------------------------
core::Command reverseChainCommand(const std::vector<core::ObjectId>& bonds) {
    std::vector<core::ObjectId> copy(bonds);
    return core::Command("rings_chains.chain.reverse",
                         core::kInvalidateGeometry | core::kInvalidateAppearance,
        [copy](core::CommandContext& ctx) -> core::CommandResult {
            for (size_t i = 0; i < copy.size(); ++i) {
                const core::BondRecord* b = ctx.document.bonds().alive(copy[i]);
                if (!b) continue;
                const core::ObjectId begin = b->beginAtom;
                const core::ObjectId end = b->endAtom;
                const core::Error e = ctx.document.setBondEndpoints(copy[i], end, begin);
                if (!e.ok()) return core::CommandResult::fail(e.code, e.message);
            }
            return core::CommandResult::success(core::kInvalidateGeometry |
                                                core::kInvalidateAppearance);
        });
}

} // namespace commands
} // namespace rings_chains
} // namespace r62