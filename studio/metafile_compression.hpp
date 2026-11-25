#pragma once

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace embedded {
// A bounded RFC1950/1951 decoder for CDXML's CompressedEnhancedMetafile.
// This keeps imports independent of installed ChemDraw and external programs.
struct Bits{const std::vector<unsigned char>& b;size_t bit;explicit Bits(const std::vector<unsigned char>& data):b(data),bit(16){} unsigned get(unsigned n){if(n>24||bit+n>b.size()*8)throw std::runtime_error("truncated deflate");unsigned v=0;for(unsigned i=0;i<n;++i,++bit)v|=((b[bit/8]>>(bit%8))&1)<<i;return v;}};
struct Huffman{
 std::map<unsigned,unsigned> codes;unsigned max=0;
 Huffman(const std::vector<unsigned>& lengths){unsigned count[16]={},next[16]={};for(auto n:lengths){if(n>15)throw std::runtime_error("invalid deflate tree");if(n)++count[n];max=std::max(max,n);}unsigned code=0;for(unsigned n=1;n<=15;++n){code=(code+count[n-1])<<1;next[n]=code;}
  for(unsigned i=0;i<lengths.size();++i){unsigned n=lengths[i];if(!n)continue;unsigned c=next[n]++,rev=0;for(unsigned j=0;j<n;++j)rev=(rev<<1)|((c>>j)&1);codes[(1u<<n)|rev]=i;}}
 unsigned read(Bits& b)const{unsigned code=0;for(unsigned n=1;n<=max;++n){code|=b.get(1)<<(n-1);auto it=codes.find((1u<<n)|code);if(it!=codes.end())return it->second;}throw std::runtime_error("invalid deflate code");}
};
inline std::vector<unsigned char> inflate(const std::vector<unsigned char>& data,size_t expected){
 if(data.size()<6||expected>32*1024*1024||!expected||(data[0]&15)!=8||((unsigned(data[0])*256+data[1])%31)||(data[1]&32))throw std::runtime_error("invalid zlib stream");
 Bits bits{data};std::vector<unsigned char> out;out.reserve(expected);bool last=false;
 const unsigned lb[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
 const unsigned le[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
 const unsigned db[]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
 const unsigned de[]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
 while(!last){last=bits.get(1);unsigned type=bits.get(2);if(type==0){bits.bit=(bits.bit+7)&~size_t(7);unsigned n=bits.get(16),inv=bits.get(16);if((n^inv)!=65535||out.size()+n>expected)throw std::runtime_error("invalid stored block");while(n--)out.push_back(bits.get(8));continue;}
  std::vector<unsigned> lit,dist;
  if(type==1){lit.resize(288);for(unsigned i=0;i<288;++i)lit[i]=i<144?8:i<256?9:i<280?7:8;dist.assign(32,5);}
  else if(type==2){unsigned nl=bits.get(5)+257,nd=bits.get(5)+1,nc=bits.get(4)+4;const unsigned order[]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};std::vector<unsigned> lengths(19),all;for(unsigned i=0;i<nc;++i)lengths[order[i]]=bits.get(3);Huffman tree(lengths);
   while(all.size()<nl+nd){unsigned c=tree.read(bits);if(c<16)all.push_back(c);else{unsigned n=c==16?bits.get(2)+3:c==17?bits.get(3)+3:bits.get(7)+11;if(c==16&&all.empty())throw std::runtime_error("invalid repeat");unsigned v=c==16?all.back():0;if(all.size()+n>nl+nd)throw std::runtime_error("long tree");all.insert(all.end(),n,v);}}lit.assign(all.begin(),all.begin()+nl);dist.assign(all.begin()+nl,all.end());}
  else throw std::runtime_error("reserved block");Huffman lt(lit),dt(dist);
  for(;;){unsigned c=lt.read(bits);if(c==256)break;if(c<256){if(out.size()==expected)throw std::runtime_error("long output");out.push_back(c);}else{if(c>285)throw std::runtime_error("bad length");unsigned n=lb[c-257]+bits.get(le[c-257]),d=dt.read(bits);if(d>=30)throw std::runtime_error("bad distance");unsigned back=db[d]+bits.get(de[d]);if(back>out.size()||out.size()+n>expected)throw std::runtime_error("bad backreference");while(n--)out.push_back(out[out.size()-back]);}}
 }
 if(out.size()!=expected)throw std::runtime_error("short output");return out;
}
inline std::vector<unsigned char> decode(const std::string& value,bool base64){
 std::vector<unsigned char> out;unsigned bits=0,count=0;const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 for(unsigned char c:value){int v=-1;if(base64){auto n=alphabet.find(c);if(n!=std::string::npos)v=int(n);}else if(c>='0'&&c<='9')v=c-'0';else if(c>='A'&&c<='F')v=c-'A'+10;else if(c>='a'&&c<='f')v=c-'a'+10;
  if(v<0)continue;bits=(bits<<(base64?6:4))|unsigned(v);count+=base64?6:4;if(count>=8){count-=8;out.push_back((bits>>count)&255);}}
 return out;
}
}
