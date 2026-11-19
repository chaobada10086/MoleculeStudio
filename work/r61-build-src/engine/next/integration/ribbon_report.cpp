#include "session.hpp"
#include "ribbon/ribbon_model.hpp"
#include <fstream>
#include <sstream>
#include <set>
#include <string>
#include <vector>
namespace r62 { namespace integration {
#include "next/integration/source_tools.hpp"
namespace {
std::string q(const std::string& s){
    bool n=s.find(',')!=std::string::npos||s.find('"')!=std::string::npos||s.find('\n')!=std::string::npos;
    if(!n)return s;std::string o="\"";for(size_t i=0;i<s.size();++i){if(s[i]=='"')o+="\"\"";else o+=s[i];}o+="\"";return o;
}
const char* kindName(ribbon::ItemKind k){switch(k){case ribbon::ItemKind::Large:return "Large";case ribbon::ItemKind::Small:return "Small";
    case ribbon::ItemKind::Split:return "Split";case ribbon::ItemKind::Gallery:return "Gallery";case ribbon::ItemKind::Toggle:return "Toggle";
    case ribbon::ItemKind::Separator:return "Separator";default:return "Label";}}
const char* srcName(ribbon::SourceKind k){switch(k){case ribbon::SourceKind::SourceCommand:return "SourceCommand";
    case ribbon::SourceKind::Action:return "Action";case ribbon::SourceKind::FileOp:return "FileOp";
    case ribbon::SourceKind::PropertyOp:return "PropertyOp";default:return "Unsupported";}}
void row(std::ostream& o,const std::string& tab,const std::string& group,const ribbon::Item& it){
    o<<q(tab)<<','<<q(group)<<','<<q(it.id)<<','<<q(it.label)<<','<<kindName(it.kind)<<','<<srcName(it.source)<<','
     <<ribbon::dispName(it.disp)<<','<<q(it.command)<<','<<it.action<<','<<it.value<<','<<q(it.hotkey)<<','<<q(it.owner)<<','<<q(it.tooltip)<<"\n";
}
}
int dumpRibbon(const std::string& path){
    std::ofstream o(path.c_str(),std::ios::binary);if(!o)return 1;o<<"\xEF\xBB\xBF";
    o<<"Tab,Group,ItemID,ItemLabel,Kind,SourceKind,Disposition,Command,Action,Value,Hotkey,Owner,Tooltip\n";
    const ribbon::Model& m=ribbon::ribbonModel();
    std::set<std::string> seen;size_t rows=0;
    for(size_t ti=0;ti<m.tabs.size();++ti)for(size_t gi=0;gi<m.tabs[ti].groups.size();++gi){
        const ribbon::Group& g=m.tabs[ti].groups[gi];
        for(size_t ii=0;ii<g.items.size();++ii){
            const ribbon::Item& it=g.items[ii];row(o,m.tabs[ti].title,g.title,it);++rows;
            if(it.source==ribbon::SourceKind::SourceCommand&&!it.command.empty())seen.insert(it.command);
            for(size_t ci=0;ci<it.children.size();++ci){const ribbon::Item& c=it.children[ci];row(o,m.tabs[ti].title,g.title,c);++rows;
                if(c.source==ribbon::SourceKind::SourceCommand&&!c.command.empty())seen.insert(c.command);}
        }
    }
    o.close();
    std::vector<std::string> missing;
    const size_t n=sizeof(sourceTools)/sizeof(sourceTools[0]);
    for(size_t i=0;i<n;++i)if(!seen.count(std::string(sourceTools[i].command)))missing.push_back(sourceTools[i].command);
    std::string dir=path.substr(0,path.find_last_of("/\\"));
    std::ofstream j((dir+"/ribbon_coverage.json").c_str());
    j<<"{\"tools_total\":"<<n<<",\"mapped_source_commands\":"<<seen.size()<<",\"model_rows\":"<<rows
     <<",\"missing\":"<<missing.size()<<",\"missing_commands\":[";
    for(size_t i=0;i<missing.size();++i)j<<(i?",":"")<<"\""<<missing[i]<<"\"";
    j<<"],\"tabs\":"<<m.tabs.size()<<",\"host_bound\":false}";
    j.close();
    return missing.empty()?0:2;
}
}}
