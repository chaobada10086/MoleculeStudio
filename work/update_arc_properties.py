from pathlib import Path
p=Path('studio/app_actions.hpp');s=p.read_text(encoding='utf-8')
s='#include "arc_properties.hpp"\n'+s
s=s.replace('bool available=false,curvatureAvailable=false,curvatureMajor=false;double ratio=0,curvature=0;','bool available=false,curvatureAvailable=false,curvatureMajor=false,centerAvailable=false,radiusAvailable=false,startAvailable=false;double ratio=0,curvature=0,radius=0,start=0;Vec2 center;')
start=s.index('    studio_i18n::appendMenu(menu,MF_STRING,40,L"复制弧度");');end=s.index('\n}',start)
s=s[:start]+'''    auto& clip=arcBendClipboard();
    studio_i18n::appendMenu(menu,MF_STRING,48,L"复制圆心");if(clip.centerAvailable)studio_i18n::appendMenu(menu,MF_STRING,49,L"粘贴圆心");
    studio_i18n::appendMenu(menu,MF_STRING,50,L"复制半径");if(clip.radiusAvailable&&!s.arcCurvatureLocked)studio_i18n::appendMenu(menu,MF_STRING,51,L"粘贴半径");
    studio_i18n::appendMenu(menu,MF_STRING,40,L"复制弧度");if(clip.available&&!s.arcBendLocked)studio_i18n::appendMenu(menu,MF_STRING,41,L"粘贴弧度");
    studio_i18n::appendMenu(menu,MF_STRING,44,L"复制曲率");if(clip.curvatureAvailable&&!s.arcCurvatureLocked)studio_i18n::appendMenu(menu,MF_STRING,45,L"粘贴曲率");
    studio_i18n::appendMenu(menu,MF_STRING,52,L"复制起始角度（相对于画布）");if(clip.startAvailable)studio_i18n::appendMenu(menu,MF_STRING,53,L"粘贴起始角度");
    studio_i18n::appendMenu(menu,MF_SEPARATOR,0,nullptr);
    studio_i18n::appendMenu(menu,MF_STRING,s.arcBendLocked?43:42,s.arcBendLocked?L"解锁弧度":L"锁定此弧度");
    studio_i18n::appendMenu(menu,MF_STRING,s.arcCurvatureLocked?47:46,s.arcCurvatureLocked?L"解锁曲率":L"锁定此曲率");'''+s[end:]
start=s.index('    if(action==40){arcBendClipboard()');end=s.index('    }else if(action==42',start)
s=s[:start]+'''    auto circle=arc_properties::read(p);auto& clip=arcBendClipboard();
    if(action==48||action==50||action==52){if(!circle.valid){status="直线或非圆弧没有可复制的圆心、半径或起始角度";invalidate();return;}
        if(action==48){clip.center=circle.center;clip.centerAvailable=true;status="已复制圆心";}
        if(action==50){clip.radius=circle.radius;clip.radiusAvailable=true;status="已复制半径";}
        if(action==52){clip.start=circle.start;clip.startAvailable=true;status="已复制起始角度 · 画布向右 0°、向下 90°";}
    }
    else if(action==40){clip.ratio=ratio;clip.available=true;status="已复制弧度（圆心角）";}
    else if(action==44){clip.curvature=curvature;clip.curvatureMajor=std::abs(p.arcSagitta)>chord*.5;clip.curvatureAvailable=true;status="已复制曲率";}
    else if(action==41||action==45||action==49||action==51||action==53){
        if((action==41&&(s.arcBendLocked||!clip.available))||(action==45&&(s.arcCurvatureLocked||!clip.curvatureAvailable))||(action==49&&!clip.centerAvailable)||(action==51&&(s.arcCurvatureLocked||!clip.radiusAvailable))||(action==53&&!clip.startAvailable))return;
        if(circle.valid){
            if(action==41)circle.sweep=-4*std::atan(2*clip.ratio);
            if(action==49)circle.center=clip.center;
            if(action==51)circle.radius=clip.radius;
            if(action==53)circle.start=clip.start;
            if(action==45){if(std::abs(clip.curvature)<1e-9){status="零曲率为直线，无法在保持有限圆心和圆心角时粘贴";invalidate();return;}circle.radius=1/std::abs(clip.curvature);}
            if(!arc_properties::write(circle,p)){status="该参数无法组成有效圆弧";invalidate();return;}
        }else if(action==41||action==45){p.arcSagitta=action==41?chord*clip.ratio:arrows::sagittaAtCurvature(clip.curvature,chord,clip.curvatureMajor);p.family=std::abs(p.arcSagitta)>1e-8?arrows::ArrowFamily::Arc:arrows::ArrowFamily::Straight;p.sweep=p.family==arrows::ArrowFamily::Arc?arrows::ArcSweep::Free:arrows::ArcSweep::Straight;p.route.clear();}
        else{status="目标必须是圆弧";invalidate();return;}
        const char* label=action==41?"粘贴弧度":action==45?"粘贴曲率":action==49?"粘贴圆心":action==51?"粘贴半径":"粘贴起始角度";
        auto e=s.submit(Command(label,kInvalidateGeometry|kInvalidateAppearance,[p,target](CommandContext& c){auto error=arrows::writeArrow(c.document,target,p,c.document.defaultLineWidth());if(error.ok()){auto ref=ObjectRef(ObjectKind::Arrow,target);c.document.noteTouchedExtensions(ref,false);c.document.extensions(ref).erase("studio.cdxml.graphic");}return error.ok()?CommandResult::success():CommandResult::fail(error.code,error.message);}),label);if(!e.ok()){error(e);return;}status=std::string("已")+label;
'''+s[end:]
p.write_text(s,encoding='utf-8')
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8').replace('choice>=40&&choice<=47','choice>=40&&choice<=53');p.write_text(s,encoding='utf-8')
p=Path('studio/engine/arrows_tools.cpp');s=p.read_text(encoding='utf-8').replace('if (haveBase) p = base;','if (haveBase) p = base;\n        editingArc=haveBase&&base.family==ArrowFamily::Arc;').replace('(state_.target!=core::kInvalidObjectId&&base.family==ArrowFamily::Arc)','editingArc');p.write_text(s,encoding='utf-8')
