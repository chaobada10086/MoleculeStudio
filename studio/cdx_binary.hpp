#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include "third_party/tinyxml2/tinyxml2.h"
// CDX <-> CDXML bridge. Coordinates remain in points, never screen pixels.
// Wire types follow the archived CambridgeSoft CDX SDK. Unknown properties
// are length-delimited and skipped; malformed lengths fail before import.
namespace cdxbinary {
#include "cdx_schema.hpp"
inline UINT format(){static UINT f=RegisterClipboardFormatW(L"ChemDraw Interchange Format");return f;}
inline std::string number(double x){std::ostringstream o;o.precision(12);o<<x;return o.str();}
inline const Property* property(unsigned tag){for(const auto& p:properties)if(p.tag==tag)return &p;return nullptr;}
inline const Property* property(const std::string& name){for(const auto& p:properties)if(name==p.name)return &p;return nullptr;}
inline const char* objectName(unsigned tag){for(const auto& p:objects)if(p.tag==tag)return p.name;return nullptr;}
inline unsigned objectTag(const std::string& name){for(const auto& p:objects)if(name==p.name && (p.tag&0x8000))return p.tag;return 0;}
inline std::string decodeText(const std::string& b,unsigned cp){if(b.empty())return {};if(!IsValidCodePage(cp))cp=1252;int n=MultiByteToWideChar(cp,0,b.data(),int(b.size()),nullptr,0);if(n<=0)throw std::runtime_error("CDX text encoding");std::wstring w(n,0);MultiByteToWideChar(cp,0,b.data(),int(b.size()),&w[0],n);n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);std::string s(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),&s[0],n,nullptr,nullptr);return s;}
struct Reader {
    const std::string& b;size_t at=0;
    explicit Reader(const std::string& bytes):b(bytes){}
    size_t remaining()const{return b.size()-at;}
    unsigned u(unsigned n){if(n>4||n>remaining())throw std::runtime_error("Truncated CDX data");unsigned v=0;for(unsigned i=0;i<n;++i)v|=unsigned((unsigned char)b[at++])<<(i*8);return v;}
    int32_t signedValue(unsigned n){unsigned v=u(n);if(n<4&&(v&(1u<<(n*8-1))))v|=(~0u)<<(n*8);return int32_t(v);}
    std::string take(size_t n){if(n>remaining())throw std::runtime_error("Invalid CDX property length");auto s=b.substr(at,n);at+=n;return s;}
};
inline void put(std::string& b,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b+=char((v>>(i*8))&255);}
inline void field(std::string& out,unsigned tag,const std::string& b){put(out,tag,2);if(b.size()<65535)put(out,b.size(),2);else{put(out,65535,2);put(out,b.size(),4);}out+=b;}
inline std::string enumName(unsigned tag,long value){for(const auto& e:enums)if(e.tag==tag&&e.value==value)return e.name;std::string s;long rest=value;for(const auto& e:enums)if(e.tag==tag&&e.bits&&e.value>0&&(rest&e.value)==e.value){if(!s.empty())s+=' ';s+=e.name;rest&=~e.value;}return !s.empty()&&!rest?s:number(value);}
inline bool enumValue(unsigned tag,const std::string& text,long& value){for(const auto& e:enums)if(e.tag==tag&&text==e.name){value=e.value;return true;}std::istringstream in(text);std::string word;long bits=0;bool found=false;while(in>>word){bool ok=false;for(const auto& e:enums)if(e.tag==tag&&e.bits&&word==e.name){bits|=e.value;ok=true;break;}if(!ok)return false;found=true;}if(found)value=bits;return found;}
struct Decoder {
    tinyxml2::XMLDocument xml;std::map<unsigned,unsigned> charsets;size_t count=0;
    unsigned codepage(unsigned font){auto i=charsets.find(font);return i==charsets.end()?1252:i->second;}
    void style(tinyxml2::XMLElement* e,Reader& r,const std::string& prefix=""){
        e->SetAttribute((prefix.empty()?"font":prefix+"Font").c_str(),r.u(2));
        e->SetAttribute((prefix.empty()?"face":prefix+"Face").c_str(),r.u(2));
        e->SetAttribute((prefix.empty()?"size":prefix+"Size").c_str(),number(r.u(2)/20.0).c_str());
        e->SetAttribute((prefix.empty()?"color":prefix+"Color").c_str(),r.u(2));
    }
    void text(tinyxml2::XMLElement* e,unsigned tag,const std::string& data,const char* name){
        Reader r(data);unsigned n=r.u(2);if(n>r.remaining()/10)throw std::runtime_error("Invalid CDX text runs");
        struct Run{unsigned start,font,face,size,color;};std::vector<Run> runs;
        for(unsigned i=0;i<n;++i){Run a;a.start=r.u(2);a.font=r.u(2);a.face=r.u(2);a.size=r.u(2);a.color=r.u(2);runs.push_back(a);}
        std::string raw=r.take(r.remaining());
        if(tag!=0x700){e->SetAttribute(name,decodeText(raw,n?codepage(runs[0].font):1252).c_str());return;}
        if(!n){auto* s=xml.NewElement("s");s->SetText(decodeText(raw,1252).c_str());e->InsertEndChild(s);return;}
        for(unsigned i=0;i<n;++i){auto a=runs[i];size_t end=i+1<n?runs[i+1].start:raw.size();if(a.start>end||end>raw.size()||(i==0&&a.start!=0))throw std::runtime_error("Invalid CDX text offset");auto* s=xml.NewElement("s");s->SetAttribute("font",a.font);s->SetAttribute("face",a.face);s->SetAttribute("size",number(a.size/20.0).c_str());s->SetAttribute("color",a.color);s->SetText(decodeText(raw.substr(a.start,end-a.start),codepage(a.font)).c_str());e->InsertEndChild(s);}
    }
    void prop(tinyxml2::XMLElement* e,unsigned tag,const std::string& data){
        Reader r(data);
        if(tag==0x80a||tag==0x80b){style(e,r,tag==0x80a?"Label":"Caption");return;}
        if(tag==0x700){text(e,tag,data,"Text");return;}
        if(tag==0x100){r.u(2);unsigned n=r.u(2);auto* table=xml.NewElement("fonttable");for(unsigned i=0;i<n;++i){unsigned id=r.u(2),cp=r.u(2),len=r.u(2);auto name=decodeText(r.take(len),cp);charsets[id]=cp;auto* f=xml.NewElement("font");f->SetAttribute("id",id);f->SetAttribute("charset",cp==65001?"utf-8":cp==936?"gb2312":cp==950?"big5":cp==932?"shift-jis":cp==1252?"iso-8859-1":number(cp).c_str());f->SetAttribute("name",name.c_str());table->InsertEndChild(f);}e->InsertEndChild(table);return;}
        if(tag==0x300){unsigned n=r.u(2);auto* table=xml.NewElement("colortable");for(unsigned i=0;i<n;++i){auto* c=xml.NewElement("color");for(auto key:{"r","g","b"})c->SetAttribute(key,number(r.u(2)/65535.0).c_str());table->InsertEndChild(c);}e->InsertEndChild(table);return;}
        const auto* p=property(tag);if(!p)return;std::string type=p->type,value;
        if(type=="CDXString"){text(e,tag,data,p->name);return;}
        if(type=="CDXCoordinate")value=number(r.signedValue(4)/65536.0);
        else if(type=="CDXPoint2D"||type=="CDXPoint3D"||type=="CDXRectangle"){
            int n=type=="CDXPoint2D"?2:type=="CDXPoint3D"?3:4;std::vector<double> v;for(int i=0;i<n;++i)v.push_back(r.signedValue(4)/65536.0);
            if(n==2)std::swap(v[0],v[1]);else if(n==3)std::swap(v[0],v[2]);else{std::swap(v[0],v[1]);std::swap(v[2],v[3]);}
            for(auto x:v){if(!value.empty())value+=' ';value+=number(x);}
        }else if(type=="CDXBoolean"||type=="CDXBooleanImplied")value=(data.empty()||r.u(1))?"yes":"no";
        else if(type=="CDXObjectIDArray"||type=="CDXObjectIDArrayWithCounts"||type=="INT16ListWithCounts"||type=="CDXElementList"){
            unsigned width=type=="INT16ListWithCounts"||type=="CDXElementList"?2:4;int n=type=="CDXObjectIDArray"?int(r.remaining()/4):r.signedValue(2);if(n<0){value="NOT";n=-n;}for(int i=0;i<n;++i){if(!value.empty())value+=' ';value+=number(r.u(width));}
        }else if(type=="CDXCurvePoints"||type=="CDXCurvePoints3D"){
            unsigned n=r.u(2),dim=type=="CDXCurvePoints"?2:3;for(unsigned i=0;i<n;++i){std::vector<double> v;for(unsigned j=0;j<dim;++j)v.push_back(r.signedValue(4)/65536.0);std::reverse(v.begin(),v.end());for(auto x:v){if(!value.empty())value+=' ';value+=number(x);}}
        }else if(type=="FLOAT64"){if(data.size()!=8)throw std::runtime_error("Invalid CDX float");double x;memcpy(&x,data.data(),8);if(!std::isfinite(x))throw std::runtime_error("Invalid CDX number");value=number(x);}
        else if(type=="INT8"||type=="UINT8"||type=="INT16"||type=="UINT16"||type=="INT32"||type=="UINT32"||type=="CDXObjectID"){
            unsigned n=type.find("8")!=std::string::npos?1:type.find("16")!=std::string::npos?2:4;
            long long v=type[0]=='I'?r.signedValue(n):(long long)r.u(n);
            value=tag==0x804?number(v/10.0):enumName(tag,long(v));
        }else return;
        e->SetAttribute(p->name,value.c_str());
    }
    void contents(Reader& r,tinyxml2::XMLElement* parent,unsigned depth){
        if(depth>128)throw std::runtime_error("CDX nesting too deep");
        for(;;){unsigned tag=r.u(2);if(!tag)return;if(++count>2000000)throw std::runtime_error("CDX object limit");
            if(tag&0x8000){unsigned id=r.u(4);const char* name=objectName(tag);auto* child=xml.NewElement(name?name:"unsupportedCDXObject");child->SetAttribute("id",id);parent->InsertEndChild(child);contents(r,child,depth+1);}
            else{size_t n=r.u(2);if(n==65535)n=r.u(4);prop(parent,tag,r.take(n));}
        }
    }
    std::string read(const std::string& data){
        if(data.size()<30||data.size()>128*1024*1024||data.compare(0,8,"VjCD0100"))throw std::runtime_error("Not a CDX document");
        Reader r(data);r.at=22;if(r.u(2)!=0x8000)throw std::runtime_error("Invalid CDX document header");r.u(4);
        auto* root=xml.NewElement("CDXML");xml.InsertEndChild(root);contents(r,root,0);
        tinyxml2::XMLPrinter print;xml.Print(&print);return print.CStr();
    }
};
inline bool toXml(const std::string& bytes,std::string& xml,std::string& error){try{Decoder d;xml=d.read(bytes);return true;}catch(const std::exception& e){error=e.what();xml.clear();return false;}}

