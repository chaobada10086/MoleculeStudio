// tests/T05/selection/t05_demo.cpp
//
// A runnable demonstration that also writes GEOMETRY EVIDENCE. Two outputs:
//
//   1. Console steps, each printing the exact numbers it checked, so the demo is
//      readable without opening anything.
//   2. An SVG per scenario under out/T05/geometry/. The SVG is not decoration:
//      it draws the BEFORE and AFTER positions of every object, so "the rotate
//      moved only the atoms" or "the copy did not move the original" is visible
//      AND diffable as text. A screenshot could not be diffed; this can.
//
// The demo links clipboard_win32.cpp, so it is also the only place the REAL
// Win32 clipboard is exercised - and it does so on a COPY of its own document,
// never on a user's file.
#include "test_support.hpp"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

using namespace r62::selection;

#ifdef R62_T05_WIN32
#include "next/selection/clipboard_win32.hpp"
#endif

namespace {

// ---------------------------------------------------------------------------
// A tiny SVG writer. Deliberately text-only: the output is diffable.
// ---------------------------------------------------------------------------
class SvgWriter {
public:
    SvgWriter(double minX, double minY, double maxX, double maxY, double scale)
        : minX_(minX), minY_(minY), maxX_(maxX), maxY_(maxY), scale_(scale) {}

    void header(std::ostream& out, const std::string& title) {
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
            << (maxX_ - minX_) * scale_ << "\" height=\""
            << (maxY_ - minY_) * scale_ << "\" viewBox=\"0 0 "
            << (maxX_ - minX_) * scale_ << " " << (maxY_ - minY_) * scale_ << "\">\n";
        // White background so a reader does not depend on their viewer theme.
        out << "<rect x=\"0\" y=\"0\" width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
        out << "<title>" << title << "</title>\n";
    }
    void footer(std::ostream& out) { out << "</svg>\n"; }

    double sx(double x) const { return (x - minX_) * scale_; }
    double sy(double y) const { return (y - minY_) * scale_; }

    void circle(std::ostream& out, const r62::core::Vec2& p, double radius,
                const std::string& stroke, const std::string& fill, double width) {
        out << "<circle cx=\"" << sx(p.x) << "\" cy=\"" << sy(p.y) << "\" r=\""
            << radius * scale_ << "\" fill=\"" << fill << "\" stroke=\"" << stroke
            << "\" stroke-width=\"" << width << "\"/>\n";
    }
    void line(std::ostream& out, const r62::core::Vec2& a, const r62::core::Vec2& b,
              const std::string& stroke, double width, const std::string& dash = "") {
        out << "<line x1=\"" << sx(a.x) << "\" y1=\"" << sy(a.y) << "\" x2=\""
            << sx(b.x) << "\" y2=\"" << sy(b.y) << "\" stroke=\"" << stroke
            << "\" stroke-width=\"" << width << "\"";
        if (!dash.empty()) out << " stroke-dasharray=\"" << dash << "\"";
        out << "/>\n";
    }
    void rect(std::ostream& out, const r62::core::Box& box, const std::string& stroke,
              const std::string& fill, double width, const std::string& dash = "") {
        if (box.empty()) return;
        out << "<rect x=\"" << sx(box.min.x) << "\" y=\"" << sy(box.min.y) << "\" width=\""
            << (box.max.x - box.min.x) * scale_ << "\" height=\""
            << (box.max.y - box.min.y) * scale_ << "\" fill=\"" << fill
            << "\" stroke=\"" << stroke << "\" stroke-width=\"" << width << "\"";
        if (!dash.empty()) out << " stroke-dasharray=\"" << dash << "\"";
        out << "/>\n";
    }
    void text(std::ostream& out, const r62::core::Vec2& p, const std::string& value,
              const std::string& fill) {
        out << "<text x=\"" << sx(p.x) << "\" y=\"" << sy(p.y) << "\" fill=\"" << fill
            << "\" font-family=\"monospace\" font-size=\"11\">" << value << "</text>\n";
    }

private:
    double minX_, minY_, maxX_, maxY_, scale_;
};

// Draw the document's atoms and bonds in one colour, and a comparison set in
// another. Overlaying BEFORE and AFTER is what makes a missed object visible.
void drawDocument(std::ostream& out, SvgWriter& svg, const r62::core::Document& document,
                  const std::string& stroke, double atomRadius, double bondWidth) {
    for (size_t i = 0; i < document.bonds().records().size(); ++i) {
        const r62::core::BondRecord& bond = document.bonds().records()[i];
        if (!bond.isAlive()) continue;
        const r62::core::AtomRecord* a = document.atoms().alive(bond.beginAtom);
        const r62::core::AtomRecord* b = document.atoms().alive(bond.endAtom);
        if (!a || !b) continue;
        svg.line(out, a->position, b->position, stroke, bondWidth);
    }
    for (size_t i = 0; i < document.atoms().records().size(); ++i) {
        const r62::core::AtomRecord& atom = document.atoms().records()[i];
        if (!atom.isAlive()) continue;
        svg.circle(out, atom.position, atomRadius, stroke, "#ffffff", 1.0);
    }
}

#include <sys/stat.h>
#include <sys/types.h>
#ifdef R62_T05_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
// MinGW's mkdir takes ONE argument (there is no mode), so the helper hides that
// difference instead of a #ifdef at every call site.
namespace { inline int makeDirectory(const char* p) { return ::mkdir(p); } }
bool writeFile(const std::string& path, const std::string& contents) {
    // Create the parent directory first: a demo that silently writes nothing
    // because the directory is missing is worse than one that fails loudly.
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        const std::string dir = path.substr(0, slash);
#ifdef R62_T05_WIN32
        CreateDirectoryA(dir.c_str(), 0);
#else
        makeDirectory(dir.c_str());
#endif
    }
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out << contents;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    t05::Harness harness;
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const std::string geometryDir = outDir + "/geometry";

