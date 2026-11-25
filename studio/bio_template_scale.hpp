#pragma once
static void scaleBioTemplate(Session& source,double bondLength){
    double factor=bondLength/source.doc.defaultBondLength();selection::TransformOptions options;options.textSizePolicy=selection::TextSizePolicy::ScaleWithGeometry;
    std::map<ObjectId,double> depth;
    for(const auto& atom:source.doc.atoms().records())if(atom.isAlive())if(auto z=source.doc.extensions(ObjectRef(ObjectKind::Atom,atom.id)).findValue("studio.atom.z"))depth[atom.id]=std::atof(z->c_str());
    selection::scaleObjects(source.doc,source.all(),Vec2(),factor,options);
    // XY setters invalidate component depth. Restore it only after all atoms
    // have moved, applying the same scale as the template geometry.
    for(const auto& entry:depth){std::ostringstream value;value<<std::setprecision(15)<<entry.second*factor;source.doc.extensions(ObjectRef(ObjectKind::Atom,entry.first)).set("studio.atom.z",value.str());}
    double labelSize=integration::molecular::setting(source.doc,"LabelSize",10);
    for(const auto& atom:source.doc.atoms().records())if(atom.isAlive()){
        auto runs=studioatom::runs(source.doc,atom.id);for(auto& run:runs)run.size=(run.size>0?run.size:labelSize)*factor;if(!runs.empty())studioatom::set(source.doc,atom.id,runs);
        auto& ext=source.doc.extensions(ObjectRef(ObjectKind::Atom,atom.id));
        for(auto key:{"studio.label.offset","studio.atom.source.xy"})if(auto value=ext.findValue(key)){std::istringstream input(*value);Vec2 point;if(input>>point.x>>point.y){std::ostringstream output;output<<std::setprecision(15)<<point.x*factor<<' '<<point.y*factor;ext.set(key,output.str());}}
    }
    source.doc.documentExtensions().set("studio.style.LabelSize",std::to_string(labelSize*factor));source.doc.setDefaultTextSize(source.doc.defaultTextSize()*factor);source.doc.setDefaultBondLength(bondLength);source.refresh();
}
