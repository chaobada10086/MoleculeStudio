#include "font_families.hpp"
#include "next/text/glyph_source.hpp"
#include <pango/pangocairo.h>

namespace r62 { namespace text {
namespace {
class LinuxFont final : public GlyphProvider {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_A8,1,1);
    cairo_t* context = cairo_create(surface);
    PangoLayout* layout = pango_cairo_create_layout(context);
    size_t samples = 0;
    static constexpr double size = 128.0;

    void configure(const FontSpec& font, bool lineMetrics = false) {
        auto* desc = pango_font_description_new();
        const auto families = linux_font::families(font.family, lineMetrics);
        pango_font_description_set_family(desc,families.c_str());
        pango_font_description_set_absolute_size(desc,size * PANGO_SCALE);
        pango_font_description_set_weight(desc,font.bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
        pango_font_description_set_style(desc,font.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
        pango_layout_set_font_description(layout,desc);
        pango_font_description_free(desc);
    }
public:
    ~LinuxFont() override {
        g_object_unref(layout);
        cairo_destroy(context);
        cairo_surface_destroy(surface);
    }
    bool glyph(uint32_t cp,const FontSpec& font,Glyph* out) override {
        if (!out || !g_unichar_validate(cp)) return false;
        configure(font);
        char chars[8];
        pango_layout_set_text(layout,chars,g_unichar_to_utf8(cp,chars));
        const double baseline = pango_layout_get_baseline(layout) / double(PANGO_SCALE);
        PangoRectangle ink,logical;
        pango_layout_get_extents(layout,&ink,&logical);
        *out = Glyph();
        out->advance = logical.width / (size * PANGO_SCALE);
        out->resolved = font;
        out->missing = pango_layout_get_unknown_glyphs_count(layout) > 0;
        cairo_new_path(context);
        cairo_move_to(context,0,-baseline);
        pango_cairo_layout_path(context,layout);
        cairo_path_t* path = cairo_copy_path_flat(context);
        std::vector<std::vector<float>> contour;
        for (int i=0; i<path->num_data; i+=path->data[i].header.length) {
            const auto type = path->data[i].header.type;
            if (type == CAIRO_PATH_CLOSE_PATH) {
                if (contour.size() >= 3) out->contours.push_back(contour);
                contour.clear();
            } else if (type == CAIRO_PATH_MOVE_TO || type == CAIRO_PATH_LINE_TO) {
                if (type == CAIRO_PATH_MOVE_TO) contour.clear();
                auto p = path->data[i+1].point;
                contour.push_back({float(p.x/size),float(-p.y/size)});
            }
        }
        cairo_path_destroy(path);
        out->hasInk = !out->contours.empty();
        out->blank = !out->hasInk;
        const double unit = size*PANGO_SCALE;
        out->x0 = ink.x/unit;
        out->x1 = (ink.x+ink.width)/unit;
        out->y1 = baseline/size-ink.y/unit;
        out->y0 = out->y1-ink.height/unit;
        ++samples;
        return true;
    }
    bool lineMetrics(const FontSpec& font,LineMetrics* out) override {
        if (!out) return false;
        configure(font, true);
        auto* metrics = pango_context_get_metrics(pango_layout_get_context(layout),
                                                  pango_layout_get_font_description(layout),nullptr);
        out->ascent = pango_font_metrics_get_ascent(metrics)/(size*PANGO_SCALE);
        out->descent = pango_font_metrics_get_descent(metrics)/(size*PANGO_SCALE);
        out->lineGap = 0; out->em = 1; out->valid = true;
        pango_font_metrics_unref(metrics);
        return true;
    }
    size_t sampleCount() const override {return samples;}
    size_t reuseCount() const override {return 0;}
    const char* providerName() const override {return "Pango/Cairo";}
};
}
GlyphProvider* createLinuxGlyphProvider(){return new LinuxFont();}
}}