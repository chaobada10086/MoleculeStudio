#pragma once
// Search is presentation only: fuzzy candidates never change chemical input.
namespace completionsearch {
inline std::string normalized(const std::string& text){
 int n=MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,0,0);if(n<1)return "";
 std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,&w[0],n);
 std::string out;for(wchar_t c:w){
  if(c==0||c==L' '||c==L'\t'||c==L'-'||c==0x2010||c==0x2011||c==0x2212||c==L'^')continue;
  if(c>=0xff01&&c<=0xff5e)c-=0xfee0;
  if(c>=0x2080&&c<=0x2089)c=L'0'+c-0x2080;
  if(c==0x2070)c=L'0';if(c==0xb9)c=L'1';if(c==0xb2)c=L'2';if(c==0xb3)c=L'3';if(c>=0x2074&&c<=0x2079)c=L'4'+c-0x2074;
  if(c==0x207a||c==0x208a)c=L'+';if(c==0x207b||c==0x208b)c=L'-';
  if(c==L'-')continue;
  if(c>=L'A'&&c<=L'Z')c+=32;if(c<128)out+=char(c);else out+='?';
 }return out;
}
inline bool letters(const std::string& v){return !v.empty()&&std::all_of(v.begin(),v.end(),[](char c){return c>='a'&&c<='z';});}
inline int rank(const std::string& query,const std::string& value){
 auto q=normalized(query),v=normalized(value);if(q.empty())return 0;if(q==v)return 0;
 auto at=v.find(q);if(at!=std::string::npos)return 10+int(at);
 // Numeric formulas get exact/substring matches only; no chemical near-misses.
 if(q.size()<3||!letters(q)||!letters(v))return -1;
 if(v.size()>=q.size()&&v.size()<=q.size()+2){size_t j=0;for(char c:v)if(j<q.size()&&c==q[j])++j;if(j==q.size())return 100+int(v.size()-q.size());}
 // One typo, insertion/deletion, or adjacent transposition, only for longer words.
 if(q.size()<4||v.size()+1<q.size()||q.size()+1<v.size())return -1;
 std::vector<int> prev(v.size()+1),cur(v.size()+1);for(size_t j=0;j<=v.size();++j)prev[j]=int(j);
 for(size_t i=1;i<=q.size();++i){cur[0]=int(i);for(size_t j=1;j<=v.size();++j)cur[j]=std::min({prev[j]+1,cur[j-1]+1,prev[j-1]+(q[i-1]!=v[j-1])});prev.swap(cur);}
 if(prev.back()==1)return 200;
 if(q.size()==v.size())for(size_t i=0;i+1<q.size();++i){auto t=q;std::swap(t[i],t[i+1]);if(t==v)return 200;}
 return -1;
}
inline std::string preview(const std::string& formula){
 const char* sub[]={"₀","₁","₂","₃","₄","₅","₆","₇","₈","₉"};const char* sup[]={"⁰","¹","²","³","⁴","⁵","⁶","⁷","⁸","⁹"};
 std::string out;for(auto r:chemcompletion::segments(formula,true))for(char c:r.text){if(c>='0'&&c<='9'&&r.script)out+=(r.script<0?sub:sup)[c-'0'];else if(r.script>0&&c=='+')out+="⁺";else if(r.script>0&&c=='-')out+="⁻";else out+=c;}return out.empty()?formula:out;
}
struct Hit{chemcompletion::Entry entry;bool formula=false;int score=0;};
inline std::vector<Hit> search(const std::string& query){
 std::vector<Hit> out;std::set<std::string> names;
 for(auto e:chemcompletion::list()){
  int score=rank(query,e.name),alias=rank(query,e.key);if(alias>=0&&(score<0||alias<score))score=alias;
  if(score>=0){Hit h;h.entry=e;h.score=score;out.push_back(h);names.insert(normalized(e.name));}
 }
 if(!normalized(query).empty())for(auto f:chemcompletion::commonFormulas()){
  int score=rank(query,f.second);if(score<0||names.count(normalized(f.second)))continue;
  Hit h;h.entry.name=f.second;h.entry.key="";h.formula=true;h.score=score;out.push_back(h);
 }
 std::stable_sort(out.begin(),out.end(),[](const Hit&a,const Hit&b){return a.score!=b.score?a.score<b.score:a.entry.name<b.entry.name;});return out;
}
}
