#pragma once

#include "arrange_actions.hpp"
#include "../transform_preview.hpp"
#include "../engine/arrow_preview_cache.hpp"
#include "attached_atoms.hpp"
#include "electron_marker_actions.hpp"
#include "next/arrows_curves/tools.hpp"
#include "next/graphics/model.hpp"
#include "structure_perspective.hpp"
#include <array>

namespace linux_selection {
using namespace r62;
using namespace r62::core;
using integration::Session;
using selection::Affine2;

inline bool closedMolecularSelection(const Document& doc, const std::set<ObjectRef>& selected) {
    bool hasAtoms = false;
    for (auto ref : selected) {
        if (ref.kind == ObjectKind::Atom) hasAtoms = true;
        else if (ref.kind != ObjectKind::Bond && ref.kind != ObjectKind::Arrow) return false;
    }
    if (!hasAtoms) return false;
    const auto atomSelected = [&](ObjectId id) { return selected.count({ObjectKind::Atom, id}) != 0; };
    for (const auto& bond : doc.bonds().records())
        if (bond.isAlive() && atomSelected(bond.beginAtom) != atomSelected(bond.endAtom)) return false;
    for (const auto& arrow : doc.arrows().records()) {
        if (!arrow.isAlive()) continue;
        const ObjectRef ref(ObjectKind::Arrow, arrow.id);
        ObjectId begin, end;
        if (!virtual_bonds::endpoints(doc, ref, begin, end)) continue;
        const bool included = selected.count(ref) != 0;
        if (atomSelected(begin) != included || atomSelected(end) != included) return false;
    }
    return true;
}
// Use the existing geometry for complete components. A cut through a plain
// carbon skeleton is also exact if every retained carbon stays bonded and
// every retained bond is already drawn as a full, unbroken centreline.
inline bool copyMolecularScene(const Session& session, render::Scene& scene, std::vector<uint8_t>& projectedMask) {
    const auto& doc = session.doc;
    std::vector<uint8_t> atoms(doc.atoms().size()), bonds(doc.bonds().size());
    for (auto ref : session.selected) {
        if (ref.kind == ObjectKind::Atom && doc.atoms().alive(ref.id)) atoms[ref.id] = 1;
        else if (ref.kind == ObjectKind::Bond && doc.bonds().alive(ref.id)) {
            const auto& bond = *doc.bonds().alive(ref.id);
            atoms[bond.beginAtom] = atoms[bond.endAtom] = 1;
        } else return false;
    }
    bool cut = false;
    for (const auto& bond : doc.bonds().records()) {
        if (!bond.isAlive()) continue;
        const bool begin = atoms[bond.beginAtom] != 0, end = atoms[bond.endAtom] != 0;
        if (begin && end) {
            bonds[bond.id] = 1;
            atoms[bond.beginAtom] = atoms[bond.endAtom] = 2;
        } else if (begin != end) cut = true;
    }
    for (const auto& arrow : doc.arrows().records()) {
        ObjectId a, b;
        if (arrow.isAlive() && virtual_bonds::endpoints(doc, {ObjectKind::Arrow, arrow.id}, a, b) &&
            ((a < atoms.size() && atoms[a]) || (b < atoms.size() && atoms[b]))) return false;
    }
    if (cut) {
        for (const auto& atom : doc.atoms().records()) {
            if (!atom.isAlive() || !atoms[atom.id]) continue;
            if (atoms[atom.id] == 1 || atom.element != 6 || atom.semantics != AtomSemantics() ||
                !doc.atomLabel(atom.id).empty()) return false;
        }
        for (const auto& bond : doc.bonds().records()) {
            if (bond.isAlive() && bonds[bond.id] && (bond.order != BondOrder::Single || bond.display != BondDisplay::Normal ||
                bond.semantics != BondSemantics::Normal || bond.stereo != BondStereo::None)) return false;
        }
    }
    render::Scene preview;
    preview.reserve(std::min(session.scene.size(), session.selected.size()));
    std::vector<uint8_t> mask(session.scene.size()+2);
    for (const auto& layer : session.scene.layers()) preview.addLayer(layer.name, layer.z);
    for (size_t index = 0; index < session.scene.size(); ++index) {
        const auto& p = session.scene.primitives()[index];
        if (p.source.kind == ObjectKind::Atom && p.source.id < atoms.size() && atoms[p.source.id]) {
            if (cut) return false;
        } else if (p.source.kind == ObjectKind::Bond && p.source.id < bonds.size() && bonds[p.source.id]) {
            if (cut) {
                const auto& bond = *doc.bonds().alive(p.source.id);
                if (bonds[bond.id] != 1 || p.points.size() != 2 || !p.subpaths.empty() ||
                    (p.kind != render::PrimKind::Segment && p.kind != render::PrimKind::Polyline) ||
                    !(p.points.front() == doc.atoms().alive(bond.beginAtom)->position) ||
                    !(p.points.back() == doc.atoms().alive(bond.endAtom)->position)) return false;
                bonds[bond.id] = 2;
            }
        } else continue;
        mask[2 + index] = 1;
        preview.add(p);
    }
    if (cut && std::find(bonds.begin(), bonds.end(), 1) != bonds.end()) return false;
    scene = std::move(preview);
    projectedMask = std::move(mask);
    return true;
}
inline Error applyMatrix(Document& doc, const std::vector<ObjectRef>& refs, const Affine2& matrix) {
    std::vector<ObjectRef> other;
    const auto transform = [&](graphics::ShapeFrame& frame) {
        const auto center = frame.centre;
        frame.centre = matrix.apply(center);
        frame.axisU = matrix.apply(center + frame.axisU) - frame.centre;
        frame.axisV = matrix.apply(center + frame.axisV) - frame.centre;
    };
    for (const auto ref : refs) {
        bool shape = false;
        Error error;
        if (ref.kind == ObjectKind::Arrow) {
            graphics::ShapeParams params;
            error = graphics::readShape(doc, ref.id, &params, &shape);
            if (error.ok() && shape) {
                transform(params.frame);
                graphics::swapCircleForOval(&params);
                error = graphics::writeShape(doc, ref.id, params, doc.defaultLineWidth());
            }
        } else if (ref.kind == ObjectKind::Bracket) {
            graphics::BracketParams params;
            error = graphics::readBracket(doc, ref.id, &params, &shape);
            if (error.ok() && shape) {
                transform(params.frame);
                error = graphics::writeBracket(doc, ref.id, params, doc.defaultLineWidth());
            }
        }
        if (!error.ok()) return error;
        if (!shape) other.push_back(ref);
    }
    const auto report = selection::transformObjects(doc, other, matrix, selection::TransformOptions());
    if (report.refsSkippedUnsupported) return {ErrorCode::Unsupported, "所选对象无法变换"};
    return {};
}

class Gesture {
    enum class Mode { None, Copy, Rotate, Scale, Bond, Wrap, Perspective, Attached, ElectronMarker };
    Mode mode_ = Mode::None;
    int handle_ = -1;
    Vec2 press_, pivot_, bondVector_;
    std::shared_ptr<Session> copySource_;
    bool copyWholeDocument_ = false;
    std::vector<uint8_t> copyProjectionMask_;
    Vec2 copyDelta_;
    Box box_;
    ObjectId text_ = kInvalidObjectId;
    std::vector<ObjectRef> refs_;
    std::set<ObjectId> perspectiveAtoms_;
    DocumentId document_ = 0;
    uint64_t serial_ = 0;
    std::unique_ptr<Session> preview_;
    arrows::PreviewArrowCache arrowCache_;
    std::function<Error(Document&)> edit_;
    bool refreshPreview_ = false;
    render::Scene transformScene_;
    std::vector<size_t> transformSlots_;
    std::vector<uint8_t> movingPrimitives_;
    Affine2 transform_;
    bool fastTransform_ = false;
    uint64_t frame_ = 0;
    studio::attached_atoms::Hit attached_;
    std::shared_ptr<ObjectId> added_;
    studio::electron_marker::Hit marker_;
    DocumentId markerDocument_ = 0;
    uint64_t markerRevision_ = 0;

