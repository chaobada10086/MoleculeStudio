#ifndef R62_NEXT_HELM_DOCUMENT_MAP_HPP
#define R62_NEXT_HELM_DOCUMENT_MAP_HPP
// =============================================================================
// re_round62_viewer/next/helm/document_map.hpp
//
// T15 - MAPPING A SequenceModel ONTO A r62::core::Document.
//
// WHY THIS FILE EXISTS
// -----------------------------------------------------------------------------
// The task requires that a HELM sequence is NOT treated as an uneditable string or
// a BioDraw picture. That is achieved by mapping every monomer onto a T01 document
// object so the ordinary T01/T02/T03/T04 machinery - commands, transactions, undo,
// persistence, hit testing, rendering - works on it unchanged.
//
// THE ID CONTRACT (this is the load-bearing decision)
// -----------------------------------------------------------------------------
//   MonomerInstanceId  ==  core::ObjectId  of the mapped node.
//
// The adapter RESERVES T01 slots until the atom slab index equals the monomer id,
// so the two number spaces are identical by construction rather than by a lookup
// table that could drift. Consequences:
//   * a connection's stable reference to a monomer is equally a stable reference
//     to a document object;
//   * moving, replacing or reordering a monomer cannot invalidate a reference;
//   * deletion is alive=false in BOTH models, and neither reuses the slot.
//
// WHAT EACH MONOMER BECOMES
// -----------------------------------------------------------------------------
//  * A CHEMISTRY-DEFINED monomer becomes a node whose element is the node's
//    DEFAULT element, with the monomer symbol preserved as an atom LABEL override
//    and as extension entries. It does NOT become its full atom structure here:
//    expanding a monomer to atoms is a separate, explicit operation, because the
//    guide treats Expand Label as its own command (PDF page 191-192).
//  * An UNKNOWN monomer becomes a node with the label preserved and NO chemical
//    claim. The stored element is carbon, which is what a document must hold, but
//    the extension entry records that the chemistry is unknown. Nothing infers a
//    structure from the symbol.
//  * A BLOB monomer becomes a node with a label and a BLOB extension entry, and is
//    never expanded.
//  * Every COVALENT sequence connection becomes a core::BondRecord whose order is
//    Single and whose semantics is Normal. A HYDROGEN connection becomes a bond
//    with BondSemantics::Hydrogen, which is the axis the T01 model reserves for
//    exactly this, and display Dashed. So a base pair is a real document bond and
//    Undo/Redo/Persistence act on it with no special case.
//
// WHERE THE SEQUENCE SEMANTICS LIVE IN THE DOCUMENT
// -----------------------------------------------------------------------------
// T01's extension container is the documented home for module-private data, and
// the T13 module already established the pattern of a 'user.<module>.' prefix.
// T15 therefore writes:
//   document bag : user.t15.sequence.header     a small versioned header
//   monomer bag  : user.t15.monomer.record      the MonomerInstance encoding
//   bond bag     : user.t15.connection.record   the connection id + kind
//
// HONEST LIMIT, stated once and enforced by a test: the NATIVE T01 format
// (.t01doc) writes every extension entry verbatim, so save/reload preserves the
// sequence exactly. The CDXML adapter in next/core does NOT know these keys, so a
// CDXML round trip is NOT claimed to preserve sequence semantics. RESULTS.md says
// this instead of implying otherwise.
//
// WHAT THIS ADAPTER DOES NOT DO
// -----------------------------------------------------------------------------
//  * It does not create paths, arrows, brackets or glyphs. Text labels are T09's
//    job and this module only records the LABEL STRING on the node.
//  * It does not implement Expand/Contract Label. Those are separate commands and
//    are listed as not implemented in coverage.csv.
// =============================================================================

#include "next/helm/sequence.hpp"
#include "next/core/document.hpp"

#include <map>
#include <string>
#include <vector>

