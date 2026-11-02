// tests/T05/selection/t05_perf_test.cpp
//
// T05 performance measurement. What it measures and what it does NOT:
//
//   MEASURED   the T05 functions themselves, on the same machine, the same
//              fixtures and the same build flags as the acceptance suite:
//                * marquee query, indexed vs full scan
//                * select-all / inverse
//                * connected-molecule walk on a 50,000-long chain
//                * a full translate over the whole selection (one transaction)
//                * the overlay batch build and merge
//   NOT MEASURED  end-to-end input latency, message dispatch, or GPU present.
//              A selection-module timing is NOT a ChemDraw speedup claim, and
//              is reported as such.
//
// Every run prints median / p95 / min / max over N samples, plus the cache
// rebuild counts, so a number can be traced to the code path that produced it.
#include "test_support.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

using namespace r62::selection;

namespace {

// Nanoseconds. A microsecond clock cannot resolve the incremental paths (they
// measure at tens of nanoseconds), and reporting 0.0000ms for a real cost would
// be a measurement claim the tooling cannot support.
double nowNanos() {
    static bool haveChrono = true;
    if (haveChrono) {
        // std::chrono::steady_clock is monotonic and has nanosecond resolution.
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    return static_cast<double>(std::clock()) * 1.0e9 / static_cast<double>(CLOCKS_PER_SEC);
}

struct Scenario {
    std::string name;
    size_t atoms;
    std::vector<double> samples;
};

void reportScenario(t05::Harness& harness, const Scenario& scenario, const std::string& extra) {
    const t05::SampleStats stats = t05::summarise(scenario.samples);
    char detail[256];
    std::snprintf(detail, sizeof(detail),
                  "objects=%zu median=%.4fms p95=%.4fms min=%.4fms max=%.4fms n=%zu %s",
                  scenario.atoms, stats.median / 1.0e6, stats.p95 / 1.0e6,
                  stats.min / 1.0e6, stats.max / 1.0e6, stats.count, extra.c_str());
    // A measured scenario must have samples and a positive median. A nanosecond
    // clock makes "0.0" mean "below the clock's resolution", which is a real
    // result - so the assertion is on the SAMPLE COUNT plus a finite median.
    harness.check(stats.count > 0 && stats.median >= 0.0 && std::isfinite(stats.median),
                  scenario.name, detail);
}

} // namespace

int main(int argc, char** argv) {
    t05::Harness harness;
    size_t samples = 30;
    std::string outDir;
    if (argc > 1) outDir = argv[1];
    if (argc > 2) samples = static_cast<size_t>(std::atoi(argv[2]));
    if (samples < 3) samples = 3;

    std::printf("T05 perf: samples=%zu outDir=%s\n", samples,
                outDir.empty() ? "(none)" : outDir.c_str());

    // =====================================================================
    // 5,000 and 50,000 atoms: marquee, select all, molecule walk, drag commit
    // =====================================================================
    const size_t counts[2] = { 5000, 50000 };
    for (int ci = 0; ci < 2; ++ci) {
        const size_t count = counts[ci];
        std::printf("\n--- %zu atoms ---\n", count);
        t05::GridFixture fixture(count, true);
        DocumentSelectionQuery reference(fixture.document);
        t05::GridSelectionQuery indexed(fixture.document, 60.0);
        harness.suite(std::string("perf") + std::to_string(count));

        // --- marquee over a small region, INDEXED -------------------------
        // A 10x10-atom region: the cost must be LOCAL, not O(N).
        {
            Scenario scenario;
            scenario.name = "marqueeIndexedSmall";
            scenario.atoms = count;
            MarqueeGesture gesture;
            gesture.anchor = r62::core::Vec2(-15, -15);
            gesture.current = r62::core::Vec2(315, 315);
            MarqueeOptions options;
            options.mode = MarqueeMode::Intersecting;
            size_t hits = 0;
            for (size_t i = 0; i < samples; ++i) {
                indexed.resetCounters();
                const double t0 = nowNanos();
                const std::vector<r62::core::ObjectRef> found =
                    queryMarquee(indexed, gesture, options, 0);
                scenario.samples.push_back(nowNanos() - t0);
                hits = found.size();
            }
            char extra[128];
            std::snprintf(extra, sizeof(extra), "hits=%zu cellsVisited=%zu totalObjects=%zu",
                          hits, indexed.cellsVisited(), indexed.candidateCount());
            reportScenario(harness, scenario, extra);
        }

        // --- the SAME marquee, FULL SCAN (the O(N) reference) -------------
        {
            Scenario scenario;
            scenario.name = "marqueeFullScanSmall";
            scenario.atoms = count;
            MarqueeGesture gesture;
            gesture.anchor = r62::core::Vec2(-15, -15);
            gesture.current = r62::core::Vec2(315, 315);
            MarqueeOptions options;
            options.mode = MarqueeMode::Intersecting;
            for (size_t i = 0; i < samples; ++i) {
                const double t0 = nowNanos();
                const std::vector<r62::core::ObjectRef> found =
                    queryMarquee(reference, gesture, options, 0);
                (void)found;
                scenario.samples.push_back(nowNanos() - t0);
            }
            reportScenario(harness, scenario, "reference path");
        }

        // --- select all --------------------------------------------------
        {
            Scenario scenario;
            scenario.name = "selectAll";
            scenario.atoms = count;
            SelectionDelta delta;
            for (size_t i = 0; i < samples; ++i) {
                SelectionSet set;
                const double t0 = nowNanos();
                const SelectionSet all = selectAll(fixture.document, reference, set, &delta);
                (void)all;
                scenario.samples.push_back(nowNanos() - t0);
            }
            char extra[64];
            std::snprintf(extra, sizeof(extra), "delta.entered=%llu",
                          static_cast<unsigned long long>(delta.entered.size()));
            reportScenario(harness, scenario, extra);
        }

        // --- connected molecule from ONE atom on a 50,000-long chain ------
        // The walk is O(component). A recursive implementation would overflow
        // the stack here; this proves the iterative one does not.
        {
            Scenario scenario;
            scenario.name = "moleculeWalk";
            scenario.atoms = count;
            SelectionDelta delta;
            for (size_t i = 0; i < 4; ++i) {
                for (size_t k = 0; k < samples / 4 + 1; ++k) {
                    SelectionSet set;
                    const double t0 = nowNanos();
                    const SelectionSet molecule = selectMolecule(
                        fixture.document, set,
                        r62::core::ObjectRef(r62::core::ObjectKind::Atom,
                                             fixture.atoms[count / 2]), false, &delta);
                    (void)molecule;
                    scenario.samples.push_back(nowNanos() - t0);
                }
            }
            char extra[96];
            std::snprintf(extra, sizeof(extra), "component=%zu (whole chain, %zu objects)",
                          count + (count > 0 ? count - 1 : 0), count);
            reportScenario(harness, scenario, extra);
        }

        // --- full-selection translate, ONE transaction --------------------
        {
            r62::core::Document document = fixture.document;
            r62::core::CommandLog log;
            SelectionSet selection;
            DocumentSelectionQuery query(document);
            selection.addAll(query.allObjects(), SelectionReason::Direct);
            SelectionCommands commands(log, selection, &query);
            Scenario scenario;
            scenario.name = "translateCommitAll";
            scenario.atoms = selection.size();
            for (size_t i = 0; i < (samples < 8 ? samples : 8); ++i) {
                const double t0 = nowNanos();
                const ActionReport moved =
                    commands.translateSelection(document, r62::core::Vec2(1.0, 0.0));
                scenario.samples.push_back(nowNanos() - t0);
                if (!moved.committed) { scenario.samples.clear(); break; }
                log.undo(document);
            }
            char extra[128];
            std::snprintf(extra, sizeof(extra), "selected=%llu entries=%llu",
                          static_cast<unsigned long long>(scenario.atoms),
                          static_cast<unsigned long long>(log.entries()));
            reportScenario(harness, scenario, extra);
        }

        // --- overlay: ONE batch for the whole selection -------------------
        {
            SelectionSet selection;
            DocumentSelectionQuery query(fixture.document);
            selection.addAll(query.allObjects(), SelectionReason::Direct);
            OverlayOptions options;
            Scenario full;
            full.name = "overlayFullBuild";
            full.atoms = selection.size();
            OverlayBatch batch;
            for (size_t i = 0; i < samples; ++i) {
                const double t0 = nowNanos();
                batch = buildSelectionOverlay(fixture.document, selection, options);
                full.samples.push_back(nowNanos() - t0);
            }
            OverlayStats stats;
            recordSubmission(&stats, batch);
            char extra[160];
            std::snprintf(extra, sizeof(extra),
                          "marks=%zu submissions=%llu marksPerSubmission=%.1f",
                          batch.markCount(), static_cast<unsigned long long>(stats.submissions),
                          batch.marksPerSubmission());
            reportScenario(harness, full, extra);

            // The incremental path must be ORDERS OF MAGNITUDE cheaper.
            Scenario incremental;
            incremental.name = "overlayIncrementalOne";
            incremental.atoms = 1;
            SelectionDelta delta;
            delta.entered.push_back(r62::core::ObjectRef(r62::core::ObjectKind::Atom,
                                                         fixture.atoms[3]));
            OverlayBatch piece;
            for (size_t i = 0; i < samples; ++i) {
                const double t0 = nowNanos();
                piece = buildSelectionOverlayIncremental(fixture.document, selection, delta, options);
                incremental.samples.push_back(nowNanos() - t0);
            }
            char extra2[128];
            std::snprintf(extra2, sizeof(extra2), "marks=%zu vs full=%zu",
                          piece.markCount(), batch.markCount());
            reportScenario(harness, incremental, extra2);

            // mergeInto cost, which is the per-frame incremental submission.
            Scenario merge;
            merge.name = "overlayMerge";
            merge.atoms = batch.markCount();
            for (size_t i = 0; i < samples; ++i) {
                OverlayBatch accumulator = batch;
                const double t0 = nowNanos();
                mergeInto(&accumulator, piece);
                merge.samples.push_back(nowNanos() - t0);
            }
            reportScenario(harness, merge, "per incremental frame");
        }

        // --- the requirement, stated as a number --------------------------
        // 60 frames that each submit ONCE. A per-object implementation would
        // have 60*marks submissions; this counts what actually happens.
        {
            SelectionSet selection;
            DocumentSelectionQuery query(fixture.document);
            selection.addAll(query.allObjects(), SelectionReason::Direct);
            OverlayOptions options;
            OverlayBatch batch = buildSelectionOverlay(fixture.document, selection, options);
            OverlayStats stats;
            for (int frame = 0; frame < 60; ++frame) recordSubmission(&stats, batch);
            char detail[320];
            std::snprintf(detail, sizeof(detail),
                          "frames=%llu submissions=%llu perFrame=%.2f marks=%zu "
                          "marksPerSubmission=%.1f (a per-object implementation would "
                          "report submissions=%llu)",
                          static_cast<unsigned long long>(stats.frames),
                          static_cast<unsigned long long>(stats.submissions),
                          static_cast<double>(stats.submissions) /
                              static_cast<double>(stats.frames),
                          batch.markCount(), batch.marksPerSubmission(),
                          static_cast<unsigned long long>(
                              stats.frames * static_cast<uint64_t>(batch.markCount())));
            harness.check(stats.submissions == stats.frames && stats.frames == 60,
                          "batchedSubmissionOver60Frames", detail);
        }
    }

    return harness.finish("T05 selection perf");
}
