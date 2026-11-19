#ifndef R62_NEXT_HELM_MONOMER_HPP
#define R62_NEXT_HELM_MONOMER_HPP
// =============================================================================
// re_round62_viewer/next/helm/monomer.hpp
//
// T15 - BIOPOLYMER MONOMER LIBRARY.
//
// WHAT THIS IS
// -----------------------------------------------------------------------------
// A monomer is NOT a string of text and NOT a BioDraw picture. It is a record
// with an explicit chemistry definition and an explicit, finite set of
// attachment points. This header owns:
//
//   * PolymerType     - the bounded set of polymer types HELM can carry.
//   * AttachmentPoint - a label ("R1", "R2", ...) together with the capping
//                       group the official library records for that point.
//   * MonomerDefinition - symbol, polymer type, name, natural analog, SMILES
//                       and the ordered attachment points.
//   * MonomerLibrary  - an ordered, deterministic collection with provenance.
//
// HONESTY RULES (these are structural, not documentation)
// -----------------------------------------------------------------------------
// R1  An unknown monomer is NEVER matched to a structure by its name or symbol.
//     unknownMonomer() returns a definition whose chemistryDefined() is false
//     and whose smiles is empty; the original source text is carried verbatim in
//     MonomerInstance::sourceText so a save/reload keeps the author definition.
// R2  A BLOB monomer has NO atom structure. isBlob() is the single predicate,
//     and no code path may synthesise atoms for it.
// R3  A CHEM monomer has a chemistry definition only when the source provided
//     one. isChem() and chemistryDefined() are separate answers.
// R4  Attachment points come from the library record, never from the symbol
//     spelling. C has R1,R2,R3; A has R1,R2 - the table says so.
// R5  Every entry carries licence + source. MonomerLibrary::add() REFUSES an
//     entry with an empty licence or an empty source, so a library assembled at
//     run time cannot silently lose provenance.
//
// DATA PROVENANCE
// -----------------------------------------------------------------------------
// The built-in phase-1 table in monomer_library.cpp is generated from the
// official Pistoia Alliance HELM Core Library (MIT):
//     https://github.com/PistoiaHELM/HELMMonomerSets
//     HELMCoreLibrary/HELMCoreLibrary.json
//     sha256 9df9d7fbaa867260e537681bea48ddd8e8251b73536b8a5c4d999b870906aa63
// The generator is out/T15/research/gen_table.py; it reads BOTH spellings of the
// cap-group field ("capGroupSmiles" and "capGroupSMILES") because the official
// file is inconsistent between its peptide and nucleotide records.
//
// WHAT THIS MODULE DOES NOT DO
// -----------------------------------------------------------------------------
//   * It does not manage remote/shared libraries, accounts or cloud data.
//   * It does not download anything at run time.
//   * It does not ship the 705-entry library; phase 1 ships the fixed 30-entry
//     natural set listed in out/T15/research/EVIDENCE.md.
// =============================================================================

