#pragma once

#include <array>
#include <cmath>

namespace studio_skin {
struct Color {
    int red, green, blue;
};

struct Preset {
    const wchar_t* name;
    Color color;
};

inline const std::array<Preset, 5>& presets() {
    static const std::array<Preset, 5> list = {{
        {L"柔和蓝", {64, 98, 190}}, {L"GPT 绿", {23, 117, 109}}, {L"岩板紫", {113, 86, 161}},
        {L"琥珀棕", {158, 91, 37}}, {L"石墨灰", {76, 94, 107}}
    }};
    return list;
}

inline Color mix(Color first, Color second, double ratio) {
    const auto channel = [ratio](int a, int b) { return int(std::lround(a + (b - a) * ratio)); };
    return {channel(first.red, second.red), channel(first.green, second.green), channel(first.blue, second.blue)};
}

inline double luminance(Color color) {
    const auto linear = [](int raw) {
        const double channel = raw / 255.;
        return channel <= .04045 ? channel / 12.92 : std::pow((channel + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.red) + .7152 * linear(color.green) + .0722 * linear(color.blue);
}

struct Palette {
    Color accent, strong, selected, hover, soft, frame, tab, border;

    explicit Palette(Color base) {
        const Color white{255, 255, 255}, black{0, 0, 0};
        accent = base;
        while (luminance(accent) > .1833) accent = mix(accent, black, .07);
        strong = mix(accent, black, .20);
        selected = mix(white, base, .19);
        hover = mix(white, base, .105);
        soft = mix(white, base, .045);
        frame = mix({246, 247, 249}, base, .036);
        tab = mix({243, 244, 246}, base, .075);
        border = mix({221, 225, 231}, base, .105);
    }
};
}
