#pragma once

// The two native shells consume the same button order, groups and original glyphs.
namespace linux_ribbon {
struct Button {
    std::string id, label, icon, cmd;
    int arg;
    bool enabled = true, iconOnly = false;
    Button(std::string i, std::string l, std::string image, std::string command = {}, int a = 0)
        : id(i), label(l), icon(image), cmd(command), arg(a) {}
};
struct Group { std::string name; std::vector<Button> items; };
#include "../ribbon_home.hpp"
#include "../ribbon_tabs.hpp"

struct Glyphs {
    GdkPixbuf* atlas = nullptr;
    std::map<std::string,int> cells;
    Glyphs() {
        const auto dir = assets() + "/assets/ribbon/";
        atlas = gdk_pixbuf_new_from_file((dir+"glyphs.png").c_str(), nullptr);
        std::ifstream names(dir+"glyphs.txt");
        std::string name;
        for (int i=0; std::getline(names,name); ++i) {
            if (!name.empty() && name.back()=='\r') name.pop_back();
            cells.emplace(name,i);
        }
    }
    ~Glyphs() { if (atlas) g_object_unref(atlas); }
    GtkWidget* image(const Button& b, int side) const {
        auto found=cells.find(b.icon);
        if (!atlas || found==cells.end()) return gtk_label_new(linux_i18n::tr(b.label.c_str()));
        const int i=found->second;
        auto* tile=gdk_pixbuf_new_subpixbuf(atlas,64*(i%16),64*(i/16),64,64);
        auto* widget=gtk_drawing_area_new();
        gtk_widget_set_size_request(widget,side,side);
        gtk_widget_set_halign(widget,GTK_ALIGN_CENTER);
        gtk_widget_set_valign(widget,GTK_ALIGN_CENTER);
        g_object_set_data_full(G_OBJECT(widget),"glyph",tile,g_object_unref);
        g_signal_connect(widget,"draw",G_CALLBACK(+[](GtkWidget* w,cairo_t* cr,gpointer)->gboolean {
            auto* p=GDK_PIXBUF(g_object_get_data(G_OBJECT(w),"glyph"));
            cairo_scale(cr,gtk_widget_get_allocated_width(w)/64.,gtk_widget_get_allocated_height(w)/64.);
            gdk_cairo_set_source_pixbuf(cr,p,0,0);
            cairo_pattern_set_filter(cairo_get_source(cr),CAIRO_FILTER_BEST);
            cairo_paint(cr);
            return TRUE;
        }),nullptr);
        return widget;
    }
};

inline std::string action(const Button& b) {
    if(b.cmd.empty() || b.cmd[0]!='@') return {};
    auto id=b.cmd.substr(1);
    static const std::map<std::string,std::string> aliases={
        {"all","selectAll"},{"font","textFont"},{"color","textColor"},{"merge","mergeSelection"},
        {"rotate","rotateSelection"},{"front","stackFront"},{"back","stackBack"},
        {"ai","assistant"},{"mechanism","mechanismArrowTool"},
        {"mechanismFishhook","fishhookTool"},{"mechanismNu","mechanismNucleophile"},
        {"mechanismLonePair","lonePair"},{"mechanismRadical","radical"},{"mechanismNext","mechanismStep"},
        {"customTemplates","openTemplate"},{"addCustomTemplate","saveTemplate"},{"importCustomTemplate","importTemplate"},
        {"exportCustomTemplate","exportTemplate"},{"otherRings","templates"},{"bioComponents","templates"},
        {"inorganicReagents","templates"},{"organicReagents","templates"},{"shapeFill","shapeFill"},
        {"table:insert","insertTable"},{"table:insertThree","insertThreeLineTable"},{"table:edit","editTable"}
    };
    if(id=="align") return std::array<const char*,6>{"alignLeft","alignHorizontal","alignRight","alignTop","alignVertical","alignBottom"}.at(b.arg);
    if(id=="textalign") return std::array<const char*,4>{"textLeft","textCenter","textRight","textJustify"}.at(b.arg);
    if(id=="catalytic") return std::array<const char*,5>{"catalyticClockwise","catalyticCounterclockwise","catalyticReagent","catalyticProduct","catalyticMetal"}.at(b.arg);
    if(id=="flag") {
        static const std::map<int,std::string> flags={{kBold,"bold"},{kItalic,"italic"},{kUnderline,"underline"},{kFormula,"textFormula"},{kSubscript,"textSubscript"},{kSuperscript,"textSuperscript"}};
        return flags.at(b.arg);
    }
    if(id=="flip") return b.arg?"flipVertical":"flipHorizontal";
    if(id=="size") return b.arg>0?"textSizeUp":"textSizeDown";
    if(id=="distribute") return b.arg?"distributeVertical":"distributeHorizontal";
    if(id=="partialCharge") return b.arg>0?"partialPositive":"partialNegative";
    if(id=="mechanismPositive") return b.arg?"circledPositiveCharge":"positiveCharge";
    if(id=="mechanismNegative") return b.arg?"circledNegativeCharge":"negativeCharge";
    auto found=aliases.find(id);
    return found==aliases.end()?id:found->second;
}
inline void bind(GtkWidget* widget,std::function<void()> fn) {
    using Callback=std::function<void()>;
    g_signal_connect_data(widget,"clicked",G_CALLBACK(+[](GtkButton* button,gpointer data) {
        if(auto* popup=gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_POPOVER))
            gtk_popover_popdown(GTK_POPOVER(popup));
        (*static_cast<Callback*>(data))();
    }),new Callback(std::move(fn)),+[](gpointer data,GClosure*) {delete static_cast<Callback*>(data);},GConnectFlags(0));
}
inline void style(GtkWidget* widget,const char* name) {
    gtk_style_context_add_class(gtk_widget_get_style_context(widget),name);
}
inline GtkWidget* label(const char* text) {
    return linux_i18n::bindLabel(gtk_label_new(nullptr),text);
}
inline GtkWidget* generationIcon(int kind) {
    auto* area=gtk_drawing_area_new();
    gtk_widget_set_size_request(area,28,22);
    g_signal_connect(area,"draw",G_CALLBACK(+[](GtkWidget* area,cairo_t* cr,gpointer data)->gboolean {
        int kind=GPOINTER_TO_INT(data);
        GdkRGBA ink; gtk_style_context_get_color(gtk_widget_get_style_context(area),gtk_widget_get_state_flags(area),&ink);
        gdk_cairo_set_source_rgba(cr,&ink);
        cairo_translate(cr,gtk_widget_get_allocated_width(area)*.5,gtk_widget_get_allocated_height(area)*.5);
        cairo_set_line_width(cr,1.5); cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND); cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
        if (!kind) {
            cairo_move_to(cr,-11,0); cairo_line_to(cr,11,0); cairo_line_to(cr,6,-4);
            cairo_move_to(cr,11,0); cairo_line_to(cr,6,4);
        } else {
            const double right=kind==2?3:11;
            const double left=kind==3?-3:-11;
            cairo_move_to(cr,-11,-3); cairo_line_to(cr,right,-3); cairo_line_to(cr,right-4,-6);
            cairo_move_to(cr,11,3); cairo_line_to(cr,left,3); cairo_line_to(cr,left+4,6);
        }
        cairo_stroke(cr);return TRUE;
    }),GINT_TO_POINTER(kind));
    return area;
}