struct Encoder {
    tinyxml2::XMLDocument xml;unsigned nextId=1;std::map<unsigned,unsigned> charsets;
    std::string stringBytes(const std::string& utf8,unsigned cp){
        if(cp==65001)return utf8;int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),nullptr,0);if(n<=0&&!utf8.empty())throw std::runtime_error("Invalid UTF-8");std::wstring w(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,utf8.data(),int(utf8.size()),&w[0],n);BOOL lost=FALSE;n=WideCharToMultiByte(cp,0,w.data(),int(w.size()),nullptr,0,nullptr,&lost);std::string b(n,0);if(n)WideCharToMultiByte(cp,0,w.data(),int(w.size()),&b[0],n,nullptr,&lost);if(lost)throw std::runtime_error("CDX font charset cannot encode text");return b;
    }
    std::string text(tinyxml2::XMLElement* e){
        std::string runs,raw;unsigned count=0;
        for(auto* s=e->FirstChildElement("s");s;s=s->NextSiblingElement("s")){
            unsigned font=s->UnsignedAttribute("font",3);if(raw.size()>65535)throw std::runtime_error("CDX text run too long");put(runs,raw.size(),2);put(runs,font,2);put(runs,s->UnsignedAttribute("face"),2);put(runs,(unsigned)std::lround(s->DoubleAttribute("size",10)*20),2);put(runs,s->UnsignedAttribute("color"),2);
            raw+=stringBytes(s->GetText()?s->GetText():"",charsets.count(font)?charsets[font]:1252);++count;
        }
        if(count>65535)throw std::runtime_error("Too many CDX text runs");std::string b;put(b,count,2);return b+runs+raw;
    }
    bool value(const Property& p,const std::string& v,std::string& out){
        std::string t=p.type;std::istringstream in(v);std::vector<double> nums;double x;while(in>>x)nums.push_back(x);
        auto coordinate=[&](double a){double scaled=std::round(a*65536.0);if(!std::isfinite(scaled)||scaled<-2147483648.0||scaled>2147483647.0)throw std::runtime_error("CDX coordinate out of range");put(out,(uint32_t)(int32_t)scaled,4);};
        if(t=="CDXString"){put(out,0,2);out+=stringBytes(v,1252);}
        else if(t=="CDXCoordinate"){if(nums.size()!=1)return false;coordinate(nums[0]);}
        else if(t=="CDXPoint2D"||t=="CDXPoint3D"||t=="CDXRectangle"){
            size_t n=t=="CDXPoint2D"?2:t=="CDXPoint3D"?3:4;if(nums.size()!=n)return false;
            if(n==2)std::swap(nums[0],nums[1]);else if(n==3)std::swap(nums[0],nums[2]);else{std::swap(nums[0],nums[1]);std::swap(nums[2],nums[3]);}for(auto a:nums)coordinate(a);
        }else if(t=="CDXBoolean"||t=="CDXBooleanImplied"){bool yes=v=="yes"||v=="true"||v=="1";if(t=="CDXBooleanImplied"){if(!yes)return false;}else put(out,yes,1);}
        else if(t=="CDXObjectIDArray"||t=="CDXObjectIDArrayWithCounts"||t=="INT16ListWithCounts"||t=="CDXElementList"){
            bool notList=v.compare(0,3,"NOT")==0;if(notList){std::istringstream list(v.substr(3));nums.clear();while(list>>x)nums.push_back(x);}
            if(t!="CDXObjectIDArray")put(out,notList?-int(nums.size()):int(nums.size()),2);for(auto a:nums)put(out,(uint32_t)a,t=="INT16ListWithCounts"||t=="CDXElementList"?2:4);
        }else if(t=="CDXCurvePoints"||t=="CDXCurvePoints3D"){
            unsigned dim=t=="CDXCurvePoints"?2:3;if(nums.size()%dim)return false;put(out,nums.size()/dim,2);for(size_t i=0;i<nums.size();i+=dim)for(unsigned j=0;j<dim;++j)coordinate(nums[i+dim-j-1]);
        }else if(t=="FLOAT64"){if(nums.size()!=1||!std::isfinite(nums[0]))return false;out.assign(reinterpret_cast<const char*>(&nums[0]),8);}
        else if(t=="INT8"||t=="UINT8"||t=="INT16"||t=="UINT16"||t=="INT32"||t=="UINT32"||t=="CDXObjectID"){
            long val;if(!enumValue(p.tag,v,val)){if(nums.size()!=1)return false;val=(long)std::lround(nums[0]*(p.tag==0x804?10:1));}put(out,val,t.find("8")!=std::string::npos?1:t.find("16")!=std::string::npos?2:4);
        }else return false;return true;
    }
    void scan(tinyxml2::XMLElement* e,unsigned depth=0){if(depth>128)throw std::runtime_error("XML nesting too deep");nextId=std::max(nextId,e->UnsignedAttribute("id")+1);for(auto* c=e->FirstChildElement();c;c=c->NextSiblingElement())scan(c,depth+1);}
    void object(tinyxml2::XMLElement* e,std::string& out,unsigned depth=0){
        if(depth>128)throw std::runtime_error("XML nesting too deep");unsigned tag=objectTag(e->Name());if(!tag)return;
        put(out,tag,2);put(out,tag==0x8000?0:e->UnsignedAttribute("id",nextId++),4);
        for(auto* a=e->FirstAttribute();a;a=a->Next()){auto* p=property(a->Name());if(!p)continue;std::string b;if(value(*p,a->Value(),b))field(out,p->tag,b);}
        // Tables must precede text so the reader can decode style-run encodings.
        if(auto* table=e->FirstChildElement("fonttable")){std::string b;unsigned n=0;for(auto* f=table->FirstChildElement("font");f;f=f->NextSiblingElement("font"))++n;put(b,1,2);put(b,n,2);for(auto* f=table->FirstChildElement("font");f;f=f->NextSiblingElement("font")){unsigned id=f->UnsignedAttribute("id");charsets[id]=65001;std::string name=f->Attribute("name")?f->Attribute("name"):"Arial";put(b,id,2);put(b,65001,2);put(b,name.size(),2);b+=name;}field(out,0x100,b);}
        if(auto* table=e->FirstChildElement("colortable")){std::string b;unsigned n=0;for(auto* c=table->FirstChildElement("color");c;c=c->NextSiblingElement("color"))++n;put(b,n,2);for(auto* c=table->FirstChildElement("color");c;c=c->NextSiblingElement("color"))for(auto key:{"r","g","b"})put(b,(unsigned)std::lround(std::max(0.0,std::min(1.0,c->DoubleAttribute(key)))*65535),2);field(out,0x300,b);}
        if(tag==0x8006)field(out,0x700,text(e));
        for(auto* c=e->FirstChildElement();c;c=c->NextSiblingElement())if(objectTag(c->Name()))object(c,out,depth+1);
        put(out,0,2);
    }
    std::string write(const std::string& input){if(xml.Parse(input.data(),input.size())!=tinyxml2::XML_SUCCESS||!xml.FirstChildElement("CDXML"))throw std::runtime_error("Invalid CDXML");auto* root=xml.RootElement();scan(root);std::string b="VjCD0100";put(b,0x01020304,4);b.append(10,0);object(root,b);return b;}
};
inline bool fromXml(const std::string& xml,std::string& bytes,std::string& error){try{Encoder e;bytes=e.write(xml);return true;}catch(const std::exception& e){error=e.what();bytes.clear();return false;}}
}