    harness.note("output directory: " + outDir);
    {
        // The geometry directory is created once, up front, so every scenario
        // below can write without repeating the setup.
        const std::string dir = geometryDir;
#ifdef R62_T05_WIN32
        CreateDirectoryA(dir.c_str(), 0);
#else
        makeDirectory(dir.c_str());
#endif
    }

    // =====================================================================
    // 1. Rotation: text, path, arrow and table must ALL move, and a bond's
    //    order must not change.
    // =====================================================================
    harness.suite("demo.rotate");
    {
        t05::MixedFixture before;
        r62::core::Document after = before.document;
        std::vector<r62::core::ObjectRef> refs = before.all;
        const r62::core::Vec2 pivot(20.0, 20.0);
        const TransformReport moved =
            rotateObjects(after, refs, pivot, 90.0, TransformOptions());
        harness.check(moved.atomsMoved > 0 && moved.textsMoved > 0 &&
                      moved.pathsMoved > 0 && moved.annotationsMoved > 0 &&
                      moved.tablesMoved > 0,
                      "everyKindRotated", moved.summary());
        const r62::core::BondRecord& bond = *after.bonds().get(before.bond.id);
        harness.check(bond.order == r62::core::BondOrder::Double &&
                      bond.display == r62::core::BondDisplay::Normal &&
                      bond.stereo == r62::core::BondStereo::None,
                      "bondSemanticsUntouched",
                      "order=" + std::to_string(static_cast<int>(bond.order)));

        std::ostringstream svg;
        SvgWriter writer(-20, -20, 130, 90, 4.0);
        writer.header(svg, "T05 rotate 90 degrees: grey=before, blue=after");
        drawDocument(svg, writer, before.document, "#9e9e9e", 2.0, 1.5);
        drawDocument(svg, writer, after, "#1a73e8", 2.0, 1.5);
        writer.text(svg, r62::core::Vec2(-18, -8), "grey = before, blue = after", "#333333");
        writer.footer(svg);
        const bool written = writeFile(geometryDir + "/rotate90.svg", svg.str());
        harness.check(written, "rotateSvgWritten", geometryDir + "/rotate90.svg");
    }

