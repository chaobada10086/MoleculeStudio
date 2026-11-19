from pathlib import Path
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8')
needle='bool roleColumn=ribbonTab==6&&!g.items.empty()&&g.items.front().id=="mechanism.base";'
assert s.count(needle)==1
s=s.replace(needle,'if(ribbonTab==5&&!onlyIcons){groupCell=52.f;for(const auto& b:g.items)groupCell=std::max(groupCell,uiTextWidth(b.label,10.f)+38.f);}'+needle)
needle='            if(!handle&&(pointerModifiers&kModControl)'
pos=s.index(needle)
s=s[:pos]+'''            if(!handle&&!(pointerModifiers&kModControl)){auto hit=s.pick(point);if(hit.kind==ObjectKind::Arrow){arrows::ArrowParams params;bool valid=false;if(arrows::readArrow(s.doc,hit.id,&params,&valid).ok()&&valid&&!arrows::isMechanismArrow(params)&&!params.virtualBond&&length(point-arrows::arcMidpoint(params.begin,params.end,params.arcSagitta))<=s.view().deviceRadiusToDocument(6)){auto preset=arrows::presetById(params.presetId);if(preset){s.selected={hit};s.activateCommand(preset->sourceCommand);a->selectionArrowEdit=handle=true;}}}}
'''+s[pos:]
p.write_text(s,encoding='utf-8')
