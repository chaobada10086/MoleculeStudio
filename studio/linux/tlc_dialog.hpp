#pragma once

#include "../tlc_preferences.hpp"
#include "config.hpp"
#include "i18n.hpp"
#include <gtk/gtk.h>

namespace linux_tlc {
using namespace r62::experimental;
using linux_i18n::tr;

inline void loadDefaults() {
    linux_config::Preferences settings;
    auto& prefs = studio_tlc::preferences();
    bool valid = false;
    auto figure = decodeFigure(settings.text("TLC", "Template"), &valid);
    if (!valid || figure.kind != FigureKind::TlcPlate || figure.lanes.empty()) return;
    double diameter = 4;
    if (!linux_config::parseNumber(settings.text("TLC", "Diameter").c_str(), diameter)) diameter = 4;
    prefs.diameter = std::clamp(diameter, 1., 16.);
    prefs.showRf = settings.flag("TLC", "ShowRf", false);
    prefs.plateTemplate = std::move(figure);
    prefs.customTemplate = true;
}

struct Dialog {
    PlateFigure figure;
    PlateFrame frame;
    double diameter;
    bool showRf;
    GtkWidget *dialog{}, *grid{}, *preview{}, *lane{}, *name{}, *count{}, *spot{}, *rf{}, *size{}, *tail{}, *opacity{};
    GtkWidget *guides{}, *origin{}, *front{}, *labels{};
    size_t laneIndex = 0, spotIndex = 0;
    bool updating = false, dragging = false;

