#include "next/experimental/preset.hpp"

#include <cstring>

namespace r62 {
namespace experimental {

namespace {

const PlatePresetSpec kPresets[] = {
    // [SRC] tools.csv row 180
    { 0u, "chooseTLCPlateTool", "r62.tool.experimental.tlc",
      "TLC plate", FigureKind::TlcPlate, 4u, true,
      "thin-layer chromatography board: lanes + spots, origin line / solvent front define Rf" },
    // [SRC] tools.csv row 181
    { 1u, "chooseGEPPlateTool", "r62.tool.experimental.gel",
      "Gel electrophoresis", FigureKind::GelElectrophoresis, 3u, true,
      "electrophoresis gel: lanes + bands; band intensity is a DISPLAY property, not a measurement" }
};

const size_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

} // namespace

const char* figureKindName(FigureKind kind) {
    switch (kind) {
    case FigureKind::TlcPlate:           return "TlcPlate";
    case FigureKind::GelElectrophoresis: return "GelElectrophoresis";
    }
    return "Unknown";
}

bool figureKindFromName(const std::string& name, FigureKind* out) {
    if (!out) return false;
    if (name == "TlcPlate")          { *out = FigureKind::TlcPlate; return true; }
    if (name == "GelElectrophoresis"){ *out = FigureKind::GelElectrophoresis; return true; }
    return false;
}

const PlatePresetSpec* platePresets() { return kPresets; }
size_t platePresetCount() { return kPresetCount; }

const PlatePresetSpec* platePresetById(uint32_t id) {
    for (size_t i = 0; i < kPresetCount; ++i)
        if (kPresets[i].presetId == id) return &kPresets[i];
    return 0;
}

const PlatePresetSpec* platePresetByCommand(const std::string& sourceCommand) {
    for (size_t i = 0; i < kPresetCount; ++i)
        if (sourceCommand == kPresets[i].sourceCommand) return &kPresets[i];
    return 0;
}

const PlatePresetSpec* platePresetByToolId(const std::string& toolId) {
    for (size_t i = 0; i < kPresetCount; ++i)
        if (toolId == kPresets[i].toolId) return &kPresets[i];
    return 0;
}

const char* const kPlacePlateToolId = "r62.tool.experimental.plate";

} // namespace experimental
} // namespace r62
