from pathlib import Path
p=Path('studio/cdx_binary.hpp');s=p.read_text(encoding='utf-8');needle='        if(p->name==std::string("ChainAngle"))';idx=s.index(needle);s=s[:idx]+'''        // ChemDraw's TLC-only CDXML dimensions are already 16.16 integers.
        if(std::string(e->Name())=="tlcspot"&&(tag==0x812||tag==0x813||tag==0xab1)){e->SetAttribute(p->name,number(double(r.signedValue(4))).c_str());return;}
'''+s[idx:]
needle='            std::string b;if(value(*p,a->Value(),b))field(out,p->tag,b);';assert needle in s
s=s.replace(needle,'''            std::string b;
            if(tag==0x8025&&(p->tag==0x812||p->tag==0x813||p->tag==0xab1)){
                double raw=std::strtod(a->Value(),nullptr);if(!std::isfinite(raw)||raw<-2147483648.||raw>2147483647.)throw std::runtime_error("TLC coordinate out of range");put(b,uint32_t(int32_t(std::round(raw))),4);field(out,p->tag,b);continue;
            }
            if(value(*p,a->Value(),b))field(out,p->tag,b);''',1);p.write_text(s,encoding='utf-8')
