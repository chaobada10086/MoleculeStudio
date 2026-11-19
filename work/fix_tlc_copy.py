from pathlib import Path
p=Path('studio/tlc_actions.hpp');s=p.read_text(encoding='utf-8');s=s.replace('copy.u=std::max(-.98,std::min(.98,original.u+target.u-lane->u));','copy.u=target.u;');p.write_text(s,encoding='utf-8')
