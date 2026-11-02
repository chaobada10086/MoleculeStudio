#ifndef CDXVIEW_SPATIAL_INDEX_HPP
#define CDXVIEW_SPATIAL_INDEX_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

// Static BVH for conservative viewport culling. IDs are input vector indices.
// Bounds are closed intervals: touching an edge (including a point) overlaps.
// Build again when geometry changes. Queries never mutate the index.
namespace spatial {
struct Box {
    double x0, y0, x1, y1;
};

struct QueryStats {
    std::size_t visited_nodes = 0;
    std::size_t tested_boxes = 0;
};

inline bool overlaps(const Box& a, const Box& b) {
    return a.x0 <= b.x1 && a.x1 >= b.x0 &&
           a.y0 <= b.y1 && a.y1 >= b.y0;
}

class Index {
public:
    // Invalid input is rejected before changing the previous valid index.
    void build(const std::vector<Box>& boxes) {
        for (std::size_t i = 0; i < boxes.size(); ++i) validate(boxes[i]);
        Index next;
        next.boxes_ = boxes;
        const std::size_t n = boxes.size();
        next.ids_.resize(n);
        for (std::size_t i = 0; i < n; ++i) next.ids_[i] = i;
        if (n) {
            // Pre-sort the ids by a Morton code of the box centre. The recursive
            // builder can then split at the middle and never call nth_element,
            // which used to dominate the cost of rebuilding the editor's spatial
            // index after every edit on a large document.
            double min_x = 0, max_x = 0, min_y = 0, max_y = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const double cx = center(boxes[i], true), cy = center(boxes[i], false);
                if (i == 0) { min_x = max_x = cx; min_y = max_y = cy; }
                else {
                    if (cx < min_x) min_x = cx; if (cx > max_x) max_x = cx;
                    if (cy < min_y) min_y = cy; if (cy > max_y) max_y = cy;
                }
            }
            const double span_x = max_x - min_x, span_y = max_y - min_y;
            if (std::isfinite(span_x) && std::isfinite(span_y) && (span_x > 0 || span_y > 0)) {
                std::vector<std::pair<std::uint64_t, std::size_t> > keyed(n);
                for (std::size_t i = 0; i < n; ++i) {
                    double fx = span_x > 0 ? (center(boxes[i], true) - min_x) / span_x : 0.0;
                    double fy = span_y > 0 ? (center(boxes[i], false) - min_y) / span_y : 0.0;
                    if (!(fx >= 0.0)) fx = 0.0; if (fx > 1.0) fx = 1.0;
                    if (!(fy >= 0.0)) fy = 0.0; if (fy > 1.0) fy = 1.0;
                    const std::uint32_t ix = static_cast<std::uint32_t>(fx * 2097151.0);
                    const std::uint32_t iy = static_cast<std::uint32_t>(fy * 2097151.0);
                    std::uint64_t code = 0;
                    for (int b = 0; b < 21; ++b) {
                        code |= std::uint64_t((ix >> b) & 1u) << (2 * b);
                        code |= std::uint64_t((iy >> b) & 1u) << (2 * b + 1);
                    }
                    keyed[i].first = code;
                    keyed[i].second = i;
                }
                std::sort(keyed.begin(), keyed.end(),
                          [](const std::pair<std::uint64_t, std::size_t>& a,
                             const std::pair<std::uint64_t, std::size_t>& b) {
                              return a.first < b.first || (a.first == b.first && a.second < b.second);
                          });
                for (std::size_t i = 0; i < n; ++i) next.ids_[i] = keyed[i].second;
            }
            next.build_node(0, n);
        }
        boxes_.swap(next.boxes_);
        ids_.swap(next.ids_);
        nodes_.swap(next.nodes_);
    }

    // Traversal order is unspecified; sort the stable IDs if draw order matters.
    // Counts reset per query. tested_boxes counts actual primitive AABB checks.
    std::vector<std::size_t> query(const Box& viewport,
                                  QueryStats* stats = nullptr) const {
        QueryStats local;
        QueryStats& counters = stats ? *stats : local;
        counters = QueryStats();
        validate(viewport);
        std::vector<std::size_t> result;
        if (!nodes_.empty()) query_node(0, viewport, counters, result);
        return result;
    }

    std::size_t size() const { return boxes_.size(); }
    std::size_t node_count() const { return nodes_.size(); }

private:
    struct Node {
        Box box;
        std::size_t begin, end, left, right;
        bool leaf;
    };
    enum { leaf_size = 8 };
    std::vector<Box> boxes_;
    std::vector<std::size_t> ids_;
    std::vector<Node> nodes_;

    static void validate(const Box& b) {
        if (!std::isfinite(b.x0) || !std::isfinite(b.y0) ||
            !std::isfinite(b.x1) || !std::isfinite(b.y1) ||
            b.x0 > b.x1 || b.y0 > b.y1)
            throw std::invalid_argument("Spatial bounds must be finite and ordered");
    }

    static double center(const Box& b, bool x_axis) {
        // Half before adding avoids overflow for same-sign finite extremes.
        return x_axis ? b.x0 * 0.5 + b.x1 * 0.5
                      : b.y0 * 0.5 + b.y1 * 0.5;
    }

    std::size_t build_node(std::size_t begin, std::size_t end) {
        Box bounds = boxes_[ids_[begin]];
        for (std::size_t i = begin + 1; i < end; ++i) {
            const Box& b = boxes_[ids_[i]];
            if (b.x0 < bounds.x0) bounds.x0 = b.x0;
            if (b.y0 < bounds.y0) bounds.y0 = b.y0;
            if (b.x1 > bounds.x1) bounds.x1 = b.x1;
            if (b.y1 > bounds.y1) bounds.y1 = b.y1;
        }
        Node node = {bounds, begin, end, 0, 0, end - begin <= leaf_size};
        const std::size_t at = nodes_.size();
        nodes_.push_back(node);
        if (!node.leaf) {
            // ids_ is already spatially ordered, so a plain median split keeps the
            // tree balanced without re-partitioning the range here.
            const std::size_t middle = begin + (end - begin) / 2;
            const std::size_t left = build_node(begin, middle);
            const std::size_t right = build_node(middle, end);
            // Reacquire by index: recursive push_back can reallocate nodes_.
            nodes_[at].left = left;
            nodes_[at].right = right;
        }
        return at;
    }

    void query_node(std::size_t at, const Box& viewport, QueryStats& stats,
                    std::vector<std::size_t>& result) const {
        const Node& node = nodes_[at];
        ++stats.visited_nodes;
        if (!overlaps(node.box, viewport)) return;
        if (node.leaf) {
            for (std::size_t i = node.begin; i < node.end; ++i) {
                const std::size_t id = ids_[i];
                ++stats.tested_boxes;
                if (overlaps(boxes_[id], viewport)) result.push_back(id);
            }
        } else {
            query_node(node.left, viewport, stats, result);
            query_node(node.right, viewport, stats, result);
        }
    }
};
} // namespace spatial

#endif
