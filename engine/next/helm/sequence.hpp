#ifndef R62_NEXT_HELM_SEQUENCE_HPP
#define R62_NEXT_HELM_SEQUENCE_HPP
// =============================================================================
// re_round62_viewer/next/helm/sequence.hpp
//
// T15 - THE BIOPOLYMER SEQUENCE MODEL.
//
// THE ONE RULE THIS FILE EXISTS TO ENFORCE
// -----------------------------------------------------------------------------
//   LAYOUT IS A VIEW. CONNECTIVITY IS CHEMISTRY.
//
// Moving, replacing, re-ordering or re-laying-out a monomer must NEVER change
// which attachment points are bonded to which. So the two are stored separately:
//
//   * SequencePolymer::monomers  holds the CHEMISTRY: an ordered list of
//     MonomerInstance records, each with a stable MonomerInstanceId.
//   * SequenceConnection         holds the CHEMISTRY: an explicit bond between
//     (monomer id, attachment label) and (monomer id, attachment label).
//   * SequenceLayout             holds the VIEW: a position per monomer. It is
//     derived and may be recomputed at any time.
//
// There is NO implicit backbone. Two adjacent monomers in the monomer list are
// NOT bonded just because they are adjacent. The backbone is a run of
// SequenceConnection records, exactly as HELM writes it. That is what makes
// 'sequence insertion does not silently rewire' testable rather than a promise.
//
// IDENTITY
// -----------------------------------------------------------------------------
// MonomerInstanceId is a document-local stable slot index, and it is the SAME
// NUMBER SPACE as core::ObjectId in the mapped document: the adapter reserves
// the T01 slot with that exact id. A connection therefore references a monomer
// by a stable reference, and moving or replacing a monomer leaves the reference
// valid. Deletion is alive = false; ids are never reused or renumbered, which
// mirrors core::ObjectSlab and the legacy CDXEDIT 1 invariant.
//
// =============================================================================

#include "next/helm/monomer.hpp"
#include "next/core/contracts.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace r62 {
namespace helm {

typedef uint32_t MonomerInstanceId;
typedef uint32_t ConnectionId;
static const MonomerInstanceId kInvalidMonomerInstanceId = 0xFFFFFFFFu;
static const ConnectionId kInvalidConnectionId = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// Display mode: structure vs cartoon
// ---------------------------------------------------------------------------
// The guide (PDF page 171-174) describes cartoon (graphic) monomers, whose
// SHAPE encodes the monomer type and whose COLOUR is derived from the natural
// analog, and the classic labelled form. This is a DISPLAY property only: it
// changes no connection and no monomer definition.
enum class SequenceDisplayMode : uint8_t {
    Structure = 0,   // labelled residue text
    Cartoon   = 1    // shape + natural-analog colour
};
const char* sequenceDisplayModeName(SequenceDisplayMode m);
bool sequenceDisplayModeFromName(const std::string& n, SequenceDisplayMode* out);

// The shapes the guide records for cartoon monomers (PDF page 172).
enum class CartoonShape : uint8_t {
    None      = 0,
    Circle    = 1,   // Peptide
    Octagon   = 2,   // DNA/RNA ribose
    Diamond   = 3,   // DNA/RNA phosphate
    Trapezoid = 4,   // DNA/RNA base
    Square    = 5,   // Chem
    Rectangle = 6    // Blob
};
const char* cartoonShapeName(CartoonShape s);
CartoonShape cartoonShapeFor(const MonomerDefinition& definition);

// ---------------------------------------------------------------------------
// Monomer instance
// ---------------------------------------------------------------------------
struct MonomerInstance {
    MonomerInstanceId id;          // stable slot; never reused
    bool              alive;       // deletion sets false
    MonomerDefinition definition;  // resolved chemistry, or unknown/BLOB record
    std::string       sourceText;  // the ORIGINAL token, verbatim (R1)
    int               copyIndex;   // which copy of a repeated symbol this is
    int               dataVersion; // the stage data version that wrote this

