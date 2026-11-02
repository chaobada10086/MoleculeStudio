#ifndef R62_T19_SESSION_HPP
#define R62_T19_SESSION_HPP
#include "next/core/tool_registry.hpp"
#include "next/persistence/persistence.hpp"
#include "next/render/scene_builder.hpp"
#include "next/interaction/input_state.hpp"
#include "next/bonds/adapters.hpp"
#include "next/bonds/edit.hpp"
#include "next/rings_chains/tool.hpp"
#include "next/arrows_curves/tools.hpp"
#include "next/graphics/tools.hpp"
#include "next/biodraw/tools.hpp"
#include "next/symbols_orbitals/tools.hpp"
#include "next/experimental/tools.hpp"
#include "next/text/render_bridge.hpp"
#include "next/selection/clipboard.hpp"
#include "next/selection/clipboard_win32.hpp"
#include "spatial_index.hpp"
#include <memory>
namespace r62 { namespace integration {
using namespace core;
struct Entry {
    std::string command, category, owner, limitation;
    std::unique_ptr<Tool> tool;
    uint32_t local;
    Entry():local(0){}
};
class Session;
namespace local_scene {
// Each document owns its painter-order metadata. Other tabs and previews must
// not evict it and force the next local edit through a full scene rebuild.
struct OrderCache {
    Session* owner=nullptr;DocumentId document=0;uint64_t generation=0;
    std::vector<uint64_t> keys;
    bool matches(const Session& s) const;
};
}
class Session : public ToolHost, public HitContext, public interaction::CandidateSink {
public:
    Session();
    Document doc;
    CommandLog history;
    ToolRegistry registry;
    PreviewScene ghost;
    render::Scene scene, gpuScene, dragScene;
    local_scene::OrderCache sceneOrderCache;
    const render::Scene& displayScene() const {return moving?dragScene:gpuScene;}
    // A rigid drag can reuse GPU triangles; boundary bonds keep the CPU path.
    bool rigidDragPreview(Vec2* delta,std::vector<uint64_t>* keys) const {
        if(!moving||movingBoundary||movingSlots.empty()||dragScene.size()!=gpuScene.size())return false;
        bool first=true;
        for(const auto& entry:movingSlots)for(size_t i:entry.second){
            if(i+2>=dragScene.size()||!scene.primitives()[i].cacheKey)return false;
            if(first){*delta=dragScene.primitives()[i+2].bounds.min-scene.primitives()[i].bounds.min;first=false;}
            keys->push_back(scene.primitives()[i].cacheKey);
        }
        return !first;
    }
    render::SceneBuilder builder;
    render::View renderView;
    interaction::InputStateMachine input;
    interaction::CommandRouter router;
    // T05 cut/copy/paste: the real Win32 transport. Session owns it so every
    // clipboard command in this process publishes and reads the same format.
    selection::Win32ClipboardTransport clipboard;
    std::vector<Entry> entries;
    std::vector<ObjectRef> selected;
    std::string status, sourcePath;
    bool dirty, fixedLen, fixedAngle, dragFixedLength; Document dragBase; bool dragBaseActive=false;
    double graphicsWidth=0;uint32_t graphicsColor=0x000000FFu;
    bool whiteCanvas;   // OLE editor: use a white full-canvas backdrop
    uint64_t serial, viewSerial;
    size_t activeIndex, rebuilds;
    std::unique_ptr<text::GlyphProvider> font;
    text::GlyphCache glyphs;
    std::unique_ptr<render::geom::TextMeasurer> measurer;
    Document& document() override {return doc;}
    const Document& document() const override {return doc;}
    CommandLog& log() override {return history;}
    PreviewScene& preview() override {return ghost;}
    const std::vector<ObjectRef>& selection() const override {return selected;}
    void setSelection(const std::vector<ObjectRef>& x) override {selected=x;}
    void addToSelection(const std::vector<ObjectRef>& x) override;
    void clearSelection() override {selected.clear();}
    const ViewTransform& view() const override {return renderView.transform;}
    void setView(const ViewTransform& x) override;
    bool fixedLength() const override {return fixedLen;}
    bool fixedAngles() const override {return fixedAngle;}
    double angleStepDegrees() const override {return 15;}
    double snapRadiusDevice() const override {return 10;}
    double releaseRadiusDevice() const override {return 13;}
    Error submit(const Command& c,const std::string& tag) override;
    void setStatus(const std::string& x) override {status=x;}
    void reportUnsupported(const std::string& x) override {status="Unsupported: "+x;}
    ObjectId nearestAtom(const Vec2&,double) const override;
    ObjectId nearestBond(const Vec2&,double) const override;
    std::vector<ObjectRef> objectsInBox(const Box&) const override;
    std::vector<ObjectRef> objectsInPolygon(const std::vector<Vec2>&) const override;
    void query(const Box&,std::vector<interaction::SnapCandidate>*,interaction::QueryStats*) override;
    size_t candidateCount() const override {return candidates.size();}
    uint64_t geometrySerial() const override {return serial;}
    void refresh(bool rebuildGeometry=true);
    void fit();
    bool activate(size_t index);
    void cancel();
    bool selectionDragActive() const { return selecting; }
    bool objectDragActive() const { return moving; }
    const std::vector<Vec2>& selectionDragPoints() const { return lasso; }
    Error gesture(PointerPhase phase,Vec2 p,uint32_t modifiers=0);
    Error move(const std::vector<ObjectRef>& refs,Vec2 delta);
    Error moveFixedLength(const std::vector<ObjectRef>& refs,Vec2 delta);
    Error undo(bool redo=false);
    Error open(const std::string& path);
    Error save(const std::string& path,bool cdxml=false);
    std::vector<ObjectRef> all() const;
    ObjectRef pick(Vec2 p) const;
    // A grouped object behaves as ONE object until it is ungrouped. These map a
    // raw pick (atom/bond/text/...) to the outermost group that owns it, and a
    // selection list to the distinct outermost groups it covers.
    ObjectRef topLevelRef(const ObjectRef& ref) const;
    std::vector<ObjectRef> topLevelSelection(const std::vector<ObjectRef>& refs) const;
    render::OverlayScene overlay();
    bool activateCommand(const std::string&);
    ObjectId editAtom;
    // The text object created by the last T09 placement; the shell opens the
    // editor on it. Reset once consumed.
    ObjectId createdTextId;
    bonds::BondEndpoint bondOrigin;
    interaction::SnapResult bondSnap;
    bool bondDrawing;
private:
    spatial::Index objectIndex, candidateIndex;
    std::vector<ObjectRef> objectRefs;
    std::vector<interaction::SnapCandidate> candidates;
    Vec2 press;
    ObjectId clickBond;
    interaction::HysteresisState bondHysteresis;
    bool selecting, moving;
    // True while a press started inside a T16 board, so the press becomes a spot
    // instead of a new plate drag.
    bool tlcSpotting;
    ObjectId tlcFigure;
    std::vector<Vec2> lasso;
    std::set<ObjectRef> movingRefs;
    std::map<ObjectRef,std::vector<size_t>> movingSlots;
    bool movingBoundary=false;
    Error buildScene();
    static interaction::RouterOutcome action(void*,const interaction::ActionRequest&);
};
Error translateDocumentObjects(Document&,const std::vector<ObjectRef>&,Vec2);
Error saveSafeNative(const Document&,std::ostream&);
Error loadSafeNative(Document&,std::istream&);
int acceptance(const std::string& root,const std::string& out);
int usabilityAcceptance(const std::string& out);
interaction::RouterOutcome chemistryAction(Session&,const interaction::ActionRequest&);
int runWindow(Session&,const std::string& path,bool smoke=false,const std::string& screenshot="",int mode=0);
int dumpHotkeys(const std::string& path);
int dumpRibbon(const std::string& path);
} }
#endif
