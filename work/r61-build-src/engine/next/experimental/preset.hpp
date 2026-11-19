#ifndef R62_NEXT_EXPERIMENTAL_PRESET_HPP
#define R62_NEXT_EXPERIMENTAL_PRESET_HPP
// =============================================================================
// re_round62_viewer/next/experimental/preset.hpp
//
// T16 - THE TWO "EXPERIMENTAL FIGURE" TOOLS FROM THE MAIN TOOL INVENTORY.
//
// -----------------------------------------------------------------------------
// SOURCE OF THE TWO COMMAND STRINGS (not invented)
// -----------------------------------------------------------------------------
// host_integration/drawing_inventory/tools.csv rows 180 and 181 (0-based line
// numbers 180/181 in the file, category \u5b9e\u9a8c\u56fe\u793a "experimental figure"):
//
//   chooseTLCPlateTool   ChromatographyToolBarID  Advanced_Toolbar.xml
//   chooseGEPPlateTool   ChromatographyToolBarID  Advanced_Toolbar.xml
//
// DRAWING_FEATURES_2026-10-14.md counts exactly 2 tools in that category. The
// strings below are copied VERBATIM from those rows and are the only two source
// commands this module owns. Nothing else in the 275-entry inventory is claimed.
//
// -----------------------------------------------------------------------------
// ONE ALGORITHM, TWO PRESETS (the same 275-is-not-275-algorithms rule)
// -----------------------------------------------------------------------------
// A TLC plate and an electrophoresis gel are the SAME structured object with a
// different geometric vocabulary:
//
//   TLC  : lanes + spots  (+ origin line + solvent front, which define Rf)
//   GEL  : lanes + bands  (+ wells at the top; no origin/front line)
//
// Both are stored as one core::AnnotationRecord whose geometry mirror is the
// board frame and whose relative content lives in the object extension bag. The
// preset id only records which command created the figure; the renderer switches
// on FigureKind, never on the preset id.
//
// -----------------------------------------------------------------------------
// HONESTY: WHAT A FIGURE IS AND IS NOT
// -----------------------------------------------------------------------------
// The figure is a STRUCTURED, EDITABLE DRAWING. It is not an experiment
// predictor, and this module contains no equation that turns a picture into a
// measured molecular weight or a concentration. Rf is the only physics-adjacent
// quantity computed here, it is computed ONLY when a valid origin line and
// solvent front are defined, and the formula is stated in model.hpp. Gel band
// "intensity" is a DISPLAY property (0..1 opacity of the drawn band); it is never
// presented as a measurement. Measured values (Rf of a standard, a marker
// molecular weight) are separate fields that carry a unit and a provenance and
// are only ever set by the caller.
// =============================================================================

#include "next/core/contracts.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace r62 {
namespace experimental {

// ---------------------------------------------------------------------------
// Version handshake (mirrors next/core, next/render, ...)
// ---------------------------------------------------------------------------
enum {
    kExperimentalMajor    = 1,
    kExperimentalMinor    = 0,
    kExperimentalRevision = 0,
    kExperimentalVersion  = kExperimentalMajor * 10000 + kExperimentalMinor * 100 + kExperimentalRevision
};

inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

inline bool requestExperimentalCompatibility(int major, int minor) {
    return major == kExperimentalMajor && minor <= kExperimentalMinor;
}

// ---------------------------------------------------------------------------
// Figure kind
// ---------------------------------------------------------------------------
enum class FigureKind : uint8_t {
    TlcPlate          = 0,
    GelElectrophoresis = 1
};

const char* figureKindName(FigureKind kind);
bool figureKindFromName(const std::string& name, FigureKind* out);

// ---------------------------------------------------------------------------
// Preset rows: exactly the 2 source commands of the "experimental figure"
// category. presetId is a PERSISTED value (it is written into the
// AnnotationRecord), so this table must never be reordered.
// ---------------------------------------------------------------------------
struct PlatePresetSpec {
    uint32_t    presetId;
    const char* sourceCommand;   // [SRC] tools.csv
    const char* toolId;          // registry id for this preset
    const char* displayName;
    FigureKind  kind;
    uint32_t    defaultLaneCount;
    bool        implemented;
    const char* note;
};

const PlatePresetSpec* platePresets();
size_t                 platePresetCount();
const PlatePresetSpec* platePresetById(uint32_t id);
const PlatePresetSpec* platePresetByCommand(const std::string& sourceCommand);
const PlatePresetSpec* platePresetByToolId(const std::string& toolId);

// The single Tool instance that serves both presets (T01 keeps ONE instance per
// ToolBehavior; see tools.hpp).
extern const char* const kPlacePlateToolId;   // "r62.tool.experimental.plate"

} // namespace experimental
} // namespace r62

#endif // R62_NEXT_EXPERIMENTAL_PRESET_HPP