    bool matches(const Session& session) const {
        return pending() && document_ == session.doc.id() && serial_ == session.serial;
    }

    bool hasMarker(const Session& session) const {
        return session.selected.empty() && markerDocument_ == session.doc.id() &&
            markerRevision_ == session.doc.revision() && studio::electron_marker::exists(session.doc, marker_);
    }

    void selectMarker(Session& session, studio::electron_marker::Hit marker) {
        marker_ = marker;
        markerDocument_ = session.doc.id();
        markerRevision_ = session.doc.revision();
        session.setSelection({});
    }

    bool beginBond(Session& session, ObjectId id, Vec2 point) {
        const auto* bond = session.doc.bonds().alive(id);
        if (!bond) return false;
        if (bond->semantics != BondSemantics::Normal || bond->order != BondOrder::Single) {
            session.status = "请选择一根可调整方向的单键";
            return true;
        }
        const auto a = bond->beginAtom, b = bond->endAtom;
        const auto branch = [&](ObjectId start, ObjectId fixed) {
            std::vector<ObjectRef> refs{{ObjectKind::Atom, start}};
            std::set<ObjectId> visited{start};
            for (size_t i = 0; i < refs.size(); ++i) {
                const auto atom = refs[i].id;
                for (auto link : session.doc.incidentBonds(atom)) {
                    if (link == id) continue;
                    const auto* edge = session.doc.bonds().alive(link);
                    if (!edge || edge->semantics == BondSemantics::Hydrogen) continue;
                    const auto other = edge->beginAtom == atom ? edge->endAtom : edge->beginAtom;
                    if (other == fixed) return std::vector<ObjectRef>{};
                    if (visited.insert(other).second) refs.emplace_back(ObjectKind::Atom, other);
                }
            }
            return refs;
        };
        auto sideA = branch(a, b);
        auto sideB = branch(b, a);
        if (sideA.empty() || sideB.empty()) {
            session.status = "环内键无法单独改变方向";
            return true;
        }
        const auto from = session.doc.atoms().alive(a)->position;
        const auto to = session.doc.atoms().alive(b)->position;
        const auto vector = to - from;
        const auto span = length(vector);
        if (span < 1e-8) {
            session.status = "该键太短，无法旋转";
            return true;
        }
        const auto side = dot(point - (from + to) * .5, vector) / span;
        const bool nearerA = std::abs(side) <= session.view().deviceRadiusToDocument(1) ?
            sideA.size() <= sideB.size() : side < 0;
        const bool terminalA = sideA.size() == 1, terminalB = sideB.size() == 1;
        const bool moveA = terminalA == terminalB ? nearerA : terminalA;
        refs_ = moveA ? std::move(sideA) : std::move(sideB);
        pivot_ = moveA ? to : from;
        bondVector_ = moveA ? vector * -1 : vector;
        mode_ = Mode::Bond;
        press_ = point;
        session.status = "旋转单键 · Shift 按 15° 调整";
        return true;
    }

