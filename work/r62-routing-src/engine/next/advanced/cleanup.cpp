#include "next/advanced/cleanup.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace r62 {
namespace advanced {

namespace {

const double kPi = 3.14159265358979323846;

size_t utf8CodePoints(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}

struct EdgeRef { size_t a; size_t b; };

// Iterative depth-first spanning forest. A 5,000-atom chain must not consume
// 5,000 C++ stack frames, so the walk uses an explicit stack.
//
// Non-tree edges are then classified from the FINISHED parent array, which is
// both simpler and safer than decision-making during the walk: an undirected
// edge is a tree edge iff one endpoint is the other's parent in the forest.
void buildSpanningTree(size_t n, const std::vector<EdgeRef>& edges,
                       std::vector<int>& parent, std::vector<int>& depth,
                       std::vector< std::vector<std::pair<size_t, size_t> > >& adj,
                       std::vector< std::pair<size_t, size_t> >& nonTree) {
    parent.assign(n, -1);
    depth.assign(n, -1);
    nonTree.clear();
    std::vector<char> visited(n, 0);
    std::vector<size_t> stack;
    std::vector<size_t> cursorStack;

    for (size_t start = 0; start < n; ++start) {
        if (visited[start]) continue;
        visited[start] = 1;
        depth[start] = 0;
        stack.push_back(start);
        cursorStack.push_back(0);
        while (!stack.empty()) {
            const size_t u = stack.back();
            if (cursorStack.back() >= adj[u].size()) {
                stack.pop_back();
                cursorStack.pop_back();
                continue;
            }
            const size_t v = adj[u][cursorStack.back()].first;
            ++cursorStack.back();
            if (v == u || visited[v]) continue;
            visited[v] = 1;
            parent[v] = static_cast<int>(u);
            depth[v] = depth[u] + 1;
            stack.push_back(v);
            cursorStack.push_back(0);
        }
    }

    for (size_t e = 0; e < edges.size(); ++e) {
        const size_t a = edges[e].a;
        const size_t b = edges[e].b;
        if (parent[b] == static_cast<int>(a) || parent[a] == static_cast<int>(b)) continue;
        nonTree.push_back(std::make_pair(a, b));
    }
}

// The fundamental cycle of a non-tree edge (u,v): u -> ... -> lca -> ... -> v.
std::vector<size_t> fundamentalCycle(size_t u, size_t v,
                                     const std::vector<int>& parent,
                                     const std::vector<int>& depth) {
    std::vector<size_t> up, down;
    size_t a = u, b = v;
    while (depth[a] > depth[b]) { up.push_back(a); a = static_cast<size_t>(parent[a]); }
    while (depth[b] > depth[a]) { down.push_back(b); b = static_cast<size_t>(parent[b]); }
    while (a != b) {
        up.push_back(a);
        down.push_back(b);
        a = static_cast<size_t>(parent[a]);
        b = static_cast<size_t>(parent[b]);
    }
    up.push_back(a);
    std::vector<size_t> cycle = up;
    for (size_t i = down.size(); i > 0; --i) cycle.push_back(down[i - 1]);
    return cycle;
}

struct Work {
    std::vector<core::Vec2> pos;
    std::vector<char>       movable;
    std::vector<core::ObjectId> ids;
    std::map<core::ObjectId, size_t> index;
};

} // namespace

bool CleanupPlan::plannedPosition(core::ObjectId id, core::Vec2* out) const {
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (atoms[i] != id) continue;
        if (out) *out = positions[i];
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// planCleanup
// ---------------------------------------------------------------------------
core::Error planCleanup(const LayoutInput& input, const CleanupOptions& options,
                        CleanupPlan* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null CleanupPlan");
    *out = CleanupPlan();
    out->documentId      = input.documentId;
    out->revision        = input.revision;
    out->structureSerial = input.structureSerial;

    if (options.ringMinSize < 3 || options.ringMaxSize < options.ringMinSize)
        return core::Error(core::ErrorCode::InvalidArgument, "invalid ring size range");
    if (options.overlapIterations < 0)
        return core::Error(core::ErrorCode::InvalidArgument, "negative iterations");

    const size_t n = input.atoms.size();
    const double bondLength = options.idealBondLength > 0.0
                                  ? options.idealBondLength
                                  : input.defaultBondLength;
    const double minSep = options.minAtomSeparation > 0.0
                              ? options.minAtomSeparation
                              : 0.62 * bondLength;
    const double spacing = options.componentSpacing > 0.0
                               ? options.componentSpacing
                               : 2.0 * bondLength;

    Work w;
    w.pos.resize(n);
    w.movable.assign(n, 1);
    w.ids.resize(n);
    std::set<core::ObjectId> scope;
    for (size_t i = 0; i < options.scope.size(); ++i) scope.insert(options.scope[i]);

    for (size_t i = 0; i < n; ++i) {
        w.ids[i] = input.atoms[i].id;
        w.pos[i] = input.atoms[i].position;
        w.index[input.atoms[i].id] = i;
        const bool inScope = scope.empty() || scope.count(input.atoms[i].id) != 0;
        const bool fixed = !inScope || input.atoms[i].locked ||
                           (input.atoms[i].anchor && options.preserveAnchors);
        w.movable[i] = fixed ? 0 : 1;
        if (input.atoms[i].anchor && options.preserveAnchors) out->anchors.push_back(w.ids[i]);
        if (input.atoms[i].locked) out->locked.push_back(w.ids[i]);
    }

    // --- adjacency ---------------------------------------------------------
    std::vector<EdgeRef> edges;
    std::vector< std::vector<std::pair<size_t, size_t> > > adj(n);
    std::vector<int> degree(n, 0);
    for (size_t e = 0; e < input.bonds.size(); ++e) {
        const LayoutBond& b = input.bonds[e];
        std::map<core::ObjectId, size_t>::const_iterator ia = w.index.find(b.a);
        std::map<core::ObjectId, size_t>::const_iterator ib = w.index.find(b.b);
        if (ia == w.index.end() || ib == w.index.end()) continue;
        if (ia->second == ib->second) continue;   // self bond
        EdgeRef er;
        er.a = ia->second;
        er.b = ib->second;
        const size_t id = edges.size();
        edges.push_back(er);
        adj[er.a].push_back(std::make_pair(er.b, id));
        adj[er.b].push_back(std::make_pair(er.a, id));
        degree[er.a] += 1;
        degree[er.b] += 1;
    }
    for (size_t i = 0; i < n; ++i) std::sort(adj[i].begin(), adj[i].end());
    for (size_t i = 0; i < n; ++i) {
        for (size_t k = 0; k + 1 < adj[i].size(); ++k) {
            if (adj[i][k].first == adj[i][k + 1].first)
                return core::Error(core::ErrorCode::DuplicateBond,
                                   "snapshot contains a duplicate bond");
        }
    }

    // --- ring idealisation --------------------------------------------------
    std::set<std::string> seenCycles;
    if (options.idealizeRings && n >= 3) {
        std::vector<int> parent, depth;
        std::vector< std::pair<size_t, size_t> > nonTree;
        buildSpanningTree(n, edges, parent, depth, adj, nonTree);
        std::sort(nonTree.begin(), nonTree.end());
        nonTree.erase(std::unique(nonTree.begin(), nonTree.end()), nonTree.end());

        for (size_t c = 0; c < nonTree.size(); ++c) {
            std::vector<size_t> cycle =
                fundamentalCycle(nonTree[c].first, nonTree[c].second, parent, depth);
            if (cycle.size() < 2) continue;
            // Deduplicate by vertex set: different non-tree edges of a fused
            // system can describe the same ring.
            std::vector<size_t> sorted(cycle);
            std::sort(sorted.begin(), sorted.end());
            std::string key;
            {
                char buf[24];
                for (size_t i = 0; i < sorted.size(); ++i) {
                    std::snprintf(buf, sizeof(buf), "%u,", static_cast<unsigned>(sorted[i]));
                    key += buf;
                }
            }
            if (!seenCycles.insert(key).second) continue;
            if (cycle.size() < static_cast<size_t>(options.ringMinSize) ||
                cycle.size() > static_cast<size_t>(options.ringMaxSize)) continue;

            // Only an ISOLATED ring is idealised: every member has degree equal
            // to its two ring neighbours. Fused/spiro rings are reported and
            // left untouched.
            bool isolated = true;
            for (size_t i = 0; i < cycle.size(); ++i)
                if (degree[cycle[i]] != 2) { isolated = false; break; }
            if (!isolated) { ++out->skippedFusedRingCount; continue; }
            bool hasFixed = false;
            for (size_t i = 0; i < cycle.size(); ++i)
                if (!w.movable[cycle[i]]) { hasFixed = true; break; }
            if (hasFixed) { ++out->skippedFusedRingCount; continue; }

            // Distinct existing positions are required to read an orientation.
            core::Vec2 centroid(0.0, 0.0);
            for (size_t i = 0; i < cycle.size(); ++i) centroid = centroid + w.pos[cycle[i]];
            centroid = core::Vec2(centroid.x / static_cast<double>(cycle.size()),
                                  centroid.y / static_cast<double>(cycle.size()));

            std::vector< std::pair<double, size_t> > byAngle;
            bool degenerate = false;
            for (size_t i = 0; i < cycle.size(); ++i) {
                const core::Vec2 d = w.pos[cycle[i]] - centroid;
                if (core::length(d) < 1e-9) { degenerate = true; break; }
                byAngle.push_back(std::make_pair(core::angleOf(d), cycle[i]));
            }
            if (degenerate) { ++out->skippedFusedRingCount; continue; }
            // Sort by angle with the index as tie-break so the order is total.
            std::sort(byAngle.begin(), byAngle.end());

            const double startAngle = byAngle[0].first;
            // Detect the winding of the original ring from the sorted order
            // relative to the ascending-angle order: sorting already gives
            // counter-clockwise-in-math order, so winding is always +1 here.
            const double radius =
                bondLength / (2.0 * std::sin(kPi / static_cast<double>(cycle.size())));
            for (size_t k = 0; k < byAngle.size(); ++k) {
                const double a = startAngle + 360.0 * static_cast<double>(k) /
                                                static_cast<double>(byAngle.size());
                w.pos[byAngle[k].second] = centroid + core::direction(a) * radius;
                out->ringAtoms.push_back(w.ids[byAngle[k].second]);
            }
            ++out->idealisedRingCount;
        }
        std::sort(out->ringAtoms.begin(), out->ringAtoms.end());
        out->ringAtoms.erase(std::unique(out->ringAtoms.begin(), out->ringAtoms.end()),
                             out->ringAtoms.end());
    }

    // --- overlap resolution --------------------------------------------------
    if (options.resolveOverlaps && minSep > 0.0 && n > 1) {
        const double cell = minSep > 1e-9 ? minSep : 1.0;
        for (int iter = 0; iter < options.overlapIterations; ++iter) {
            std::map< std::pair<long, long>, std::vector<size_t> > grid;
            for (size_t i = 0; i < n; ++i) {
                if (!w.movable[i]) continue;
                const long cx = static_cast<long>(std::floor(w.pos[i].x / cell));
                const long cy = static_cast<long>(std::floor(w.pos[i].y / cell));
                grid[std::make_pair(cx, cy)].push_back(i);
            }
            std::vector<core::Vec2> disp(n, core::Vec2(0.0, 0.0));
            bool any = false;
            for (std::map< std::pair<long, long>, std::vector<size_t> >::const_iterator it =
                     grid.begin(); it != grid.end(); ++it) {
                for (long dx = -1; dx <= 1; ++dx) {
                    for (long dy = -1; dy <= 1; ++dy) {
                        std::map< std::pair<long, long>, std::vector<size_t> >::const_iterator jt =
                            grid.find(std::make_pair(it->first.first + dx,
                                                     it->first.second + dy));
                        if (jt == grid.end()) continue;
                        if (dx < 0 || (dx == 0 && dy < 0)) continue;   // each cell pair once
                        if (dx == 0 && dy == 0) {
                            // within the same cell: all pairs
                            const std::vector<size_t>& v = it->second;
                            for (size_t a = 0; a < v.size(); ++a) {
                                for (size_t b = a + 1; b < v.size(); ++b) {
                                    const size_t i = v[a], j = v[b];
                                    core::Vec2 d = w.pos[j] - w.pos[i];
                                    double dist = core::length(d);
                                    if (dist >= minSep) continue;
                                    if (dist < 1e-12) {
                                        const double ang =
                                            static_cast<double>((w.ids[i] * 2654435761u) % 360u);
                                        d = core::direction(ang);
                                        dist = 1.0;
                                    }
                                    const core::Vec2 push =
                                        d * ((minSep - dist) / dist * 0.5);
                                    disp[i] = disp[i] - push;
                                    disp[j] = disp[j] + push;
                                    any = true;
                                }
                            }
                        } else {
                            const std::vector<size_t>& va = it->second;
                            const std::vector<size_t>& vb = jt->second;
                            for (size_t a = 0; a < va.size(); ++a) {
                                for (size_t b = 0; b < vb.size(); ++b) {
                                    const size_t i = va[a], j = vb[b];
                                    if (i == j) continue;
                                    core::Vec2 d = w.pos[j] - w.pos[i];
                                    double dist = core::length(d);
                                    if (dist >= minSep) continue;
                                    if (dist < 1e-12) {
                                        const double ang =
                                            static_cast<double>((w.ids[i] * 2654435761u) % 360u);
                                        d = core::direction(ang);
                                        dist = 1.0;
                                    }
                                    const core::Vec2 push =
                                        d * ((minSep - dist) / dist * 0.5);
                                    disp[i] = disp[i] - push;
                                    disp[j] = disp[j] + push;
                                    any = true;
                                }
                            }
                        }
                    }
                }
            }
            if (!any) break;
            for (size_t i = 0; i < n; ++i) {
                if (!w.movable[i]) continue;
                if (disp[i].x == 0.0 && disp[i].y == 0.0) continue;
                w.pos[i] = w.pos[i] + disp[i];
                ++out->overlapPairsResolved;
            }
        }
    }

    // --- text avoidance ------------------------------------------------------
    if (options.avoidText && !input.texts.empty()) {
        struct InkBox { double minx, maxx, miny, maxy; };
        std::vector<InkBox> boxes;
        for (size_t t = 0; t < input.texts.size(); ++t) {
            const LayoutText& txt = input.texts[t];
            if (txt.locked) continue;
            const double size = txt.size > 0.0 ? txt.size : input.defaultTextSize;
            const size_t chars = utf8CodePoints(txt.text);
            const double width = 0.62 * size * static_cast<double>(chars > 0 ? chars : 1);
            InkBox box;
            box.minx = txt.position.x;
            box.maxx = txt.position.x + width;
            box.miny = txt.position.y - size;
            box.maxy = txt.position.y;
            boxes.push_back(box);
        }
        for (size_t i = 0; i < n; ++i) {
            if (!w.movable[i]) continue;
            for (size_t b = 0; b < boxes.size(); ++b) {
                const InkBox& box = boxes[b];
                if (w.pos[i].x < box.minx || w.pos[i].x > box.maxx) continue;
                if (w.pos[i].y < box.miny || w.pos[i].y > box.maxy) continue;
                const double dl = w.pos[i].x - box.minx;
                const double dr = box.maxx - w.pos[i].x;
                const double du = w.pos[i].y - box.miny;
                const double dd = box.maxy - w.pos[i].y;
                const double m = std::min(std::min(dl, dr), std::min(du, dd));
                if (m == dl)      w.pos[i].x = box.minx - 0.5;
                else if (m == dr) w.pos[i].x = box.maxx + 0.5;
                else if (m == du) w.pos[i].y = box.miny - 0.5;
                else              w.pos[i].y = box.maxy + 0.5;
                ++out->textAvoidanceMoves;
            }
        }
    }

    // --- arrange components --------------------------------------------------
    if (options.arrangeComponents) {
        std::vector<Component> comps = atomComponents(input);
        // A component that contains an immovable atom stays put; the others are
        // laid out left to right after it.
        double cursorX = 0.0;
        double centreY = 0.0;
        bool haveCursor = false;
        for (size_t c = 0; c < comps.size(); ++c) {
            const core::Box box = componentBounds(input, comps[c]);
            bool anchored = false;
            for (size_t i = 0; i < comps[c].atoms.size(); ++i) {
                const LayoutAtom* a = findAtom(input, comps[c].atoms[i]);
                if (!a) continue;
                if (a->locked || (a->anchor && options.preserveAnchors)) { anchored = true; break; }
            }
            if (anchored) {
                if (!haveCursor) { cursorX = box.max.x + spacing; centreY = (box.min.y + box.max.y) * 0.5; haveCursor = true; }
                else cursorX = std::max(cursorX, box.max.x + spacing);
                continue;
            }
            if (!haveCursor) {
                cursorX = 0.0;
                centreY = (box.min.y + box.max.y) * 0.5;
                haveCursor = true;
            }
            const double cy = (box.min.y + box.max.y) * 0.5;
            for (size_t i = 0; i < comps[c].atoms.size(); ++i) {
                std::map<core::ObjectId, size_t>::const_iterator f =
                    w.index.find(comps[c].atoms[i]);
                if (f == w.index.end() || !w.movable[f->second]) continue;
                const core::Vec2 p = w.pos[f->second];
                w.pos[f->second] = core::Vec2(p.x - box.min.x + cursorX,
                                              p.y - cy + centreY);
            }
            cursorX += (box.max.x - box.min.x) + spacing;
            out->componentsRepresentative.push_back(comps[c].atoms.empty()
                                                        ? core::kInvalidObjectId
                                                        : comps[c].atoms.front());
        }
    }

    // --- recentre -------------------------------------------------------------
    if (options.recentre) {
        core::Vec2 sum(0.0, 0.0);
        size_t count = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!w.movable[i]) continue;
            sum = sum + w.pos[i];
            ++count;
        }
        if (count > 0) {
            const core::Vec2 centroid(sum.x / static_cast<double>(count),
                                      sum.y / static_cast<double>(count));
            const core::Vec2 delta = options.pivot - centroid;
            if (delta.x != 0.0 || delta.y != 0.0)
                for (size_t i = 0; i < n; ++i)
                    if (w.movable[i]) w.pos[i] = w.pos[i] + delta;
        }
    }

