// Extra spots are appended, but native CDXML serializes them by lane. Preserve
// element IDs, colours and tail opacity even when those orders are different.
{auto mixed=candidate;auto first=mixed.spots.front().id;mixed.spots.front().tailOpacity=.91;mixed.spots.front().fillRGBA=0x123456FF;
 auto added=studio_tlc::addSpot(mixed,frame,mixed.lanes.front().id,5,.14);require(added!=0,"interleaved spot fixture");
 Session source;ObjectId id;require(addFigure(source.doc,mixed,frame,.6,&id).ok(),"add interleaved fixture");source.refresh();auto original=source.doc.digest();
 for(const char* suffix:{"r62doc","cdxml","cdx"}){
    auto output=dir+"/tlc-interleaved."+suffix;
    if(std::string(suffix)=="cdx"){std::string xml,binary,why;require(studioio::exportXml(source.doc,&xml).ok()&&cdxbinary::fromXml(xml,binary,why)&&persistence::writeFileAtomic(output,binary).ok(),"save CDX plate");}
    else require(source.save(output,std::string(suffix)=="cdxml").ok(),"save interleaved plate");
    require(source.doc.digest()==original,"export never moves live document");
    Session reopened;require(reopened.open(output).ok(),"reopen interleaved plate");PlateFigure actual;PlateFrame actualFrame;
    require(readFigureAndFrame(reopened.doc,0,&actual,&actualFrame,0).ok()&&actual.spots.size()==mixed.spots.size(),"all interleaved spots reopen");
    for(const auto& spot:mixed.spots){auto copy=findSpot(actual,spot.id);require(copy&&copy->laneId==spot.laneId&&std::abs(copy->v-spot.v)<1e-7&&copy->fillRGBA==spot.fillRGBA&&std::abs(copy->tailOpacity-spot.tailOpacity)<1e-7,"spot identity, position, colour and tail opacity survive lane reordering");}
    if(std::string(suffix)!="r62doc"){auto bounds=actualFrame.aabb();require(bounds.min.x>=0&&bounds.min.y>=0,"exported TLC is inside positive canvas");}
 }
}
int repairedBoards=0,repairedSpots=0;
for(const char* name:{"asf","asdf"}){
 const auto input=dir+"/"+name+"-input.cdxml";if(GetFileAttributesW(W(input).c_str())==INVALID_FILE_ATTRIBUTES)continue;
 Session imported;require(imported.open(input).ok(),"read user's saved TLC file");
 std::vector<PlateFigure> before;std::vector<PlateFrame> frames;
 for(const auto& symbol:imported.doc.symbols().records())if(symbol.isAlive()&&isFigureObject(imported.doc,figureRef(symbol.id))){PlateFigure figure;PlateFrame f;require(readFigureAndFrame(imported.doc,symbol.id,&figure,&f,0).ok(),"read user plate");before.push_back(figure);frames.push_back(f);}
 require(!before.empty(),"user fixture contains TLC data");auto digest=imported.doc.digest();std::string xml;require(studioio::exportXml(imported.doc,&xml).ok(),"export repaired user file");require(imported.doc.digest()==digest,"repair leaves original document coordinates intact");
 tinyxml2::XMLDocument tree;require(tree.Parse(xml.c_str())==tinyxml2::XML_SUCCESS,"parse repaired export");int plates=0;
 std::function<void(tinyxml2::XMLElement*)> checkCorners=[&](tinyxml2::XMLElement* node){for(auto e=node->FirstChildElement();e;e=e->NextSiblingElement()){if(std::string(e->Name())=="tlcplate"){++plates;for(auto key:{"TopLeft","TopRight","BottomLeft","BottomRight"}){double x=0,y=0;std::istringstream value(e->Attribute(key));require(bool(value>>x>>y)&&x>=0&&y>=0,"user TLC corners translated onto paper");}}checkCorners(e);}};checkCorners(tree.RootElement());require(plates==int(before.size()),"all user boards exported");
 auto output=dir+"/"+name+"-repaired.cdxml";require(persistence::writeFileAtomic(output,xml).ok(),"write repaired copy");Session reopened;require(reopened.open(output).ok(),"reopen repaired user file");size_t index=0;
 for(const auto& symbol:reopened.doc.symbols().records())if(symbol.isAlive()&&isFigureObject(reopened.doc,figureRef(symbol.id))){PlateFigure figure;PlateFrame f;require(readFigureAndFrame(reopened.doc,symbol.id,&figure,&f,0).ok()&&index<before.size(),"read repaired board");
    const auto& previous=before[index];require(figure.spots.size()==previous.spots.size()&&figure.lanes.size()==previous.lanes.size(),"user lane and spot counts retained");
    for(const auto& spot:previous.spots){auto copy=findSpot(figure,spot.id);require(copy&&std::abs(copy->v-spot.v)<1e-7&&std::abs(copy->tailOpacity-spot.tailOpacity)<1e-7&&std::abs(copy->radiusU-spot.radiusU)<1e-7,"user spot measurements and appearance retained");}
    auto bounds=f.aabb();require(bounds.min.x>=0&&bounds.min.y>=0,"repaired board visible on page");++index;++repairedBoards;repairedSpots+=int(figure.spots.size());}
 require(index==before.size(),"no saved plate is lost");
 std::string second;require(studioio::exportXml(reopened.doc,&second).ok(),"repeat repaired save");tinyxml2::XMLDocument secondTree;require(secondTree.Parse(second.c_str())==tinyxml2::XML_SUCCESS,"repeat save parses");
}
std::ofstream(dir+"/tlc-persistence-checks.json")<<"{\"checkbox_can_be_unchecked\":true,\"selected_vs_all_diameter_scope\":true,\"native_cdxml_cdx\":true,\"interleaved_spot_identity_and_appearance\":true,\"positive_canvas_corners\":true,\"live_document_unchanged_on_export\":true,\"repaired_user_boards\":"<<repairedBoards<<",\"repaired_user_spots\":"<<repairedSpots<<"}";