#include "next/core/contracts.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace r62 {
namespace helm {

// ---------------------------------------------------------------------------
// Version handshake
// ---------------------------------------------------------------------------
enum {
    kHelmMajor    = 1,
    kHelmMinor    = 0,
    kHelmRevision = 0,
    kHelmVersion  = kHelmMajor * 10000 + kHelmMinor * 100 + kHelmRevision
};

inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

inline bool requestHelmCompatibility(int major, int minor) {
    return major == kHelmMajor && minor <= kHelmMinor;
}

// ---------------------------------------------------------------------------
// STAGED DATA VERSIONS
// ---------------------------------------------------------------------------
// Each supported stage has its OWN data version, because a document written by a
// later stage must be readable (or explicitly refused) by an earlier reader.
enum {
    // Stage 1: editable natural peptide + basic RNA/DNA, explicit monomer ids,
    // attachment points, insert/replace/delete, direction, structure/cartoon
    // display toggle.
    kStage1DataVersion = 1,
    // Stage 2: defined CHEM/BLOB, cross-chain connections, complementary
    // strands (pair), and hairpin layout.
    kStage2DataVersion = 1,
    kLatestStage = 2
};

const char* stageName(int stage);
bool stageHasVersion(int stage, int version);

// ---------------------------------------------------------------------------
// Polymer types
// ---------------------------------------------------------------------------
// The ChemDraw User Guide (PDF page 168-170) lists exactly AA(peptide), RNA,
// CHEM and BLOB as the monomer types the toolbar offers; HELM spells the
// peptide type "PEPTIDE". Both spellings are accepted on input and "PEPTIDE" is
// what this module writes.
enum class PolymerType : uint8_t {
    Unknown = 0,
    Peptide = 1,   // HELM token PEPTIDE, guide token AA
    Rna     = 2,   // HELM token RNA, guide token RNA (covers DNA via d)
    Chem    = 3,   // HELM token CHEM, guide token CHEM
    Blob    = 4    // HELM token BLOB, guide token BLOB
};

const char* polymerTypeToken(PolymerType t);
const char* polymerTypeName(PolymerType t);
bool polymerTypeFromToken(const std::string& token, PolymerType* out);
// The lowercase stable token polymerTypeName() writes, and the HELM token, are
// both accepted. Two functions rather than one because a caller that persists must
// be able to read back exactly what it wrote.
bool polymerTypeFromName(const std::string& name, PolymerType* out);
bool polymerTypeFromId(const std::string& polymerId, PolymerType* out);
bool polymerTypeIsNucleic(PolymerType t);

// ---------------------------------------------------------------------------
// Monomer role inside a nucleotide
// ---------------------------------------------------------------------------
// ChemDraw lays an RNA residue down as THREE monomers - ribose, phosphate and
// base (guide PDF page 181). The role is stored, not guessed; Backbone is the
// safe default for every peptide.
enum class MonomerRole : uint8_t {
    Unknown  = 0,
    Backbone = 1,   // peptide residue, or RNA sugar/phosphate
    Branch   = 2    // nucleobase or other side group
};

const char* monomerRoleName(MonomerRole r);
bool monomerRoleFromName(const std::string& name, MonomerRole* out);

// ---------------------------------------------------------------------------
// Attachment points
// ---------------------------------------------------------------------------
struct AttachmentPoint {
    std::string label;         // "R1" | "R2" | ... ; never empty in a valid record
    std::string capGroupName;  // "" == the library stated none
    std::string capGroupSmiles;
    AttachmentPoint() {}
    AttachmentPoint(const std::string& l, const std::string& name, const std::string& smi)
        : label(l), capGroupName(name), capGroupSmiles(smi) {}
    bool operator==(const AttachmentPoint& o) const {
        return label == o.label && capGroupName == o.capGroupName &&
               capGroupSmiles == o.capGroupSmiles;
    }
    bool operator!=(const AttachmentPoint& o) const { return !(*this == o); }
};

// Validate an attachment label exactly as the Pistoia reference implementation
// does: the literal "pair", or R followed by one or more digits.
// (MonomerParser.validateAttachmentLabel)
bool isValidAttachmentLabel(const std::string& label);

// The stricter HELM 2 rule (StateMachineParser.checkDetailsConnections): an
// R-group is R[1-9][0-9]* or '?'. R0 is syntactically a label for the HELM 1
// toolkit but is REFUSED by the HELM 2 regex, so the two are separate predicates
// and the HELM reader uses this one.
bool isValidHelm2AttachmentLabel(const std::string& label);
bool parseAttachmentLabel(const std::string& label, int* outNumber);
std::string formatAttachmentLabel(int number);

extern const char* const kAttachmentR1;
extern const char* const kAttachmentR2;
extern const char* const kAttachmentR3;
extern const char* const kAttachmentPair;

// ---------------------------------------------------------------------------
// Monomer definition
// ---------------------------------------------------------------------------
enum { kMaxAttachmentPoints = 8 };

struct MonomerDefinition {
    std::string  symbol;
    PolymerType  polymerType;
    MonomerRole  role;
    std::string  name;
    std::string  naturalAnalog;
    std::string  smiles;
    bool         natural;
    bool         chemistryDefined;
    std::vector<AttachmentPoint> attachments;

    // --- provenance (mandatory for a library entry; see R5) ----------------
    std::string  license;
    std::string  source;
    std::string  sourceRecordId;

    MonomerDefinition()
        : polymerType(PolymerType::Unknown), role(MonomerRole::Unknown),
          natural(false), chemistryDefined(false) {}

    const AttachmentPoint* attachment(const std::string& label) const;
    bool hasAttachment(const std::string& label) const { return attachment(label) != 0; }
    size_t attachmentCount() const { return attachments.size(); }

    bool isBlob() const { return polymerType == PolymerType::Blob; }
    bool isChem() const { return polymerType == PolymerType::Chem; }
    bool canExpandToAtoms() const;

