#pragma once
#include "mechanism_step_prepare.hpp"
#include "engine/mechanism_products.hpp"
#include "mechanism_drawn_step.hpp"
#include "mechanism_page_flow.hpp"
#include "mechanism_local_cleanup.hpp"
#include "expansion_cleanup.hpp"

App::StepPlan App::planMechanismStep(const Document& doc,const std::vector<ObjectRef>& selection,const std::vector<ObjectRef>& scene,int mode,int arrowStyle,bool interactive){
    StepPlan plan;
    std::vector<r62::mechanism::ElectronMove> moves;
    std::vector<arrows::ArrowParams> inputs;
    std::vector<ObjectId> inputIds;
    std::map<ObjectId,arrows::ArrowParams> mergedArrows;
    if(selection.empty()){throw std::runtime_error("请先框选同一步的机理箭头和反应物，再点“生成下一步”。");}
    for(auto ref:studioio::expanded(doc,selection)){
        if(ref.kind!=ObjectKind::Arrow)continue;
        arrows::ArrowParams p;bool valid=false;
        if(!arrows::readArrow(doc,ref.id,&p,&valid).ok()||!valid)continue;
        if(!p.mechanism&&p.presetId==49)p.mechanism=true;
        if(!p.mechanism){
            if(p.family==arrows::ArrowFamily::Arc){throw std::runtime_error("选中的旧弧线没有电子关联，请用“机理箭头”重新连接原子或化学键。");}
            continue;
        }
        if((p.head.atBegin&&p.head.sides!=arrows::HeadSides::Both)||!p.head.atEnd||p.shafts!=arrows::ShaftCount::One){
            throw std::runtime_error("请选择单向机理箭头或双电子双向机理箭头。");
        }
        if(p.head.atBegin&&p.head.atEnd){mergedArrows[ref.id]=p;p.head.atBegin=false;inputs.push_back(p);inputIds.push_back(ref.id);inputs.push_back(arrows::reversedArrow(p));inputIds.push_back(ref.id);}
        else{inputs.push_back(p);inputIds.push_back(ref.id);}
    }
    if(inputs.empty()&&mode==0){throw std::runtime_error("选区中没有机理箭头。请用“机理箭头”连接电子来源与目标，再框选生成。");}
    const auto companionDepartures=studio_mechanism::includeDrawnHalogenDepartures(doc,inputs,inputIds);
    studio_mechanism::resolveCarbonylContacts(doc,inputs);
    auto prepared=std::make_shared<Document>();
    std::vector<ObjectRef> preparedProducts;
    const auto expandedSelection=studioio::expanded(doc,selection);
    const std::vector<ObjectRef> selected(expandedSelection.begin(),expandedSelection.end());
    bool arranged=false;
    auto inferred=inputs.empty()?studio_mechanism_products::cloneStage(doc,selected,*prepared,preparedProducts):
        studio_mechanism::prepareProduct(doc,selected,inputs,prepared.get(),&moves,&preparedProducts,nullptr,&arranged);
    bool correctedElectronCount=false;
    bool drawn=false;
    if(!inferred.ok()){
        if(inferred.code==ErrorCode::InvalidGeometry||!moves.empty())throw std::runtime_error(inferred.message);
        auto corrected=inputs;
        bool changesElectronCount=false;
        if(studio_mechanism::correctCarbonylRadicalArrows(doc,corrected,changesElectronCount)&&!changesElectronCount){
            auto retry=studio_mechanism::prepareProduct(doc,selected,corrected,prepared.get(),&moves,&preparedProducts,nullptr,&arranged);
            if(retry.ok()){
                inputs.swap(corrected);inferred=retry;correctedElectronCount=changesElectronCount;
            }
        }
        if(!inferred.ok()&&inferred.code!=ErrorCode::InvalidSemantics&&!inputs.empty()){
            inferred=studio_drawn_step::prepare(doc,selected,inputs,*prepared,moves,preparedProducts);
            drawn=inferred.ok();
        }
        if(!inferred.ok()){throw std::runtime_error(inferred.message);}
    }
    auto finalized=studio_mechanism_products::finish(*prepared,doc,preparedProducts,mode);
    if(!finalized.ok()){throw std::runtime_error(finalized.message);}
    if(arrowStyle>0)for(auto ref:preparedProducts)if(ref.kind==ObjectKind::Arrow){
        arrows::ArrowParams current;bool valid=false;auto e=arrows::readArrow(*prepared,ref.id,&current,&valid);
        if(!e.ok()||!valid)continue;
        auto styled=arrows::applyPresetStyle(current,arrowStyle==1?38:39,prepared->defaultLineWidth());
        if(arrowStyle>=2)styled.splitRatio=arrowStyle==2?.28:.72;
        e=arrows::writeArrow(*prepared,ref.id,styled,prepared->defaultLineWidth());if(!e.ok()){throw std::runtime_error(e.message);}
    }
    if(interactive){status="正在局部整理反应中心附近的支链…（Esc 取消）";invalidate(false);if(!testing)UpdateWindow(h);}
    const auto localCleanup=studio_local_cleanup::run(h,*prepared,doc,preparedProducts);
    if(testing)std::ofstream(testOut+"/local-cleanup-progress.txt",std::ios::app)<<"movable="<<localCleanup.movable<<" attempted="<<localCleanup.attempted<<" changed="<<localCleanup.changed<<" "<<localCleanup.detail<<"\n";
    if(localCleanup.cancelled){throw std::runtime_error("已取消生成下一步，原结构保持不变");}
    auto sourceRefs=scene;std::vector<ObjectRef> participants=selected;
    for(auto ref:preparedProducts)if(ref.kind==ObjectKind::Atom){auto original=studio_mechanism_products::sourceAtom(*prepared,ref.id);if(doc.atoms().alive(original))participants.emplace_back(ObjectKind::Atom,original);}
    auto sourceRow=selection::selectionBounds(doc,participants);
    auto wrapped=plan.wrapped;
    auto created=plan.created;
    plan.command=Command("生成机理下一步",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,
        [moves,inputs,inputIds,mergedArrows,sourceRefs,sourceRow,wrapped,created,prepared,preparedProducts](CommandContext& c){
            if(moves.size()!=inputs.size())return CommandResult::fail(ErrorCode::InvalidArgument,"机理箭头与电子转移数量不一致。");
            std::set<ObjectId> rebound;
            for(size_t i=0;i<inputs.size();++i){
                if(!rebound.insert(inputIds[i]).second)continue;
                auto p=mergedArrows.count(inputIds[i])?mergedArrows.at(inputIds[i]):inputs[i];
                if(p.electronSource.valid()&&p.electronSource!=moves[i].source)p.sourceVisualAnchor=moves[i].source.kind==ObjectKind::Atom?1:5;
                p.electronSource=moves[i].source;
                if(!moves[i].sharedTarget&&!moves[i].retainDrawnTarget){if(p.electronTarget.valid()&&p.electronTarget!=moves[i].target)p.targetVisualAnchor=moves[i].target.kind==ObjectKind::Atom?1:5;p.electronTarget=moves[i].target;}
                auto bound=arrows::writeArrow(c.document,inputIds[i],p,c.document.defaultLineWidth());if(!bound.ok())return CommandResult::fail(bound.code,bound.message);
            }
            created->clear();auto e=studioio::append(*prepared,c.document,preparedProducts,Vec2(),created.get());
            if(!e.ok())return CommandResult::fail(e.code,e.message);
            auto flow=studio_mechanism_page_flow::place(c.document,*created,sourceRefs,sourceRow,wrapped.get());
            if(!flow.ok())return CommandResult::fail(flow.code,flow.message);
            return CommandResult::success();
        });
    plan.detail=std::to_string(moves.size())+" 次电子转移）；原结构保留，可继续编辑或撤销。";
    if(!drawn&&arranged)plan.detail+=" 已自动整理新产物的成键位置。";
    else if(localCleanup.changed)plan.detail+=" 已自动整理反应位点附近的支链。";
    if(companionDepartures)plan.detail+=" 已带上同一反应中心已画出的断键箭头。";
    if(correctedElectronCount)plan.detail+=" 已将羰基的双电子箭头改为单电子鱼钩。";
    if(drawn)plan.detail+=" 已按箭头直接编辑成键、断键与电子状态。";
    return plan;
}

void App::generateMechanismStep(int mode,int arrowStyle){
    s.cancel();
    try {
        auto plan=planMechanismStep(s.doc,s.selected,s.all(),mode,arrowStyle,true);
        auto result=s.submit(plan.command,"生成机理下一步");
        if(!result.ok())throw std::runtime_error(result.message);
        s.selected=*plan.created;
        s.activateCommand("chooseSelectionTool_Marquee");
        s.fit();updateInfo();
        status=std::string(*plan.wrapped?"已换到下一行左侧生成下一步结构（":"已在右侧生成下一步结构（")+plan.detail;
    } catch(const std::exception& e){
        status=e.what();
        if(!testing)MessageBoxW(h,W(status).c_str(),L"未生成下一步",MB_OK|MB_ICONINFORMATION);
    }
    invalidate();
}
