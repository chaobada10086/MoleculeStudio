#pragma once
inline void fuzzySearchCheck(const std::string& dir){
 auto require=[](bool b,const char* why){if(!b)throw std::runtime_error(why);};
 using namespace completionsearch;
 auto find=[](const std::vector<Hit>& hits,const std::string& name)->const Hit*{for(const auto& h:hits)if(h.entry.name==name)return &h;return nullptr;};
 auto sulf=search("SO4");auto h=find(sulf,"SO4");require(h&&h->formula&&h->entry.key.empty(),"SO4 is a formula preview, never a structural nickname checkbox");require(find(sulf,"Na2SO4"),"formula substring includes sulfate salts");
 auto uni=search("ＳＯ₄");require(find(uni,"SO4"),"fullwidth and subscript digits normalized");require(normalized("SO42-")==normalized("SO₄²⁻")&&normalized("SO42-")==normalized("SO4^2-"),"charge spelling normalization");auto ion=search("SO₄²⁻");require(find(ion,"SO42-"),"ionic superscript query");
 for(const char* q:{"tbu","tBu"," t bu "}){auto rows=search(q);require(!rows.empty()&&rows.front().entry.name=="tBu"&&rows.front().score==0,"tBu normalization ranks exact first");}
 auto ph=search("ph");require(!ph.empty()&&ph.front().entry.name=="Ph","short case-folded exact");require(rank("oh","Ph")<0,"no fuzzy expansion for short symbols");require(rank("SO4","SO3")<0&&rank("SO4","PO4")<0,"no numeric chemical autocorrection");require(rank("tmep","temp")==200,"adjacent typo candidate");
 auto old=chemcompletion::state();struct Restore{chemcompletion::State v;~Restore(){chemcompletion::state()=v;}}restore{old};
 chemcompletion::state().custom["xtestalias"]="RareLabel";chemcompletion::state().revision++;
 auto alias=search("xtestalias");require(alias.size()==1&&alias[0].entry.name=="RareLabel"&&alias[0].entry.key=="xtestalias","alias lookup retains original checkbox key");
 auto canonical=search("RareLabel");require(find(canonical,"RareLabel"),"canonical spelling searchable");auto oldg=customgroups::entries();customgroups::entries().push_back({"Ns","test"});++chemcompletion::state().revision;require(find(search("ns"),"Ns"),"custom structure searchable");customgroups::entries()=oldg;
 require(search("zzzzzzzzzzzzzzzz").empty(),"unknown query empty");require(preview("SO42-")=="SO₄²⁻","formula preview script");
 std::ofstream(dir+"/fuzzy-search-checks.json")<<"{\"passed\":true,\"formula_scope\":true,\"unicode\":true,\"aliases\":true,\"original_keys\":true,\"ranking\":true,\"numeric_not_fuzzy\":true,\"custom_structure\":true,\"preview\":true}";
}