    bool operator==(const MonomerDefinition& o) const;
    bool operator!=(const MonomerDefinition& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
// A monomer definition is identified by (polymerType, symbol). That pair, not
// the symbol alone, is the key: the official library contains BOTH a PEPTIDE P
// (Proline) and an RNA p (Phosphate), and T is Threonine in a peptide and
// Thymine in RNA. A global symbol table would be wrong.
struct MonomerKey {
    PolymerType polymerType;
    std::string symbol;
    MonomerKey() : polymerType(PolymerType::Unknown) {}
    MonomerKey(PolymerType t, const std::string& s) : polymerType(t), symbol(s) {}
    bool operator<(const MonomerKey& o) const {
        return polymerType != o.polymerType ? polymerType < o.polymerType : symbol < o.symbol;
    }
    bool operator==(const MonomerKey& o) const {
        return polymerType == o.polymerType && symbol == o.symbol;
    }
};

struct LibraryProvenance {
    std::string license;
    std::string source;
    std::string retrievedUtc;
    std::string note;
    LibraryProvenance() {}
};

// A per-entry problem found by MonomerLibrary::validate().
struct LibraryProblem {
    std::string symbol;
    std::string reason;
    LibraryProblem() {}
    LibraryProblem(const std::string& s, const std::string& r) : symbol(s), reason(r) {}
};

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------
class MonomerLibrary {
public:
    MonomerLibrary() {}

    // Insert or replace. REFUSES (returns false and changes nothing) when:
    //   * symbol is empty,
    //   * polymerType is Unknown,
    //   * a duplicate label exists among the attachments,
    //   * an attachment label is not pair or R<n>,
    //   * licence is empty, or source is empty (R5).
    bool add(const MonomerDefinition& definition, std::string* reasonIfRefused = 0);

    void setLibraryProvenance(const LibraryProvenance& p) { provenance_ = p; }
    const LibraryProvenance& libraryProvenance() const { return provenance_; }

    const MonomerDefinition* find(PolymerType t, const std::string& symbol) const;

    // Resolve a symbol that the NOTATION writes with different capitalisation than
    // the library stores. This is a measured fact, not a convenience: the official
    // Core Library stores the ribose as lowercase 'r' and the phosphate as lowercase
    // 'p', while HELM notation strings write 'R' and 'P' inside an RNA polymer
    // (ChemDraw guide PDF pages 180-182 and the Pistoia test corpus both use the
    // uppercase form). The lookup is EXACT first; only when there is no exact match
    // is a case-insensitive match accepted, and only when it is UNIQUE. An ambiguous
    // case fold returns 0, because guessing between two monomers is exactly the
    // silent substitution this module refuses.
    const MonomerDefinition* findCaseTolerant(PolymerType t, const std::string& symbol) const;
    const MonomerDefinition* find(const MonomerKey& key) const { return find(key.polymerType, key.symbol); }
    bool contains(PolymerType t, const std::string& symbol) const { return find(t, symbol) != 0; }

    size_t size() const { return entries_.size(); }
    const std::vector<MonomerDefinition>& entries() const { return entries_; }

    std::vector<const MonomerDefinition*> byPolymerType(PolymerType t) const;

    // Deterministic: exact symbol hits first, then symbol prefix, then name
    // prefix, then the rest; each group in insertion order.
    std::vector<const MonomerDefinition*> search(const std::string& query, PolymerType filter) const;

    // Symbols whose naturalAnalog equals `analog` for one polymer type.
    std::vector<const MonomerDefinition*> byNaturalAnalog(PolymerType t, const std::string& analog) const;

    // Returns the number of problems and fills `problems` when non-null.
    size_t validate(std::vector<LibraryProblem>* problems) const;

    void clear() { entries_.clear(); index_.clear(); }

private:
    std::vector<MonomerDefinition> entries_;
    std::map<MonomerKey, size_t> index_;
    LibraryProvenance provenance_;
};

// The library every parser starts from. Constructed once per call (no global
// mutable state).
MonomerLibrary builtinNaturalLibrary();

extern const char* const kPistoiaCoreLicense;
extern const char* const kPistoiaCoreSource;
extern const char* const kPistoiaCoreRetrieved;

// A definition for a monomer we do NOT know. chemistryDefined() is false and
// smiles is empty; `symbol` is kept verbatim.
MonomerDefinition unknownMonomer(PolymerType t, const std::string& symbol);

// A definition for a BLOB. Never expandable.
MonomerDefinition blobMonomer(const std::string& symbol);

// A definition for a CHEM monomer with a caller-supplied definition. The SMILES
// string is stored verbatim; this module does not parse it.
MonomerDefinition chemMonomer(const std::string& symbol, const std::string& smiles);

// Does a monomer with `available` connections satisfy a position that needs
// `required`? The ChemDraw guide states the rule for REPLACEMENT (PDF page 183):
// "To replace a monomer with N number of connections, the replacing monomer
// should have M number of connections, where N <= M." This answers exactly that
// comparison; it does not decide which connections those are.
bool connectionsSatisfy(size_t required, size_t available);

} // namespace helm
} // namespace r62

#endif // R62_NEXT_HELM_MONOMER_HPP