    // --- emit ------------------------------------------------------------------
    out->atoms.resize(n);
    out->positions.resize(n);
    for (size_t i = 0; i < n; ++i) {
        out->atoms[i] = w.ids[i];
        out->positions[i] = w.pos[i];
        if (!(w.pos[i] == input.atoms[i].position)) out->moved.push_back(w.ids[i]);
    }

    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "rings=%d skippedFused=%d overlaps=%d textAvoid=%d moved=%llu",
                      out->idealisedRingCount, out->skippedFusedRingCount,
                      out->overlapPairsResolved, out->textAvoidanceMoves,
                      static_cast<unsigned long long>(out->moved.size()));
        out->note = buf;
    }
    out->valid = true;
    return core::Error::success();
}

core::Error planCleanup(const core::Document& doc, const CleanupOptions& options,
                        CleanupPlan* out) {
    LayoutInput input;
    core::Error e = captureLayoutInput(doc, &input);
    if (!e.ok()) return e;
    return planCleanup(input, options, out);
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------
core::Command applyCleanupCommand(const CleanupPlan& plan, bool enforceFreshPlan) {
    CleanupPlan copy = plan;
    return core::Command("advanced.cleanup", core::kInvalidateGeometry,
        [copy, enforceFreshPlan](core::CommandContext& ctx) -> core::CommandResult {
            core::Document& doc = ctx.document;
            if (!copy.valid)
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                 "cleanup plan is not valid");
            if (doc.id() != copy.documentId)
                return core::CommandResult::fail(core::ErrorCode::DocumentMismatch,
                                                 "cleanup plan belongs to another document");
            if (enforceFreshPlan && doc.revision() != copy.revision)
                return core::CommandResult::fail(core::ErrorCode::RevisionConflict,
                                                 "cleanup plan is stale; re-plan first");
            for (size_t i = 0; i < copy.atoms.size(); ++i) {
                core::Error r = doc.setAtomPosition(copy.atoms[i], copy.positions[i]);
                if (!r.ok()) return core::CommandResult::fail(r.code, r.message);
            }
            return core::CommandResult::success(core::kInvalidateGeometry);
        });
}

std::string describeCleanupPlan(const CleanupPlan& plan) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "cleanup[valid=%s doc=%llu rev=%llu atoms=%llu moved=%llu %s]",
                  plan.valid ? "yes" : "no",
                  static_cast<unsigned long long>(plan.documentId),
                  static_cast<unsigned long long>(plan.revision),
                  static_cast<unsigned long long>(plan.atoms.size()),
                  static_cast<unsigned long long>(plan.moved.size()),
                  plan.note.c_str());
    return std::string(buf);
}

size_t applyPlanInPlaceQuiet(core::Document& doc, const CleanupPlan& plan) {
    size_t moved = 0;
    for (size_t i = 0; i < plan.atoms.size(); ++i) {
        const core::AtomRecord* a = doc.atoms().alive(plan.atoms[i]);
        if (!a) continue;
        if (a->position == plan.positions[i]) continue;
        if (doc.setAtomPosition(plan.atoms[i], plan.positions[i]).ok()) ++moved;
    }
    return moved;
}

} // namespace advanced
} // namespace r62