    bool beginWrap(const Session& session, const Box& ink) {
        if (refs_.size() != 1 || refs_.front().kind != ObjectKind::Text) return false;
        const auto ref = refs_.front();
        const auto* record = session.doc.texts().alive(ref.id);
        if (!record) return false;
        const auto& bag = session.doc.extensions(ref);
        const auto* rotation = bag.findValue("studio.text.RotationAngle");
        if (rotation && std::abs(std::atof(rotation->c_str())) > 1e-7) return false;
        const auto* stored = bag.findValue("studio.text.width");
        const auto width = stored ? std::atof(stored->c_str()) : ink.width();
        if (width <= 0) return false;
        const auto align = record->format.align;
        const auto left = record->position.x - width * (align == 1 ? .5 : align == 2 ? 1. : 0.);
        box_ = Box(Vec2(left, ink.min.y), Vec2(left + width, ink.max.y));
        text_ = ref.id;
        mode_ = Mode::Wrap;
        return true;
    }

    bool beginPerspective(Session& session, Vec2 point) {
        if (!studioio::inside(studioio::selectionBounds(session), point,
                session.view().deviceRadiusToDocument(4))) return false;
        std::set<ObjectId> atoms;
        Box bounds;
        for (auto ref : studioio::expanded(session.doc, session.selected)) {
            if (ref.kind != ObjectKind::Atom) continue;
            if (const auto* atom = session.doc.atoms().alive(ref.id)) {
                if (atoms.empty()) bounds = Box(atom->position, atom->position);
                else bounds.add(atom->position);
                atoms.insert(ref.id);
            }
        }
        if (atoms.size() < 3 || bounds.empty()) return false;

        cancel();
        session.cancel();
        document_ = session.doc.id();
        serial_ = session.serial;
        pivot_ = (bounds.min + bounds.max) * .5;
        press_ = point;
        perspectiveAtoms_ = std::move(atoms);
        mode_ = Mode::Perspective;
        return true;
    }