    // =====================================================================
    // 2. Copy: the original does NOT move and no copied bond touches it.
    // =====================================================================
    harness.suite("demo.copy");
    {
        t05::BenzeneFixture fixture;
        std::vector<r62::core::ObjectRef> refs;
        for (size_t i = 0; i < fixture.atoms.size(); ++i)
            refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[i]));
        for (size_t i = 0; i < fixture.bonds.size(); ++i)
            refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Bond, fixture.bonds[i]));
        refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Text, fixture.caption));

        const r62::core::Box beforeBounds = selectionBounds(fixture.document, refs);
        std::string package;
        r62::persistence::SelectionPackageInfo info;
        r62::persistence::encodeSelectionPackage(fixture.document, refs, &package, &info);
        const PasteReport pasted = pastePackage(fixture.document, package,
                                                r62::core::Vec2(60, 0));
        const r62::core::Box afterBounds = selectionBounds(fixture.document, refs);
        harness.check(pasted.pasted && beforeBounds == afterBounds,
                      "originalDidNotMoveAfterCopy",
                      "min=" + t05::number(afterBounds.min.x) + "," + t05::number(afterBounds.min.y));
        // Count how many bonds touch an ORIGINAL atom but are not an original bond.
        std::set<r62::core::ObjectId> originalAtoms(fixture.atoms.begin(), fixture.atoms.end());
        std::set<r62::core::ObjectId> originalBonds(fixture.bonds.begin(), fixture.bonds.end());
        size_t offending = 0;
        for (size_t i = 0; i < fixture.document.bonds().records().size(); ++i) {
            const r62::core::BondRecord& bond = fixture.document.bonds().records()[i];
            if (!bond.isAlive() || originalBonds.count(bond.id)) continue;
            if (originalAtoms.count(bond.beginAtom) || originalAtoms.count(bond.endAtom)) ++offending;
        }
        harness.check(offending == 0 && pasted.objectsInserted == 13,
                      "copyIsASeparateMolecule",
                      "inserted=" + std::to_string(pasted.objectsInserted) +
                      " bondsBackToOriginal=" + std::to_string(offending));

        std::ostringstream svg;
        SvgWriter writer(-30, -50, 130, 40, 4.0);
        writer.header(svg, "T05 copy: grey=original, blue=pasted copy");
        // Draw only the originals in grey first, then everything in blue: the
        // overlap makes an accidental move visible.
        r62::core::Document originalOnly;
        (void)originalOnly;
        drawDocument(svg, writer, fixture.document, "#1a73e8", 2.0, 1.5);
        writer.rect(svg, beforeBounds, "#9e9e9e", "none", 1.0, "3,2");
        writer.text(svg, r62::core::Vec2(-28, -42), "dashed = original bounds (unmoved)", "#333333");
        writer.footer(svg);
        harness.check(writeFile(geometryDir + "/copy_ring.svg", svg.str()),
                      "copySvgWritten", geometryDir + "/copy_ring.svg");
    }

    // =====================================================================
    // 3. Marquee: the exact hit set, drawn as the region plus the hits.
    // =====================================================================
    harness.suite("demo.marquee");
    {
        t05::BenzeneFixture fixture;
        DocumentSelectionQuery query(fixture.document);
        MarqueeGesture gesture;
        gesture.anchor = r62::core::Vec2(-20, -20);
        gesture.current = r62::core::Vec2(20, 5);
        MarqueeOptions options;
        options.mode = MarqueeMode::Contained;
        const std::vector<r62::core::ObjectRef> hits = queryMarquee(query, gesture, options, 0);
        // 4 of the 6 ring atoms fall inside (-20,-20)..(20,5); the two upper
        // vertices (y = 12.47) do not. Three bonds lie wholly inside. The two
        // upper bonds and the caption are excluded, which is the point.
        harness.check(hits.size() == 7, "containedMarqueeHitCount",
                      "hits=" + std::to_string(hits.size()) +
                      " (4 atoms + 3 bonds; upper vertices and the caption excluded)");

        std::ostringstream svg;
        SvgWriter writer(-30, -50, 40, 40, 5.0);
        writer.header(svg, "T05 marquee: contained vs intersecting");
        drawDocument(svg, writer, fixture.document, "#9e9e9e", 1.5, 1.0);
        writer.rect(svg, r62::core::Box(gesture.anchor, gesture.current),
                    "#1a73e8", "rgba(26,115,232,0.08)", 1.5, "4,2");
        for (size_t i = 0; i < hits.size(); ++i) {
            const r62::core::Box box = objectBounds(fixture.document, hits[i]);
            writer.rect(svg, box, "#d93025", "none", 1.0);
        }
        writer.text(svg, r62::core::Vec2(-28, -42), "blue = marquee, red = contained hits", "#333333");
        writer.footer(svg);
        harness.check(writeFile(geometryDir + "/marquee_contained.svg", svg.str()),
                      "marqueeSvgWritten", geometryDir + "/marquee_contained.svg");
    }

    // =====================================================================
    // 4. Align + distribute: geometry evidence that references survive.
    // =====================================================================
    harness.suite("demo.align");
    {
        r62::core::Document document;
        r62::core::ObjectId ids[4];
        const double xs[4] = { 0.0, 37.0, 61.0, 100.0 };
        for (int i = 0; i < 4; ++i) document.addAtom(r62::core::Vec2(xs[i], 0), 6, &ids[i]);
        r62::core::ObjectId bondA = r62::core::kInvalidObjectId;
        r62::core::ObjectId bondB = r62::core::kInvalidObjectId;
        r62::core::BondRecord record;
        record.beginAtom = ids[0]; record.endAtom = ids[1];
        record.order = r62::core::BondOrder::Double;
        document.addBondRecord(record, &bondA);
        record.beginAtom = ids[2]; record.endAtom = ids[3];
        record.order = r62::core::BondOrder::Triple;
        document.addBondRecord(record, &bondB);

        std::vector<r62::core::ObjectRef> refs;
        for (int i = 0; i < 4; ++i)
            refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, ids[i]));

        const DistributeReport distributed =
            distributeObjects(document, refs, DistributeMode::HorizontalCenters);
        harness.check(distributed.applied && distributed.objectsMoved == 2,
                      "distributeMovedInnerObjectsOnly",
                      "moved=" + std::to_string(distributed.objectsMoved) +
                      " considered=" + std::to_string(distributed.objectsConsidered));
        harness.check(document.atoms().get(ids[0])->position.x == 0.0 &&
                      document.atoms().get(ids[3])->position.x == 100.0,
                      "extremesUnmoved",
                      "left=" + t05::number(document.atoms().get(ids[0])->position.x) +
                      " right=" + t05::number(document.atoms().get(ids[3])->position.x));
        harness.check(document.bonds().get(bondA)->order == r62::core::BondOrder::Double &&
                      document.bonds().get(bondB)->order == r62::core::BondOrder::Triple,
                      "bondOrdersSurvivedDistribute",
                      "double=" + std::to_string(static_cast<int>(document.bonds().get(bondA)->order)) +
                      " triple=" + std::to_string(static_cast<int>(document.bonds().get(bondB)->order)));

        // ALIGN to the LEFT of the selection bounds.
        r62::core::Document aligned;
        r62::core::ObjectId aids[3];
        aligned.addAtom(r62::core::Vec2(0, 0), 6, &aids[0]);
        aligned.addAtom(r62::core::Vec2(30, 10), 6, &aids[1]);
        aligned.addAtom(r62::core::Vec2(50, 20), 6, &aids[2]);
        std::vector<r62::core::ObjectRef> arefs;
        for (int i = 0; i < 3; ++i)
            arefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, aids[i]));
        AlignOptions alignOptions;
        alignObjects(aligned, arefs, AlignMode::Left, alignOptions, 0);
        harness.check(aligned.atoms().get(aids[0])->position.x == 0.0 &&
                      aligned.atoms().get(aids[1])->position.x == 0.0 &&
                      aligned.atoms().get(aids[2])->position.x == 0.0,
                      "alignLeftSnappedAllToZero",
                      "x0=" + t05::number(aligned.atoms().get(aids[0])->position.x) +
                      " x1=" + t05::number(aligned.atoms().get(aids[1])->position.x) +
                      " x2=" + t05::number(aligned.atoms().get(aids[2])->position.x));

        std::ostringstream svg;
        SvgWriter writer(-10, -30, 120, 30, 5.0);
        writer.header(svg, "T05 distribute: grey=before, blue=after");
        // Before positions come from the recorded xs[].
        for (int i = 0; i < 4; ++i)
            writer.circle(svg, r62::core::Vec2(xs[i], 0), 2.0, "#9e9e9e", "#ffffff", 1.0);
        drawDocument(svg, writer, document, "#1a73e8", 2.0, 1.5);
        writer.text(svg, r62::core::Vec2(-8, -22),
                    "grey = before (0,37,61,100), blue = after", "#333333");
        writer.footer(svg);
        harness.check(writeFile(geometryDir + "/distribute.svg", svg.str()),
                      "distributeSvgWritten", geometryDir + "/distribute.svg");
    }

    // =====================================================================
    // 5. Batched overlay: the submission count, per frame.
    // =====================================================================
    harness.suite("demo.overlay");
    {
        t05::GridFixture fixture(5000, false);
        DocumentSelectionQuery query(fixture.document);
        SelectionSet selection;
        selection.addAll(query.allObjects(), SelectionReason::Direct);
        OverlayOptions options;
        OverlayBatch batch = buildSelectionOverlay(fixture.document, selection, options);
        OverlayStats stats;
        for (int frame = 0; frame < 30; ++frame) recordSubmission(&stats, batch);
        harness.check(stats.frames == 30 && stats.submissions == 30,
                      "thirtyFramesThirtySubmissions",
                      stats.summary());
        // The batch was handed over 30 times, so its own ratio is
        // marks/submissions. The STATS ratio is the one that matters: it is the
        // number of marks that travelled in each hand-off.
        harness.check(batch.markCount() >= 5000 &&
                      stats.marksPerSubmission() >= 5000.0 &&
                      stats.submissions == 30,
                      "marksPerSubmissionIsLarge",
                      "marks=" + std::to_string(batch.markCount()) +
                      " statsPerSubmission=" + t05::number(stats.marksPerSubmission()) +
                      " batchSubmissions=" + std::to_string(batch.submissions));
    }

    // =====================================================================
    // 6. The REAL Win32 clipboard, on a document the demo owns.
    // =====================================================================
    harness.suite("demo.clipboard");
