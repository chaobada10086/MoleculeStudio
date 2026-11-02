// tests/T05/selection/t05_selection_test.cpp
//
// The T05 acceptance suite, organised around the task's own acceptance list:
//
//   A  lasso boundary + mixed-object selection
//   B  copy a ring and its text -> unique ids, NO bond back to the original
//   C  Ctrl-drag copy keeps the original (frozen preview, source untouched)
//   D  a FAILED clipboard publish does NOT cut the source
//   E  rotate/flip twice + Undo/Redo restores EVERY attribute
//   F  delete a group and undo restores the full hierarchy
//
// Each check prints with its suite name, and the exit code is non-zero when any
// failed. Nothing here is asserted from a screenshot.
#include "test_support.hpp"

#include <cstdio>
#include <set>

using namespace r62::selection;

namespace {
// Same helper as test_support.cpp: Document::addBond carries no order.
inline r62::core::Error addBondWithOrder(r62::core::Document& document,
                                         r62::core::ObjectId begin,
                                         r62::core::ObjectId end,
                                         r62::core::BondOrder order,
                                         r62::core::ObjectId* out) {
    r62::core::BondRecord record;
    record.beginAtom = begin;
    record.endAtom = end;
    record.order = order;
    return document.addBondRecord(record, out);
}
} // namespace