inline GtkWidget* arrowPreview(Window& window, const arrows::PresetSpec* fixed, int width, int height) {
    auto* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area,width,height);
    g_object_set_data(G_OBJECT(area),"preview-preset",const_cast<arrows::PresetSpec*>(fixed));
    g_signal_connect(area,"draw",G_CALLBACK(+[](GtkWidget* area,cairo_t* cr,Window* window)->gboolean {
        const auto* preset=static_cast<const arrows::PresetSpec*>(g_object_get_data(G_OBJECT(area),"preview-preset"));
        if (!preset) preset=window->arrowStyle;
        auto params=arrows::resolvePreset(preset->presetId,Vec2(0,0),Vec2(72,0),.6);
        arrows::ArrowGeometry geometry;
        if (!arrows::buildArrowGeometry(params,.6,arrows::Tessellation(),&geometry).ok() || !geometry.valid)
            return FALSE;
        const auto size=geometry.bounds.max-geometry.bounds.min;
        const auto center=(geometry.bounds.min+geometry.bounds.max)*.5;
        const auto width=gtk_widget_get_allocated_width(area);
        const auto height=gtk_widget_get_allocated_height(area);
        const auto scale=std::min((width-4.)/std::max(1.,size.x),(height-4.)/std::max(1.,size.y));
        if (scale<=0) return FALSE;
        GdkRGBA color;
        gtk_style_context_get_color(gtk_widget_get_style_context(area),gtk_widget_get_state_flags(area),&color);
        gdk_cairo_set_source_rgba(cr,&color);
        cairo_translate(cr,width*.5,height*.5);
        cairo_scale(cr,scale,scale);
        cairo_translate(cr,-center.x,-center.y);
        cairo_set_line_width(cr,std::max(1./scale,geometry.strokeWidth));
        cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
        auto path=[&](const std::vector<Vec2>& points) {
            if (points.empty()) return;
            cairo_move_to(cr,points.front().x,points.front().y);
            for (size_t i=1;i<points.size();++i) cairo_line_to(cr,points[i].x,points[i].y);
        };
        if (geometry.dashOn>0) {
            double dashes[2];dashes[0]=geometry.dashOn;dashes[1]=geometry.dashOff;
            cairo_set_dash(cr,dashes,2,0);
        }
        for (const auto& line:geometry.shafts) path(line);
        cairo_stroke(cr);
        cairo_set_dash(cr,nullptr,0,0);
        for (const auto& line:geometry.markers) path(line);
        cairo_stroke(cr);
        for (const auto& head:geometry.heads) {
            if (!head.outline.empty()) {
                path(head.outline);cairo_close_path(cr);
                if (head.filled) cairo_fill(cr); else cairo_stroke(cr);
            }
            for (size_t i=1;i<head.strokes.size();i+=2) {
                cairo_move_to(cr,head.strokes[i-1].x,head.strokes[i-1].y);
                cairo_line_to(cr,head.strokes[i].x,head.strokes[i].y);
            }
            cairo_stroke(cr);
        }
        return TRUE;
    }),&window);
    return area;
}

