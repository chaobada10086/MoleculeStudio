#include "next/advanced/perspective.hpp"

#include "next/core/tool_registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace r62 {
namespace advanced {

namespace {

const double kPi = 3.14159265358979323846;

struct Vec3 {
    double x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
};

double clampDegrees(double d) {
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}

bool isPerspectiveObject(const core::ObjectRef& ref) {
    return ref.kind == core::ObjectKind::Atom || ref.kind == core::ObjectKind::Text;
}

std::vector<std::string> splitSemicolon(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ';') { out.push_back(current); current.clear(); }
        else current.push_back(text[i]);
    }
    out.push_back(current);
    return out;
}

bool parseDouble(const std::string& s, double* out) {
    if (s.empty()) return false;
    char* end = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (!end || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    *out = v;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
PerspectiveCamera identityCamera() { return PerspectiveCamera(); }

PerspectiveCamera canonicalCamera(const PerspectiveCamera& camera) {
    PerspectiveCamera out;
    if (!decodeCamera(encodeCamera(camera), &out).ok()) return camera;
    return out;
}

std::string encodeCamera(const PerspectiveCamera& c) {
    char buf[192];
    std::snprintf(buf, sizeof(buf), "v1;%d;%.6f;%.6f;%.6f;%.6f;%.6f",
                  c.enabled ? 1 : 0, c.yawDeg, c.pitchDeg, c.depth,
                  c.pivot.x, c.pivot.y);
    return std::string(buf);
}

core::Error decodeCamera(const std::string& text, PerspectiveCamera* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null camera");
    std::vector<std::string> parts = splitSemicolon(text);
    if (parts.size() != 7 || parts[0] != "v1")
        return core::Error(core::ErrorCode::ParseError, "unrecognised camera encoding");
    if (parts[1] != "0" && parts[1] != "1")
        return core::Error(core::ErrorCode::ParseError, "enabled must be 0 or 1");
    PerspectiveCamera c;
    c.enabled = parts[1] == "1";
    if (!parseDouble(parts[2], &c.yawDeg) || !parseDouble(parts[3], &c.pitchDeg) ||
        !parseDouble(parts[4], &c.depth) || !parseDouble(parts[5], &c.pivot.x) ||
        !parseDouble(parts[6], &c.pivot.y))
        return core::Error(core::ErrorCode::ParseError, "camera field is not a finite number");
    *out = c;
    return core::Error::success();
}

PerspectiveCamera readCamera(const core::Document& doc) {
    const std::string* v = doc.documentExtensions().findValue(kCameraKey);
    if (!v) return identityCamera();
    PerspectiveCamera c;
    if (!decodeCamera(*v, &c).ok()) return identityCamera();
    return c;
}

// ---------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------
core::Vec2 projectPoint(const core::Vec2& reference, double z,
                        const PerspectiveCamera& camera, const core::Vec2& pivot) {
    if (!camera.enabled) return reference;
    const double yaw   = camera.yawDeg   * kPi / 180.0;
    const double pitch = camera.pitchDeg * kPi / 180.0;
    const double x = reference.x - pivot.x;
    const double y = reference.y - pivot.y;
    const double zz = std::isfinite(z) ? z : 0.0;

    // Rotate about the horizontal (screen X) axis first, then about the
    // vertical (screen Y) axis. Document y grows DOWN, so the sign of a
    // positive pitch is a project convention, recorded in RESULTS.md; what is
    // normative is that pitch/yaw 0 returns the input untouched.
    const double y1 =  y * std::cos(pitch) - zz * std::sin(pitch);
    const double z1 =  y * std::sin(pitch) + zz * std::cos(pitch);
    const double x1 =  x;

    const double x2 = x1 * std::cos(yaw) + z1 * std::sin(yaw);
    const double z2 = -x1 * std::sin(yaw) + z1 * std::cos(yaw);
    const double y2 = y1;

    double s = 1.0;
    if (camera.depth > 1e-9) {
        double denom = camera.depth + z2;
        if (denom < 1.0) denom = 1.0;
        s = camera.depth / denom;
    }
    return core::Vec2(pivot.x + x2 * s, pivot.y + y2 * s);
}

// ---------------------------------------------------------------------------
// Depth field
// ---------------------------------------------------------------------------
double readObjectDepth(const core::Document& doc, const core::ObjectRef& ref) {
    const std::string* v = doc.extensions(ref).findValue(kDepthKey);
    if (!v) return 0.0;
    return std::strtod(v->c_str(), 0);
}

std::vector<DepthSample> readDepths(const core::Document& doc) {
    std::vector<DepthSample> out;
    const std::vector<core::AtomRecord>& atoms = doc.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        core::ObjectRef ref(core::ObjectKind::Atom, atoms[i].id);
        if (!doc.extensions(ref).findValue(kDepthKey)) continue;
        out.push_back(DepthSample(ref, readObjectDepth(doc, ref)));
    }
    const std::vector<core::TextRecord>& texts = doc.texts().records();
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) continue;
        core::ObjectRef ref(core::ObjectKind::Text, texts[i].id);
        if (!doc.extensions(ref).findValue(kDepthKey)) continue;
        out.push_back(DepthSample(ref, readObjectDepth(doc, ref)));
    }
    std::sort(out.begin(), out.end(),
              [](const DepthSample& a, const DepthSample& b) {
                  if (a.ref.kind != b.ref.kind) return a.ref.kind < b.ref.kind;
                  return a.ref.id < b.ref.id;
              });
    return out;
}

