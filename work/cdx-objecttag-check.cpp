#include "cdx_binary.hpp"
#include <iostream>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
 const char* xml="<CDXML><page id='1'><tlcplate id='2'><objecttag id='3' Name='MoleculeStudio/TLC' Value='v|1;kind|TlcPlate' Persistent='yes' TagType='String'/><objecttag id='4' Name='float' Value='-1.25' TagType='Double'/><objecttag id='5' Name='integer' Value='-2147483648' TagType='Long'/><objecttag id='6' Name='empty' Value='' TagType='3'/></tlcplate></page></CDXML>";
 std::string bytes,back,error;require(cdxbinary::fromXml(xml,bytes,error),error.c_str());require(cdxbinary::toXml(bytes,back,error),error.c_str());
 tinyxml2::XMLDocument tree;require(tree.Parse(back.c_str())==tinyxml2::XML_SUCCESS,"parse decoded tags");auto tag=tree.RootElement()->FirstChildElement()->FirstChildElement()->FirstChildElement();
 require(tag&&std::string(tag->Attribute("Value"))=="v|1;kind|TlcPlate","string payload preserved with Value before TagType");
 tag=tag->NextSiblingElement();require(tag&&tag->DoubleAttribute("Value")==-1.25,"double payload preserved");
 tag=tag->NextSiblingElement();require(tag&&tag->IntAttribute("Value")==(-2147483647-1),"signed integer payload preserved");
 tag=tag->NextSiblingElement();require(tag&&tag->Attribute("Value")&&std::string(tag->Attribute("Value")).empty(),"empty payload preserved");
 // Malformed numeric lengths must fail rather than silently desynchronizing.
 std::string invalid="VjCD0100";cdxbinary::put(invalid,0x01020304,4);invalid.append(10,0);cdxbinary::put(invalid,0x8000,2);cdxbinary::put(invalid,0,4);cdxbinary::put(invalid,0x8011,2);cdxbinary::put(invalid,1,4);std::string type;cdxbinary::put(type,2,2);cdxbinary::field(invalid,0xd00,type);cdxbinary::field(invalid,0xd05,"x");cdxbinary::put(invalid,0,2);cdxbinary::put(invalid,0,2);
 require(!cdxbinary::toXml(invalid,back,error),"reject truncated typed value");std::cout<<"ObjectTag String/Double/Long, empty and out-of-order properties, malformed lengths: PASS\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what();return 1;}}
