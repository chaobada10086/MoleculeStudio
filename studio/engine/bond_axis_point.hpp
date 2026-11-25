#pragma once
#include "next/bonds/geometry.hpp"

namespace r62 { namespace bonds {
inline core::Vec2 pointOnBondAxis(const BondAxis& axis, double along, double across = 0) {
    return axis.begin+axis.dir*along+axis.normal*across;
}
} }
