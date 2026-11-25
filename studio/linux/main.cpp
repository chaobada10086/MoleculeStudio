#include <fcntl.h>
#include <unistd.h>
#include "next/integration/session.hpp"
#include "next/core/commands.hpp"
#include "next/chemistry/edit.hpp"
#include "next/integration/ribbon/command_labels.hpp"
#include "interop.hpp"
#include "arrange_actions.hpp"
#include "chemistry_clean.hpp"
#include "engine/valence_warnings.hpp"
#include "rapid_sprout.hpp"
#include "stereo_annotation.hpp"
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_BROADWAY
#include <gdk/gdkbroadway.h>
#endif
#include <iostream>
#include "reagent_catalog.hpp"
#include "bio_catalog.hpp"
#include "formula.hpp"
#include "mass_dialog.hpp"
#include "calculator.hpp"
#include "conformer_model.hpp"
#include "config.hpp"
#include "drawing_styles.hpp"
#include "page.hpp"
#include "canvas.hpp"
#include "canvas_scrollbar.hpp"
#include "print.hpp"
#include "office_preview.hpp"
#include "i18n.hpp"
#include "skin.hpp"
#include "shortcuts.hpp"
#include "skill_export.hpp"
#include "caption.hpp"
#include "atom_label_runs.hpp"
#include "table.hpp"
#include "table_canvas.hpp"
#include "custom_templates.hpp"
#include "nmr.hpp"
#include "worker.hpp"
#include "update.hpp"
#include "ocsr.hpp"
#include "ocsr_review_edit.hpp"

using r62::integration::Session;
using namespace r62::core;
using namespace r62;
using linux_i18n::tr;
#include "nicknames.hpp"
#include "selection_gesture.hpp"
#include "tlc_dialog.hpp"
#include "custom_groups.hpp"
#include "template_gallery.hpp"
#include "atom_hydrogen_actions.hpp"
#include "hotspot_actions.hpp"
#include "ai.hpp"
#include "mechanism_tools.hpp"
#include "functional_group_actions.hpp"
#include "catalytic_dialog.hpp"
#include "graphic_format.hpp"
#include "projection_session.hpp"
#include "projection_templates.hpp"

static const std::string& assets() {
    static const std::string path = [] {
        gchar* executable = g_file_read_link("/proc/self/exe", nullptr);
        gchar* directory = executable ? g_path_get_dirname(executable) : nullptr;
        std::string source = directory ? std::string(directory) + "/../../studio" : "";
        g_free(directory);
        g_free(executable);
        if (!source.empty() && g_file_test((source + "/linux/build_engine.py").c_str(), G_FILE_TEST_IS_REGULAR))
            return source;
        return std::string("/usr/share/moleculestudio-native");
    }();
    return path;
}

static std::string conversionScript() {
    const auto installed = assets() + "/convert_structure.py";
    return g_file_test(installed.c_str(), G_FILE_TEST_IS_REGULAR) ? installed : assets() + "/linux/convert_structure.py";
}

static bool runLocalCleanup(GtkWindow* owner, const char* title, const std::string& input, std::string& output) {
    std::string failure;
    bool cancelled = false;
    output = linux_worker::run(owner, title,
        {"/usr/bin/python3", conversionScript(), "text/xml", "text/xml", "clean"},
        input, failure, owner ? 45 : 15, false, &cancelled);
    if (!output.empty()) return true;
    output = cancelled ? "已取消化学服务" : failure;
    return false;
}

static studio_mechanism_step::StepPlan planMechanismStep(
    GtkWindow* owner, const Document& document, const std::vector<ObjectRef>& selection,
    const std::vector<ObjectRef>& scene, int mode, int style, bool keepLossStructures) {
    return studio_mechanism_step::planStep(document, selection, scene, mode, style, keepLossStructures,
        [owner](Document& product, const Document& source, const std::vector<ObjectRef>& made) {
            return studio_local_cleanup::run(product, source, made,
                [owner](const std::string& input, std::string& output) {
                    return runLocalCleanup(owner, "正在局部整理反应中心附近的支链…", input, output);
                });
        });
}

struct OcsrReview {
    GWeakRef target;
    DocumentId document;

    OcsrReview(GtkWidget* owner, DocumentId id) : document(id) { g_weak_ref_init(&target, G_OBJECT(owner)); }
    ~OcsrReview() { g_weak_ref_clear(&target); }
};

static GtkWidget* showDocument(GtkApplication* app, const char* path = nullptr, std::unique_ptr<OcsrReview> review = {},
                               GtkWidget* preferred = nullptr, bool separate = false);
#include "../presentation_catalog.hpp"
#include "../bio_template_scale.hpp"
#include "../ring_library_data.hpp"
#include "../ring_library_template.hpp"
struct Window;
static std::vector<Window*> documentsOf(GtkApplication* app);
static void titleDocument(Window&, const std::string&, const char*);
static void selectDocument(Window&);
static void queueSelectionInfo(Window&);
static void closeDocument(Window&);
static void chooseDocuments(Window*);
static bool openSpectrumTab(Window&, const std::string&);

struct Window {
    GtkWidget *page = nullptr, *tab = nullptr, *tabLabel = nullptr;
    std::string shellTitle;
    std::string tabSourcePath;
    bool spectrumPage = false;
    Session session;
    linux_canvas::ContentCache contentCache;
    linux_canvas::PanCache panCache;
    linux_canvas::PanCache placementCache{true};
    linux_canvas::PanCache moveCache{true};
    linux_canvas::PartialMoveLayers partialMoveLayers;
    std::vector<uint8_t> moveMask;
    std::weak_ptr<Session> cachedPlacement;
    render::View placementView;
    guint placementIdle = 0;
    guint wheelIdle = 0;
    double pinchStartScale = 0;
    Vec2 pinchDocumentAnchor, pinchPixel;
    guint overviewIdle = 0, selectionIdle = 0;
    bool planSelectionLayers = false;
    std::vector<ObjectRef> warmedSelection;
    uint64_t warmedSerial = 0, warmedView = 0;
    int warmedFactor = 0;
    linux_canvas::FrameBuffer frameBuffer;
    std::string canvasCursor;
    std::map<std::string,std::vector<GtkWidget*>> ribbonTools;
    std::string highlightedTool;
    GtkWidget* documentStats=nullptr;
    GtkWidget* hoveredRibbonTool=nullptr;
    bool statusPriority=false;
    std::string statusMessage;
    uint64_t statsRevision=0;
    double statsZoom=-1;
    size_t statsAtoms=0, statsBonds=0;
    linux_ai::Panel assistant;
    GtkWidget *window = nullptr, *canvas = nullptr, *status = nullptr, *tools = nullptr;
    GtkWidget* canvasOverlay = nullptr;
    GtkAdjustment* scrollX = nullptr;
    GtkAdjustment* scrollY = nullptr;
    Box scrollBounds;
    uint64_t scrollRevision = 0;
    bool scrollBoundsReady = false;
    bool syncingScroll = false;
    bool draggingScrollbar = false;

    void updateScrollbars() {
        const auto view = session.view();
        const double unit = view.docPerDevice();
        if (!scrollBoundsReady || scrollRevision != session.serial) {
            scrollBounds = session.scene.bounds();
            const auto paper = studioio::Page::get(session.doc);
            if (paper.valid()) {
                scrollBounds.add(Vec2(paper.x, paper.y));
                scrollBounds.add(Vec2(paper.x + paper.width * paper.across, paper.y + paper.height * paper.down));
            }
            if (scrollBounds.empty()) scrollBounds = Box(view.center, view.center);
            const Vec2 margin(std::max(8 * unit, scrollBounds.width() * .02),
                              std::max(8 * unit, scrollBounds.height() * .02));
            scrollBounds.min = scrollBounds.min - margin;
            scrollBounds.max = scrollBounds.max + margin;
            scrollBoundsReady = true;
            scrollRevision = session.serial;
        }
        syncingScroll = true;
        const double step = 40.0 / gtk_widget_get_scale_factor(canvas);
        auto update = [unit,step](GtkAdjustment* adj, double lo, double hi, double center, double pixels) {
            const double upper = std::max(pixels, (hi - lo) / unit);
            const double value = std::clamp((center - lo) / unit - pixels / 2, 0., upper - pixels);
            if (gtk_adjustment_get_upper(adj) != upper || gtk_adjustment_get_page_size(adj) != pixels ||
                gtk_adjustment_get_step_increment(adj) != step)
                gtk_adjustment_configure(adj, value, 0, upper, step, pixels, pixels);
            else if (gtk_adjustment_get_value(adj) != value) gtk_adjustment_set_value(adj, value);
        };
        update(scrollX, scrollBounds.min.x, scrollBounds.max.x, view.center.x, session.renderView.widthPx);
        update(scrollY, scrollBounds.min.y, scrollBounds.max.y, view.center.y, session.renderView.heightPx);
        syncingScroll = false;
    }