    void wrapEdit(const Session& session, Vec2 target) {
        const auto* record = session.doc.texts().alive(text_);
        if (!record) return;
        const auto minWidth = std::max(2., (record->size > 0 ? record->size : session.doc.defaultTextSize()) * .5);
        const bool right = handle_ == 4;
        const auto edge = (right ? box_.max.x : box_.min.x) + target.x - press_.x;
        const auto left = right ? box_.min.x : std::min(edge, box_.max.x - minWidth);
        const auto width = right ? std::max(edge - left, minWidth) : box_.max.x - left;
        const auto align = record->format.align;
        const auto anchor = left + width * (align == 1 ? .5 : align == 2 ? 1. : 0.);
        const auto id = text_;
        const auto y = record->position.y;
        edit_ = [id, anchor, width, y](Document& doc) {
            auto error = doc.setTextPosition(id, Vec2(anchor, y));
            if (error.ok()) {
                const ObjectRef ref(ObjectKind::Text, id);
                doc.noteTouchedExtensions(ref, false);
                doc.extensions(ref).set("studio.text.width", std::to_string(width));
            }
            return error;
        };
    }

public:
    const std::vector<uint8_t>& movingPrimitives() const { return movingPrimitives_; }
    static std::array<Vec2,9> frameHandles(const Session& session,const Box& bounds) {
        const auto unit = session.view().deviceRadiusToDocument(session.view().dpiScale * .75);
        const Box frame(bounds.min - Vec2(unit * 3, unit * 3), bounds.max + Vec2(unit * 3, unit * 3));
        const auto center = (frame.min + frame.max) * .5;
        return{frame.min, Vec2(center.x, frame.min.y),
            Vec2(frame.max.x, frame.min.y), Vec2(frame.min.x, center.y), Vec2(frame.max.x, center.y),
            Vec2(frame.min.x, frame.max.y), Vec2(center.x, frame.max.y), frame.max,
            Vec2(center.x, frame.min.y - 12 * unit)};
    }
    static int handleAt(const Session& session,const std::array<Vec2,9>& handles,Vec2 pixel) {
        const bool singleBond = session.selected.size() == 1 && session.selected.front().kind == ObjectKind::Bond;
        int hit = -1;
        double best = 6.;
        for (size_t i = singleBond ? 8 : 0; i < handles.size(); ++i) {
            const auto distance = length(session.view().toDevice(handles[i]) - pixel);
            if (distance <= best) { hit = int(i); best = distance; }
        }
        return hit;
    }
    const char* cursor(Session& session,Vec2 pixel) const {
        if(mode_==Mode::Rotate || mode_==Mode::Bond || mode_==Mode::Attached)return "grabbing";
        if(mode_==Mode::Wrap)return "ew-resize";
        if(!studioio::selectionHasFrame(session))return "";
        auto bounds=studioio::selectionBounds(session);
        if(bounds.empty())return "";
        const int i=mode_==Mode::Scale?handle_:handleAt(session,frameHandles(session,bounds),pixel);
        if(i==8)return "grab";
        if(i==1||i==6)return "ns-resize";
        if(i==3||i==4)return "ew-resize";
        if(i==0||i==7)return "nwse-resize";
        if(i==2||i==5)return "nesw-resize";
        return studioio::inside(bounds,session.view().toDocument(pixel),session.view().deviceRadiusToDocument(3))?"grab":"";
    }
    Error beginCopy(Session& session, Vec2 point) {
        auto source = std::make_shared<Session>();
        session.cancel();
        if (session.selected.empty()) return {};
        copyWholeDocument_ = session.selected.size() >= 200 && session.selected == session.all();
        copyProjectionMask_.clear();
        if (copyWholeDocument_) {
            copyProjectionMask_.assign(session.scene.size()+2, 1);
            copyProjectionMask_[0] = copyProjectionMask_[1] = 0;
        }
        if (copyWholeDocument_ || (session.selected.size() >= 200 && copyMolecularScene(session, source->scene, copyProjectionMask_))) {
            // The release handler checks the document and session serial before
            // duplicating these live objects in a single undoable command.
            source->selected = session.selected;
        } else {
            auto result = studioio::append(session.doc, source->doc, session.selected, Vec2(), nullptr);
            if (!result.ok()) return result;
            result = source->buildScene();
            if (!result.ok()) return result;
        }
        copySource_ = std::move(source);
        copyDelta_ = {};
        press_ = point;
        serial_ = session.serial;
        document_ = session.doc.id();
        mode_ = Mode::Copy;
        return {};
    }
    const std::shared_ptr<Session>& copySource() const { return copySource_; }
    const std::vector<uint8_t>& copyProjectionMask() const { return copyProjectionMask_; }
    bool hasLiveCopyProjection(const Session& session) const { return !copyProjectionMask_.empty() && matches(session); }
    bool copyUsesDocumentScene(const Session& session) const { return copyWholeDocument_ && matches(session); }
    Vec2 copyDelta() const { return copyDelta_; }
    bool pending() const { return mode_ != Mode::None; }
    bool transformingSelection() const {
        return edit_ && (mode_ == Mode::Rotate || mode_ == Mode::Scale || mode_ == Mode::Bond);
    }
    bool tilting() const { return mode_ == Mode::Perspective; }

