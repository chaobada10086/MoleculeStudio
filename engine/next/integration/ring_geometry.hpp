#ifndef R62_T19_RING_GEOMETRY_HPP
#define R62_T19_RING_GEOMETRY_HPP
#include "next/bonds/geometry.hpp"
#include "next/core/document.hpp"
#include <set>
namespace r62 { namespace integration {
// Projection adapter only: never changes bond order, aromaticity or coordinates.
class RingGeometry {
    const core::Document& doc;
    std::vector<std::vector<std::pair<core::ObjectId,core::ObjectId> > > edges;
public:
    explicit RingGeometry(const core::Document&);
    bonds::BondRenderData build(const core::BondRecord&,const std::set<core::ObjectRef>* moving=0,core::Vec2 delta=core::Vec2()) const;
};
} }
#endif
