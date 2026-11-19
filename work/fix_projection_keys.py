from pathlib import Path
p=Path('studio/app_actions.hpp');s=p.read_text(encoding='utf-8');Path('work/projection-edit-backup/app_actions.hpp').write_bytes(p.read_bytes())
s=s.replace('bool App::shortcut(UINT key,uint32_t mask,bool editor){','''bool App::shortcut(UINT key,uint32_t mask,bool editor){
    // Resolve projection endpoints before letter shortcuts consume WM_CHAR.
    if(!editor&&!textEditor.active&&key>='A'&&key<='Z'&&!(mask&(kModControl|kModAlt))&&pointerOverCanvas&&!down&&!bioPlacing&&!ringPlacing&&!patternPlacing&&!radicalPlacing&&!virtualBondTool&&!phenalenePlacing&&!equivalencePlacement&&!partialChargePlacement&&editProjectionEndpoint(s.view().toDocument(pointerPixel))){
        textEditor.select(0,-1);swallowHotkeyChar=key=='T';return true;
    }''',1);p.write_text(s,encoding='utf-8')
p=Path('studio/projection_checks.hpp');s=p.read_text(encoding='utf-8').replace("studio_i18n::sendMessage(app.canvas,WM_CHAR,'O',0);studio_i18n::sendMessage(app.canvas,WM_CHAR,'H',0);", "studio_i18n::sendMessage(app.canvas,WM_KEYDOWN,'O',0);studio_i18n::sendMessage(app.canvas,WM_CHAR,'O',0);studio_i18n::sendMessage(app.canvas,WM_KEYDOWN,'H',0);studio_i18n::sendMessage(app.canvas,WM_CHAR,'H',0);")
p.write_text(s,encoding='utf-8')
