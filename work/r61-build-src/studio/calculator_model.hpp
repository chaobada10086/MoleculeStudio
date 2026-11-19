#pragma once
#include <cerrno>
namespace yieldcalc {
using namespace r62;using namespace core;
inline bool number(const std::wstring& text,double& value,bool positive=false){
    const wchar_t* begin=text.c_str();wchar_t* end=nullptr;errno=0;
    value=std::wcstod(begin,&end);if(begin==end||errno==ERANGE||!std::isfinite(value))return false;
    while(*end&&std::iswspace(*end))++end;
    return !*end&&(positive?value>0:value>=0);
}
inline std::wstring compact(double value,int precision=7){std::wostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(precision)<<value;return out.str();}
inline double massFactor(int unit){const double factors[]={1.,.001,.000001,1000.};return factors[std::max(0,std::min(3,unit))];}
inline double amountFactor(int unit){const double factors[]={1.,.001,1.,.000001};return factors[std::max(0,std::min(3,unit))];}
inline int amountUnit(double mol){return mol<.1?1:2;}
inline bool convert(double input,double molecularWeight,bool fromMass,double& grams,double& mol){
    if(!std::isfinite(input)||input<0||!std::isfinite(molecularWeight)||molecularWeight<=0)return false;
    grams=fromMass?input:input*molecularWeight;mol=fromMass?input/molecularWeight:input;
    return std::isfinite(grams)&&std::isfinite(mol);
}
inline bool percent(double mol,double startMol,double coefficient,double startCoefficient,double& value){
    if(!std::isfinite(mol)||mol<0||!std::isfinite(startMol)||startMol<=0||
       !std::isfinite(coefficient)||coefficient<=0||!std::isfinite(startCoefficient)||startCoefficient<=0)return false;
    value=100.*(mol/startMol)*(startCoefficient/coefficient);return std::isfinite(value);
}
struct Row {
    std::vector<ObjectRef> refs;Formula formula;Box bounds;
    int role=2,massUnit=0,amountDisplay=0; // role: reactant, reagent, product
    double coefficient=1,grams=0,mol=0,yield=0,initialMol=0,initialCoefficient=1;bool valid=false,yieldValid=false,include=true;
    std::string problem;
    std::shared_ptr<Session> preview;
    std::string key()const{return namelink::encodeRefs(refs);}
};
inline std::vector<Row> collect(Session& session,const std::vector<ObjectRef>& selected){
    const Document& doc=session.doc;auto chosen=studioio::expanded(doc,selected);
    std::set<ObjectId> seeds,seen;std::vector<Row> rows;
    for(auto ref:chosen){if(ref.kind==ObjectKind::Atom&&doc.atoms().alive(ref.id))seeds.insert(ref.id);
        if(ref.kind==ObjectKind::Text&&doc.texts().alive(ref.id)&&compounddata::find(doc.plainText(ref.id))){Row row;row.refs={ref};rows.push_back(row);}}
    for(auto seed:seeds)if(seen.insert(seed).second){Row row;std::vector<ObjectId> queue{seed};
        for(size_t i=0;i<queue.size();++i){auto id=queue[i];row.refs.emplace_back(ObjectKind::Atom,id);
            for(auto bid:doc.incidentBonds(id)){auto b=doc.bonds().alive(bid);auto next=b->beginAtom==id?b->endAtom:b->beginAtom;if(seen.insert(next).second)queue.push_back(next);}}
        std::set<ObjectId> atoms(queue.begin(),queue.end());
        for(const auto& b:doc.bonds().records())if(b.isAlive()&&atoms.count(b.beginAtom)&&atoms.count(b.endAtom))row.refs.emplace_back(ObjectKind::Bond,b.id);
        rows.push_back(row);
    }
    arrows::ArrowParams reaction;bool haveArrow=false;double longest=0;
    for(auto ref:chosen)if(ref.kind==ObjectKind::Arrow){arrows::ArrowParams p;bool ok=false;
        if(arrows::readArrow(doc,ref.id,&p,&ok).ok()&&ok&&!arrows::isMechanismArrow(p)&&!p.virtualBond&&length(p.end-p.begin)>longest){reaction=p;longest=length(p.end-p.begin);haveArrow=true;}}
    for(auto& row:rows){row.formula=studio::analyze(doc,row.refs);row.bounds=Box(Vec2(1,1),Vec2(-1,-1));
        std::set<ObjectRef> refs(row.refs.begin(),row.refs.end());
        for(const auto& p:session.scene.primitives())if(refs.count(p.source)&&!p.bounds.empty()){if(row.bounds.empty())row.bounds=p.bounds;else{row.bounds.add(p.bounds.min);row.bounds.add(p.bounds.max);}}
        if(row.bounds.empty())for(auto ref:row.refs)if(ref.kind==ObjectKind::Atom){auto p=doc.atoms().alive(ref.id)->position;if(row.bounds.empty())row.bounds=Box(p,p);else row.bounds.add(p);}
        if(haveArrow){auto direction=reaction.end-reaction.begin;if(reaction.head.atBegin&&!reaction.head.atEnd)direction=direction*-1;
            row.role=dot((row.bounds.min+row.bounds.max)*.5-(reaction.begin+reaction.end)*.5,direction)<0?0:2;}
        row.preview=std::make_shared<Session>();studioio::append(doc,row.preview->doc,row.refs,Vec2(),nullptr);row.preview->refresh();
        for(const auto& t:doc.texts().records())if(t.isAlive()){
            const auto& bag=doc.extensions(ObjectRef(ObjectKind::Text,t.id));auto key=bag.findValue("studio.yield.refs");
            if(key&&*key==row.key()){double value=0;auto raw=bag.findValue("studio.yield.grams");if(raw&&number(W(*raw),value)&&row.formula.valid){row.grams=value;row.mol=value/row.formula.averageMass;row.valid=true;}
                if(auto role=bag.findValue("studio.yield.role"))row.role=std::max(0,std::min(2,std::atoi(role->c_str())));
                if(auto coefficient=bag.findValue("studio.yield.coefficient"))if(number(W(*coefficient),value,true))row.coefficient=value;
                if(auto initial=bag.findValue("studio.yield.initialMol"))if(number(W(*initial),value,true))row.initialMol=value;
                if(auto coefficient=bag.findValue("studio.yield.initialCoefficient"))if(number(W(*coefficient),value,true))row.initialCoefficient=value;
                break;}
        }
    }
    std::stable_sort(rows.begin(),rows.end(),[&](const Row& a,const Row& b){return a.bounds.min.y<b.bounds.min.y;});
    // Sort each visual reaction row from left to right without a non-transitive comparator.
    for(size_t first=0;first<rows.size();){size_t last=first+1;while(last<rows.size()&&rows[last].bounds.min.y-rows[first].bounds.min.y<doc.defaultBondLength()*2)++last;
        std::stable_sort(rows.begin()+first,rows.begin()+last,[](const Row& a,const Row& b){return a.bounds.min.x<b.bounds.min.x;});first=last;}
    return rows;
}
inline std::string label(const Row& row,int massUnit=0){
    // Annotation unit 0 is automatic; other entries are g, mg and µg.
    int unit=massUnit?massUnit-1:row.grams>=1?0:row.grams>=.001?1:2;
    const char* names[]={"g","mg","µg"};double value=row.grams/massFactor(unit);
    std::ostringstream text;text.imbue(std::locale::classic());
    if(value!=0&&value<.1)text<<std::setprecision(3)<<value;else text<<std::fixed<<std::setprecision(1)<<value;
    text<<' '<<names[unit];
    if(row.role==2&&row.yieldValid){std::ostringstream y;y.imbue(std::locale::classic());y<<std::fixed<<std::setprecision(1)<<row.yield;auto value=y.str();if(value.size()>2&&value.substr(value.size()-2)==".0")value.resize(value.size()-2);text<<"\nYield = "<<value<<" %";}
    return text.str();
}
inline Error insert(Session& session,const std::vector<Row>& rows,int unit){
    std::vector<Row> ready;for(const auto& row:rows)if(row.include&&row.valid&&row.formula.valid&&row.coefficient>0&&std::isfinite(row.coefficient))ready.push_back(row);
    if(ready.empty())return Error(ErrorCode::InvalidArgument,"请先填写至少一个有效的质量或物质的量。");
    return session.submit(Command("插入质量与产率",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,[ready,unit](CommandContext& c){
        auto& doc=c.document;std::vector<Box> placed;
        for(const auto& row:ready){ObjectId target=kInvalidObjectId;
            for(const auto& t:doc.texts().records())if(t.isAlive()){auto key=doc.extensions(ObjectRef(ObjectKind::Text,t.id)).findValue("studio.yield.refs");if(key&&*key==row.key()){target=t.id;break;}}
            const auto text=label(row,unit);double size=doc.defaultTextSize();Vec2 at((row.bounds.min.x+row.bounds.max.x)*.5,row.bounds.max.y+size*1.6);
            size_t width=0,start=0;for(size_t i=0;i<=text.size();++i)if(i==text.size()||text[i]=='\n'){width=std::max(width,i-start);start=i+1;}
            const double half=std::max(size*2.,width*size*.28),height=size*(row.role==2&&row.yieldValid?2.6:1.3);
            Box area(Vec2(at.x-half,at.y-size),Vec2(at.x+half,at.y+height));
            for(const auto& other:placed)if(area.min.x<other.max.x&&area.max.x>other.min.x&&area.min.y<other.max.y&&area.max.y>other.min.y){double delta=other.max.y-area.min.y+size*.5;at.y+=delta;area.min.y+=delta;area.max.y+=delta;}placed.push_back(area);
            Error error;if(target==kInvalidObjectId)error=doc.addText(at,size,text,&target);
            else{error=doc.setTextPlain(target,text);if(error.ok())error=doc.setTextPosition(target,at);}
            if(!error.ok())return CommandResult::fail(error.code,error.message);
            TextFormat format;format.fontName=doc.defaultFont();format.size=size;format.align=1;format.colorRGBA=0x000000FFu;
            error=doc.setTextFormat(target,format);if(!error.ok())return CommandResult::fail(error.code,error.message);
            auto& bag=doc.extensions(ObjectRef(ObjectKind::Text,target));bag.set("studio.yield.refs",row.key());bag.set("studio.yield.grams",U(compact(row.grams,15)));bag.set("studio.yield.mol",U(compact(row.mol,15)));bag.set("studio.yield.role",std::to_string(row.role));bag.set("studio.yield.coefficient",U(compact(row.coefficient,15)));
            bag.set("studio.yield.initialMol",U(compact(row.initialMol,15)));bag.set("studio.yield.initialCoefficient",U(compact(row.initialCoefficient,15)));
        }
        return CommandResult::success();
    }),"插入质量与产率");
}
}