inline void install(Window& w,GtkWidget* tabs,GtkWidget* actions,GtkWidget* pages,GtkWidget* tools,GtkWidget* ions) {
    static const Glyphs glyphs;
    const char* names[]={"开始","图形","文字","AI","表格","模板","机理绘制"};
    const std::vector<std::vector<Group>> layout={makeTools(),makeExpandedTools(false),makeExpandedTools(true),
        makeAITools(),makeTableTools(),makeTemplateTools(),makeMechanismTools()};
    GSList* radios=nullptr;
    auto create=[&](GtkWidget* grid,const Button& b,int index,bool captions,int glyphSide,bool aiCard=false) {
        const auto key=action(b);
        std::function<void()> run;
        const bool arrowMenu = b.cmd == "@arrows";
        if (b.id == "chooseArrowTool_Large_Solid")
            run = [&w]{ w.activateShortcut(linux_shortcuts::shortcut("arrowTool")); };
        else if(!b.cmd.empty() && b.cmd[0]!='@') {
            for(size_t i=0;i<w.session.entries.size();++i)
                if(w.session.entries[i].command==b.cmd && w.session.entries[i].limitation.empty()){
                    run=[&w,i]{gtk_combo_box_set_active_id(GTK_COMBO_BOX(w.tools),std::to_string(i).c_str());};
                    break;
                }
        } else if(b.cmd.compare(0,7,"@table:")==0 && key.compare(0,6,"table:")==0)
            run=[&w,op=b.cmd.substr(7),title=b.label]{w.tableAction(op,title);};
        else if(b.cmd=="@aiSettings") run=[&w]{w.chooseSettings(true);};
        else if(b.cmd=="@otherRings") run=[&w]{w.templateLibrary(Window::Library::Rings);};
        else if(b.cmd=="@bioComponents") run=[&w]{w.templateLibrary(Window::Library::Bio);};
        else if(b.cmd=="@inorganicReagents") run=[&w]{w.templateLibrary(Window::Library::Inorganic);};
        else if(b.cmd=="@organicReagents") run=[&w]{w.templateLibrary(Window::Library::Organic);};
        else if(b.cmd=="@textTidy") run=[&w]{w.tidyText(false);};
        else if(b.cmd=="@autoTextLayout") run=[&w]{gtk_menu_item_activate(GTK_MENU_ITEM(w.automaticLayout));};
        else if(b.cmd=="@projection") run=[&w,k=b.arg]{w.chooseProjection(k==0?stereo_projection::Fischer:k==1?stereo_projection::Sawhorse:stereo_projection::Newman,0);};
        else if(b.cmd=="@generationArrowStyle") run=[&w,k=b.arg]{w.chooseGenerationStyle(k);};
        else if(b.cmd=="@commonIons") run=[ions]{gtk_menu_popup_at_pointer(GTK_MENU(ions),nullptr);};
        else for(auto& shortcut:linux_shortcuts::catalog()) if(shortcut.id==key) {
            run=[&w,s=&shortcut]{w.activateShortcut(*s);};break;
        }
        const bool fill = b.cmd=="@shapeFill";
        const bool shapes = b.cmd=="@shapes";
        if((!run && !arrowMenu && !shapes && !fill) || (!b.enabled && b.cmd!="@addCustomTemplate")) return false;
        const bool generation=b.cmd=="@generationArrowStyle";
        auto* button=(arrowMenu || shapes || fill) ? gtk_menu_button_new() : generation ? gtk_toggle_button_new() : gtk_button_new();
        if (generation) {
            style(button,"generation-style");
            w.generationButtons[b.arg].push_back(GTK_TOGGLE_BUTTON(button));
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),w.generationArrowStyle==b.arg);
        }
        if (b.cmd=="@addCustomTemplate") g_object_bind_property(w.saveTemplateItem,"sensitive",button,"sensitive",G_BINDING_SYNC_CREATE);
        if (b.cmd == "@otherRings") {
            auto* hold = gtk_gesture_long_press_new(button);
            gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(hold), GTK_PHASE_CAPTURE);
            gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(hold), GDK_BUTTON_PRIMARY);
            g_object_set_data_full(G_OBJECT(button), "ring-long-press", hold, g_object_unref);
            g_signal_connect(hold, "pressed", G_CALLBACK(+[](GtkGestureLongPress* gesture, double, double, Window* owner) {
                gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
                owner->templateLibrary(Window::Library::Rings);
            }), &w);
            g_signal_connect(button, "button-release-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, Window* owner) -> gboolean {
                if (event->button != GDK_BUTTON_SECONDARY) return FALSE;
                owner->templateLibrary(Window::Library::Rings);
                return TRUE;
            }), &w);
        }
        gtk_widget_set_focus_on_click(button,FALSE);
        style(button,"ribbon-tool");
        auto* inside=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
        if(aiCard){
            const bool primary=b.id=="ai.open";
            auto* content=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
            gtk_box_pack_start(GTK_BOX(content),inside,FALSE,FALSE,0);
            auto* detail=label(primary?"独立窗口":"API、模型与预设提示词");
            gtk_label_set_xalign(GTK_LABEL(detail),0);
            gtk_box_pack_start(GTK_BOX(content),detail,FALSE,FALSE,0);
            gtk_container_add(GTK_CONTAINER(button),content);
            gtk_widget_set_size_request(button,-1,76);
            gtk_widget_set_hexpand(button,TRUE);
            style(button,primary?"ai-ribbon-primary":"ai-ribbon-settings");
            gtk_box_set_spacing(GTK_BOX(inside),9);
        }else gtk_container_add(GTK_CONTAINER(button),inside);
        const bool currentArrow = arrowMenu || b.id == "chooseArrowTool_Large_Solid";
        auto* icon = currentArrow ? arrowPreview(w,nullptr,arrowMenu ? 72 : 24,24) :
            b.cmd=="@generationArrowStyle" ? generationIcon(b.arg) : glyphs.image(b,b.iconOnly?24:glyphSide);
        gtk_box_pack_start(GTK_BOX(inside),icon,FALSE,FALSE,0);
        if (currentArrow) w.arrowButtons.push_back(button);
        if(captions && !b.iconOnly) gtk_box_pack_start(GTK_BOX(inside),label(b.label.c_str()),FALSE,FALSE,0);
        gtk_widget_set_tooltip_text(button,linux_i18n::tr(b.label.c_str()));
        g_object_set_data_full(G_OBJECT(button),"ribbon-label",g_strdup(b.label.c_str()),g_free);
        g_object_set_data_full(G_OBJECT(button),"ribbon-command",g_strdup(b.cmd.c_str()),g_free);
        g_object_set_data_full(G_OBJECT(button),"ribbon-action",g_strdup(key.c_str()),g_free);
        g_signal_connect(button,"state-flags-changed",G_CALLBACK(+[](GtkWidget* item,GtkStateFlags previous,Window* w){
            const auto state=gtk_widget_get_state_flags(item);
            if (!((state ^ previous) & GTK_STATE_FLAG_PRELIGHT)) return;
            if (state & GTK_STATE_FLAG_PRELIGHT) {
                w->hoveredRibbonTool=item;
                if (!w->statusPriority) gtk_label_set_text(GTK_LABEL(w->status),w->ribbonCaption(item).c_str());
            } else w->leaveRibbonTool(item);
        }),&w);
        g_signal_connect(button,"unmap",G_CALLBACK(+[](GtkWidget* item,Window* w){
            w->leaveRibbonTool(item);
        }),&w);
        atk_object_set_name(gtk_widget_get_accessible(button),linux_i18n::tr(b.label.c_str()));
        GtkWidget* cell=button;
        if(b.cmd=="chooseTLCPlateTool"){
            cell=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
            gtk_box_pack_start(GTK_BOX(cell),button,TRUE,TRUE,0);
            auto* more=gtk_menu_button_new();
            atk_object_set_name(gtk_widget_get_accessible(more),tr("薄层板点样设置"));
            gtk_box_pack_start(GTK_BOX(cell),more,FALSE,FALSE,0);
            auto* menu=gtk_menu_new();
            auto add=[&](const char* title,std::function<void()> callback){
                auto* item=linux_i18n::bindLabel(gtk_menu_item_new_with_label(""),title);
                using Callback=std::function<void()>;
                g_signal_connect_data(item,"activate",G_CALLBACK(+[](GtkWidget*,gpointer data){(*static_cast<Callback*>(data))();}),
                    new Callback(std::move(callback)),+[](gpointer data,GClosure*){delete static_cast<Callback*>(data);},G_CONNECT_DEFAULT);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);return item;
            };
            add("绘制薄层板",run);
            add("新建薄层板的点样设置…",[&w]{w.tlcSettings();});
            auto* edit=add("编辑选中的薄层板…",[&w]{
                for(auto ref:w.session.selected)if(experimental::isFigureObject(w.session.doc,ref)){w.tlcSettings(ref.id);break;}
            });
            g_object_set_data(G_OBJECT(menu),"edit-plate",edit);
            g_signal_connect(menu,"show",G_CALLBACK(+[](GtkWidget* menu,Window* w){
                bool selected=false;
                for(auto ref:w->session.selected)if(experimental::isFigureObject(w->session.doc,ref)){selected=true;break;}
                gtk_widget_set_sensitive(GTK_WIDGET(g_object_get_data(G_OBJECT(menu),"edit-plate")),selected);
            }),&w);
            gtk_widget_show_all(menu);gtk_menu_button_set_popup(GTK_MENU_BUTTON(more),menu);
        }
        gtk_grid_attach(GTK_GRID(grid),cell,aiCard?index:index/3,aiCard?0:index%3,1,1);
        if(!currentArrow && !b.cmd.empty() && b.cmd[0]!='@')w.ribbonTools[b.cmd].push_back(button);
        if (fill) {
            auto* menu = gtk_menu_new();
            const char* labels[] = {"无填充","浅青色","浅蓝色","浅橙色","自定义颜色…"};
            for (unsigned i=0;i<5;++i) {
                auto* item = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""),labels[i]);
                g_object_set_data(G_OBJECT(item),"fill-choice",GUINT_TO_POINTER(i));
                g_signal_connect(item,"activate",G_CALLBACK(+[](GtkWidget* item,Window* w){
                    w->changeShapeFill(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"fill-choice")));
                }),&w);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
            }
            gtk_widget_show_all(menu);
            gtk_menu_button_set_popup(GTK_MENU_BUTTON(button),menu);
        } else if (shapes) {
            auto* menu = gtk_menu_new();
            std::map<std::string,GtkWidget*> categories;
            for (const auto& shape : presentationCatalog()) {
                auto*& group = categories[shape.category];
                if (!group) {
                    auto* item = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""),shape.category.c_str());
                    group = gtk_menu_new();
                    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),group);
                    gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
                }
                auto* item = gtk_menu_item_new();
                auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
                auto* preview = gtk_drawing_area_new();
                gtk_widget_set_size_request(preview,46,24);
                g_signal_connect(preview,"draw",G_CALLBACK(+[](GtkWidget* area,cairo_t* cr,const PresentationShape* spec)->gboolean{
                    if (spec->points.empty()) return FALSE;
                    const double width = gtk_widget_get_allocated_width(area)-8;
                    const double height = gtk_widget_get_allocated_height(area)-6;
                    cairo_move_to(cr,4+spec->points[0].x*width,3+spec->points[0].y*height);
                    for (size_t i=1;i<spec->points.size();++i)
                        cairo_line_to(cr,4+spec->points[i].x*width,3+spec->points[i].y*height);
                    cairo_close_path(cr);
                    cairo_set_source_rgb(cr,220./255,239./255,233./255);
                    cairo_fill_preserve(cr);
                    cairo_set_source_rgb(cr,.13,.40,.37);
                    cairo_set_line_width(cr,1.2);
                    cairo_stroke(cr);
                    return TRUE;
                }),const_cast<PresentationShape*>(&shape));
                gtk_container_add(GTK_CONTAINER(item),row);
                gtk_box_pack_start(GTK_BOX(row),preview,FALSE,FALSE,0);
                gtk_box_pack_start(GTK_BOX(row),label(shape.title.c_str()),FALSE,FALSE,0);
                linux_i18n::bindIconLabel(item,shape.title.c_str());
                g_object_set_data(G_OBJECT(item),"shape",const_cast<PresentationShape*>(&shape));
                g_signal_connect(item,"activate",G_CALLBACK(+[](GtkWidget* item,Window* w){
                    w->cancelShape();
                    w->pendingTemplate.reset();
                    w->cancelMechanismPlacement();
                    w->session.cancel();
                    w->session.activateCommand("chooseSelectionTool_Marquee");
                    w->pendingShape = static_cast<const PresentationShape*>(g_object_get_data(G_OBJECT(item),"shape"));
                    w->session.status = "插入图案 · 在画布上拖出大小（单击使用默认大小），Esc 取消";
                    w->report(Error());
                    gtk_widget_grab_focus(w->canvas);
                }),&w);
                gtk_menu_shell_append(GTK_MENU_SHELL(group),item);
            }
            gtk_widget_show_all(menu);
            gtk_menu_button_set_popup(GTK_MENU_BUTTON(button),menu);
        } else if (arrowMenu) {
            auto* menu = gtk_menu_new();
            gtk_widget_set_name(menu,"arrow-palette");
            auto category = [&](const char* name) {
                auto* group = linux_i18n::bindLabel(gtk_menu_item_new_with_label(""), name);
                auto* sub = gtk_menu_new();
                gtk_widget_set_name(sub,"arrow-palette");
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(group),sub);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu),group);
                return sub;
            };
            auto preset = [&](GtkWidget* parent, unsigned id, const char* name) {
                const auto* style = arrows::presetById(id);
                if (!style || !style->implemented) return;
                auto* item = gtk_menu_item_new();
                auto* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
                gtk_container_add(GTK_CONTAINER(item),row);
                if (parent==menu) {
                    gtk_box_pack_start(GTK_BOX(row),label(name),FALSE,FALSE,0);
                    gtk_box_pack_end(GTK_BOX(row),arrowPreview(w,style,38,18),FALSE,FALSE,0);
                } else {
                    gtk_box_pack_start(GTK_BOX(row),arrowPreview(w,style,68,22),FALSE,FALSE,0);
                    gtk_box_pack_start(GTK_BOX(row),label(name),FALSE,FALSE,0);
                }
                linux_i18n::bindIconLabel(item,name);
                g_object_set_data(G_OBJECT(item),"preset",GUINT_TO_POINTER(id));
                g_signal_connect(item,"activate",G_CALLBACK(+[](GtkWidget* item,Window* w) {
                    auto id=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"preset"));
                    w->chooseArrowStyle(*arrows::presetById(id));
                }),&w);
                gtk_menu_shell_append(GTK_MENU_SHELL(parent),item);
            };
            auto* reaction = category("反应箭头");
            preset(reaction,8,"反应箭头"); preset(reaction,10,"虚线箭头"); preset(reaction,40,"逆合成箭头");
            preset(menu,58,"弧线箭头");
            auto* equilibrium = category("平衡与共振");
            preset(equilibrium,38,"平衡箭头"); preset(equilibrium,39,"不等长平衡箭头"); preset(equilibrium,13,"共振双向箭头");
            auto* more = category("更多样式");
            preset(more,9,"粗箭头"); preset(more,32,"空心箭头"); preset(more,11,"左半头箭头");
            preset(more,12,"右半头箭头"); preset(more,14,"禁止反应（交叉）"); preset(more,15,"禁止反应（双斜线）");
            auto* mechanism = category("机理箭头");
            preset(mechanism,49,"机理箭头"); preset(mechanism,57,"机理鱼钩箭头");
            auto* sizes = category("箭头头部大小（非机理）");
            for (int tier=0;tier<5;++tier) {
                auto* item=linux_i18n::bindLabel(gtk_menu_item_new_with_label(""),arrows::headTierName(tier));
                g_object_set_data(G_OBJECT(item),"head-tier",GINT_TO_POINTER(tier));
                g_signal_connect(item,"activate",G_CALLBACK(+[](GtkWidget* item,Window* w) {
                    w->setArrowHeadTier(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"head-tier")));
                }),&w);
                gtk_menu_shell_append(GTK_MENU_SHELL(sizes),item);
            }
            gtk_widget_show_all(menu);
            gtk_menu_button_set_popup(GTK_MENU_BUTTON(button),menu);
        } else if (generation) {
            g_object_set_data(G_OBJECT(button),"generation-index",GINT_TO_POINTER(b.arg));
            g_signal_connect(button,"clicked",G_CALLBACK(+[](GtkWidget* button,Window* w){
                if (w->syncingGenerationStyle) return;
                w->chooseGenerationStyle(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"generation-index")));
                if (auto* popup=gtk_widget_get_ancestor(button,GTK_TYPE_POPOVER)) gtk_popover_popdown(GTK_POPOVER(popup));
            }),&w);
        } else bind(button,run);
        return true;
    };
    struct FoldGroup {
        GtkWidget *stack, *expanded, *grid, *popover, *more, *caption;
        std::vector<GtkWidget*> direct;
        size_t page;
        void compact(bool enabled, int visible=1, bool showMenu=true) const {
            gtk_widget_set_visible(more,showMenu);
            for(size_t i=0;i<direct.size();++i)
                gtk_widget_set_visible(direct[i],int(i)<visible);
            auto* target=enabled?popover:expanded;
            if(gtk_widget_get_parent(grid)!=target){
                g_object_ref(grid);
                gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(grid)),grid);
                gtk_container_add(GTK_CONTAINER(target),grid);
                g_object_unref(grid);
            }
            gtk_stack_set_visible_child_name(GTK_STACK(stack),enabled?"compact":"expanded");
        }
    };
    auto foldGroups=std::make_shared<std::vector<FoldGroup>>();
    auto compactMode=std::make_shared<bool>(false);
    std::vector<GtkWidget*> pageScrolls;
    auto foldable=[&](GtkWidget* grid,GtkWidget* first,GtkWidget* caption,const std::string& title,size_t page,int count){
        auto* stack=gtk_stack_new();
        gtk_stack_set_homogeneous(GTK_STACK(stack),FALSE);
        auto* expanded=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
        gtk_box_pack_start(GTK_BOX(expanded),grid,page==3,page==3,0);
        gtk_stack_add_named(GTK_STACK(stack),expanded,"expanded");
        auto* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
        gtk_container_add(GTK_CONTAINER(row),first);
        auto* more=gtk_menu_button_new();
        style(more,"ribbon-tool");
        {
            style(more,"home-group-menu");
            auto* chevron=gtk_drawing_area_new();
            gtk_widget_set_size_request(chevron,16,16);
            gtk_container_add(GTK_CONTAINER(more),chevron);
            atk_object_set_role(gtk_widget_get_accessible(chevron),ATK_ROLE_REDUNDANT_OBJECT);
            g_signal_connect(chevron,"draw",G_CALLBACK(+[](GtkWidget* widget,cairo_t* cr,gpointer)->gboolean{
                GdkRGBA color;
                gtk_style_context_get_color(gtk_widget_get_style_context(widget),gtk_widget_get_state_flags(widget),&color);
                gdk_cairo_set_source_rgba(cr,&color);
                cairo_translate(cr,gtk_widget_get_allocated_width(widget)*.5,gtk_widget_get_allocated_height(widget)*.5);
                cairo_set_line_width(cr,1.5);
                cairo_move_to(cr,-3,-2);cairo_line_to(cr,0,1);cairo_line_to(cr,3,-2);cairo_stroke(cr);
                return TRUE;
            }),nullptr);
            gtk_widget_set_valign(more,GTK_ALIGN_START);
        }
        atk_object_set_name(gtk_widget_get_accessible(more),tr(title.c_str()));
        gtk_widget_set_tooltip_text(more,tr(title.c_str()));
        gtk_widget_show_all(more);
        gtk_widget_set_no_show_all(more,TRUE);
        auto* popover=gtk_popover_new(more);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(more),popover);
        gtk_container_add(GTK_CONTAINER(row),more);
        gtk_stack_add_named(GTK_STACK(stack),row,"compact");
        std::vector<GtkWidget*> direct;
        for(int i=0;i<count;++i){
            auto* cell=gtk_grid_get_child_at(GTK_GRID(first),i/3,i%3);
            gtk_widget_show_all(cell);
            gtk_widget_set_no_show_all(cell,TRUE);
            direct.push_back(cell);
        }
        foldGroups->push_back({stack,expanded,grid,popover,more,caption,std::move(direct),page});
        return stack;
    };
    for(size_t page=0;page<layout.size();++page) {
        const auto key=std::to_string(page);
        auto* tab=gtk_radio_button_new(radios);
        radios=gtk_radio_button_get_group(GTK_RADIO_BUTTON(tab));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(tab),FALSE);
        gtk_container_add(GTK_CONTAINER(tab),label(names[page]));
        gtk_widget_set_valign(tab,GTK_ALIGN_CENTER);
        gtk_widget_set_size_request(tab,-1,28);
        style(tab,"ribbon-tab");
        style(tab,"ribbon-page");
        if(page==3)gtk_widget_set_margin_start(tab,6);
        gtk_box_pack_start(GTK_BOX(page==3?actions:tabs),tab,FALSE,FALSE,0);
        bind(tab,[pages,key]{gtk_stack_set_visible_child_name(GTK_STACK(pages),key.c_str());});
        auto* scroll=gtk_scrolled_window_new(nullptr,nullptr);
        pageScrolls.push_back(scroll);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_NEVER);
        gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll),TRUE);
        auto* band=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,page==0?4:3);
        gtk_container_add(GTK_CONTAINER(scroll),band);
        auto* pageRow=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
        gtk_box_pack_start(GTK_BOX(pageRow),scroll,TRUE,TRUE,0);
        gtk_stack_add_named(GTK_STACK(pages),pageRow,key.c_str());
        for(const auto& group:layout[page]) {
            auto* card=gtk_box_new(GTK_ORIENTATION_VERTICAL,1);
            auto* grid=gtk_grid_new();
            if(page==3){
                gtk_grid_set_column_homogeneous(GTK_GRID(grid),TRUE);
                gtk_grid_set_column_spacing(GTK_GRID(grid),12);
                gtk_widget_set_hexpand(grid,TRUE);
            }
            int count=0;
            auto* first=gtk_grid_new();
            const int glyphSide=page==0?17:18;
            for(const auto& b:group.items) if(create(grid,b,count,page!=0,glyphSide,page==3)){
                create(first,b,count,page==6 && group.name=="生成产物",glyphSide);
                ++count;
            }
            auto* caption=label(group.name.c_str());
            gtk_box_pack_start(GTK_BOX(card),foldable(grid,first,caption,group.name,page,count),TRUE,FALSE,0);
            gtk_box_pack_end(GTK_BOX(card),caption,FALSE,FALSE,0);
            if(!count){foldGroups->pop_back();gtk_widget_destroy(card);continue;}
            gtk_widget_set_valign(card,GTK_ALIGN_START);
            style(card,"ribbon-card");
            if(page==0)style(card,"home-card");
            gtk_box_pack_start(GTK_BOX(band),card,page==3,page==3,0);
        }
        if (page == 2) {
            auto* fields = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
            gtk_widget_set_valign(fields,GTK_ALIGN_START);
            gtk_box_pack_end(GTK_BOX(pageRow),fields,FALSE,FALSE,4);
            auto addField = [&](GtkWidget*& entry, const char* name, int width,
                                std::initializer_list<const char*> choices) {
                auto* combo = gtk_combo_box_text_new_with_entry();
                for (auto choice : choices) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),choice);
                entry = gtk_bin_get_child(GTK_BIN(combo));
                gtk_entry_set_width_chars(GTK_ENTRY(entry),width);
                gtk_entry_set_max_width_chars(GTK_ENTRY(entry),width);
                atk_object_set_name(gtk_widget_get_accessible(entry),tr(name));
                gtk_box_pack_start(GTK_BOX(fields),combo,FALSE,FALSE,0);
                g_signal_connect(combo,"changed",G_CALLBACK(+[](GtkComboBox* box,Window* owner){
                    if (gtk_combo_box_get_active(box) >= 0)
                        owner->applyTypographyEntry(gtk_bin_get_child(GTK_BIN(box)));
                }),&w);
                g_signal_connect(entry,"activate",G_CALLBACK(+[](GtkEntry* field,Window* owner){
                    owner->applyTypographyEntry(GTK_WIDGET(field));
                }),&w);
                g_signal_connect(entry,"key-press-event",G_CALLBACK(+[](GtkWidget*,GdkEventKey* key,Window* owner)->gboolean{
                    if (key->keyval != GDK_KEY_Escape) return FALSE;
                    gtk_widget_grab_focus(owner->captionEditor ? owner->captionEditor->view() : owner->canvas);
                    owner->syncTypography();
                    return TRUE;
                }),&w);
            };
            addField(w.fontEntry,"字体",12,{"Arial","Times New Roman","Calibri","Courier New","Microsoft YaHei","Symbol"});
            addField(w.sizeEntry,"字号",3,{"8","9","10","11","12","14","16","18","20","24","28","32","36","48","72"});
            g_signal_connect_after(fields,"map",G_CALLBACK(+[](GtkWidget*,Window* owner){owner->syncTypography();}),&w);
        }
        if(page==0){
            auto* quick=gtk_grid_new();
            gtk_grid_set_column_homogeneous(GTK_GRID(quick),TRUE);
            gtk_grid_set_column_spacing(GTK_GRID(quick),8);
            gtk_grid_set_row_spacing(GTK_GRID(quick),6);
            gtk_widget_set_valign(quick,GTK_ALIGN_START);
            int i=0;
            for(auto id:{"structureToName","nameToStructure","cleanup","expand","collapse","textTidy","stereo","nmr","recognize"}){
                auto* b=gtk_button_new();std::string key=id;
                const auto* s=key=="textTidy"?nullptr:&linux_shortcuts::shortcut(key.c_str());
                gtk_container_add(GTK_CONTAINER(b),label(s?s->label.c_str():"文字整理"));
                bind(b,[&w,s]{if(s)w.activateShortcut(*s);else w.tidyText(false);});
                style(b,"ribbon-quick");
                style(b,"home-quick");
                gtk_grid_attach(GTK_GRID(quick),b,i%3,i/3,1,1);++i;
            }
            gtk_box_pack_end(GTK_BOX(pageRow),quick,FALSE,FALSE,6);
        }
    }
    auto* toggle=gtk_button_new();
    style(toggle,"ribbon-quick");
    auto* labels=gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(labels),label("收起"),"expanded");
    gtk_stack_add_named(GTK_STACK(labels),label("展开"),"compact");
    gtk_container_add(GTK_CONTAINER(toggle),labels);
    gtk_box_pack_start(GTK_BOX(actions),toggle,FALSE,FALSE,0);
    gtk_box_reorder_child(GTK_BOX(actions),toggle,1);
    auto layoutPage=[foldGroups,compactMode](size_t page,int width){
        std::vector<int> columns;
        int fullWidth=0;
        for(const auto& g:*foldGroups)if(g.page==page){
            columns.push_back(1);
            if(page==0){
                fullWidth+=int((g.direct.size()+2)/3)*26+11;
            }else{
                int gridWidth=0,captionWidth=0;
                gtk_widget_get_preferred_width(g.grid,nullptr,&gridWidth);
                gtk_widget_get_preferred_width(g.caption,nullptr,&captionWidth);
                fullWidth+=std::max(gridWidth,captionWidth)+10;
            }
        }
        const bool narrow=page!=3 && width>1 && width<fullWidth;
        int spare=width-int(columns.size())*(26+24+6);
        bool grew=true;
        while(narrow && !*compactMode && spare>=26 && grew){
            grew=false;
            size_t i=0;
            for(const auto& g:*foldGroups)if(g.page==page){
                if(spare>=26 && size_t(columns[i]*3)<g.direct.size()){
                    ++columns[i];spare-=26;grew=true;
                }
                ++i;
            }
        }
        size_t i=0;
        for(const auto& g:*foldGroups)if(g.page==page){
            const int visible=*compactMode?1:columns[i]*3;
            g.compact(*compactMode || narrow,visible,page==0 || *compactMode || visible<int(g.direct.size()));
            ++i;
        }
    };
    using Resize=std::function<void(int)>;
    for(size_t page=0;page<pageScrolls.size();++page){
        auto* pane=pageScrolls[page];
        g_object_set_data_full(G_OBJECT(pane),"layout-ribbon", new Resize([layoutPage,page](int width){layoutPage(page,width);}),
            +[](gpointer data){delete static_cast<Resize*>(data);});
        g_signal_connect(pane,"size-allocate",G_CALLBACK(+[](GtkWidget* scroll,GtkAllocation* a,gpointer){
            if(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(scroll),"ribbon-width"))==a->width)return;
            g_object_set_data(G_OBJECT(scroll),"ribbon-width",GINT_TO_POINTER(a->width));
            if(g_object_get_data(G_OBJECT(scroll),"ribbon-resizing"))return;
            g_object_set_data(G_OBJECT(scroll),"ribbon-resizing",GINT_TO_POINTER(1));
            gtk_widget_add_tick_callback(scroll,+[](GtkWidget* widget,GdkFrameClock*,gpointer)->gboolean {
                g_object_set_data(G_OBJECT(widget),"ribbon-resizing",nullptr);
                auto* resize=static_cast<Resize*>(g_object_get_data(G_OBJECT(widget),"layout-ribbon"));
                (*resize)(gtk_widget_get_allocated_width(widget));
                return G_SOURCE_REMOVE;
            },nullptr,nullptr);
        }),nullptr);
    }
    auto apply=[compactMode,layoutPage,pageScrolls,labels,toggle](bool compact){
        *compactMode=compact;
        for(size_t page=0;page<pageScrolls.size();++page)
            layoutPage(page,gtk_widget_get_allocated_width(pageScrolls[page]));
        gtk_stack_set_visible_child_name(GTK_STACK(labels),compact?"compact":"expanded");
        atk_object_set_name(gtk_widget_get_accessible(toggle),tr(compact?"展开":"收起"));
    };
    gtk_widget_show_all(pages);
    gtk_widget_show_all(labels);
    bool compact=linux_config::Preferences().flag("Ribbon","Compact",false);
    apply(compact);
    bind(toggle,[apply,compact,&w]() mutable {
        compact=!compact;
        apply(compact);
        std::string error;
        if(!linux_config::Preferences().flag("Ribbon","Compact",compact,&error))w.report({ErrorCode::IoError,error});
    });
    gtk_stack_set_visible_child_name(GTK_STACK(pages),"0");
}
}