    GtkWidget* canvasWithScrollbars() {
        auto* grid = gtk_grid_new();
        gtk_widget_set_hexpand(canvasOverlay, TRUE);
        gtk_widget_set_vexpand(canvasOverlay, TRUE);
        gtk_grid_attach(GTK_GRID(grid), canvasOverlay, 0, 0, 1, 1);
        for (const auto orientation : {GTK_ORIENTATION_HORIZONTAL, GTK_ORIENTATION_VERTICAL}) {
            const bool horizontal = orientation == GTK_ORIENTATION_HORIZONTAL;
            auto* bar = gtk_scrollbar_new(orientation, nullptr);
            gtk_style_context_add_class(gtk_widget_get_style_context(bar), "canvas-scrollbar");
            auto* adj = gtk_range_get_adjustment(GTK_RANGE(bar));
            (horizontal ? scrollX : scrollY) = adj;
            gtk_grid_attach(GTK_GRID(grid), bar, horizontal ? 0 : 1, horizontal ? 1 : 0, 1, 1);
            g_signal_connect(adj, "value-changed", G_CALLBACK(+[](GtkAdjustment* adj, Window* w) {
                if (w->syncingScroll || !w->scrollBoundsReady) return;
                w->finishCanvasText();
                auto view = w->session.view();
                const double offset = (gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj) / 2) * view.docPerDevice();
                if (adj == w->scrollX) view.center.x = w->scrollBounds.min.x + offset;
                else view.center.y = w->scrollBounds.min.y + offset;
                w->session.setView(view);
                gtk_widget_queue_draw(w->canvas);
            }), this);
            g_signal_connect(bar, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, Window* w) -> gboolean {
                w->draggingScrollbar = true;
                return FALSE;
            }), this);
            linux_scrollbar::Press::attach(bar);
            auto end = +[](GtkWidget*, GdkEvent*, Window* w) -> gboolean {
                w->draggingScrollbar = false;
                gtk_widget_queue_draw(w->canvas);
                return FALSE;
            };
            g_signal_connect(bar, "button-release-event", G_CALLBACK(end), this);
            g_signal_connect(bar, "grab-broken-event", G_CALLBACK(end), this);
        }
        return grid;
    }
    GtkWidget* captionBox = nullptr;
    std::unique_ptr<linux_caption::Editor> captionEditor;
    GtkWidget* fontEntry = nullptr;
    GtkWidget* sizeEntry = nullptr;
    bool syncingTypography = false;
    std::string nextFamily;
    double nextSize = 0.;
    Vec2 captionAnchor;
    ObjectId editedCaption = kInvalidObjectId;
    studio_projection::Endpoint captionEndpoint;
    GtkWidget* atomEntry = nullptr;
    ObjectId editedAtom = kInvalidObjectId;
    std::string originalAtomLabel;
    linux_caption::AtomLabelRuns atomLabelRuns;
    GtkWidget* selectionMenu = nullptr;
    GtkWidget* tlcSpotMenu = nullptr;
    ObjectId tlcMenuPlate = kInvalidObjectId;
    uint32_t tlcMenuSpot = 0;
    uint64_t tlcMenuSerial = 0;
    guint tlcClickTimer=0;
    Vec2 tlcClickPoint;
    uint32_t tlcClickMods=0;
    uint64_t tlcClickSerial=0;
    DocumentId tlcClickDocument=0;
    void finishTlcClick(bool commit) {
        if(!tlcClickTimer)return;
        g_source_remove(tlcClickTimer);tlcClickTimer=0;
        if(!commit || session.serial!=tlcClickSerial || session.doc.id()!=tlcClickDocument ||
            session.entries[session.activeIndex].command!="chooseTLCPlateTool")return;
        auto result=session.gesture(PointerPhase::Down,tlcClickPoint,tlcClickMods);
        if(result.ok())result=session.gesture(PointerPhase::Up,tlcClickPoint,tlcClickMods);
        report(result);
    }
    static uint32_t modifiers(guint state) {
        return (state&GDK_SHIFT_MASK ? kModShift : 0) | (state&GDK_CONTROL_MASK ? kModControl : 0) |
            (state&GDK_MOD1_MASK ? kModAlt : 0);
    }
    void deferTlcClick(Vec2 pixel,guint state) {
        tlcClickPoint=session.view().toDocument(pixel);
        tlcClickMods=modifiers(state);tlcClickSerial=session.serial;tlcClickDocument=session.doc.id();
        int delay=250;
        g_object_get(gtk_widget_get_settings(canvas),"gtk-double-click-time",&delay,nullptr);
        tlcClickTimer=g_timeout_add(delay,+[](gpointer data)->gboolean {
            static_cast<Window*>(data)->finishTlcClick(true);return G_SOURCE_REMOVE;
        },this);
    }
    bool showTlcSpotMenu(Vec2 pixel, GdkEvent* event);
    void activateTlcSpot(int action);
    void tlcSettings(ObjectId id=kInvalidObjectId, uint32_t selectedSpot=0);
    GtkWidget* projectionConversion = nullptr;
    GtkWidget* attachBaseHydrogenItem = nullptr;
    GtkWidget* catalyticOutlineItem = nullptr;
    std::unique_ptr<OcsrReview> ocsrReview;
    ObjectId projectionContextAxis = kInvalidObjectId;
    ObjectRef catalyticOutline;
    bool perspectiveTool = false;
    linux_mechanism::Placement mechanismPlacement = linux_mechanism::Placement::None;
    ObjectId virtualBondOrigin = kInvalidObjectId;
    GtkCheckMenuItem *fixedLength = nullptr, *fixedAngles = nullptr;
    GtkCheckMenuItem* automaticLayout = nullptr;
    bool down = false;
    bool panning = false;
    bool pointerOnCanvas = false;
    Vec2 canvasPointer;
    studio_projection::Endpoint projectionHover;
    linux_table::CanvasDrag tableDrag;
    ObjectId cellTable = kInvalidObjectId;
    DocumentId cellDocument = 0;
    linux_table::Cell firstCell, lastCell;
    Box selectedCellBox;
    uint64_t cellBoxSerial = 0;

    void selectTableCell(ObjectId id, linux_table::Cell position, bool extend) {
        if (!extend || cellTable != id || cellDocument != session.doc.id()) firstCell = position;
        lastCell = position;
        cellTable = id;
        cellDocument = session.doc.id();
        selectedCellBox = Box();
    }

    void drawTableRange(cairo_t* cr) {
        if (cellDocument != session.doc.id() || cellTable == kInvalidObjectId ||
            session.selected.size() != 1 || session.selected.front() != ObjectRef(ObjectKind::Table,cellTable)) return;
        if (selectedCellBox.width() <= 0 || cellBoxSerial != session.serial) {
            graphics::TableData data;
            if (!graphics::readTable(session.doc,cellTable,&data,nullptr).ok()) return;
            firstCell = linux_table::bounded(data.grid,firstCell);
            lastCell = linux_table::bounded(data.grid,lastCell);
            const auto range = linux_table::selection(data.grid,firstCell,lastCell);
            selectedCellBox = Box(data.grid.cellBox(range.top,range.left).min,
                                  data.grid.cellBox(range.bottom,range.right).max);
            cellBoxSerial = session.serial;
        }
        const auto a = session.view().toDevice(selectedCellBox.min);
        const auto b = session.view().toDevice(selectedCellBox.max);
        cairo_save(cr);
        cairo_rectangle(cr,a.x,a.y,b.x-a.x,b.y-a.y);
        cairo_set_source_rgba(cr,.15,.45,.82,.12);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr,.15,.45,.82,.9);
        cairo_set_line_width(cr,1.);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
    linux_selection::Gesture selectionGesture;
    const PresentationShape* pendingShape = nullptr;
    Vec2 shapeStart;
    bool draggingShape = false;
    bool shapeFillEnabled = true;
    uint32_t shapeFill = 0xE6F2EFFFu;

    void changeShapeFill(unsigned choice) {
        uint32_t color = choice == 2 ? 0xDFEBFAFFu : choice == 3 ? 0xFFF0DCFFu : 0xE6F2EFFFu;
        if (choice == 4) {
            auto picked = linux_graphic_format::color(GTK_WINDOW(window),"图案填充",shapeFill);
            if (!picked) return;
            color = *picked;
        }
        shapeFill = color;
        shapeFillEnabled = choice != 0;
        auto refs = studioio::expanded(session.doc,session.selected);
        if (refs.empty()) return;
        report(session.submit(Command("图案填充",kInvalidateAppearance,
            [refs,color,enabled=shapeFillEnabled](CommandContext& c){
                for (auto ref : refs) if (ref.kind == ObjectKind::Path) {
                    if (auto* path = c.document.paths().mutableGet(ref.id)) {
                        path->fillEnabled = enabled;
                        path->fillRGBA = color;
                        c.document.noteTouchedPath(ref.id);
                    }
                }
                return CommandResult::success();
            }),"图案填充"));
    }

    void cancelShape() {
        if (!pendingShape) return;
        pendingShape = nullptr;
        draggingShape = false;
        session.ghost.clear();
        gtk_widget_queue_draw(canvas);
    }

    void placeShape(Vec2 end, bool commit) {
        double width = std::abs(end.x-shapeStart.x), height = std::abs(end.y-shapeStart.y);
        Vec2 origin(std::min(end.x,shapeStart.x),std::min(end.y,shapeStart.y));
        const double minimum = session.view().deviceRadiusToDocument(6);
        if (commit && (width < minimum || height < minimum)) {
            width = 96; height = 64; origin = shapeStart - Vec2(48,32);
        }
        PathRecord path;
        path.closed = true;
        path.fillEnabled = shapeFillEnabled;
        path.fillRGBA = shapeFill;
        path.style.colorRGBA = session.graphicsColor;
        path.style.width = session.graphicsWidth > 0 ? session.graphicsWidth : session.doc.defaultLineWidth();
        for (auto point : pendingShape->points)
            path.points.push_back(origin + Vec2(point.x*width, point.y*height));
        session.ghost.clear();
        if (!commit) {
            PreviewItem line;
            line.primitive = PreviewPrimitive::Polyline;
            line.style.width = path.style.width;
            line.style.colorRGBA = path.style.colorRGBA;
            line.points = path.points;
            line.points.push_back(line.points.front());
            session.ghost.add(line);
            gtk_widget_queue_draw(canvas);
            return;
        }
        cancelShape();
        auto id = std::make_shared<ObjectId>(kInvalidObjectId);
        auto result = session.submit(Command("插入图案", kInvalidateGeometry|kInvalidateAppearance,
            [path,id](CommandContext& c){
                auto error = c.document.addPath(path,id.get());
                return error.ok() ? CommandResult::success() : CommandResult::fail(error.code,error.message);
            }), "插入图案");
        if (result.ok()) session.setSelection({ObjectRef(ObjectKind::Path,*id)});
        report(result);
    }

    std::shared_ptr<Session> pendingTemplate;
    const char* pendingInsertion = "插入自定义模板";
    const char* pendingInserted = "自定义模板已插入";
    std::string lastExternalPaste;
    int externalPasteCount = 0;
    GtkWidget* saveTemplateItem = nullptr;
    studio::rapid_sprout::State rapidSprout;
    bool updatingToolLabels = false;
    bool expandPending = false;
    bool fitOnFirstDraw = false;
    DocumentId expandedDocument = 0;
    uint64_t expandedSerial = 0;
    std::vector<ObjectRef> expandedSelection;
    std::vector<std::pair<GtkWidget*,const linux_shortcuts::Shortcut*>> shortcutHints;
    int generationArrowStyle = 0;
    bool syncingGenerationStyle = false;
    std::array<std::vector<GtkToggleButton*>,4> generationButtons;
    std::array<GtkCheckMenuItem*,4> generationMenu{};
    void chooseGenerationStyle(int index) {
        if (syncingGenerationStyle) return;
        syncingGenerationStyle=true;
        generationArrowStyle=index;
        for (size_t i=0;i<generationButtons.size();++i)
            for (auto* button:generationButtons[i])
                gtk_toggle_button_set_active(button,int(i)==index);
        gtk_check_menu_item_set_active(generationMenu[index],TRUE);
        syncingGenerationStyle=false;
    }
    std::vector<GtkWidget*> arrowButtons;
    const arrows::PresetSpec* arrowStyle = arrows::presetById(0);

    void setArrowHeadTier(int tier) {
        finishCanvasText();
        std::vector<ObjectId> ids;
        for (const auto& record : session.doc.arrows().records()) {
            if (!record.isAlive()) continue;
            arrows::ArrowParams params;
            bool valid = false;
            auto error = arrows::readArrow(session.doc,record.id,&params,&valid);
            if (!error.ok()) { report(error); return; }
            if (valid && !arrows::isMechanismArrow(params)) ids.push_back(record.id);
        }
        if (!ids.empty()) {
            auto error = session.submit(Command("箭头头部大小", kInvalidateAppearance | kInvalidateGeometry,
                [ids,tier](CommandContext& c) {
                    for (auto id : ids) {
                        arrows::ArrowParams params;
                        bool valid = false;
                        auto result = arrows::readArrow(c.document,id,&params,&valid);
                        if (result.ok() && valid) {
                            result = arrows::writeArrow(c.document,id,arrows::withHeadTier(params,tier),
                                                       c.document.defaultLineWidth());
                            const ObjectRef ref(ObjectKind::Arrow,id);
                            c.document.noteTouchedExtensions(ref,false);
                            c.document.extensions(ref).erase("studio.cdxml.graphic");
                        }
                        if (!result.ok()) return CommandResult::fail(result.code,result.message);
                    }
                    return CommandResult::success();
                }), "箭头头部大小");
            if (!error.ok()) { report(error); return; }
        }
        session.arrowHeadTier = tier;
        if (!linux_config::Preferences().text("Arrow","HeadTier",std::to_string(tier))) {
            report({ErrorCode::IoError,"无法写入设置"});
            return;
        }
        gtk_widget_grab_focus(canvas);
        report({});
    }

    void chooseArrowStyle(const arrows::PresetSpec& preset) {
        cancelShape();
        finishCanvasText();
        leavePerspectiveTool();
        selectionGesture.cancel();
        tableDrag.cancel();
        pendingTemplate.reset();
        std::vector<ObjectId> selected;
        if (preset.family != arrows::ArrowFamily::Arc) {
            for (auto ref : session.selected) {
                if (ref.kind == ObjectKind::Arrow && session.doc.arrows().alive(ref.id) &&
                    static_cast<const Document&>(session.doc).extensions(ref).find(arrows::kArrowParamsKey))
                    selected.push_back(ref.id);
            }
        }
        if (!selected.empty()) {
            auto result = session.submit(Command("箭头样式", kInvalidateAppearance | kInvalidateGeometry,
                [selected, id=preset.presetId, tier=session.arrowHeadTier](CommandContext& c) {
                    for (auto object : selected) {
                        arrows::ArrowParams params;
                        bool valid = false;
                        auto error = arrows::readArrow(c.document, object, &params, &valid);
                        if (error.ok() && valid && !arrows::isMechanismArrow(params)) {
                            const auto width = c.document.defaultLineWidth();
                            const auto updated = arrows::applyPresetStyle(params, id, width);
                            error = arrows::writeArrow(c.document, object, arrows::withHeadTier(updated,tier), width);
                        }
                        if (!error.ok()) return CommandResult::fail(error.code,error.message);
                    }
                    return CommandResult::success();
                }), "箭头样式");
            if (!result.ok()) { report(result); return; }
        }
        arrowStyle = &preset;
        for (auto* button : arrowButtons) gtk_widget_queue_draw(button);
        session.activateCommand(preset.sourceCommand);
        gtk_widget_grab_focus(canvas);
        if (!linux_config::Preferences().text("Arrow","Command",preset.sourceCommand)) {
            report({ErrorCode::IoError,"无法写入设置"});
            return;
        }
        report({});
    }
    Vec2 panStart;

    bool activateShortcut(const linux_shortcuts::Shortcut& item);
    bool handleKey(const GdkEventKey& event);
    bool activateHotspotShortcut(const keymap::Hotkey& key, const interaction::Hotspot& spot);

    void updateCanvasCursor(Vec2 pixel) {
        auto* native=gtk_widget_get_window(canvas);
        if(!native)return;
        const char* name="";
        const auto& tool=session.entries[session.activeIndex];
        if(panning || session.objectDragActive() || tableDrag.pending())name="grabbing";
        else if(pointerOnCanvas && (mechanismPlacement!=linux_mechanism::Placement::None || pendingTemplate || pendingShape))name="crosshair";
        else if(pointerOnCanvas && tool.owner=="T09")name="text";
        else if(pointerOnCanvas && tool.owner=="T05" && !session.selected.empty()){
            if(perspectiveTool)name=selectionGesture.tilting()?"grabbing":"grab";
            else name=selectionGesture.cursor(session,pixel);
        }
        if (!*name && pointerOnCanvas && linux_table::canvasTool(session)) {
            const auto point=session.view().toDocument(pixel);
            for (const auto& table : session.doc.tables().records())
                if (table.isAlive() && table.frame.contains(point)) { name="grab"; break; }
        }
        if(canvasCursor==name)return;
        canvasCursor=name;
        auto* cursor=*name?gdk_cursor_new_from_name(gtk_widget_get_display(canvas),name):nullptr;
        gdk_window_set_cursor(native,cursor);
        if(cursor)g_object_unref(cursor);
    }
    void updateRibbonTool() {
        const auto& active=session.entries[session.activeIndex].command;
        for (auto* button : arrowButtons) {
            auto* context = gtk_widget_get_style_context(button);
            if (active == arrowStyle->sourceCommand) gtk_style_context_add_class(context,"active-tool");
            else gtk_style_context_remove_class(context,"active-tool");
        }
        if(active==highlightedTool)return;
        for(auto* item:ribbonTools[highlightedTool])gtk_style_context_remove_class(gtk_widget_get_style_context(item),"active-tool");
        for(auto* item:ribbonTools[active])gtk_style_context_add_class(gtk_widget_get_style_context(item),"active-tool");
        highlightedTool=active;
    }
    void updateDocumentStats() {
        if(!documentStats || (statsRevision==session.serial && statsZoom==session.view().scale))return;
        if(statsZoom<0 || statsRevision!=session.serial){
            auto alive=[](const auto& records){
                return std::count_if(records.begin(),records.end(),[](const auto& record){return record.isAlive();});
            };
            statsAtoms=alive(session.doc.atoms().records());
            statsBonds=alive(session.doc.bonds().records());
            statsRevision=session.serial;
        }
        statsZoom=session.view().scale;
        auto value=std::string(tr("原子总数 "))+std::to_string(statsAtoms)+" · "+tr("键")+" "+
            std::to_string(statsBonds)+" | "+std::to_string(int(100*statsZoom))+"%";
        gtk_label_set_text(GTK_LABEL(documentStats),value.c_str());
    }

    void leavePerspectiveTool() {
        if (!perspectiveTool) return;
        perspectiveTool = false;
        updateCanvasCursor(canvasPointer);
    }

    GtkWidget *valenceMenu=nullptr, *valenceItem=nullptr;
    ObjectId valenceTarget=kInvalidObjectId;
    uint64_t valenceMenuSerial=0;
    bool showValenceMenu(Vec2 point,GdkEvent* event) {
        std::map<ObjectId,Box> ignored;
        for (const auto& entry:session.doc.allObjectExtensions()) {
            if (entry.first.kind!=ObjectKind::Atom || !studio::validation::ignored(session.doc,entry.first.id)) continue;
            if (const auto* atom=session.doc.atoms().alive(entry.first.id))
                ignored.emplace(atom->id,Box(atom->position,atom->position));
        }
        if (!ignored.empty()) for (const auto& p:session.scene.primitives()) {
            if (p.source.kind!=ObjectKind::Atom || p.bounds.empty()) continue;
            auto it=ignored.find(p.source.id);
            if (it!=ignored.end()) { it->second.add(p.bounds.min); it->second.add(p.bounds.max); }
        }
        valenceTarget=kInvalidObjectId;
        double nearest=std::numeric_limits<double>::max();
        auto consider=[&](const auto& candidates) {
            for (const auto& [id,bounds]:candidates) {
                if (!studioio::inside(studio::validation::warningSquare(bounds),point,session.view().deviceRadiusToDocument(3))) continue;
                const double distance=length(session.doc.atoms().alive(id)->position-point);
                if (distance<nearest || (distance==nearest && id<valenceTarget)) { nearest=distance; valenceTarget=id; }
            }
        };
        consider(session.valenceWarnings());
        consider(ignored);
        if (valenceTarget==kInvalidObjectId) return false;
        if (!valenceMenu) {
            valenceMenu=gtk_menu_new();
            gtk_menu_attach_to_widget(GTK_MENU(valenceMenu),canvas,nullptr);
            valenceItem=gtk_menu_item_new_with_label("");
            gtk_menu_shell_append(GTK_MENU_SHELL(valenceMenu),valenceItem);
            g_signal_connect(valenceItem,"activate",G_CALLBACK(+[](GtkWidget*,Window* w){
                if (w->session.serial!=w->valenceMenuSerial) return;
                const bool ignore=!studio::validation::ignored(w->session.doc,w->valenceTarget);
                auto result=w->session.submit(studio::validation::setIgnored(w->valenceTarget,ignore),"价态提示");
                if (result.ok()) w->session.status=ignore?"已忽略此原子的价态错误 · 右键可恢复":"已恢复此原子的价态检查";
                w->report(result);
            }),this);
        }
        valenceMenuSerial=session.serial;
        gtk_menu_item_set_label(GTK_MENU_ITEM(valenceItem),tr(ignored.count(valenceTarget)?"恢复此原子的价态检查":"忽略此原子的价态错误"));
        gtk_widget_show_all(valenceMenu);
        gtk_menu_popup_at_pointer(GTK_MENU(valenceMenu),event);
        return true;
    }
    bool showSelectionMenu(Vec2 pixel, GdkEvent* event = nullptr) {
        if (down || panning || selectionGesture.pending() || tableDrag.pending() || pendingTemplate) return false;
        const auto point = session.view().toDocument(pixel);
        if (event && showTlcSpotMenu(pixel, event)) return true;
        if (event && showValenceMenu(point,event)) return true;
        projectionContextAxis = studio_projection::axisAt(session, point);
        const auto ref = session.pick(point);
        graphics::ShapeParams outline;
        catalyticOutline = studio::catalytic::ellipse(session.doc, ref, outline) ? ref : ObjectRef();
        if (catalyticOutline.valid()) {
            session.setSelection({catalyticOutline});
            report({});
        }
        if (session.selected.empty()) {
            if (projectionContextAxis == kInvalidObjectId) return false;
            const auto* bond = session.doc.bonds().alive(projectionContextAxis);
            std::vector<ObjectRef> refs;
            for (const auto atom : projection_molecule::component(session.doc, bond->beginAtom))
                refs.emplace_back(ObjectKind::Atom, atom);
            session.setSelection(refs);
            report({});
        }
        gtk_widget_set_visible(projectionConversion, projectionContextAxis != kInvalidObjectId);
        gtk_widget_set_visible(catalyticOutlineItem, catalyticOutline.valid());
        gtk_widget_set_visible(attachBaseHydrogenItem, session.selected.size() == 1 &&
            linux_mechanism::canAttachHydrogen(session.doc, session.selected.front()));
        gtk_widget_grab_focus(canvas);
        if (event) gtk_menu_popup_at_pointer(GTK_MENU(selectionMenu), event);
        else gtk_menu_popup_at_widget(GTK_MENU(selectionMenu), canvas, GDK_GRAVITY_CENTER, GDK_GRAVITY_NORTH_WEST, nullptr);
        return true;
    }

    void hoverCanvas(Vec2 pixel, guint state) {
        if (pixel.x < 0 || pixel.y < 0 || pixel.x >= gtk_widget_get_allocated_width(canvas) ||
                pixel.y >= gtk_widget_get_allocated_height(canvas)) {
            leaveCanvas();
            return;
        }
        const auto buttons = linux_table::controls(session);
        const auto previousControl = pointerOnCanvas ? buttons.at(canvasPointer) : -1;
        pointerOnCanvas = true;
        canvasPointer = pixel;
        updateCanvasCursor(pixel);
        interaction::InputEvent move;
        move.devicePosition = pixel;
        move.documentId = session.doc.id();
        move.viewGeneration = session.viewSerial;
        move.modifiers = (state & GDK_SHIFT_MASK ? kModShift : 0) |
            (state & GDK_CONTROL_MASK ? kModControl : 0) | (state & GDK_MOD1_MASK ? kModAlt : 0);
        const auto before = session.input.hotspot().object;
        session.input.handle(move, &session);
        bool arrowFeedback = false;
        if (!down && !panning) {
            auto* arrow = dynamic_cast<arrows::ArrowTool*>(session.entries[session.activeIndex].tool.get());
            if (arrow && (arrow->presetId()==49 || arrow->presetId()==57 || arrow->presetId()==58)) {
                const bool hadPreview = !session.ghost.items().empty();
                arrow->hoverPreview(session, session.view().toDocument(pixel), move.modifiers);
                arrowFeedback = hadPreview || !session.ghost.items().empty();
            }
        }
        const auto previousEndpoint = projectionHover;
        projectionHover = {};
        if (!pendingTemplate && !perspectiveTool &&
                session.entries[session.activeIndex].command.find("Eraser") == std::string::npos)
            projectionHover = studio_projection::endpointAt(session, session.view().toDocument(pixel));
        if (arrowFeedback || pendingTemplate || !(before == session.input.hotspot().object) || previousControl != buttons.at(pixel) ||
                previousEndpoint.found() != projectionHover.found() || previousEndpoint.capture != projectionHover.capture)
            gtk_widget_queue_draw(canvas);
    }

    void leaveCanvas() {
        if (!down) {
            auto* arrow = dynamic_cast<arrows::ArrowTool*>(session.entries[session.activeIndex].tool.get());
            if (arrow && (arrow->presetId()==49 || arrow->presetId()==57)) session.ghost.clear();
        }
        pointerOnCanvas = false;
        projectionHover = {};
        updateCanvasCursor(canvasPointer);
        rapidSprout.clear();
        interaction::InputEvent leave;
        leave.kind = interaction::InputKind::FocusLost;
        session.input.handle(leave, &session);
        gtk_widget_queue_draw(canvas);
    }

    std::string ribbonCaption(GtkWidget* button) const {
        auto field=[button](const char* key){
            return static_cast<const char*>(g_object_get_data(G_OBJECT(button),key));
        };
        const char* command=field("ribbon-command");
        const char* action=field("ribbon-action");
        std::string result=tr(field("ribbon-label"));
        bool first=true;
        for (const auto& item:linux_shortcuts::catalog()) {
            if (!(item.tool && g_str_equal(command,item.tool)) && item.id!=action) continue;
            auto binding=linux_shortcuts::current().binding(item);
            if (!binding.key) continue;
            result+=first?"  ":" / ";
            result+=binding.key==GDK_KEY_space && !binding.modifiers ? tr("空格") : linux_shortcuts::display(binding);
            first=false;
        }
        return result;
    }
    void showStatus(const std::string& message, bool priority=false, bool valence=false) {
        statusMessage=message;
        statusPriority=priority;
        auto* style=gtk_widget_get_style_context(status);
        if (valence) gtk_style_context_add_class(style,"valence-warning");
        else gtk_style_context_remove_class(style,"valence-warning");
        const auto caption=hoveredRibbonTool && !priority ? ribbonCaption(hoveredRibbonTool) : message;
        gtk_label_set_text(GTK_LABEL(status),caption.c_str());
    }
    void leaveRibbonTool(GtkWidget* button) {
        if (hoveredRibbonTool!=button) return;
        hoveredRibbonTool=nullptr;
        gtk_label_set_text(GTK_LABEL(status),statusMessage.c_str());
    }
    void refreshShortcutHints() {
        for (const auto& hint : shortcutHints) {
            const auto binding = linux_shortcuts::current().binding(*hint.second);
            if (GTK_IS_MENU_ITEM(hint.first)) {
                auto* label = gtk_bin_get_child(GTK_BIN(hint.first));
                if (GTK_IS_ACCEL_LABEL(label)) gtk_accel_label_set_accel(GTK_ACCEL_LABEL(label),
                    binding.key, binding.modifiers);
            } else {
                gtk_widget_set_tooltip_text(hint.first, binding.key ? linux_shortcuts::display(binding).c_str() : nullptr);
            }
        }
    }

    void chooseShortcuts() {
        if (!linux_shortcuts::configure(GTK_WINDOW(window))) return;
        auto* app = gtk_window_get_application(GTK_WINDOW(window));
        for (auto* document : documentsOf(app)) {
            if (document) document->refreshShortcutHints();
        }
    }

    void chooseDrawingStyles() {
        studio_drawing::Style style;
        bool applyCurrent = false;
        bool saved;
        {
            linux_drawing::Editor edit(GTK_WINDOW(window));
            saved = edit.run(style, applyCurrent);
        }
        gtk_widget_grab_focus(canvas);
        if (!saved) return;
        applyDrawingStyle(style, applyCurrent);
    }

    void applyDrawingStyle(const studio_drawing::Style& style, bool applyCurrent) {
        if (!applyCurrent) {
            session.status = "已保存新文档的绘图默认格式";
            report(Error());
            return;
        }
        const auto result = session.submit(Command("结构绘图默认格式",
            kInvalidateAppearance | kInvalidateGeometry,
            [style](CommandContext& ctx) {
                style.apply(ctx.document, linux_i18n::utf8);
                return CommandResult::success();
            }), "结构绘图默认格式");
        if (result.ok()) session.status = "已应用当前文档的绘图格式";
        report(result);
    }

    void fitPage() {
        const auto page = studioio::Page::get(session.doc);
        if (!page.valid()) return;
        auto transform = session.view();
        const double width = page.width * page.across, height = page.height * page.down;
        const auto visibleWidth = std::max(1, gtk_widget_get_allocated_width(canvas));
        const auto visibleHeight = std::max(1, gtk_widget_get_allocated_height(canvas));
        transform.center = Vec2(page.x + width * .5, page.y + height * .5);
        transform.scale = .92 * std::min(visibleWidth / width, visibleHeight / height) / transform.dpiScale;
        session.setView(transform);
        gtk_widget_queue_draw(canvas);
    }

    bool applyPage(const studioio::Page& original, const studioio::Page& page) {
        Error result;
        if (page.width != original.width || page.height != original.height ||
                page.across != original.across || page.down != original.down) {
            result = session.submit(Command("页面设置", kInvalidateAppearance,
                [page](CommandContext& ctx) {
                    page.store(ctx.document);
                    return CommandResult::success();
                }), "页面设置");
        }
        report(result);
        if (!result.ok()) return false;
        fitPage();
        return true;
    }

    void chooseSettings(bool openAI = false) {
        session.cancel();
        down = panning = false;
        auto* dialog = gtk_dialog_new_with_buttons(tr("设置"), GTK_WINDOW(window), GTK_DIALOG_MODAL,
            tr("关闭"), GTK_RESPONSE_CLOSE, tr("保存设置"), GTK_RESPONSE_ACCEPT, nullptr);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 790, 520);
        auto* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE, 0);
        auto* stack = GTK_STACK(gtk_stack_new());
        auto* sidebar = gtk_stack_sidebar_new();
        gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), stack);
        gtk_box_pack_start(GTK_BOX(box), sidebar, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(stack), TRUE, TRUE, 0);
        auto addPage = [&](const char* id, const char* title) {
            auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            auto* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_container_add(GTK_CONTAINER(scroll), panel);
            gtk_stack_add_titled(stack, scroll, id, tr(title));
            return GTK_BOX(panel);
        };
        auto original = studioio::Page::get(session.doc);
        linux_drawing::Editor drawing(GTK_WINDOW(dialog), addPage("drawing", "结构绘图"));
        linux_page::Editor paper(GTK_WINDOW(dialog), original, addPage("page", "页面与排版"));
        auto* aiPage = addPage("ai", "AI 设置");
        gtk_container_set_border_width(GTK_CONTAINER(aiPage), 12);
        linux_ai::Panel::SettingsFields aiFields(aiPage, assistant);
        gtk_expander_set_expanded(GTK_EXPANDER(aiFields.preferences), TRUE);
        auto* other = addPage("other", "其他设置");
        gtk_container_set_border_width(GTK_CONTAINER(other), 12);
        for (auto id : {"shortcuts", "customGroups", "language", "skin", "exportSkill", "checkUpdates"}) {
            const auto& command = linux_shortcuts::shortcut(id);
            auto* button = gtk_button_new_with_label(tr(command.label.c_str()));
            gtk_widget_set_halign(button, GTK_ALIGN_START);
            gtk_box_pack_start(other, button, FALSE, FALSE, 0);
            g_object_set_data(G_OBJECT(button), "command-id", const_cast<char*>(id));
            g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkWidget* b, Window* w) {
                auto id = static_cast<const char*>(g_object_get_data(G_OBJECT(b), "command-id"));
                w->activateShortcut(linux_shortcuts::shortcut(id));
            }), this);
        }
        auto* status = gtk_label_new("");
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), status, FALSE, FALSE, 2);
        gtk_widget_show_all(dialog);
        if (openAI) gtk_stack_set_visible_child_name(stack, "ai");
        while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            studio_drawing::Style style;
            auto page = original;
            if (drawing.changed && !drawing.readFields(style)) {
                gtk_stack_set_visible_child_name(stack, "drawing");
                continue;
            }
            if (paper.changed && !paper.validate(page)) {
                gtk_stack_set_visible_child_name(stack, "page");
                continue;
            }
            if (g_utf8_strlen(linux_ai::Panel::textOf(aiFields.presetInput).c_str(), -1) > 6000) {
                gtk_stack_set_visible_child_name(stack, "ai");
                gtk_label_set_text(GTK_LABEL(status), tr("预设提示词不得超过 6000 字"));
                continue;
            }
            if (drawing.changed) {
                bool current = false;
                if (!drawing.store(style, current)) {
                    gtk_stack_set_visible_child_name(stack, "drawing");
                    continue;
                }
                applyDrawingStyle(style, current);
            }
            if (paper.changed) {
                if (!paper.store(page) || !applyPage(original, page)) {
                    gtk_stack_set_visible_child_name(stack, "page");
                    continue;
                }
                original = page;
                paper.saved(page);
            }
            aiFields.apply(assistant);
            const auto aiError = assistant.saveSettings();
            if (!aiError.empty()) {
                gtk_stack_set_visible_child_name(stack, "ai");
                gtk_label_set_text(GTK_LABEL(status), aiError.c_str());
                continue;
            }
            gtk_label_set_text(GTK_LABEL(status), tr("已保存 · 设置已更新"));
        }
        gtk_widget_destroy(dialog);
        gtk_widget_grab_focus(canvas);
    }
    void choosePageSetup() {
        session.cancel();
        down = panning = false;
        auto original = studioio::Page::get(session.doc);
        auto page = original;
        {
            linux_page::Editor editor(GTK_WINDOW(window), original);
            while (editor.run(page)) {
                if (!applyPage(original, page)) break;
                original = page;
                editor.saved(page);
            }
        }
        gtk_widget_grab_focus(canvas);
    }

    void zoom(Vec2 pixel,double steps){
        const auto v=session.view();
        zoomAt(pixel,v.toDocument(pixel),v.scale*std::pow(1.15,steps));
    }
    void zoomAt(Vec2 pixel, Vec2 anchor, double scale){
        finishCanvasText();
        expandPending = false;
        auto v=session.view();
        v.scale=std::clamp(scale,.02,80.);
        v.center=v.center+anchor-v.toDocument(pixel);
        session.setView(v);
        if (session.gpuScene.size() >= 4000 || (panning && session.gpuScene.size() >= 200)) {
            if (wheelIdle) g_source_remove(wheelIdle);
            frameBuffer.cancelRefinement();
            wheelIdle = g_timeout_add(180, +[](gpointer data) -> gboolean {
                auto* w = static_cast<Window*>(data);
                auto& s = w->session;
                w->wheelIdle = 0;
                if (!w->down && !w->panning && &s.displayScene() == &s.gpuScene &&
                    w->frameBuffer.beginRefinement(s.gpuScene, s.renderView, s.serial, gtk_widget_get_scale_factor(w->canvas))) {
                    w->wheelIdle = g_idle_add(+[](gpointer data) -> gboolean {
                        auto* w = static_cast<Window*>(data);
                        auto& s = w->session;
                        if (!w->down && !w->panning && &s.displayScene() == &s.gpuScene &&
                            w->frameBuffer.refine(s.gpuScene, s.renderView, s.measurer.get(), s.serial,
                                                  gtk_widget_get_scale_factor(w->canvas))) return G_SOURCE_CONTINUE;
                        w->frameBuffer.cancelRefinement();
                        w->wheelIdle = 0;
                        gtk_widget_queue_draw(w->canvas);
                        return G_SOURCE_REMOVE;
                    }, w);
                } else gtk_widget_queue_draw(w->canvas);
                return G_SOURCE_REMOVE;
            }, this);
        }
        gtk_widget_queue_draw(canvas);
    }
    void pan(Vec2 pixel){
        expandPending = false;
        auto v=session.view();
        v.center=v.center+v.toDocument(panStart)-v.toDocument(pixel);
        session.setView(v);
        panStart=pixel;
        // A wheel preview stays interactive while the pointer is still moving.
        if (wheelIdle) {
            if (auto* source = g_main_context_find_source_by_id(nullptr, wheelIdle))
                g_source_set_ready_time(source, g_get_monotonic_time() + 180 * 1000);
        }
        gtk_widget_queue_draw(canvas);
    }
    void report(const Error& e, bool keepExpansion = false) {
        queueSelectionInfo(*this);
        updateRibbonTool();
        if (saveTemplateItem) gtk_widget_set_sensitive(saveTemplateItem, !session.selected.empty());
        if (!keepExpansion) expandPending = false;
        gchar* name = session.sourcePath.empty() ? g_strdup(tr(ocsrReview ? "识别结果修订" : "未命名结构")) :
            g_filename_display_basename(session.sourcePath.c_str());
        std::string title = std::string(session.dirty ? "* " : "") + name + " — MoleculeStudio";
        titleDocument(*this, title, name);
        g_free(name);
        if (tools) gtk_combo_box_set_active_id(GTK_COMBO_BOX(tools), std::to_string(session.activeIndex).c_str());
        if (pointerOnCanvas && !down && !panning && !tableDrag.pending() && !selectionGesture.pending())
            hoverCanvas(canvasPointer, 0);
        if (status) {
            const auto& source = e.ok() ? session.status : e.message;
            const std::string saved = "已保存 ";
            const auto message = e.ok() && source.compare(0, saved.size(), saved) == 0 ?
                std::string(tr("已保存")) + " " + source.substr(saved.size()) : linux_i18n::message(source);
            if (!e.ok()) showStatus(message,true);
            else if (const auto& warnings=session.valenceWarnings(); !warnings.empty()) {
                const auto atom=session.doc.atoms().alive(warnings.begin()->first);
                const auto assessment=studio::validation::assess(session.doc,*atom);
                std::string caption=std::string(tr("价态超限"))+" · "+elementSymbol(assessment.element)+
                    tr(" 的价态为 ")+std::to_string(assessment.used)+tr("，上限 ")+std::to_string(assessment.limit);
                if (warnings.size()>1) caption+=std::string(tr(" · 共 "))+std::to_string(warnings.size())+tr(" 处");
                showStatus(caption,true,true);
            } else if(source.find("host unbound")!=std::string::npos || source.find("choose")==0){
                const auto& tool=session.entries[session.activeIndex];
                const auto it=ribbonTools.find(tool.command);
                showStatus(it!=ribbonTools.end() && !it->second.empty() ? ribbonCaption(it->second.front()) :
                    linux_i18n::tr(r62::integration::ribbon::zhLabelForCommand(tool.command,tool.category).c_str()));
            } else showStatus(message);
        }
        gtk_widget_queue_draw(canvas);
    }
    void refreshToolLabels() {
        using namespace r62::integration::ribbon;
        updatingToolLabels = true;
        auto* list = GTK_COMBO_BOX_TEXT(tools);
        gtk_combo_box_text_remove_all(list);
        for (size_t index = 0; index < session.entries.size(); ++index) {
            const auto& entry = session.entries[index];
            if (!entry.limitation.empty()) continue;
            const auto group = entry.category == "arrow" ? "箭头" : zhCategory(entry.category);
            const auto label = linux_i18n::message(group) + " · " +
                linux_i18n::message(zhLabelForCommand(entry.command, entry.category));
            gtk_combo_box_text_append(list, std::to_string(index).c_str(), label.c_str());
        }
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(tools), std::to_string(session.activeIndex).c_str());
        updatingToolLabels = false;
    }
    void chooseLanguage() {
        auto* dialog = gtk_dialog_new_with_buttons(tr("界面语言"), GTK_WINDOW(window), GTK_DIALOG_MODAL,
            tr("取消"), GTK_RESPONSE_CANCEL, tr("应用"), GTK_RESPONSE_ACCEPT, nullptr);
        auto* choices = gtk_combo_box_text_new();
        for (int index = 0; index < studio_i18n::languageCount; ++index)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(choices),
                linux_i18n::utf8(studio_i18n::name(index)).c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(choices), linux_i18n::current());
        auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_container_set_border_width(GTK_CONTAINER(content), 12);
        gtk_container_add(GTK_CONTAINER(content), choices);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        gtk_widget_show_all(dialog);
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        const int selected = gtk_combo_box_get_active(GTK_COMBO_BOX(choices));
        gtk_widget_destroy(dialog);
        if (response != GTK_RESPONSE_ACCEPT || selected == linux_i18n::current()) return;
        if (!linux_i18n::save(selected)) {
            report({ErrorCode::IoError, "无法写入设置"});
            return;
        }
        auto* app = gtk_window_get_application(GTK_WINDOW(window));
        for (auto* document : documentsOf(app)) {
            if (!document || document->spectrumPage) continue;
            linux_i18n::refreshLabels(document->window);
            document->refreshToolLabels();
            document->refreshShortcutHints();
            document->statsZoom=-1;
            document->report(Error());
        }
    }
    void arrange(const char* operation, int argument = 0) {
        studio::arrangeSelection(session, session.status, operation, argument);
        report(Error());
    }
    void setConstraints(bool length, bool angles) {
        session.fixedLen = length;
        session.fixedAngle = angles;
        auto profile = session.input.profile();
        profile.fixedLength = length;
        profile.fixedAngles = angles;
        session.input.setProfile(profile);
    }
    void persistConstraints(const char* key, bool enabled) {
        setConstraints(gtk_check_menu_item_get_active(fixedLength), gtk_check_menu_item_get_active(fixedAngles));
        std::string error;
        if (!linux_config::Preferences().flag("Drawing", key, enabled, &error)) report({ErrorCode::IoError, error});
    }
    std::vector<ObjectId> selectedCaptions() const {
        const auto selection = studioio::expanded(session.doc, session.selected);
        std::vector<ObjectId> captions;
        for (auto ref : selection)
            if (ref.kind == ObjectKind::Text && session.doc.texts().alive(ref.id)) captions.push_back(ref.id);
        return captions;
    }
    void formatText(int flags, int alignment = -1) {
        const auto captions = selectedCaptions();
        if (captions.empty()) {
            report({ErrorCode::InvalidArgument, "请先选择文字"});
            return;
        }
        report(session.submit(Command("文字格式", kInvalidateGeometry | kInvalidateAppearance,
            [captions, flags, alignment](CommandContext& context){
                auto& doc = context.document;
                for (auto id : captions) {
                    auto format = doc.texts().alive(id)->format;
                    auto runs = doc.runs(id);
                    Error e;
                    if (alignment >= 0) {
                        format.align = uint8_t(alignment);
                    } else {
                        const bool clear = std::all_of(runs.begin(), runs.end(), [=](const TextRun& run){
                            return ((format.flags | run.flags) & flags) == flags;
                        });
                        const int mask = flags & (kSubscript | kSuperscript | kFormula) ?
                            kSubscript | kSuperscript | kFormula : flags;
                        format.flags &= ~mask;
                        for (auto& run : runs) {
                            run.flags &= ~mask;
                            if (!clear) run.flags |= flags;
                        }
                        e = doc.setTextRuns(id, runs);
                    }
                    if (e.ok()) e = doc.setTextFormat(id, format);
                    if (!e.ok()) return CommandResult::fail(e.code, e.message);
                }
                return CommandResult::success();
            }), "文字格式"));
    }
    void syncTypography() {
        if (!sizeEntry || !gtk_widget_get_mapped(sizeEntry) || syncingTypography) return;
        std::string family = nextFamily.empty() ? session.doc.defaultFont() : nextFamily;
        double points = nextSize > 0. ? nextSize : session.doc.defaultTextSize();
        if (captionEditor) {
            std::tie(family, points) = captionEditor->typography();
        } else {
            for (const auto id : selectedCaptions()) {
                const auto* text = session.doc.texts().alive(id);
                if (!text) continue;
                family = text->format.fontName.empty() ? session.doc.defaultFont() : text->format.fontName;
                points = text->size > 0. ? text->size : session.doc.defaultTextSize();
                const auto runs = session.doc.runs(id);
                if (!runs.empty() && !runs.front().fontName.empty()) family = runs.front().fontName;
                break;
            }
        }
        char number[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_formatd(number, sizeof(number), "%.8g", points);
        syncingTypography = true;
        for (auto field : {std::make_pair(fontEntry,family),std::make_pair(sizeEntry,std::string(number))})
            if (!gtk_widget_has_focus(field.first) && field.second != gtk_entry_get_text(GTK_ENTRY(field.first)))
                gtk_entry_set_text(GTK_ENTRY(field.first),field.second.c_str());
        syncingTypography = false;
    }
    void applyTypographyEntry(GtkWidget* entry) {
        if (syncingTypography) return;
        const char* value = gtk_entry_get_text(GTK_ENTRY(entry));
        std::string family;
        double points = 0.;
        if (entry == fontEntry) family = value;
        else {
            char* end = nullptr;
            points = g_ascii_strtod(value,&end);
            if (*end || !(points >= 4. && points <= 200.)) points = 0.;
        }
        if (!family.empty() || points > 0.) {
            if (captionEditor) captionEditor->setTypography(family,points);
            else {
                if (!family.empty()) nextFamily = family;
                if (points > 0.) nextSize = points;
                const auto captions = selectedCaptions();
                if (!captions.empty()) applyCaptionTypography(captions,family,points,0,{});
            }
        }
        gtk_widget_grab_focus(captionEditor ? captionEditor->view() : canvas);
        syncTypography();
    }
    void changeTypography(int action) {
        const auto captions = selectedCaptions();
        if (captions.empty()) { report({ErrorCode::InvalidArgument, "请先选择文字"}); return; }
        const auto& doc = session.doc;
        const auto& first = *doc.texts().alive(captions.front());
        auto* parent = GTK_WINDOW(window);
        std::string family;
        double points = 0.;
        std::optional<uint32_t> color;
        if (action == 0) {
            auto* picker = gtk_font_chooser_dialog_new(tr("字体"), parent);
            auto* initial = pango_font_description_new();
            const auto currentFamily = first.format.fontName.empty() ? doc.defaultFont() : first.format.fontName;
            pango_font_description_set_family(initial, currentFamily.c_str());
            pango_font_description_set_size(initial, static_cast<int>((first.size > 0. ? first.size : doc.defaultTextSize()) * PANGO_SCALE));
            gtk_font_chooser_set_font_desc(GTK_FONT_CHOOSER(picker), initial);
            pango_font_description_free(initial);
            if (gtk_dialog_run(GTK_DIALOG(picker)) == GTK_RESPONSE_OK) {
                auto* font = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(picker));
                if (font) {
                    if (const auto* name = pango_font_description_get_family(font)) family = name;
                    points = pango_font_description_get_size(font) / double(PANGO_SCALE);
                    pango_font_description_free(font);
                }
            }
            gtk_widget_destroy(picker);
        } else if (action == 2) color = linux_graphic_format::color(parent, "文字颜色", first.format.colorRGBA);
        gtk_widget_grab_focus(canvas);
        if ((action == 0 && family.empty()) || (action == 2 && !color)) return;
        const auto amount = action == 1 || action == -1 ? action : 0;
        applyCaptionTypography(captions,family,points,amount,color);
    }
    void applyCaptionTypography(const std::vector<ObjectId>& captions, const std::string& family,
                                double points, int amount, std::optional<uint32_t> color) {
        report(session.submit(Command("文字格式", kInvalidateGeometry | kInvalidateAppearance,
            [captions, family, points, amount, color](CommandContext& context) {
                auto& doc = context.document;
                for (auto id : captions) {
                    const auto* text = doc.texts().alive(id);
                    if (!text) return CommandResult::fail(ErrorCode::NotFound, "文字对象不存在");
                    auto format = text->format;
                    const auto size = text->size > 0. ? text->size : doc.defaultTextSize();
                    if (!family.empty()) format.fontName = family;
                    if (color) format.colorRGBA = *color;
                    auto runs = doc.runs(id);
                    for (auto& run : runs) {
                        if (!family.empty()) run.fontName = family;
                        if (points > 0.) run.size = 0.;
                        else if (amount) run.size = std::max(4., (run.size > 0. ? run.size : size) + amount);
                    }
                    auto result = doc.setTextFormat(id, format);
                    if (result.ok() && !runs.empty() && (!family.empty() || points > 0. || amount)) result = doc.setTextRuns(id, runs);
                    if (result.ok() && (points > 0. || amount))
                        result = doc.setTextSize(id, points > 0. ? points : std::max(4., size + amount));
                    if (!result.ok()) return CommandResult::fail(result.code, result.message);
                    if (color) {
                        auto& attributes = doc.extensions(ObjectRef(ObjectKind::Text, id));
                        attributes.set("studio.color", std::to_string(*color));
                        attributes.erase("studio.color.override");
                        attributes.erase("studio.text.colors");
                        attributes.erase("studio.text.colors.edited");
                    }
                }
                return CommandResult::success();
            }), "文字格式"));
    }
    void preciseTransform(bool scale) {
        if (session.selected.empty()) { report({ErrorCode::InvalidArgument, "请先选择结构"}); return; }
        const auto* title = scale ? "缩放对象" : "旋转对象";
        const auto* prompt = scale ? "比例（%）" : "角度（正值顺时针）";
        auto* dialog = gtk_dialog_new_with_buttons(tr(title), GTK_WINDOW(window), GTK_DIALOG_MODAL,
            tr("取消"), GTK_RESPONSE_CANCEL, tr("应用"), GTK_RESPONSE_ACCEPT, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        auto* body = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_container_set_border_width(GTK_CONTAINER(body), 12);
        auto* label = gtk_label_new(tr(prompt));
        auto* spin = gtk_spin_button_new_with_range(scale ? .1 : -3600., scale ? 10000. : 3600., 1.);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), scale ? 100. : 90.);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
        atk_object_set_name(gtk_widget_get_accessible(spin), tr(prompt));
        gtk_box_pack_start(GTK_BOX(body), label, FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(body), spin, FALSE, FALSE, 5);
        gtk_widget_show_all(dialog);
        const auto response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
        const auto value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
        gtk_widget_destroy(dialog);
        gtk_widget_grab_focus(canvas);
        if (response != GTK_RESPONSE_ACCEPT) return;
        transformSelected(scale, value);
    }
    void transformSelected(bool scale, double value) {
        const auto refs = session.selected;
        const auto* title = scale ? "缩放对象" : "旋转对象";
        const auto box = studioio::selectionBounds(session);
        const auto center = box.empty() ? session.view().center : (box.min + box.max) * .5;
        const auto matrix = scale ? selection::Affine2::scaling(center, value / 100., value / 100.) :
            selection::Affine2::rotation(center, -value);
        report(session.submit(Command(title, kInvalidateGeometry | kInvalidateAppearance,
            [refs, matrix](CommandContext& context) {
                const auto error = linux_selection::applyMatrix(context.document, refs, matrix);
                return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
            }), title));
    }
    void tidyText(bool restore) {
        const auto& document = static_cast<const Document&>(session.doc);
        const auto selection = studioio::expanded(document, session.selected);
        std::set<ObjectId> atoms, captions;
        if (restore) {
            for (const auto ref : selection) {
                if ((ref.kind == ObjectKind::Atom && !document.atomLabel(ref.id).empty()) ||
                    (ref.kind == ObjectKind::Text && document.texts().alive(ref.id))) {
                    const auto& extensions = document.extensions(ref);
                    const auto runs = ref.kind == ObjectKind::Atom ? studioatom::runs(document, ref.id) : document.runs(ref.id);
                    if (extensions.findValue(textbackup::key()) || std::any_of(runs.begin(), runs.end(), [](const TextRun& run) {
                            return run.flags & (kSubscript | kSuperscript | kFormula);
                        })) {
                        (ref.kind == ObjectKind::Atom ? atoms : captions).insert(ref.id);
                    }
                }
            }
        } else {
            for (const auto ref : studio_chemistry::scope(document, session.selected))
                if (ref.kind == ObjectKind::Atom && texttidy::needsTidyAtom(document, ref.id)) atoms.insert(ref.id);
            for (const auto ref : selection)
                if (ref.kind == ObjectKind::Text && texttidy::needsTidyText(document, ref.id)) captions.insert(ref.id);
            auto bounds = studioio::selectionBounds(session);
            if (std::none_of(selection.begin(), selection.end(), [](const ObjectRef& ref) {
                    return ref.kind == ObjectKind::Atom || ref.kind == ObjectKind::Bond;
                })) bounds = Box(Vec2(1, 1), Vec2());
            if (!bounds.empty()) {
                const double margin = std::max(12.0, std::max(bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y) * .05);
                const Vec2 padding(margin, margin);
                bounds.add(bounds.min - padding);
                bounds.add(bounds.max + padding);
            }
            for (const auto& text : document.texts().records()) {
                if (!text.isAlive() || captions.count(text.id)) continue;
                if (!session.selected.empty()) {
                    if (bounds.empty()) continue;
                    const auto box = selection::objectBounds(document, ObjectRef(ObjectKind::Text, text.id));
                    if (box.empty() || box.max.x < bounds.min.x || box.min.x > bounds.max.x ||
                        box.max.y < bounds.min.y || box.min.y > bounds.max.y) continue;
                }
                if (texttidy::needsTidyText(document, text.id)) captions.insert(text.id);
            }
        }
        if (atoms.empty() && captions.empty()) {
            session.status = restore ? "所选文字没有可撤销的自动排版" : "所选文字已经是整理后的格式";
            report({});
            return;
        }
        const auto* title = restore ? "撤销文字整理" : "文字整理";
        report(session.submit(Command(title, kInvalidateStructure | kInvalidateAppearance | kInvalidateGeometry,
            [atoms, captions, restore](CommandContext& context) {
                for (const auto id : atoms) {
                    const auto result = restore ? textbackup::restore(context.document, ObjectRef(ObjectKind::Atom, id)) :
                        texttidy::tidyAtom(context.document, id, nullptr);
                    if (!result.ok()) return CommandResult::fail(result.code, result.message);
                }
                for (const auto id : captions) {
                    const auto result = restore ? textbackup::restore(context.document, ObjectRef(ObjectKind::Text, id)) :
                        texttidy::tidyText(context.document, id, nullptr);
                    if (!result.ok()) return CommandResult::fail(result.code, result.message);
                }
                return CommandResult::success();
            }), title));
    }
    void finishAtomLabel(bool accept) {
        if (!atomEntry) return;
        auto* entry = atomEntry;
        atomEntry = nullptr;
        const auto id = editedAtom;
        editedAtom = kInvalidObjectId;
        auto value = chemcompletion::normalize(gtk_entry_get_text(GTK_ENTRY(entry)));
        if (chemcompletion::formulaLayout()) value = chemcompletion::hyphenatedGroup(value);
        const auto content = atomLabelRuns.capture(value);
        gtk_widget_destroy(entry);
        if (accept && (value != originalAtomLabel || atomLabelRuns.changed()) && session.doc.atoms().alive(id)) {
            if (value.empty()) report(session.submit(studio::hotspot_actions::clearLabel(id), "原子标签"));
            else {
                const bool textChanged = value != originalAtomLabel;
                auto command = nicknames::labelCommand(id, value);
                report(session.submit(Command("原子标签",
                    kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance | kInvalidateSemantics,
                    [id, command, content, value, textChanged](CommandContext& context) {
                        auto& bag = context.document.extensions(ObjectRef(ObjectKind::Atom, id));
                        auto result = CommandResult::success();
                        if (textChanged) {
                            bag.erase("studio.atom.runs");
                            bag.erase("studio.label.text");
                            bag.erase("pst.attr.MoleculeStudioLiteralLabel");
                            result = command.execute(context);
                        } else if (context.document.atomLabel(id).empty()) {
                            const auto error = context.document.setAtomLabel(id, value);
                            if (!error.ok()) return CommandResult::fail(error.code, error.message);
                        }
                        if (result.ok() && content.runs.empty() && textChanged && chemcompletion::formulaLayout() &&
                            value.find('-')!=std::string::npos && context.document.atomLabel(id)==value) {
                            const auto error=texttidy::tidyAtom(context.document,id,nullptr);
                            if (!error.ok()) return CommandResult::fail(error.code,error.message);
                        }
                        if (result.ok() && !content.runs.empty() && context.document.atomLabel(id) == value) {
                            studioatom::set(context.document, id, content.runs);
                            if (!content.colors.empty()) {
                                bag.set("studio.text.colors", texttidy::writeColors(content.colors));
                                bag.set("studio.text.colors.edited", "1");
                            }
                        }
                        return result;
                    }), "原子标签"));
            }
        }
    }

    void editLabel(ObjectId id, const std::string& typed = {}) {
        finishCanvasText();
        const auto* atom = session.doc.atoms().alive(id);
        if (!atom) return;
        auto label = session.doc.atomLabel(id);
        if (label.empty()) label = chemistry::readAtomLabel(session.doc, id, {}).text;
        originalAtomLabel = label.empty() ? elementSymbol(atom->element) : label;
        editedAtom = id;
        atomEntry = gtk_entry_new();
        auto* entry = GTK_ENTRY(atomEntry);
        atk_object_set_name(gtk_widget_get_accessible(atomEntry), tr("原子标签"));
        linux_groups::complete(entry);
        gtk_entry_set_width_chars(entry, 9);
        gtk_entry_set_text(entry, (typed.empty() ? originalAtomLabel : typed).c_str());
        atomLabelRuns.bind(entry, session.doc, id);
        gtk_widget_set_halign(atomEntry, GTK_ALIGN_START);
        gtk_widget_set_valign(atomEntry, GTK_ALIGN_START);
        gtk_widget_show(atomEntry);
        GtkRequisition request;
        gtk_widget_get_preferred_size(atomEntry, nullptr, &request);
        const auto anchor = session.view().toDevice(atom->position);
        const auto clamp = [](double position, int available) {
            return std::max(0, std::min(int(position), available));
        };
        gtk_widget_set_margin_start(atomEntry, clamp(anchor.x - 8, gtk_widget_get_allocated_width(canvas) - request.width));
        gtk_widget_set_margin_top(atomEntry, clamp(anchor.y - request.height / 2, gtk_widget_get_allocated_height(canvas) - request.height));
        gtk_overlay_add_overlay(GTK_OVERLAY(canvasOverlay), atomEntry);
        g_signal_connect_after(entry, "activate", G_CALLBACK(+[](GtkEntry*, Window* w) {
            w->finishAtomLabel(true);
            gtk_widget_grab_focus(w->canvas);
        }), this);
        g_signal_connect(entry, "key-press-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, Window* w) -> gboolean {
            if (event->keyval != GDK_KEY_Escape) return FALSE;
            if (gtk_entry_im_context_filter_keypress(GTK_ENTRY(widget), event)) return TRUE;
            w->finishAtomLabel(true);
            gtk_widget_grab_focus(w->canvas);
            return TRUE;
        }), this);
        gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_SECONDARY, "edit-clear-symbolic");
        gtk_entry_set_icon_tooltip_text(entry, GTK_ENTRY_ICON_SECONDARY, tr("取消"));
        g_signal_connect(entry, "icon-release", G_CALLBACK(+[](GtkEntry*, GtkEntryIconPosition, GdkEvent*, Window* w) {
            w->finishAtomLabel(false);
            gtk_widget_grab_focus(w->canvas);
        }), this);
        gtk_widget_grab_focus(atomEntry);
        if (typed.empty()) gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
        else gtk_editable_set_position(GTK_EDITABLE(entry), -1);
    }
    void manageCustomGroups() {
        std::string label;
        {
            linux_groups::Manager manager(GTK_WINDOW(window), session);
            label = manager.run();
        }
        gtk_widget_grab_focus(canvas);
        if (label.empty()) return;
        ObjectId target = kInvalidObjectId;
        if (session.selected.size() == 1 && session.selected.front().kind == ObjectKind::Atom)
            target = session.selected.front().id;
        report(nicknames::insert(session, label, target));
    }
    bool submitCaptionChemistry(Vec2 point, ObjectId caption, const linux_caption::Content& content) {
        const auto& typed = content.text;
        if (!chemcompletion::formulaLayout() || typed.empty() || typed.size() > 96 ||
            typed.find_first_of("\r\n") != std::string::npos || linux_caption::literal(session.doc, caption)) return false;
        auto label = chemcompletion::chemicalToken(typed);
        if (label.empty()) label = typed;
        label = chemcompletion::hyphenatedGroup(label);
        const auto* compound = compounddata::ion(label);
        const auto* hydride = compounddata::hydride(label);
        const auto* radical = studio::compact_radicals::find(label);
        int charge = 0;
        bool explicitCharge = false;
        const auto* ion = studioions::find(label, &charge, &explicitCharge);
        if (!compound && !hydride && !radical && (!ion || !explicitCharge)) return false;

        const auto formatted = texttidy::tidy(typed, true, content.runs, content.colors, false, true);
        const auto& rendered = formatted.changed ? formatted.plain : typed;
        const auto runs = rendered == label ? (formatted.changed ? formatted.runs : content.runs) : std::vector<TextRun>{};
        const auto colors = !content.colors.empty() && !runs.empty() ?
            texttidy::writeColors(formatted.changed ? formatted.colors : content.colors) : std::string();
        double size = session.doc.defaultTextSize();
        if (const auto* original = session.doc.texts().alive(caption)) {
            point = original->position;
            if (original->size > 0) size = original->size;
        }
        auto created = std::make_shared<ObjectId>(kInvalidObjectId);
        const auto result = session.submit(Command("输入可编辑化学对象",
            kInvalidateStructure | kInvalidateSemantics | kInvalidateAppearance | kInvalidateGeometry,
            [point, size, caption, label, compound, hydride, radical, ion, charge, created, runs, colors](CommandContext& context) {
                auto& document = context.document;
                const int element = compound || hydride || radical ? 6 : ion->element;
                auto error = document.addAtom(point + Vec2(size * .35, -size * .35), element, created.get());
                if (error.ok()) {
                    if (compound) error = nicknames::applyWholeIon(document, *created, *compound);
                    else if (radical) error = studio::compact_radicals::apply(document, *created, label, runs);
                    else if (hydride) {
                        error = nicknames::apply(document, *created, label);
                        if (error.ok() && !runs.empty()) studioatom::set(document, *created, runs);
                    }
                    else error = studioions::apply(document, *created, *ion, charge);
                }
                if (error.ok() && !colors.empty() && (hydride || radical))
                    document.extensions(ObjectRef(ObjectKind::Atom, *created)).set("studio.text.colors", colors);
                if (error.ok() && caption != kInvalidObjectId) {
                    const ObjectRef from(ObjectKind::Text, caption), to(ObjectKind::Atom, *created);
                    for (const auto& group : document.groups().records()) {
                        if (!group.isAlive() || std::find(group.members.begin(), group.members.end(), from) == group.members.end())
                            continue;
                        auto& members = document.groups().mutableGet(group.id)->members;
                        std::replace(members.begin(), members.end(), from, to);
                        document.noteTouchedGroup(group.id);
                    }
                    error = document.removeObject(from);
                }
                return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
            }), "输入可编辑化学对象");
        if (result.ok()) session.setSelection({ObjectRef(ObjectKind::Atom, *created)});
        report(result);
        return true;
    }
    void finishCaption(bool accept) {
        if (!captionEditor) return;
        auto editor = std::move(captionEditor);
        auto content = editor->capture();
        const bool changed = editor->changed(content);
        const bool textChanged = editor->textChanged(content);
        const auto caption = editedCaption;
        const auto point = captionAnchor;
        const bool editing = caption != kInvalidObjectId;
        const auto* endpoint = captionEndpoint.found() ? &captionEndpoint : nullptr;
        gtk_widget_destroy(captionBox);
        captionBox = nullptr;
        if (!accept) return;
        if (editing && changed) {
            if (!textChanged || !submitCaptionChemistry(point, caption, content))
                report(session.submit(linux_caption::edit(caption, std::move(content), chemcompletion::formulaLayout()), "编辑文字"));
        } else if (endpoint && !content.text.empty()) {
            auto created = std::make_shared<ObjectId>(kInvalidObjectId);
            auto create = studio_projection::addEndpointCaption(*endpoint, content.text, created);
            const auto result = session.submit(Command("投影取代基", kInvalidateStructure | kInvalidateAppearance | kInvalidateGeometry,
                [create, content = std::move(content), created](CommandContext& context) {
                    auto added = create.execute(context);
                    if (!added.ok()) return added;
                    auto error = linux_caption::apply(context.document, *created, content.runs, content.colors, false);
                    return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
                }), "投影取代基");
            if (result.ok()) session.setSelection({ObjectRef(ObjectKind::Text, *created)});
            report(result);
        } else if (!editing && !content.text.empty() && !submitCaptionChemistry(point, caption, content)) {
            ObjectId id=kInvalidObjectId;
            auto result=session.submit(linux_caption::add(point,nextSize > 0. ? nextSize : session.doc.defaultTextSize(),std::move(content),
                chemcompletion::formulaLayout(),&id),"原位文字");
            if(result.ok())session.setSelection({ObjectRef(ObjectKind::Text,id)});
            report(result);
        }
    }

    void finishCanvasText() {
        finishAtomLabel(true);
        finishCaption(true);
    }

    void editCaption(Vec2 point, ObjectId caption = kInvalidObjectId,
                     const studio_projection::Endpoint* endpoint = nullptr, const std::string& typed = {}) {
        finishCanvasText();
        const auto* existing = session.doc.texts().alive(caption);
        captionAnchor = existing ? existing->position : point;
        editedCaption = caption;
        captionEndpoint = endpoint ? *endpoint : studio_projection::Endpoint{};
        const auto anchor = session.view().toDevice(captionAnchor);
        const auto scale = session.view().scale * session.view().dpiScale;
        const int available = std::max(1, gtk_widget_get_allocated_width(canvas));
        int width = std::min(available, 200);
        if (existing) {
            const auto& doc = static_cast<const Document&>(session.doc);
            if (auto* stored = doc.extensions({ObjectKind::Text, caption}).findValue("studio.text.width"))
                width = std::min(available, std::max(60, int(std::round(g_ascii_strtod(stored->c_str(), nullptr) * scale))));
        }
        captionBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        gtk_widget_set_halign(captionBox, GTK_ALIGN_START);
        gtk_widget_set_valign(captionBox, GTK_ALIGN_START);
        gtk_overlay_add_overlay(GTK_OVERLAY(canvasOverlay), captionBox);
        captionEditor = std::make_unique<linux_caption::Editor>(captionBox, session.doc, caption, typed, scale, width, nextFamily, nextSize);
        auto* view = captionEditor->view();
        gtk_widget_set_name(view,"canvas-caption-input");
        atk_object_set_name(gtk_widget_get_accessible(view), tr("编辑文字"));
        auto* cancel = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
        gtk_widget_set_tooltip_text(cancel, tr("取消"));
        gtk_widget_set_focus_on_click(cancel, FALSE);
        gtk_box_pack_end(GTK_BOX(captionEditor->controls()), cancel, FALSE, FALSE, 0);
        g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton*, Window* w) {
            w->finishCaption(false);
            gtk_widget_grab_focus(w->canvas);
        }), this);
        g_signal_connect(view, "key-press-event", G_CALLBACK(+[](GtkWidget* view, GdkEventKey* event, Window* w) -> gboolean {
            const bool enter = event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter;
            if (!enter && event->keyval != GDK_KEY_Escape) return FALSE;
            if (enter && (event->state & GDK_SHIFT_MASK) && !(event->state & GDK_CONTROL_MASK)) return FALSE;
            if (gtk_text_view_im_context_filter_keypress(GTK_TEXT_VIEW(view), event)) return TRUE;
            w->finishCaption(true);
            gtk_widget_grab_focus(w->canvas);
            return TRUE;
        }), this);
        gtk_widget_show_all(captionBox);
        GtkRequisition request;
        gtk_widget_get_preferred_size(captionBox, nullptr, &request);
        const double size = existing && existing->size > 0 ? existing->size : session.doc.defaultTextSize();
        gtk_widget_set_margin_start(captionBox, std::max(0, std::min(int(anchor.x), available - request.width)));
        gtk_widget_set_margin_top(captionBox, std::max(0, std::min(int(anchor.y - size * scale),
            gtk_widget_get_allocated_height(canvas) - request.height)));
        gtk_widget_grab_focus(view);
    }
    bool editProjectionEndpoint(Vec2 point, const std::string& typed = {}) {
        const auto endpoint = studio_projection::endpointAt(session, point);
        if (!endpoint.found()) return false;
        down = false;
        selectionGesture.cancel();
        session.cancel();
        if (endpoint.atom != kInvalidObjectId) editLabel(endpoint.atom, typed);
        else if (endpoint.text != kInvalidObjectId) editCaption(Vec2(), endpoint.text, nullptr, typed);
        else editCaption(endpoint.caption, kInvalidObjectId, &endpoint, typed);
        gtk_widget_grab_focus(canvas);
        return true;
    }
    void editTable(ObjectId id) {
        if (cellTable != id || cellDocument != session.doc.id()) selectTableCell(id,{},false);
        tableDrag.cancel();
        session.cancel();
        down = false;
        session.setSelection({ObjectRef(ObjectKind::Table, id)});
        {
            linux_table::Editor editor(GTK_WINDOW(window), session, id, lastCell,
                [this] { report(Error()); }, &firstCell);
            editor.run();
            selectTableCell(id,editor.firstCell(),false);
            selectTableCell(id,editor.lastCell(),true);
        }
        report(Error());
        gtk_widget_grab_focus(canvas);
    }
    void tableAction(const std::string& action, const std::string& title) {
        finishCanvasText();
        if (session.selected.size()!=1 || session.selected.front().kind!=ObjectKind::Table) {
            report({ErrorCode::InvalidArgument,"请先在画布上选择一个表格"});
            return;
        }
        const auto id = session.selected.front().id;
        if (cellTable!=id || cellDocument!=session.doc.id()) selectTableCell(id,{},false);
        if (action=="selectAll") {
            const auto* table = session.doc.tables().alive(id);
            if (!table || !table->rows || !table->columns) return;
            selectTableCell(id,{},false);
            selectTableCell(id,{table->rows-1,table->columns-1},true);
            editTable(id);
            return;
        }
        using namespace linux_table;
        static const std::map<std::string,Operation> operations = {
            {"rowAbove",RowAbove},{"rowBelow",RowBelow},{"deleteRows",DeleteRow},
            {"colLeft",ColLeft},{"colRight",ColRight},{"deleteCols",DeleteCol},
            {"rowHeight",RowHeight},{"colWidth",ColWidth},{"merge",Merge},{"split",Split},
            {"grid",Grid},{"three",ThreeLines},{"horizontal",Horizontal},{"outer",Outer},{"none",NoBorder},
            {"clear",Clear},{"fontSize",FontSize},{"bold",Bold},{"left",AlignLeft},{"center",AlignCenter},
            {"right",AlignRight},{"top",AlignTop},{"middle",AlignMiddle},{"bottom",AlignBottom},
            {"equalRows",EqualRows},{"equalCols",EqualColumns},{"autoFit",AutoFit},
            {"borderWidth",BorderWidth},{"deleteTable",DeleteTable}
        };
        const auto op = operations.find(action);
        if (op==operations.end()) return;
        auto result = applyOperation(GTK_WINDOW(window),session,id,firstCell,lastCell,op->second,title);
        if (result.ok() && op->second==DeleteTable) {
            session.setSelection({});
            cellTable=kInvalidObjectId;
        }
        report(result);
        gtk_widget_grab_focus(canvas);
    }

    bool cancelMechanismPlacement() {
        if (mechanismPlacement == linux_mechanism::Placement::None) return false;
        mechanismPlacement = linux_mechanism::Placement::None;
        virtualBondOrigin = kInvalidObjectId;
        down = false;
        session.ghost.clear();
        updateCanvasCursor(canvasPointer);
        return true;
    }
    void placeMechanismSymbol(Vec2 point) {
        using linux_mechanism::Placement;
        const auto placement = mechanismPlacement;
        cancelMechanismPlacement();
        const bool equal = placement == Placement::Equivalence;
        const auto title = equal ? "插入等同符号" : "插入部分电荷标注";
        const auto width = session.graphicsWidth;
        const auto color = session.graphicsColor;
        auto created = std::make_shared<ObjectId>(kInvalidObjectId);
        const auto result = session.submit(Command(title, kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
            [point, equal, placement, width, color, created](CommandContext& context) {
                if (equal) return equivalence(context.document, point, width, color, *created);
                return partialCharge(context.document, placement == Placement::PartialNegative ? -1 : 1, point, *created);
            }), title);
        if (result.ok()) session.setSelection({ObjectRef(equal ? ObjectKind::Group : ObjectKind::Text, *created)});
        report(result);
    }
    void virtualBondGesture(PointerPhase phase, Vec2 point) {
        const auto target = session.nearestAtom(point, std::max(12., session.snapRadiusDevice()));
        if (phase == PointerPhase::Down) {
            virtualBondOrigin = target;
            if (target == kInvalidObjectId) {
                report({ErrorCode::InvalidArgument, "请从已有原子开始画待成键"});
                return;
            }
        }
        const auto* origin = session.doc.atoms().alive(virtualBondOrigin);
        const auto* end = session.doc.atoms().alive(target);
        session.ghost.clear();
        if (phase == PointerPhase::Up) {
            const auto begin = virtualBondOrigin;
            virtualBondOrigin = kInvalidObjectId;
            if (!origin || !end || begin == target) {
                report({ErrorCode::InvalidArgument, "待成键的两端需要捕捉两个不同原子"});
                return;
            }
            auto created = std::make_shared<ObjectId>(kInvalidObjectId);
            const auto result = session.submit(Command("插入待成键", kInvalidateStructure | kInvalidateGeometry | kInvalidateAppearance,
                [begin, target, created](CommandContext& context) {
                    const auto error = virtual_bonds::add(context.document, begin, target, created.get());
                    return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
                }), "插入待成键");
            if (result.ok()) session.setSelection({ObjectRef(ObjectKind::Arrow, *created)});
            report(result);
            return;
        }
        if (origin) {
            PreviewItem line;
            line.primitive = PreviewPrimitive::Polyline;
            line.points = {origin->position, end ? end->position : point};
            line.style.colorRGBA = virtual_bonds::color;
            line.style.width = session.doc.defaultLineWidth();
            line.style.dash = uint8_t(DashStyle::Dashed);
            session.ghost.add(line);
        }
        for (const auto* atom : {origin, end == origin ? nullptr : end}) {
            if (!atom) continue;
            PreviewItem highlight;
            highlight.subject = ObjectRef(ObjectKind::Atom, atom->id);
            highlight.primitive = PreviewPrimitive::Polyline;
            const auto radius = session.view().deviceRadiusToDocument(12);
            for (int step = 0; step <= 24; ++step)
                highlight.points.push_back(atom->position + direction(step * 15.) * radius);
            highlight.style.colorRGBA = 0x0078D4FFu;
            highlight.style.width = session.view().deviceRadiusToDocument(2.5);
            session.ghost.add(highlight);
        }
        gtk_widget_queue_draw(canvas);
    }
    void exportDrawingSkill() {
        auto* owner = GTK_WINDOW(window);
        const auto folder = linux_skill::chooseFolder(owner);
        gtk_widget_grab_focus(canvas);
        if (folder.empty()) return;
        auto source = linux_skill::fs::path(assets()) / "skills" / linux_skill::name;
        if (!g_file_test((source / "SKILL.md").c_str(), G_FILE_TEST_IS_REGULAR))
            source = linux_skill::fs::path(assets()).parent_path() / "skills" / linux_skill::name;
        const auto target = folder / linux_skill::name;
        const bool replace = g_file_test(target.c_str(), G_FILE_TEST_IS_DIR) &&
            !g_file_test(target.c_str(), G_FILE_TEST_IS_SYMLINK);
        if (replace && !linux_skill::confirmReplacement(owner, target)) return;
        const auto result = linux_skill::exportTo(source, folder, replace);
        if (result.ok()) session.status = std::string(tr("已导出 AI 绘图 Skill")) + " · " + target.string();
        report(result);
    }
    void pointer(PointerPhase phase,double x,double y,guint state) {
        unsigned mods = modifiers(state);
        Vec2 point=session.view().toDocument(Vec2(x,y));
        if (mechanismPlacement != linux_mechanism::Placement::None) {
            if (mechanismPlacement == linux_mechanism::Placement::VirtualBond) virtualBondGesture(phase, point);
            else if (phase == PointerPhase::Up) placeMechanismSymbol(point);
            return;
        }
        if(session.entries[session.activeIndex].owner=="T09"){
            if (phase != PointerPhase::Up) return;
            if (editProjectionEndpoint(point)) return;
            const auto atom = session.nearestAtom(point, 8.);
            if (atom != kInvalidObjectId) {
                editLabel(atom);
                return;
            }
            const auto hit = session.pick(point);
            editCaption(point, hit.kind == ObjectKind::Text ? hit.id : kInvalidObjectId);
            return;
        }
        auto result=session.gesture(phase,point,mods);
        if(phase==PointerPhase::Move && result.ok()){
            updateCanvasCursor({x,y});
            gtk_widget_queue_draw(canvas);
        }else report(result);
    }
    void conformer(){
        auto refs=studioio::expanded(session.doc,session.selected);
        if(session.selected.empty())for(auto ref:session.all())refs.insert(ref);
        std::set<ObjectId> atoms;
        std::vector<ObjectId> queue;
        auto add=[&](ObjectId id){if(atoms.insert(id).second)queue.push_back(id);};
        for(auto ref:refs){
            if(ref.kind==ObjectKind::Atom)add(ref.id);
            if(ref.kind==ObjectKind::Bond){
                auto* bond=session.doc.bonds().alive(ref.id);
                if(bond){add(bond->beginAtom);add(bond->endAtom);}
            }
        }
        for(size_t i=0;i<queue.size();++i)for(auto id:session.doc.incidentBonds(queue[i])){
            auto* bond=session.doc.bonds().alive(id);
            if(bond){add(bond->beginAtom);add(bond->endAtom);}
        }
        std::vector<ObjectRef> scope;
        for(auto id:atoms)scope.emplace_back(ObjectKind::Atom,id);
        for(auto& bond:session.doc.bonds().records())
            if(bond.isAlive() && atoms.count(bond.beginAtom))scope.emplace_back(ObjectKind::Bond,bond.id);
        studio_conformer::generate(session,scope,session.status,[](const std::string& input,std::string& output){
            GError* error=nullptr;
            const auto script = assets() + "/chemistry_3d.py";
            auto* proc=g_subprocess_new(GSubprocessFlags(G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE),
                &error,"/usr/bin/timeout","35","/usr/bin/python3",script.c_str(),"/dev/stdin","/dev/stdout",nullptr);
            gchar *result=nullptr,*diagnostic=nullptr;
            bool ok=proc && g_subprocess_communicate_utf8(proc,input.c_str(),nullptr,&result,&diagnostic,&error) && g_subprocess_get_successful(proc);
            output=error?error->message:(result && *result?result:(diagnostic && *diagnostic?diagnostic:"三维计算未完成"));
            g_free(result);g_free(diagnostic);g_clear_error(&error);
            if(proc)g_object_unref(proc);
            return ok;
        });
        report(Error());
    }
    void properties(){
        linux_mass::show(GTK_WINDOW(window),
            studio::analyze(session.doc, session.selected.empty() ? session.all() : session.selected));
    }
    Error exportObjectsXml(const std::vector<ObjectRef>& refs, std::string* output) {
        Document subset;
        auto result = studioio::append(session.doc, subset, refs, Vec2(), nullptr);
        return result.ok() ? studioio::exportXml(subset, output) : result;
    }
    Error selectedXml(std::string* output) {
        return exportObjectsXml(session.selected.empty() ? session.all() : session.selected, output);
    }
    void cleanStructure(bool reaction = false) {
        studio_chemistry::CleanupRequest request;
        auto result = studio_chemistry::prepareCleanup(session.doc, session.selected, reaction, request);
        if (!result.ok()) { report(result); return; }
        const auto document = session.doc.id();
        const auto serial = session.serial;
        std::string failure;
        const auto output = linux_worker::run(GTK_WINDOW(window), "正在整理化学结构…",
            {"/usr/bin/python3", conversionScript(), "text/xml", "text/xml", "clean"}, request.xml, failure, 45);
        if (!failure.empty()) { report({ErrorCode::InvalidArgument, failure}); return; }
        if (output.empty()) return;
        if (session.doc.id() != document || session.serial != serial) {
            report({ErrorCode::InvalidArgument, "等待期间文档已更改，请重新整理。"});
            return;
        }
        Command command;
        result = studio_chemistry::prepareCleanupCommand(session.doc, request, output, command);
        if (result.ok()) result = session.submit(command, "整理结构");
        if (result.ok()) session.status = studio_chemistry::cleanupStatus(request);
        report(result);
    }
    void collapseGroups() {
        Error result;
        if (!studio_group_actions::foldCatalytic(session, false, result))
            result = studio_group_actions::collapse(session);
        report(result);
    }
    void expandGroups() {
        const bool repeated = expandPending && expandedDocument == session.doc.id() &&
            expandedSerial == session.serial && expandedSelection == session.selected;
        expandPending = false;
        Error result;
        if (studio_group_actions::foldCatalytic(session, true, result)) { report(result); return; }
        studio_group_actions::NextExpansion next;
        result = studio_group_actions::expand(session, repeated, [this](const std::string& input, std::string& output) {
            return runLocalCleanup(GTK_WINDOW(window), "正在整理化学结构…", input, output);
        }, next);
        if (result.ok() && next == studio_group_actions::NextExpansion::CleanSelection) {
            cleanStructure();
            return;
        }
        if (result.ok() && next == studio_group_actions::NextExpansion::AllowRepeatCleanup) {
            expandPending = true;
            expandedDocument = session.doc.id();
            expandedSerial = session.serial;
            expandedSelection = session.selected;
        }
        report(result, true);
    }
    void annotateStereo() {
        std::string input;
        auto result = studio_stereo::writeInput(session, input);
        if (!result.ok()) { report(result); return; }
        const auto document = session.doc.id();
        const auto serial = session.serial;
        std::string failure;
        const auto output = linux_worker::run(GTK_WINDOW(window), "正在标注构型…（Esc 取消）",
            {"/usr/bin/python3", conversionScript(), "text/xml", "text/x-moleculestudio-stereo", "stereolabels"},
            input, failure, 45);
        if (!failure.empty()) { report({ErrorCode::InvalidArgument, failure}); return; }
        if (output.empty()) return;
        if (session.doc.id() != document || session.serial != serial) {
            report({ErrorCode::RevisionConflict, "等待期间文档已更改，请重新标注。"});
            return;
        }
        report(studio_stereo::apply(session, output));
    }
    void structureToName() {
        auto refs = studio_chemistry::scope(session.doc, session.selected);
        if (refs.empty()) { report({ErrorCode::InvalidArgument, "请先绘制或选中结构"}); return; }
        const auto first = session.doc.atoms().alive(refs.front().id)->position;
        Box bounds(first, first);
        for (auto ref : refs)
            if (ref.kind == ObjectKind::Atom) bounds.add(session.doc.atoms().alive(ref.id)->position);
        std::string input;
        auto exported = exportObjectsXml(refs, &input);
        if (!exported.ok()) { report(exported); return; }
        const auto document = session.doc.id();
        const auto serial = session.serial;
        std::string failure;
        const auto name = linux_worker::run(GTK_WINDOW(window), "正在生成系统名称…",
            {"/usr/bin/python3", conversionScript(), "text/xml", "chemical/x-name"}, input, failure, 45);
        if (!failure.empty()) { report({ErrorCode::InvalidArgument, failure}); return; }
        if (name.empty()) return;
        if (session.doc.id() != document || session.serial != serial) {
            report({ErrorCode::InvalidArgument, "等待期间文档已更改，请重新选择结构。"});
            return;
        }
        auto id = std::make_shared<ObjectId>(kInvalidObjectId);
        auto result = session.submit(Command("系统命名", kInvalidateAppearance | kInvalidateGeometry,
            [refs, name, bounds, id](CommandContext& context) {
                auto error = studioNaming::createCaption(context.document, name,
                    Vec2(bounds.min.x, bounds.max.y + 22), refs, id.get());
                return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
            }), "系统命名");
        if (result.ok()) {
            session.setSelection({{ObjectKind::Text, *id}});
            session.status = name;
        }
        report(result);
    }
    void nameToStructure() {
        std::string input;
        for (auto ref : session.selected) {
            if (ref.kind != ObjectKind::Text || !session.doc.texts().alive(ref.id)) continue;
            if (!input.empty()) input += ' ';
            input += session.doc.plainText(ref.id);
        }
        if (input.empty()) {
            auto* dialog = gtk_dialog_new_with_buttons(tr("命名转结构"), GTK_WINDOW(window), GTK_DIALOG_MODAL,
                tr("取消"), GTK_RESPONSE_CANCEL, tr("插入"), GTK_RESPONSE_ACCEPT, nullptr);
            auto* entry = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(entry), tr("英文系统名称"));
            gtk_entry_set_width_chars(GTK_ENTRY(entry), 48);
            gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
            gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
            gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
            gtk_widget_show_all(dialog);
            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
                input = gtk_entry_get_text(GTK_ENTRY(entry));
            gtk_widget_destroy(dialog);
        }
        if (input.empty()) return;
        const auto document = session.doc.id();
        const auto serial = session.serial;
        std::string failure;
        const auto cdxml = linux_worker::run(GTK_WINDOW(window), "正在解析系统名称…",
            {"/usr/bin/python3", conversionScript(), "chemical/x-name", "text/xml"}, input, failure, 35);
        if (!failure.empty()) { report({ErrorCode::InvalidArgument, failure}); return; }
        if (cdxml.empty()) return;
        if (session.doc.id() != document || session.serial != serial) {
            report({ErrorCode::InvalidArgument, "等待期间文档已更改，请重新插入结构。"});
            return;
        }
        report(insertCdxml(cdxml, "命名转结构"));
    }
    void predictSpectrum() {
        if (!g_file_test("/usr/share/moleculestudio-nmr/csp5/__init__.py", G_FILE_TEST_IS_REGULAR)) {
            report({ErrorCode::Unsupported, "请安装 moleculestudio-nmr 核磁预测运行时"});
            return;
        }
        auto* owner = GTK_WINDOW(window);
        std::string source;
        auto loaded = selectedXml(&source);
        if (!loaded.ok()) { report(loaded); return; }
        if (openSpectrumTab(*this, source)) return;
        const auto action = linux_nmr::options(owner);
        if (action.empty()) return;
        std::string failure;
        const auto output = linux_worker::run(owner, "正在预测核磁位移…",
            {"/usr/bin/python3", conversionScript(), "text/xml", "text/x-moleculestudio-nmr", action}, source, failure, 90);
        if (!failure.empty()) {
            auto* message = gtk_message_dialog_new(owner, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE,
                                                  "%s", tr("核磁预测失败"));
            gtk_dialog_add_button(GTK_DIALOG(message), tr("关闭"), GTK_RESPONSE_CLOSE);
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(message), "%s", linux_i18n::message(failure).c_str());
            gtk_dialog_run(GTK_DIALOG(message));
            gtk_widget_destroy(message);
        } else if (!output.empty()) {
            linux_nmr::Spectrum spectrum;
            if (spectrum.read(output)) linux_nmr::show(owner, spectrum);
            else report({ErrorCode::ParseError, "无法读取核磁预测结果"});
        }
    }
    void recognizeImage() {
        std::string failure;
        const auto recognized = linux_ocsr::import(GTK_WINDOW(window), failure);
        if (!failure.empty()) { report({ErrorCode::InvalidArgument, failure}); return; }
        if (recognized.path.empty()) return;
        if (recognized.action == linux_ocsr::ReviewAction::Edit) {
            auto* app = gtk_window_get_application(GTK_WINDOW(window));
            showDocument(app, recognized.path.c_str(), std::make_unique<OcsrReview>(page, session.doc.id()));
            return;
        }
        auto source = std::make_shared<Session>();
        auto result = source->open(recognized.path);
        if (result.ok() && recognized.action == linux_ocsr::ReviewAction::Copy) {
            source->setSelection(source->all());
            copyRecognized(std::move(source));
            return;
        } else if (result.ok()) {
            result = insertStructure(source, "插入识别结构");
            if (result.ok()) {
                linux_ocsr::confirm(GTK_WINDOW(window), recognized, failure);
                if (!failure.empty()) session.status = tr("已插入识别结构") + std::string(" · ") + failure;
            }
        }
        report(result);
    }
    void copyRecognized(std::shared_ptr<Session> source) {
        struct Pending {
            GWeakRef owner;
            std::shared_ptr<Session> source;
            Pending(GtkWidget* window, std::shared_ptr<Session> value) : source(std::move(value)) {
                g_weak_ref_init(&owner, G_OBJECT(window));
            }
            ~Pending() { g_weak_ref_clear(&owner); }
        };
        // Leave the nested dialog/event before claiming the X11 selection.
        // Otherwise GTK may reuse the older event that opened recognition.
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, +[](gpointer opaque) -> gboolean {
            auto& job = *static_cast<Pending*>(opaque);
            const std::unique_ptr<GObject, decltype(&g_object_unref)> owner(
                G_OBJECT(g_weak_ref_get(&job.owner)), g_object_unref);
            if (!owner || !gtk_widget_get_parent(GTK_WIDGET(owner.get()))) return G_SOURCE_REMOVE;
            auto& target = *static_cast<Window*>(g_object_get_data(owner.get(), "document-window"));
            const auto result = job.source->router.dispatch(r62::interaction::HotkeyAction::EditCopy, "GTK");
            if (result == r62::interaction::RouterOutcome::Handled) {
                target.session.status = tr("已复制结构");
                target.report({});
            } else {
                target.report({ErrorCode::IoError, job.source->status});
            }
            return G_SOURCE_REMOVE;
        }, new Pending(page, std::move(source)), +[](gpointer opaque) { delete static_cast<Pending*>(opaque); });
    }
    void finishOcsrReview(bool copy) {
        if (!ocsrReview || session.all().empty()) {
            report({ErrorCode::InvalidArgument, "预览中没有可用结构"});
            return;
        }
        if (copy) {
            const auto selected = session.selected;
            session.setSelection(session.all());
            const auto outcome = session.router.dispatch(r62::interaction::HotkeyAction::EditCopy, "GTK");
            session.setSelection(selected);
            report(outcome == r62::interaction::RouterOutcome::Handled ? Error() : Error(ErrorCode::IoError, session.status));
            return;
        }

        const std::unique_ptr<GObject, decltype(&g_object_unref)> owner(
            G_OBJECT(g_weak_ref_get(&ocsrReview->target)), g_object_unref);
        auto* sourceWindow = owner ? static_cast<Window*>(g_object_get_data(owner.get(), "document-window")) : nullptr;
        if (!sourceWindow || !gtk_widget_get_parent(GTK_WIDGET(owner.get())) ||
            sourceWindow->session.doc.id() != ocsrReview->document) {
            report({ErrorCode::NotFound, "原文档已关闭，可复制修订结构或另存为"});
            return;
        }
        std::string xml;
        auto result = ocsr_edit::exportReview(session.doc, &xml);
        auto source = std::make_shared<Session>();
        if (result.ok()) result = source->openBytes(xml);
        if (result.ok()) result = sourceWindow->insertStructure(source, "插入修订后的识别结构");
        if (!result.ok()) { report(result); return; }
        sourceWindow->report({});
        selectDocument(*sourceWindow);
        gtk_window_present(GTK_WINDOW(sourceWindow->window));
        session.dirty = false;
        gtk_window_close(GTK_WINDOW(window));
    }
    void generateMechanismStep(int mode) {
        session.cancel();
        const auto serial = session.serial;
        const auto document = session.doc.id();
        try {
            const Document source = session.doc;
            auto plan = planMechanismStep(GTK_WINDOW(window), source, session.selected, session.all(),
                                          mode, generationArrowStyle, false);
            if (session.serial != serial || session.doc.id() != document)
                throw std::runtime_error("等待期间文档已更改，请重新选择并生成。");
            const auto result = session.submit(plan.command, "生成机理下一步");
            if (!result.ok()) throw std::runtime_error(result.message);
            session.selected = *plan.created;
            session.activateCommand("chooseSelectionTool_Marquee");
            session.fit();
            session.status = std::string(*plan.wrapped ? "已换到下一行左侧生成下一步结构（" :
                                                        "已在右侧生成下一步结构（") + plan.detail;
            report(Error());
        } catch (const std::exception& error) {
            report({ErrorCode::InvalidArgument, error.what()});
        }
    }
    void showAssistant() {
        auto script = assets() + "/run_ai.py";
        if (!g_file_test(script.c_str(), G_FILE_TEST_IS_REGULAR)) script = assets() + "/linux/run_ai.py";
        try {
            const auto error = assistant.request(GTK_WINDOW(window), session, script,
                [](CommandContext& command, const std::vector<ObjectRef>& selection, int mode, int style,
                   bool keepLossStructures, std::vector<ObjectRef>& refs) {
                    auto plan = planMechanismStep(nullptr, command.document, selection, aiobjects::all(command.document),
                                                  mode, style, keepLossStructures);
                    auto result = plan.command.execute(command);
                    if (result.ok()) refs = *plan.created;
                    return result;
                });
            report(error.empty() ? Error() : Error(ErrorCode::InvalidArgument, error));
        } catch (const std::exception& error) {
            report({ErrorCode::InvalidArgument, error.what()});
        }
    }
    Error insertStructure(std::shared_ptr<Session> source, const std::string& label = "插入结构",
                          const Vec2* center = nullptr,
                          studioio::PasteFormatAction action = studioio::PasteFormatAction::Default){
        const auto delta = (center ? *center : session.view().center) - source->view().center;
        auto objects = source->all();
        auto inserted = std::make_shared<std::vector<ObjectRef>>();
        const auto sourceFormat = studioio::drawingFormat(source->doc);
        const auto targetFormat = studioio::drawingFormat(session.doc);
        auto result = session.submit(Command(label, kInvalidateStructure | kInvalidateGeometry,
            [source, objects, delta, inserted, action, sourceFormat, targetFormat](CommandContext& context){
                inserted->clear();
                if (action == studioio::PasteFormatAction::AdoptSource) studioio::adoptPasteFormat(context.document, sourceFormat);
                auto e = studioio::append(source->doc, context.document, objects, delta, inserted.get());
                if (e.ok() && action == studioio::PasteFormatAction::KeepTarget)
                    studioio::normalizePasteFormat(context.document, *inserted, targetFormat);
                return e.ok() ? CommandResult::success() : CommandResult::fail(e.code, e.message);
            }), label);
        if (result.ok()) session.setSelection(*inserted);
        return result;
    }
    Error duplicateObjects(const std::vector<ObjectRef>& objects, Vec2 delta) {
        auto inserted = std::make_shared<std::vector<ObjectRef>>();
        auto result = session.submit(Command("拖动复制", kInvalidateStructure | kInvalidateGeometry,
            [objects, delta, inserted](CommandContext& context) {
                inserted->clear();
                auto e = studioio::append(context.document, context.document, objects, delta, inserted.get());
                return e.ok() ? CommandResult::success() : CommandResult::fail(e.code, e.message);
            }), "拖动复制");
        if (result.ok()) session.setSelection(*inserted);
        return result;
    }
    bool pasteExternal() {
        using namespace r62::selection;
        using studioio::PasteFormatAction;
        auto& clipboard = session.clipboard;
        if (clipboard.hasFormat(kDocumentClipboardFormat) || clipboard.hasFormat(kSelectionClipboardFormatName)) return false;

        std::string bytes;
        bool typed = false;
        for (const auto* format : {kCdxmlClipboardFormat, kCdxmlVendorClipboardFormat, kCdxClipboardFormat}) {
            const auto read = clipboard.read(format, &bytes, &typed);
            if (!read.ok()) { report(read); return true; }
            if (typed) break;
        }
        bool image = false;
        if (!typed) {
            const char* imageType = nullptr;
            const auto read = linux_image::clipboardBytes(bytes, imageType);
            if (!read.ok()) { report(read); return true; }
            image = imageType != nullptr;
            if (image) {
                const double width = session.view().deviceRadiusToDocument(std::max(100., session.renderView.widthPx));
                const double height = session.view().deviceRadiusToDocument(std::max(100., session.renderView.heightPx));
                bytes = linux_image::clipboardDocument(bytes, imageType, width, height);
                if (bytes.empty()) {
                    report({ErrorCode::ParseError, "无法读取剪贴板图像，或图像过大"});
                    return true;
                }
            } else {
                bytes = clipboard.readText();
            }
        }
        if (bytes.empty()) return false;
        if (bytes.size() > 128u * 1024u * 1024u) {
            report({ErrorCode::InvalidArgument, "剪贴板数据过大"});
            return true;
        }

        auto source = std::make_shared<Session>();
        const bool structure = typed || bytes.compare(0, 8, "VjCD0100") == 0 || bytes.find("<CDXML") != std::string::npos;
        ObjectId caption = kInvalidObjectId;
        auto result = structure ? source->openBytes(bytes) :
            source->doc.addText(Vec2(), session.doc.defaultTextSize(), bytes, &caption);
        if (!result.ok()) { report(result); return true; }
        const auto objects = source->all();
        if (objects.empty()) { report({ErrorCode::NotFound, "剪贴板中没有可编辑的对象"}); return true; }
        if (!structure) { source->refresh(); source->fit(); }

        auto action = PasteFormatAction::Default;
        const auto sourceFormat = studioio::drawingFormat(source->doc);
        const auto targetFormat = studioio::drawingFormat(session.doc);
        const auto mode = studioio::pasteFormatDecision(sourceFormat, targetFormat,
            studioio::documentEmpty(session.doc), typed);
        if (mode == studioio::PasteFormatMode::AdoptSource) action = PasteFormatAction::AdoptSource;
        else if (mode == studioio::PasteFormatMode::Ask) {
            const auto document = session.doc.id();
            const auto serial = session.serial;
            auto* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                GTK_BUTTONS_NONE, "%s", tr("选择粘贴格式"));
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s\n%s\n\n%s\n%s",
                tr("复制源格式"), studioio::drawingFormatSummary(sourceFormat).c_str(),
                tr("当前文档格式"), studioio::drawingFormatSummary(targetFormat).c_str());
            gtk_dialog_add_button(GTK_DIALOG(dialog), tr("取消"), GTK_RESPONSE_CANCEL);
            gtk_dialog_add_button(GTK_DIALOG(dialog), tr("保留当前文档格式"), 1);
            gtk_dialog_add_button(GTK_DIALOG(dialog), tr("使用复制源格式"), GTK_RESPONSE_ACCEPT);
            gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
            const auto response = gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            gtk_widget_grab_focus(canvas);
            if ((response != GTK_RESPONSE_ACCEPT && response != 1) || document != session.doc.id() || serial != session.serial)
                return true;
            action = response == GTK_RESPONSE_ACCEPT ? PasteFormatAction::AdoptSource : PasteFormatAction::KeepTarget;
        }

        gchar* hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
            reinterpret_cast<const guchar*>(bytes.data()), bytes.size());
        const std::string digest(hash);
        g_free(hash);
        const auto repeat = digest == lastExternalPaste ? externalPasteCount + 1 : 1;
        const auto origin = source->view().center;
        const auto pixel = session.view().toDevice(origin);
        const auto visible = pixel.x >= 0 && pixel.x <= session.renderView.widthPx &&
            pixel.y >= 0 && pixel.y <= session.renderView.heightPx;
        const double offset = 12. * (image ? repeat - 1 : repeat);
        const auto center = (visible && !image ? origin : session.view().center) + Vec2(offset, offset);
        session.cancel();
        result = insertStructure(source, "粘贴", &center, action);
        if (result.ok()) {
            lastExternalPaste = digest;
            externalPasteCount = repeat;
            leavePerspectiveTool();
            session.activateCommand("chooseSelectionTool_Marquee");
            if (image) session.status = tr("已粘贴图像");
            if (action == PasteFormatAction::AdoptSource) session.status = tr("已粘贴 · 已整体采用复制源格式");
            if (action == PasteFormatAction::KeepTarget) session.status = tr("已粘贴 · 保留当前文档格式");
        }
        report(result);
        return true;
    }
    Error insertCdxml(const std::string& cdxml, const std::string& label = "插入结构") {
        auto source = std::make_shared<Session>();
        auto result = persistence::readCdxml(cdxml, source->doc);
        if (!result.ok()) return result;
        source->refresh();
        source->fit();
        return insertStructure(source, label);
    }
    void chooseProjection(int kind, int variant) {
        if (kind == stereo_projection::Fischer && variant == 0) {
            auto* dialog = gtk_dialog_new_with_buttons(tr("费歇尔投影"), GTK_WINDOW(window), GTK_DIALOG_MODAL,
                tr("取消"), GTK_RESPONSE_CANCEL, tr("确认"), GTK_RESPONSE_ACCEPT, nullptr);
            auto* content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
            auto* label = gtk_label_new(tr("交叉中心数（1–12）"));
            auto* spin = gtk_spin_button_new_with_range(1, 12, 1);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 2);
            gtk_box_pack_start(content, label, FALSE, FALSE, 8);
            gtk_box_pack_start(content, spin, FALSE, FALSE, 8);
            gtk_widget_show_all(dialog);
            const bool confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
            variant = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
            gtk_widget_destroy(dialog);
            gtk_widget_grab_focus(canvas);
            if (!confirmed) return;
        }
        try {
            auto source = kind < 0 ? stereo_projection::sugar(session.doc, variant, kind == -2) :
                stereo_projection::make(session.doc, kind, variant);
            source->fit();
            leavePerspectiveTool();
            selectionGesture.cancel();
            tableDrag.cancel();
            session.cancel();
            down = panning = false;
            cancelShape();
            pendingTemplate = std::move(source);
            pendingInsertion = "插入立体投影";
            pendingInserted = "立体投影已插入";
            session.status = tr("点击画布放置，Esc 取消");
            gtk_widget_grab_focus(canvas);
            report({});
        } catch (const std::exception& error) {
            report({ErrorCode::InvalidArgument, error.what()});
        }
    }
    inline static const std::pair<const char*, const char*> conversionFormats[] = {
            {"chemical/x-daylight-smiles", "SMILES"},
            {"chemical/x-helm", "HELM"},
            {"chemical/x-mdl-molfile", "Molfile V2000"},
            {"chemical/x-mdl-molfile-v3000", "Molfile V3000"},
            {"chemical/x-name", "英文系统名称（导入）"}
        };
    void convertStructure(){
        auto* dialog=gtk_dialog_new_with_buttons(tr("化学格式转换"),GTK_WINDOW(window),GTK_DIALOG_MODAL,
                                    tr("关闭"),GTK_RESPONSE_CANCEL,tr("复制结构为文本"),1,tr("从文本插入"),2,nullptr);
        auto* content=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        auto* formats=gtk_combo_box_text_new();
        for (auto entry : conversionFormats)
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(formats), entry.first, tr(entry.second));
        g_signal_connect(formats, "changed", G_CALLBACK(+[](GtkComboBox* format, GtkDialog* dialog){
            const auto* mime = gtk_combo_box_get_active_id(format);
            gtk_dialog_set_response_sensitive(dialog, 1, mime && g_strcmp0(mime, "chemical/x-name") != 0);
        }), dialog);
        gtk_combo_box_set_active(GTK_COMBO_BOX(formats),0);
        auto* field=gtk_text_view_new();
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(field),TRUE);
        auto* scroll=gtk_scrolled_window_new(nullptr,nullptr);
        gtk_widget_set_size_request(scroll,540,240);
        gtk_container_add(GTK_CONTAINER(scroll),field);
        gtk_box_pack_start(GTK_BOX(content),formats,FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(content),scroll,TRUE,TRUE,0);
        gtk_widget_show_all(dialog);
        int action=gtk_dialog_run(GTK_DIALOG(dialog));
        const auto* selectedFormat = gtk_combo_box_get_active_id(GTK_COMBO_BOX(formats));
        if ((action != 1 && action != 2) || !selectedFormat) {
            gtk_widget_destroy(dialog);
            return;
        }
        const std::string mime=selectedFormat;
        GtkTextBuffer* buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(field));
        GtkTextIter start,end;
        gtk_text_buffer_get_bounds(buffer,&start,&end);
        gchar* value=gtk_text_buffer_get_text(buffer,&start,&end,FALSE);
        std::string input=value;
        g_free(value);
        gtk_widget_destroy(dialog);
        convertChemicalText(action==1,mime,std::move(input));
    }
    void chemicalClipboard(bool copy, int format) {
        std::string input;
        if (!copy) {
            auto* value = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
            if (value) input = value;
            g_free(value);
            if (input.empty()) {
                report({ErrorCode::InvalidArgument, "剪贴板中没有可转换的文本"});
                return;
            }
        }
        convertChemicalText(copy,conversionFormats[format].first,std::move(input));
    }
    void convertChemicalText(bool copy, const std::string& mime, std::string input) {
        if(copy){
            auto e=selectedXml(&input);
            if(!e.ok()){report(e);return;}
        }
        GError* error=nullptr;
        const auto script = conversionScript();
        auto* proc=g_subprocess_new(GSubprocessFlags(G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE),
                         &error,"/usr/bin/timeout","35","/usr/bin/python3",script.c_str(),
                         copy?"text/xml":mime.c_str(),copy?mime.c_str():"text/xml",nullptr);
        gchar *result=nullptr,*diagnostic=nullptr;
        if(proc && g_subprocess_communicate_utf8(proc,input.c_str(),nullptr,&result,&diagnostic,&error) && g_subprocess_get_successful(proc)){
            if(copy){
                gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),result,-1);
                showStatus(tr("已复制化学格式文本"));
            }else{
                report(insertCdxml(result));
            }
        }else{
            report({ErrorCode::InvalidArgument,
                error ? error->message : diagnostic && *diagnostic ? diagnostic : "化学格式转换未完成"});
        }
        g_clear_error(&error);
        if(proc)g_object_unref(proc);
        g_free(result);
        g_free(diagnostic);
    }
    enum class Library { All, Bio, Inorganic, Organic, Rings, Custom };
    std::weak_ptr<Session> ringPlacementSource;
    int pendingRingIndex=-1;
    static std::string ringKey(int i) {
        const auto& variant=ring_library::variants[i];
        const auto& parent=ring_library::parents[variant.parent];
        return std::string(parent.key)+"|"+std::to_string(i-parent.firstVariant);
    }
    static std::vector<std::string> recentRings() {
        std::istringstream saved(linux_config::Preferences().text("RingLibrary","Recent"));
        std::vector<std::string> keys;
        std::string key;
        while(keys.size()<12 && std::getline(saved,key,'\n')) keys.push_back(key);
        return keys;
    }
    void rememberPlacedRing(const std::shared_ptr<Session>& source) {
        if(source!=ringPlacementSource.lock() || pendingRingIndex<0) return;
        const auto key=ringKey(pendingRingIndex);
        auto keys=recentRings();
        keys.erase(std::remove(keys.begin(),keys.end(),key),keys.end());
        keys.insert(keys.begin(),key);
        if(keys.size()>12)keys.resize(12);
        std::ostringstream saved;
        for(const auto& entry:keys)saved<<entry<<'\n';
        linux_config::Preferences().text("RingLibrary","Recent",saved.str());
        ringPlacementSource.reset();pendingRingIndex=-1;
    }
    Error loadLibraryEntry(GtkTreeModel* model,GtkTreeIter& iter,std::shared_ptr<Session>& drawing) {
        int sugar=-1;gboolean mirror=FALSE;
        gtk_tree_model_get(model,&iter,9,&sugar,10,&mirror,-1);
        if (sugar>=0) {
            gchar* family=nullptr;
            gtk_tree_model_get(model,&iter,1,&family,-1);
            const std::string key=family?family:"";
            g_free(family);
            const char* side=key=="beta_d_glucosamine"?"NH2":
                (key=="beta_d_glcnac" || key=="beta_d_galnac")?"NHAc":"OH";
            drawing=stereo_projection::sugar(session.doc,sugar,mirror,side,key=="beta_d_glucuronic"?"COOH":"CH2OH");
            return {};
        }
        drawing=std::make_shared<Session>();
        auto& source=*drawing;
        gint ring=-1,kind=0;
        gchar* path=nullptr;
        gtk_tree_model_get(model,&iter,1,&path,2,&ring,3,&kind,-1);
        if (kind==int(Library::Custom)) {
            std::string file(path);g_free(path);
            try {linux_templates::read(file,source.doc);source.refresh();return {};}
            catch (const std::runtime_error& e) {return {ErrorCode::InvalidArgument,e.what()};}
        }
        if (ring<0) {
            auto result=source.open(assets()+"/"+path);
            g_free(path);
            return result;
        }
        g_free(path);
        const double length=session.doc.defaultBondLength();
        source.doc.setDefaultBondLength(length);
        auto refs=std::make_shared<std::vector<ObjectRef>>();
        auto result=source.submit(ringTemplateCommand(ring,Vec2(),length,refs),"插入环母体");
        if(!result.ok())return result;
        for(auto ref:*refs) {
            result=studio::graphic_style::apply(source.doc,ref,session.graphicsWidth,session.graphicsColor,session.graphicsWidth>0,true);
            if(!result.ok())return result;
        }
        source.refresh();
        return {};
    }
    void templateLibrary(Library library=Library::All){
        const char* title=library==Library::Custom?"我的模板":library==Library::Rings?"更多环":library==Library::Bio?"生物分子":library==Library::Inorganic?"常见无机试剂":library==Library::Organic?"常见有机合成试剂":"结构模板";
        auto* dialog=gtk_dialog_new_with_buttons(tr(title),GTK_WINDOW(window),GTK_DIALOG_MODAL,
                                                  tr("取消"),GTK_RESPONSE_CANCEL,tr(library==Library::Custom?"放置":"插入"),GTK_RESPONSE_OK,nullptr);
        gtk_widget_set_name(dialog,"template-library");
        gtk_window_set_default_size(GTK_WINDOW(dialog), 900, 550);
        auto* store=gtk_list_store_new(11,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_INT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_STRING,G_TYPE_INT,G_TYPE_BOOLEAN);
        std::set<std::string> categories;
        std::map<std::pair<Library,std::string>,int> families;
        std::vector<std::string> familyKeywords;
        auto add=[&](Library kind,const std::string& title,const std::string& path,const std::string& category,
                     const std::string& caption,int ring=-1,const std::string& family={},
                     const std::string& variant="标准结构",int sugar=-1,bool mirror=false){
            GtkTreeIter iter;
            auto [familyIt,inserted]=families.emplace(std::make_pair(kind,family.empty()?path:family),int(families.size()));
            if (inserted) familyKeywords.emplace_back();
            const int familyId=familyIt->second;
            auto key=linux_templates::folded(title);
            familyKeywords[familyId]+="\n"+key;
            gtk_list_store_insert_with_values(store,&iter,-1,0,title.c_str(),1,path.c_str(),2,ring,3,int(kind),
                                              4,key.c_str(),5,category.c_str(),6,caption.c_str(),7,familyId,8,variant.c_str(),9,sugar,10,mirror,-1);
            if (kind==library || (library==Library::All && kind!=Library::Rings && kind!=Library::Custom))
                categories.insert(category);
        };
        {
            const auto recent=recentRings();
            std::vector<int> order;
            for(const auto& key:recent)
                for(int i=0;i<ring_library::variantCount;++i)
                    if(ringKey(i)==key && std::find(order.begin(),order.end(),i)==order.end())order.push_back(i);
            const size_t recentCount=order.size();
            for(int i=0;i<ring_library::variantCount;++i)
                if(std::find(order.begin(),order.end(),i)==order.end())order.push_back(i);
            for(size_t row=0;row<order.size();++row) {
                const int i=order[row];
                const auto& v=ring_library::variants[i];
                const auto& p=ring_library::parents[v.parent];
                add(Library::Rings,std::string(p.title)+" · "+v.title+"   "+p.english+"   "+v.formula+"   "+p.category+"   "+p.aliases+(row<recentCount?"   最近使用":""),"",p.category,p.title,i,p.key,v.title);
            }
        }
        for (const auto& item:reagentEntries())
            add(item.inorganic?Library::Inorganic:Library::Organic,std::string(item.title)+"   "+item.alias+"   "+item.category,std::string("reagent_templates/")+item.file,item.category,item.title);
        const auto& bio=bioComponents();
        for (size_t i=0;i<bio.size();++i) {
            const auto& item=bio[i];
            const auto separator=item.title.find(" · ");
            const auto parent=item.title.substr(0,separator);
            const auto form=separator==std::string::npos?"标准结构":item.title.substr(separator+std::string(" · ").size());
            const auto family=item.file.substr(0,item.file.find('-'));
            const auto keywords=item.english+"   "+item.category+"   "+item.formula+"   "+item.file;
            add(Library::Bio,item.title+"   "+keywords,"bio_templates/"+item.file,item.category,parent,-1,family,form);
            const bool last=i+1==bio.size() || bio[i+1].file.substr(0,bio[i+1].file.find('-'))!=family;
            if (!last || item.category!="糖类" || parent.find("脱氧")!=std::string::npos) continue;
            const auto& sugars=stereo_projection::sugars();
            for(size_t n=0;n<sugars.size();++n) if(parent.find(sugars[n].title)!=std::string::npos) {
                add(Library::Bio,parent+" · 费歇尔投影   "+keywords,family,item.category,parent,-1,family,"费歇尔投影",
                    int(n),parent.find("-L-")!=std::string::npos);
                break;
            }
        }
        struct Search {
            Library origin;
            const std::vector<std::string>& keywords;
            std::string query;
            bool customLoaded=false;
            std::function<void()> loadCustom;
            GtkTreeModelFilter* filter=nullptr;
            std::string category;
            linux_templates::Gallery* gallery=nullptr;
        };
        Search stateSearch{library,familyKeywords,{},false,[&]{
            try {
                for(const auto& item:linux_templates::entries(linux_templates::directory()))
                    add(Library::Custom,item.name+"   "+item.group,item.path,item.group,item.name);
            } catch(const std::runtime_error& e) {report({ErrorCode::InvalidArgument,e.what()});}
        }};
        if(library==Library::Custom) {stateSearch.loadCustom();stateSearch.customLoaded=true;}
        auto* search=gtk_search_entry_new();
        g_signal_connect(search,"stop-search",G_CALLBACK(+[](GtkSearchEntry*,GtkDialog* dialog){
            gtk_dialog_response(dialog,GTK_RESPONSE_CANCEL);
        }),dialog);
        gtk_entry_set_placeholder_text(GTK_ENTRY(search),tr("搜索全部模板库：环、生物分子、试剂、自定义模板"));
        auto* filtered=gtk_tree_model_filter_new(GTK_TREE_MODEL(store),nullptr);
        stateSearch.filter=GTK_TREE_MODEL_FILTER(filtered);
        gtk_tree_model_filter_set_visible_func(stateSearch.filter,+[](GtkTreeModel* model,GtkTreeIter* it,gpointer data)->gboolean{
            auto& state=*static_cast<Search*>(data);
            int kind=0;gtk_tree_model_get(model,it,3,&kind,-1);
            if(state.query.empty()) {
                if (!state.category.empty()) {
                    gchar* category=nullptr;
                    gtk_tree_model_get(model,it,5,&category,-1);
                    const bool matches=category && state.category==category;
                    g_free(category);
                    if (!matches) return FALSE;
                }
                return state.origin==Library::All ? (kind==int(Library::Bio)||kind==int(Library::Organic)||kind==int(Library::Inorganic)) : kind==int(state.origin);
            }
            int family=0;gtk_tree_model_get(model,it,7,&family,-1);
            return state.keywords.at(family).find(state.query)!=std::string::npos;
        },&stateSearch,nullptr);
        g_signal_connect(search,"changed",G_CALLBACK(+[](GtkEditable* entry,Search* state){
            state->query=linux_templates::folded(gtk_entry_get_text(GTK_ENTRY(entry)));
            if(!state->query.empty() && !state->customLoaded) {
                state->customLoaded=true;
                state->loadCustom();
            }
            gtk_tree_model_filter_refilter(state->filter);
            if(state->gallery) state->gallery->refilter();
        }),&stateSearch);
        auto* content=GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
        gtk_container_set_border_width(GTK_CONTAINER(content),8);
        gtk_box_pack_start(content,search,FALSE,FALSE,2);
        auto* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
        gtk_box_pack_start(content,row,TRUE,TRUE,4);
        auto* categoryList=gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(categoryList),GTK_SELECTION_SINGLE);
        auto categoryRow=[&](const std::string& label){
            auto* text=gtk_label_new(tr(label.c_str()));
            gtk_label_set_xalign(GTK_LABEL(text),0);
            gtk_widget_set_margin_start(text,6);
            gtk_widget_set_margin_end(text,6);
            gtk_widget_set_margin_top(text,3);
            gtk_widget_set_margin_bottom(text,3);
            gtk_list_box_insert(GTK_LIST_BOX(categoryList),text,-1);
        };
        categoryRow("全部");
        std::vector<std::string> categoryNames(categories.begin(),categories.end());
        for(const auto& category:categoryNames) categoryRow(category);
        auto* sidebar=gtk_scrolled_window_new(nullptr,nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(sidebar),categoryList);
        gtk_box_pack_start(GTK_BOX(row),sidebar,FALSE,FALSE,0);
        auto* panel=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
        gtk_box_pack_start(GTK_BOX(row),panel,TRUE,TRUE,0);
        linux_templates::Gallery gallery(GTK_DIALOG(dialog), GTK_BOX(panel), filtered,
            [&](GtkTreeIter& iter){
                std::shared_ptr<Session> source;
                return loadLibraryEntry(filtered,iter,source).ok()?source:nullptr;
            });
        stateSearch.gallery=&gallery;
        struct Categories { Search& state; const std::vector<std::string>& names; GtkWidget* search; };
        Categories chooser{stateSearch,categoryNames,search};
        gtk_list_box_select_row(GTK_LIST_BOX(categoryList),gtk_list_box_get_row_at_index(GTK_LIST_BOX(categoryList),0));
        g_signal_connect(categoryList,"row-selected",G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, Categories* chooser){
            if(!row) return;
            const int index=gtk_list_box_row_get_index(row);
            chooser->state.category=index>0?chooser->names.at(index-1):std::string();
            if(!chooser->state.query.empty()) gtk_entry_set_text(GTK_ENTRY(chooser->search),"");
            else {
                gtk_tree_model_filter_refilter(chooser->state.filter);
                chooser->state.gallery->refilter();
            }
        }),&chooser);
        gtk_widget_show_all(dialog);
        gallery.refilter();
        gtk_widget_grab_focus(search);
        if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_OK){
            GtkTreeIter iter;
            if(gallery.selected(iter)){
                std::shared_ptr<Session> source;
                Error result=loadLibraryEntry(filtered,iter,source);
                int kind=0;gtk_tree_model_get(filtered,&iter,3,&kind,-1);
                const auto chosen=static_cast<Library>(kind);
                if(result.ok() && library==Library::All && chosen!=Library::Rings && chosen!=Library::Custom) result=insertStructure(source,"插入结构模板");
                else if(result.ok()) {
                    if(chosen!=Library::Rings && chosen!=Library::Custom) scaleBioTemplate(*source,session.doc.defaultBondLength());
                    source->fit();
                    if(chosen==Library::Rings) {
                        auto view=source->view();view.center=Vec2();source->setView(view);
                        gtk_tree_model_get(filtered,&iter,2,&pendingRingIndex,-1);
                        ringPlacementSource=source;
                    }
                    leavePerspectiveTool();selectionGesture.cancel();tableDrag.cancel();cancelShape();
                    cancelMechanismPlacement();session.cancel();down=panning=false;
                    pendingTemplate=std::move(source);
                    pendingInsertion="插入结构模板";pendingInserted="结构模板已插入";
                    session.status=tr("点击画布放置，Esc 取消");
                }
                report(result);
            }
        }
        gtk_widget_destroy(dialog);
        g_object_unref(filtered);
        g_object_unref(store);
        gtk_widget_grab_focus(canvas);
    }
    void customTemplateAction(linux_shortcuts::Action action) {
        cancelShape();
    using Action = linux_shortcuts::Action;
        auto* owner = GTK_WINDOW(window);
        try {
            if (action == Action::SaveTemplate) {
                if (linux_templates::saveSelection(owner, session)) session.status = "自定义模板已保存";
            } else if (action == Action::ImportTemplate) {
                const auto path = linux_templates::chooseFile(owner, true);
                if (!path.empty()) {
                    Document doc;
                    const auto input = linux_templates::read(path, doc);
                    const auto saved = linux_templates::save(doc, input.group, input.name, true);
                    session.status = std::string(tr("模板已导入")) + " · " + saved.name;
                }
            } else {
                std::string name;
                auto source = linux_templates::Picker(owner, true).run(name);
                if (source) {
                    const auto path = linux_templates::chooseFile(owner, false, name);
                    if (!path.empty()) {
                        linux_templates::write(source->doc, path);
                        session.status = "自定义模板已导出";
                    }
                }
            }
            report({});
        } catch (const std::runtime_error& error) {
            report({ErrorCode::InvalidArgument, error.what()});
        }
        gtk_widget_grab_focus(canvas);
    }
    bool write(const std::string& path){
        gchar* absolute = g_canonicalize_filename(path.c_str(), nullptr);
        std::string target = absolute;
        g_free(absolute);
        auto* app = gtk_window_get_application(GTK_WINDOW(window));
        for (auto* other : documentsOf(app)) {
            if (other && other != this && other->session.sourcePath == target) {
                report({ErrorCode::InvalidArgument, "该文件已在另一个窗口打开"});
                return false;
            }
        }
        auto result=studioio::saveDocumentFile(session.doc,target,true);
        if(result.ok()){
            session.sourcePath=target;
            session.dirty=false;
            session.status="已保存 "+target;
        }
        report(result);
        return result.ok();
    }
    enum class SaveMode { Current, SaveAs, ExportCdxml };

    bool save(SaveMode mode = SaveMode::Current) {
        if(mode == SaveMode::Current && !session.sourcePath.empty()){
            return write(session.sourcePath);
        }
        const bool cdxml = mode == SaveMode::ExportCdxml;
        auto* dialog = gtk_file_chooser_dialog_new(tr(cdxml ? "导出 CDXML…" : "另存为 CDXML、CDX 或 R62DOC"),GTK_WINDOW(window),GTK_FILE_CHOOSER_ACTION_SAVE,
                          tr("取消"),GTK_RESPONSE_CANCEL,tr("保存"),GTK_RESPONSE_ACCEPT,nullptr);
        gchar* name = session.sourcePath.empty() ? g_strdup((std::string(tr("分子结构")) + ".cdxml").c_str()) : g_path_get_basename(session.sourcePath.c_str());
        std::string suggested(name);
        g_free(name);
        if (cdxml) {
            suggested = std::filesystem::path(suggested).replace_extension(".cdxml").string();
            auto* filter = gtk_file_filter_new();
            gtk_file_filter_set_name(filter, "ChemDraw XML (*.cdxml)");
            gtk_file_filter_add_pattern(filter, "*.[cC][dD][xX][mM][lL]");
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        }
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), suggested.c_str());
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);
        auto* chooser = GTK_FILE_CHOOSER(dialog);
        auto* hint = gtk_label_new(cdxml ? "CDXML (*.cdxml)" : "CDXML (*.cdxml)   ·   CDX (*.cdx)   ·   R62DOC (*.r62doc)");
        gtk_widget_show(hint);
        gtk_file_chooser_set_extra_widget(chooser, hint);
        bool saved = false;
        while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            gchar* filename = gtk_file_chooser_get_filename(chooser);
            if (!filename) continue;
            std::string path(filename);
            gchar* basename = g_path_get_basename(filename);
            const std::string leaf(basename);
            g_free(basename);
            g_free(filename);
            const auto dot = leaf.find_last_of('.');
            if (dot == std::string::npos || dot == 0) {
                path += ".cdxml";
                if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
                    gchar* folder = g_path_get_dirname(path.c_str());
                    gtk_file_chooser_set_current_folder(chooser, folder);
                    g_free(folder);
                    gtk_file_chooser_set_current_name(chooser, (leaf + ".cdxml").c_str());
                    continue;
                }
            } else {
                const char* ext = leaf.c_str() + dot;
                if (cdxml && g_ascii_strcasecmp(ext, ".cdxml")) {
                    gtk_label_set_text(GTK_LABEL(hint), "ChemDraw XML (*.cdxml)");
                    continue;
                }
                if (g_ascii_strcasecmp(ext, ".cdxml") && g_ascii_strcasecmp(ext, ".cdx") &&
                    g_ascii_strcasecmp(ext, ".r62doc")) {
                    gtk_label_set_text(GTK_LABEL(hint), tr("请选择 .cdxml、.cdx 或 .r62doc 扩展名"));
                    continue;
                }
            }
            saved = write(path);
            break;
        }
        gtk_widget_destroy(dialog);
        gtk_widget_grab_focus(canvas);
        return saved;
    }

    void printDocument() {
        bool submitted = false;
        const auto* title = gtk_window_get_title(GTK_WINDOW(window));
        Error result;
        try {
            result = linux_print::print(GTK_WINDOW(window), session, title ? title : "MoleculeStudio", submitted);
        } catch (const std::exception& e) {
            result = {ErrorCode::IoError, e.what()};
        }
        gtk_widget_grab_focus(canvas);
        if (submitted && result.ok()) session.status = "已提交打印任务";
        if (submitted || !result.ok()) report(result);
    }

    void exportOffice() {
        auto* dialog=gtk_file_chooser_dialog_new(tr("导出可编辑 Office 对象"), GTK_WINDOW(window),
            GTK_FILE_CHOOSER_ACTION_SAVE,tr("取消"),GTK_RESPONSE_CANCEL,tr("导出"),GTK_RESPONSE_ACCEPT,nullptr);
        auto* chooser=GTK_FILE_CHOOSER(dialog);
        gtk_file_chooser_set_current_name(chooser,"MoleculeStudio.docx");
        gtk_file_chooser_set_do_overwrite_confirmation(chooser,TRUE);
        auto* filter=gtk_file_filter_new();
        gtk_file_filter_set_name(filter,"Word (*.docx) / PowerPoint (*.pptx)");
        gtk_file_filter_add_pattern(filter,"*.docx");
        gtk_file_filter_add_pattern(filter,"*.pptx");
        gtk_file_chooser_add_filter(chooser,filter);
        std::string path;
        if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){
            auto* filename=gtk_file_chooser_get_filename(chooser);
            if(filename)path=filename;
            g_free(filename);
        }
        gtk_widget_destroy(dialog);
        if(path.empty())return;
        GError* failure=nullptr;
        auto* dir=g_dir_make_tmp("moleculestudio-ole-XXXXXX",&failure);
        if(!dir){report({ErrorCode::IoError,failure->message});g_clear_error(&failure);return;}
        std::string folder(dir),source=folder+"/drawing.r62doc",svg=folder+"/preview.svg";
        g_free(dir);
        std::ostringstream bytes;
        auto embeddedDocument=session.doc;
        embeddedDocument.documentExtensions().set("studio.ole.fixed-preview","1");
        auto e=integration::saveSafeNative(embeddedDocument,bytes);
        const auto native=bytes.str();
        if(e.ok()&&!g_file_set_contents(source.c_str(),native.data(),native.size(),&failure)){
            e={ErrorCode::IoError,failure->message};g_clear_error(&failure);
        }
        std::string diagnostic;
        bool cancelled=false;
        if(e.ok()){
            const auto script=assets()+(g_file_test((assets()+"/office_ole.py").c_str(),G_FILE_TEST_IS_REGULAR)?"/office_ole.py":"/linux/office_ole.py");
            linux_worker::run(GTK_WINDOW(window),"导出可编辑 Office 对象",
                {"/usr/bin/python3",script,source,svg,path,"/proc/"+std::to_string(getpid())+"/exe"},
                "",diagnostic,120,false,&cancelled);
            if(!diagnostic.empty())e={ErrorCode::IoError,diagnostic};
        }
        std::remove(source.c_str());std::remove(svg.c_str());rmdir(folder.c_str());
        if(cancelled)return;
        if(e.ok()) session.status="已导出 "+path;
        report(e);
    }
    void exportPdf() {
        auto* dialog = gtk_file_chooser_dialog_new(tr("导出 PDF"), GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SAVE,
            tr("取消"), GTK_RESPONSE_CANCEL, tr("导出"), GTK_RESPONSE_ACCEPT, nullptr);
        auto* chooser = GTK_FILE_CHOOSER(dialog);
        std::string suggested = tr("分子结构");
        if (!session.sourcePath.empty()) {
            auto* name = g_filename_display_basename(session.sourcePath.c_str());
            suggested = name;
            g_free(name);
            const auto extension = suggested.rfind('.');
            if (extension != std::string::npos && extension != 0) suggested.erase(extension);
        }
        gtk_file_chooser_set_current_name(chooser, (suggested + ".pdf").c_str());
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
        auto* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "PDF");
        gtk_file_filter_add_mime_type(filter, "application/pdf");
        gtk_file_chooser_add_filter(chooser, filter);
        gtk_file_chooser_add_choice(chooser, "fit-drawing", tr("将完整绘图置于一页"), nullptr, nullptr);
        bool fitDrawing = false;
        std::string path;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            fitDrawing = g_strcmp0(gtk_file_chooser_get_choice(chooser, "fit-drawing"), "true") == 0;
            auto* target = gtk_file_chooser_get_filename(chooser);
            if (target) {
                path = target;
                g_free(target);
            }
        }
        gtk_widget_destroy(dialog);
        gtk_widget_grab_focus(canvas);
        if (path.empty()) return;
        if (path.size() < 4 || g_ascii_strcasecmp(path.c_str() + path.size() - 4, ".pdf") != 0) {
            path += ".pdf";
            if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
                auto* confirm = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                    GTK_BUTTONS_NONE, "%s", tr("替换现有的 PDF 文件？"));
                auto* name = g_filename_display_basename(path.c_str());
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(confirm), "%s", name);
                g_free(name);
                gtk_dialog_add_buttons(GTK_DIALOG(confirm), tr("取消"), GTK_RESPONSE_CANCEL,
                    tr("替换"), GTK_RESPONSE_ACCEPT, nullptr);
                const int response = gtk_dialog_run(GTK_DIALOG(confirm));
                gtk_widget_destroy(confirm);
                if (response != GTK_RESPONSE_ACCEPT) return;
            }
        }
        Error result;
        try {
            result = linux_print::exportPdf(session, path, fitDrawing);
        } catch (const std::exception& e) {
            result = {ErrorCode::IoError, e.what()};
        }
        if (result.ok()) session.status = "已导出 PDF";
        report(result);
    }
    bool canClose() {
        if (!session.dirty) return true;
        auto* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_NONE, "%s", tr("保存当前文档的更改？"));
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), tr("取消"), GTK_RESPONSE_CANCEL, tr("放弃"), GTK_RESPONSE_REJECT,
                               tr("保存"), GTK_RESPONSE_ACCEPT, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        const int action = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return action == GTK_RESPONSE_REJECT || (action == GTK_RESPONSE_ACCEPT && save());
    }
};