// ---------------------------------------------------------------------------
// View tilt
// ---------------------------------------------------------------------------
PerspectiveCamera planViewTilt(const ViewTiltRequest& request) {
    PerspectiveCamera c = request.base;
    // Horizontal drag rotates about the vertical axis; vertical drag about the
    // horizontal axis. The sign is a project convention; magnitude is
    // sensitivity * document-space drag distance.
    double dyaw   = -request.dragDocumentDelta.x * request.degreesPerUnit;
    double dpitch =  request.dragDocumentDelta.y * request.degreesPerUnit;
    if (request.axisOnly) {
        if (std::fabs(dyaw) >= std::fabs(dpitch)) dpitch = 0.0;
        else dyaw = 0.0;
    }
    c.yawDeg   = clampDegrees(c.yawDeg + dyaw);
    c.pitchDeg = clampDegrees(c.pitchDeg + dpitch);
    if (dyaw != 0.0 || dpitch != 0.0) c.enabled = true;
    c.pivot = request.base.pivot;
    return canonicalCamera(c);
}

// ---------------------------------------------------------------------------
// Bond-axis tilt
// ---------------------------------------------------------------------------
core::Error planBondAxisTilt(const core::Document& doc,
                             const std::vector<core::ObjectRef>& subset,
                             const core::Vec2& axisPoint,
                             const core::Vec2& axisDirection,
                             double angleDegrees,
                             std::vector<DepthSample>* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null depth output");
    out->clear();
    if (!axisPoint.finite() || !axisDirection.finite() || !std::isfinite(angleDegrees))
        return core::Error(core::ErrorCode::InvalidGeometry, "non-finite axis tilt input");
    const double len = core::length(axisDirection);
    if (len < 1e-9)
        return core::Error(core::ErrorCode::InvalidGeometry, "degenerate axis direction");
    const Vec3 u(axisDirection.x / len, axisDirection.y / len, 0.0);
    const double theta = angleDegrees * kPi / 180.0;
    const double ct = std::cos(theta);
    const double st = std::sin(theta);

    for (size_t i = 0; i < subset.size(); ++i) {
        const core::ObjectRef& ref = subset[i];
        if (!isPerspectiveObject(ref)) continue;   // orbitals etc. get nothing
        core::Vec2 base(0.0, 0.0);
        if (ref.kind == core::ObjectKind::Atom) {
            const core::AtomRecord* a = doc.atoms().alive(ref.id);
            if (!a) continue;
            base = a->position;
        } else {
            const core::TextRecord* t = doc.texts().alive(ref.id);
            if (!t) continue;
            base = t->position;
        }
        const Vec3 p(base.x - axisPoint.x, base.y - axisPoint.y,
                     readObjectDepth(doc, ref));
        // Rodrigues: p' = p cos + (u x p) sin + u (u . p)(1 - cos)
        const Vec3 cross(u.y * p.z - u.z * p.y,
                         u.z * p.x - u.x * p.z,
                         u.x * p.y - u.y * p.x);
        const double dot = u.x * p.x + u.y * p.y + u.z * p.z;
        const Vec3 rotated(p.x * ct + cross.x * st + u.x * dot * (1.0 - ct),
                           p.y * ct + cross.y * st + u.y * dot * (1.0 - ct),
                           p.z * ct + cross.z * st + u.z * dot * (1.0 - ct));
        out->push_back(DepthSample(ref, rotated.z));
    }
    std::sort(out->begin(), out->end(),
              [](const DepthSample& a, const DepthSample& b) {
                  if (a.ref.kind != b.ref.kind) return a.ref.kind < b.ref.kind;
                  return a.ref.id < b.ref.id;
              });
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
core::Command applyPerspectiveCommand(const PerspectiveCamera& camera,
                                      const std::vector<DepthSample>& depths,
                                      core::DocumentId expectedDocument,
                                      uint64_t expectedRevision,
                                      bool enforceFresh) {
    PerspectiveCamera cam = canonicalCamera(camera);
    std::vector<DepthSample> samples(depths);
    return core::Command("advanced.perspective",
                         core::kInvalidateView | core::kInvalidateAppearance,
        [cam, samples, expectedDocument, expectedRevision, enforceFresh]
        (core::CommandContext& ctx) -> core::CommandResult {
            core::Document& doc = ctx.document;
            if (enforceFresh) {
                if (expectedDocument != 0 && doc.id() != expectedDocument)
                    return core::CommandResult::fail(core::ErrorCode::DocumentMismatch,
                                                     "perspective plan belongs to another document");
                if (expectedRevision != 0 && doc.revision() != expectedRevision)
                    return core::CommandResult::fail(core::ErrorCode::RevisionConflict,
                                                     "perspective plan is stale");
            }
            doc.documentExtensions().set(kCameraKey, encodeCamera(cam));
            noteExtensionWrite(doc, core::ObjectRef(), true);
            for (size_t i = 0; i < samples.size(); ++i) {
                const core::ObjectRef& ref = samples[i].ref;
                if (ref.kind == core::ObjectKind::Atom) {
                    if (!doc.atoms().alive(ref.id)) continue;
                } else if (ref.kind == core::ObjectKind::Text) {
                    if (!doc.texts().alive(ref.id)) continue;
                } else {
                    continue;
                }
                if (samples[i].z == 0.0) clearDepthQuiet(doc, ref);
                else writeDepthQuiet(doc, ref, samples[i].z);
            }
            return core::CommandResult::success(core::kInvalidateView |
                                                core::kInvalidateAppearance);
        });
}

core::Command clearPerspectiveCommand() {
    return core::Command("advanced.perspective.clear",
                         core::kInvalidateView | core::kInvalidateAppearance,
        [](core::CommandContext& ctx) -> core::CommandResult {
            core::Document& doc = ctx.document;
            doc.documentExtensions().erase(kCameraKey);
            noteExtensionWrite(doc, core::ObjectRef(), true);
            std::vector<DepthSample> all = readDepths(doc);
            for (size_t i = 0; i < all.size(); ++i) clearDepthQuiet(doc, all[i].ref);
            return core::CommandResult::success(core::kInvalidateView |
                                                core::kInvalidateAppearance);
        });
}

// ---------------------------------------------------------------------------
// Flatten
// ---------------------------------------------------------------------------
core::Error planFlatten(const core::Document& doc, const PerspectiveCamera& camera,
                        FlattenPlan* out) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null FlattenPlan");
    *out = FlattenPlan();
    out->documentId = doc.id();
    out->revision   = doc.revision();
    out->camera     = camera;

    const core::Vec2 pivot = camera.pivot;
    const std::vector<core::AtomRecord>& atoms = doc.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        core::ObjectRef ref(core::ObjectKind::Atom, atoms[i].id);
        const core::Vec2 p = projectPoint(atoms[i].position, readObjectDepth(doc, ref),
                                          camera, pivot);
        out->objects.push_back(ref);
        out->positions.push_back(p);
    }
    const std::vector<core::TextRecord>& texts = doc.texts().records();
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) continue;
        core::ObjectRef ref(core::ObjectKind::Text, texts[i].id);
        const core::Vec2 p = projectPoint(texts[i].position, readObjectDepth(doc, ref),
                                          camera, pivot);
        out->objects.push_back(ref);
        out->positions.push_back(p);
    }
    // Every object that carries a depth is cleared, whether or not it had a
    // position rewritten (an orbital never does, but a stale depth on one must
    // still go).
    std::vector<DepthSample> depths = readDepths(doc);
    for (size_t i = 0; i < depths.size(); ++i) out->clearedDepths.push_back(depths[i].ref);

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "baked=%llu clearedDepths=%llu yaw=%.3f pitch=%.3f depth=%.3f",
                  static_cast<unsigned long long>(out->objects.size()),
                  static_cast<unsigned long long>(out->clearedDepths.size()),
                  camera.yawDeg, camera.pitchDeg, camera.depth);
    out->note = buf;
    out->valid = true;
    return core::Error::success();
}