    Lane& currentLane() { return figure.lanes[laneIndex]; }
    Spot* currentSpot() {
        const auto ids = studio_tlc::spotsIn(figure, currentLane().id);
        return spotIndex < ids.size() ? mutableSpot(&figure, ids[spotIndex]) : nullptr;
    }
    void row(int n, const char* text, GtkWidget* control) {
        auto* label = gtk_label_new(tr(text));
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), label, 0, n, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), control, 1, n, 1, 1);
        atk_object_set_name(gtk_widget_get_accessible(control), tr(text));
    }
    void refresh() {
        updating = true;
        laneIndex = std::min(laneIndex, figure.lanes.size()-1);
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(lane));
        for (size_t i=0; i<figure.lanes.size(); ++i)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(lane), std::to_string(i+1).c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(lane), int(laneIndex));
        gtk_entry_set_text(GTK_ENTRY(name), currentLane().label.c_str());
        const auto ids = studio_tlc::spotsIn(figure, currentLane().id);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(count), double(ids.size()));
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(spot));
        for (size_t i=0; i<ids.size(); ++i)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(spot), std::to_string(i+1).c_str());
        spotIndex = ids.empty() ? 0 : std::min(spotIndex, ids.size()-1);
        gtk_combo_box_set_active(GTK_COMBO_BOX(spot), ids.empty() ? -1 : int(spotIndex));
        auto* s = currentSpot();
        for (auto* control : {spot,rf,tail,opacity}) gtk_widget_set_sensitive(control, s != nullptr);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(rf), s ? studio_tlc::rf(figure,*s) : 0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(tail), s ? s->tailLengthV*50 : 0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(opacity), s ? s->tailOpacity*100 : 0);
        updating = false;
        gtk_widget_queue_draw(preview);
    }
    struct Board { double x,y,w,h; };
    Board board() const {
        const double w=gtk_widget_get_allocated_width(preview), h=gtk_widget_get_allocated_height(preview);
        const double scale=std::min((w-50)/(2*frame.halfWidth()),(h-60)/(2*frame.halfHeight()));
        const double bw=2*frame.halfWidth()*scale, bh=2*frame.halfHeight()*scale;
        return {(w-bw)/2,(h-bh)/2-8,bw,bh};
    }
    void paint(cairo_t* c) {
        const auto b=board();
        auto x=[&](double u){return b.x+(u+1)*b.w/2;};
        auto y=[&](double v){return b.y+(v+1)*b.h/2;};
        auto line=[&](double u,double v,double p,double q){cairo_move_to(c,x(u),y(v));cairo_line_to(c,x(p),y(q));cairo_stroke(c);};
        cairo_set_source_rgb(c,1,1,1);cairo_paint(c);
        cairo_set_source_rgb(c,.23,.26,.25);cairo_rectangle(c,b.x,b.y,b.w,b.h);cairo_stroke(c);
        if(figure.originDefined)line(-1,figure.originV,1,figure.originV);
        if(figure.frontDefined)line(-1,figure.frontV,1,figure.frontV);
        for(const auto& l:figure.lanes){
            if(figure.showLaneBorders)line(l.u,-.92,l.u,.92);
            auto* layout=gtk_widget_create_pango_layout(preview,l.label.c_str());
            int width=0;pango_layout_get_pixel_size(layout,&width,nullptr);
            cairo_move_to(c,x(l.u)-width/2.,y(1)+4);pango_cairo_show_layout(c,layout);g_object_unref(layout);
        }
        auto* selected=currentSpot();
        for(const auto& s:figure.spots){
            for(const auto& slice:studio_tlc::tailSlices(s)){
                cairo_set_source_rgba(c,.18,.18,.18,slice.opacity);
                cairo_move_to(c,x(slice.points[0].x),y(slice.points[0].y));
                for(size_t i=1;i<slice.points.size();++i)cairo_line_to(c,x(slice.points[i].x),y(slice.points[i].y));
                cairo_close_path(c);cairo_fill(c);
            }
            const auto radius=std::max(3.,s.radiusU*b.w/2);
            cairo_set_source_rgb(c,.18,.18,.18);cairo_arc(c,x(s.u),y(s.v),radius,0,2*G_PI);cairo_fill(c);
            if(selected && selected->id==s.id){cairo_set_source_rgb(c,.05,.47,.42);cairo_arc(c,x(s.u),y(s.v),radius+4,0,2*G_PI);cairo_stroke(c);}
        }
        if(showRf){
            // Lay out labels in preview pixels, including when a tiny document plate is enlarged.
            PlateFrame upright({}, {b.w/2,0}, {0,b.h/2});
            cairo_set_source_rgb(c,.18,.18,.18);
            for(const auto& label:studio_tlc::rfLabels(figure,upright,15)){
                cairo_set_font_size(c,label.size);
                cairo_move_to(c,b.x+b.w/2+label.position.x,b.y+b.h/2+label.position.y+label.size);
                cairo_show_text(c,label.text.c_str());
            }
        }
    }
    void pointer(double px,double py,bool press) {
        const auto b=board();
        if(press){
            double best=1e9;const Spot* hit=nullptr;
            for(const auto& s:figure.spots){
                const auto distance=std::hypot(px-b.x-(s.u+1)*b.w/2,py-b.y-(s.v+1)*b.h/2);
                if(distance<best && distance<=std::max(7.,s.radiusU*b.w/2+3)){hit=&s;best=distance;}
            }
            if(!hit)return;
            for(size_t i=0;i<figure.lanes.size();++i)if(figure.lanes[i].id==hit->laneId)laneIndex=i;
            const auto ids=studio_tlc::spotsIn(figure,currentLane().id);
            spotIndex=size_t(std::find(ids.begin(),ids.end(),hit->id)-ids.begin());
            dragging=true;refresh();return;
        }
        auto* s=currentSpot();if(!dragging||!s)return;
        double u=2*(px-b.x)/b.w-1,v=2*(py-b.y)/b.h-1;
        auto dest=std::min_element(figure.lanes.begin(),figure.lanes.end(),[&](const Lane& a,const Lane& z){return std::abs(a.u-u)<std::abs(z.u-u);});
        if(dest->id!=s->laneId && studio_tlc::spotsIn(figure,dest->id).size()>=30)return;
        laneIndex=size_t(dest-figure.lanes.begin());s->laneId=dest->id;s->u=dest->u;
        if(figure.originV>figure.frontV)studio_tlc::setRf(figure,*s,(figure.originV-v)/(figure.originV-figure.frontV));
        const auto ids=studio_tlc::spotsIn(figure,dest->id);spotIndex=size_t(std::find(ids.begin(),ids.end(),s->id)-ids.begin());
        refresh();
    }
    bool run(GtkWindow* parent) {
        if(figure.lanes.empty())studio_tlc::addLane(figure);
        studio_tlc::assignUnowned(figure);
        dialog=gtk_dialog_new_with_buttons(tr("薄层板点样设置"),parent,GTK_DIALOG_MODAL,tr("取消"),GTK_RESPONSE_CANCEL,tr("应用"),GTK_RESPONSE_ACCEPT,nullptr);
        auto* box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,14);
        gtk_container_set_border_width(GTK_CONTAINER(box),12);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),box);
        grid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(grid),4);gtk_grid_set_column_spacing(GTK_GRID(grid),8);
        gtk_box_pack_start(GTK_BOX(box),grid,FALSE,FALSE,0);
        preview=gtk_drawing_area_new();gtk_widget_set_size_request(preview,300,330);
        gtk_widget_add_events(preview,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK);
        gtk_box_pack_start(GTK_BOX(box),preview,TRUE,TRUE,0);
        lane=gtk_combo_box_text_new();name=gtk_entry_new();spot=gtk_combo_box_text_new();
        count=gtk_spin_button_new_with_range(0,30,1);rf=gtk_spin_button_new_with_range(0,1,.01);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(rf),4);
        tail=gtk_spin_button_new_with_range(0,80,.1);opacity=gtk_spin_button_new_with_range(0,100,1);
        size=gtk_spin_button_new_with_range(1,16,.1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(size),diameter);
        row(0,"样品列",lane);row(1,"样品名称",name);row(2,"本列斑点数",count);row(3,"斑点",spot);row(4,"Rf",rf);
        row(5,"拖尾长度 (%)",tail);row(6,"拖尾不透明度 (%)",opacity);row(7,"斑点直径",size);
        auto check=[&](int n,const char* text,bool enabled){
            auto* w=gtk_check_button_new_with_label(tr(text));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),enabled);
            gtk_grid_attach(GTK_GRID(grid),w,n%2,8+n/2,1,1);return w;
        };
        guides=check(0,"样品列辅助线",figure.showLaneBorders);origin=check(1,"起点线",figure.originDefined);
        front=check(2,"溶剂前沿",figure.frontDefined);labels=check(3,"显示 Rf",showRf);
        for(auto* w:{guides,origin,front,labels})g_signal_connect(w,"toggled",G_CALLBACK(+[](GtkToggleButton* w,Dialog* d){
            auto v=bool(gtk_toggle_button_get_active(w));auto* c=GTK_WIDGET(w);
            if(c==d->guides)d->figure.showLaneBorders=v;else if(c==d->origin)d->figure.originDefined=v;else if(c==d->front)d->figure.frontDefined=v;else d->showRf=v;
            gtk_widget_queue_draw(d->preview);
        }),this);
        for(int i=0;i<2;++i){
            auto* w=gtk_button_new_with_label(tr(i?"删除样品列":"添加样品列"));gtk_grid_attach(GTK_GRID(grid),w,i,10,1,1);
            g_object_set_data(G_OBJECT(w),"remove",GINT_TO_POINTER(i));
            g_signal_connect(w,"clicked",G_CALLBACK(+[](GtkButton* btn,Dialog* d){
                if(g_object_get_data(G_OBJECT(btn),"remove"))studio_tlc::removeLane(d->figure,d->currentLane().id);
                else if(studio_tlc::addLane(d->figure))d->laneIndex=d->figure.lanes.size()-1;
                d->spotIndex=0;d->refresh();
            }),this);
        }
        for(auto* c:{lane,spot})g_signal_connect(c,"changed",G_CALLBACK(+[](GtkComboBox* w,Dialog* d){
            int i=gtk_combo_box_get_active(w);if(i<0||d->updating)return;
            if(GTK_WIDGET(w)==d->lane){d->laneIndex=size_t(i);d->spotIndex=0;}else d->spotIndex=size_t(i);
            d->refresh();
        }),this);
        g_signal_connect(name,"changed",G_CALLBACK(+[](GtkEditable* w,Dialog* d){
            if(!d->updating){d->currentLane().label=gtk_entry_get_text(GTK_ENTRY(w));gtk_widget_queue_draw(d->preview);}
        }),this);
        for(auto* c:{count,rf,tail,opacity,size})g_signal_connect(c,"value-changed",G_CALLBACK(+[](GtkSpinButton* w,Dialog* d){
            if(d->updating)return;
            double v=gtk_spin_button_get_value(w);auto* c=GTK_WIDGET(w);
            if(c==d->count){studio_tlc::setSpotCount(d->figure,d->frame,d->currentLane().id,int(v),d->diameter);d->refresh();}
            else if(c==d->size){d->diameter=v;for(auto& spot:d->figure.spots)studio_tlc::resizeSpots(spot,d->frame,v);}
            else if(auto* s=d->currentSpot()){
                if(c==d->rf)studio_tlc::setRf(d->figure,*s,v);else if(c==d->tail)s->tailLengthV=v/50;else s->tailOpacity=v/100;
            }
            gtk_widget_queue_draw(d->preview);
        }),this);
        g_signal_connect(preview,"draw",G_CALLBACK(+[](GtkWidget*,cairo_t* c,Dialog* d)->gboolean{d->paint(c);return TRUE;}),this);
        g_signal_connect(preview,"button-press-event",G_CALLBACK(+[](GtkWidget*,GdkEventButton* e,Dialog* d)->gboolean{if(e->button!=1)return FALSE;d->pointer(e->x,e->y,true);return TRUE;}),this);
        g_signal_connect(preview,"motion-notify-event",G_CALLBACK(+[](GtkWidget*,GdkEventMotion* e,Dialog* d)->gboolean{if(!d->dragging)return FALSE;d->pointer(e->x,e->y,false);return TRUE;}),this);
        g_signal_connect(preview,"button-release-event",G_CALLBACK(+[](GtkWidget*,GdkEventButton*,Dialog* d)->gboolean{d->dragging=false;return TRUE;}),this);
        refresh();gtk_widget_show_all(dialog);
        bool applied=gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT;
        gtk_widget_destroy(dialog);return applied;
    }
};
}
