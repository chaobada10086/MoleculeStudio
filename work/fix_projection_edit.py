from pathlib import Path
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8')
s=s.replace('void projectionMenu(int kind);','bool projectionChecking=false;void projectionMenu(int kind);',1)
needle='        if(a->beginElectronMarkerDrag(pixel,pointerModifiers))return 0;'
s=s.replace(needle,'''        // Projection captions are children of a group, so generic text picking
        // cannot reach them. Route before selection handles and free-text placement.
        if(s.entries[s.activeIndex].owner=="T09"){
            if(a->textEditor.active&&!a->finishText(true))return 0;
            if(a->editProjectionEndpoint(s.view().toDocument(pixel)))return 0;
        }
'''+needle,1)
needle='if(wp>32&&wp!=127&&!(mods()&kModControl)){a->route('
s=s.replace(needle,'if(wp>32&&wp!=127&&!(mods()&kModControl)){if(a->pointerOverCanvas&&!a->bioPlacing&&!a->ringPlacing&&!a->patternPlacing&&!a->radicalPlacing&&!a->virtualBondTool&&a->editProjectionEndpoint(s.view().toDocument(a->pointerPixel))){a->textEditor.select(0,-1);studio_i18n::sendMessage(a->edit,WM_CHAR,wp,lp);return 0;}a->route(',1)
s=s.replace('a.currentFixesChecking=argc>2&&first==L"--current-fixes-check";','a.projectionChecking=argc>2&&first==L"--projection-check";a.currentFixesChecking=a.projectionChecking||(argc>2&&first==L"--current-fixes-check");',1)
p.write_text(s,encoding='utf-8')
p=Path('studio/current_fixes_checks.hpp');s=p.read_text(encoding='utf-8').replace('void App::currentFixesCheck(const std::string& dir){','void App::currentFixesCheck(const std::string& dir){\n    if(projectionChecking){checkProjectionTemplates(*this,dir);s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;}',1);p.write_text(s,encoding='utf-8')
p=Path('studio/projection_actions.hpp');s=p.read_text(encoding='utf-8');s=s.replace('    double best=s.view()', '''    // Pick existing captions by their actual bounds, including long group names
    // outside the endpoint radius. A group pick otherwise hides its child text.
    for(const auto& g:s.doc.groups().records())if(g.isAlive()&&isProjectionGroupName(g.label))
        for(auto ref:g.members)if(ref.kind==ObjectKind::Text&&s.doc.texts().alive(ref.id)){
            auto box=selection::objectBounds(s.doc,ref);double pad=s.view().deviceRadiusToDocument(2);
            if(point.x>=box.min.x-pad&&point.x<=box.max.x+pad&&point.y>=box.min.y-pad&&point.y<=box.max.y+pad){openText(ref.id);return true;}
        }
    double best=s.view()''',1)
s=s.replace('std::vector<std::pair<Vec2,Vec2>> lines;Vec2 circle;', 'std::vector<Vec2> groupTips;bool chosen=false;std::vector<std::pair<Vec2,Vec2>> lines;Vec2 circle;',1)
s=s.replace('if(terminal)allTips.push_back(tip);','if(terminal)groupTips.push_back(tip);',1).replace('groupId=g.id;','groupId=g.id;chosen=true;',1)
s=s.replace('        }\n    }\n    if(groupId', '        }\n        if(chosen)allTips=groupTips;\n    }\n    if(groupId',1)
p.write_text(s,encoding='utf-8')
