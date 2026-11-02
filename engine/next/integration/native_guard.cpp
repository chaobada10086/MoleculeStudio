#include "session.hpp"
#include <sstream>
#include <iomanip>
namespace r62 { namespace integration {
// Temporary adapter for a measured T01 v10000 defect: its serializer omits
// Table/Group slabs. Store exactly those public records in a persistent extension
// through T02, then restore them. No public model or T01 format is redefined.
// Remove this adapter only after a versioned upstream fix and migration tests.
static const char* key="integration.t19.omitted-slabs.v1";
Error saveSafeNative(const Document& d,std::ostream& out){
    if(d.tables().empty()&&d.groups().empty())return persistence::saveNative(d,out);
    if(d.documentExtensions().find(key))return Error(ErrorCode::Unsupported,"Reserved T19 recovery key collision");
    Document copy=d;std::ostringstream s;s<<std::setprecision(17)<<"1 "<<d.tables().size()<<' '<<d.groups().size()<<'\n';
    for(const auto& t:d.tables().records()){
        s<<t.id<<' '<<int(t.alive)<<' '<<t.sourceId<<' '<<t.frame.min.x<<' '<<t.frame.min.y<<' '<<t.frame.max.x<<' '<<t.frame.max.y<<' '<<t.rows<<' '<<t.columns<<' '<<t.rowHeight<<' '<<t.cells.size();
        for(auto c:t.cells){s<<' '<<c;} s<<'\n';
    }
    for(const auto& g:d.groups().records()){
        s<<g.id<<' '<<int(g.alive)<<' '<<g.sourceId<<' '<<g.zIndex<<' '<<core::hexEncode(g.label)<<' '<<g.members.size();
        for(auto r:g.members){s<<' '<<int(r.kind)<<' '<<r.id;} s<<'\n';
    }
    copy.documentExtensions().set(key,s.str());return persistence::saveNative(copy,out);
}
Error loadSafeNative(Document& d,std::istream& input){
    Document scratch;Error e=persistence::loadNative(scratch,input,0,persistence::UnknownTypePolicy::Preserve);if(!e.ok())return e;
    const std::string* payload=static_cast<const Document&>(scratch).documentExtensions().findValue(key);
    if(payload){
        std::istringstream s(*payload);int version;size_t nt,ng;
        if(!(s>>version>>nt>>ng)||version!=1||nt>1000000||ng>1000000)return Error(ErrorCode::ParseError,"Malformed T19 recovery header");
        std::vector<TableRecord> tables;std::vector<GroupRecord> groups;
        for(size_t i=0;i<nt;++i){TableRecord t;int alive;size_t n;
            if(!(s>>t.id>>alive>>t.sourceId>>t.frame.min.x>>t.frame.min.y>>t.frame.max.x>>t.frame.max.y>>t.rows>>t.columns>>t.rowHeight>>n)||t.id!=i||n>1000000)return Error(ErrorCode::ParseError,"Malformed table recovery record");
            t.alive=uint8_t(alive);t.cells.resize(n);for(auto& c:t.cells)if(!(s>>c))return Error(ErrorCode::ParseError,"Truncated table recovery");tables.push_back(t);
        }
        for(size_t i=0;i<ng;++i){GroupRecord g;int alive;size_t n;std::string label;
            if(!(s>>g.id>>alive>>g.sourceId>>g.zIndex>>label>>n)||g.id!=i||n>1000000)return Error(ErrorCode::ParseError,"Malformed group recovery record");
            g.alive=uint8_t(alive);if(!core::hexDecode(label,&g.label).ok())return Error(ErrorCode::ParseError,"Malformed group label");
            for(size_t j=0;j<n;++j){int kind;ObjectId id;if(!(s>>kind>>id))return Error(ErrorCode::ParseError,"Truncated group recovery");g.members.push_back(ObjectRef(ObjectKind(kind),id));}groups.push_back(g);
        }
        scratch.tables().mutableRecords()=tables;scratch.groups().mutableRecords()=groups;scratch.documentExtensions().erase(key);scratch.rebuildDerived();
        e=scratch.validate();if(!e.ok())return e;
    }
    d=std::move(scratch);return Error();
}
} }

