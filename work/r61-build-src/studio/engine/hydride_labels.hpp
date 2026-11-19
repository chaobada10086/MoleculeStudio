#pragma once
#include "next/core/document.hpp"
#include <cstdlib>
#include <utility>
#include <vector>

namespace studio { namespace hydride_labels {
// Explicit terminal H tokens are editable drawing content, including labels
// whose residue is not in the chemical dictionary. Interior H stays untouched.
inline int terminal(std::string label,std::string* residue=nullptr,bool preferFront=false,bool* hydrogenFirst=nullptr){
    for(const auto& p:std::vector<std::pair<std::string,std::string>>{{"₀","0"},{"₁","1"},{"₂","2"},{"₃","3"},{"₄","4"},{"₅","5"},{"₆","6"},{"₇","7"},{"₈","8"},{"₉","9"}}){size_t at=0;while((at=label.find(p.first,at))!=std::string::npos){label.replace(at,p.first.size(),p.second);at+=p.second.size();}}
    if(label.size()<2||label.find_first_of(" \t\r\n")!=std::string::npos)return 0;
    size_t at=label.size();while(at&&label[at-1]>='0'&&label[at-1]<='9')--at;
    int count=1;std::string base;bool first=false;
    if(!(preferFront&&label.front()=='H')&&at&&label[at-1]=='H'&&at>1){base=label.substr(0,at-1);if(at<label.size())count=std::atoi(label.substr(at).c_str());}
    else if(label.front()=='H'){first=true;at=1;while(at<label.size()&&label[at]>='0'&&label[at]<='9')++at;if(at==label.size())return 0;base=label.substr(at);if(at>1)count=std::atoi(label.substr(1,at-1).c_str());}
    else return 0;
    if(count<1)return 0;
    if(residue)*residue=base;if(hydrogenFirst)*hydrogenFirst=first;return count;
}
// Read an exact elemental hydride, in either drawing direction. A negative
// result means the label may be an abbreviation and must be left alone.
inline int count(int element,const std::string& label,bool* hydrogenFirst=nullptr){
    if(element<=1)return -1;
    std::string normalized;
    for(size_t i=0;i<label.size();++i){
        const auto digit=static_cast<unsigned char>(i+2<label.size()?label[i+2]:0);
        if(static_cast<unsigned char>(label[i])==0xe2&&i+2<label.size()&&
           static_cast<unsigned char>(label[i+1])==0x82&&digit>=0x80&&digit<=0x89){
            normalized+=char('0'+digit-0x80);
            i+=2;
        }else normalized+=label[i];
    }
    const std::string symbol=r62::core::elementSymbol(element);
    std::string digits;
    bool first=false;
    if(normalized.compare(0,symbol.size()+1,symbol+"H")==0){
        digits=normalized.substr(symbol.size()+1);
    }else if(normalized.size()>symbol.size()&&normalized.front()=='H'&&
             normalized.compare(normalized.size()-symbol.size(),symbol.size(),symbol)==0){
        first=true;
        digits=normalized.substr(1,normalized.size()-symbol.size()-1);
    }else return -1;
    int n=0;
    for(char digit:digits){
        if(digit<'0'||digit>'9'||n>9)return -1;
        n=n*10+(digit-'0');
    }
    if(!digits.empty()&&n==0)return -1;
    if(hydrogenFirst)*hydrogenFirst=first;
    return digits.empty()?1:n;
}

inline bool legacyHydroxyl(const r62::core::AtomRecord& atom,const std::string& label){
    using r62::core::NodeType;
    const auto type=static_cast<NodeType>(atom.semantics.nodeType);
    return (type==NodeType::Nickname||type==NodeType::Fragment)&&
           (atom.element==6||atom.element==8)&&(label=="OH"||label=="HO");
}
} }
