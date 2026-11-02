#ifndef R62_NEXT_ADVANCED_PERSPECTIVE_HPP
#define R62_NEXT_ADVANCED_PERSPECTIVE_HPP
// =============================================================================
// next/advanced/perspective.hpp  --  T17 structure perspective
//
// NATIVE BEHAVIOUR THIS IMPLEMENTS (source: the shipped ChemDraw user guide, the
// "Structure Perspective" section, and the installed GUI command metadata)
// -----------------------------------------------------------------------------
//   * The Structure Perspective tool tilts molecules or parts of molecules
//     through three dimensions. Drag inside the selected structure's rectangle;
//     Shift+drag limits the rotation to the X or Y axis.
//   * You can select part of a molecule and rotate it around a particular bond.
//   * The effect CANNOT be applied to orbitals.
//   * Object > Flatten removes the z-coordinate information but does NOT change
//     the appearance.
//
// THE BOUNDARY THIS MODULE KEEPS (the point of the whole file)
// -----------------------------------------------------------------------------
//  * The 2D document position of every object is the REFERENCE LAYOUT. No
//    perspective operation writes it except the explicit, user-invoked Flatten.
//  * Out-of-plane information lives in a SEPARATE FIELD: the per-object
//    extension key advanced.perspective.z. Absent == 0.
//  * The view direction / lens lives in the document extension key
//    advanced.perspective.camera. A view tilt changes the camera ONLY: no
//    coordinate, no bond, no element, no topology.
//  * Because both live in the T01 extension bag, they are part of the document
//    digest, part of one transaction patch, and part of native serialization:
//    undo and save round trip the perspective as one step.
//  * Flatten is the ONE operation that rewrites 2D coordinates; it bakes the
//    projected positions in, clears every depth and disables the camera, so the
//    appearance is preserved exactly while the z information disappears -
//    which is what the native command documents.
// =============================================================================

#include "next/advanced/common.hpp"
#include "next/core/tool_registry.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace advanced {

// ---------------------------------------------------------------------------
// Camera: the editable projection
// ---------------------------------------------------------------------------
struct PerspectiveCamera {
    bool       enabled;   // false == flat (z ignored entirely)
    double     yawDeg;    // rotation about the vertical (screen Y) axis
    double     pitchDeg;  // rotation about the horizontal (screen X) axis
    double     depth;     // perspective distance; <= 0 == orthographic
    core::Vec2 pivot;     // the structure point the rotation is about
    PerspectiveCamera()
        : enabled(false), yawDeg(0.0), pitchDeg(0.0), depth(0.0), pivot(0.0, 0.0) {}
    bool operator==(const PerspectiveCamera& o) const {
        return enabled == o.enabled && yawDeg == o.yawDeg && pitchDeg == o.pitchDeg &&
               depth == o.depth && pivot == o.pivot;
    }
    bool operator!=(const PerspectiveCamera& o) const { return !(*this == o); }
};

PerspectiveCamera identityCamera();

// Round a camera through its own stored encoding. The stored form has finite
// precision, so the preview value and the committed value must be the SAME
// rounded value or "the preview matches the commit" would be true only
// approximately. Every camera that is committed is canonicalised first.
PerspectiveCamera canonicalCamera(const PerspectiveCamera& camera);

// Document-level camera persistence (extension bag).
PerspectiveCamera readCamera(const core::Document& doc);
std::string       encodeCamera(const PerspectiveCamera& camera);
core::Error       decodeCamera(const std::string& text, PerspectiveCamera* out);

// ---------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------
// Orthographic when camera.depth <= 0, otherwise a real perspective divide.
// With camera.enabled == false the result is exactly (reference.x, reference.y):
// a disabled camera can never perturb a coordinate.
core::Vec2 projectPoint(const core::Vec2& reference, double z,
                        const PerspectiveCamera& camera, const core::Vec2& pivot);

// ---------------------------------------------------------------------------
// The separate 3D field
// ---------------------------------------------------------------------------
struct DepthSample {
    core::ObjectRef ref;
    double         z;
    DepthSample() : z(0.0) {}
    DepthSample(const core::ObjectRef& r, double z_) : ref(r), z(z_) {}
};

// Every object that currently carries a depth, ordered by (kind, id).
std::vector<DepthSample> readDepths(const core::Document& doc);
double readObjectDepth(const core::Document& doc, const core::ObjectRef& ref);

// ---------------------------------------------------------------------------
// View tilt: a display-only change
// ---------------------------------------------------------------------------
struct ViewTiltRequest {
    PerspectiveCamera base;
    core::Vec2        dragDocumentDelta;   // document units
    double            degreesPerUnit;      // sensitivity (project policy)
    bool              axisOnly;            // Shift: constrain to one axis
    ViewTiltRequest()
        : dragDocumentDelta(0.0, 0.0), degreesPerUnit(0.6), axisOnly(false) {}
};

// Pure. Horizontal drag rotates about the vertical axis, vertical drag about
// the horizontal axis. axisOnly keeps the dominant component and zeroes the
// other, which is the documented Shift behaviour.
PerspectiveCamera planViewTilt(const ViewTiltRequest& request);