#ifdef R62_T05_WIN32
    {
        r62::selection::Win32ClipboardTransport transport;
        harness.note(std::string("win32 clipboard transport: ") + transport.name() +
                     " registeredFormat=" + std::to_string(transport.registeredFormat()));
        t05::BenzeneFixture fixture;
        std::vector<r62::core::ObjectRef> refs;
        for (size_t i = 0; i < fixture.atoms.size(); ++i)
            refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[i]));
        for (size_t i = 0; i < fixture.bonds.size(); ++i)
            refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Bond, fixture.bonds[i]));

        const PublishReport published =
            publishSelection(fixture.document, refs, transport, true);
        harness.check(published.published && published.objectsEncoded == 12,
                      "win32PublishSucceeded",
                      "bytes=" + std::to_string(published.bytes) +
                      " objects=" + std::to_string(published.objectsEncoded) +
                      (published.why.empty() ? "" : (" why=" + published.why)));
        if (published.published) {
            std::string fetched;
            const FetchReport read = fetchSelection(transport, &fetched);
            // The package must come back BYTE-IDENTICAL through the real OS
            // clipboard: a truncation at a NUL byte is the classic failure and
            // is exactly what the registered format exists to avoid.
            const PublishReport republished =
                publishSelection(fixture.document, refs, transport, false);
            harness.check(read.present && !fetched.empty() && republished.published &&
                          fetched.size() == republished.bytes,
                          "win32FetchReturnsTheSamePackage",
                          "fetched=" + std::to_string(fetched.size()) +
                          " bytes republished=" + std::to_string(republished.bytes));
            // Read the package back into a SEPARATE document and check the
            // connectivity survived the real OS round trip.
            r62::core::Document target;
            const PasteReport pasted = pasteSelection(target, transport,
                                                      r62::core::Vec2(50, 50));
            size_t newAtoms = 0, newBonds = 0;
            for (size_t i = 0; i < target.atoms().records().size(); ++i)
                if (target.atoms().records()[i].isAlive()) ++newAtoms;
            for (size_t i = 0; i < target.bonds().records().size(); ++i)
                if (target.bonds().records()[i].isAlive()) ++newBonds;
            harness.check(pasted.pasted && newAtoms == 6 && newBonds == 6,
                          "win32RoundTripKeptConnectivity",
                          "atoms=" + std::to_string(newAtoms) +
                          " bonds=" + std::to_string(newBonds));
        }
        // A failed publish must not be reachable through the real API, so the
        // demo reports what the OS actually did rather than injecting a failure.
        harness.note(std::string("last Win32 error = ") +
                     std::to_string(transport.lastError()));
    }
#else
    harness.note("built without win32 support; the clipboard tests use the "
                 "in-process transport");
#endif

    return harness.finish("T05 selection demo");
}
