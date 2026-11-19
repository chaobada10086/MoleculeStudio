#ifndef R62_NEXT_PERSISTENCE_PERSISTENCE_HPP
#define R62_NEXT_PERSISTENCE_PERSISTENCE_HPP
// =============================================================================
// re_round62_viewer/next/persistence/persistence.hpp
//
// T02 - PERSISTENCE: CDXML / native document / selection package / clipboard
//                   payload compatibility.
//
// BUILT ON T01 AND NOT REPLACING IT
// -----------------------------------------------------------------------------
// This module owns NO document model. Every function here takes
// r62::core::Document (or r62::core::Document::RawState) and speaks the public
// vocabulary from next/core/contracts.hpp. It never redefines Atom/Bond/Text,
// never declares a second main model and never installs a WndProc.
//
// WHAT EQUIVALENCE MEANS HERE  (three DIFFERENT words, never synonyms)
// -----------------------------------------------------------------------------
//   * STRUCTURAL EQUIVALENCE - the logical document content is the same:
//     same atoms (markings + coordinates), same bonds (endpoints + all four
//     axes), same text (runs + formatting), same extension entries, same
//     defaults. Compared field by field by an INDEPENDENT comparator
//     (tools/semantic_compare.py), not by re-running this writer.
//   * DISPLAY EQUIVALENCE   - renders the same when read back through a
//     reader that understands the document. Can be reachable when structural
//     equivalence is not (two spellings of one font, re-ordered bond lists).
//   * BYTE EQUIVALENCE      - the serialized byte stream is identical. Only
//     meaningful for load X then export X with no edit; this module DOES claim
//     and test byte identity for that case, by re-emitting source fragments it
//     preserved instead of regenerating them.
//
// The three are reported SEPARATELY by DocumentRoundTrip. A round trip that is
// only display-equivalent must never be reported as lossless.
//
// HONESTY RULES BAKED INTO THE API
// -----------------------------------------------------------------------------
// R1  An edit or export that cannot preserve semantics is REFUSED with a
//     locatable diagnostic (object ref + source id + CDXML element + reason).
//     It is NEVER approximated into a single bond or a plain carbon and then
//     saved anyway. See RefusalReport.
// R2  Unknown objects, unknown attributes and unedited source regions are
//     PRESERVED, not dropped. Unknown editable objects are preserved as opaque
//     source fragments; unknown attributes of edited objects are kept in the
//     T01 extension bag and re-emitted.
// R3  Export NEVER overwrites its own input and NEVER overwrites a vendor or
//     user file. Atomic replace refuses to replace a different file.
// R4  Binary CDX import is NOT implemented. There is no openly published vendor
//     spec for the .cdx binary and no official third-party converter reachable
//     from this project; a guessed parser would be worse than a refusal. See
//     cdxSupportStatus().
// =============================================================================

#include "next/core/document.hpp"
#include "next/core/serialization.hpp"