core::Command applyFlattenCommand(const FlattenPlan& plan, bool enforceFreshPlan) {
    FlattenPlan copy = plan;
    return core::Command("advanced.perspective.flatten",
                         core::kInvalidateGeometry | core::kInvalidateAppearance,
        [copy, enforceFreshPlan](core::CommandContext& ctx) -> core::CommandResult {
            core::Document& doc = ctx.document;
            if (!copy.valid)
                return core::CommandResult::fail(core::ErrorCode::InvalidArgument,
                                                 "flatten plan is not valid");
            if (doc.id() != copy.documentId)
                return core::CommandResult::fail(core::ErrorCode::DocumentMismatch,
                                                 "flatten plan belongs to another document");
            if (enforceFreshPlan && doc.revision() != copy.revision)
                return core::CommandResult::fail(core::ErrorCode::RevisionConflict,
                                                 "flatten plan is stale; re-plan first");
            for (size_t i = 0; i < copy.objects.size(); ++i) {
                const core::ObjectRef& ref = copy.objects[i];
                core::Error r = (ref.kind == core::ObjectKind::Atom)
                    ? doc.setAtomPosition(ref.id, copy.positions[i])
                    : doc.setTextPosition(ref.id, copy.positions[i]);
                if (!r.ok()) return core::CommandResult::fail(r.code, r.message);
            }
            for (size_t i = 0; i < copy.clearedDepths.size(); ++i)
                clearDepthQuiet(doc, copy.clearedDepths[i]);
            doc.documentExtensions().erase(kCameraKey);
            noteExtensionWrite(doc, core::ObjectRef(), true);
            return core::CommandResult::success(core::kInvalidateGeometry |
                                                core::kInvalidateAppearance);
        });
}