#include "document_tabs.hpp"
#include "nmr_page.hpp"

static void chooseDocuments(Window* w) {
    auto* dialog = gtk_file_chooser_dialog_new(tr("打开分子结构"), GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_OPEN,
                                             tr("取消"), GTK_RESPONSE_CANCEL, tr("打开"), GTK_RESPONSE_ACCEPT, nullptr);
    auto* chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_select_multiple(chooser, TRUE);
    GSList* files = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT ?
        gtk_file_chooser_get_filenames(chooser) : nullptr;
    gtk_widget_destroy(dialog);
    auto* app = gtk_window_get_application(GTK_WINDOW(w->window));
    for (auto* file = files; file; file = file->next)
        showDocument(app, static_cast<const char*>(file->data), {}, w->window);
    g_slist_free_full(files, g_free);
}

bool Window::activateShortcut(const linux_shortcuts::Shortcut& item) {
    finishCanvasText();
    cancelShape();
    using Action = linux_shortcuts::Action;
    using Hotkey = r62::interaction::HotkeyAction;
    pendingTemplate.reset();
    cancelMechanismPlacement();
    if (selectionGesture.pending()) {
        selectionGesture.cancel();
        gtk_widget_queue_draw(canvas);
    }
    if (tableDrag.pending()) {
        tableDrag.cancel();
        gtk_widget_queue_draw(canvas);
    }
    auto route = [&](Hotkey action) {
        session.router.dispatch(action, "GTK");
        report(Error());
    };
    switch (item.action) {
        case Action::New: showDocument(gtk_window_get_application(GTK_WINDOW(window)), nullptr, {}, window); break;
        case Action::Open: chooseDocuments(this); break;
        case Action::Close: closeDocument(*this); return true;
        case Action::Save: save(); break;
        case Action::SaveAs: save(SaveMode::SaveAs); break;
        case Action::ExportCdxml: save(SaveMode::ExportCdxml); break;
        case Action::Print: printDocument(); break;
        case Action::ExportPdf: exportPdf(); break;
        case Action::Undo: report(session.undo()); break;
        case Action::Redo: report(session.undo(true)); break;
        case Action::Repeat: route(Hotkey::RepeatLastAction); break;
        case Action::Copy: route(Hotkey::EditCopy); break;
        case Action::CopyPng: report(linux_canvas::copySelectionPng(session, gtk_widget_get_scale_factor(canvas))); break;
        case Action::Cut: route(Hotkey::EditCut); break;
        case Action::Paste:
            if (!pasteExternal()) route(Hotkey::EditPaste);
            break;
        case Action::SelectAll: route(Hotkey::EditSelectAll); break;
        case Action::Delete: {
            Error result;
            if (!selectionGesture.deleteMarker(session, result)) {
                result = session.submit(commands::deleteObjects(session.selected), "删除");
                if (result.ok()) session.setSelection({});
            }
            report(result);
            break;
        }
        case Action::EditLabel: {
            if (session.selected.size() != 1) return false;
            const auto ref = session.selected.front();
            if (ref.kind == ObjectKind::Atom) editLabel(ref.id);
            else if (ref.kind == ObjectKind::Text) editCaption(Vec2(), ref.id);
            else if (ref.kind == ObjectKind::Table) editTable(ref.id);
            else return false;
            break;
        }
        case Action::EditTable:
            if (session.selected.size() == 1 && session.selected.front().kind == ObjectKind::Table)
                editTable(session.selected.front().id);
            else report({ErrorCode::InvalidArgument, "请先在画布上选择一个表格"});
            break;
        case Action::InsertTable: {
            Error result;
            const auto id = linux_table::insert(GTK_WINDOW(window), session, item.argument != 0, result);
            if (id != kInvalidObjectId) {
                session.activateCommand("chooseSelectionTool_Marquee");
                session.status = item.label;
                editTable(id);
            } else report(result);
            gtk_widget_grab_focus(canvas);
            break;
        }
        case Action::Fit: route(Hotkey::ZoomFit); break;
        case Action::FitPage: fitPage(); break;
        case Action::ActualSize: route(Hotkey::ZoomActualSize); break;
        case Action::ZoomIn: route(Hotkey::ZoomIn); break;
        case Action::ZoomOut: route(Hotkey::ZoomOut); break;
        case Action::FixedLength: gtk_check_menu_item_set_active(fixedLength, !gtk_check_menu_item_get_active(fixedLength)); break;
        case Action::FixedAngles: gtk_check_menu_item_set_active(fixedAngles, !gtk_check_menu_item_get_active(fixedAngles)); break;
        case Action::NudgeHorizontal:
        case Action::NudgeVertical: {
            if (session.selected.empty()) return false;
            const auto delta = item.action == Action::NudgeHorizontal
                ? Vec2(item.argument, 0) : Vec2(0, item.argument);
            report(session.move(session.selected, delta));
            break;
        }
        case Action::Group: arrange("group"); break;
        case Action::Ungroup: arrange("ungroup"); break;
        case Action::Align: arrange("align", item.argument); break;
        case Action::Distribute: arrange("distribute", item.argument); break;
        case Action::Merge: report(studio::transformSelection(session, "merge", 0)); break;
        case Action::Rotate: report(studio::transformSelection(session, "rotate", 0)); break;
        case Action::RotateStep:
            if (session.selected.empty()) return false;
            transformSelected(false, item.argument);
            break;
        case Action::PreciseTransform: preciseTransform(item.argument != 0); break;
        case Action::Flip: report(studio::transformSelection(session, "flip", item.argument)); break;
        case Action::Stack: report(studio::stackSelection(session, item.argument != 0)); break;
        case Action::Bold: formatText(kBold); break;
        case Action::Italic: formatText(kItalic); break;
        case Action::Underline: formatText(kUnderline); break;
        case Action::TextScript: formatText(item.argument); break;
        case Action::TextTypography: changeTypography(item.argument); break;
        case Action::TextAlign: formatText(0, item.argument); break;
        case Action::Formula: properties(); break;
        case Action::Calculator: linux_calculator::show(GTK_WINDOW(window), session); report({}); break;
        case Action::Skin: {
            const auto message = linux_skin::configure(GTK_WINDOW(window));
            report(message.empty() ? Error() : Error(ErrorCode::IoError, message));
            break;
        }
        case Action::Conformer: conformer(); break;
        case Action::Format: convertStructure(); break;
        case Action::CopyChemicalText: chemicalClipboard(true,item.argument); break;
        case Action::PasteChemicalText: chemicalClipboard(false,item.argument); break;
        case Action::Templates: templateLibrary(); break;
        case Action::OpenTemplate: templateLibrary(Library::Custom); break;
        case Action::SaveTemplate:
        case Action::ImportTemplate:
        case Action::ExportTemplate: customTemplateAction(item.action); break;
        case Action::NameToStructure: nameToStructure(); break;
        case Action::StructureToName: structureToName(); break;
        case Action::Cleanup: cleanStructure(); break;
        case Action::CleanReaction: cleanStructure(true); break;
        case Action::Expand: expandGroups(); break;
        case Action::Collapse: collapseGroups(); break;
        case Action::CustomGroups: manageCustomGroups(); break;
        case Action::Stereo: annotateStereo(); break;
        case Action::Nmr: predictSpectrum(); break;
        case Action::Recognize: recognizeImage(); break;
        case Action::Assistant: showAssistant(); break;
        case Action::MechanismStep: generateMechanismStep(item.argument); break;
        case Action::Catalytic: {
            const auto mode = item.argument;
            auto* parent = GTK_WINDOW(window);
            Error result;
            if (mode == 4) result = linux_catalytic::metal(parent, session);
            else if (mode == 2 || mode == 3)
                result = studio::catalytic::branch(session, studio::catalytic::selected(session), mode == 2);
            else if (mode != 5 || catalyticOutline.valid())
                result = linux_catalytic::edit(parent, session, mode != 1, mode == 5 ? catalyticOutline : ObjectRef());
            gtk_widget_grab_focus(canvas);
            report(result);
            break;
        }
        case Action::MechanismReagent:
            report(linux_mechanism::insertReagent(session, static_cast<linux_mechanism::Reagent>(item.argument)));
            break;
        case Action::MechanismState: {
            const bool circled = std::abs(item.argument) == 3;
            const auto state = circled ? item.argument / 3 : item.argument;
            const auto result = linux_mechanism::setElectronState(session, state, circled);
            if (result.ok()) session.activateCommand("chooseSelectionTool_Marquee");
            report(result);
            break;
        }
        case Action::MechanismPlacement:
            session.cancel();
            leavePerspectiveTool();
            mechanismPlacement = static_cast<linux_mechanism::Placement>(item.argument);
            session.status = mechanismPlacement == linux_mechanism::Placement::VirtualBond ?
                "在两个原子之间拖动，画出待成键 · Esc 退出" : "点击画布放置标注 · Esc 取消";
            gtk_widget_grab_focus(canvas);
            updateCanvasCursor(canvasPointer);
            report({});
            break;
        case Action::TransitionState: {
            auto bounds = studioio::selectionBounds(session);
            if (session.selected.empty() || bounds.empty())
                bounds = Box(session.view().center - Vec2(40, 26), session.view().center + Vec2(40, 26));
            else {
                bounds.min = bounds.min - Vec2(10, 10);
                bounds.max = bounds.max + Vec2(10, 10);
            }
            auto created = std::make_shared<ObjectId>(kInvalidObjectId);
            const auto result = session.submit(Command("插入过渡态中括号", kInvalidateGeometry | kInvalidateAppearance,
                [bounds, created](CommandContext& context) { return transitionBracket(context.document, bounds, created.get()); }),
                "插入过渡态中括号");
            if (result.ok()) {
                session.activateCommand("chooseSelectionTool_Marquee");
                session.setSelection({ObjectRef(ObjectKind::Bracket, *created)});
            }
            report(result);
            break;
        }
        case Action::ReactiveCenter: {
            const auto result = studio::reactive_centers::insert(session, item.argument != 0);
            if (result.ok()) session.activateCommand("chooseSelectionTool_Marquee");
            report(result);
            break;
        }
        case Action::AttachBaseHydrogen:
            report(linux_mechanism::attachHydrogen(session, session.selected.size() == 1 ? session.selected.front() : ObjectRef()));
            break;
        case Action::Language: chooseLanguage(); break;
        case Action::Settings: chooseShortcuts(); break;
        case Action::ExportSkill: exportDrawingSkill(); break;
        case Action::CheckUpdates: {
            const auto installed = assets() + "/update.py";
            const auto worker = g_file_test(installed.c_str(), G_FILE_TEST_IS_REGULAR) ? installed : assets() + "/linux/update.py";
            const auto result = linux_update::run(GTK_WINDOW(window), worker, session.status);
            gtk_widget_grab_focus(canvas);
            report(result);
            break;
        }
        case Action::DrawingStyles: chooseDrawingStyles(); break;
        case Action::GraphicFormat: {
            auto* parent = GTK_WINDOW(window);
            Error result;
            if (item.argument == 0)
                result = linux_graphic_format::apply(session,
                    linux_graphic_format::width(parent, session.graphicsWidth, session.doc.defaultLineWidth()), {});
            else if (item.argument == 1)
                result = linux_graphic_format::apply(session, {},
                    linux_graphic_format::color(parent, "绘图颜色", session.graphicsColor));
            else
                result = linux_graphic_format::fill(session, linux_graphic_format::color(parent, "图案填充",
                    session.graphicsFill ? session.graphicsFill : 0xe6f2efff, true));
            gtk_widget_grab_focus(canvas);
            report(result);
            break;
        }
        case Action::PageSetup: choosePageSetup(); break;
        case Action::Perspective:
            session.cancel();
            session.activateCommand("chooseSelectionTool_Marquee");
            perspectiveTool = true;
            session.status = "结构透视：选中结构后拖动 · Shift 限制单轴 · Esc 退出";
            gtk_widget_grab_focus(canvas);
            report({});
            break;
        case Action::Tool:
            leavePerspectiveTool();
            if (!session.activateCommand(item.id == "arrowTool" ? arrowStyle->sourceCommand : item.tool)) return false;
            gtk_widget_grab_focus(canvas);
            report(Error());
            break;
        case Action::Hotspot: case Action::EditHoveredAtom: case Action::ClearHoveredLabel:
            return false;
    }
    return true;
}