#include <iosfwd>
#include <sstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace r62 {
namespace persistence {

// ---------------------------------------------------------------------------
// Provenance / honesty constants
// ---------------------------------------------------------------------------
enum {
    kCoreInterfaceVersionMin = 10000,   // 1.0.0
    kCoreInterfaceMajor      = 1,
    kCoreInterfaceMinor      = 0
};

enum {
    kExtFormatVersion = 1,        // kR62EXT container format version
    kSelFormatVersion = 1,        // selection package payload version
    kSelMagicSize     = 8
};

extern const char* const kExtMagic;         // "R62EXT"
extern const char* const kSelMagic;         // "R62SEL1"

// Preserved-extension key namespaces this module writes. Documented, because
// other modules will read them.
//
//   pst.source.magic          document bag: source container magic
//   pst.source.encoding       document bag: source encoding declaration
//   pst.catalog.<name>        document bag: type catalog entry
//   pst.wire.<element>/<id>   document bag: opaque container for raw CDXML of
//                             an object we could not model itself
//   pst.raw.<id>              document bag: verbatim source fragment bytes
//   pst.src.<ObjectKind>.<id> object bag: verbatim source bytes
//   pst.attr.order.<ObjectKind>.<id>  object bag: original attribute order
extern const char* const kExtensionPrefix;

// ---------------------------------------------------------------------------
// Package-level capability statement
// ---------------------------------------------------------------------------
// Mirrors what RESULTS.md / coverage.csv report. Exposed as an API so a host
// (T18/T19) can query it at runtime instead of trusting a document.
struct SupportStatus {
    bool cdxmlImportSubset;        // the documented, validated CDXML subset
    bool cdxmlImportPreserving;    // preserves unknown objects as raw fragments
    bool cdxmlExportNew;           // export of the modelled subset (new document)
    bool cdxmlExportLossless;      // load -> export byte identity, no edit
    bool cdxmlExportRefusesLossy;  // refuses instead of silently degrading
    bool nativeDocument;           // kR62EXT extended container
    bool selectionPackage;         // encode/decode + paste remapping
    bool win32Clipboard;           // T05 owns this; always false here
    bool binaryCdxImport;          // always false here
    SupportStatus();
};
SupportStatus supportStatus();

// Human-readable reason a capability is absent. Never empty for a false flag.
const char* cdxSupportStatus();
const char* clipboardOwnershipNote();

// ---------------------------------------------------------------------------
// Identity map: source CDXML id <-> internal stable ObjectId
// ---------------------------------------------------------------------------
// Needed for two different jobs:
//   1. Patched re-export: an object whose source id we can still find can be
//      written back in place, keeping every attribute we never understood.
//   2. Paste-time reference rewriting: every intra-selection reference (bond
//      endpoints, annotation anchors, table cells, group members, source ids)
//      must be remapped into the destination document id space, and a
//      collision must be DETECTED, not papered over.
//
// Objects the document carries no source id for (created in this session) map
// to kInvalidObjectId and are exported as new objects.
class SourceIdMap {
public:
    enum { kNoSourceId = 0 };

    SourceIdMap() {}

