from pathlib import Path
p=Path('studio/arrow_shortcut_checks.hpp');s=p.read_text(encoding='utf-8');s=s.replace('auto& preset=arrows::arrowPresets()[i];if(!preset.implemented)continue;','auto& stored=arrows::arrowPresets()[i];if(!stored.implemented)continue;auto& preset=*(stored.family==arrows::ArrowFamily::Arc?arrows::presetById(58):&stored);')
s=s.replace('"chooseArrowTool_270_CW"','arrows::presetById(58)->sourceCommand')
s=s.replace('        require(s.doc.arrows().size()==before+1,"shortcut-selected tool draws an arrow");','''        if(preset->presetId==58){auto bend=Vec2(-150,y+25);s.gesture(PointerPhase::Move,bend);s.gesture(PointerPhase::Down,bend);s.gesture(PointerPhase::Up,bend);}
        require(s.doc.arrows().size()==before+1,"shortcut-selected tool draws an arrow");''')
p.write_text(s,encoding='utf-8')