bool Window::activateHotspotShortcut(const keymap::Hotkey& key, const interaction::Hotspot& spot) {
    using namespace studio::hotspot_actions;
    const std::string op = key.command;
    const std::string value = key.value;
    const auto ref = spot.object;
    const auto id = ref.id;
    if (op != "SPROUT" || value != "1") rapidSprout.clear();
    if (op == "CHARGE") {
        const std::vector<ObjectRef> targets = ref.valid() ? std::vector<ObjectRef>{ref} : session.selected;
        std::set<ObjectId> atoms;
        for (const auto target : targets)
            if (target.kind == ObjectKind::Atom && session.doc.atoms().alive(target.id)) atoms.insert(target.id);
        if (atoms.empty()) return false;
        const int delta = value == "+" ? 1 : -1;
        report(session.submit(Command("原子电荷", kInvalidateStructure | kInvalidateAppearance | kInvalidateSemantics,
            [atoms, delta](CommandContext& ctx) {
                for (const auto atom : atoms) {
                    const auto current = ctx.document.atoms().alive(atom)->semantics.charge;
                    const auto result = chemistry::setChargeCommand({atom}, current + delta).execute(ctx);
                    if (!result.ok()) return result;
                }
                return CommandResult::success();
            }), "原子电荷"));
    } else if (op == "SELECT") {
        session.selected = {ref};
        report(Error());
    } else if (op == "LABELTEXT") {
        const auto* atom = session.doc.atoms().alive(id);
        if (!atom) return false;
        if (value == "O" || value == "N") {
            studio::atom_hydrogen::prefer(session, session.status, id, value == "O" ? 8 : 7);
            report(Error());
        } else if (value == "H" && studio::atom_hydrogen::cycle(session, session.status, id)) {
            report(Error());
        } else if (value == "R" && session.doc.incidentBonds(id).size() == 1) {
            report(session.submit(markTerminalR(id), "末端 R 基团"));
        } else {
            report(session.submit(label(id, value, *atom), "原子快捷键"));
        }
    } else if (op == "SPROUT") {
        if (value == "1") {
            const uint64_t now = uint64_t(g_get_monotonic_time() / 1000);
            std::vector<ObjectId> recent;
            if (rapidSprout.continues(session.doc, id, now, session.serial, session.history.cursor()))
                recent = rapidSprout.tips;
            const auto added = std::make_shared<studio::rapid_sprout::Addition>();
            const auto result = session.submit(studio::rapid_sprout::command(id, recent, added), "添加结构片段");
            if (result.ok()) {
                recent.push_back(added->atom);
                rapidSprout.record(session.doc, id, now, session.serial, session.history.cursor(), std::move(recent));
                if (added->arranged) session.status = "已按连续添键整理支链角度";
            } else rapidSprout.clear();
            report(result);
        } else report(session.submit(sprout(id, std::stoi(value)), "添加结构片段"));
    } else if (op == "ATOMNUMBER" || op == "ATTACHMENTPOINT" || op == "BONDPOSITION") {
        const auto title = op == "ATOMNUMBER" ? "原子编号" : op == "ATTACHMENTPOINT" ? "连接点" : "堆叠顺序";
        int64_t z = 0;
        if (op == "BONDPOSITION") {
            session.selected = {ref};
            const auto& doc = static_cast<const Document&>(session.doc);
            for (auto object : session.all())
                if (const auto* layer = doc.extensions(object).findValue("studio.z"))
                    z = std::max<int64_t>(z, g_ascii_strtoll(layer->c_str(), nullptr, 10));
            // Imported stacking metadata can contain arbitrary integers.
            z = std::min<int64_t>(z, std::numeric_limits<int>::max() - 100) + 100;
        }
        report(session.submit(Command(title, kInvalidateStructure | kInvalidateAppearance,
            [op, ref, z](CommandContext& ctx) {
                auto& doc = ctx.document;
                if (op == "ATTACHMENTPOINT") {
                    auto sem = doc.atoms().alive(ref.id)->semantics;
                    sem.nodeType = uint8_t(NodeType::ExternalConnection);
                    sem.showLabel = true;
                    auto result = doc.setAtomSemantics(ref.id, sem);
                    if (result.ok()) result = doc.setAtomLabel(ref.id, "*");
                    if (!result.ok()) return CommandResult::fail(result.code, result.message);
                } else {
                    auto& bag = doc.extensions(ref);
                    if (op == "BONDPOSITION") bag.set("studio.z", std::to_string(z));
                    else if (bag.findValue("studio.atom.number")) bag.erase("studio.atom.number");
                    else bag.set("studio.atom.number", std::to_string(ref.id + 1));
                }
                return CommandResult::success();
            }), title));
    } else if (op == "BONDORDER") {
        const auto order = value == "1" ? BondOrder::Single : value == "2" ? BondOrder::Double : BondOrder::Triple;
        report(session.submit(chemistry::setBondOrderWithRefreshCommand(id, order), "化学键级"));
    } else if (op == "BONDDOUBLEPOSITION") {
        const auto side = value == "LEFT" ? bonds::DoubleSide::Left :
            value == "RIGHT" ? bonds::DoubleSide::Right : bonds::DoubleSide::Centered;
        report(session.submit(bonds::setBondSideCommand({id}, side), "双键位置"));
    } else if (op == "BONDDISPLAY") {
        report(session.submit(bondDisplay(id, value), "键显示"));
    } else if (op == "FUSERING") {
        report(fuse(session, id, spot.point, std::stoi(value)));
    } else return false;
    return true;
}