// ---------------------------------------------------------------------------
// projectDocumentInto
// ---------------------------------------------------------------------------
core::Error projectDocumentInto(const core::Document& src,
                                const PerspectiveCamera& camera,
                                core::Document* dst) {
    if (!dst) return core::Error(core::ErrorCode::InvalidArgument, "null destination");
    dst->setDefaultBondLength(src.defaultBondLength());
    dst->setDefaultLineWidth(src.defaultLineWidth());
    dst->setDefaultTextSize(src.defaultTextSize());
    dst->setDefaultFont(src.defaultFont());
    dst->setName(src.name());

    std::map<core::ObjectId, core::ObjectId> atomMap;
    const std::vector<core::AtomRecord>& atoms = src.atoms().records();
    for (size_t i = 0; i < atoms.size(); ++i) {
        if (!atoms[i].isAlive()) continue;
        core::AtomRecord rec = atoms[i];
        core::ObjectRef ref(core::ObjectKind::Atom, atoms[i].id);
        rec.position = projectPoint(atoms[i].position, readObjectDepth(src, ref),
                                    camera, camera.pivot);
        core::ObjectId newId = core::kInvalidObjectId;
        core::Error e = dst->addAtomRecord(rec, &newId);
        if (!e.ok()) return e;
        atomMap[atoms[i].id] = newId;
        const std::string& label = src.atomLabel(atoms[i].id);
        if (!label.empty()) dst->setAtomLabelQuiet(newId, label);
    }
    const std::vector<core::BondRecord>& bonds = src.bonds().records();
    for (size_t i = 0; i < bonds.size(); ++i) {
        if (!bonds[i].isAlive()) continue;
        std::map<core::ObjectId, core::ObjectId>::const_iterator ia =
            atomMap.find(bonds[i].beginAtom);
        std::map<core::ObjectId, core::ObjectId>::const_iterator ib =
            atomMap.find(bonds[i].endAtom);
        if (ia == atomMap.end() || ib == atomMap.end()) continue;
        core::BondRecord rec = bonds[i];
        rec.beginAtom = ia->second;
        rec.endAtom = ib->second;
        core::ObjectId newId = core::kInvalidObjectId;
        core::Error e = dst->addBondRecord(rec, &newId);
        if (!e.ok()) return e;
    }
    const std::vector<core::TextRecord>& texts = src.texts().records();
    for (size_t i = 0; i < texts.size(); ++i) {
        if (!texts[i].isAlive()) continue;
        core::TextRecord rec = texts[i];
        core::ObjectRef ref(core::ObjectKind::Text, texts[i].id);
        rec.position = projectPoint(texts[i].position, readObjectDepth(src, ref),
                                    camera, camera.pivot);
        core::ObjectId newId = core::kInvalidObjectId;
        core::Error e = dst->addTextRecord(rec, &newId);
        if (!e.ok()) return e;
        dst->setRunsQuiet(newId, src.runs(texts[i].id));
    }
    return core::Error::success();
}

