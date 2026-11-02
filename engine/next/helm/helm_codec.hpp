#ifndef R62_NEXT_HELM_HELM_CODEC_HPP
#define R62_NEXT_HELM_HELM_CODEC_HPP
// =============================================================================
// re_round62_viewer/next/helm/helm_codec.hpp
//
// T15 - HELM NOTATION READER AND WRITER.
//
// GRAMMAR (from the Pistoia reference implementations; see EVIDENCE.md)
// -----------------------------------------------------------------------------
// The HELM string has FOUR top-level sections separated by '$', and the canonical
// terminator is the literal four characters $$$$ :
//
//    POLYMER $ CONNECTION $ GROUPING $ ANNOTATION $$$$
//
// Polymer section:
//    <POLYMER_ID><n>{<monomer>[.<monomer>...]}"<annotation>"
//    * the polymer id must start with a letter and end with digits
//      (PolymerNode.java), and its letter prefix names the type.
//    * a monomer is a symbol, optionally followed by a quoted annotation.
//    * a CHEM monomer may instead be an inline definition: [ ... ]
//
// Connection section, comma separated; a '-' separates source from target and a
// '+' concatenates several polymers on one side (EdgeParser.java javadoc):
//    SourcePolymer[+SourcePolymer...],TargetPolymer[+...],Source-Target
//      standard connection : <position>:<RGroupID>
//      hydrogen bond       : <position>:pair
//      generic             : generic:
//
// Grouping section: G<k>(<polymer>:<range>[+<polymer>:<range>...])<annotation>
//    A group is preserved but stage 1/2 do not create new ones.
//
// Annotation section: <POLYMER_ID>{<key>:<value>}...  e.g. RNA1{StrandType:ss}
//    The guide (PDF page 185) records the strand-type annotation with the
//    keyword 'strandtype' and the values 'ss' (sense) and 'as' (antisense).
//
// VERSION SUFFIX
// -----------------------------------------------------------------------------
// A trailing V2.0 marks the HELM 2 notation. Both V1 (no suffix) and V2 are
// accepted on input; the writer emits V2.0 because that is what current ChemDraw
// writes and what its own examples show.
//
// WHAT IS REFUSED, AND WHY
// -----------------------------------------------------------------------------
// Every refusal carries a byte OFFSET into the input, converted to line/column by
// the shared helper, so a caller can point at the character. The parser refuses:
//   * an unknown polymer type token,
//   * a malformed polymer id,
//   * unmatched braces or quotes,
//   * an attachment label that is neither 'pair' nor R<n>,
//   * a connection that names a polymer or position that does not exist,
//   * a connection that names an attachment point the monomer does not define
//     (UNLESS the monomer is a BLOB, which by definition has no points),
//   * a second section that is non-empty but unparseable.
// It does NOT refuse an unknown MONOMER SYMBOL: that is preserved verbatim with
// no invented structure, which is the required behaviour.
// =============================================================================

#include "next/helm/sequence.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace helm {

// ----------------------------------------------------------------------------
// Options and report
// ----------------------------------------------------------------------------
struct HelmParseOptions {
    // When true (default) a connection whose monomer does not define the named
    // attachment point is a hard error. When false it is recorded as a warning
    // and the connection is still created, which is what a lossy import of a
    // third-party file may need.
    bool strictAttachments;
    // When true (default) the parser requires the four-section terminator.
    // A simple-notation string with no '$' at all is still accepted either way,
    // because HELM defines that form.
    bool requireFourSections;
    // Maximum number of monomers over all polymers. A hostile file must not be
    // able to exhaust memory.
    size_t maxMonomers;
    HelmParseOptions()
        : strictAttachments(true), requireFourSections(true), maxMonomers(200000) {}
};

struct HelmParseReport {
    int    detectedVersion;      // 1 or 2; 2 when a V2.0 suffix was present
    bool   hadFourSections;      // the $$$$ terminator was present
    size_t polymersRead;
    size_t monomersRead;
    size_t connectionsRead;
    size_t hydrogenConnections;
    size_t groupsRead;
    size_t annotationsRead;
    size_t unknownMonomers;      // symbols the library did not define
    size_t chemMonomers;
    size_t blobMonomers;
    std::vector<SequenceDiagnostic> warnings;
    HelmParseReport();
};

// ----------------------------------------------------------------------------
// Parse / write
// ----------------------------------------------------------------------------
// Parse `text` into `model`. On FAILURE `model` is left EXACTLY as it was: the
// parser builds into a scratch model and only moves it in on success. That is
// the all-or-nothing rule the rest of the project uses.
core::Error parseHelm(const std::string& text,
                      SequenceModel& model,
                      HelmParseReport* report,
                      const HelmParseOptions& options = HelmParseOptions());

// Diagnostics form: on failure the caller gets the positioned error(s).
core::Error parseHelmDetailed(const std::string& text,
                              SequenceModel& model,
                              HelmParseReport* report,
                              std::vector<SequenceDiagnostic>* errors,
                              const HelmParseOptions& options = HelmParseOptions());

// Serialize. The output is canonical: polymers in model order, connections in
// model order, and the four-section terminator plus the V2.0 suffix.
std::string writeHelm(const SequenceModel& model);

// Serialize only the SIMPLE form (no connections, no groups, no annotations),
// which is what HELM calls simple notation. Refuses by returning an empty string
// when the model cannot be expressed that way, rather than emitting a string that
// would silently drop connections.
std::string writeHelmSimple(const SequenceModel& model);
bool helmIsSimple(const SequenceModel& model);

// ----------------------------------------------------------------------------
// Canonicalization helpers exposed for tests
// ----------------------------------------------------------------------------
// Render one monomer token exactly as the writer would: the symbol, or an inline
// CHEM definition, with the annotation appended when present.
std::string formatMonomerToken(const MonomerInstance& instance);
// Render one connection: A,B,3:R2-5:R1 plus its quoted annotation.
std::string formatConnection(const SequenceModel& model, const SequenceConnection& conn);

// The literal terminator the writer emits.
extern const char* const kHelmTerminator;     // "$$$$"
extern const char* const kHelmVersionSuffix;  // "V2.0"

} // namespace helm
} // namespace r62

#endif // R62_NEXT_HELM_HELM_CODEC_HPP