static bool tlcSpotAt(Session& session, Vec2 pixel, ObjectId& plate, uint32_t& spot) {
    using namespace experimental;
    const auto point = session.view().toDocument(pixel);
    const auto ref = session.pick(point);
    if (!isFigureObject(session.doc, ref)) return false;
    PlateFigure figure;
    PlateFrame frame;
    if (!readFigureAndFrame(session.doc, ref.id, &figure, &frame, nullptr).ok() ||
        figure.kind != FigureKind::TlcPlate) return false;
    const auto element = pickElement(figure, frame, point, session.view().deviceRadiusToDocument(8));
    if (element.kind != ElementKind::Spot) return false;
    plate = ref.id;
    spot = element.id;
    return true;
}

static bool deleteHoveredTlcSpot(Window& window) {
    ObjectId plate;
    uint32_t spot;
    if (!tlcSpotAt(window.session, window.canvasPointer, plate, spot)) return false;
    window.report(window.session.submit(experimental::removeSpotCommand(plate, spot, nullptr), "删除薄层板斑点"));
    return true;
}

enum TlcSpotAction { CopyLeft, CopyRight, CopyAllLanes, DeleteSpot, EditSpotRf };

bool Window::showTlcSpotMenu(Vec2 pixel, GdkEvent* event) {
    if (!tlcSpotAt(session, pixel, tlcMenuPlate, tlcMenuSpot)) return false;
    tlcMenuSerial = session.serial;
    if (!tlcSpotMenu) {
        tlcSpotMenu = gtk_menu_new();
        gtk_menu_attach_to_widget(GTK_MENU(tlcSpotMenu), canvas, nullptr);
        const char* labels[] = {"复制到左侧样品列", "复制到右侧样品列", "复制到全部样品列", "删除斑点", "编辑斑点 Rf…"};
        for (int i = CopyLeft; i <= EditSpotRf; ++i) {
            auto* item = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""), labels[i]);
            g_object_set_data(G_OBJECT(item), "spot-action", GINT_TO_POINTER(i));
            g_signal_connect(item, "activate", G_CALLBACK(+[](GtkWidget* item, Window* w) {
                w->activateTlcSpot(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "spot-action")));
            }), this);
            gtk_menu_shell_append(GTK_MENU_SHELL(tlcSpotMenu), item);
        }
        gtk_widget_show_all(tlcSpotMenu);
    }
    gtk_widget_grab_focus(canvas);
    gtk_menu_popup_at_pointer(GTK_MENU(tlcSpotMenu), event);
    return true;
}