// ---------------------------------------------------------------------------
// Tool
// ---------------------------------------------------------------------------
PerspectiveTool::PerspectiveTool()
    : sensitivity_(0.6), pivot_(0.0, 0.0), origin_(0.0, 0.0), pressed_(false),
      havePivot_(false), axisOnly_(false) {
    id_ = "r62.tool.perspective.structure";
}

core::CommandResult PerspectiveTool::beginGesture(core::ToolHost& host,
                                                  const core::PointerState& pointer,
                                                  core::HitContext& hits) {
    (void)hits;
    base_ = readCamera(host.document());
    if (!havePivot_) {
        // Default pivot: the centroid of the current selection, or the pointer
        // when nothing is selected.
        const std::vector<core::ObjectRef>& sel = host.selection();
        core::Vec2 sum(0.0, 0.0);
        size_t n = 0;
        for (size_t i = 0; i < sel.size(); ++i) {
            if (sel[i].kind != core::ObjectKind::Atom) continue;
            const core::AtomRecord* a = host.document().atoms().alive(sel[i].id);
            if (!a) continue;
            sum = sum + a->position;
            ++n;
        }
        pivot_ = n > 0 ? core::Vec2(sum.x / static_cast<double>(n),
                                    sum.y / static_cast<double>(n))
                       : pointer.position;
    }
    base_.pivot = pivot_;
    pending_ = base_;
    origin_ = pointer.position;
    pressed_ = true;
    axisOnly_ = (pointer.modifiers & core::kModShift) != 0;
    status_ = "perspective: drag to tilt; Shift constrains to one axis";
    host.setStatus(status_);
    return core::CommandResult::noChange();
}