    void cancelTransformForZoom() {
        if (mode_ == Mode::Bond || mode_ == Mode::Scale || mode_ == Mode::Rotate ||
                mode_ == Mode::Perspective || mode_ == Mode::Wrap) cancel();
    }

    void cancel() {
        mode_ = Mode::None;
        copySource_.reset();
        copyProjectionMask_.clear();
        copyWholeDocument_ = false;
        refs_.clear();
        perspectiveAtoms_.clear();
        edit_ = {};
        preview_.reset();
        arrowCache_.clear();
        transformScene_.clear();
        transformSlots_.clear();
        movingPrimitives_.clear();
        fastTransform_ = false;
        attached_ = {};
        added_.reset();
        marker_ = {};
    }

    bool deleteMarker(Session& session, Error& result) {
        if (pending() || !hasMarker(session)) return false;
        const auto marker = marker_;
        cancel();
        result = session.submit(studio::electron_marker::remove(marker), "删除电子标记");
        if (result.ok()) session.status = "已删除电子标记，氢数已按剩余成键更新";
        return true;
    }

    void drawMarker(const Session& session, render::OverlayScene& overlay) const {
        if (!hasMarker(session)) return;
        render::Primitive edge;
        edge.kind = render::PrimKind::Polyline;
        edge.layer = render::LayerId(render::layers::kSelection);
        edge.style.stroke = render::Paint(0x0078D4FFu, true);
        edge.style.fill.enabled = false;
        edge.style.strokeWidth = session.view().deviceRadiusToDocument(1.5);
        if (mode_ == Mode::ElectronMarker && edit_) {
            const auto& atom = *session.doc.atoms().alive(marker_.atom);
            const auto body = arrows::mechanismAtomBodyBox(session.doc, atom);
            const auto center = (body.min + body.max) * .5;
            const auto radius = session.view().deviceRadiusToDocument(14);
            for (int index = 0; index <= 32; ++index)
                edge.points.push_back(center + direction(index * 360. / 32.) * radius);
            edge.bounds = Box(center - Vec2(radius, radius), center + Vec2(radius, radius));
        } else {
            const auto outline = studio::electron_marker::bounds(session.doc, marker_);
            if (!outline) return;
            const auto pad = session.view().deviceRadiusToDocument(4);
            const auto min = outline->min - Vec2(pad, pad), max = outline->max + Vec2(pad, pad);
            edge.points = {min, Vec2(max.x, min.y), max, Vec2(min.x, max.y), min};
            edge.bounds = Box(min, max);
        }
        overlay.add(edge);
    }

