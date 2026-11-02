#ifndef STUDIO_PROJECT_TEST_DATA_HPP
#define STUDIO_PROJECT_TEST_DATA_HPP
#include <sstream>
#include <cmath>
// Generated project test molecule, independent of installed editor exports.
inline std::string projectBenzeneXml(){
    std::ostringstream xml;xml<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?><CDXML BondLength=\"30\" LabelFont=\"3\" CaptionFont=\"3\"><fonttable><font id=\"3\" charset=\"iso-8859-1\" name=\"Arial\"/></fonttable><page id=\"1\" BoundingBox=\"0 0 595 842\"><fragment id=\"2\">";
    for(int i=0;i<6;++i){double angle=i*std::acos(-1.)/3;xml<<"<n id=\""<<i+3<<"\" p=\""<<100+30*std::cos(angle)<<" "<<100+30*std::sin(angle)<<"\"/>";}
    for(int i=0;i<6;++i)xml<<"<b id=\""<<i+9<<"\" B=\""<<i+3<<"\" E=\""<<(i+1)%6+3<<"\" Order=\""<<(i%2?1:2)<<"\"/>";
    xml<<"</fragment></page></CDXML>";return xml.str();
}
inline std::string projectAcetoneXml(){return "<CDXML BondLength=\"14.4\" LineWidth=\"0.6\" LabelSize=\"10\" LabelFont=\"3\" CaptionFont=\"3\"><fonttable><font id=\"3\" name=\"Arial\"/></fonttable><page id=\"1\"><fragment id=\"2\"><n id=\"3\" p=\"0 0\"/><n id=\"4\" p=\"0 -14.4\" Element=\"8\"><t p=\"0 -14.4\"><s font=\"3\" size=\"10\" face=\"96\">O</s></t></n><n id=\"5\" p=\"-12.4708 7.2\"/><n id=\"6\" p=\"12.4708 7.2\"/><b id=\"7\" B=\"3\" E=\"4\" Order=\"2\"/><b id=\"8\" B=\"3\" E=\"5\"/><b id=\"9\" B=\"3\" E=\"6\"/></fragment></page></CDXML>";}
inline std::string projectCorpusXml(int variant){
    int atoms=3+variant%8;bool ring=(variant/8)%2==0;double pi=std::acos(-1.);
    std::ostringstream xml;xml<<"<CDXML BondLength=\"30\"><page id=\"1\"><fragment id=\"2\">";
    for(int i=0;i<atoms;++i){double x=ring?100+40*std::cos(2*pi*i/atoms):30*i,y=ring?100+40*std::sin(2*pi*i/atoms):(i%2)*15;
        int element=i==0&&variant%4!=0?(variant%2?7:8):6;
        xml<<"<n id=\""<<i+3<<"\" Element=\""<<element<<"\" p=\""<<x<<" "<<y<<"\"";if(i==0&&element==6&&variant>=16)xml<<" Isotope=\"13\"";xml<<"/>";
    }
    for(int i=0;i<(ring?atoms:atoms-1);++i)xml<<"<b id=\""<<atoms+i+3<<"\" B=\""<<i+3<<"\" E=\""<<(i+1)%atoms+3<<"\"/>";
    xml<<"</fragment></page></CDXML>";return xml.str();
}
#endif