core::CommandResult PerspectiveTool::updateGesture(core::ToolHost& host,
                                                   const core::PointerState& pointer,
                                                   core::HitContext& hits) {
    (void)hits;
    if (!pressed_) return core::CommandResult::noChange();
    axisOnly_ = (pointer.modifiers & core::kModShift) != 0;
    ViewTiltRequest req;
    req.base = base_;
    req.dragDocumentDelta = pointer.position - origin_;
    req.degreesPerUnit = sensitivity_;
    req.axisOnly = axisOnly_;
    pending_ = planViewTilt(req);

    core::PreviewScene& preview = host.preview();
    preview.clear();
    const std::vector<core::ObjectRef>& sel = host.selection();
    for (size_t i = 0; i < sel.size(); ++i) {
        if (!isPerspectiveObject(sel[i])) continue;
        core::PreviewItem item;
        item.primitive = core::PreviewPrimitive::Highlight;
        item.subject = sel[i];
        preview.add(item);
    }
    {
        core::PreviewItem label;
        label.primitive = core::PreviewPrimitive::Label;
        label.points.push_back(pivot_);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "yaw=%.1f pitch=%.1f%s",
                      pending_.yawDeg, pending_.pitchDeg,
                      axisOnly_ ? " (axis)" : "");
        label.text = buf;
        preview.add(label);
    }
    return core::CommandResult::success(core::kInvalidateView);
}

core::CommandResult PerspectiveTool::endGesture(core::ToolHost& host,
                                                const core::PointerState& pointer,
                                                core::HitContext& hits) {
    (void)pointer; (void)hits;
    if (!pressed_) return core::CommandResult::noChange();
    pressed_ = false;
    host.preview().clear();
    if (pending_ == base_) {
        status_ = "perspective: no change";
        host.setStatus(status_);
        return core::CommandResult::noChange();
    }
    const core::DocumentId docId = host.document().id();
    const uint64_t revision = host.document().revision();
    core::Error e = host.submit(
        applyPerspectiveCommand(pending_, std::vector<DepthSample>(), docId, revision, true),
        "perspective.tilt");
    if (!e.ok()) {
        status_ = std::string("perspective rejected: ") + e.message;
        host.setStatus(status_);
        return core::CommandResult::fail(e.code, e.message);
    }
    committed_ = pending_;
    status_ = axisOnly_ ? "perspective tilt committed (axis constrained)"
                        : "perspective tilt committed";
    host.setStatus(status_);
    return core::CommandResult::success(core::kInvalidateView | core::kInvalidateAppearance);
}

core::CommandResult PerspectiveTool::cancelGesture(core::ToolHost& host) {
    pressed_ = false;
    pending_ = base_;
    host.preview().clear();
    status_ = "perspective cancelled";
    host.setStatus(status_);
    return core::CommandResult::noChange();
}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------
const std::vector<PerspectiveCapability>& perspectiveCapabilities() {
    static const std::vector<PerspectiveCapability> caps = []() {
        std::vector<PerspectiveCapability> v;
        PerspectiveCapability tilt;
        tilt.command = "chooseStructurePerspectiveTool";
        tilt.provenance = Provenance::OwnImplemented;
        tilt.editsCoordinates = false;
        tilt.note = "view camera (yaw/pitch) in a document setting; no coordinate or "
                    "topology change; Shift constrains to one axis";
        v.push_back(tilt);
        PerspectiveCapability flatten;
        flatten.command = "toggleStructurePerspective/Flatten";
        flatten.provenance = Provenance::OwnImplemented;
        flatten.editsCoordinates = true;
        flatten.note = "Object > Flatten: bakes the projection into the 2D positions, "
                       "clears the depth field and disables the camera";
        v.push_back(flatten);
        return v;
    }();
    return caps;
}

} // namespace advanced
} // namespace r62
