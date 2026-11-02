#ifndef STUDIO_TEXT_TIDY_HPP
#define STUDIO_TEXT_TIDY_HPP
#include "chem_completion.hpp"
#include "chem_italic.hpp"
#include <cctype>
// "文字整理": apply the existing group-label pipeline (chemcompletion) to text
// that is already in the document. The inline editor runs this pipeline only
// when a label is committed; this command reaches labels and captions that were
// typed earlier, imported, or pasted.
//
// Two independent passes, exactly the ones the editor uses:
//   1. SPELLING - dictionary abbreviations and canonical formula casing
//      (cba -> CBA is not attempted; only chemical tokens are re-cased).
//   2. TYPOGRAPHY - atom counts become subscripts and charges/isotopes become
//      superscripts, using the same Segment list as the inline editor.
// A caption is no longer required to be one chemical token: formulas embedded
// in a sentence ("1. NaH, (EtO)2P(O)CH2CO2Et, THF") are formatted in place while
// the prose around them is untouched. Existing fonts, sizes, bold/italic/
// underline and per-run colours are kept.
namespace texttidy {
inline int scriptSign(uint16_t flags){
    if(flags&kSuperscript)return 1;
    if(flags&kSubscript)return -1;
    return 0;
}
inline bool continuation(unsigned char c){return (c&0xC0)==0x80;}
inline std::vector<uint32_t> readColors(const Document& doc,const ObjectRef& ref){
    std::vector<uint32_t> out;
    auto raw=static_cast<const Document&>(doc).extensions(ref).findValue("studio.text.colors");
    if(!raw)return out;
    std::istringstream in(*raw);uint32_t color;
    while(in>>color)out.push_back(color);
    return out;
}
inline std::string writeColors(const std::vector<uint32_t>& colors){
    std::ostringstream out;for(auto color:colors)out<<color<<' ';return out.str();
}
inline size_t runAt(const std::vector<TextRun>& runs,size_t offset){
    if(runs.empty())return 0;
    size_t at=0;
    for(size_t i=0;i<runs.size();++i){if(offset<at+runs[i].utf8.size())return i;at+=runs[i].utf8.size();}
    return runs.size()-1;
}
inline uint32_t colorAt(const std::vector<uint32_t>& colors,size_t index){
    if(colors.empty())return 0;
    return colors[std::min(index,colors.size()-1)];
}
inline void appendStyle(std::string& out,const TextRun& run,uint32_t color){
    // One record per Unicode character, not per run, so a different run split
    // with identical formatting compares equal.
    auto sign=scriptSign(run.flags);
    char head[8];head[0]=char('0'+std::abs(sign));head[1]=(run.flags&kFormula)?'1':'0';head[2]=(run.flags&kBold)?'1':'0';head[3]=(run.flags&kItalic)?'1':'0';head[4]=(run.flags&kUnderline)?'1':'0';head[5]='|';head[6]='\0';
    std::ostringstream tail;tail<<run.fontName<<'|'<<run.size<<'|'<<color<<'|';
    for(unsigned char c:run.utf8){if(continuation(c))continue;out.append(head);out+=tail.str();}
}
inline std::string signature(const std::vector<TextRun>& runs,const std::vector<uint32_t>& colors){
    std::string out;
    for(size_t i=0;i<runs.size();++i)appendStyle(out,runs[i],colorAt(colors,i));
    return out;
}
struct Piece {std::string text;int script;size_t source;};
struct Tidy {
    bool changed=false,hasColors=false;
    std::string plain;
    std::vector<TextRun> runs;
    std::vector<uint32_t> colors;
};
inline size_t utf8Length(unsigned char c){
    if(c<0x80)return 1;
    if((c>>5)==0x06)return 2;
    if((c>>4)==0x0E)return 3;
    if((c>>3)==0x1E)return 4;
    return 1;
}
inline std::vector<TextRun> buildRuns(const std::vector<Piece>& pieces,const std::vector<TextRun>& oldRuns,const std::vector<uint32_t>& oldColors,bool atom,const std::string& canonical,std::vector<uint32_t>* colorsOut){
    std::vector<TextRun> runs;colorsOut->clear();
    auto italic=chemitalic::maskUtf8(canonical);auto customFormat=chemcompletion::savedFormat(canonical);
    std::vector<size_t> codepointAt(canonical.size()+1,0);
    size_t codepoint=0;
    for(size_t i=0;i<canonical.size();){
        size_t length=utf8Length((unsigned char)canonical[i]);
        codepointAt[i]=codepoint;
        for(size_t k=1;k<length&&i+k<canonical.size();++k)codepointAt[i+k]=codepoint;
        i+=length;++codepoint;
    }
    codepointAt[canonical.size()]=codepoint;
    size_t canonicalAt=0;
    for(const auto& piece:pieces){
        if(piece.text.empty())continue;
        size_t used=0;
        while(used<piece.text.size()){
            size_t length=utf8Length((unsigned char)piece.text[used]);
            if(used+length>piece.text.size())length=piece.text.size()-used;
            size_t index=oldRuns.empty()?0:runAt(oldRuns,piece.source+used);
            TextRun run;
            if(!oldRuns.empty()){
                run.fontName=oldRuns[index].fontName;run.size=oldRuns[index].size;
                run.flags=uint16_t(oldRuns[index].flags&~(kSubscript|kSuperscript));
            }
            if(piece.script<0)run.flags=uint16_t((run.flags&~kFormula)|kSubscript);
            else if(piece.script>0)run.flags=uint16_t((run.flags&~kFormula)|kSuperscript);
            else if(atom)run.flags=uint16_t(run.flags|kFormula);
            size_t sourceIndex=std::min(canonicalAt+used,canonical.size());
            size_t maskIndex=codepointAt[sourceIndex];
            if(customFormat.size()==canonical.size()&&sourceIndex<customFormat.size()){run.flags&=~kItalic;if((customFormat[sourceIndex]-'0')&4)run.flags|=kItalic;}else if(maskIndex<italic.size()&&italic[maskIndex])run.flags=uint16_t(run.flags|kItalic);
            run.utf8=piece.text.substr(used,length);
            uint32_t color=colorAt(oldColors,index);
            if(!runs.empty()&&runs.back().flags==run.flags&&runs.back().fontName==run.fontName&&runs.back().size==run.size&&colorsOut->back()==color)runs.back().utf8+=run.utf8;
            else{runs.push_back(run);colorsOut->push_back(color);}
            used+=length;
        }
        canonicalAt+=piece.text.size();
    }
    return runs;
}
inline bool tokenChar(unsigned char c){
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='('||c==')'||c=='['||c==']'||c=='+'||c=='-';
}
inline bool formulaToken(const std::string& token){
    if(token.size()<2||token.size()>64)return false;
    bool letter=false,digit=false,paren=false,charge=false;
    int depth=0,upper=0;
    for(unsigned char c:token){
        if(c>='A'&&c<='Z'){letter=true;++upper;}
        else if(c>='a'&&c<='z')letter=true;
        else if(c>='0'&&c<='9')digit=true;
        else if(c=='('||c=='['){paren=true;++depth;}
        else if(c==')'||c==']'){--depth;if(depth<0)return false;}
        else if(c=='+'||c=='-')charge=true;
        else return false;
    }
    if(depth!=0||!letter||!digit)return false;
    bool count=false;
    for(size_t i=0;i<token.size();++i)if(token[i]>='0'&&token[i]<='9'){
        char previous=i?token[i-1]:0;
        if((previous>='A'&&previous<='Z')||(previous>='a'&&previous<='z')||previous==')'||previous==']'){count=true;break;}
    }
    if(!count&&!charge)return false;
    // A single capital letter plus digits is far more often a figure/scheme
    // number (S1, C8) than a formula. Keep it unless the common-formula table
    // knows it or normalisation changes its spelling (ch3 -> CH3).
    if(!paren&&!charge&&upper<2){
        std::string lower;for(char c:token)lower+=char(c>='A'&&c<='Z'?c+32:c);
        if(!chemcompletion::commonFormulas().count(lower)&&chemcompletion::normalize(token,true)==token)return false;
    }
    return true;
}
inline std::vector<std::pair<size_t,size_t>> formulaRanges(const std::string& text){
    std::vector<std::pair<size_t,size_t>> ranges;
    size_t i=0;
    while(i<text.size()){
        unsigned char c=text[i];
        bool start=(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='('||c=='[';
        if(!start||(i>0&&std::isalnum((unsigned char)text[i-1]))){++i;continue;}
        size_t j=i;while(j<text.size()&&tokenChar((unsigned char)text[j]))++j;
        std::string token=text.substr(i,j-i);
        if(formulaToken(token))ranges.push_back(std::make_pair(i,j));
        i=j;
    }
    return ranges;
}
inline bool embeddedPieces(const std::string& text,std::vector<Piece>* pieces,std::string* canonical){
    auto ranges=formulaRanges(text);
    if(ranges.empty())return false;
    size_t last=0;
    for(const auto& range:ranges){
        if(range.first>last)pieces->push_back({text.substr(last,range.first-last),0,last});
        std::string token=text.substr(range.first,range.second-range.first);
        // Only re-case tokens that are uniformly lower/upper case. Mixed-case
        // abbreviations such as PhB(OH)2 or EtO must keep the author's spelling;
        // normalisation would read Ph as P+H and turn it into PHB.
        bool allLower=true,allUpper=true,hasLetter=false;
        for(unsigned char c:token)if(std::isalpha(c)){hasLetter=true;if(c>='a'&&c<='z')allUpper=false;if(c>='A'&&c<='Z')allLower=false;}
        std::string spelling=token;
        if(hasLetter&&(allLower||allUpper))spelling=chemcompletion::normalize(token,true);
        auto segments=chemcompletion::segments(spelling,true);
        if(segments.empty())pieces->push_back({token,0,range.first});
        else for(const auto& segment:segments)pieces->push_back({segment.text,segment.script,range.first});
        last=range.second;
    }
    if(last<text.size())pieces->push_back({text.substr(last),0,last});
    for(const auto& piece:*pieces)*canonical+=piece.text;
    return true;
}
// atom=true marks baseline pieces as kFormula, like the committed atom label
// path; captions keep the flags they already had.
inline Tidy tidy(const std::string& text,bool caption,const std::vector<TextRun>& oldRuns,const std::vector<uint32_t>& oldColors,bool atom){
    Tidy out;
    std::vector<Piece> pieces;std::string canonical;bool built=false;
    if(caption){
        std::string whole=chemcompletion::chemicalToken(text,true);
        if(!whole.empty()){
            auto segments=chemcompletion::segments(whole,true);
            if(!segments.empty()){
                size_t offset=0;
                for(const auto& segment:segments){pieces.push_back({segment.text,segment.script,std::min(offset,text.empty()?0:text.size()-1)});offset+=segment.text.size();}
                canonical=whole;built=true;
            }
        }
        if(!built)built=embeddedPieces(text,&pieces,&canonical);
        if(!built){pieces.push_back({text,0,0});canonical=text;built=true;}
    }else{
        canonical=chemcompletion::normalize(text,true);
        auto segments=chemcompletion::segments(canonical,true);
        if(!segments.empty()){
            size_t offset=0;
            for(const auto& segment:segments){pieces.push_back({segment.text,segment.script,std::min(offset,text.empty()?0:text.size()-1)});offset+=segment.text.size();}
            built=true;
        }
        if(!built){pieces.push_back({canonical,0,0});built=true;}
    }
    if(!built||pieces.empty())return out;
    bool hasScript=false;for(const auto& piece:pieces)if(piece.script!=0)hasScript=true;
    bool hasItalic=false;{auto mask=chemitalic::maskUtf8(canonical);for(size_t i=0;i<mask.size();++i)if(mask[i]){hasItalic=true;break;}}
    // A label that is already spelled correctly and has nothing to raise,
    // lower or italicise must not create an undo entry just to restate its
    // formatting.
    if(canonical==text&&!hasScript&&!hasItalic)return out;
    out.runs=buildRuns(pieces,oldRuns,oldColors,atom,canonical,&out.colors);
    out.hasColors=!oldColors.empty();
    out.plain=canonical;
    out.changed=canonical!=text||signature(oldRuns,oldColors)!=signature(out.runs,out.colors);
    return out;
}
inline bool needsTidyAtom(const Document& doc,ObjectId id){
    std::string plain=doc.atomLabel(id);
    if(plain.empty())return false;
    return tidy(plain,false,studioatom::runs(doc,id),readColors(doc,ObjectRef(ObjectKind::Atom,id)),true).changed;
}
inline bool needsTidyText(const Document& doc,ObjectId id){
    std::string plain;for(const auto& run:doc.runs(id))plain+=run.utf8;
    if(plain.empty())return false;
    return tidy(plain,true,doc.runs(id),readColors(doc,ObjectRef(ObjectKind::Text,id)),false).changed;
}
// Returns an error only when the document rejects a write; *changed reports
// whether this object actually needed work.
inline Error tidyAtom(Document& doc,ObjectId id,bool* changed){
    if(changed)*changed=false;
    std::string plain=doc.atomLabel(id);
    if(plain.empty())return Error();
    ObjectRef ref(ObjectKind::Atom,id);
    std::vector<TextRun> old=studioatom::runs(doc,id);
    std::vector<uint32_t> colors=readColors(doc,ref);
    Tidy result=tidy(plain,false,old,colors,true);
    if(!result.changed||result.runs.empty())return Error();
    Error e=doc.setAtomLabel(id,result.plain);
    if(!e.ok())return e;
    studioatom::set(doc,id,result.runs);
    if(result.hasColors)doc.extensions(ref).set("studio.text.colors",writeColors(result.colors));
    if(changed)*changed=true;
    return Error();
}
inline Error tidyText(Document& doc,ObjectId id,bool* changed){
    if(changed)*changed=false;
    const auto* text=doc.texts().alive(id);
    if(!text)return Error();
    std::string plain;
    for(const auto& run:doc.runs(id))plain+=run.utf8;
    if(plain.empty())return Error();
    ObjectRef ref(ObjectKind::Text,id);
    std::vector<TextRun> old=doc.runs(id);
    std::vector<uint32_t> colors=readColors(doc,ref);
    Tidy result=tidy(plain,true,old,colors,false);
    if(!result.changed||result.runs.empty())return Error();
    Error e=doc.setTextRuns(id,result.runs);
    if(!e.ok())return e;
    if(result.hasColors)doc.extensions(ref).set("studio.text.colors",writeColors(result.colors));
    if(changed)*changed=true;
    return Error();
}
}  // namespace texttidy
#endif
