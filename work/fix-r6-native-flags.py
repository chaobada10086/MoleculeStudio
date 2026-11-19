from pathlib import Path
p=Path('studio/cdx_binary.hpp');s=p.read_text(encoding='utf-8');needle='            std::string b;\n            if(std::string(e->Name())=="tlcspot"';assert needle in s;s=s.replace(needle,'''            // Native TLC flags are presence-only booleans (zero-byte payload).
            if((tag==0x801d&&(p->tag>=0xaa2&&p->tag<=0xaa5))||(tag==0x801f&&p->tag==0xab2)){
                std::string flag=a->Value();if(flag=="yes"||flag=="true"||flag=="1")field(out,p->tag,"");continue;
            }
            std::string b;
            if(std::string(e->Name())=="tlcspot"''',1);p.write_text(s,encoding='utf-8')