// ---------------------------------------------------------------------------
// Bond-axis tilt: rotate part of a molecule around a particular bond
// ---------------------------------------------------------------------------
// Rotates the 3D point of every listed object about the line (axisPoint,
// axisDirection) and writes ONLY the resulting out-of-plane z. The 2D reference
// position is not touched, so the operation cannot corrupt the layout.
core::Error planBondAxisTilt(const core::Document& doc,
                             const std::vector<core::ObjectRef>& subset,
                             const core::Vec2& axisPoint,
                             const core::Vec2& axisDirection,
                             double angleDegrees,
                             std::vector<DepthSample>* out);

// ---------------------------------------------------------------------------
// One command applied atomically
// ---------------------------------------------------------------------------
// Writes the camera and/or the listed depths. refs without a sample are left
// alone. When enforceFresh is set the command refuses a stale plan.
core::Command applyPerspectiveCommand(const PerspectiveCamera& camera,
                                      const std::vector<DepthSample>& depths,
                                      core::DocumentId expectedDocument = 0,
                                      uint64_t expectedRevision = 0,
                                      bool enforceFresh = false);

core::Command clearPerspectiveCommand();

// ---------------------------------------------------------------------------
// Flatten (Object > Flatten)
// ---------------------------------------------------------------------------
struct FlattenPlan {
    bool          valid;
    core::Error   error;
    core::DocumentId documentId;
    uint64_t      revision;
    PerspectiveCamera camera;                 // the camera that was baked
    std::vector<core::ObjectRef> objects;     // objects whose position is rewritten
    std::vector<core::Vec2>      positions;   // projected position per object
    std::vector<core::ObjectRef> clearedDepths;
    std::string   note;
    FlattenPlan() : valid(false), documentId(0), revision(0) {}
};

// Pure. Only atoms and captions are supported; orbitals/annotations are
// reported as unsupported and never receive a perspective effect.
core::Error planFlatten(const core::Document& doc, const PerspectiveCamera& camera,
                        FlattenPlan* out);

// Applies the plan: rewrites positions, clears every depth it covered, disables
// the camera. One transaction == one undo step.
core::Command applyFlattenCommand(const FlattenPlan& plan, bool enforceFreshPlan = true);

// Copy the whole document into dst (topology, semantics, text) with every
// atom/caption position replaced by its projection. Used for image evidence and
// by the demo; it never mutates src.
core::Error projectDocumentInto(const core::Document& src,
                                const PerspectiveCamera& camera,
                                core::Document* dst);

// ---------------------------------------------------------------------------
// The tool
// ---------------------------------------------------------------------------
// Implements core::ToolBehavior::Perspective. The gesture ONLY changes the
// camera; updateGesture publishes a preview and never mutates the document.
class PerspectiveTool : public core::Tool {
public:
    PerspectiveTool();

    const core::ToolId& id() const { return id_; }
    core::ToolBehavior behavior() const { return core::ToolBehavior::Perspective; }

    // Toolbar activation. Default sensitivity is a PROJECT value; the native
    // guide does not publish a degree-per-unit constant.
    void setSensitivity(double degreesPerUnit) { sensitivity_ = degreesPerUnit; }
    double sensitivity() const { return sensitivity_; }

    void setPivot(const core::Vec2& p) { pivot_ = p; havePivot_ = true; }
    core::Vec2 pivot() const { return pivot_; }

    // The camera the gesture will commit. A test reads this to prove preview and
    // commit used one value.
    const PerspectiveCamera& pendingCamera() const { return pending_; }
    const PerspectiveCamera& committedCamera() const { return committed_; }
    bool axesConstrained() const { return axisOnly_; }
    const std::string& status() const { return status_; }

    core::CommandResult beginGesture(core::ToolHost& host, const core::PointerState& pointer,
                                     core::HitContext& hits);
    core::CommandResult updateGesture(core::ToolHost& host, const core::PointerState& pointer,
                                      core::HitContext& hits);
    core::CommandResult endGesture(core::ToolHost& host, const core::PointerState& pointer,
                                   core::HitContext& hits);
    core::CommandResult cancelGesture(core::ToolHost& host);

private:
    core::ToolId      id_;
    double            sensitivity_;
    PerspectiveCamera base_;
    PerspectiveCamera pending_;
    PerspectiveCamera committed_;
    core::Vec2        pivot_;
    core::Vec2        origin_;
    bool              pressed_;
    bool              havePivot_;
    bool              axisOnly_;
    std::string       status_;
};

// ---------------------------------------------------------------------------
// Capability record used by coverage and by the host
// ---------------------------------------------------------------------------
struct PerspectiveCapability {
    const char* command;      // tools.csv command name
    Provenance  provenance;
    bool        editsCoordinates;  // true only for Flatten
    const char* note;
};
const std::vector<PerspectiveCapability>& perspectiveCapabilities();

} // namespace advanced
} // namespace r62

#endif // R62_NEXT_ADVANCED_PERSPECTIVE_HPP