    MonomerInstance()
        : id(kInvalidMonomerInstanceId), alive(false), copyIndex(0),
          dataVersion(kStage1DataVersion) {}
    bool isAlive() const { return alive; }
    bool chemistryKnown() const { return definition.chemistryDefined; }
};

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------
// `pair` connections are hydrogen bonds and are marked so, because HELM treats
// them as a different section and the guide (PDF page 185) records that base
// pairing is what a hairpin layout depends on.
enum class ConnectionKind : uint8_t {
    Covalent = 0,   // a normal inter/intra polymer bond, R<n> to R<m>
    Hydrogen = 1,   // the HELM 'pair' keyword
    Generic  = 2    // the HELM 'generic' keyword: endpoints unspecified
    // NOTE: inter-chain, complementary strand and hairpin are LAYOUT and
    // ANNOTATION concepts built on these three edge kinds, not a fourth edge.
};
const char* connectionKindName(ConnectionKind k);

// The mirror of a backbone attachment label when a chain is REVERSED: R1 and R2
// swap, R3 and every other label are unchanged. Reversing a polymer must not
// change WHAT is bonded, only which end is read as the start, so the two ends of
// a covalent backbone edge trade places. Exposed as a pure function so a test
// can check the rule without building a model.
std::string oppositeBackboneAttachment(const std::string& label);

struct ConnectionEndpoint {
    size_t      polymer;     // index into SequenceModel::polymers()
    size_t      position;    // 0-based index inside that polymer monomer list
    std::string attachment;  // R1 | R2 | pair | (empty for generic)
    ConnectionEndpoint() : polymer(0), position(0) {}
    ConnectionEndpoint(size_t p, size_t pos, const std::string& a)
        : polymer(p), position(pos), attachment(a) {}
};

struct SequenceConnection {
    ConnectionId      id;
    bool              alive;
    ConnectionKind    kind;
    ConnectionEndpoint from;
    ConnectionEndpoint to;
    std::string       annotation;  // the quoted label HELM allows on an edge
    SequenceConnection() : id(kInvalidConnectionId), alive(false),
                           kind(ConnectionKind::Covalent) {}
};

// ---------------------------------------------------------------------------
// Polymer
// ---------------------------------------------------------------------------
struct SequencePolymer {
    std::string  polymerId;                  // e.g. PEPTIDE1; unique in a model
    PolymerType  type;
    std::vector<MonomerInstance> monomers;   // index == position
    SequenceDisplayMode display;
    // Guide PDF page 185: a DNA/RNA strand type of 'ss' (sense) or 'as'
    // (antisense) is written as a third-section 'strandtype' annotation.
    // An empty string means the source did not state one.
    std::string  strandType;
    SequencePolymer() : type(PolymerType::Unknown),
                         display(SequenceDisplayMode::Structure) {}

    size_t liveCount() const;
    int    firstLiveIndex() const;
    int    lastLiveIndex() const;
};

// ---------------------------------------------------------------------------
// Layout (VIEW ONLY)
// ---------------------------------------------------------------------------
// A flat, derived annotation. Nothing here is chemistry; regenerating it must
// not change a single connection. `lineBreakBefore` records a guide feature
// (PDF page 191): a residue can start a new line.
struct MonomerLayout {
    core::Vec2 position;
    bool       lineBreakBefore;
    MonomerLayout() : lineBreakBefore(false) {}
};

struct SequenceLayout {
    std::map<MonomerInstanceId, MonomerLayout> placements;
    double residuePitch;   // document units between adjacent residues
    double linePitch;      // document units between wrapped lines
    uint64_t computedFor;  // the model serial this layout was built from
    SequenceLayout() : residuePitch(14.4), linePitch(18.0), computedFor(0) {}
    void clear() { placements.clear(); }
    size_t size() const { return placements.size(); }
    bool has(MonomerInstanceId id) const { return placements.find(id) != placements.end(); }
    core::Vec2 get(MonomerInstanceId id) const {
        std::map<MonomerInstanceId, MonomerLayout>::const_iterator it = placements.find(id);
        return it == placements.end() ? core::Vec2(0, 0) : it->second.position;
    }
};

// ---------------------------------------------------------------------------
// A diagnostic with a POSITION
// ---------------------------------------------------------------------------
// The acceptance criteria require the parse error position for every stage.
// `offset` is a 0-based byte offset into whatever text was parsed; `line` and
// `column` are 1-based and derived from it.
struct SequenceDiagnostic {
    core::ErrorCode code;
    std::string     message;
    size_t          offset;
    size_t          line;
    size_t          column;
    std::string     polymerId;
    std::string     monomerSymbol;
    SequenceDiagnostic()
        : code(core::ErrorCode::Ok), offset(0), line(0), column(0) {}
    SequenceDiagnostic(core::ErrorCode c, const std::string& m, size_t off)
        : code(c), message(m), offset(off), line(0), column(0) {}
    std::string summary() const;
};

// Compute 1-based line/column from a 0-based offset. Exposed because every
// caller that reports a position must use the same arithmetic.
void computeLineColumn(const std::string& text, size_t offset, size_t* line, size_t* column);

// ---------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------
class SequenceModel {
public:
    SequenceModel();

    // ---- identity ---------------------------------------------------------
    uint64_t serial() const { return serial_; }
    void     bumpSerial() { ++serial_; }

    // Attachment-check serial: changes ONLY when a monomer is added, removed or
    // replaced, i.e. when the set of available attachment points may change.
    // A pure layout change must NOT bump it: that is the executable form of
    // 'layout is a view'.
    uint64_t attachmentSerial() const { return attachmentSerial_; }
    void     bumpAttachmentSerial() { ++attachmentSerial_; }

    const MonomerLibrary& library() const { return library_; }
    MonomerLibrary&       library() { return library_; }
    void setLibrary(const MonomerLibrary& library) { library_ = library; }

