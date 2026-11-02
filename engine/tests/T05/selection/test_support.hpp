// tests/T05/selection/test_support.hpp
//
// Shared harness for the T05 acceptance tests. Same output convention as
// tests/T01/core and tests/T04/interaction, so the console logs concatenate
// without a parser:
//
//   [PASS|FAIL] <suite>.<check>  <detail>
//
// and a non-zero exit when any check failed.
#ifndef R62_T05_TEST_SUPPORT_HPP
#define R62_T05_TEST_SUPPORT_HPP

#include "next/selection/commands.hpp"
#include "next/selection/overlay.hpp"
#include "next/selection/selection_set.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace t05 {

using namespace r62::selection;

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------
class Harness {
public:
    Harness() : checks_(0), failures_(0) {}
    void suite(const std::string& name) { suite_ = name; }
    void check(bool condition, const std::string& name, const std::string& detail) {
        ++checks_;
        if (!condition) ++failures_;
        std::printf("[%s] %s.%s  %s\n", condition ? "PASS" : "FAIL",
                    suite_.c_str(), name.c_str(), detail.c_str());
    }
    void note(const std::string& text) { std::printf("[NOTE] %s\n", text.c_str()); }
    int checks() const { return checks_; }
    int failures() const { return failures_; }
    int finish(const std::string& title) {
        std::printf("\n%s: %d checks, %d failures\n", title.c_str(), checks_, failures_);
        return failures_ == 0 ? 0 : 1;
    }
private:
    int checks_;
    int failures_;
    std::string suite_;
};

std::string number(double v);
bool near(double a, double b, double tolerance = 1e-9);

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------
// A benzene ring: 6 atoms at radius 1.4*10/... no: the ring radius is derived
// from the default bond length so the atoms are exactly one bond apart.
struct BenzeneFixture {
    r62::core::Document document;
    std::vector<r62::core::ObjectId> atoms;
    std::vector<r62::core::ObjectId> bonds;
    r62::core::ObjectId caption;
    r62::core::ObjectId group;
    BenzeneFixture();
    // The ring's exact centre, so a rotate/flip test knows its own pivot.
    r62::core::Vec2 centre() const;
};

// A chain of `count` atoms in a grid, spaced 30 units, exactly the fixture the
// T04 50,000-atom perf test uses so the numbers are comparable.
struct GridFixture {
    r62::core::Document document;
    std::vector<r62::core::ObjectId> atoms;
    GridFixture(size_t count, bool connectChain);
};

// A document with one of EVERY object kind T05 must handle, so a transform that
// silently skips a table is caught rather than invisible.
struct MixedFixture {
    r62::core::Document document;
    std::vector<r62::core::ObjectRef> all;
    r62::core::ObjectRef atom;
    r62::core::ObjectRef bond;
    r62::core::ObjectRef text;
    r62::core::ObjectRef path;
    r62::core::ObjectRef arrow;
    r62::core::ObjectRef table;
    r62::core::ObjectRef group;
    std::vector<r62::core::ObjectId> tableCells;
    MixedFixture();
};

// ---------------------------------------------------------------------------
// An indexed SelectionQuery, to prove the marquee paths agree
// ---------------------------------------------------------------------------
// Deliberately NOT another full scan: it keeps a coarse uniform grid of object
// boxes and answers a box query from the cells the query overlaps. The test
// compares it against DocumentSelectionQuery (a full scan) on the same document
// and asserts the ObjectRef sets are IDENTICAL. That is the same technique T04
// uses for the BVH, applied to selection.
class GridSelectionQuery : public SelectionQuery {
public:
    explicit GridSelectionQuery(const r62::core::Document& document, double cellSize);
    void build();
    virtual std::vector<r62::core::ObjectRef> objectsInBox(const r62::core::Box& box) const;
    virtual std::vector<r62::core::ObjectRef> objectsInPolygon(const std::vector<r62::core::Vec2>& points,
                                                               bool containedOnly) const;
    virtual std::vector<r62::core::ObjectRef> allObjects() const;
    virtual r62::core::Box objectBoundsForQuery(const r62::core::ObjectRef& ref) const;
    virtual uint64_t geometrySerial() const { return serial_; }
    void bumpSerial() { ++serial_; }
    size_t cellsVisited() const { return cellsVisited_; }
    void resetCounters() { cellsVisited_ = 0; }
    size_t candidateCount() const { return refs_.size(); }
private:
    void cellRange(const r62::core::Box& box, long* x0, long* y0, long* x1, long* y1) const;
    const r62::core::Document& document_;
    double cellSize_;
    uint64_t serial_;
    std::vector<r62::core::ObjectRef> refs_;
    std::vector<r62::core::Box> boxes_;
    std::map<std::pair<long, long>, std::vector<size_t> > cells_;
    mutable size_t cellsVisited_;
};

// ---------------------------------------------------------------------------
// A selection sink for the delete/transaction bridge
// ---------------------------------------------------------------------------
// The cut path takes a DeleteObjectsFn. This logger counts the calls and records
// the refs, so a test can assert "deleteFn was NEVER called" on a publish
// failure - which is the literal statement of rule R1.
struct DeleteLog {
    size_t calls;
    std::vector<r62::core::ObjectRef> lastRefs;
    r62::core::Error forcedError;
    DeleteLog() : calls(0), forcedError() {}
    void clear() { calls = 0; lastRefs.clear(); }
};
r62::core::Error countingDeleteFn(void* userData,
                                  const std::vector<r62::core::ObjectRef>& refs);

// Apply a cut's delete through a real CommandLog, so the undo path is exercised.
struct LogDeleteBridge {
    r62::core::CommandLog* log;
    r62::core::Document*   document;
    LogDeleteBridge() : log(0), document(0) {}
};
r62::core::Error logDeleteFn(void* userData,
                             const std::vector<r62::core::ObjectRef>& refs);

// ---------------------------------------------------------------------------
// A crypto-free digest of the document, used as an INDEPENDENT check
// ---------------------------------------------------------------------------
// Document::digest() is T01's; this is a second, differently-computed signature
// over the same logical content, so "the failure changed nothing" is not
// verified by the same code that performed the change.
std::string independentDocumentSignature(const r62::core::Document& document);

// Median / p95 of a sample vector, in the units the caller pushed.
struct SampleStats {
    double median;
    double p95;
    double min;
    double max;
    size_t count;
    SampleStats() : median(0), p95(0), min(0), max(0), count(0) {}
};
SampleStats summarise(std::vector<double> samples);

} // namespace t05

#endif // R62_T05_TEST_SUPPORT_HPP