void Window::activateTlcSpot(int action) {
    using namespace experimental;
    if (session.serial != tlcMenuSerial) return;
    if (action == DeleteSpot) {
        report(session.submit(removeSpotCommand(tlcMenuPlate, tlcMenuSpot, nullptr), "删除薄层板斑点"));
        return;
    }
    PlateFigure figure;
    PlateFrame frame;
    if (!readFigureAndFrame(session.doc, tlcMenuPlate, &figure, &frame, nullptr).ok()) return;
    auto* spot = mutableSpot(&figure, tlcMenuSpot);
    if (!spot) return;
    if (action == EditSpotRf) {
        auto* dialog = gtk_dialog_new_with_buttons(tr("编辑斑点 Rf"), GTK_WINDOW(window), GTK_DIALOG_MODAL,
            tr("取消"), GTK_RESPONSE_CANCEL, tr("应用"), GTK_RESPONSE_ACCEPT, nullptr);
        auto* spin = gtk_spin_button_new_with_range(0., 1., .01);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), studio_tlc::rf(figure, *spot));
        gtk_entry_set_activates_default(GTK_ENTRY(spin), TRUE);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), spin);
        gtk_widget_show_all(dialog);
        const auto response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
        const auto value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
        gtk_widget_destroy(dialog);
        if (response != GTK_RESPONSE_ACCEPT || session.serial != tlcMenuSerial) return;
        studio_tlc::setRf(figure, *spot, value);
    } else {
        const auto source = *spot;
        const auto& lanes = figure.lanes;
        auto lane = std::find_if(lanes.begin(), lanes.end(), [&](const Lane& item) { return item.id == source.laneId; });
        if (lane == lanes.end()) return;
        const auto count = figure.spots.size();
        for (auto dest = lanes.begin(); dest != lanes.end(); ++dest) {
            auto distance = dest - lane;
            if (!distance || (action == CopyLeft && distance != -1) || (action == CopyRight && distance != 1)) continue;
            if (studio_tlc::spotsIn(figure, dest->id).size() >= 30) continue;
            auto copy = source;
            copy.id = allocateElementId(&figure);
            copy.laneId = dest->id;
            copy.u = dest->u;
            figure.spots.push_back(copy);
        }
        if (count == figure.spots.size()) return;
    }
    const char* label = action == EditSpotRf ? "编辑斑点 Rf" : "复制薄层板斑点";
    report(session.submit(Command(label, kInvalidateStructure | kInvalidateGeometry,
        [id = tlcMenuPlate, figure = std::move(figure)](CommandContext& c) {
            auto e = writeFigure(c.document, id, figure);
            return e.ok() ? CommandResult::success() : CommandResult::fail(e.code, e.message);
        }), label));
}

void Window::tlcSettings(ObjectId id, uint32_t selectedSpot) {
    using namespace experimental;
    const auto revision = session.serial;
    auto& prefs = studio_tlc::preferences();
    PlateFrame frame({}, {70,0}, {0,100});
    linux_tlc::Dialog dlg{studio_tlc::create(frame),frame,prefs.diameter,prefs.showRf};
    if (id != kInvalidObjectId) {
        auto error = readFigureAndFrame(session.doc,id,&dlg.figure,&dlg.frame,nullptr);
        if (!error.ok()) { report(error); return; }
        if (dlg.figure.kind != FigureKind::TlcPlate) return;
        const auto& ext = static_cast<const Document&>(session.doc).extensions(figureRef(id));
        auto flag = ext.findValue("studio.tlc.showRf");
        dlg.showRf = flag && *flag=="1";
        if(auto size=ext.findValue("studio.tlc.spotDiameter")) linux_config::parseNumber(size->c_str(),dlg.diameter);
        if(!dlg.figure.spots.empty())dlg.diameter=std::clamp(2*dlg.figure.spots.front().radiusU*dlg.frame.halfWidth(),1.,16.);
        if(auto* s=findSpot(dlg.figure,selectedSpot)){
            for(size_t i=0;i<dlg.figure.lanes.size();++i)if(dlg.figure.lanes[i].id==s->laneId)dlg.laneIndex=i;
            auto ids=studio_tlc::spotsIn(dlg.figure,s->laneId);
            dlg.spotIndex=size_t(std::find(ids.begin(),ids.end(),s->id)-ids.begin());
        }
    }
    if (!dlg.run(GTK_WINDOW(window)) || session.serial!=revision) return;
    if (id == kInvalidObjectId) {
        std::string message;
        if(!linux_config::Preferences().replaceGroup("TLC",{{"Template",encodeFigure(dlg.figure)},
            {"Diameter",std::to_string(dlg.diameter)},{"ShowRf",dlg.showRf?"true":"false"}},&message)){
            report({ErrorCode::IoError,message});return;
        }
        prefs.customTemplate=true;prefs.plateTemplate=std::move(dlg.figure);
        prefs.diameter=dlg.diameter;prefs.showRf=dlg.showRf;
        for(size_t i=0;i<session.entries.size();++i)if(session.entries[i].command=="chooseTLCPlateTool"){
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(tools),std::to_string(i).c_str());break;
        }
        session.status="已保存薄层板点样模板";report({});return;
    }
    report(session.submit(Command("薄层板点样设置",kInvalidateStructure|kInvalidateGeometry|kInvalidateAppearance,
        [id,figure=std::move(dlg.figure),diameter=dlg.diameter,show=dlg.showRf](CommandContext& c){
            auto e=writeFigure(c.document,id,figure);
            if(!e.ok())return CommandResult::fail(e.code,e.message);
            auto& ext=c.document.extensions(figureRef(id));
            ext.set("studio.tlc.spotDiameter",std::to_string(diameter));ext.set("studio.tlc.showRf",show?"1":"0");
            return CommandResult::success();
        }),"薄层板点样设置"));
}

bool Window::handleKey(const GdkEventKey& event) {
    using namespace linux_shortcuts;
    const auto value = chord(event.keyval, event.state);
    const bool canvasFocused = gtk_window_get_focus(GTK_WINDOW(window)) == canvas;
    const auto* canvasTool = canvasFocused ? current().match(value, Scope::Canvas) : nullptr;
    if (event.keyval == GDK_KEY_Escape || (canvasTool && canvasTool->id == "marqueeTool")) {
        cancelShape();
        finishTlcClick(false);
        down = panning = false;
        cancelMechanismPlacement();
        if (pendingTemplate) {
            pendingTemplate.reset();
            session.status = "已取消模板放置";
        }
        tableDrag.cancel();
        rapidSprout.clear();
        selectionGesture.cancel();
        leavePerspectiveTool();
        session.cancel();
        session.selected.clear();
        session.activateCommand("chooseSelectionTool_Marquee");
        updateCanvasCursor(canvasPointer);
        report(Error());
        return true;
    }
    if (event.is_modifier) return false;
    if (canvasFocused && pointerOnCanvas && !down && !panning && !tableDrag.pending() && !selectionGesture.pending())
        hoverCanvas(canvasPointer, event.state);
    const auto spot = canvasFocused && pointerOnCanvas && !down && !panning && !tableDrag.pending() && !selectionGesture.pending()
        ? session.input.hotspot() : interaction::Hotspot();
    if (const auto* global = current().match(value, Scope::Window)) {
        rapidSprout.clear();
        if (global->action == Action::Delete && canvasFocused && pointerOnCanvas && !down && !panning &&
            !tableDrag.pending() && !selectionGesture.pending() && deleteHoveredTlcSpot(*this)) return true;
        if (global->action == Action::Delete && session.selected.empty()) {
            Error result;
            if (selectionGesture.deleteMarker(session, result)) report(result);
            else if (canvasFocused && pointerOnCanvas && !down && !panning && !tableDrag.pending() && !selectionGesture.pending()) {
                const auto marker = studio::electron_marker::at(session.doc, session.view().toDocument(canvasPointer),
                    session.view().deviceRadiusToDocument(3), true);
                if (marker.atom != kInvalidObjectId) report(session.submit(studio::electron_marker::remove(marker), "删除电子标记"));
                else if (spot.valid()) report(session.submit(commands::deleteObjects({spot.object}), "删除"));
            }
            return true;
        }
        return activateShortcut(*global);
    }
    if (!canvasFocused || down || panning) return false;
    if (spot.valid()) {
        const auto scope = spot.kind == interaction::HotspotKind::Atom ? Scope::Atom : Scope::Bond;
        if (const auto* item = current().match(value, scope)) {
            if (item->action == Action::Hotspot) return activateHotspotShortcut(keymap::hotkeys[item->argument], spot);
            rapidSprout.clear();
            if (item->action == Action::EditHoveredAtom) editLabel(spot.object.id);
            else report(session.submit(studio::hotspot_actions::clearLabel(spot.object.id), "清除原子标签"));
            return true;
        }
    }
    if (const auto* item = current().match(value, Scope::Atom)) {
        if (item->action == Action::Hotspot) {
            const auto& key = keymap::hotkeys[item->argument];
            if (std::string(key.command) == "CHARGE" && activateHotspotShortcut(key, {})) return true;
        }
    }
    if (const auto* tool = canvasTool) {
        rapidSprout.clear();
        if (tool->id == "textTool" && spot.valid()) {
            const auto point = session.view().toDocument(canvasPointer);
            auto id = spot.object.id;
            if (spot.kind == interaction::HotspotKind::Bond) {
                const auto* bond = session.doc.bonds().alive(id);
                const auto a = session.doc.atoms().alive(bond->beginAtom)->position;
                const auto b = session.doc.atoms().alive(bond->endAtom)->position;
                id = length(point - a) <= length(point - b) ? bond->beginAtom : bond->endAtom;
            }
            activateShortcut(*tool);
            editLabel(id);
            return true;
        }
        return activateShortcut(*tool);
    }
    // A custom text-format binding can also format the canvas selection when
    // the canvas has no action assigned to that key.
    if (const auto* text = current().match(value, Scope::Text)) return activateShortcut(*text);
    const auto character = gdk_keyval_to_unicode(event.keyval);
    if (projectionHover.found() && !(event.state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK)) &&
            g_unichar_isgraph(character)) {
        gchar typed[7]{};
        g_unichar_to_utf8(character, typed);
        return editProjectionEndpoint(session.view().toDocument(canvasPointer), typed);
    }
    return false;
}

#include "ribbon.hpp"

