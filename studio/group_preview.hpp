#pragma once
#include "nickname_expand.hpp"
#include "compatible_renderer.hpp"
namespace grouppreview {
inline std::unique_ptr<Session> load(const std::string& fragment){
    tinyxml2::XMLDocument xml;if(xml.Parse(fragment.c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement())throw std::runtime_error("结构数据损坏");
            nicknameexpand::walk(xml.RootElement(),[&](tinyxml2::XMLElement* e){if(e->Attribute("NodeType")&&std::string(e->Attribute("NodeType"))=="ExternalConnectionPoint"){e->SetAttribute("NodeType","GenericNickname");e->SetAttribute("NumHydrogens",0);auto t=xml.NewElement("t"),s=xml.NewElement("s");s->SetText("R");t->InsertEndChild(s);e->InsertEndChild(t);}});
            tinyxml2::XMLPrinter p;xml.Print(&p);std::unique_ptr<Session> session(new Session());auto error=persistence::readCdxml(std::string("<CDXML BondLength=\"14.4\"><page id=\"2000000000\">")+p.CStr()+"</page></CDXML>",session->doc);if(!error.ok())throw std::runtime_error(error.message);
            session->refresh();
    return session;
}
}
