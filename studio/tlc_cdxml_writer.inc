// Standard vector children provide a visible fallback in other editors. The
// metadata restores one structured, editable board when reopened in Studio.
core::Error emitExperimentalFigure(Writer& writer,core::ObjectId id){
    experimental::PlateFigure figure;experimental::PlateFrame frame;
    auto result=experimental::readFigureAndFrame(writer.document,id,&figure,&frame,0);if(!result.ok())return result;
    auto ref=experimental::figureRef(id);const auto& bag=writer.document.extensions(ref);
    if(auto value=bag.findValue("studio.color.override"))figure.border.colorRGBA=uint32_t(std::strtoull(value->c_str(),0,10));
    if(auto value=bag.findValue("studio.graphics.width"))figure.border.width=std::atof(value->c_str());
    auto rfFlag=bag.findValue("studio.tlc.showRf");const bool showRf=rfFlag&&*rfFlag=="1";
    std::vector<render::Primitive> primitives;text::LayoutOptions options;
    result=experimental::appendFigurePrimitives(figure,frame,ref,1,100,10,writer.document.defaultLineWidth(),writer.document.defaultTextSize(),0,options,&primitives);if(!result.ok())return result;
    for(auto& annotation:figure.annotations)if(annotation.textId!=core::kInvalidObjectId){auto text=writer.document.texts().alive(annotation.textId);if(!text)return fail("TLC annotation text is missing",ErrorCode::NotFound);annotation.textId=writer.objectId(ObjectRef(ObjectKind::Text,text->id),text->sourceId);}
    auto& out=writer.out;out<<"<group";attributeInteger(out,"id",writer.objectId(ref,writer.document.symbols().get(id)->sourceId));
    attribute(out,"MoleculeStudioTLC",experimental::encodeFigure(figure));attribute(out,"MoleculeStudioShowRf",showRf?"1":"0");
    if(auto diameter=bag.findValue("studio.tlc.spotDiameter"))attribute(out,"MoleculeStudioDotSize",*diameter);
    std::ostringstream geometry;geometry.precision(17);geometry<<frame.centre.x<<' '<<frame.centre.y<<' '<<frame.axisU.x<<' '<<frame.axisU.y<<' '<<frame.axisV.x<<' '<<frame.axisV.y;attribute(out,"MoleculeStudioFrame",geometry.str());
    auto bounds=frame.aabb();std::ostringstream box;box.precision(17);box<<bounds.min.x<<' '<<bounds.min.y<<' '<<bounds.max.x<<' '<<bounds.max.y;attribute(out,"BoundingBox",box.str());out<<">";
    for(const auto& p:primitives){if(p.points.empty())continue;
        if(p.kind==render::PrimKind::Text){if(!showRf&&p.text.compare(0,3,"Rf ")==0)continue;out<<"<t";attributeInteger(out,"id",writer.reserveFileId());attribute(out,"p",numberToText(p.points[0].x)+" "+numberToText(p.points[0].y));attribute(out,"MoleculeStudioInk",std::to_string(p.style.fill.rgba));out<<"><s";attributeInteger(out,"font",writer.fontFor(writer.document.defaultFont()));attributeNumber(out,"size",p.textSize);std::string escaped;escapeContent(p.text,&escaped);out<<">"<<escaped<<"</s></t>";continue;}
        bool closed=p.kind==render::PrimKind::Polygon;out<<"<curve";attributeInteger(out,"id",writer.reserveFileId());attributeInteger(out,"CurveType",(closed?1:0)|(p.style.fill.enabled?128:0));attribute(out,"Closed",closed?"yes":"no");
        std::ostringstream points;for(auto point:p.points)for(int i=0;i<3;++i)points<<numberToText(point.x)<<' '<<numberToText(point.y)<<' ';attribute(out,"CurvePoints",points.str());
        attribute(out,"MoleculeStudioInk",std::to_string(p.style.stroke.rgba));attributeNumber(out,"LineWidth",p.style.strokeWidth);attribute(out,"LineType",!p.style.stroke.enabled?"None":p.style.dash.on>0?"Dashed":"Solid");
        attribute(out,"FillType",p.style.fill.enabled?"Solid":"None");if(p.style.fill.enabled)attribute(out,"MoleculeStudioFillInk",std::to_string(p.style.fill.rgba));out<<"/>";
    }
    out<<"</group>";return core::Error::success();
}
