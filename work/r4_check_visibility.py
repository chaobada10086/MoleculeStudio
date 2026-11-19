from pathlib import Path
p=Path('studio/current_fixes_checks.hpp');s=p.read_text(encoding='utf-8');s=s.replace('void App::currentFixesCheck(const std::string& dir){','void App::currentFixesCheck(const std::string& dir){\n    ribbonVisible=true;ribbonCompact=false;');p.write_text(s,encoding='utf-8')
