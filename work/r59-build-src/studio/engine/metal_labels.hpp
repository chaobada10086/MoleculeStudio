#pragma once
#include "next/core/document.hpp"
#include <algorithm>
#include <string>
#include <vector>
namespace studio { namespace metal_labels {
using namespace r62::core;
struct Span {size_t start=0,size=0;bool valid()const{return size!=0;}};
inline Span find(const std::string& text){
    static const std::string symbols="|Sc|Ti|V|Cr|Mn|Fe|Co|Ni|Cu|Zn|Y|Zr|Nb|Mo|Tc|Ru|Rh|Pd|Ag|Cd|Hf|Ta|W|Re|Os|Ir|Pt|Au|Hg|Rf|Db|Sg|Bh|Hs|Mt|Ds|Rg|Cn|";
    Span found;
    for(size_t i=0;i<text.size();){if(text[i]<'A'||text[i]>'Z'){++i;continue;}size_t end=i+1;while(end<text.size()&&text[end]>='a'&&text[end]<='z')++end;
        if(symbols.find("|"+text.substr(i,end-i)+"|")!=std::string::npos){if(found.valid())return Span();found.start=i;found.size=end-i;}i=end;
    }return found;
}
inline bool active(const Document& doc,ObjectId id){return !doc.incidentBonds(id).empty()&&find(doc.atomLabel(id)).valid();}
inline bool bondOnRight(const Document& doc,ObjectId id){auto atom=doc.atoms().alive(id);Vec2 sum;for(auto bid:doc.incidentBonds(id)){auto bond=doc.bonds().alive(bid);auto other=doc.atoms().alive(bond->beginAtom==id?bond->endAtom:bond->beginAtom);auto v=other->position-atom->position;double distance=length(v);if(distance>1e-8)sum=sum+v*(1/distance);}return sum.x>1e-8;}
inline std::vector<TextRun> slice(const std::vector<TextRun>& runs,size_t start,size_t count){std::vector<TextRun> result;size_t cursor=0;for(auto run:runs){size_t end=cursor+run.utf8.size(),a=std::max(cursor,start),b=std::min(end,start+count);if(b>a){run.utf8=run.utf8.substr(a-cursor,b-a);result.push_back(run);}cursor=end;}return result;}
inline std::vector<TextRun> scripted(const std::vector<TextRun>& runs){std::string text;for(const auto& run:runs)text+=run.utf8;std::vector<TextRun> result;size_t cursor=0;
    for(auto run:runs){for(size_t i=0;i<run.utf8.size();){size_t n=1;unsigned char c=run.utf8[i];if(c>=0xF0)n=4;else if(c>=0xE0)n=3;else if(c>=0xC0)n=2;auto part=run;part.utf8=run.utf8.substr(i,n);
            if(text[cursor+i]=='n'&&cursor+i>0&&text[cursor+i-1]=='L'&&!(part.flags&(kSubscript|kSuperscript)))part.flags=(part.flags&~kFormula)|kSubscript;
            if(!result.empty()&&result.back().flags==part.flags&&result.back().fontName==part.fontName&&result.back().size==part.size)result.back().utf8+=part.utf8;else result.push_back(part);i+=n;}
        cursor+=run.utf8.size();
    }return result;
}
inline std::vector<TextRun> canonical(const std::string& label,const std::vector<TextRun>& runs){auto metal=find(label);if(!metal.valid())return runs;
    auto result=slice(runs,metal.start,metal.size);for(auto part:slice(runs,0,metal.start))result.push_back(part);for(auto part:slice(runs,metal.start+metal.size,label.size()-metal.start-metal.size))result.push_back(part);return scripted(result);
}
} }