    bool begin(Session& session, Vec2 pixel, guint modifiers, bool perspective = false) {
        cancel();
        const auto& tool = session.entries[session.activeIndex];
        const bool selectionTool = tool.owner == "T05";
        if (tool.owner == "T09" || tool.command.find("Eraser") != std::string::npos ||
                (modifiers & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) return false;
        const auto point = session.view().toDocument(pixel);
        if (perspective) return selectionTool && beginPerspective(session, point);
        const double labelTolerance = session.view().deviceRadiusToDocument(3);
        const bool nearArtwork = session.nearAtomArtwork(point, labelTolerance);
        if (nearArtwork && ((modifiers & GDK_SHIFT_MASK) || !dynamic_cast<const arrows::ArrowTool*>(tool.tool.get()))) {
            const auto marker = studio::electron_marker::at(session.doc, point,
                session.view().deviceRadiusToDocument(selectionTool ? 3 : 2), selectionTool);
            if (marker.atom != kInvalidObjectId) {
                session.cancel();
                selectMarker(session, marker);
                press_ = point;
                document_ = session.doc.id();
                serial_ = session.serial;
                mode_ = Mode::ElectronMarker;
                session.status = "已选中电子标记 · 拖动调整位置，Delete 删除";
                return true;
            }
        }
        if (!selectionTool) return false;
        const auto attached = studio::attached_atoms::hitTest(session.doc, session.glyphs, point, labelTolerance, nearArtwork);
        if (attached.target.valid()) {
            cancel();
            session.cancel();
            document_ = session.doc.id();
            serial_ = session.serial;
            press_ = point;
            attached_ = attached;
            mode_ = Mode::Attached;
            added_ = std::make_shared<ObjectId>(kInvalidObjectId);
            session.status = attached.terminal ? "拉出末端原子" : "拉出附着氢";
            return true;
        }
        const auto selection = session.selected;
        const auto bounds = studioio::selectionBounds(session);
        const bool hasFrame = studioio::selectionHasFrame(session) && !bounds.empty();
        const auto handles=hasFrame ? frameHandles(session,bounds) : std::array<Vec2,9>{};
        const auto center=(bounds.min+bounds.max)*.5;
        const auto unit=session.view().deviceRadiusToDocument(session.view().dpiScale*.75);
        const bool singleBond=selection.size()==1 && selection.front().kind==ObjectKind::Bond;
        const int hit=hasFrame ? handleAt(session,handles,pixel) : -1;
        const bool inside=hasFrame && studioio::inside(bounds,point,unit*3);
        const bool singleAtom=selection.size()==1 && selection.front().kind==ObjectKind::Atom;
        if (hit<0 && !(modifiers & GDK_SHIFT_MASK) && (!inside || singleAtom)) {
            const auto target=session.pick(point);
            const auto* bond=target.kind==ObjectKind::Bond ? session.doc.bonds().alive(target.id) : nullptr;
            if (bond && bond->order==BondOrder::Single && bond->semantics==BondSemantics::Normal) {
                session.setSelection({target});
                document_=session.doc.id();
                if (!beginBond(session,target.id,point)) return false;
                if (pending()) session.cancel();
                serial_=session.serial;
                return true;
            }
        }
        if (!hasFrame) return false;
        if (hit < 0 && !(singleBond && inside)) return false;
        if (hit >= 0 && hit != 8 && inside && selection.size() > 1 &&
                session.pick(point).kind == ObjectKind::Bond) return false;
        cancel();
        document_ = session.doc.id();
        serial_ = session.serial;
        if (singleBond) {
            if (!beginBond(session, selection.front().id, point)) return false;
        } else {
            refs_ = selection;
            if (session.dragFixedLength) {
                const auto expanded = studioio::expanded(session.doc, refs_);
                const auto selected = [&](ObjectId id) { return expanded.count(ObjectRef(ObjectKind::Atom, id)); };
                const bool hasAtoms = std::any_of(expanded.begin(), expanded.end(),
                    [](auto ref) { return ref.kind == ObjectKind::Atom; });
                if (hasAtoms) {
                    const auto atom = session.nearestAtom(point, 8);
                    if (atom != kInvalidObjectId && selected(atom)) {
                        session.setSelection({session.topLevelRef(ObjectRef(ObjectKind::Atom, atom))});
                        return false;
                    }
                }
                if (hasAtoms && hit != 8) return false;
                if (hasAtoms && std::any_of(session.doc.bonds().records().begin(), session.doc.bonds().records().end(),
                        [&](const auto& bond) { return bond.isAlive() && selected(bond.beginAtom) != selected(bond.endAtom); })) {
                    session.status = "键长锁定中，请拖动原子调整键角";
                    return true;
                }
            }
            press_ = point;
            handle_ = hit;
            if ((hit == 3 || hit == 4) && beginWrap(session, bounds)) {
                session.status = "拖动文字框边缘调整宽度";
            } else {
                pivot_ = hit == 8 ? center : handles[7 - hit];
                mode_ = hit == 8 ? Mode::Rotate : Mode::Scale;
                const auto owned = studioio::expanded(session.doc, refs_);
                const auto& primitives = session.scene.primitives();
                // Transform the selected scene primitives just as the Windows
                // canvas does. A bond spanning the selection boundary still
                // needs its moving endpoint rebuilt by the geometry preview.
                const bool complete = std::all_of(primitives.begin(), primitives.end(), [&](const auto& p) {
                    return !p.source.valid() || owned.count(p.source);
                });
                if (session.gpuScene.size() >= primitives.size() && (complete || closedMolecularSelection(session.doc, owned))) {
                    for (size_t i = 0; i < primitives.size(); ++i)
                        if (owned.count(primitives[i].source)) transformSlots_.push_back(i);
                    fastTransform_ = !transformSlots_.empty();
                    if (fastTransform_) {
                        transformScene_ = session.gpuScene;
                        if (transformSlots_.size() < transformScene_.size() / 2 && transformScene_.size() >= 200) {
                            movingPrimitives_.assign(transformScene_.size(), 0);
                            const auto offset = transformScene_.size() - primitives.size();
                            for (auto index : transformSlots_) movingPrimitives_[offset + index] = 1;
                        }
                    }
                }
                session.status = hit == 8 ? "旋转选区 · Shift 按 15° 调整" : "调整选区大小";
            }
        }
        if (pending()) session.cancel();
        serial_ = session.serial;
        return true;
    }

    void update(const Session& session, Vec2 pixel, bool snap) {
        if (!matches(session)) { cancel(); return; }
        const auto point = session.view().toDocument(pixel);
        if (mode_ == Mode::Copy) {
            copyDelta_ = point - press_;
            if (snap) {
                if (std::abs(copyDelta_.x) >= std::abs(copyDelta_.y)) copyDelta_.y = 0;
                else copyDelta_.x = 0;
            }
            return;
        }
        refreshPreview_ = true;
        edit_ = {};
        if (mode_ == Mode::ElectronMarker) {
            const auto delta = point - press_;
            if (length(delta) < session.view().deviceRadiusToDocument(3)) return;
            const auto* atom = session.doc.atoms().alive(marker_.atom);
            if (!atom) return;
            const auto host = marker_.site == 3 ? arrows::mechanismVisualBoxes(session.doc).at(marker_.atom).lonePairHost :
                atom->position;
            const auto em = std::max(.1, integration::molecular::setting(session.doc, "LabelSize", 10));
            auto desired = marker_.center + delta - host;
            if (length(desired) < 1e-8) desired = (marker_.center - host) * .01;
            const auto offset = integration::molecular::boundedElectronOffset(desired, em);
            const auto command = studio::electron_marker::move(session.doc, marker_, host, offset, em);
            edit_ = [command](Document& document) {
                CommandContext context(document);
                return command.execute(context).error;
            };
            return;
        }
        if (mode_ == Mode::Attached) {
            if (length(point - press_) < session.view().deviceRadiusToDocument(4)) return;
            const auto direction = attached_.center + point - press_ - attached_.host;
            const auto distance = length(direction);
            if (distance < 1e-8) return;
            const double unit = session.doc.defaultBondLength();
            const auto position = attached_.host + direction * (std::clamp(distance, unit, unit * 1.5) / distance);
            edit_ = [site = attached_, position, added = added_](Document& document) {
                auto host = site.target.id;
                if (site.target.kind == ObjectKind::Text) {
                    const auto error = studio::attached_atoms::materializeHydrogenCaption(document, host, site.host, &host);
                    if (!error.ok()) return error;
                }
                return studio::attached_atoms::exposeAttachedProton(document, host, position, added.get(),
                    site.front, site.terminal);
            };
            return;
        }
        if (length(point - press_) < session.view().deviceRadiusToDocument(1)) return;
        if (mode_ == Mode::Wrap) { wrapEdit(session, point); return; }
        if (mode_ == Mode::Perspective) {
            auto delta = point - press_;
            if (snap) {
                if (std::abs(delta.x) > std::abs(delta.y)) delta.y = 0;
                else delta.x = 0;
            }
            const auto yaw = -.020 * delta.x, pitch = .020 * delta.y;
            edit_ = [atoms = perspectiveAtoms_, pivot = pivot_, yaw, pitch](Document& doc) {
                return structure_perspective::tilt(doc, atoms, pivot, yaw, pitch);
            };
            return;
        }
        auto from = press_ - pivot_;
        const auto to = point - pivot_;
        const auto refs = refs_;
        if (mode_ == Mode::Bond || mode_ == Mode::Rotate) {
            if (length(to) <= session.view().deviceRadiusToDocument(1.5)) return;
            if (mode_ == Mode::Bond && length(from) <= session.view().deviceRadiusToDocument(1.5)) from = bondVector_;
            const auto degrees = std::atan2(from.x * to.y - from.y * to.x, dot(from, to)) * 180. / 3.141592653589793;
            const auto angle = snap ? std::round(degrees / 15.) * 15. : degrees;
            if (std::abs(angle) < 1e-8) return;
            if (mode_ == Mode::Bond) {
                const auto delta = Affine2::rotation(Vec2(), -angle).apply(bondVector_) - bondVector_;
                edit_ = [refs, delta](Document& doc) { return integration::translateDocumentObjects(doc, refs, delta); };
            } else {
                const auto matrix = transform_ = Affine2::rotation(pivot_, -angle);
                edit_ = [refs, matrix](Document& doc) { return applyMatrix(doc, refs, matrix); };
            }
            return;
        }
        const bool vertical = handle_ == 1 || handle_ == 6;
        const bool horizontal = handle_ == 3 || handle_ == 4;
        const auto axis = vertical ? from.y : horizontal ? from.x : dot(from, from);
        if (std::abs(axis) < 1e-9) return;
        const auto factor = std::clamp((vertical ? to.y : horizontal ? to.x : dot(to, from)) / axis, .01, 100.);
        const bool graphics = std::all_of(refs.begin(), refs.end(),
            [](auto ref) { return ref.kind == ObjectKind::Arrow || ref.kind == ObjectKind::Bracket; });
        const auto matrix = transform_ = Affine2::scaling(pivot_, graphics && vertical ? 1. : factor,
                                                    graphics && horizontal ? 1. : factor);
        edit_ = [refs, matrix](Document& doc) { return applyMatrix(doc, refs, matrix); };
    }

    const render::Scene* transformScene(const Session& s) {
        if (!fastTransform_ || !matches(s)) return nullptr;
        if (!edit_) return &s.gpuScene;
        if (refreshPreview_) {
            const auto offset=s.gpuScene.size()-s.scene.size();
            ++frame_;
            for (auto index:transformSlots_) {
                auto& p=transformScene_.mutablePrimitives()[offset+index];
                p=s.gpuScene.primitives()[offset+index];
                studio_transform_preview::apply(s.doc,s.scene.primitives()[index],transform_,p);
                p.version=(p.version^(frame_*0x9E3779B97F4A7C15ull))|1ull;
            }
            transformScene_.touch();
            refreshPreview_=false;
        }
        return &transformScene_;
    }
    Session* preview(Session& session) {
        if (!matches(session) || !edit_) return nullptr;
        if (!preview_) preview_ = std::make_unique<Session>();
        if (refreshPreview_) {
            arrows::PreviewArrowCache::Scope cachedArrows(arrowCache_);
            preview_->doc = session.doc;
            if (!edit_(preview_->doc).ok()) return nullptr;
            preview_->whiteCanvas = session.whiteCanvas;
            if (transformingSelection()) {
                preview_->refreshDrawingPreview();
            } else {
                preview_->setSelection(session.selected);
                preview_->refresh();
            }
            refreshPreview_ = false;
        }
        preview_->setView(session.view());
        return preview_.get();
    }

    Error finish(Session& session, Vec2 pixel, bool snap) {
        update(session, pixel, snap);
        auto edit = std::move(edit_);
        const auto mode = mode_;
        const auto attached = attached_;
        const auto marker = marker_;
        auto added = std::move(added_);
        cancel();
        if (!edit) {
            if (mode == Mode::Attached) session.setSelection({attached.target});
            if (mode == Mode::ElectronMarker) selectMarker(session, marker);
            return {};
        }
        const auto* label = mode == Mode::Attached ? (attached.terminal ? "拉出末端原子" : "拉出附着氢") :
            mode == Mode::Perspective ? "结构透视" : mode == Mode::Bond ? "旋转单键" :
            mode == Mode::Wrap ? "文字框宽度" : mode == Mode::ElectronMarker ? "移动电子标记" : "选区缩放/旋转";
        const auto invalidation = kInvalidateGeometry | kInvalidateAppearance |
            (mode == Mode::Attached ? kInvalidateStructure | kInvalidateSemantics : 0);
        auto result = session.submit(Command(label, invalidation,
            [edit](CommandContext& context) {
                const auto error = edit(context.document);
                return error.ok() ? CommandResult::success() : CommandResult::fail(error.code, error.message);
            }), label);
        if (result.ok() && added) session.setSelection({ObjectRef(ObjectKind::Atom, *added)});
        if (result.ok() && mode == Mode::ElectronMarker) selectMarker(session, marker);
        return result;
    }
};

}
