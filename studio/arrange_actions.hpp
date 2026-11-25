#ifndef STUDIO_ARRANGE_ACTIONS_HPP
#define STUDIO_ARRANGE_ACTIONS_HPP
#include "interop.hpp"
#include "engine/name_link.hpp"
#include "next/integration/session.hpp"
#include "next/selection/transform.hpp"
#include "next/selection/selection_set.hpp"
#include "next/selection/grouping.hpp"
#include "next/text/rich_text.hpp"
#include <limits>

namespace studio {
using namespace r62;
using namespace core;
// Alignment, distribution, grouping and ungrouping, all through one entry
// point so the ribbon, the keyboard shortcuts and the menus cannot drift apart.
//
// Measured text boxes for layout. Without this the selection module can only
// box a caption by its anchor point, so 左对齐/水平居中/右对齐 all collapse the
// anchors onto one line and the second of the three clicks moves nothing. The
// measured box is produced by the SAME layout engine that draws the caption, so
// align agrees with the rectangle the user sees around the selection.
struct StudioTextBounds : selection::TextBoundsProvider {
    // The cache is owned by the Session, which owns the history that stores the
    // command, so a Ctrl+Y replay resolves the same live metrics.
    text::GlyphCache* cache;
    explicit StudioTextBounds(text::GlyphCache& glyphs):cache(&glyphs){}
    bool textInkBox(const Document& document,ObjectId id,Box* out)const override{
        if(!out||!cache)return false;
        const TextRecord* text=document.texts().alive(id);
        if(!text)return false;
        text::LayoutOptions options;
        options.defaultSize=text->size>0.0?text->size:document.defaultTextSize();
        options.defaultFont=text->format.fontName.empty()?document.defaultFont():text->format.fontName;
        options.defaultFlags=text->format.flags;options.align=core::TextAlign(text->format.align>3?0:text->format.align);
        const auto& bag=document.extensions(ObjectRef(ObjectKind::Text,id));
        if(auto width=bag.findValue("studio.text.width"))options.widthLimit=std::atof(width->c_str());
        if(auto height=bag.findValue("studio.text.LineHeight")){if(std::atof(height->c_str())>0){options.spacing=text::LineSpacing::Fixed;options.fixedLineHeight=std::atof(height->c_str());}}
        text::RichLayout layout;
        if(!text::layoutRichText(document.runs(id),Vec2(),options,cache,&layout).ok())return false;
        if(layout.metrics.inkBox.empty()||!selection::boxFinite(layout.metrics.inkBox))return false;
        Vec2 offset=text->position;offset.y-=layout.metrics.firstBaseline;
        if(text->format.align==1)offset.x-=layout.metrics.width*.5;else if(text->format.align==2)offset.x-=layout.metrics.width;
        *out=layout.metrics.inkBox;out->min=out->min+offset;out->max=out->max+offset;
        if(options.widthLimit>0){double left=text->position.x-(text->format.align==1?options.widthLimit*.5:text->format.align==2?options.widthLimit:0);out->min.x=left;out->max.x=left+options.widthLimit;double inset=std::max(1.,options.defaultSize*.12);out->min.y-=inset;out->max.y+=inset;}
        return true;
    }
};

inline Error stackSelection(integration::Session& session, bool front) {
    if (session.selected.empty()) return {ErrorCode::InvalidArgument, "请先选择要排列的对象"};
    const auto refs = studioio::expanded(session.doc, session.selected, true);
    const int64_t minZ = std::numeric_limits<int32_t>::min();
    const int64_t maxZ = std::numeric_limits<int32_t>::max();
    int64_t selectedEdge = front ? maxZ : minZ;
    int64_t otherEdge = front ? minZ : maxZ;
    std::set<ObjectRef> drawn;
    bool hasOther = false;
    int64_t imageEdge = front ? maxZ : minZ;
    for (const auto& primitive : session.scene.primitives()) {
        if (!primitive.source.valid()) continue;
        if (refs.count(primitive.source)) {
            drawn.insert(primitive.source);
            if (primitive.text.compare(0, 10, "CDXML-EMF:") == 0)
                imageEdge = front ? std::min(imageEdge, int64_t(primitive.z))
                                  : std::max(imageEdge, int64_t(primitive.z));
            selectedEdge = front ? std::min(selectedEdge, int64_t(primitive.z))
                                 : std::max(selectedEdge, int64_t(primitive.z));
        } else {
            hasOther = true;
            otherEdge = front ? std::max(otherEdge, int64_t(primitive.z))
                              : std::min(otherEdge, int64_t(primitive.z));
        }
    }
    if (drawn.empty() || !hasOther) return {};
    auto delta = otherEdge - selectedEdge + (front ? 1 : -1);
    // The Windows canvas paints embedded metafiles in two passes: negative Z
    // behind vector objects, nonnegative Z in front. Keep the selection together.
    if (front && imageEdge != maxZ) delta = std::max(delta, -imageEdge);
    if (!front && imageEdge != minZ) delta = std::min(delta, -1 - imageEdge);
    if (front ? delta <= 0 : delta >= 0) return {};
    if (delta <= minZ || delta > maxZ)
        return {ErrorCode::OutOfRange, "图层顺序超出文档范围"};

    std::map<ObjectRef, int64_t> changes;
    const Document& source = session.doc;
    for (const auto& ref : drawn) {
        const auto* stored = source.extensions(ref).findValue("studio.z");
        const auto old = stored ? std::strtoll(stored->c_str(), nullptr, 10) : 0;
        if (old < minZ || old > maxZ || old + delta < minZ || old + delta > maxZ)
            return {ErrorCode::OutOfRange, "图层顺序超出文档范围"};
        changes.emplace(ref, old + delta);
    }
    for (const auto& primitive : session.scene.primitives()) {
        if (!drawn.count(primitive.source)) continue;
        const auto z = int64_t(primitive.z) + delta;
        if (z < minZ || z > maxZ) return {ErrorCode::OutOfRange, "图层顺序超出文档范围"};
    }
    const char* title = front ? "置于顶层" : "置于底层";
    return session.submit(Command(title, kInvalidateAppearance, [changes](CommandContext& context) {
        for (const auto& entry : changes) {
            context.document.noteTouchedExtensions(entry.first, false);
            context.document.extensions(entry.first).set("studio.z", std::to_string(entry.second));
        }
        return CommandResult::success();
    }), "堆叠顺序");
}

inline Error transformSelection(integration::Session& session, const std::string& operation, int argument) {
    const auto refs = session.topLevelSelection(session.selected);
    if (refs.empty()) return {ErrorCode::InvalidArgument, "请先选择要排列的对象"};
    const auto bounds = studioio::selectionBounds(session);
    const auto pivot = bounds.empty() ? session.view().center : (bounds.min + bounds.max) * .5;
    const char* title = operation == "rotate" ? "旋转 90°" : operation == "merge" ? "合并" :
        argument == 0 ? "水平翻转" : "垂直翻转";
    return session.submit(Command(title, kInvalidateStructure | kInvalidateGeometry,
        [refs, pivot, operation, argument](CommandContext& context) {
            if (operation == "merge") {
                const auto report = selection::mergeSelection(context.document, refs, 2.0);
                if (!report.error.ok()) return CommandResult::fail(report.error.code, report.error.message);
                if (!report.mergedPairs) return CommandResult::fail(ErrorCode::Unsupported, "没有可合并的重合原子");
            } else if (operation == "rotate") {
                selection::rotateObjects(context.document, refs, pivot, -90, selection::TransformOptions());
            } else {
                selection::flipObjects(context.document, refs, pivot, argument == 0, selection::TransformOptions());
            }
            return CommandResult::success();
        }), title);
}

inline bool arrangeSelection(integration::Session& s, std::string& status, const std::string& op, int arg){
    std::vector<ObjectRef> refs=s.topLevelSelection(s.selected);
    if(op=="group"){
        if(refs.size()<2){status=refs.size()==1&&refs[0].kind==ObjectKind::Group?"所选对象已经编组":refs.empty()?"请先选择要编组的对象":"编组需要至少两个独立对象";return true;}
        auto created=std::make_shared<ObjectId>(kInvalidObjectId);
        auto report=std::make_shared<selection::GroupReport>();
        Error e=s.submit(Command("编组",kInvalidateStructure,[refs,created,report](CommandContext& c){
            Error result=selection::createGroup(c.document,refs,"结构组",&*report);
            if(!result.ok())return CommandResult::fail(result.code,result.message);
            *created=report->group;
            return CommandResult::success();
        }),"编组");
        if(e.ok()&&*created!=kInvalidObjectId){
            s.selected={ObjectRef(ObjectKind::Group,*created)};
            status="已编组 "+std::to_string(report->membersAdded)+" 个对象 · Ctrl+Shift+G 解组";
        }
        return true;
    }
    if(op=="ungroup"){
        std::vector<ObjectId> groups;
        for(const auto& ref:s.selected){
            if(ref.kind==ObjectKind::Group){if(s.doc.groups().alive(ref.id))groups.push_back(ref.id);continue;}
            auto owners=selection::groupsContaining(s.doc,ref);
            if(!owners.empty())groups.push_back(owners.back());
        }
        std::sort(groups.begin(),groups.end());groups.erase(std::unique(groups.begin(),groups.end()),groups.end());
        if(groups.empty()){status="请先选择编组或编组内的对象";return true;}
        auto freed=std::make_shared<std::vector<ObjectRef> >();
        auto report=std::make_shared<selection::GroupReport>();
        Error e=s.submit(Command("解组",kInvalidateStructure,[groups,freed,report](CommandContext& c){
            selection::SelectionSet result=selection::ungroup(c.document,groups,false,&*report);
            *freed=result.refs();
            // Ungrouping a 命名结构 pair is an explicit "these are independent
            // again" instruction: drop the structure link so the stale red frame
            // cannot outlive the association the user just dissolved.
            for(size_t k=0;k<freed->size();++k)if((*freed)[k].kind==ObjectKind::Text&&namelink::hasLink(c.document,(*freed)[k].id))namelink::clearLink(c.document,(*freed)[k].id);
            return CommandResult::success();
        }),"解组");
        if(e.ok()){
            s.selected=*freed;
            status="已解组 "+std::to_string(report->groupsRemoved)+" 个编组 · 释放 "+std::to_string(report->membersReparented)+" 个对象";
        }
        return true;
    }
    if(op=="align"){
        if(refs.size()<2){status=refs.size()==1?"对齐需要至少两个独立对象":"请先选择要排列的对象";return true;}
        auto report=std::make_shared<selection::TransformReport>();
        Error e=s.submit(Command("对齐",kInvalidateGeometry,[&s,refs,arg,report](CommandContext& c){
            StudioTextBounds metrics(s.glyphs);
            selection::AlignOptions options;
            options.textBounds=&metrics;
            selection::alignObjects(c.document,refs,selection::AlignMode(arg),options,&*report);
            return CommandResult::success();
        }),"对齐");
        if(e.ok()){
            if(report->objectsConsidered<2)status="所选对象属于同一结构，已作为整体处理";
            else if(report->objectsMoved==0)status="对齐完成 · 对象已在目标位置";
            else status="对齐完成 · 移动 "+std::to_string(report->objectsMoved)+" 个对象";
        }
        return true;
    }
    if(op=="distribute"){
        if(refs.size()<3){status="分布需要至少三个独立对象";return true;}
        auto report=std::make_shared<selection::DistributeReport>();
        Error e=s.submit(Command("分布",kInvalidateGeometry,[&s,refs,arg,report](CommandContext& c){
            StudioTextBounds metrics(s.glyphs);
            selection::DistributeOptions options;
            options.textBounds=&metrics;
            *report=selection::distributeObjects(c.document,refs,selection::DistributeMode(arg),options);
            if(!report->applied)return CommandResult::fail(ErrorCode::Unsupported,report->why);
            return CommandResult::success();
        }),"分布");
        if(e.ok())status=(arg==0?"水平分布完成 · 移动 ":"垂直分布完成 · 移动 ")+std::to_string(report->objectsMoved)+" 个对象";
        return true;
    }
    return false;
}
} // namespace studio
#endif
