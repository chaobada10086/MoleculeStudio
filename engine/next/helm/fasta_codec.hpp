#ifndef R62_NEXT_HELM_FASTA_CODEC_HPP
#define R62_NEXT_HELM_FASTA_CODEC_HPP
// =============================================================================
// re_round62_viewer/next/helm/fasta_codec.hpp
//
// T15 - FASTA READER AND WRITER FOR NATURAL SEQUENCES.
//
// WHAT THE GUIDE ACTUALLY SAYS (ChemDraw User Guide, PDF reader pages 186-193)
// -----------------------------------------------------------------------------
//  * Copy As > FASTA emits a string of NATURAL ANALOGS from a PEPTIDE or RNA
//    HELM sequence.
//  * It supports ONE peptide or RNA sequence. Multiple sequences, and CHEM and
//    BLOB sequences, are NOT supported.
//  * A peptide monomer whose natural analog is unknown is written as 'X'.
//    An RNA monomer whose natural analog is unknown is written as 'N'.
//  * For RNA only the BASE is emitted.
//  * Pasting FASTA requires the user to state which of Peptide / RNA / DNA it is.
//    A U pasted as DNA becomes T, and a T pasted as RNA becomes U.
//  * A FASTA definition line (the '>' header) is displayed below the pasted
//    sequence, so the header is carried, not discarded.
//
// CONSEQUENCE FOR THIS MODULE
// -----------------------------------------------------------------------------
// FASTA is a NATURAL-ANALOG view of the sequence, not the sequence itself. It is
// therefore LOSSY by construction and this module says so in the API rather than
// in a paragraph:
//
//   * writeFasta() returns a report counting how many residues could NOT be
//     represented by a natural analog (each became X or N).
//   * writeFasta() REFUSES (returns an error) when the model holds a CHEM or BLOB
//     polymer, or more than one polymer, because the guide says that case is not
//     supported. It does not silently emit a partial string.
//   * readFasta() takes the polymer type EXPLICITLY, exactly as the paste dialog
//     does, and applies the documented U/T substitution in the direction stated.
//   * A residue whose letter has no monomer in the library becomes an UNKNOWN
//     monomer carrying the letter verbatim: FASTA cannot say more than that.
// =============================================================================

#include "next/helm/sequence.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace helm {

// The letter used when a residue has no natural analog (guide PDF pages 187-188).
extern const char kUnknownPeptideAnalog;   // 'X'
extern const char kUnknownNucleicAnalog;   // 'N'

// ----------------------------------------------------------------------------
// Reading
// ----------------------------------------------------------------------------
enum class FastaKind : uint8_t {
    Peptide = 0,
    Rna     = 1,
    Dna     = 2
};
const char* fastaKindName(FastaKind k);
bool fastaKindFromName(const std::string& n, FastaKind* out);

struct FastaReadOptions {
    // The guide states that pasting as RNA substitutes a DNA T with U, and
    // pasting as DNA substitutes an RNA U with T (PDF pages 192-193). Turning
    // this off reads the letters literally, which is useful for a round-trip
    // test that must not fold the two letter sets together.
    bool applyNuSubstitution;
    // Hard ceiling so a hostile file cannot exhaust memory.
    size_t maxResidues;
    FastaReadOptions() : applyNuSubstitution(true), maxResidues(1000000) {}
};

struct FastaRecord {
    std::string header;    // everything after '>' on the definition line
    std::string sequence;  // letters only, whitespace removed
    size_t      offset;    // byte offset of the record in the input
    FastaRecord() : offset(0) {}
};

struct FastaReadReport {
    size_t records;
    size_t residues;
    size_t substitutedResidues;   // U<->T changes applied
    size_t unknownResidues;       // letters with no library monomer
    std::vector<SequenceDiagnostic> warnings;
    FastaReadReport();
};

// Split FASTA text into records WITHOUT interpreting them. Exposed because a
// caller may want the header lines even when it will not build a model.
core::Error splitFasta(const std::string& text, std::vector<FastaRecord>* records,
                       std::vector<SequenceDiagnostic>* errors);

// Build a model from FASTA text. `kind` says which monomer set to use; this is
// the API form of the guide's paste dialog. `polymerId` may be empty, in which
// case "PEPTIDE1" / "RNA1" is used.
core::Error readFasta(const std::string& text, FastaKind kind, SequenceModel& model,
                      FastaReadReport* report,
                      std::vector<SequenceDiagnostic>* errors,
                      const std::string& polymerId = std::string(),
                      const FastaReadOptions& options = FastaReadOptions());

// ----------------------------------------------------------------------------
// Writing
// ----------------------------------------------------------------------------
struct FastaWriteOptions {
    // Emit the '">"' definition line. When the model carries no name, a stable
    // placeholder is written so the record shape is still valid FASTA.
    bool writeHeader;
    // Characters per output line; 0 means a single line.
    size_t lineWidth;
    FastaWriteOptions() : writeHeader(true), lineWidth(60) {}
};

struct FastaWriteReport {
    size_t residues;
    size_t unknownAnalogs;     // written as X or N
    size_t polymersConsidered;
    bool   peptideKind;
    FastaWriteReport();
};

// Write the natural-analog sequence of a SINGLE peptide or RNA polymer.
// REFUSES with Unsupported when the model holds a CHEM or BLOB polymer, holds
// more than one polymer, or holds no polymer at all. That refusal is the guide's
// own limitation, made executable.
core::Error writeFasta(const SequenceModel& model, std::string* out,
                       FastaWriteReport* report,
                       const FastaWriteOptions& options = FastaWriteOptions(),
                       const std::string& header = std::string());

// The natural analog of one monomer, as FASTA would print it. Returns the letter,
// or the unknown placeholder, and sets `unknown`.
char fastaAnalogFor(const MonomerDefinition& definition, bool* unknown);

} // namespace helm
} // namespace r62

#endif // R62_NEXT_HELM_FASTA_CODEC_HPP