static gboolean draw(GtkWidget* widget,cairo_t* cr,Window* w) {
    w->syncTypography();
    auto& s=w->session;
    auto& view=s.renderView;
    s.devicePixelsPerViewPixel=gtk_widget_get_scale_factor(widget);
    view.widthPx=gtk_widget_get_allocated_width(widget);
    view.heightPx=gtk_widget_get_allocated_height(widget);
    auto transform = s.view();
    const Vec2 center(view.widthPx * .5, view.heightPx * .5);
    if (center != transform.viewportCenter) {
        transform.viewportCenter = center;
        s.setView(transform);
        if (w->pointerOnCanvas && !w->down && !w->panning && !w->tableDrag.pending() && !w->selectionGesture.pending())
            w->hoverCanvas(w->canvasPointer, 0);
    }
    if (w->fitOnFirstDraw && view.widthPx > 40 && view.heightPx > 40) {
        w->fitOnFirstDraw = false;
        s.fit();
    }
    w->updateDocumentStats();
    w->updateScrollbars();
    if (s.projectedMoveActive() || s.partialMoveActive()) {
        // Move the structure over stationary paper. Keep overlays in screen coordinates.
        render::Scene paper;
        paper.add(s.gpuScene.primitives()[0]);
        paper.add(s.gpuScene.primitives()[1]);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        linux_cairo::draw(cr, paper, view, s.measurer.get());
        auto shifted = view;
        shifted.transform.center = shifted.transform.center - s.projectedMoveOffset();
        const int factor = gtk_widget_get_scale_factor(widget);
        if (w->moveMask.size() != s.gpuScene.size()) {
            w->moveMask.assign(s.gpuScene.size(), 1);
            w->moveMask[0] = w->moveMask[1] = 0;
        }
        const linux_canvas::PanCache::ProjectedSelection projected{s.gpuScene, view,
            w->frameBuffer.coversDrawing(s.gpuScene, view, s.serial, factor) ?
                w->contentCache.current(s.gpuScene, view, s.serial) : nullptr, w->moveMask, &w->contentCache, s.serial};
        if (s.partialMoveActive())
            w->partialMoveLayers.draw(cr,s,view,factor,projected.batch,w->contentCache);
        else if (!w->moveCache.draw(cr, s.scene, shifted, s.measurer.get(), s.serial, factor, &projected))
            linux_cairo::draw(cr, s.scene, shifted, s.measurer.get());
        const auto overlay = s.overlay();
        linux_cairo::draw(cr, overlay.scene(), view, s.measurer.get());
        linux_page::canvasGrid(cr, studioio::Page::get(s.doc), s.view());
        return TRUE;
    }
    w->moveCache.clear();
    if (s.selected.size() < 200) { w->partialMoveLayers.clear(); w->warmedSelection.clear(); }
    auto* transformed = w->selectionGesture.transformScene(s);
    auto* geometryPreview = transformed ? nullptr : w->selectionGesture.preview(s);
    const bool hideSelection = transformed || (geometryPreview && w->selectionGesture.transformingSelection());
    auto overlay=hideSelection ? render::OverlayScene() : geometryPreview ? geometryPreview->overlay() : s.overlay();
    if (s.selectionDragActive()) {
        overlay = render::OverlayScene();
        studioio::selectionOverlay(s,overlay);
    }
    w->selectionGesture.drawMarker(s, overlay);
    if (!w->down && !w->panning && !w->pendingTemplate && !w->tableDrag.pending() && !w->selectionGesture.pending() &&
            w->projectionHover.found()) studio_projection::highlightEndpoint(overlay, s.view(), w->projectionHover.capture);
    const auto* preview = w->tableDrag.preview(s);
    const auto& display = transformed ? *transformed : geometryPreview ? geometryPreview->displayScene() : s.displayScene();
    w->panCache.wheel = w->wheelIdle && !w->down;
    const bool interactiveView = w->panning || w->draggingScrollbar || w->panCache.wheel;
    if (!interactiveView) w->panCache.clear();
    auto e=w->frameBuffer.paintFrame(cr, preview ? *preview : display, preview ? nullptr : &overlay.scene(),
        view, s.measurer.get(), !preview && &display==&s.gpuScene ? &w->contentCache : nullptr, s.serial,
        interactiveView && !preview && &display==&s.gpuScene ? &w->panCache : nullptr, gtk_widget_get_scale_factor(widget),
        transformed || geometryPreview || preview || w->down || w->selectionGesture.copySource(),
        preview || hideSelection, preview ? &w->tableDrag.movingPrimitives() :
        transformed && transformed != &s.gpuScene ? &w->selectionGesture.movingPrimitives() : nullptr);
    if (!e.ok()){w->showStatus(e.message,true);return FALSE;}
    if (s.selectionDragActive() && s.selectionDragPoints().size()>1) {
        const auto& points=s.selectionDragPoints();
        const auto start=s.view().toDevice(points.front());
        cairo_save(cr);
        if (s.entries[s.activeIndex].command.find("Lasso")!=std::string::npos) {
            cairo_move_to(cr,start.x,start.y);
            for (auto p:points) {const auto q=s.view().toDevice(p); cairo_line_to(cr,q.x,q.y);}
            cairo_close_path(cr);
        } else {
            const auto end=s.view().toDevice(points.back());
            cairo_rectangle(cr,start.x,start.y,end.x-start.x,end.y-start.y);
        }
        cairo_set_source_rgba(cr,.22,.54,.82,.06); cairo_fill_preserve(cr);
        const double dash[]={3.,3.}; cairo_set_dash(cr,dash,2,0);
        cairo_set_source_rgba(cr,.22,.49,.73,.86); cairo_set_line_width(cr,1.); cairo_stroke(cr);
        cairo_restore(cr);
    }
    const int scale = gtk_widget_get_scale_factor(widget);
    if (!w->down && !interactiveView && &display == &s.gpuScene && !preview &&
        s.scene.size() >= 2000 && s.selected.size() >= 200 &&
        (w->warmedSerial != s.serial || w->warmedView != s.viewSerial ||
         w->warmedFactor != scale || w->warmedSelection != s.selected)) {
        if (w->selectionIdle) g_source_remove(w->selectionIdle);
        w->warmedSelection = s.selected;
        w->warmedView = s.viewSerial;
        w->warmedSerial = s.serial;
        w->warmedFactor = scale;
        w->planSelectionLayers = true;
        w->selectionIdle = g_idle_add(+[](gpointer data) -> gboolean {
            auto* w = static_cast<Window*>(data);
            auto& s = w->session;
            if (!w->down && !w->panning && gtk_widget_get_mapped(w->canvas) &&
                s.serial == w->warmedSerial && s.viewSerial == w->warmedView &&
                gtk_widget_get_scale_factor(w->canvas) == w->warmedFactor &&
                s.selected == w->warmedSelection) {
                if (w->planSelectionLayers) {
                    w->planSelectionLayers = false;
                    if (w->partialMoveLayers.prepareSelected(s,w->warmedFactor)) return G_SOURCE_CONTINUE;
                } else {
                    const auto* batch = w->frameBuffer.coversDrawing(s.gpuScene,s.renderView,s.serial,w->warmedFactor)
                        ? w->contentCache.current(s.gpuScene,s.renderView,s.serial) : nullptr;
                    if (w->partialMoveLayers.warmNext(s,w->warmedFactor,batch,w->contentCache)) return G_SOURCE_CONTINUE;
                }
            }
            w->selectionIdle = 0;
            return G_SOURCE_REMOVE;
        },w);
    }
    if (!interactiveView && !preview && &display == &s.gpuScene && !w->overviewIdle && w->frameBuffer.needsOverview()) {
        w->overviewIdle = g_idle_add(+[](gpointer data) -> gboolean {
            auto* w = static_cast<Window*>(data);
            auto& s = w->session;
            if (!w->wheelIdle && !w->panning && !w->down &&
                w->frameBuffer.prepareOverview(s.gpuScene, s.measurer.get(), s.serial)) return G_SOURCE_CONTINUE;
            w->overviewIdle = 0;
            return G_SOURCE_REMOVE;
        }, w);
    }
    const auto& copy = w->selectionGesture.copySource();
    if (copy || (w->pendingTemplate && w->pointerOnCanvas && !w->panning)) {
        const auto& source = copy ? (w->selectionGesture.copyUsesDocumentScene(s) ? s : *copy) : *w->pendingTemplate;
        const auto delta = copy ? w->selectionGesture.copyDelta() : s.view().toDocument(w->canvasPointer) - source.view().center;
        auto shifted = view;
        shifted.transform.center = shifted.transform.center - delta;
        const auto& placement = copy ? copy : w->pendingTemplate;
        const bool changedSource = w->cachedPlacement.lock() != placement;
        if (changedSource) {
            w->placementCache.clear();
            w->cachedPlacement = placement;
        }
        if (source.scene.size() >= 200 && (changedSource || !shifted.viewportEquals(w->placementView))) {
            w->placementView = shifted;
            if (w->placementIdle) {
                auto* timer = g_main_context_find_source_by_id(nullptr, w->placementIdle);
                g_source_set_ready_time(timer, g_get_monotonic_time() + 150 * 1000);
            } else {
                w->placementIdle = g_timeout_add(150, +[](gpointer data) -> gboolean {
                    auto* w = static_cast<Window*>(data);
                    w->placementIdle = 0;
                    gtk_widget_queue_draw(w->canvas);
                    return G_SOURCE_REMOVE;
                }, w);
            }
        }
        const int factor = gtk_widget_get_scale_factor(widget);
        auto* content = copy && w->selectionGesture.hasLiveCopyProjection(s) ? &w->contentCache : nullptr;
        const linux_canvas::PanCache::ProjectedSelection projected{s.gpuScene, view,
            content && w->frameBuffer.coversDrawing(s.gpuScene, view, s.serial, factor) ?
                content->current(s.gpuScene, view, s.serial) : nullptr, w->selectionGesture.copyProjectionMask(), content, s.serial};
        if (!w->placementIdle || source.scene.size() < 200 || !w->placementCache.draw(cr, source.scene, shifted,
                s.measurer.get(), source.serial, factor, &projected))
            linux_cairo::draw(cr, source.scene, shifted, s.measurer.get());
    } else {
        w->placementCache.clear();
        w->cachedPlacement.reset();
        if (w->placementIdle) { g_source_remove(w->placementIdle); w->placementIdle = 0; }
    }
    linux_page::canvasGrid(cr, studioio::Page::get(s.doc), s.view());
    if (!w->down && !w->panning && !w->tableDrag.pending() && !w->pendingTemplate && !w->selectionGesture.pending()) {
        const auto buttons = linux_table::controls(s);
        w->drawTableRange(cr);
        linux_table::drawControls(cr, buttons, w->pointerOnCanvas ? buttons.at(w->canvasPointer) : -1);
    }
    return TRUE;
}
static GtkWidget* showDocument(GtkApplication* app, const char* path, std::unique_ptr<OcsrReview> review, GtkWidget* preferred, bool separate) {
    std::string absolute;
    if (path) {
        gchar* normalized = g_canonicalize_filename(path, nullptr);
        absolute = normalized;
        g_free(normalized);
        for (auto* existing : documentsOf(app)) {
            if (!review && existing->session.sourcePath == absolute) {
                selectDocument(*existing);
                gtk_window_present(GTK_WINDOW(existing->window));
                return existing->window;
            }
        }
    }
    auto& w = *new Window();
    linux_config::Preferences arrowPreferences;
    const auto headTier = arrowPreferences.text("Arrow","HeadTier");
    if (headTier.size()==1 && headTier[0]>='0' && headTier[0]<='4')
        w.session.arrowHeadTier = headTier[0]-'0';
    const auto storedArrow = arrowPreferences.text("Arrow","Command");
    if (const auto* preset = arrows::presetByCommand(storedArrow); preset && preset->implemented)
        w.arrowStyle = preset;
    w.session.allowProjectedMove = true;
    w.session.preparePartialMove = [&w](const std::vector<uint8_t>& motion) {
        return w.partialMoveLayers.prepare(w.session,motion,gtk_widget_get_scale_factor(w.canvas));
    };
    w.ocsrReview = std::move(review);
    if (!path) {
        linux_drawing::Preferences::load().defaults.apply(w.session.doc, linux_i18n::utf8);
        linux_page::initialize(w.session.doc);
        w.session.refresh();
    }
    // Keep the midpoint of a standard-length bond reachable at normal zoom.
    w.session.hotspotAtomPriorityPixels = 4.;
    auto hoverProfile = w.session.input.profile();
    hoverProfile.retainPreviousTarget = false;
    w.session.input.setProfile(hoverProfile);
    const bool tabbed = !w.ocsrReview && g_strcmp0(g_application_get_application_id(G_APPLICATION(app)), "org.moleculestudio.Native") == 0;
    DocumentHost* host = nullptr;
    if (tabbed && !separate) {
        host = hostFor(preferred ? preferred : GTK_WIDGET(gtk_application_get_active_window(app)));
        if (!host || !host->tabbed) {
            host = nullptr;
            for (auto* peer : documentsOf(app)) if (hostFor(peer->window)->tabbed) { host = hostFor(peer->window); break; }
        }
    }
    const bool freshHost = !host;
    if (freshHost) {
        host = new DocumentHost();
        host->tabbed = tabbed;
        w.window = host->shell = gtk_application_window_new(app);
        host->stack = gtk_stack_new();
        gtk_stack_set_homogeneous(GTK_STACK(host->stack), FALSE);
        gtk_container_add(GTK_CONTAINER(w.window), host->stack);
        gtk_style_context_add_class(gtk_widget_get_style_context(w.window), "moleculestudio-editor");
        gtk_window_set_icon_name(GTK_WINDOW(w.window), "org.moleculestudio.Native");
        g_object_set_data_full(G_OBJECT(w.window), "document-host", host,
                               +[](gpointer value){ delete static_cast<DocumentHost*>(value); });
        auto* display = gtk_widget_get_display(w.window);
        auto* monitor = gdk_display_get_primary_monitor(display);
        if (!monitor) monitor = gdk_display_get_monitor(display, 0);
        GdkRectangle workarea{};
        if (monitor) gdk_monitor_get_workarea(monitor, &workarea);
        gtk_window_set_default_size(GTK_WINDOW(w.window), 1440, 880);
        bool fillWorkarea = monitor && (workarea.width <= 1440 || workarea.height <= 880);
    #ifdef GDK_WINDOWING_BROADWAY
        // A browser tab can connect or resize after the first window is constructed.
        fillWorkarea = fillWorkarea || GDK_IS_BROADWAY_DISPLAY(display);
    #endif
        // Let the window manager include title bar and frame extents in the fit.
        if (fillWorkarea) gtk_window_maximize(GTK_WINDOW(w.window));
        auto* header=gtk_header_bar_new();
        gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header),TRUE);
        gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header),":minimize,maximize,close");
        g_object_set(header,"spacing",0,nullptr);
        auto* brand=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,15);
        gtk_style_context_add_class(gtk_widget_get_style_context(brand), "document-brand");
        gtk_widget_set_margin_start(brand,16);
        auto* mark=gtk_drawing_area_new();
        atk_object_set_role(gtk_widget_get_accessible(mark), ATK_ROLE_REDUNDANT_OBJECT);
        gtk_widget_set_size_request(mark,24,24);
        gtk_widget_set_valign(mark,GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(mark), "document-mark");
        g_signal_connect(mark,"draw",G_CALLBACK(+[](GtkWidget* widget,cairo_t* cr,gpointer)->gboolean {
            gtk_render_background(gtk_widget_get_style_context(widget),cr,0,0,24,24);
            cairo_translate(cr,12,12);
            cairo_scale(cr,18./28,18./28);
            cairo_set_source_rgb(cr,1,1,1);
            cairo_set_line_width(cr,1.6);
            cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
            Vec2 vertices[6];
            for(int i=0;i<6;++i){
                const double angle=(i/3.-.5)*3.141592653589793;
                vertices[i]=Vec2(10*cos(angle),10*sin(angle));
                if(!i)cairo_move_to(cr,vertices[i].x,vertices[i].y);
                else cairo_line_to(cr,vertices[i].x,vertices[i].y);
            }
            cairo_close_path(cr);
            for(int i=0;i<6;i+=2){
                cairo_move_to(cr,.67*vertices[i].x,.67*vertices[i].y);
                cairo_line_to(cr,.67*vertices[i+1].x,.67*vertices[i+1].y);
            }
            cairo_stroke(cr);
            return TRUE;
        }),nullptr);
        gtk_box_pack_start(GTK_BOX(brand),mark,FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(brand),gtk_label_new("Molecule Studio"),FALSE,FALSE,0);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header),brand);
        if (tabbed) {
            host->strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            host->scroll = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(host->scroll), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
            gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(host->scroll), TRUE);
            gtk_widget_set_size_request(host->scroll, 90, -1);
            gtk_container_add(GTK_CONTAINER(host->scroll), host->strip);
            gtk_header_bar_pack_start(GTK_HEADER_BAR(header), host->scroll);
            gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header), gtk_label_new(""));
            addSelectionInfo(*host, GTK_HEADER_BAR(header));
            auto* list = documentButton(DocumentButton::List);
            gtk_widget_set_tooltip_text(list, tr("打开的文档"));
            atk_object_set_name(gtk_widget_get_accessible(list), tr("打开的文档"));
            gtk_style_context_add_class(gtk_widget_get_style_context(list),"document-action");
            g_signal_connect(list, "clicked", G_CALLBACK(showDocumentList), host);
            auto* add = documentButton(DocumentButton::New);
            gtk_widget_set_tooltip_text(add, tr("新建文档"));
            atk_object_set_name(gtk_widget_get_accessible(add), tr("新建文档"));
            gtk_style_context_add_class(gtk_widget_get_style_context(add),"document-action");
            bindDocumentMenu(add, *host);
            gtk_header_bar_pack_start(GTK_HEADER_BAR(header), add);
            gtk_header_bar_pack_start(GTK_HEADER_BAR(header), list);
            g_object_set_data(G_OBJECT(w.window),"document-brand",brand);
            g_signal_connect(w.window,"configure-event",G_CALLBACK(+[](GtkWidget* shell,GdkEventConfigure* event,DocumentHost* h)->gboolean{
                auto* brand=GTK_WIDGET(g_object_get_data(G_OBJECT(shell),"document-brand"));
                gtk_widget_set_size_request(brand,event->width >= 800 ? 194 : 140,-1);
                int brandWidth=0;
                gtk_widget_get_preferred_width(brand,&brandWidth,nullptr);
                const int reserved = std::min(344, std::max(160, event->width / 2 - 96));
                const int width=std::max(90,event->width-brandWidth-reserved-54);
                gtk_widget_set_size_request(h->strip,width,-1);
                gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(h->scroll),width);
                return FALSE;
            }),host);
            g_signal_connect(add, "clicked", G_CALLBACK(+[](GtkButton*, DocumentHost* h) {
                showDocument(gtk_window_get_application(GTK_WINDOW(h->shell)), nullptr, {}, h->shell);
            }), host);
        }
        gtk_window_set_titlebar(GTK_WINDOW(w.window),header);
        g_signal_connect(w.window, "key-press-event", G_CALLBACK(documentKey), host);
        g_signal_connect(w.window, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, DocumentHost* h)->gboolean {
            auto* original = h->active;
            for (auto* doc : h->documents) {
                doc->finishCanvasText();
                if (!doc->session.dirty) continue;
                selectDocument(*doc);
                if (!doc->canClose()) return TRUE;
            }
            if (original) selectDocument(*original);
            return FALSE;
        }), host);
    } else w.window = host->shell;
    auto* box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
    auto* bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    auto* tabs=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    auto* pages=gtk_stack_new();
    gtk_widget_set_margin_start(box,10);
    gtk_widget_set_margin_end(box,10);
    w.page = box;
    g_object_set_data_full(G_OBJECT(box), "document-window", &w,
                           +[](gpointer value){ delete static_cast<Window*>(value); });
    gtk_container_add(GTK_CONTAINER(host->stack),box);
    addDocumentTab(*host,w);
    gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
    gtk_widget_set_size_request(bar,-1,36);
    auto* tabViewport=gtk_scrolled_window_new(nullptr,nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tabViewport),GTK_POLICY_AUTOMATIC,GTK_POLICY_NEVER);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(tabViewport),TRUE);
    gtk_widget_set_margin_start(tabViewport,12);
    gtk_box_pack_start(GTK_BOX(bar),tabViewport,TRUE,TRUE,0);
    gtk_container_add(GTK_CONTAINER(tabViewport),tabs);
    gtk_box_pack_start(GTK_BOX(box),pages,FALSE,FALSE,0);
    w.canvas=gtk_drawing_area_new(); w.status=gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(w.status),0);
    gtk_label_set_ellipsize(GTK_LABEL(w.status), PANGO_ELLIPSIZE_END);
    gtk_widget_set_can_focus(w.canvas,TRUE);
    w.canvasOverlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(w.canvasOverlay), w.canvas);
    gtk_box_pack_start(GTK_BOX(box), w.canvasWithScrollbars(), TRUE, TRUE, 0);
    auto* footer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_style_context_add_class(gtk_widget_get_style_context(footer),"document-footer");
    gtk_widget_set_margin_start(footer,18);
    gtk_widget_set_margin_end(footer,16);
    gtk_widget_set_size_request(footer,-1,30);
    gtk_box_pack_end(GTK_BOX(box),footer,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(footer),w.status,TRUE,TRUE,0);
    w.documentStats=gtk_label_new("");
    gtk_widget_set_name(w.documentStats,"document-statistics");
    gtk_label_set_xalign(GTK_LABEL(w.documentStats),0);
    gtk_widget_set_size_request(w.documentStats,274,-1);
    gtk_box_pack_end(GTK_BOX(footer),w.documentStats,FALSE,FALSE,0);
    auto* tools=w.tools=gtk_combo_box_text_new();
    w.refreshToolLabels();
    g_signal_connect(tools,"changed",G_CALLBACK(+[](GtkComboBox* c,Window* w){
        auto* id=gtk_combo_box_get_active_id(c);
        if (!id || w->updatingToolLabels) return;
        const auto index = std::stoul(id);
        if (index != w->session.activeIndex) {
            w->finishCanvasText();
            w->leavePerspectiveTool();
            w->pendingTemplate.reset();
            w->selectionGesture.cancel();
            w->tableDrag.cancel();
            w->session.activate(index);
            w->report(Error());
        }
    }),&w);
    auto connectAction = [&](GtkWidget* widget, const char* signal, const char* id) {
        const auto& command = linux_shortcuts::shortcut(id);
        w.shortcutHints.emplace_back(widget, &command);
        g_object_set_data(G_OBJECT(widget), "shortcut-command", const_cast<linux_shortcuts::Shortcut*>(&command));
        g_signal_connect(widget, signal, G_CALLBACK(+[](GtkWidget* widget, Window* window) {
            const auto* command = static_cast<const linux_shortcuts::Shortcut*>(
                g_object_get_data(G_OBJECT(widget), "shortcut-command"));
            window->activateShortcut(*command);
        }), &w);
    };
    auto* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_widget_set_margin_end(actions,4);
    gtk_box_pack_end(GTK_BOX(bar),actions,FALSE,FALSE,0);
    auto quickButton = [&](const char* label, const char* id) {
        auto* button=linux_i18n::bindLabel(gtk_button_new(),label);
        linux_ribbon::style(button,"ribbon-tab");
        gtk_widget_set_focus_on_click(button,FALSE);
        gtk_box_pack_start(GTK_BOX(actions),button,FALSE,FALSE,0);
        connectAction(button,"clicked",id);
    };
    auto* dragLock=linux_i18n::bindLabel(gtk_toggle_button_new(),"锁定键长");
    linux_ribbon::style(dragLock,"ribbon-tab");
    gtk_widget_set_focus_on_click(dragLock,FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dragLock),w.session.dragFixedLength);
    gtk_box_pack_start(GTK_BOX(actions),dragLock,FALSE,FALSE,0);
    g_signal_connect(dragLock,"toggled",G_CALLBACK(+[](GtkToggleButton* button,Window* w){
        w->session.dragFixedLength=gtk_toggle_button_get_active(button);
        w->session.status=w->session.dragFixedLength ? "锁定键长：开" : "锁定键长：关";
        w->report(Error());
    }),&w);
    if (w.ocsrReview) {
        auto* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        for (const auto& command : {std::make_pair("插入修订结果", 0), std::make_pair("复制修订结果", 1),
                std::make_pair("整体构型翻转", 2), std::make_pair("实楔与粗实线互换", 3),
                std::make_pair("虚楔与粗虚线互换", 4)}) {
            auto* item = linux_i18n::bindLabel(gtk_button_new(), command.first);
            g_object_set_data(G_OBJECT(item), "ocsr-review-command", GINT_TO_POINTER(command.second));
            g_signal_connect(item, "clicked", G_CALLBACK(+[](GtkWidget* item, Window* w) {
                const int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "ocsr-review-command"));
                if (action <= 1) w->finishOcsrReview(action == 1);
                else if (action == 2) w->report(w->session.submit(ocsr_edit::flipConfiguration(), "整体构型翻转"));
                else w->report(w->session.submit(ocsr_edit::swapBondTypes(action == 4), "互换识别键型"));
            }), &w);
            gtk_box_pack_start(GTK_BOX(controls), item, FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
    }
    auto* menus = gtk_menu_bar_new();
    gtk_box_pack_start(GTK_BOX(bar), menus, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(bar), menus, 0);
    auto menuBranch = [&](GtkWidget* parent, const char* label) {
        auto* item = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""), label);
        auto* popup = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), popup);
        gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
        return popup;
    };
        auto item = [&](GtkWidget* popup, const char* label, const char* id) {
        auto* widget = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""), label);
        connectAction(widget, "activate", id);
        gtk_menu_shell_append(GTK_MENU_SHELL(popup), widget);
        return widget;
    };
    auto tidyItem = [&](GtkWidget* popup, bool restore) {
        auto* widget = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""), restore ? "撤销文字整理" : "文字整理");
        g_object_set_data(G_OBJECT(widget), "restore-text", GINT_TO_POINTER(restore));
        g_signal_connect(widget, "activate", G_CALLBACK(+[](GtkWidget* widget, Window* window) {
            window->tidyText(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "restore-text")) != 0);
        }), &w);
        gtk_menu_shell_append(GTK_MENU_SHELL(popup), widget);
    };
    auto* file = menuBranch(menus,"文件");
    auto* fileButton=gtk_menu_get_attach_widget(GTK_MENU(file));
    gtk_widget_set_margin_start(menus,6);
    gtk_widget_set_size_request(fileButton,64,-1);
    gtk_label_set_xalign(GTK_LABEL(gtk_bin_get_child(GTK_BIN(fileButton))),.5);
    linux_ribbon::style(menus,"ribbon-file");
    auto menu = [&](const char* label) { return menuBranch(file, label); };
    item(file, "新建", "new");
    item(file, "打开…", "open");
    item(file, "保存", "save");
    item(file, "另存为…", "saveAs");
    item(file, "导出 CDXML…", "exportCdxml");
    item(file, "打印…", "print");
    item(file, "导出 PDF…", "exportPdf");
    auto* officeExport=linux_i18n::bindLabel(gtk_menu_item_new_with_label(""),"导出可编辑 Office 对象…");
    g_signal_connect(officeExport,"activate",G_CALLBACK(+[](GtkWidget*,Window* w){w->exportOffice();}),&w);
    gtk_menu_shell_append(GTK_MENU_SHELL(file),officeExport);
    auto* copyChemical = menuBranch(file,"复制为化学格式");
    auto* pasteChemical = menuBranch(file,"从化学格式粘贴");
    for (const auto* id : {"copyAsSMILES","copyAsHELM","copyAsMol","copyAsMolV3000"})
        item(copyChemical,linux_shortcuts::shortcut(id).label.c_str(),id);
    for (const auto* id : {"pasteAsSMILES","pasteAsHELM","pasteAsMol"})
        item(pasteChemical,linux_shortcuts::shortcut(id).label.c_str(),id);
    item(file, "关闭文档", "close");
    if (tabbed) {
        auto* newWindow = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""), "新建窗口");
        g_signal_connect_swapped(newWindow, "activate", G_CALLBACK(newDocumentWindow), w.window);
        gtk_menu_shell_append(GTK_MENU_SHELL(file), newWindow);
    }
    w.selectionMenu = gtk_menu_new();
    gtk_menu_attach_to_widget(GTK_MENU(w.selectionMenu), w.canvas, nullptr);
    w.catalyticOutlineItem = item(w.selectionMenu, "添加催化循环路径…", "catalyticOutline");
    item(w.selectionMenu, "复制", "copy");
    item(w.selectionMenu, "复制为 PNG", "copyPng");
    item(w.selectionMenu, "剪切", "cut");
    item(w.selectionMenu, "删除", "delete");
    w.attachBaseHydrogenItem = item(w.selectionMenu, "添加 B–H 化学键", "attachBaseHydrogen");
    tidyItem(w.selectionMenu, false);
    tidyItem(w.selectionMenu, true);
    auto* conversion = menuBranch(w.selectionMenu, "转换立体投影");
    w.projectionConversion = gtk_menu_get_attach_widget(GTK_MENU(conversion));
    auto projectionChoice = [&](GtkWidget* parent, const char* label, int kind, int variant, bool convert) {
        auto* choice = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""), label);
        g_object_set_data(G_OBJECT(choice), "projection-kind", GINT_TO_POINTER(kind));
        g_object_set_data(G_OBJECT(choice), "projection-variant", GINT_TO_POINTER(variant));
        g_object_set_data(G_OBJECT(choice), "projection-convert", GINT_TO_POINTER(convert));
        g_signal_connect(choice, "activate", G_CALLBACK(+[](GtkWidget* choice, Window* w) {
            const auto kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(choice), "projection-kind"));
            const auto variant = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(choice), "projection-variant"));
            if (g_object_get_data(G_OBJECT(choice), "projection-convert"))
                w->report(studio_projection::convert(w->session, w->projectionContextAxis, kind, variant));
            else w->chooseProjection(kind, variant);
        }), &w);
        gtk_menu_shell_append(GTK_MENU_SHELL(parent), choice);
    };
    projectionChoice(conversion, "费歇尔投影", stereo_projection::Fischer, 0, true);
    auto* toSawhorse = menuBranch(conversion, "锯架式");
    auto* toNewman = menuBranch(conversion, "纽曼投影");
    const char* variants[] = {"交叉式", "重叠式"};
    for (int variant = 0; variant < 2; ++variant) {
        projectionChoice(toSawhorse, variants[variant], stereo_projection::Sawhorse, variant, true);
        projectionChoice(toNewman, variants[variant], stereo_projection::Newman, variant, true);
    }
    gtk_widget_show_all(w.selectionMenu);
    g_signal_connect(w.canvas, "popup-menu", G_CALLBACK(+[](GtkWidget* canvas, Window* w)->gboolean {
        return w->showSelectionMenu(Vec2(gtk_widget_get_allocated_width(canvas) * .5,
            gtk_widget_get_allocated_height(canvas) * .5));
    }), &w);
    auto* arrange = menu("排列");
    item(arrange, "编组", "group");
    item(arrange, "解组", "ungroup");
    item(arrange, "左对齐", "alignLeft");
    item(arrange, "水平居中", "alignHorizontal");
    item(arrange, "右对齐", "alignRight");
    item(arrange, "顶部对齐", "alignTop");
    item(arrange, "垂直居中", "alignVertical");
    item(arrange, "底部对齐", "alignBottom");
    item(arrange, "水平分布", "distributeHorizontal");
    item(arrange, "垂直分布", "distributeVertical");
    item(arrange, "合并", "mergeSelection");
    item(arrange, "旋转 90°", "rotateSelection");
    item(arrange, "旋转对象…", "rotateDegrees");
    item(arrange, "缩放对象…", "scalePercent");
    item(arrange, "水平翻转", "flipHorizontal");
    item(arrange, "垂直翻转", "flipVertical");
    item(arrange, "置于顶层", "stackFront");
    item(arrange, "置于底层", "stackBack");
    auto* formatting = menu("文字格式");
    item(formatting, "字体…", "textFont");
    item(formatting, "增大字号", "textSizeUp");
    item(formatting, "减小字号", "textSizeDown");
    item(formatting, "文字颜色…", "textColor");
    item(formatting, "加粗", "bold");
    item(formatting, "斜体", "italic");
    item(formatting, "下划线", "underline");
    item(formatting, "化学式", "textFormula");
    item(formatting, "上标", "textSuperscript");
    item(formatting, "下标", "textSubscript");
    item(formatting, "左对齐", "textLeft");
    item(formatting, "居中", "textCenter");
    item(formatting, "右对齐", "textRight");
    item(formatting, "两端对齐", "textJustify");
    auto* templates = menu("模板");
    item(templates, "结构模板…", "templates");
    item(templates, "我的模板…", "openTemplate");
    w.saveTemplateItem = item(templates, "添加自定义模板…", "saveTemplate");
    item(templates, "导入自定义模板…", "importTemplate");
    item(templates, "导出自定义模板…", "exportTemplate");
    auto* tables = menu("表格");
    item(tables, "插入表格…", "insertTable");
    item(tables, "插入三线表…", "insertThreeLineTable");
    item(tables, "绘制表格", "tableTool");
    item(tables, "编辑表格…", "editTable");
    auto* chemistry = menu("化学");
    item(chemistry, "预测核磁…", "nmr");
    item(chemistry, "图片转结构…", "recognize");
    item(chemistry, "AI 绘图…", "assistant");
    item(chemistry, "结构转命名", "structureToName");
    item(chemistry, "命名转结构…", "nameToStructure");
    item(chemistry, "二维结构整理", "cleanup");
    item(chemistry, "反应整理", "cleanReaction");
    item(chemistry, "官能团展开", "expand");
    item(chemistry, "官能团收起", "collapse");
    item(chemistry, "自定义官能团…", "customGroups");
    tidyItem(chemistry, false);
    tidyItem(chemistry, true);
    auto* layout = linux_i18n::bindLabel(gtk_check_menu_item_new_with_label(""), "自动排版");
    w.automaticLayout = GTK_CHECK_MENU_ITEM(layout);
    gtk_check_menu_item_set_active(w.automaticLayout, chemcompletion::formulaLayout());
    g_signal_connect(layout, "toggled", G_CALLBACK(+[](GtkCheckMenuItem* control, Window* window) {
        const bool enabled = gtk_check_menu_item_get_active(control);
        const bool saved = chemcompletion::saveFormulaLayout(enabled);
        auto* app = gtk_window_get_application(GTK_WINDOW(window->window));
        for (auto* peer : documentsOf(app)) {
            if (!peer || !peer->automaticLayout) continue;
            g_signal_handlers_block_matched(peer->automaticLayout, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, peer);
            gtk_check_menu_item_set_active(peer->automaticLayout, chemcompletion::formulaLayout());
            g_signal_handlers_unblock_matched(peer->automaticLayout, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, peer);
        }
        if (!saved) window->report({ErrorCode::IoError, "无法保存自动文字排版设置。"});
    }), &w);
    gtk_menu_shell_append(GTK_MENU_SHELL(chemistry), layout);
    item(chemistry, "构型标注", "stereo");
    item(chemistry, "计算器", "calculator");
    auto* projections = menu("立体投影");
    auto* fischer = menuBranch(projections, "费歇尔投影");
    for (int count = 1; count <= 4; ++count)
        projectionChoice(fischer, std::to_string(count).c_str(), stereo_projection::Fischer, count, false);
    projectionChoice(fischer, "自定义中心数…", stereo_projection::Fischer, 0, false);
    auto* sawhorse = menuBranch(projections, "锯架式");
    auto* newman = menuBranch(projections, "纽曼投影");
    for (int variant = 0; variant < 2; ++variant) {
        projectionChoice(sawhorse, variants[variant], stereo_projection::Sawhorse, variant, false);
        projectionChoice(newman, variants[variant], stereo_projection::Newman, variant, false);
    }
    for (const auto mirror : {false, true}) {
        auto* sugars = menuBranch(projections, mirror ? "L-糖" : "D-糖");
        const auto& choices = stereo_projection::sugars();
        for (size_t index = 0; index < choices.size(); ++index)
            projectionChoice(sugars, choices[index].title, mirror ? -2 : -1, int(index), false);
    }
    auto* mechanism = menu("反应机理");
    item(mechanism, "机理箭头", "mechanismArrowTool");
    item(mechanism, "机理鱼钩箭头", "fishhookTool");
    item(mechanism, "等同符号 ≡", "mechanismEquivalence");
    item(mechanism, "待成键", "virtualBond");
    item(mechanism, "过渡态 [ ]‡", "transitionState");
    auto* reagents = menuBranch(mechanism, "通用试剂");
    item(reagents, "插入 B⁻", "mechanismBase");
    item(reagents, "插入 Nu⁻", "mechanismNucleophile");
    item(reagents, "插入 H⁺", "mechanismProton");
    auto* ions = menuBranch(mechanism, "常见离子");
    for (const int charge : {-1, 1}) {
        auto* choices = menuBranch(ions, charge < 0 ? "常见负离子" : "常见正离子");
        const auto& entries = studioions::entries();
        for (size_t index = 0; index < entries.size(); ++index) {
            const bool inList = charge < 0 ? index < 25 : index >= 25 || std::strcmp(entries[index].label, "H") == 0;
            if (!inList) continue;
            const auto caption = std::string(entries[index].label) + (charge < 0 ? "⁻" : "⁺");
            auto* entry = gtk_menu_item_new_with_label(caption.c_str());
            g_object_set_data(G_OBJECT(entry), "ion-id", GINT_TO_POINTER(charge * (static_cast<int>(index) + 1)));
            g_signal_connect(entry, "activate", G_CALLBACK(+[](GtkWidget* widget, Window* window) {
                const int code = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "ion-id"));
                window->pendingTemplate.reset();
                window->cancelShape();
                window->cancelMechanismPlacement();
                window->selectionGesture.cancel();
                window->tableDrag.cancel();
                gtk_widget_grab_focus(window->canvas);
                window->report(linux_mechanism::insertCommonIon(window->session,
                    studioions::entries().at(std::abs(code) - 1), code < 0 ? -1 : 1));
            }), &w);
            gtk_menu_shell_append(GTK_MENU_SHELL(choices), entry);
        }
    }
    auto* charge = menuBranch(mechanism, "原子电荷");
    item(charge, "正电荷", "positiveCharge");
    item(charge, "负电荷", "negativeCharge");
    item(charge, "圈正电荷", "circledPositiveCharge");
    item(charge, "圈负电荷", "circledNegativeCharge");
    item(charge, "标注 δ+", "partialPositive");
    item(charge, "标注 δ−", "partialNegative");
    item(mechanism, "插入孤对电子", "lonePair");
    item(mechanism, "插入自由基", "radical");
    auto* reactive = menuBranch(mechanism, "活性中心");
    item(reactive, "插入连烯", "allene");
    item(reactive, "插入卡宾", "carbene");
    item(mechanism, "生成下一步", "mechanismStep");
    item(mechanism, "下一步 + 质子转移", "mechanismTransfer");
    item(mechanism, "下一步 − 质子", "mechanismDeprotonate");
    auto* styleItem = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""), "生成箭头");
    auto* styleMenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(styleItem), styleMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(mechanism), styleItem);
    GSList* styles = nullptr;
    const char* styleLabels[] = {"单向", "可逆", "可逆趋势向左", "可逆趋势向右"};
    for (int i = 0; i < 4; ++i) {
        auto* style = linux_i18n::bindLabel(gtk_radio_menu_item_new_with_label(styles, ""), styleLabels[i]);
        styles = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(style));
        w.generationMenu[i]=GTK_CHECK_MENU_ITEM(style);
        g_object_set_data(G_OBJECT(style), "arrow-style", GINT_TO_POINTER(i));
        g_signal_connect(style, "toggled", G_CALLBACK(+[](GtkCheckMenuItem* entry, Window* window){
            if (gtk_check_menu_item_get_active(entry))
                window->chooseGenerationStyle(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "arrow-style")));
        }), &w);
        gtk_menu_shell_append(GTK_MENU_SHELL(styleMenu), style);
    }
    auto* catalytic = menu("催化循环");
    item(catalytic, "顺时针循环…", "catalyticClockwise");
    item(catalytic, "逆时针循环…", "catalyticCounterclockwise");
    item(catalytic, "加入试剂", "catalyticReagent");
    item(catalytic, "放出产物", "catalyticProduct");
    item(catalytic, "金属与配体…", "catalyticMetal");
    auto* drawing = menu("绘图设置");
    item(drawing, "绘图预设…", "drawingStyles");
    item(drawing, "线宽…", "graphicsWidth");
    item(drawing, "绘图颜色…", "graphicsColor");
    item(drawing, "图案填充…", "shapeFill");
    auto* view = menu("视图");
    item(view, "适应窗口", "fit");
    item(view, "适合页面", "fitPage");
    item(view, "实际大小", "actualSize");
    item(view, "放大", "zoomIn");
    item(view, "缩小", "zoomOut");
    item(view, "结构透视", "perspective");
    auto* settingsButton=gtk_button_new();
    linux_ribbon::style(settingsButton,"ribbon-tab");
    gtk_container_add(GTK_CONTAINER(settingsButton),linux_ribbon::label("设置"));
    g_signal_connect(settingsButton,"clicked",G_CALLBACK(+[](GtkButton*, Window* w){ w->chooseSettings(); }),&w);
    gtk_box_pack_start(GTK_BOX(actions),settingsButton,FALSE,FALSE,0);
    {
        auto* settingsMenu=menu("设置");
        item(settingsMenu, "绘图预设…", "drawingStyles");
        item(settingsMenu, "页面与排版…", "pageSetup");
        item(settingsMenu, "快捷键…", "shortcuts");
        item(settingsMenu, "自定义官能团…", "customGroups");
        item(settingsMenu, "界面语言…", "language");
        item(settingsMenu, "皮肤…", "skin");
        item(settingsMenu, "另存 AI 绘图 Skill…", "exportSkill");
        item(settingsMenu, "检查更新…", "checkUpdates");
    }
    quickButton("适合窗口","fit");
    linux_config::Preferences settings;
    auto constraint = [&](const char* label, const char* key) {
        auto* control = linux_i18n::bindLabel(gtk_check_menu_item_new_with_label(""), label);
        auto* checked = GTK_CHECK_MENU_ITEM(control);
        gtk_check_menu_item_set_active(checked, settings.flag("Drawing", key, true));
        g_object_set_data(G_OBJECT(control), "constraint-key", const_cast<char*>(key));
        g_signal_connect(control, "toggled", G_CALLBACK(+[](GtkCheckMenuItem* control, Window* window){
            auto* key = static_cast<const char*>(g_object_get_data(G_OBJECT(control), "constraint-key"));
            window->persistConstraints(key, gtk_check_menu_item_get_active(control));
        }), &w);
        w.shortcutHints.emplace_back(control, &linux_shortcuts::shortcut(
            std::string(key) == "FixedLength" ? "fixedLength" : "fixedAngles"));
        gtk_menu_shell_append(GTK_MENU_SHELL(drawing), control);
        return checked;
    };
    w.fixedLength = constraint("固定键长", "FixedLength");
    w.fixedAngles = constraint("固定键角", "FixedAngles");
    w.setConstraints(gtk_check_menu_item_get_active(w.fixedLength), gtk_check_menu_item_get_active(w.fixedAngles));
    auto* completion = linux_i18n::bindLabel(gtk_check_menu_item_new_with_label(""), "补齐官能团拼写");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(completion), chemcompletion::enabled());
    g_signal_connect(completion, "toggled", G_CALLBACK(+[](GtkCheckMenuItem* control, Window* window){
        if (!chemcompletion::saveEnabled(gtk_check_menu_item_get_active(control)))
            window->report({ErrorCode::IoError, "无法写入官能团补齐设置"});
    }), &w);
    gtk_menu_shell_append(GTK_MENU_SHELL(drawing), completion);
    gtk_widget_add_events(w.canvas,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK|
        GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK|GDK_TOUCHPAD_GESTURE_MASK|GDK_SCROLL_MASK|GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(w.canvas, "enter-notify-event", G_CALLBACK(+[](GtkWidget*, GdkEventCrossing* e, Window* w)->gboolean {
        if (!w->down && !w->panning) w->hoverCanvas(Vec2(e->x, e->y), e->state);
        return FALSE;
    }), &w);
    g_signal_connect(w.canvas, "leave-notify-event", G_CALLBACK(+[](GtkWidget*, GdkEventCrossing*, Window* w)->gboolean {
        w->leaveCanvas();
        return FALSE;
    }), &w);
    g_signal_connect(w.canvas, "unmap", G_CALLBACK(+[](GtkWidget*, Window* w) {
        if (w->selectionIdle) { g_source_remove(w->selectionIdle); w->selectionIdle = 0; }
        w->warmedSelection.clear();
        if (!w->session.partialMoveActive()) w->partialMoveLayers.clear();
    }), &w);
    g_signal_connect(w.canvas, "destroy", G_CALLBACK(+[](GtkWidget*, Window* w) {
        w->finishTlcClick(false);
        if (w->placementIdle) { g_source_remove(w->placementIdle); w->placementIdle = 0; }
        if (w->selectionIdle) { g_source_remove(w->selectionIdle); w->selectionIdle = 0; }
        if (w->overviewIdle) { g_source_remove(w->overviewIdle); w->overviewIdle = 0; }
        if (w->wheelIdle) { g_source_remove(w->wheelIdle); w->wheelIdle = 0; }
    }), &w);
    g_signal_connect(w.canvas,"event",G_CALLBACK(+[](GtkWidget*,GdkEvent* event,Window* w)->gboolean {
        if (event->type != GDK_TOUCHPAD_PINCH) return FALSE;
        const auto& pinch = event->touchpad_pinch;
        if (pinch.phase == GDK_TOUCHPAD_GESTURE_PHASE_BEGIN) {
            w->pinchStartScale = 0;
            if (pinch.n_fingers != 2 || w->down || w->panning) return FALSE;
            w->selectionGesture.cancelTransformForZoom();
            if (w->selectionGesture.pending()) return FALSE;
            if (w->tableDrag.pending()) w->tableDrag.cancel();
            w->pinchPixel = Vec2(pinch.x,pinch.y);
            w->pinchDocumentAnchor = w->session.view().toDocument(w->pinchPixel);
            w->pinchStartScale = w->session.view().scale;
        } else if (w->pinchStartScale == 0) return FALSE;
        else if (pinch.phase == GDK_TOUCHPAD_GESTURE_PHASE_UPDATE) {
            if (std::isfinite(pinch.scale) && pinch.scale > 0) {
                w->pinchPixel = w->pinchPixel + Vec2(pinch.dx,pinch.dy);
                w->zoomAt(w->pinchPixel,w->pinchDocumentAnchor,w->pinchStartScale*pinch.scale);
            }
        } else {
            w->pinchStartScale = 0;
            if (w->pointerOnCanvas && !w->down && !w->panning)
                w->hoverCanvas(Vec2(pinch.x,pinch.y),pinch.state);
        }
        return TRUE;
    }),&w);
    g_signal_connect(w.canvas,"scroll-event",G_CALLBACK(+[](GtkWidget*,GdkEventScroll* e,Window* w)->gboolean{
        double steps=0;
        if(e->direction==GDK_SCROLL_UP)steps=1;
        else if(e->direction==GDK_SCROLL_DOWN)steps=-1;
        else if(e->direction==GDK_SCROLL_SMOOTH)steps=-e->delta_y;
        if(steps==0 || w->down)return FALSE;
        w->selectionGesture.cancelTransformForZoom();
        if(w->selectionGesture.pending())return FALSE;
        if(w->tableDrag.pending()) w->tableDrag.cancel();
        w->zoom(Vec2(e->x,e->y),steps);
        if (!w->panning) w->hoverCanvas(Vec2(e->x,e->y), e->state);
        return TRUE;
    }),&w);
    g_signal_connect(w.canvas,"grab-broken-event",G_CALLBACK(+[](GtkWidget*,GdkEventGrabBroken*,Window* w)->gboolean{
        w->panning=false;
        w->down=false;
        w->cancelMechanismPlacement();
        w->tableDrag.cancel();
        w->selectionGesture.cancel();
        w->session.cancel();
        w->leaveCanvas();
        return FALSE;
    }),&w);
    g_signal_connect(w.canvas,"draw",G_CALLBACK(draw),&w);
    gtk_widget_set_has_tooltip(w.canvas, TRUE);
    g_signal_connect(w.canvas, "query-tooltip", G_CALLBACK(+[](GtkWidget*, gint x, gint y, gboolean keyboard,
            GtkTooltip* tooltip, Window* w) -> gboolean {
        if (keyboard || w->down || w->panning || w->tableDrag.pending() || w->pendingTemplate ||
                w->selectionGesture.pending()) return FALSE;
        const auto buttons = linux_table::controls(w->session);
        const auto index = buttons.at(Vec2(x, y));
        if (index < 0) return FALSE;
        gtk_tooltip_set_text(tooltip, tr(buttons.buttons[index].label));
        return TRUE;
    }), &w);
    g_signal_connect(w.canvas,"button-press-event",G_CALLBACK(+[](GtkWidget*,GdkEventButton* e,Window* w)->gboolean{
        w->finishCanvasText();
        w->leaveCanvas();
        if(e->button==2){
            w->cancelMechanismPlacement();
            w->tableDrag.cancel();
            w->selectionGesture.cancel();
            w->session.cancel();
            w->down=false;
            w->panning=true;
            w->panStart=Vec2(e->x,e->y);
            w->updateCanvasCursor(w->panStart);
            return TRUE;
        }
        if (w->pendingShape && e->button == 3) {w->cancelShape(); return TRUE;}
        if (w->pendingTemplate && e->button == 3) {
            w->pendingTemplate.reset();
            w->session.status = "已取消模板放置";
            w->report({});
            return TRUE;
        }
        if (e->button == 3 && w->cancelMechanismPlacement()) {
            w->report({});
            return TRUE;
        }
        if (e->type == GDK_BUTTON_PRESS && gdk_event_triggers_context_menu(reinterpret_cast<GdkEvent*>(e)) &&
                w->showSelectionMenu(Vec2(e->x, e->y), reinterpret_cast<GdkEvent*>(e))) return TRUE;
        if(e->button!=1 || w->panning)return FALSE;
        gtk_widget_grab_focus(w->canvas);
        if (w->pendingShape) {w->shapeStart=w->session.view().toDocument(Vec2(e->x,e->y)); w->draggingShape=true; return TRUE;}
        if (e->type == GDK_BUTTON_PRESS && w->mechanismPlacement != linux_mechanism::Placement::None) {
            w->down = true;
            w->pointer(PointerPhase::Down, e->x, e->y, e->state);
            return TRUE;
        }
        if (w->pendingTemplate && e->type == GDK_BUTTON_PRESS) {
            const auto point = w->session.view().toDocument(Vec2(e->x, e->y));
            auto source = std::move(w->pendingTemplate);
            const auto result = w->insertStructure(source, w->pendingInsertion, &point);
            if (result.ok()) {
                w->session.activateCommand("chooseSelectionTool_Marquee");
                w->rememberPlacedRing(source);
                w->session.status = w->pendingInserted;
            }
            w->report(result);
            return TRUE;
        }
        if(e->type==GDK_BUTTON_PRESS && w->tlcClickTimer){
            int threshold=5;
            g_object_get(gtk_widget_get_settings(w->canvas),"gtk-double-click-distance",&threshold,nullptr);
            auto delta=w->session.view().toDevice(w->tlcClickPoint)-Vec2(e->x,e->y);
            if(std::abs(delta.x)>threshold || std::abs(delta.y)>threshold)w->finishTlcClick(true);
        }
        const auto& tool=w->session.entries[w->session.activeIndex];
        if(e->type==GDK_2BUTTON_PRESS && (tool.owner=="T05" || tool.command=="chooseTLCPlateTool")){
            const auto pixel=Vec2(e->x,e->y);
            const auto ref=w->session.pick(w->session.view().toDocument(pixel));
            if(experimental::isFigureObject(w->session.doc,ref)){
                ObjectId plate;uint32_t spot=0;tlcSpotAt(w->session,pixel,plate,spot);
                w->finishTlcClick(false);w->session.deferTlcSpotClick();
                w->down=false;w->selectionGesture.cancel();w->session.cancel();
                w->tlcSettings(ref.id,spot);return TRUE;
            }
        }
        if (e->type == GDK_2BUTTON_PRESS && !w->perspectiveTool &&
                w->session.entries[w->session.activeIndex].command.find("Eraser") == std::string::npos &&
                w->editProjectionEndpoint(w->session.view().toDocument(Vec2(e->x, e->y)))) return TRUE;
        if (e->type == GDK_BUTTON_PRESS &&
                w->selectionGesture.begin(w->session, Vec2(e->x, e->y), e->state, w->perspectiveTool)) {
            w->down = false;
            {
                w->pointerOnCanvas = true;
                w->canvasPointer = Vec2(e->x, e->y);
                w->updateCanvasCursor(w->canvasPointer);
            }
            w->report({});
            return TRUE;
        }
        if (linux_table::canvasTool(w->session) && !(e->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) {
            const Vec2 pixel(e->x, e->y);
            const auto buttons = linux_table::controls(w->session);
            const auto control = buttons.at(pixel);
            if (e->type == GDK_BUTTON_PRESS && control >= 0) {
                w->report(linux_table::applyControl(w->session, buttons, control));
                return TRUE;
            }
            linux_table::Cell cell;
            const auto point = w->session.view().toDocument(pixel);
            const auto id = linux_table::at(w->session.doc, point, cell);
            if (id != kInvalidObjectId) {
                w->selectTableCell(id,cell,e->state & GDK_SHIFT_MASK);
                if (e->type == GDK_2BUTTON_PRESS) w->editTable(id);
                else if (e->type == GDK_BUTTON_PRESS) {
                    w->down = false;
                    w->tableDrag.begin(w->session, id, point);
                    w->updateCanvasCursor(pixel);
                    w->report(Error());
                }
                return TRUE;
            }
        }
        if (e->type != GDK_BUTTON_PRESS) return TRUE;
        const auto point = w->session.view().toDocument(Vec2(e->x,e->y));
        if ((e->state & GDK_CONTROL_MASK) && tool.owner == "T05" &&
                tool.command.find("Eraser") == std::string::npos && !w->perspectiveTool) {
            auto& session = w->session;
            const auto hit = session.pick(point);
            const bool insideSelection = studioio::inside(studioio::selectionBounds(session), point);
            if (hit.valid() || insideSelection) {
                // In the selection interior, a molecular hit keeps the current selection.
                // Only other hits need the tool's normal selection update before copying.
                if (!insideSelection || (hit.valid() && hit.kind != ObjectKind::Atom && hit.kind != ObjectKind::Bond))
                    w->pointer(PointerPhase::Down,e->x,e->y,e->state);
                w->down=false;
                w->report(w->selectionGesture.beginCopy(session, point));
                return TRUE;
            }
        }
        w->down=true;
        w->pointer(PointerPhase::Down,e->x,e->y,e->state);
        return TRUE;
    }),&w);
    g_signal_connect(w.canvas,"motion-notify-event",G_CALLBACK(+[](GtkWidget*,GdkEventMotion* e,Window* w)->gboolean{
        if(w->draggingShape)w->placeShape(w->session.view().toDocument(Vec2(e->x,e->y)),false);
        else if(w->panning)w->pan(Vec2(e->x,e->y));
        else if (w->mechanismPlacement == linux_mechanism::Placement::VirtualBond)
            w->virtualBondGesture(PointerPhase::Move, w->session.view().toDocument(Vec2(e->x, e->y)));
        else if (w->selectionGesture.pending()) {
            w->selectionGesture.update(w->session, Vec2(e->x, e->y), e->state & GDK_SHIFT_MASK);
            gtk_widget_queue_draw(w->canvas);
        }
        else if (w->tableDrag.pending()) {
            const bool wasDragging = w->tableDrag.preview(w->session) != nullptr;
            w->tableDrag.update(w->session, w->session.view().toDocument(Vec2(e->x, e->y)), e->state & GDK_SHIFT_MASK);
            if (wasDragging != (w->tableDrag.preview(w->session) != nullptr) || !w->tableDrag.pending())
                w->report(Error());
            else if (wasDragging) gtk_widget_queue_draw(w->canvas);
        }
        else if(w->down)w->pointer(PointerPhase::Move,e->x,e->y,e->state);
        else w->hoverCanvas(Vec2(e->x, e->y), e->state);
        return TRUE;
    }),&w);
    g_signal_connect(w.canvas,"button-release-event",G_CALLBACK(+[](GtkWidget*,GdkEventButton* e,Window* w)->gboolean{
        if(e->button==1 && w->draggingShape){w->placeShape(w->session.view().toDocument(Vec2(e->x,e->y)),true);return TRUE;}
        if(e->button==2){w->panning=false;gtk_widget_queue_draw(w->canvas);w->hoverCanvas(Vec2(e->x, e->y), e->state);return TRUE;}
        if (e->button == 1 && w->selectionGesture.copySource()) {
            w->selectionGesture.update(w->session, Vec2(e->x,e->y), e->state & GDK_SHIFT_MASK);
            auto copy = w->selectionGesture.copySource();
            const auto delta = w->selectionGesture.copyDelta();
            w->selectionGesture.cancel();
            if (copy && length(delta) >= w->session.view().deviceRadiusToDocument(2)) {
                if (!copy->selected.empty()) w->report(w->duplicateObjects(copy->selected, delta));
                else {
                    const auto center = copy->view().center + delta;
                    w->report(w->insertStructure(copy, "拖动复制", &center));
                }
            } else w->report({});
        } else if (e->button == 1 && w->selectionGesture.pending())
            w->report(w->selectionGesture.finish(w->session, Vec2(e->x, e->y), e->state & GDK_SHIFT_MASK));
        else if (e->button == 1 && w->tableDrag.pending())
            w->report(w->tableDrag.finish(w->session, w->session.view().toDocument(Vec2(e->x, e->y)), e->state & GDK_SHIFT_MASK));
        else if(w->down && e->button==1){
            w->down=false;
            if(w->session.deferTlcSpotClick()){
                w->finishTlcClick(true);
                w->deferTlcClick(Vec2(e->x,e->y),e->state);
            }else w->pointer(PointerPhase::Up,e->x,e->y,e->state);
        }
        if (!w->down && !w->panning && !w->tableDrag.pending()) w->hoverCanvas(Vec2(e->x, e->y), e->state);
        return TRUE;
    }),&w);
    Error loaded;
    if (path) {
        try { loaded = w.session.open(absolute); }
        catch (const std::exception& e) { loaded = {ErrorCode::InvalidArgument, e.what()}; }
    }
    if (w.ocsrReview) {
        w.session.sourcePath.clear();
        if (loaded.ok()) w.session.status = "可修订识别结构，再插回原文档或复制";
    }
    w.fitOnFirstDraw = path && loaded.ok() && w.session.gpuScene.size() > 0;
    for(auto entry:{std::make_pair("分子式","formula"),std::make_pair("三维整理","conformer"),std::make_pair("格式转换","format"),std::make_pair("撤销","undo"),std::make_pair("重做","redo")})
        item(file,entry.first,entry.second);
    auto* more=gtk_menu_button_new();
    gtk_container_add(GTK_CONTAINER(more),gtk_image_new_from_icon_name("view-more-symbolic",GTK_ICON_SIZE_MENU));
    linux_i18n::bindIconLabel(more,"绘图工具");
    auto* pop=gtk_popover_new(more);
    gtk_container_add(GTK_CONTAINER(pop),tools);
    gtk_widget_show(tools);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(more),pop);
    gtk_box_pack_end(GTK_BOX(bar),more,FALSE,FALSE,0);
    linux_ribbon::style(more,"ribbon-tab");
    linux_ribbon::style(more,"ribbon-overflow");
    linux_ribbon::install(w,tabs,actions,pages,tools,ions);
    w.report(loaded);
    using RibbonLayout=std::function<void()>;
    auto layoutTabs=[bar,box,menus,fileButton,tabViewport,tabs]{
        const bool narrow=gtk_widget_get_allocated_width(bar)<800;
        auto* style=gtk_widget_get_style_context(box);
        if(narrow)gtk_style_context_add_class(style,"narrow-ribbon");
        else gtk_style_context_remove_class(style,"narrow-ribbon");
        gtk_widget_set_size_request(fileButton,narrow?42:64,-1);
        gtk_widget_set_margin_start(menus,narrow?0:6);
        gtk_widget_set_margin_start(tabViewport,narrow?0:12);
        GList* children=gtk_container_get_children(GTK_CONTAINER(tabs));
        int naturalWidth=0, count=0;
        for(auto* node=children;node;node=node->next){
            int width=0;
            gtk_widget_get_preferred_width(GTK_WIDGET(node->data),nullptr,&width);
            naturalWidth+=width;
            ++count;
        }
        g_list_free(children);
        if(count>1){
            const int space=gtk_widget_get_allocated_width(tabViewport)-naturalWidth;
            gtk_box_set_spacing(GTK_BOX(tabs),std::clamp(space/(count-1),0,24));
        }
    };
    g_object_set_data_full(G_OBJECT(tabViewport),"ribbon-layout",new RibbonLayout(layoutTabs),
        +[](gpointer fn){delete static_cast<RibbonLayout*>(fn);});
    g_signal_connect(tabViewport,"size-allocate",G_CALLBACK(+[](GtkWidget* viewport,GtkAllocation*,gpointer){
        if(g_object_get_data(G_OBJECT(viewport),"ribbon-layout-queued"))return;
        g_object_set_data(G_OBJECT(viewport),"ribbon-layout-queued",GINT_TO_POINTER(1));
        gtk_widget_add_tick_callback(viewport,+[](GtkWidget* widget,GdkFrameClock*,gpointer)->gboolean{
            g_object_set_data(G_OBJECT(widget),"ribbon-layout-queued",nullptr);
            (*static_cast<RibbonLayout*>(g_object_get_data(G_OBJECT(widget),"ribbon-layout")))();
            return G_SOURCE_REMOVE;
        },nullptr,nullptr);
    }),nullptr);
    w.highlightedTool.clear();w.updateRibbonTool();
    w.refreshShortcutHints();
    gtk_widget_show_all(w.page);
    if (w.tab) gtk_widget_show_all(w.tab);
    if (freshHost) gtk_widget_show_all(w.window);
    selectDocument(w);
    gtk_window_present(GTK_WINDOW(w.window));
    return w.window;
}

