from pathlib import Path
p=Path('studio/studio.cpp');s=p.read_text(encoding='utf-8')
s=s.replace('#include "branch_direction.hpp"','#include "branch_direction.hpp"\n#include "structure_perspective.hpp"')
s=s.replace('A("剪切","cut","cut");','')
# Column-major ribbon: first row selection, perspective, copy.
s=s.replace('A("全选","group","all");','A("全选","group","all");')
s=s.replace('A("撤销","undo","undo");A("重做","redo","redo");A("全选","group","all");','A("结构透视","perspective","perspective");A("撤销","undo","undo");A("重做","redo","redo");A("全选","group","all");')
s=s.replace('bool transformDragging=false;','bool perspectiveTool=false,perspectiveDragging=false,perspectiveChecking=false;\n    std::set<ObjectId> perspectiveAtoms;Vec2 perspectivePivot,perspectivePress;double perspectiveYaw=0,perspectivePitch=0;DocumentId perspectiveDocument=0;uint64_t perspectiveRevision=0;std::unique_ptr<Session> perspectivePreview;\n    bool beginStructurePerspective(Vec2 pixel);void updateStructurePerspective(Vec2 pixel,uint32_t modifiers);void finishStructurePerspective(Vec2 pixel,uint32_t modifiers);\n    bool transformDragging=false;')
s=s.replace('#include "selection_transform.hpp"','#include "structure_perspective_ui.hpp"\n#include "selection_transform.hpp"')
s=s.replace('case WM_LBUTTONDOWN:a->insertionPreviewPending=false;','case WM_LBUTTONDOWN:a->insertionPreviewPending=false;\n        if(a->beginStructurePerspective(pixel))return 0;')
s=s.replace('bool selected=b.cmd==s.entries[s.activeIndex].command||','bool selected=(perspectiveTool?b.cmd=="@perspective":b.cmd==s.entries[s.activeIndex].command)||')
s=s.replace('cancelSelectionTransform();\n    if(tableDragPending)', 'cancelSelectionTransform();\n    if(!b.cmd.empty()&&b.cmd[0]!=\'@\')perspectiveTool=false;\n    if(tableDragPending)')
s=s.replace('WORD canvasCursorId(Vec2 pixel){','WORD canvasCursorId(Vec2 pixel){if(perspectiveTool)return 32649;')
s=s.replace('else if(id=="cut")','else if(id=="perspective"){g.DrawEllipse(&p,4,4,21,21);L(3,9,9,14);L(9,14,18,14);L(18,14,14,10);L(18,14,14,18);L(10,3,10,8);}\n    else if(id=="cut")')
s=s.replace('#include "hover_delete_checks.hpp"','#include "hover_delete_checks.hpp"\n#include "structure_perspective_checks.hpp"')
s=s.replace('if(hoverDeleteChecking){','if(perspectiveChecking){checkStructurePerspective(*this,testOut);s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);return;}\n    if(hoverDeleteChecking){')
s=s.replace('    checkHoverDelete(*this,testOut);','    checkStructurePerspective(*this,testOut);\n    checkHoverDelete(*this,testOut);')
s=s.replace('a.hoverDeleteChecking=argc>2', 'a.perspectiveChecking=argc>2&&first==L"--perspective-check";a.hoverDeleteChecking=argc>2')
s=s.replace('first==L"--test"||a.hoverDeleteChecking','first==L"--test"||a.perspectiveChecking||a.hoverDeleteChecking')
p.write_text(s,encoding='utf-8')
p=Path('studio/extra_actions.hpp');s=p.read_text(encoding='utf-8');start=s.index('    if(op=="perspective")');end=s.index('\n',start)
s=s[:start]+'    if(op=="perspective"){s.cancel();s.activateCommand("chooseSelectionTool_Marquee");perspectiveTool=true;status="结构透视 (Structure Perspective)：选中结构后拖动，Shift 限制单轴，Esc 退出";updateCanvasCursor();invalidate();return true;}'+s[end:];p.write_text(s,encoding='utf-8')
p=Path('studio/selection_transform.hpp');s=p.read_text(encoding='utf-8');s=s.replace('void App::updateSelectionTransform(Vec2 pixel,uint32_t modifiers){','void App::updateSelectionTransform(Vec2 pixel,uint32_t modifiers){\n    if(perspectiveDragging){updateStructurePerspective(pixel,modifiers);return;}');s=s.replace('void App::finishSelectionTransform(Vec2 pixel,uint32_t modifiers){','void App::finishSelectionTransform(Vec2 pixel,uint32_t modifiers){\n    if(perspectiveDragging){finishStructurePerspective(pixel,modifiers);return;}');s=s.replace('    if(!transformDragging)return;transformDragging=false;','    if(!transformDragging)return;perspectiveDragging=false;perspectivePreview.reset();transformDragging=false;');s=s.replace('const render::Scene& App::selectionTransformScene(){','const render::Scene& App::selectionTransformScene(){\n    if(perspectiveDragging&&perspectivePreview)return perspectivePreview->gpuScene;');p.write_text(s,encoding='utf-8')
p=Path('studio/app_actions.hpp');s=p.read_text(encoding='utf-8').replace('bool App::shortcut(UINT key,uint32_t mask,bool editor){','bool App::shortcut(UINT key,uint32_t mask,bool editor){\n    if(!editor&&perspectiveTool&&(key==VK_SPACE||key==VK_ESCAPE)){cancelSelectionTransform();perspectiveTool=false;s.activateCommand("chooseSelectionTool_Marquee");updateCanvasCursor();invalidate();return true;}');p.write_text(s,encoding='utf-8')
p=Path('studio/structure_perspective_ui.hpp');s=p.read_text(encoding='utf-8').replace('Box bounds;for(auto id:perspectiveAtoms)bounds.include','auto first=s.doc.atoms().alive(*perspectiveAtoms.begin())->position;Box bounds(first,first);for(auto id:perspectiveAtoms)bounds.add');p.write_text(s,encoding='utf-8')
p=Path('studio/engine/session.cpp');s=p.read_text(encoding='utf-8').replace('        clipBridgeCrossing(doc,b,data);','''        clipBridgeCrossing(doc,b,data);
        if(static_cast<const Document&>(doc).extensions(ObjectRef(ObjectKind::Bond,b.id)).findValue("pst.attr.MoleculeStudioPerspectiveFront")&&b.display==BondDisplay::Normal&&b.stereo==BondStereo::None&&!data.shapes.empty()){
            // Only the outer skeleton stroke is bold. Keep the inner pi stroke.
            data.shapes.front().strokeWidth=std::max(doc.defaultLineWidth()*3.2,molecular::setting(doc,"BoldWidth",2));
        }''');p.write_text(s,encoding='utf-8')