    // ---- polymer access ---------------------------------------------------
    size_t polymerCount() const { return polymers_.size(); }
    const std::vector<SequencePolymer>& polymers() const { return polymers_; }
    std::vector<SequencePolymer>&       mutablePolymers() { return polymers_; }
    const SequencePolymer* polymer(size_t index) const {
        return index < polymers_.size() ? &polymers_[index] : 0;
    }
    SequencePolymer* mutablePolymer(size_t index) {
        return index < polymers_.size() ? &polymers_[index] : 0;
    }
    size_t polymerIndexById(const std::string& polymerId) const;

    // ---- connections ------------------------------------------------------
    size_t connectionCount() const { return connections_.size(); }
    const std::vector<SequenceConnection>& connections() const { return connections_; }
    std::vector<SequenceConnection>& mutableConnections() { return connections_; }
    const SequenceConnection* connection(size_t index) const {
        return index < connections_.size() ? &connections_[index] : 0;
    }

    // ---- layout (VIEW) ----------------------------------------------------
    const SequenceLayout& layout() const { return layout_; }
    SequenceLayout&       mutableLayout() { return layout_; }
    void setLayout(const SequenceLayout& layout);

    // ---- editing primitives (no undo; commands.hpp wraps them) ------------
    core::Error addPolymer(const std::string& polymerId, PolymerType type, size_t* outIndex);

    core::Error insertMonomer(size_t polymerIndex, size_t position,
                              const std::string& symbol,
                              MonomerInstanceId* outId);
    core::Error insertMonomerInstance(size_t polymerIndex, size_t position,
                                      const MonomerInstance& instance,
                                      MonomerInstanceId* outId);

    // Replace the monomer at `position`. The instance id is PRESERVED, so every
    // connection that referenced it still references the same slot. Connection
    // validity is NOT silently repaired: call checkConnections() afterwards.
    core::Error replaceMonomer(size_t polymerIndex, size_t position,
                               const std::string& symbol,
                               size_t* outRequiredConnections);

    // Delete the monomer at `position`: alive=false, slot retained, and all
    // connections touching it are killed. Does NOT join the neighbours, because
    // joining them would invent a bond the author did not draw.
    core::Error deleteMonomer(size_t polymerIndex, size_t position,
                              size_t* outKilledConnections);

    // Reverse the monomer ORDER of a polymer and flip every covalent connection
    // with it, so the chemistry is unchanged and only the reading direction
    // differs. This is the stage-1 'direction' requirement.
    core::Error reversePolymer(size_t polymerIndex);

    // ---- connection primitives --------------------------------------------
    core::Error connect(size_t fromPolymer, size_t fromPosition, const std::string& fromAttachment,
                        size_t toPolymer, size_t toPosition, const std::string& toAttachment,
                        ConnectionKind kind, ConnectionId* outId);
    core::Error disconnect(ConnectionId id);
    core::Error setConnectionAnnotation(ConnectionId id, const std::string& annotation);

    // ---- queries ----------------------------------------------------------
    // Is one specific attachment point of this monomer still unused by a live
    // covalent connection? The single-point form of the query above, used by
    // connect() so the two cannot disagree.
    bool isAttachmentFree(size_t polymerIndex, size_t position,
                          const std::string& attachment) const;
    std::vector<std::string> freeAttachments(size_t polymerIndex, size_t position) const;
    size_t connectionCountAt(size_t polymerIndex, size_t position) const;
    std::vector<std::string> usedAttachments(size_t polymerIndex, size_t position) const;

    size_t validate(std::vector<SequenceDiagnostic>* problems) const;
    size_t checkConnections(std::vector<SequenceDiagnostic>* problems) const;

    // ---- statistics -------------------------------------------------------
    struct Stats {
        size_t polymers;
        size_t monomers;
        size_t liveMonomers;
        size_t connections;
        size_t covalentConnections;
        size_t hydrogenConnections;
        size_t deadSlots;
        size_t unknownMonomers;
        size_t blobMonomers;
        Stats();
    };
    Stats stats() const;

    // A stable digest over chemistry ONLY (monomer definitions by stable id plus
    // live connections). The AUTHORING TEXT of a chemistry-defined monomer is NOT
    // mixed, because two spellings of one molecule (R(A)P and r.A.p) must digest
    // equal; the text IS mixed for an unknown/BLOB monomer, where it is the identity.
    // live connections). Layout is EXCLUDED on purpose: two models that differ
    // only in layout must digest equal.
    uint64_t chemistryDigest() const;
    uint64_t digest() const;

    void clear();

private:
    MonomerInstanceId allocateInstanceId();

    MonomerLibrary library_;
    std::vector<SequencePolymer> polymers_;
    std::vector<SequenceConnection> connections_;
    SequenceLayout layout_;
    uint64_t serial_;
    uint64_t attachmentSerial_;
    std::vector<MonomerInstance> idPool_;
};

} // namespace helm
} // namespace r62

#endif // R62_NEXT_HELM_SEQUENCE_HPP