    // Record a mapping. A repeated insertion of the SAME pair is a no-op;
    // a different pair with a colliding source id is REFUSED (returns false)
    // and the first mapping is kept. Duplicate source ids in one file are a
    // fact of life (two imports, two fragments); pretending they are unique
    // is what produces silently mis-wired bonds.
    bool bind(int32_t sourceId, const core::ObjectRef& ref);
    // Duplicate detection without mutation.
    bool collides(int32_t sourceId) const;
    // All refs seen for one source id (more than one == a collision).
    const std::vector<core::ObjectRef>* refsFor(int32_t sourceId) const;
    core::ObjectRef refFor(int32_t sourceId) const;          // first binding
    int32_t sourceIdFor(const core::ObjectRef& ref) const;   // 0 when absent
    // Sorted, unique list of source ids that map to more than one object. Materialised on
    // demand from a std::set because the check inside bind() is on the hot read path and a
    // linear scan over a growing vector is quadratic.
    const std::vector<int32_t>& collisions() const;
    size_t size() const { return forward_.size(); }
    void clear();
    // Build from a document: every alive object carrying sourceId != 0.
    // Duplicate source ids across slabs are REPORTED, never merged.
    static SourceIdMap fromDocument(const core::Document& document);
    // Source ids that BOTH maps contain. Duplicate imports are expected here;
    // the caller decides whether to renumber or refuse.
    std::vector<int32_t> intersections(const SourceIdMap& other) const;

private:
    std::map<int32_t, std::vector<core::ObjectRef> > forward_;
    std::map<core::ObjectRef, int32_t> backward_;
    std::set<int32_t> collisionSet_;
    mutable std::vector<int32_t> collisionsSnapshot_;
};

// ---------------------------------------------------------------------------
// Diagnostics shared by import and export
// ---------------------------------------------------------------------------
// One refused item. path is a structural description such as
// /CDXML/page[2]/fragment[1]/graphic[3] and sourceId is the CDXML id when the
// element had one, so a caller can point a user at the object.
struct Refusal {
    std::string path;
    std::string element;
    std::string attribute;   // empty when the element itself is the problem
    std::string reason;
    int32_t     sourceId;
    core::ObjectRef ref;     // resolved when the object exists in the document
    Refusal() : sourceId(0) {}
    // One stable line per refusal; tests assert on it, humans read it.
    std::string summaryLine() const {
        std::ostringstream line;
        line << path;
        if (!element.empty()) line << " <" << element << ">";
        if (sourceId != 0) line << " id=" << sourceId;
        if (!attribute.empty()) line << " @" << attribute;
        line << ": " << reason;
        return line.str();
    }
};

struct RefusalReport {
    std::vector<Refusal> items;
    void add(const Refusal& refusal) { items.push_back(refusal); }
    bool empty() const { return items.empty(); }
    size_t size() const { return items.size(); }
    // Compact multi-line summary, stable enough to assert on in tests.
    std::string summary() const;
    void clear() { items.clear(); }
};

// What a load actually did. Counts are evidence, not decoration.
struct CdxmlReadReport {
    uint64_t pages, fragments, nodes, bonds, texts, textRuns;
    uint64_t fonts, fontSizes, colors;
    uint64_t graphics, rawElements, rawFragments;
    uint64_t unknownAttributesPreserved;
    uint64_t duplicateSourceIds;
    uint64_t danglingReferences;
    uint64_t objectsImported;
    CdxmlReadReport();
};

// How faithful a particular write was. Kept next to the write, not inferred.
struct CdxmlWriteFidelity {
    uint64_t objectsFromSource;     // re-emitted from preserved source bytes
    uint64_t objectsRegenerated;    // generated because no source existed
    uint64_t rawFragmentsEmitted;
    uint64_t unknownAttributesEmitted;
    uint32_t newFileIdsAssigned;
    CdxmlWriteFidelity();
};

// ---------------------------------------------------------------------------
// CDXML read
// ---------------------------------------------------------------------------
struct CdxmlReadOptions {
    // Keep source fragments / attributes we cannot model. Default true: a
    // reader that drops them cannot be part of a lossless workflow at all.
    bool preserveUnknowns;
    // When a semantic feature is found that the model COULD hold but this
    // reader does not map, refuse the load (default) instead of importing a
    // document that would be silently wrong downstream.
    bool refuseUnmappedSemantics;
    // Reject an input in which two objects claim the same CDXML id or a bond
    // references an atom that does not exist.
    bool strictReferences;
    // Hard ceiling on objects, so a hostile file cannot exhaust memory.
    uint64_t maxObjects;
    CdxmlReadOptions();
};

// Read CDXML bytes into document. The document must be EMPTY; on failure it is
// left completely untouched (all-or-nothing, like the T01 adapter).
core::Error readCdxml(const std::string& bytes, core::Document& document,
                      CdxmlReadReport* report = 0, RefusalReport* refusals = 0,
                      const CdxmlReadOptions& options = CdxmlReadOptions());

core::Error readCdxmlFile(const std::string& path, core::Document& document,
                          CdxmlReadReport* report = 0, RefusalReport* refusals = 0,
                          const CdxmlReadOptions& options = CdxmlReadOptions());

// Raw byte read. Kept separate so tests can prove Unicode paths work without
// depending on the process locale.
core::Error readFileBytes(const std::string& path, std::string* bytes);

// ---------------------------------------------------------------------------
// CDXML write
// ---------------------------------------------------------------------------
enum class CdxmlExportMode {
    // Re-emit preserved source fragments/attributes when the object has not
    // been touched. Byte identity is claimed ONLY for the no-edit case.
    Lossless = 0,
    // Emit only what the model holds. Documented as lossy; exists for
    // comparison and for export a fresh file.
    NewDocument = 1
};

struct CdxmlWriteOptions {
    CdxmlExportMode mode;
    // Refuse (default) rather than emit a document that lost semantics.
    bool failOnLossyEntities;
    // Allow writing when a duplicate source id exists (the objects are then
    // emitted as new ids instead of overwriting each other).
    bool allowDuplicateSourceIds;
    bool prettyPrint;
    CdxmlWriteOptions();
};

// Count what a write would lose, WITHOUT writing. This is what makes explicit
// refusal testable: a caller can ask first, then decide.
struct CdxmlFidelityPlan {
    uint64_t objectsPreserved;
    uint64_t objectsRegenerated;
    uint64_t objectsLost;
    uint64_t objectsRefused;
    std::vector<std::string> losses;   // one line per reason
    CdxmlFidelityPlan();
    bool lossless() const { return objectsLost == 0 && objectsRefused == 0; }
};

core::Error planCdxmlWrite(const core::Document& document, CdxmlFidelityPlan* plan,
                           const CdxmlWriteOptions& options = CdxmlWriteOptions());

core::Error writeCdxml(const core::Document& document, std::ostream& output,
                       RefusalReport* refusals = 0,
                       CdxmlWriteFidelity* fidelity = 0,
                       const CdxmlWriteOptions& options = CdxmlWriteOptions());

core::Error writeCdxml(const core::Document& document, const std::string& path,
                       RefusalReport* refusals = 0,
                       CdxmlWriteFidelity* fidelity = 0,
                       const CdxmlWriteOptions& options = CdxmlWriteOptions());

std::string writeCdxmlToString(const core::Document& document,
                               RefusalReport* refusals = 0,
                               CdxmlWriteFidelity* fidelity = 0,
                               const CdxmlWriteOptions& options = CdxmlWriteOptions());

// ---------------------------------------------------------------------------
// Round trip assessment
// ---------------------------------------------------------------------------
// Three separate verdicts. Never collapse them into one ok flag.
struct DocumentRoundTrip {
    bool structural;   // logical content identical
    bool display;      // renders identically
    bool byteExact;    // serialized bytes identical
    core::Error readError;
    core::Error writeError;
    std::string detail;
    DocumentRoundTrip();
};

// load -> write(mode) -> load -> compare.
DocumentRoundTrip assessRoundTrip(const std::string& sourceBytes,
                                  CdxmlExportMode mode = CdxmlExportMode::Lossless,
                                  const CdxmlWriteOptions& options = CdxmlWriteOptions());

// ---------------------------------------------------------------------------
// Extended native document (kR62EXT)
// ---------------------------------------------------------------------------
// A CONTAINER around the T01 T01DOC payload, NOT a replacement format:
//
//   R62EXT <containerVersion> <payloadMagic> <payloadVersion> <encoding> <payloadBytes>
//   K <keyHex> <valueHex>                 (catalog; see DocumentCatalog)
//   <payload bytes, exactly payloadBytes>
//   Z <crc32Hex>
//
// Why a container: the T01 native format has no room for a self-describing
// type catalog, no checksum, and no place to record which reader version wrote
// it. Adding fields to T01DOC would be a T01 interface change, which this task
// forbids T02 from making; an outer container achieves the same goal while
// keeping the T01 payload byte-identical, so an older T01 reader can still read
// the payload if it is extracted.
enum class UnknownTypePolicy {
    // Refuse to LOAD when the catalog declares a type this build cannot read:
    // the document would lose objects a newer writer put there.
    Refuse = 0,
    // Load what is understood and keep the declaration for the next save.
    Preserve = 1
};

struct DocumentCatalog {
    int32_t   formatVersion;        // container version
    std::string producer;           // "T02 persistence"
    std::string producerVersion;
    uint64_t  payloadChecksum;      // CRC-32 of the payload, 0 == absent
    std::vector<std::string> knownTypes;      // type names this writer understood
    std::vector<std::string> unknownTypes;    // declarations it did not understand
    std::map<std::string, uint64_t> typeCounts;  // type name -> live objects
    DocumentCatalog();
};

struct NativeSaveOptions {
    core::SaveOptions payload;      // passed straight to T01 saveDocument
    bool writeCatalog;              // default true
    bool writeChecksum;             // default true
    UnknownTypePolicy unknownPolicy;
    std::vector<std::string> declaredTypes;   // extra declarations to record
    NativeSaveOptions();
};

struct NativeLoadReport {
    int32_t  containerVersion;
    std::string encoding;
    bool     containerPresent;      // false == a bare T01DOC payload
    uint64_t payloadBytes;
    uint64_t payloadChecksumStored;
    uint64_t payloadChecksumComputed;
    bool     checksumVerified;
    core::LoadReport payloadReport;
    DocumentCatalog catalog;
    NativeLoadReport();
};

core::Error saveNative(const core::Document& document, std::ostream& output,
                       const NativeSaveOptions& options = NativeSaveOptions(),
                       const DocumentCatalog* catalogOverride = 0);
core::Error saveNative(const core::Document& document, const std::string& path,
                       const NativeSaveOptions& options = NativeSaveOptions(),
                       const DocumentCatalog* catalogOverride = 0);
core::Error loadNative(core::Document& document, std::istream& input,
                       NativeLoadReport* report = 0,
                       UnknownTypePolicy policy = UnknownTypePolicy::Refuse);
core::Error loadNative(core::Document& document, const std::string& path,
                       NativeLoadReport* report = 0,
                       UnknownTypePolicy policy = UnknownTypePolicy::Refuse);
// Inspect a container header without touching a Document.
core::Error inspectNative(const std::string& bytes, NativeLoadReport* report);
// Extract the bare T01DOC payload (what an older T01 reader could consume).
core::Error extractNativePayload(const std::string& bytes, std::string* payload);

// CRC-32 (IEEE 802.3), exposed because the acceptance test recomputes it with
// an independent implementation.
uint32_t crc32(const void* data, size_t bytes, uint32_t seed = 0);

// ---------------------------------------------------------------------------
// Selection package (T05 publishes this on the Win32 clipboard)
// ---------------------------------------------------------------------------
// The package is a SELF-CONTAINED document: no reference may point outside it,
// because after a copy the source document may be gone. References are stored
// as package-local ids; decodeSelectionPackage remaps them into the target.
struct SelectionPackageInfo {
    int32_t  version;
    uint64_t objectCount;
    uint64_t atomCount, bondCount, textCount, otherCount;
    uint64_t byteSize;
    std::vector<int32_t> sourceIds;
    SelectionPackageInfo();
};

// refs == the objects to copy. Objects referenced BY the selection but not
// listed are included automatically (a bond whose atoms were selected), which
// is what makes the package self-contained.
core::Error encodeSelectionPackage(const core::Document& document,
                                   const std::vector<core::ObjectRef>& refs,
                                   std::string* output,
                                   SelectionPackageInfo* info = 0);

core::Error decodeSelectionPackage(const std::string& bytes,
                                   core::Document& target,
                                   std::vector<core::ObjectRef>* inserted,
                                   RefusalReport* refusals = 0);

core::Error inspectSelectionPackage(const std::string& bytes,
                                    SelectionPackageInfo* info);

// Remap every internal reference of a package into another document id space.
// Exposed separately because a host may want to preview the mapping before
// committing to an insertion.
struct PasteMapping {
    std::map<core::ObjectId, core::ObjectId> atoms;
    std::map<core::ObjectId, core::ObjectId> bonds;
    std::map<core::ObjectId, core::ObjectId> texts;
    std::map<core::ObjectId, core::ObjectId> others;
    std::vector<int32_t> remappedSourceIds;
    uint64_t referencesRewritten;
    PasteMapping();
};

core::Error remapForPaste(const core::Document& incoming,
                          core::Document& target,
                          PasteMapping* mapping,
                          RefusalReport* refusals = 0);

// ---------------------------------------------------------------------------
// Atomic, non-destructive file writing
// ---------------------------------------------------------------------------
// Guarantees, in order of importance:
//   1. A failure at ANY point leaves the previous destination file intact.
//   2. The destination is never the file the content came from (refused).
//   3. The bytes land before the rename, and the rename is the last step.
struct AtomicWriteOptions {
    // Refuse when destination and sourcePath resolve to the same file.
    std::string sourcePath;
    // Post-write verification: re-read the temporary file and compare bytes.
    bool verifyAfterWrite;
    // Verify the temp file is a complete, loadable document before replacing.
    bool verifyParses;
    AtomicWriteOptions();
};

struct AtomicWriteResult {
    uint64_t bytesWritten;
    bool     verified;
    bool     replacedExisting;
    bool     tempRemoved;
    AtomicWriteResult();
};

core::Error writeFileAtomic(const std::string& path, const std::string& bytes,
                            const AtomicWriteOptions& options = AtomicWriteOptions(),
                            AtomicWriteResult* result = 0);

// Cross-platform helpers used by the above and by the tests.
bool sameFile(const std::string& a, const std::string& b);
bool fileExists(const std::string& path);

// Path handling that must work with non-ASCII names on Windows. These are the
// ONLY places in this module that touch a path, so a Unicode bug has one home.
std::string pathDirectory(const std::string& path);
std::string pathFileName(const std::string& path);
std::string pathJoin(const std::string& directory, const std::string& name);

} // namespace persistence
} // namespace r62

#endif // R62_NEXT_PERSISTENCE_PERSISTENCE_HPP