int main(int argc,char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--export-office-source") == 0) {
        try {
            Session session;
            auto result = session.open(argv[2]);
            if (result.ok()) result = session.save(argv[3], false);
            if (result.ok()) return 0;
            std::cerr << result.message << '\n';
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
        }
        return 1;
    }
    if (argc > 1 && std::strcmp(argv[1], "--export-office-preview") == 0) {
        if (argc != 4 && argc != 5 && argc != 7) {
            std::cerr << "Usage: moleculestudio-native --export-office-preview INPUT OUTPUT.svg [EMBEDDED.r62doc [WIDTH_100TH_MM HEIGHT_100TH_MM]]\n";
            return 2;
        }
        try {
            int width = 0, height = 0;
            if (argc == 7) {
                auto dimension=[](const char* text) {
                    size_t end = 0;
                    int value = std::stoi(text, &end);
                    if (value <= 0 || text[end]) throw std::invalid_argument("Invalid Office frame dimension");
                    return value;
                };
                width = dimension(argv[5]);
                height = dimension(argv[6]);
            }
            const auto result = linux_office::writePreview(argv[2], argv[3], argc >= 5 ? argv[4] : "", width, height);
            if (result.ok()) return 0;
            std::cerr << result.message << '\n';
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
        }
        return 1;
    }
    // LibreOffice keeps the read end of a pipe. Closing this window releases
    // the write end even if another document is still open in this process.
    int officeFd = -1;
    std::string appId = "org.moleculestudio.Native";
    if (argc == 3 && std::strcmp(argv[1], "--edit-office-object") == 0) {
        gchar* path = g_canonicalize_filename(argv[2], nullptr);
        gchar* digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path, -1);
        appId += ".office.o" + std::string(digest);
        g_free(digest);
        g_free(path);
        if (const char* value = g_getenv("MOLECULESTUDIO_OFFICE_FD")) {
            char* end = nullptr;
            const long fd = std::strtol(value, &end, 10);
            if (*value && !*end && fd >= 3 && fd <= G_MAXINT &&
                    fcntl(int(fd), F_SETFD, FD_CLOEXEC) == 0) officeFd = int(fd);
            g_unsetenv("MOLECULESTUDIO_OFFICE_FD");
        }
        argv[1] = argv[2];
        argv[2] = nullptr;
        argc = 2;
    }
    linux_tlc::loadDefaults();
    auto* app = gtk_application_new(appId.c_str(), G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "startup", G_CALLBACK(+[](GtkApplication*, gpointer) {
        linux_i18n::applyDirection();
        std::string failure;
        if (!linux_skin::applyStylesheet(linux_skin::Settings::load(), failure)) g_warning("%s", failure.c_str());
    }), nullptr);
    g_signal_connect(app, "activate", G_CALLBACK(+[](GtkApplication* app, gpointer){
        auto* current = gtk_application_get_active_window(app);
        if (current) gtk_window_present(current);
        else showDocument(app);
    }), nullptr);
    g_signal_connect(app, "open", G_CALLBACK(+[](GtkApplication* app, GFile** files, gint count, gchar*, gpointer data){
        for (int i = 0; i < count; ++i) {
            gchar* path = g_file_get_path(files[i]);
            if (path) {
                auto* window = showDocument(app, path);
                auto& fd = *static_cast<int*>(data);
                if (fd >= 0) {
                    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer arg) {
                        close(GPOINTER_TO_INT(arg));
                    }), GINT_TO_POINTER(fd));
                    fd = -1;
                }
            }
            g_free(path);
        }
    }), &officeFd);
    const int result = g_application_run(G_APPLICATION(app), argc, argv);
    if (officeFd >= 0) close(officeFd);
    g_object_unref(app);
    return result;
}
