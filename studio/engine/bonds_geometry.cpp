// MoleculeStudio bond primitives, implemented from the project geometry contract.
// Input/output types are project interfaces; no external drawing engine is used.
#include "next/bonds/geometry.hpp"
#include <algorithm>
#include <cmath>

namespace r62 { namespace bonds {
BondAxis makeAxis(const core::Vec2& a,const core::Vec2& b,double minimum) {
    BondAxis result;result.begin=a;result.end=b;
    double x=b.x-a.x,y=b.y-a.y;result.length=std::hypot(x,y);
    result.degenerate=!a.finite()||!b.finite()||!std::isfinite(result.length)||result.length<=std::max(0.,minimum);
    result.dir=result.degenerate?core::Vec2(1,0):core::Vec2(x/result.length,y/result.length);
    result.normal=core::Vec2(-result.dir.y,result.dir.x);
    if(!std::isfinite(result.length))result.length=0;
    return result;
}
BondAxis makeAxisReversed(const core::Vec2& a,const core::Vec2& b,double minimum) {
    BondAxis result=makeAxis(b,a,minimum);result.reversed=true;return result;
}
double multipleBondSpacing(double length,const BondGeometryOptions& options) {
    return std::isfinite(length)&&length>0?length*options.spacingPercent*.01:0;
}
core::Vec2 clearLabel(const core::Vec2& start,const core::Vec2& target,double radius,double margin) {
    const double distance=std::hypot(target.x-start.x,target.y-start.y);
    if(radius<=0||margin<=0||distance<=1e-12)return start;
    return start+(target-start)*(std::min(radius+margin,distance*.5)/distance);
}
namespace {
class Builder {
    const BondAxis& axis;
    const BondGeometryOptions& style;
public:
    BondRenderData output;
    Builder(const BondAxis& a,const BondGeometryOptions& s):axis(a),style(s){output.axisBegin=a.begin;output.axisEnd=a.end;}
    core::Vec2 point(double along,double across=0) const {return axis.begin+axis.dir*along+axis.normal*across;}
    void stroke(double start,double finish,double left=0,double right=0) {
        BondShape shape;shape.kind=BondShapeKind::Stroke;shape.points={point(start,left),point(finish,right)};output.shapes.push_back(shape);
    }
    void quadrilateral(double start,double finish,double firstWidth,double lastWidth,bool outline=false) {
        std::vector<core::Vec2> corners={point(start,-firstWidth),point(start,firstWidth),point(finish,lastWidth),point(finish,-lastWidth)};
        BondShape shape;shape.kind=outline?BondShapeKind::Stroke:BondShapeKind::FilledPolygon;
        if(outline){shape.points=corners;shape.points.push_back(corners.front());shape.strokeWidth=style.lineWidth;}
        else{shape.outline=corners;shape.fill=true;}
        output.shapes.push_back(shape);
    }
    void stereo(bool hashed,bool tapered,bool hollow=false) {
        double farWidth=(tapered?style.boldWidth*style.wedgeEndScale:style.boldWidth)*.5;
        double nearWidth=tapered?style.lineWidth*.5:farWidth;
        if(!hashed){quadrilateral(0,axis.length,nearWidth,farWidth,hollow);return;}
        const double thickness=std::min(axis.length,std::max(.01,style.lineWidth));
        const double pitch=style.hashSpacing>0?style.hashSpacing:2.5;
        int stripes=std::max(1,std::min(256,int((axis.length-thickness)/pitch)+1));
        for(int index=0;index<stripes;++index){
            double start=stripes==1?0:(axis.length-thickness)*index/(stripes-1),finish=start+thickness;
            double width0=nearWidth+(farWidth-nearWidth)*start/axis.length;
            double width1=nearWidth+(farWidth-nearWidth)*finish/axis.length;
            quadrilateral(start,finish,width0,width1);
        }
    }
    void arrow() {
        double head=axis.length*style.dativeHeadPercent*.01;
        if(head<=0){stroke(0,axis.length);return;}
        double stem=std::max(0.,axis.length-head),half=head*std::tan(style.dativeHeadAngle*std::acos(-1.)/180.);
        stroke(0,stem>0?stem:axis.length);
        BondShape shape;shape.kind=BondShapeKind::Polyline;shape.points={point(stem,half),axis.end,point(stem,-half)};output.shapes.push_back(shape);
    }
};
int multiplicity(core::BondOrder order,bool aromatic) {
    switch(order){
    case core::BondOrder::Double:case core::BondOrder::SingleOrDouble:case core::BondOrder::SingleOrAromatic:case core::BondOrder::DoubleOrAromatic:return 2;
    case core::BondOrder::Triple:return 3;
    case core::BondOrder::Quadruple:return 4;
    case core::BondOrder::Aromatic:return aromatic?2:1;
    default:return 1;
    }
}
}
BondRenderData buildBondGeometry(const BondAxis& axis,const BondAxes& axes,const BondGeometryOptions& options,const EndpointContext& first,const EndpointContext& last) {
    Builder builder(axis,options);
    if(axis.degenerate){builder.output.degenerate=true;builder.output.note="zero-length or non-finite bond axis";return builder.output;}
    builder.output.clippedByLabel=first.drawsLabel||last.drawsLabel;
    switch(axes.display){
    case core::BondDisplay::Wedge:builder.stereo(false,true);builder.output.note="solid wedge, narrow end at begin";return builder.output;
    case core::BondDisplay::HollowWedge:builder.stereo(false,true,true);builder.output.note="hollow wedge, narrow end at begin";return builder.output;
    case core::BondDisplay::WedgeHash:builder.stereo(true,true);builder.output.note="hashed wedge tapering from the narrow end";return builder.output;
    case core::BondDisplay::Hash:builder.stereo(true,false);builder.output.note="uniform hashed bond";return builder.output;
    case core::BondDisplay::Bold:builder.stroke(0,axis.length);builder.output.shapes.back().strokeWidth=options.boldWidth;builder.output.note="bold bond";return builder.output;
    default:break;
    }
    int lines=multiplicity(axes.order,axes.semantics==core::BondSemantics::Aromatic);
    double start=std::min(axis.length/3.,axis.length*options.multipleEndGapPercent*.01+(first.drawsLabel?options.labelClearance:0));
    double endGap=std::min(axis.length/3.,axis.length*options.multipleEndGapPercent*.01+(last.drawsLabel?options.labelClearance:0));
    if(lines==3){if(!first.drawsLabel)start=0;if(!last.drawsLabel)endGap=0;}
    double finish=axis.length-endGap,spacing=multipleBondSpacing(axis.length,options);
    if(axes.stereo==core::BondStereo::Either&&lines==2){
        double side=spacing>0?spacing:axis.length*.1;
        builder.stroke(start,finish,side,-side);builder.stroke(start,finish,-side,side);
        builder.output.note="crossed double bond (double either)";return builder.output;
    }
    if(axes.semantics==core::BondSemantics::Dative){builder.arrow();builder.output.note="dative bond, arrowhead at the acceptor (end)";return builder.output;}
    if(lines==1)builder.stroke(0,axis.length);
    else {
        std::vector<double> offsets;
        if(lines==3)offsets={0,spacing,-spacing};
        else if(lines==4)offsets={spacing*.5,-spacing*.5,spacing*1.5,-spacing*1.5};
        else switch(axes.side){
            case DoubleSide::Left:offsets={0,-spacing};break;
            case DoubleSide::Right:offsets={0,spacing};break;
            case DoubleSide::Centered:offsets={spacing,-2*spacing};break;
            default:offsets={spacing,-spacing};break;
        }
        if(finish>=start)for(double offset:offsets)builder.stroke(start,finish,offset,offset);
    }
    for(auto& shape:builder.output.shapes){
        bool dotted=axes.semantics==core::BondSemantics::Hydrogen||axes.display==core::BondDisplay::Dotted;
        if(dotted||axes.display==core::BondDisplay::Dashed){shape.dashOn=dotted?options.dotOn:options.dashOn;shape.dashOff=dotted?options.dotOff:options.dashOff;}
        else if(axes.display==core::BondDisplay::Wavy){shape.wavyAmplitude=options.wavyAmplitude;shape.wavyWavelength=options.wavyWavelength;}
    }
    if(axes.semantics==core::BondSemantics::Aromatic&&builder.output.shapes.size()>1){builder.output.shapes[1].dashOn=options.dashOn;builder.output.shapes[1].dashOff=options.dashOff;builder.output.note="aromatic bond: solid line plus dashed companion";}
    else if(axes.semantics==core::BondSemantics::Hydrogen)builder.output.note="hydrogen bond drawn dotted";
    else if(axes.display==core::BondDisplay::Wavy)builder.output.note="wavy bond";
    return builder.output;
}
BondRenderData buildBondGeometry(const core::Vec2& first,const core::Vec2& last,const BondAxes& axes,const BondGeometryOptions& options) {
    return buildBondGeometry(makeAxis(first,last,options.minDecoratedLength),axes,options,EndpointContext(),EndpointContext());
}
size_t visibleShapeCount(const BondRenderData& data){size_t count=0;for(const auto& shape:data.shapes)count+=shape.kind==BondShapeKind::Polyline?shape.points.size()/2:1;return count;}
size_t totalPointCount(const BondRenderData& data){size_t count=0;for(const auto& shape:data.shapes)count+=shape.points.size()+shape.outline.size()+shape.fillHoles.size();return count;}
} }