namespace r62 {
namespace helm {

// ----------------------------------------------------------------------------
// Extension keys
// ----------------------------------------------------------------------------
extern const char* const kKeyDocumentHeader;   // "user.t15.sequence.header"
extern const char* const kKeyMonomerRecord;    // "user.t15.monomer.record"
extern const char* const kKeyConnectionRecord; // "user.t15.connection.record"
extern const char* const kKeyPolymerRecord;    // "user.t15.polymer.record"

// The stage data version this build writes, and the newest it can read. A document
// written by a NEWER stage is refused rather than partially interpreted.
enum { kDocumentMapVersion = 1 };

// ----------------------------------------------------------------------------
// Encoding
// ----------------------------------------------------------------------------
// A monomer record, as stored in the atom's extension bag. It is a single-line
// 'key=value;' list so a corrupt record is detectable and unknown keys are
// forward compatible, which is the same choice T13 made.
//
//   v=<mapVersion>;pv=<stageDataVersion>;t=<polymerType>;r=<role>;s=<symbolHex>;
//   a=<naturalAnalogHex>;n=<nameHex>;c=<copyIndex>;st=<sourceTextHex>;
//   k=<known0|1>;sm=<smilesHex>;li=<licenseHex>;so=<sourceHex>;
//   at=<labelHex>,<capNameHex>,<capSmilesHex>|<labelHex>,...
// A monomer record carries the polymer ORDER and the POSITION inside that polymer.
// Without them a reader would have to infer a position from the GLOBAL atom id,
// which is only correct for the first polymer: the second polymer's monomers start
// at a high id and would be read as a huge run of empty slots. The order is a small
// dense 0-based index into the polymer list, not the source object id.
std::string encodeMonomerRecord(const MonomerInstance& instance, size_t polymerOrder,
                                size_t position);
std::string encodeMonomerRecord(const MonomerInstance& instance);
bool decodeMonomerRecord(const std::string& text, MonomerInstance* out, std::string* reason);
// The order/position form. `polymerOrder` and `position` are set to kInvalidOrder
// when the record does not carry them (a record written by the 3-argument encoder).
bool decodeMonomerRecord(const std::string& text, MonomerInstance* out,
                         size_t* polymerOrder, size_t* position, std::string* reason);
static const size_t kInvalidOrder = static_cast<size_t>(-1);

// A connection record: v=<mapVersion>;id=<connectionId>;k=<kind>;
// fp=<fromPolymer>;fi=<fromPosition>;fa=<fromAttachmentHex>;tp=..;ti=..;ta=..;
// an=<annotationHex>
std::string encodeConnectionRecord(const SequenceConnection& connection);
bool decodeConnectionRecord(const std::string& text, SequenceConnection* out, std::string* reason);

// A polymer record, stored on the FIRST monomer of the polymer (and on the
// document when the polymer is empty): v=..;id=<polymerIdHex>;t=<polymerType>;
// d=<display>;st=<strandTypeHex>
std::string encodePolymerRecord(const SequencePolymer& polymer, size_t polymerOrder);
std::string encodePolymerRecord(const SequencePolymer& polymer);
bool decodePolymerRecord(const std::string& text, SequencePolymer* out, std::string* reason);
bool decodePolymerRecord(const std::string& text, SequencePolymer* out, size_t* polymerOrder,
                         std::string* reason);

// ----------------------------------------------------------------------------
// Mapping
// ----------------------------------------------------------------------------
struct MapReport {
    size_t atomsCreated;
    size_t bondsCreated;
    size_t polymersCreated;
    size_t unknownMonomers;
    size_t blobMonomers;
    size_t hydrogenBonds;
    std::vector<SequenceDiagnostic> warnings;
    MapReport();
};

// Write `model` into `document`. The document is RESET first (a fresh Document is
// produced by the caller in practice). On failure the document is left in the
// state it had before the call, because a scratch document is built and moved in.
core::Error mapToDocument(const SequenceModel& model, core::Document& document,
                          MapReport* report);

// Read a sequence back out of a document. Only objects carrying T15 records are
// considered; anything else is ignored, so a document that mixes a sequence with
// ordinary chemistry reads correctly and the ordinary chemistry is left alone.
//
// When a monomer's record is missing but the atom carries a T15 label override,
// the monomer is reconstructed as an UNKNOWN monomer rather than guessed.
core::Error mapFromDocument(const core::Document& document, SequenceModel& model,
                            MapReport* report);

// The atom LABEL a monomer maps to. This is the string T09 measures and draws.
std::string labelForMonomer(const MonomerDefinition& definition);

// The element a monomer node is stored with. It is the document's placeholder for
// 'a labelled node', NOT a claim about the monomer's chemistry: the chemical
// identity is in the extension record and, when known, in the SMILES.
int elementForMonomer(const MonomerDefinition& definition);

// The inverse of the id contract, stated as a function so a caller never has to
// assume it: the ObjectId a monomer maps to.
inline core::ObjectId objectIdOf(MonomerInstanceId id) {
    return static_cast<core::ObjectId>(id);
}
inline MonomerInstanceId monomerIdOf(core::ObjectId id) {
    return static_cast<MonomerInstanceId>(id);
}

// Does this document carry a T15 sequence at all? Used by an integrator to decide
// whether the sequence commands should be offered.
bool documentHasSequence(const core::Document& document);

// The header text recorded at map time (the FASTA-style annotation or the polymer
// list). Empty when none was recorded.
std::string documentSequenceHeader(const core::Document& document);

} // namespace helm
} // namespace r62

#endif // R62_NEXT_HELM_DOCUMENT_MAP_HPP
