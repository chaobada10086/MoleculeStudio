#pragma once

#include "next/selection/transform.hpp"
#include "next/render/geometry.hpp"

namespace studio_transform_preview {
inline void apply(const r62::core::Document& doc,const r62::render::Primitive& original,
                  const r62::selection::Affine2& transform,r62::render::Primitive& p) {
    using namespace r62;
    using namespace r62::core;
        auto matrix=transform;
        if(original.source.kind==ObjectKind::Atom){auto a=doc.atoms().alive(original.source.id);if(a)matrix=selection::Affine2::translation(transform.apply(a->position)-a->position);}
        if(original.source.kind==ObjectKind::Text){auto t=doc.texts().alive(original.source.id);if(t){
            const Vec2 anchor=transform.apply(t->position);double scale=std::hypot(transform.m00,transform.m10);
            if(scale>1e-12&&transform.determinant()>0){matrix.m00/=scale;matrix.m01/=scale;matrix.m10/=scale;matrix.m11/=scale;matrix.m02=anchor.x-matrix.m00*t->position.x-matrix.m01*t->position.y;matrix.m12=anchor.y-matrix.m10*t->position.x-matrix.m11*t->position.y;}
            else matrix=selection::Affine2::translation(anchor-t->position);
        }}
        for(auto& pt:p.points)pt=matrix.apply(pt);
        for(auto& sub:p.subpaths)for(auto& pt:sub.points)pt=matrix.apply(pt);
        p.bounds=render::geom::computeBounds(p,.6);
}
}