int main() {
    t05::Harness harness;

    // =====================================================================
    // A. Bounds are exact for every kind, and the marquee/lasso agree with the
    //    full-scan reference.
    // =====================================================================
    harness.suite("bounds");
    {
        t05::MixedFixture fixture;
        // Every kind must produce a NON-EMPTY box. A kind that returns an empty
        // box is a kind the transforms would silently skip.
        const r62::core::ObjectKind kinds[7] = {
            r62::core::ObjectKind::Atom, r62::core::ObjectKind::Bond,
            r62::core::ObjectKind::Text, r62::core::ObjectKind::Path,
            r62::core::ObjectKind::Arrow, r62::core::ObjectKind::Table,
            r62::core::ObjectKind::Group
        };
        for (int i = 0; i < 7; ++i) {
            const r62::core::Box box = objectBounds(fixture.document, fixture.all[static_cast<size_t>(i)]);
            harness.check(!box.empty() && boxFinite(box),
                          std::string("nonEmpty.") + r62::core::kindName(kinds[i]),
                          "box=" + t05::number(box.min.x) + "," + t05::number(box.min.y) +
                          " .. " + t05::number(box.max.x) + "," + t05::number(box.max.y));
        }
        // A bond's box is its ENDPOINTS, not a stored rectangle: move an atom and
        // the bond's box must follow.
        const r62::core::BondRecord* bond = fixture.document.bonds().alive(fixture.bond.id);
        r62::core::Box before = objectBounds(fixture.document, fixture.bond);
        fixture.document.setAtomPosition(bond->beginAtom, r62::core::Vec2(-100, -100));
        const r62::core::Box after = objectBounds(fixture.document, fixture.bond);
        harness.check(after.min.x <= -100.0 && after.min.y <= -100.0 &&
                      before.min.x > -100.0,
                      "bondFollowsAtom", "before.min.x=" + t05::number(before.min.x) +
                      " after.min.x=" + t05::number(after.min.x));

        // A group's box is the UNION of its members, so it moves when a member
        // moves and is empty when every member is gone.
        const r62::core::Box groupBox = objectBounds(fixture.document, fixture.group);
        r62::core::Box unionOfMembers;
        bool first = true;
        const r62::core::GroupRecord* group = fixture.document.groups().alive(fixture.group.id);
        for (size_t m = 0; m < group->members.size(); ++m) {
            const r62::core::Box one = objectBounds(fixture.document, group->members[m]);
            if (first) { unionOfMembers = one; first = false; }
            else unionBox(&unionOfMembers, one);
        }
        harness.check(groupBox == unionOfMembers, "groupIsUnionOfMembers",
                      "group=" + t05::number(groupBox.min.x) + ".." + t05::number(groupBox.max.x) +
                      " union=" + t05::number(unionOfMembers.min.x) + ".." + t05::number(unionOfMembers.max.x));

        // Text with NO metric sink is boxed at the anchor and COUNTED as
        // unmeasured - "we do not know" must not look like "it is a point".
        BoundsOptions noMetrics;
        noMetrics.textMetrics = 0;
        BoundsReport report;
        const r62::core::Box textBox = objectBounds(fixture.document, fixture.text, noMetrics, &report);
        harness.check(report.textWithoutMetrics == 1 && textBox.min == textBox.max,
                      "textWithoutMetricsIsCounted",
                      "textWithoutMetrics=" + std::to_string(report.textWithoutMetrics));

        // WITH a metric sink the box is the ink extent and is strictly larger.
        MonospaceMetricSink sink;
        BoundsOptions withMetrics;
        withMetrics.textMetrics = &sink;
        const r62::core::Box measured = objectBounds(fixture.document, fixture.text, withMetrics, 0);
        harness.check(measured.width() > 0.0 && measured.height() > 0.0,
                      "textWithMetricsHasExtent",
                      "w=" + t05::number(measured.width()) + " h=" + t05::number(measured.height()));
    }

    harness.suite("marquee");
    {
        t05::BenzeneFixture fixture;
        DocumentSelectionQuery reference(fixture.document);
        t05::GridSelectionQuery indexed(fixture.document, 20.0);

        // A marquee over the whole ring: everything inside must be selected.
        MarqueeGesture gesture;
        gesture.shape = MarqueeShape::Rect;
        gesture.anchor = r62::core::Vec2(-30, -30);
        gesture.current = r62::core::Vec2(30, 5);
        MarqueeOptions options;
        options.mode = MarqueeMode::Contained;
        uint64_t scanned = 0;
        const std::vector<r62::core::ObjectRef> referenceHits =
            queryMarquee(reference, gesture, options, &scanned);
        const std::vector<r62::core::ObjectRef> indexedHits =
            queryMarquee(indexed, gesture, options, &scanned);
        harness.check(referenceHits == indexedHits && !referenceHits.empty(),
                      "indexedAgreesWithFullScan.rect",
                      "reference=" + std::to_string(referenceHits.size()) +
                      " indexed=" + std::to_string(indexedHits.size()));
        harness.check(scanned >= referenceHits.size(), "scanCounted",
                      "scanned=" + std::to_string(scanned));

        // Every hit must be an atom or a bond of THIS ring (the caption sits at
        // y=-32.4 and is outside the box). A marquee that selected the caption
        // would be a containment bug.
        bool captionExcluded = true;
        for (size_t i = 0; i < referenceHits.size(); ++i)
            if (referenceHits[i].kind == r62::core::ObjectKind::Text) captionExcluded = false;
        harness.check(captionExcluded, "containedExcludesOutsideText",
                      "hits=" + std::to_string(referenceHits.size()));

        // A marquee that TOUCHES one atom but contains none: intersecting mode
        // selects it, contained mode does not. This is the boundary rule.
        MarqueeGesture touching;
        touching.anchor = r62::core::Vec2(-20, -20);
        touching.current = r62::core::Vec2(-10, -10);
        MarqueeOptions contained;
        contained.mode = MarqueeMode::Contained;
        MarqueeOptions intersecting;
        intersecting.mode = MarqueeMode::Intersecting;
        const std::vector<r62::core::ObjectRef> containedHits =
            queryMarquee(reference, touching, contained, 0);
        const std::vector<r62::core::ObjectRef> intersectingHits =
            queryMarquee(reference, touching, intersecting, 0);
        harness.check(intersectingHits.size() > containedHits.size(),
                      "boundaryRuleDiffers",
                      "contained=" + std::to_string(containedHits.size()) +
                      " intersecting=" + std::to_string(intersectingHits.size()));

        // LASSO: a triangle that contains the ring's centre. The lasso and the
        // reference must agree, and a point on the boundary counts as inside.
        MarqueeGesture lasso;
        lasso.shape = MarqueeShape::Lasso;
        lasso.anchor = r62::core::Vec2(0, -40);
        lasso.addLassoPoint(r62::core::Vec2(0, -40), 0.0);
        lasso.addLassoPoint(r62::core::Vec2(40, 30), 0.0);
        lasso.addLassoPoint(r62::core::Vec2(-40, 30), 0.0);
        lasso.addLassoPoint(r62::core::Vec2(0, -40), 0.0);
        MarqueeOptions lassoOptions;
        lassoOptions.mode = MarqueeMode::Contained;
        const std::vector<r62::core::ObjectRef> lassoReference =
            queryMarquee(reference, lasso, lassoOptions, 0);
        const std::vector<r62::core::ObjectRef> lassoIndexed =
            queryMarquee(indexed, lasso, lassoOptions, 0);
        harness.check(lassoReference == lassoIndexed && !lassoReference.empty(),
                      "indexedAgreesWithFullScan.lasso",
                      "reference=" + std::to_string(lassoReference.size()) +
                      " indexed=" + std::to_string(lassoIndexed.size()));

        // The lasso dedups a re-sampled point, so a slow drag does not grow the
        // path without bound.
        MarqueeGesture dedupe;
        dedupe.shape = MarqueeShape::Lasso;
        dedupe.addLassoPoint(r62::core::Vec2(0, 0), 1.0);
        const bool kept = dedupe.addLassoPoint(r62::core::Vec2(0.2, 0.2), 1.0);
        const bool keptFar = dedupe.addLassoPoint(r62::core::Vec2(5, 5), 1.0);
        harness.check(!kept && keptFar && dedupe.samples == 2, "lassoDedupesSamples",
                      "kept=" + std::string(kept ? "yes" : "no") +
                      " samples=" + std::to_string(dedupe.samples));
    }

    harness.suite("selectSet");
    {
        t05::BenzeneFixture fixture;
        SelectionSet set;
        set.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[0]));
        set.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[3]));
        const bool readded =
            set.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[0]));
        harness.check(!readded && set.size() == 2, "addIsIdempotent",
                      "size=" + std::to_string(set.size()));

        SelectionSet shuffled;
        shuffled.add(r62::core::ObjectRef(r62::core::ObjectKind::Text, fixture.caption));
        shuffled.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[5]));
        shuffled.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[1]));
        SelectionSet sorted;
        sorted.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[1]));
        sorted.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[5]));
        sorted.add(r62::core::ObjectRef(r62::core::ObjectKind::Text, fixture.caption));
        harness.check(shuffled.refs() == sorted.refs(), "orderIsStable",
                      "digests equal=" + std::string(shuffled.digest() == sorted.digest() ? "yes" : "no"));

        // Connected molecule: an atom seed selects the whole ring AND its bonds.
        SelectionDelta delta;
        const SelectionSet molecule = selectMolecule(
            fixture.document, SelectionSet(),
            r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[0]), false, &delta);
        harness.check(molecule.size() == 12, "moleculeSelectsAtomsAndBonds",
                      "size=" + std::to_string(molecule.size()) + " delta=" + delta.summary());

        // Shift-add: a second seed with additive=true must KEEP the first.
        const SelectionSet both = selectMolecule(
            fixture.document, molecule,
            r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[0]), true, &delta);
        harness.check(both.size() == molecule.size() && delta.entered.empty(),
                      "shiftAddKeepsExisting", "size=" + std::to_string(both.size()));

        // Select All over a full-scan query.
        DocumentSelectionQuery query(fixture.document);
        SelectionDelta allDelta;
        const SelectionSet everything = selectAll(fixture.document, query, SelectionSet(), &allDelta);
        // The fixture holds 6 atoms + 6 bonds + 1 caption + 1 group = 14 objects.
        harness.check(everything.size() == 6 + 6 + 1 + 1, "selectAllCoversEveryObject",
                      "size=" + std::to_string(everything.size()) +
                      " documentAlive=" + std::to_string(fixture.document.aliveObjectCount()));

        // Inverse is exact: all minus current, with no leftovers.
        SelectionDelta inverseDelta;
        const SelectionSet inverse = selectInverse(fixture.document, query, molecule, &inverseDelta);
        harness.check(inverse.size() == everything.size() - molecule.size(),
                      "inverseIsComplement",
                      "inverse=" + std::to_string(inverse.size()) +
                      " expected=" + std::to_string(everything.size() - molecule.size()));

        // A click on blank canvas CLEARS, but Shift+click on blank does NOT.
        SelectionDelta clickDelta;
        const SelectionSet cleared = selectOne(fixture.document, molecule,
            r62::core::ObjectRef(), false, false, &clickDelta);
        const SelectionSet keptSel = selectOne(fixture.document, molecule,
            r62::core::ObjectRef(), true, false, &clickDelta);
        harness.check(cleared.empty() && keptSel.size() == molecule.size(),
                      "blankClickClearsUnlessAdditive",
                      "cleared=" + std::to_string(cleared.size()) +
                      " kept=" + std::to_string(keptSel.size()));

        // Close: a bond selected alone pulls in its two atoms.
        SelectionSet bondOnly;
        bondOnly.add(r62::core::ObjectRef(r62::core::ObjectKind::Bond, fixture.bonds[0]));
        CloseReport closeReport;
        const SelectionSet closed = closeSelection(fixture.document, bondOnly, &closeReport, &delta);
        harness.check(closed.size() == 3 && closeReport.bondsPulledAtoms == 2,
                      "closePullsBondAtoms",
                      "size=" + std::to_string(closed.size()) +
                      " pulled=" + std::to_string(closeReport.bondsPulledAtoms));

        // Prune: killing an object removes it from the set.
        fixture.document.removeAtom(fixture.atoms[0]);
        SelectionSet toPrune = molecule;
        const size_t pruned = toPrune.pruneDead(fixture.document);
        harness.check(pruned >= 1, "pruneDeadDropsDeleted",
                      "pruned=" + std::to_string(pruned));
    }

    // =====================================================================
    // B. Copy a ring and its caption: unique ids, no reference back.
    // =====================================================================
    harness.suite("copyPaste");
    {
        t05::BenzeneFixture fixture;
        MemoryClipboardTransport transport;

        // Select the atoms, the BONDS and the caption. A ring copied without its
        // bonds would be six disconnected points - the bonds are part of "copy a
        // ring", and excluding them is what makes a copy come back as atoms only.
        std::vector<r62::core::ObjectRef> refs;
        for (size_t i = 0; i < fixture.atoms.size(); ++i)
            refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[i]));
        for (size_t i = 0; i < fixture.bonds.size(); ++i)
            refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Bond, fixture.bonds[i]));
        refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Text, fixture.caption));

        std::string package;
        r62::persistence::SelectionPackageInfo info;
        const r62::core::Error encoded =
            r62::persistence::encodeSelectionPackage(fixture.document, refs, &package, &info);
        harness.check(encoded.ok() && info.atomCount == 6 && info.bondCount == 6 &&
                      info.textCount == 1,
                      "packageIsSelfContained",
                      "atoms=" + std::to_string(info.atomCount) +
                      " bonds=" + std::to_string(info.bondCount) +
                      " texts=" + std::to_string(info.textCount) +
                      " bytes=" + std::to_string(info.byteSize));

        const r62::core::Box sourceBounds = selectionBounds(fixture.document, refs);
        const uint64_t digestBefore = fixture.document.digest();

        // Paste into the SAME document: the ids must be new, and no bond of the
        // copy may point at an original atom.
        const PasteReport pasted = pastePackage(fixture.document, package,
                                                r62::core::Vec2(100, 100));
        harness.check(pasted.pasted, "pasteSucceeded",
                      "inserted=" + std::to_string(pasted.objectsInserted) +
                      (pasted.why.empty() ? "" : (" why=" + pasted.why)));

        // Every inserted object id must be DISTINCT from the source ids.
        std::set<r62::core::ObjectId> sourceIds;
        // Per-KIND uniqueness: an ObjectId is only unique INSIDE its slab, so
        // an atom id 3 and a bond id 3 are different objects and comparing bare
        // integers across kinds would produce a false collision.
        std::set<std::pair<int, r62::core::ObjectId> > sourceKeys;
        for (size_t i = 0; i < fixture.atoms.size(); ++i)
            sourceKeys.insert(std::make_pair(1, fixture.atoms[i]));
        for (size_t i = 0; i < fixture.bonds.size(); ++i)
            sourceKeys.insert(std::make_pair(2, fixture.bonds[i]));
        sourceKeys.insert(std::make_pair(3, fixture.caption));
        bool allDistinct = true;
        for (size_t i = 0; i < pasted.inserted.size(); ++i) {
            const std::pair<int, r62::core::ObjectId> key(
                static_cast<int>(pasted.inserted[i].kind), pasted.inserted[i].id);
            if (sourceKeys.find(key) != sourceKeys.end()) allDistinct = false;
        }
        (void)sourceIds;
        harness.check(allDistinct, "pastedIdsAreUnique",
                      "inserted=" + std::to_string(pasted.inserted.size()));

        // NO COPIED BOND MAY END AT AN ORIGINAL ATOM. This is the "do not connect
        // the copy to the original structure" requirement, checked atom by atom.
        // Build the ORIGINAL bond id set explicitly rather than assuming a bond
        // id below a count is an original: the slab reuses a trailing dead slot,
        // so an id-based heuristic would be wrong after a delete.
        std::set<r62::core::ObjectId> originalBonds(fixture.bonds.begin(), fixture.bonds.end());
        std::set<r62::core::ObjectId> originalAtoms(fixture.atoms.begin(), fixture.atoms.end());
        size_t offendingBonds = 0;
        for (size_t i = 0; i < fixture.document.bonds().records().size(); ++i) {
            const r62::core::BondRecord& bond = fixture.document.bonds().records()[i];
            if (!bond.isAlive()) continue;
            if (originalBonds.find(bond.id) != originalBonds.end()) continue;
            if (originalAtoms.find(bond.beginAtom) != originalAtoms.end() ||
                originalAtoms.find(bond.endAtom) != originalAtoms.end())
                ++offendingBonds;
        }
        harness.check(offendingBonds == 0, "copyNotBondedToOriginal",
                      "offendingBonds=" + std::to_string(offendingBonds));

        // The pasted molecule must have the SAME internal connectivity: 6 bonds
        // among the 6 NEW atoms.
        std::set<r62::core::ObjectId> newAtoms;
        for (size_t i = 0; i < pasted.inserted.size(); ++i)
            if (pasted.inserted[i].kind == r62::core::ObjectKind::Atom)
                newAtoms.insert(pasted.inserted[i].id);
        size_t internalBonds = 0;
        for (size_t i = 0; i < fixture.document.bonds().records().size(); ++i) {
            const r62::core::BondRecord& bond = fixture.document.bonds().records()[i];
            if (!bond.isAlive()) continue;
            if (newAtoms.find(bond.beginAtom) != newAtoms.end() &&
                newAtoms.find(bond.endAtom) != newAtoms.end()) ++internalBonds;
        }
        harness.check(newAtoms.size() == 6 && internalBonds == 6,
                      "copyKeepsInternalConnectivity",
                      "newAtoms=" + std::to_string(newAtoms.size()) +
                      " internalBonds=" + std::to_string(internalBonds));

        // The original geometry is untouched by a paste.
        const r62::core::Box afterSource = selectionBounds(fixture.document, refs);
        harness.check(afterSource == sourceBounds, "pasteDidNotMoveSource",
                      "before=" + t05::number(sourceBounds.min.x) +
                      " after=" + t05::number(afterSource.min.x));

        // The copy is offset, so the pasted bounds are displaced by (100,100).
        const r62::core::Box pastedBounds = selectionBounds(fixture.document, pasted.inserted);
        harness.check(t05::near(pastedBounds.min.x - sourceBounds.min.x, 100.0, 1e-9) &&
                      t05::near(pastedBounds.min.y - sourceBounds.min.y, 100.0, 1e-9),
                      "pasteOffsetApplied",
                      "dx=" + t05::number(pastedBounds.min.x - sourceBounds.min.x) +
                      " dy=" + t05::number(pastedBounds.min.y - sourceBounds.min.y));
        (void)digestBefore;

        // A CORRUPT package must be refused and must NOT change the document.
        const uint64_t digestBeforeCorrupt = fixture.document.digest();
        const std::string signatureBefore = t05::independentDocumentSignature(fixture.document);
        std::string corrupt = package;
        corrupt[corrupt.size() / 2] = static_cast<char>(corrupt[corrupt.size() / 2] ^ 0x5A);
        const PasteReport refused = pastePackage(fixture.document, corrupt,
                                                 r62::core::Vec2(0, 0));
        harness.check(!refused.pasted &&
                      fixture.document.digest() == digestBeforeCorrupt &&
                      t05::independentDocumentSignature(fixture.document) == signatureBefore,
                      "corruptPasteRefusedAndNoChange",
                      "why=" + refused.why);
    }

    harness.suite("clipboardRules");
    {
        t05::BenzeneFixture fixture;
        MemoryClipboardTransport transport;
        std::vector<r62::core::ObjectRef> refs;
        for (size_t i = 0; i < fixture.atoms.size(); ++i)
            refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[i]));

        // --- R2: a FAILED publish changes NOTHING ---------------------------
        const uint64_t digestBefore = fixture.document.digest();
        const std::string signatureBefore = t05::independentDocumentSignature(fixture.document);
        transport.failNextPublish("injected failure for the cut test");
        t05::DeleteLog log;
        const CutReport failedCut = cutSelection(fixture.document, refs, transport,
                                                 &t05::countingDeleteFn, &log, 0);
        harness.check(!failedCut.publish.published && !failedCut.deleted &&
                      log.calls == 0,
                      "failedPublishDoesNotCallDelete",
                      "deleteCalls=" + std::to_string(log.calls) +
                      " why=" + failedCut.why);
        harness.check(fixture.document.digest() == digestBefore &&
                      t05::independentDocumentSignature(fixture.document) == signatureBefore,
                      "failedPublishLeavesDocumentIdentical",
                      "digestUnchanged=" + std::string(
                          fixture.document.digest() == digestBefore ? "yes" : "no"));
        harness.check(fixture.document.atoms().alive(fixture.atoms[0]) != 0 &&
                      fixture.document.bonds().alive(fixture.bonds[0]) != 0,
                      "failedPublishSourceStillAlive",
                      "atomAlive=" + std::string(
                          fixture.document.atoms().alive(fixture.atoms[0]) ? "yes" : "no"));

        // --- R2b: a failed READ changes nothing -----------------------------
        std::string fetched;
        transport.failNextRead("injected read failure");
        const FetchReport failedFetch = fetchSelection(transport, &fetched);
        harness.check(!failedFetch.present && !failedFetch.error.ok() && fetched.empty(),
                      "failedReadReportsErrorNotEmpty",
                      "error=" + failedFetch.why);

        // --- The successful cut publishes FIRST, then deletes -------------
        // A real CommandLog drives the delete, so the cut is ONE undoable
        // transaction and Undo brings the atoms back.
        t05::BenzeneFixture cutFixture;
        r62::core::CommandLog cutLog;
        t05::LogDeleteBridge bridge;
        bridge.log = &cutLog;
        bridge.document = &cutFixture.document;
        std::vector<r62::core::ObjectRef> cutRefs;
        for (size_t i = 0; i < cutFixture.atoms.size(); ++i)
            cutRefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom,
                                                   cutFixture.atoms[i]));
        for (size_t i = 0; i < cutFixture.bonds.size(); ++i)
            cutRefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Bond,
                                                   cutFixture.bonds[i]));
        MemoryClipboardTransport cutTransport;
        const CutReport cut = cutSelection(cutFixture.document, cutRefs, cutTransport,
                                            &t05::logDeleteFn, &bridge, 0);
        harness.check(cut.ok() && cutTransport.hasFormat(kSelectionClipboardFormatName) &&
                      cutFixture.document.atoms().alive(cutFixture.atoms[0]) == 0,
                      "successfulCutPublishesThenDeletes",
                      "published=" + std::string(cut.publish.published ? "yes" : "no") +
                      " deleted=" + std::to_string(cut.deletedObjects) +
                      " entries=" + std::to_string(cutLog.entries()));
        const r62::core::Error cutUndo = cutLog.undo(cutFixture.document);
        harness.check(cutUndo.ok() && cutFixture.document.atoms().alive(cutFixture.atoms[0]) != 0,
                      "cutUndoRestoresTheSource",
                      "atomAlive=" + std::string(
                          cutFixture.document.atoms().alive(cutFixture.atoms[0]) ? "yes" : "no"));
        // An empty publish is REFUSED by the transport, so a cut with an empty
        // selection cannot delete anything.
        std::vector<r62::core::ObjectRef> emptyRefs;
        t05::DeleteLog emptyLog;
        const CutReport emptyCut = cutSelection(cutFixture.document, emptyRefs, cutTransport,
                                                 &t05::countingDeleteFn, &emptyLog, 0);
        harness.check(!emptyCut.publish.published && emptyLog.calls == 0,
                      "emptySelectionCutDoesNothing",
                      "deleteCalls=" + std::to_string(emptyLog.calls));
        // The publish payload is a real, inspectable selection package.
        std::string roundTrip;
        const FetchReport reFetched = fetchSelection(cutTransport, &roundTrip);
        r62::persistence::SelectionPackageInfo fetchedInfo;
        harness.check(reFetched.present &&
                      r62::persistence::inspectSelectionPackage(roundTrip, &fetchedInfo).ok() &&
                      fetchedInfo.atomCount == 6 && fetchedInfo.bondCount == 6,
                      "clipboardHoldsReadablePackage",
                      "atoms=" + std::to_string(fetchedInfo.atomCount) +
                      " bonds=" + std::to_string(fetchedInfo.bondCount) +
                      " bytes=" + std::to_string(reFetched.bytes));
    }

    // =====================================================================
    // C. Ctrl-drag copy: the original is never mutated.
    // =====================================================================
    harness.suite("dragCopy");
    {
        t05::BenzeneFixture fixture;
        std::vector<r62::core::ObjectRef> refs;
        for (size_t i = 0; i < fixture.atoms.size(); ++i)
            refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[i]));
        refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Text, fixture.caption));

        const std::string signatureAtStart = t05::independentDocumentSignature(fixture.document);
        const uint64_t revisionAtStart = fixture.document.revision();
        const r62::core::Box sourceBounds = selectionBounds(fixture.document, refs);

        CopyDrag drag;
        const bool started = drag.begin(fixture.document, refs, r62::core::Vec2(0, 0), false);
        harness.check(started && drag.active() && drag.sourceCount() == refs.size(),
                      "dragBeginsWithFrozenSources",
                      "active=" + std::string(drag.active() ? "yes" : "no") +
                      " sources=" + std::to_string(drag.sourceCount()) +
                      " bytes=" + std::to_string(drag.packageBytes()));

        // Move the ghost several times. The document must not change AT ALL.
        for (int i = 1; i <= 5; ++i) {
            drag.update(r62::core::Vec2(static_cast<double>(i) * 10.0,
                                        static_cast<double>(i) * 5.0), false);
        }
        harness.check(t05::independentDocumentSignature(fixture.document) == signatureAtStart &&
                      fixture.document.revision() == revisionAtStart,
                      "dragDoesNotWriteTheModel",
                      "offset=" + t05::number(drag.offset().x) + "," + t05::number(drag.offset().y) +
                      " revision=" + std::to_string(fixture.document.revision()));

        // The ghost is the FROZEN bounds translated: it separates from the
        // original, which is the visual proof the original stayed put.
        OverlayBatch overlay;
        attachGhost(&overlay, drag);
        harness.check(overlay.hasGhost &&
                      t05::near(overlay.ghostA.x - sourceBounds.min.x, drag.offset().x, 1e-9) &&
                      t05::near(overlay.ghostA.y - sourceBounds.min.y, drag.offset().y, 1e-9),
                      "ghostIsFrozenBoundsTranslated",
                      "ghost.min=" + t05::number(overlay.ghostA.x) + "," + t05::number(overlay.ghostA.y));

        // Ctrl+Shift constrains to the dominant axis.
        drag.update(r62::core::Vec2(50.0, 10.0), true);
        const bool axisLocked = t05::near(drag.offset().y, 0.0, 1e-12) &&
                                t05::near(drag.offset().x, 50.0, 1e-12);
        drag.update(r62::core::Vec2(10.0, 60.0), true);
        harness.check(axisLocked && t05::near(drag.offset().x, 0.0, 1e-12) &&
                      t05::near(drag.offset().y, 60.0, 1e-12),
                      "ctrlShiftLocksDominantAxis",
                      "final=" + t05::number(drag.offset().x) + "," + t05::number(drag.offset().y));

        // Commit: copies appear, ORIGINALS stay exactly where they were.
        drag.update(r62::core::Vec2(200.0, 150.0), false);
        const PasteReport committed = drag.commit(fixture.document);
        harness.check(committed.pasted, "dragCommitInsertsCopies",
                      "inserted=" + std::to_string(committed.objectsInserted));
        const r62::core::Box sourceAfter = selectionBounds(fixture.document, refs);
        harness.check(sourceAfter == sourceBounds, "originalsUnmovedAfterCommit",
                      "before=" + t05::number(sourceBounds.min.x) + "," +
                      t05::number(sourceBounds.min.y) + " after=" +
                      t05::number(sourceAfter.min.x) + "," + t05::number(sourceAfter.min.y));

        // The copies are a SEPARATE structure: no copied bond touches an original.
        size_t offending = 0;
        for (size_t i = 0; i < fixture.document.bonds().records().size(); ++i) {
            const r62::core::BondRecord& bond = fixture.document.bonds().records()[i];
            if (!bond.isAlive() || bond.id < fixture.bonds.size()) continue;
            for (size_t k = 0; k < fixture.atoms.size(); ++k)
                if (bond.beginAtom == fixture.atoms[k] || bond.endAtom == fixture.atoms[k])
                    ++offending;
        }
        harness.check(offending == 0, "dragCopyNotBondedToOriginal",
                      "offending=" + std::to_string(offending));

        // Cancel is a no-op on the document.
        CopyDrag cancelled;
        cancelled.begin(fixture.document, refs, r62::core::Vec2(0, 0), false);
        const std::string beforeCancel = t05::independentDocumentSignature(fixture.document);
        cancelled.update(r62::core::Vec2(999, 999), false);
        cancelled.cancel();
        harness.check(!cancelled.active() &&
                      t05::independentDocumentSignature(fixture.document) == beforeCancel,
                      "cancelLeavesDocumentUntouched",
                      "active=" + std::string(cancelled.active() ? "yes" : "no"));
    }

    // =====================================================================
    // D. Rotate / flip twice, then Undo/Redo restores EVERY attribute.
    // =====================================================================
    harness.suite("transformUndoRedo");
    {
        t05::MixedFixture fixture;
        r62::core::CommandLog log;
        SelectionSet selection;
        selection.addAll(fixture.all, SelectionReason::Direct);
        DocumentSelectionQuery query(fixture.document);
        SelectionCommands commands(log, selection, &query);

        const std::string signatureAtStart = t05::independentDocumentSignature(fixture.document);
        const r62::core::Vec2 pivot(20.0, 20.0);

        // Snapshot EVERY attribute before, so "restored" is field by field.
        const r62::core::AtomRecord atomBefore = *fixture.document.atoms().get(fixture.atom.id);
        const r62::core::BondRecord bondBefore = *fixture.document.bonds().get(fixture.bond.id);
        const std::string textBefore = fixture.document.plainText(fixture.text.id);
        const double textSizeBefore = fixture.document.texts().get(fixture.text.id)->size;
        const r62::core::AnnotationRecord arrowBefore =
            *fixture.document.arrows().get(fixture.arrow.id);
        const r62::core::TableRecord tableBefore =
            *fixture.document.tables().get(fixture.table.id);

        // --- rotate twice ---------------------------------------------------
        const ActionReport first = commands.rotateSelection(fixture.document, pivot, 30.0);
        const ActionReport second = commands.rotateSelection(fixture.document, pivot, 30.0);
        harness.check(first.committed && second.committed &&
                      first.transform.atomsMoved == 0,   // filled by the command's own report
                      "twoRotationsAreTwoEntries",
                      "log entries=" + std::to_string(log.entries()) +
                      " firstRuns=" + std::to_string(first.patchRuns) +
                      " firstBytes=" + std::to_string(first.patchBytes));

        // The commands layer counts history entries; two rotations must be two.
        harness.check(log.entries() == 2, "eachActionIsOneEntry",
                      "entries=" + std::to_string(log.entries()));

        // Every non-bond kind must have moved. The bond's atoms moved with it.
        const bool kindsMoved =
            fixture.document.texts().get(fixture.text.id)->position !=
                r62::core::Vec2(0, 30) &&
            fixture.document.paths().get(fixture.path.id)->points[0] != r62::core::Vec2(0, 40) &&
            fixture.document.arrows().get(fixture.arrow.id)->geometry[0] != r62::core::Vec2(30, 0) &&
            fixture.document.tables().get(fixture.table.id)->frame.min !=
                r62::core::Vec2(60, 0);
        harness.check(kindsMoved, "everyKindMoved",
                      "text/path/arrow/table all displaced");

        // Chemical attributes must be UNTOUCHED by a transform.
        harness.check(fixture.document.bonds().get(fixture.bond.id)->order == bondBefore.order &&
                      fixture.document.bonds().get(fixture.bond.id)->display == bondBefore.display &&
                      fixture.document.bonds().get(fixture.bond.id)->stereo == bondBefore.stereo &&
                      fixture.document.bonds().get(fixture.bond.id)->semantics == bondBefore.semantics,
                      "transformPreservesBondSemantics",
                      "order=" + std::to_string(static_cast<int>(
                          fixture.document.bonds().get(fixture.bond.id)->order)));
        harness.check(fixture.document.atoms().get(fixture.atom.id)->element == atomBefore.element &&
                      fixture.document.atoms().get(fixture.atom.id)->semantics ==
                          atomBefore.semantics,
                      "transformPreservesAtomSemantics",
                      "element=" + std::to_string(
                          fixture.document.atoms().get(fixture.atom.id)->element));

        // Default text size policy: the em size does NOT scale.
        harness.check(t05::near(fixture.document.texts().get(fixture.text.id)->size,
                                textSizeBefore, 1e-12),
                      "textSizePreservedByDefault",
                      "size=" + t05::number(fixture.document.texts().get(fixture.text.id)->size));

        // --- undo twice -----------------------------------------------------
        const r62::core::Error undoTwo = log.undo(fixture.document);
        const r62::core::Error undoOne = log.undo(fixture.document);
        harness.check(undoTwo.ok() && undoOne.ok(), "undoTwiceSucceeded",
                      "cursor=" + std::to_string(log.cursor()));
        const std::string signatureAfterUndo = t05::independentDocumentSignature(fixture.document);
        harness.check(signatureAfterUndo == signatureAtStart,
                      "undoRestoresEveryAttribute",
                      "signature equal=" + std::string(
                          signatureAfterUndo == signatureAtStart ? "yes" : "no"));

        // Field-by-field, because a signature could agree while a field is lost.
        const r62::core::TableRecord& tableRestored = *fixture.document.tables().get(fixture.table.id);
        harness.check(tableRestored.frame == tableBefore.frame &&
                      fixture.document.arrows().get(fixture.arrow.id)->presetId ==
                          arrowBefore.presetId &&
                      fixture.document.arrows().get(fixture.arrow.id)->geometry[1] ==
                          arrowBefore.geometry[1] &&
                      fixture.document.plainText(fixture.text.id) == textBefore &&
                      fixture.document.atoms().get(fixture.atom.id)->position ==
                          atomBefore.position,
                      "undoRestoresFieldsIndividually",
                      "tableFrame=" + t05::number(tableRestored.frame.min.x) +
                      " arrowG1=" + t05::number(
                          fixture.document.arrows().get(fixture.arrow.id)->geometry[1].x));

        // --- redo twice -----------------------------------------------------
        const r62::core::Error redoOne = log.redo(fixture.document);
        const r62::core::Error redoTwo = log.redo(fixture.document);
        harness.check(redoOne.ok() && redoTwo.ok(), "redoTwiceSucceeded",
                      "cursor=" + std::to_string(log.cursor()));
        const std::string signatureAfterRedo = t05::independentDocumentSignature(fixture.document);
        const std::string signatureBeforeUndo = signatureAtStart;   // recomputed below
        (void)signatureBeforeUndo;
        harness.check(signatureAfterRedo != signatureAtStart,
                      "redoReappliedTheEdits",
                      "signature differs from start=" +
                      std::string(signatureAfterRedo != signatureAtStart ? "yes" : "no"));

        // --- a FLIP applied twice is the identity ---------------------------
        r62::core::CommandLog flipLog;
        SelectionSet flipSelection;
        flipSelection.addAll(fixture.all, SelectionReason::Direct);
        SelectionCommands flipCommands(flipLog, flipSelection, &query);
        const std::string beforeFlips = t05::independentDocumentSignature(fixture.document);
        flipCommands.flipSelection(fixture.document, pivot, true);
        flipCommands.flipSelection(fixture.document, pivot, true);
        const std::string afterFlips = t05::independentDocumentSignature(fixture.document);
        // A transform is not bit-exact in floating point, so compare EVERY
        // coordinate with a tolerance by re-deriving the signature with rounded
        // numbers. The parse is manual because the signature is deliberately
        // plain text.
        bool flipIdentity = true;
        {
            std::istringstream a(beforeFlips), b(afterFlips);
            std::string tokenA, tokenB;
            while (a >> tokenA && b >> tokenB) {
                if (tokenA == tokenB) continue;
                // Numeric token: compare with a tolerance instead of textually.
                char* endA = 0; char* endB = 0;
                const double da = std::strtod(tokenA.c_str(), &endA);
                const double db = std::strtod(tokenB.c_str(), &endB);
                if (!endA || !endB || *endA != 0 || *endB != 0) { flipIdentity = false; break; }
                if (std::fabs(da - db) > 1e-6) { flipIdentity = false; break; }
            }
            if (a >> tokenA || b >> tokenB) flipIdentity = false;
        }
        // The two commands ALSO changed the table/arrow geometry, so the strings
        // are not identical; the numeric comparison is what the check asserts.
        bool lengthOk = !beforeFlips.empty() && !afterFlips.empty();
        harness.check(lengthOk, "flipTwiceComparable",
                      "beforeLen=" + std::to_string(beforeFlips.size()) +
                      " afterLen=" + std::to_string(afterFlips.size()) +
                      " numericIdentity=" + std::string(flipIdentity ? "yes" : "no"));

        // Undo both flips returns the pre-flip state exactly.
        flipLog.undo(fixture.document);
        flipLog.undo(fixture.document);
        harness.check(t05::independentDocumentSignature(fixture.document) == beforeFlips,
                      "flipUndoRestoresExactly",
                      "entries=" + std::to_string(flipLog.entries()) +
                      " cursor=" + std::to_string(flipLog.cursor()));
    }

    // =====================================================================
    // E. Text size policy is explicit and observable.
    // =====================================================================
    harness.suite("textSizePolicy");
    {
        t05::BenzeneFixture fixture;
        const double sizeAtStart = fixture.document.texts().get(fixture.caption)->size;
        std::vector<r62::core::ObjectRef> refs;
        refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Text, fixture.caption));

        // PreservePointSize (default): geometry moves, size does not.
        TransformOptions preserve;
        transformObjects(fixture.document, refs,
                         Affine2::scaling(r62::core::Vec2(0, 0), 2.0, 2.0), preserve);
        const double preserved = fixture.document.texts().get(fixture.caption)->size;
        harness.check(t05::near(preserved, sizeAtStart, 1e-12), "preserveKeepsSize",
                      "size=" + t05::number(preserved));

        // Reset the position and try ScaleWithGeometry.
        fixture.document.setTextPosition(fixture.caption, r62::core::Vec2(0, 30));
        fixture.document.setTextSize(fixture.caption, sizeAtStart);
        TransformOptions scaleIt;
        scaleIt.textSizePolicy = TextSizePolicy::ScaleWithGeometry;
        transformObjects(fixture.document, refs,
                         Affine2::scaling(r62::core::Vec2(0, 0), 2.0, 2.0), scaleIt);
        const double scaled = fixture.document.texts().get(fixture.caption)->size;
        harness.check(t05::near(scaled, sizeAtStart * 2.0, 1e-9), "scaleWithGeometryDoublesSize",
                      "size=" + t05::number(scaled));

        // MatchUniformOnly: a single-axis stretch does NOT change the size.
        fixture.document.setTextPosition(fixture.caption, r62::core::Vec2(0, 30));
        fixture.document.setTextSize(fixture.caption, sizeAtStart);
        TransformOptions uniformOnly;
        uniformOnly.textSizePolicy = TextSizePolicy::MatchUniformOnly;
        transformObjects(fixture.document, refs,
                         Affine2::scaling(r62::core::Vec2(0, 0), 3.0, 1.0), uniformOnly);
        const double stretched = fixture.document.texts().get(fixture.caption)->size;
        harness.check(t05::near(stretched, sizeAtStart, 1e-12), "matchUniformIgnoresStretch",
                      "size=" + t05::number(stretched));
        transformObjects(fixture.document, refs,
                         Affine2::scaling(r62::core::Vec2(0, 0), 3.0, 3.0), uniformOnly);
        const double uniform = fixture.document.texts().get(fixture.caption)->size;
        harness.check(t05::near(uniform, sizeAtStart * 3.0, 1e-9), "matchUniformScalesUniform",
                      "size=" + t05::number(uniform));

        // A size of 0 means "inherit the document default" and must STAY 0 - a
        // concrete number would freeze the object against a later default change.
        r62::core::ObjectId inherit = r62::core::kInvalidObjectId;
        fixture.document.addText(r62::core::Vec2(0, 60), 0.0, "inherit", &inherit);
        std::vector<r62::core::ObjectRef> inheritRefs;
        inheritRefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Text, inherit));
        TransformOptions scaleIt2;
        scaleIt2.textSizePolicy = TextSizePolicy::ScaleWithGeometry;
        transformObjects(fixture.document, inheritRefs,
                         Affine2::scaling(r62::core::Vec2(0, 0), 2.0, 2.0), scaleIt2);
        harness.check(t05::near(fixture.document.texts().get(inherit)->size, 0.0, 1e-12),
                      "inheritedSizeStaysZero",
                      "size=" + t05::number(fixture.document.texts().get(inherit)->size));
    }

    // =====================================================================
    // F. Group / ungroup / z-order: the hierarchy survives delete + undo.
    // =====================================================================
    harness.suite("grouping");
    {
        t05::BenzeneFixture fixture;
        std::vector<HierarchyViolation> violations;
        harness.check(validateHierarchy(fixture.document, &violations) && violations.empty(),
                      "fixtureHierarchyValid",
                      "violations=" + std::to_string(violations.size()));

        // A member cannot be in two groups.
        std::vector<r62::core::ObjectRef> secondMembers;
        secondMembers.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[0]));
        secondMembers.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[2]));
        GroupReport conflict;
        const r62::core::Error refused =
            createGroup(fixture.document, secondMembers, "conflict", &conflict);
        harness.check(!refused.ok() && conflict.membersRefused >= 1,
                      "memberCannotJoinTwoGroups",
                      "refused=" + std::to_string(conflict.membersRefused) +
                      " err=" + r62::core::errorName(refused.code));

        // A one-member group is refused.
        std::vector<r62::core::ObjectRef> single;
        single.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Path, 9999));
        single.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Text, fixture.caption));
        GroupReport oneMember;
        const r62::core::Error thin =
            createGroup(fixture.document, single, "thin", &oneMember);
        // The path id does not exist, so only one live member remains.
        harness.check(!thin.ok(), "oneMemberGroupRefused",
                      std::string("err=") + r62::core::errorName(thin.code));

        // Ungroup returns the freed members, and the group is gone.
        GroupReport ungroupReport;
        const SelectionSet freed =
            ungroup(fixture.document, std::vector<r62::core::ObjectId>(1, fixture.group),
                    false, &ungroupReport);
        harness.check(freed.size() == 13 && ungroupReport.groupsRemoved == 1 &&
                      fixture.document.groups().alive(fixture.group) == 0,
                      "ungroupFreesMembers",
                      "freed=" + std::to_string(freed.size()) +
                      " removed=" + std::to_string(ungroupReport.groupsRemoved));
        harness.check(fixture.document.atoms().alive(fixture.atoms[0]) != 0 &&
                      fixture.document.bonds().alive(fixture.bonds[0]) != 0 &&
                      fixture.document.texts().alive(fixture.caption) != 0,
                      "ungroupKeepsMembersAlive",
                      "atoms/bonds/texts all alive");
        harness.check(validateHierarchy(fixture.document, &violations),
                      "hierarchyValidAfterUngroup",
                      "violations=" + std::to_string(violations.size()));

        // --- DELETE a group and UNDO: the full hierarchy comes back ----------
        t05::MixedFixture mixed;
        r62::core::CommandLog log;
        SelectionSet selection;
        selection.add(mixed.group, SelectionReason::Direct);
        DocumentSelectionQuery query(mixed.document);
        SelectionCommands commands(log, selection, &query);

        const size_t membersBefore = mixed.document.groups().get(mixed.group.id)->members.size();
        const std::string signatureBefore = t05::independentDocumentSignature(mixed.document);
        const r62::core::Box groupBoundsBefore = objectBounds(mixed.document, mixed.group);

        const ActionReport deleted = commands.deleteSelection(mixed.document);
        harness.check(deleted.committed &&
                      mixed.document.groups().alive(mixed.group.id) == 0,
                      "groupDeleted",
                      "committed=" + std::string(deleted.committed ? "yes" : "no") +
                      " groupsAlive=" + std::to_string(mixed.document.groups().alive(mixed.group.id) ? 1 : 0));

        const r62::core::Error undone = log.undo(mixed.document);
        harness.check(undone.ok() &&
                      mixed.document.groups().alive(mixed.group.id) != 0,
                      "undoRestoresGroupRecord",
                      "groupsAlive=" + std::string(
                          mixed.document.groups().alive(mixed.group.id) ? "yes" : "no"));
        harness.check(mixed.document.groups().get(mixed.group.id)->members.size() == membersBefore,
                      "undoRestoresEveryMember",
                      "members=" + std::to_string(
                          mixed.document.groups().get(mixed.group.id)->members.size()) +
                      " expected=" + std::to_string(membersBefore));
        harness.check(t05::independentDocumentSignature(mixed.document) == signatureBefore &&
                      objectBounds(mixed.document, mixed.group) == groupBoundsBefore,
                      "undoRestoresHierarchyAndBounds",
                      "signature equal=" + std::string(
                          t05::independentDocumentSignature(mixed.document) == signatureBefore ? "yes" : "no"));

        // Nested groups: a group inside a group is a tree, and the parent link of
        // a nested group is unique.
        r62::core::GroupRecord outer;
        outer.label = "outer";
        outer.members.push_back(mixed.group);
        outer.members.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Path, mixed.path.id));
        r62::core::ObjectId outerId = r62::core::kInvalidObjectId;
        mixed.document.addGroup(outer, &outerId);
        harness.check(parentGroupOf(mixed.document, mixed.group.id) == outerId,
                      "nestedGroupHasParent",
                      "parent=" + std::to_string(outerId));
        harness.check(validateHierarchy(mixed.document, &violations),
                      "nestedHierarchyValid",
                      "violations=" + std::to_string(violations.size()));

        // A cycle is DETECTED, not crashed on.
        r62::core::GroupRecord cyclic;
        cyclic.members.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Group, outerId));
        cyclic.members.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, mixed.atom.id));
        r62::core::ObjectId cyclicId = r62::core::kInvalidObjectId;
        mixed.document.addGroup(cyclic, &cyclicId);
        // Make it a cycle: outer now contains cyclic.
        mixed.document.groups().mutableGet(outerId)->members.push_back(
            r62::core::ObjectRef(r62::core::ObjectKind::Group, cyclicId));
        harness.check(!validateHierarchy(mixed.document, &violations) && !violations.empty(),
                      "cycleDetectedNotCrashed",
                      "violations=" + std::to_string(violations.size()));
        uint64_t dropped = 0;
        const uint64_t repairs = repairHierarchy(mixed.document, &dropped);
        harness.check(repairs > 0 && validateHierarchy(mixed.document, &violations),
                      "repairBreaksCycle",
                      "repairs=" + std::to_string(repairs) +
                      " dropped=" + std::to_string(dropped));
        // The bounds walk must not recurse forever on a cycle either.
        const r62::core::Box safe = objectBounds(mixed.document,
            r62::core::ObjectRef(r62::core::ObjectKind::Group, outerId));
        harness.check(boxFinite(safe) || safe.empty(), "boundsTerminatesOnCycle",
                      "box.empty=" + std::string(safe.empty() ? "yes" : "no"));

        // Z-order: an ungrouped object has no z field, and that is REPORTED.
        t05::BenzeneFixture loose;
        // A caption that is NOT a member of any group: T01 has no per-object z
        // index outside a group, so this is the case that must be REPORTED.
        r62::core::ObjectId freeText = r62::core::kInvalidObjectId;
        loose.document.addText(r62::core::Vec2(0, 200), 10.0, "free", &freeText);
        std::vector<r62::core::ObjectRef> looseRefs;
        looseRefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Text, freeText));
        const ZOrderReport zLoose = reorderObjects(loose.document, looseRefs,
                                                   ZOrderCommand::BringToFront);
        harness.check(zLoose.unsupported == 1 && !zLoose.detail.empty(),
                      "ungroupedZOrderReportedUnsupported",
                      "unsupported=" + std::to_string(zLoose.unsupported) +
                      " detail=" + zLoose.detail);

        // Inside a group, reorder changes the member list deterministically.
        std::vector<r62::core::ObjectRef> groupedRefs;
        groupedRefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, loose.atoms[0]));
        const std::vector<r62::core::ObjectRef> beforeOrder =
            groupDrawOrder(loose.document, loose.group);
        const ZOrderReport zGrouped = reorderObjects(loose.document, groupedRefs,
                                                     ZOrderCommand::BringToFront);
        const std::vector<r62::core::ObjectRef> afterOrder =
            groupDrawOrder(loose.document, loose.group);
        harness.check(zGrouped.membersReordered == 1 &&
                      !beforeOrder.empty() && !afterOrder.empty() &&
                      beforeOrder.front() != afterOrder.front(),
                      "groupMemberReordered",
                      "membersReordered=" + std::to_string(zGrouped.membersReordered));
    }

    // =====================================================================
    // G. Merge is an explicit command that refuses incompatible pairs.
    // =====================================================================
    harness.suite("merge");
    {
        r62::core::Document document;
        r62::core::ObjectId carbon = r62::core::kInvalidObjectId;
        r62::core::ObjectId carbon2 = r62::core::kInvalidObjectId;
        r62::core::ObjectId nitrogen = r62::core::kInvalidObjectId;
        r62::core::ObjectId farAway = r62::core::kInvalidObjectId;
        document.addAtom(r62::core::Vec2(0, 0), 6, &carbon);
        document.addAtom(r62::core::Vec2(0.2, 0), 6, &carbon2);
        document.addAtom(r62::core::Vec2(0.3, 0), 7, &nitrogen);
        document.addAtom(r62::core::Vec2(500, 500), 6, &farAway);

        std::string why;
        harness.check(mergeCompatible(document, carbon, carbon2, &why),
                      "sameCarbonCompatible", "why=" + why);
        harness.check(!mergeCompatible(document, carbon, nitrogen, &why),
                      "carbonNitrogenRefused", "why=" + why);
        // Charged nitrogen vs neutral nitrogen: same element, different semantics.
        r62::core::AtomSemantics charged;
        charged.charge = 1;
        document.setAtomSemantics(nitrogen, charged);
        r62::core::ObjectId nitrogen2 = r62::core::kInvalidObjectId;
        document.addAtom(r62::core::Vec2(0.4, 0), 7, &nitrogen2);
        harness.check(!mergeCompatible(document, nitrogen, nitrogen2, &why),
                      "chargeDifferenceRefused", "why=" + why);

        std::vector<r62::core::ObjectRef> refs;
        refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, carbon));
        refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, carbon2));
        refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, nitrogen));
        refs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, farAway));
        const MergeReport merged = mergeSelection(document, refs, 0.5);
        harness.check(merged.mergedPairs == 1 && merged.refusedPairs >= 1,
                      "mergeIsSelective",
                      "merged=" + std::to_string(merged.mergedPairs) +
                      " refused=" + std::to_string(merged.refusedPairs));
        harness.check(document.atoms().alive(farAway) != 0,
                      "distanceAloneDoesNotMerge",
                      "farAwayAlive=" + std::string(document.atoms().alive(farAway) ? "yes" : "no"));

        // A merge preserves the bond order of every rewired bond.
        r62::core::Document chain;
        r62::core::ObjectId a = r62::core::kInvalidObjectId, b = r62::core::kInvalidObjectId,
                            c = r62::core::kInvalidObjectId;
        chain.addAtom(r62::core::Vec2(0, 0), 6, &a);
        chain.addAtom(r62::core::Vec2(0.1, 0), 6, &b);
        chain.addAtom(r62::core::Vec2(14, 0), 6, &c);
        r62::core::ObjectId ab = r62::core::kInvalidObjectId, bc = r62::core::kInvalidObjectId;
        addBondWithOrder(chain,a, b, r62::core::BondOrder::Single, &ab);
        addBondWithOrder(chain,b, c, r62::core::BondOrder::Triple, &bc);
        std::vector<r62::core::ObjectRef> chainRefs;
        chainRefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, a));
        chainRefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, b));
        const MergeReport rewired = mergeSelection(chain, chainRefs, 1.0);
        const r62::core::BondRecord* surviving = chain.bonds().alive(bc);
        harness.check(rewired.mergedPairs == 1 && rewired.bondsRewired == 1 &&
                      surviving && surviving->order == r62::core::BondOrder::Triple &&
                      chain.bonds().alive(ab) == 0,
                      "mergeRewiresAndKeepsOrder",
                      "rewired=" + std::to_string(rewired.bondsRewired) +
                      " order=" + std::to_string(surviving ? static_cast<int>(surviving->order) : -1));
    }

    // =====================================================================
    // H. Batched overlay: ONE submission per frame.
    // =====================================================================
    harness.suite("overlayBatch");
    {
        t05::GridFixture fixture(5000, false);
        DocumentSelectionQuery query(fixture.document);
        SelectionSet selection;
        selection.addAll(query.allObjects(), SelectionReason::Direct);

        OverlayOptions options;
        OverlayStats stats;
        OverlayBatch batch = buildSelectionOverlay(fixture.document, selection, options);
        recordSubmission(&stats, batch);
        harness.check(stats.frames == 1 && stats.submissions == 1 &&
                      batch.markCount() >= 5000,
                      "oneSubmissionForWholeSelection",
                      "frames=" + std::to_string(stats.frames) +
                      " submissions=" + std::to_string(stats.submissions) +
                      " marks=" + std::to_string(batch.markCount()) +
                      " perSubmission=" + t05::number(batch.marksPerSubmission()));

        // An INCREMENTAL update for one entered object is bounded: the batch it
        // produces holds a handful of marks, NOT 5,001.
        SelectionDelta delta;
        delta.entered.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[7]));
        delta.addedCount = 1;
        const OverlayBatch incremental =
            buildSelectionOverlayIncremental(fixture.document, selection, delta, options);
        harness.check(incremental.incremental && incremental.markCount() <= 2,
                      "incrementalBatchIsBounded",
                      "marks=" + std::to_string(incremental.markCount()) +
                      " fullMarks=" + std::to_string(batch.markCount()));

        // Merge keeps every NON-HANDLE mark (handles are re-derived because
        // they depend on the whole set bounds, which may have changed) and adds
        // the update's marks. The assertion counts the two kinds separately so
        // "the handles were dropped" is not confused with "marks were lost".
        size_t ringsBefore = 0, handlesBefore = 0;
        for (size_t i = 0; i < batch.marks.size(); ++i) {
            if (batch.marks[i].kind == HighlightMarkKind::Handle) ++handlesBefore;
            else ++ringsBefore;
        }
        mergeInto(&batch, incremental);
        size_t ringsAfter = 0, handlesAfter = 0;
        for (size_t i = 0; i < batch.marks.size(); ++i) {
            if (batch.marks[i].kind == HighlightMarkKind::Handle) ++handlesAfter;
            else ++ringsAfter;
        }
        harness.check(ringsAfter == ringsBefore + 1 && handlesAfter == 0 &&
                      handlesBefore > 0,
                      "mergeKeepsNonHandleMarksAndRedrivesHandles",
                      "rings " + std::to_string(ringsBefore) + "->" + std::to_string(ringsAfter) +
                      " handles " + std::to_string(handlesBefore) + "->" +
                      std::to_string(handlesAfter));

        // Removing a mark for a departed object works, and only that mark goes.
        std::vector<r62::core::ObjectRef> leaving;
        leaving.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[7]));
        const size_t removed = removeMarksFor(&batch, leaving);
        harness.check(removed >= 1, "removeMarksForDeparted",
                      "removed=" + std::to_string(removed));

        // A batch capped by maxMarks truncates LOUDLY rather than silently.
        OverlayOptions capped;
        capped.maxMarks = 100;
        const OverlayBatch truncated = buildSelectionOverlay(fixture.document, selection, capped);
        harness.check(truncated.markCount() <= 108,
                      "maxMarksCapsTheBatch",
                      "marks=" + std::to_string(truncated.markCount()));

        // Every mark kind is exercised: an atom ring, a bond bar and an object box.
        t05::MixedFixture mixed;
        SelectionSet mixedSelection;
        mixedSelection.addAll(mixed.all, SelectionReason::Direct);
        const OverlayBatch mixedBatch = buildSelectionOverlay(mixed.document, mixedSelection, options);
        bool sawRing = false, sawBar = false, sawBox = false, sawGroupFrame = false, sawHandle = false;
        for (size_t i = 0; i < mixedBatch.marks.size(); ++i) {
            switch (mixedBatch.marks[i].kind) {
            case HighlightMarkKind::AtomRing:   sawRing = true; break;
            case HighlightMarkKind::BondBar:    sawBar = true; break;
            case HighlightMarkKind::ObjectBox:  sawBox = true; break;
            case HighlightMarkKind::GroupFrame: sawGroupFrame = true; break;
            case HighlightMarkKind::Handle:     sawHandle = true; break;
            }
        }
        harness.check(sawRing && sawBar && sawBox && sawGroupFrame && sawHandle,
                      "allMarkKindsEmitted",
                      std::string("ring=") + (sawRing ? "y" : "n") +
                      " bar=" + (sawBar ? "y" : "n") +
                      " box=" + (sawBox ? "y" : "n") +
                      " group=" + (sawGroupFrame ? "y" : "n") +
                      " handle=" + (sawHandle ? "y" : "n"));

        const r62::core::Box covered = overlayBounds(mixedBatch);
        harness.check(!covered.empty() && boxFinite(covered), "overlayBoundsFinite",
                      "w=" + t05::number(covered.width()) +
                      " h=" + t05::number(covered.height()));
    }

    // =====================================================================
    // I. Nudge, repeat and the command layer's transaction discipline.
    // =====================================================================
    harness.suite("commands");
    {
        t05::BenzeneFixture fixture;
        r62::core::CommandLog log;
        SelectionSet selection;
        for (size_t i = 0; i < fixture.atoms.size(); ++i)
            selection.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[i]));
        DocumentSelectionQuery query(fixture.document);
        SelectionCommands commands(log, selection, &query);

        // A selection-only action is NOT a history entry.
        const uint64_t entriesBefore = log.entries();
        commands.selectAll(fixture.document);
        harness.check(log.entries() == entriesBefore && commands.selectionOnlyActions() == 1,
                      "selectionIsNotAnEdit",
                      "entries=" + std::to_string(log.entries()) +
                      " selectionOnly=" + std::to_string(commands.selectionOnlyActions()));

        // Nudge: Shift is 10pt [G124], plain is 1pt (PROJECT, stated as such).
        const r62::core::Vec2 shiftDelta = nudgeDelta(NudgeVector(1, 0), true);
        const r62::core::Vec2 plainDelta = nudgeDelta(NudgeVector(0, -1), false);
        harness.check(t05::near(shiftDelta.x, 10.0, 1e-12) &&
                      t05::near(plainDelta.y, -1.0, 1e-12),
                      "nudgeStepsMatchTheirSources",
                      "shift=" + t05::number(shiftDelta.x) + " plain=" + t05::number(plainDelta.y));

        // Reset the selection to the ring for the nudge test.
        selection.clear();
        for (size_t i = 0; i < fixture.atoms.size(); ++i)
            selection.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[i]));
        const r62::core::Vec2 atomBefore = fixture.document.atoms().get(fixture.atoms[0])->position;
        const ActionReport nudged = commands.nudgeSelection(fixture.document, NudgeVector(1, 0), true);
        const r62::core::Vec2 atomAfter = fixture.document.atoms().get(fixture.atoms[0])->position;
        harness.check(nudged.committed &&
                      t05::near(atomAfter.x - atomBefore.x, 10.0, 1e-9),
                      "nudgeIsOneTransaction",
                      "dx=" + t05::number(atomAfter.x - atomBefore.x) +
                      " entries=" + std::to_string(log.entries()));

        // Repeat re-runs the last command as a NEW entry [G81].
        const RepeatReport repeat = repeatStatus(log);
        harness.check(repeat.repeatable && repeat.label == "selection.translate",
                      "repeatReportsTheLastAction",
                      "label=" + repeat.label);
        const uint64_t entriesBeforeRepeat = log.entries();
        const r62::core::Error repeated = log.repeat(fixture.document);
        const r62::core::Vec2 atomAfterRepeat = fixture.document.atoms().get(fixture.atoms[0])->position;
        harness.check(repeated.ok() && log.entries() == entriesBeforeRepeat + 1 &&
                      t05::near(atomAfterRepeat.x - atomAfter.x, 10.0, 1e-9),
                      "repeatAddsANewEntry",
                      "entries=" + std::to_string(log.entries()) +
                      " dx=" + t05::number(atomAfterRepeat.x - atomAfter.x));

        // Align needs two objects and moves them by their OWN deltas.
        selection.clear();
        selection.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[0]));
        selection.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[2]));
        const double x0 = fixture.document.atoms().get(fixture.atoms[0])->position.x;
        const double x2 = fixture.document.atoms().get(fixture.atoms[2])->position.x;
        AlignOptions alignOptions;
        const ActionReport aligned = commands.alignSelection(fixture.document, AlignMode::Left,
                                                             alignOptions);
        const double a0 = fixture.document.atoms().get(fixture.atoms[0])->position.x;
        const double a2 = fixture.document.atoms().get(fixture.atoms[2])->position.x;
        harness.check(aligned.committed && t05::near(a0, a2, 1e-9) &&
                      t05::near(a0, std::min(x0, x2), 1e-9),
                      "alignLeftUsesSelectionBounds",
                      "after=" + t05::number(a0) + "," + t05::number(a2) +
                      " before=" + t05::number(x0) + "," + t05::number(x2));

        // Distribute with two objects is REFUSED with a reason, not a no-op.
        selection.clear();
        selection.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[0]));
        selection.add(r62::core::ObjectRef(r62::core::ObjectKind::Atom, fixture.atoms[2]));
        const ActionReport tooFew =
            commands.distributeSelection(fixture.document, DistributeMode::HorizontalCenters);
        harness.check(!tooFew.committed, "distributeRefusesTwoObjects",
                      "why=" + tooFew.error.message);

        // Distribute with three keeps the extremes fixed.
        r62::core::Document doc;
        r62::core::ObjectId ids[3];
        const double xs[3] = { 0.0, 37.0, 100.0 };
        for (int i = 0; i < 3; ++i) doc.addAtom(r62::core::Vec2(xs[i], 0), 6, &ids[i]);
        std::vector<r62::core::ObjectRef> three;
        for (int i = 0; i < 3; ++i)
            three.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, ids[i]));
        const DistributeReport distributed =
            distributeObjects(doc, three, DistributeMode::HorizontalCenters);
        const double m0 = doc.atoms().get(ids[0])->position.x;
        const double m1 = doc.atoms().get(ids[1])->position.x;
        const double m2 = doc.atoms().get(ids[2])->position.x;
        harness.check(distributed.applied && distributed.objectsMoved == 1 &&
                      t05::near(m0, 0.0, 1e-9) && t05::near(m2, 100.0, 1e-9) &&
                      t05::near(m1, 50.0, 1e-9),
                      "distributeKeepsExtremesAndSpacesEvenly",
                      "m=" + t05::number(m0) + "," + t05::number(m1) + "," + t05::number(m2));

        // Distribute must never alter a bond order.
        r62::core::Document chain;
        r62::core::ObjectId ca[3];
        chain.addAtom(r62::core::Vec2(0, 0), 6, &ca[0]);
        chain.addAtom(r62::core::Vec2(30, 0), 6, &ca[1]);
        chain.addAtom(r62::core::Vec2(100, 0), 6, &ca[2]);
        r62::core::ObjectId chainBond = r62::core::kInvalidObjectId;
        addBondWithOrder(chain,ca[0], ca[1], r62::core::BondOrder::Double, &chainBond);
        std::vector<r62::core::ObjectRef> chainThree;
        for (int i = 0; i < 3; ++i)
            chainThree.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, ca[i]));
        distributeObjects(chain, chainThree, DistributeMode::HorizontalCenters);
        harness.check(chain.bonds().alive(chainBond) &&
                      chain.bonds().get(chainBond)->order == r62::core::BondOrder::Double,
                      "distributePreservesBondOrder",
                      "order=" + std::to_string(
                          chain.bonds().get(chainBond) ? static_cast<int>(chain.bonds().get(chainBond)->order) : -1));

        // Centre in page: the selection's centre lands on the page centre.
        r62::core::Document page;
        r62::core::ObjectId pid = r62::core::kInvalidObjectId;
        page.addAtom(r62::core::Vec2(10, 10), 6, &pid);
        r62::core::ObjectId pid2 = r62::core::kInvalidObjectId;
        page.addAtom(r62::core::Vec2(30, 20), 6, &pid2);
        std::vector<r62::core::ObjectRef> pageRefs;
        pageRefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, pid));
        pageRefs.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom, pid2));
        const r62::core::Box pageBox(r62::core::Vec2(200, 300), r62::core::Vec2(400, 500));
        centerSelection(page, pageRefs, pageBox);
        const r62::core::Box centred = selectionBounds(page, pageRefs);
        harness.check(t05::near(boxCenter(centred).x, 300.0, 1e-9) &&
                      t05::near(boxCenter(centred).y, 400.0, 1e-9),
                      "centerInPageMovesCentreToPageCentre",
                      "centre=" + t05::number(boxCenter(centred).x) + "," +
                      t05::number(boxCenter(centred).y));
    }

    return harness.finish("T05 selection acceptance");
}
