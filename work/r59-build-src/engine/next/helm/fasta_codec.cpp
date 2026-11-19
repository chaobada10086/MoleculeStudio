// =============================================================================
// re_round62_viewer/next/helm/fasta_codec.cpp
//
// T15 - FASTA implementation. The guide references are in fasta_codec.hpp.
// =============================================================================

#include "next/helm/fasta_codec.hpp"

#include <cctype>
#include <cstdio>

namespace r62 {
namespace helm {

const char kUnknownPeptideAnalog = 'X';
const char kUnknownNucleicAnalog = 'N';

FastaReadReport::FastaReadReport()
    : records(0), residues(0), substitutedResidues(0), unknownResidues(0) {}

FastaWriteReport::FastaWriteReport()
    : residues(0), unknownAnalogs(0), polymersConsidered(0), peptideKind(true) {}

const char* fastaKindName(FastaKind k) {
    switch (k) {
        case FastaKind::Rna: return "rna";
        case FastaKind::Dna: return "dna";
        default: return "peptide";
    }
}

bool fastaKindFromName(const std::string& n, FastaKind* out) {
    if (!out) return false;
    if (n == "peptide" || n == "PEPTIDE") { *out = FastaKind::Peptide; return true; }
    if (n == "rna" || n == "RNA")         { *out = FastaKind::Rna;     return true; }
    if (n == "dna" || n == "DNA")         { *out = FastaKind::Dna;     return true; }
    return false;
}

namespace {

bool isSpaceF(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

bool isLetterF(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

} // namespace

// ----------------------------------------------------------------------------
// splitFasta
// ----------------------------------------------------------------------------
core::Error splitFasta(const std::string& text, std::vector<FastaRecord>* records,
                       std::vector<SequenceDiagnostic>* errors) {
    records->clear();
    size_t i = 0;
    FastaRecord current;
    bool haveCurrent = false;
    while (i < text.size()) {
        const size_t lineStart = i;
        size_t lineEnd = text.find('\n', i);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        std::string line = text.substr(lineStart, lineEnd - lineStart);
        while (!line.empty() && (line[line.size() - 1] == '\r')) line.pop_back();
        const bool isHeader = !line.empty() && line[0] == '>';
        if (isHeader) {
            if (haveCurrent) records->push_back(current);
            current = FastaRecord();
            current.header = line.substr(1);
            current.offset = lineStart;
            haveCurrent = true;
        } else {
            for (size_t k = 0; k < line.size(); ++k) {
                const char c = line[k];
                if (isSpaceF(c)) continue;
                if (!isLetterF(c) && c != '*' && c != '-') {
                    if (errors) {
                        SequenceDiagnostic d(core::ErrorCode::ParseError,
                            std::string("non-letter character in a FASTA sequence: '") + c + "'",
                            lineStart + k);
                        d.line = 0; d.column = 0;
                        errors->push_back(d);
                    }
                    return core::Error(core::ErrorCode::ParseError,
                                       "non-letter character in a FASTA sequence");
                }
                if (c == '*' || c == '-') continue;   // stop codon / gap: not a residue
                if (!haveCurrent) {
                    // A bare sequence with no header is legal FASTA text and is a
                    // single unnamed record.
                    current = FastaRecord();
                    current.offset = lineStart;
                    haveCurrent = true;
                }
                current.sequence.push_back(static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c))));
            }
        }
        i = lineEnd + 1;
    }
    if (haveCurrent) records->push_back(current);
    return core::Error();
}

// ----------------------------------------------------------------------------
// Analog lookup
// ----------------------------------------------------------------------------
char fastaAnalogFor(const MonomerDefinition& definition, bool* unknown) {
    if (unknown) *unknown = false;
    // Only a monomer with an EXPLICIT natural analog can be written. A letter
    // derived from the symbol would be a guess, which is exactly what R1 forbids.
    const std::string& analog = definition.naturalAnalog;
    if (analog.size() == 1 && isLetterF(analog[0])) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(analog[0])));
        // The official library records 'X' for pyrrolysine and for 'any residue'.
        // Writing X is therefore faithful, not a fallback.
        if (unknown && c == 'X') *unknown = true;
        return c;
    }
    if (unknown) *unknown = true;
    if (definition.polymerType == PolymerType::Peptide) return kUnknownPeptideAnalog;
    return kUnknownNucleicAnalog;
}

// ----------------------------------------------------------------------------
// Reading
// ----------------------------------------------------------------------------
core::Error readFasta(const std::string& text, FastaKind kind, SequenceModel& model,
                      FastaReadReport* report, std::vector<SequenceDiagnostic>* errors,
                      const std::string& polymerId, const FastaReadOptions& options) {
    std::vector<FastaRecord> records;
    std::vector<SequenceDiagnostic> localErrors;
    core::Error split = splitFasta(text, &records, &localErrors);
    if (!split.ok()) {
        if (errors) *errors = localErrors;
        return split;
    }
    if (records.empty()) {
        SequenceDiagnostic d(core::ErrorCode::ParseError, "FASTA input contains no sequence", 0);
        if (errors) errors->push_back(d);
        return core::Error(core::ErrorCode::ParseError, "FASTA input contains no sequence");
    }
    // The guide supports a SINGLE sequence for paste. Multiple records are read,
    // but the first is placed and the rest are reported, so nothing is dropped
    // without a diagnostic.
    FastaReadReport local;
    local.records = records.size();

    const bool nucleic = (kind != FastaKind::Peptide);
    const PolymerType type = nucleic ? PolymerType::Rna : PolymerType::Peptide;
    std::string id = polymerId;
    if (id.empty()) id = nucleic ? "RNA1" : "PEPTIDE1";

    SequenceModel scratch;
    scratch.setLibrary(model.library());
    size_t polymerIndex = 0;
    core::Error added = scratch.addPolymer(id, type, &polymerIndex);
    if (!added.ok()) {
        if (errors) errors->push_back(SequenceDiagnostic(added.code, added.message, 0));
        return added;
    }

    const std::string& letters = records[0].sequence;
    for (size_t i = 0; i < letters.size(); ++i) {
        if (i >= options.maxResidues) {
            SequenceDiagnostic d(core::ErrorCode::LimitExceeded,
                                 "FASTA residue count exceeds the configured limit", i);
            if (errors) errors->push_back(d);
            return core::Error(core::ErrorCode::LimitExceeded, d.message);
        }
        char letter = letters[i];
        // The documented paste-time substitution (guide PDF pages 192-193).
        if (options.applyNuSubstitution && nucleic) {
            if (kind == FastaKind::Rna && letter == 'T') { letter = 'U'; ++local.substitutedResidues; }
            else if (kind == FastaKind::Dna && letter == 'U') { letter = 'T'; ++local.substitutedResidues; }
        }
        if (letter == kUnknownPeptideAnalog || letter == kUnknownNucleicAnalog) {
            // X and N mean 'unknown natural analog'. They are preserved as an
            // unknown monomer carrying the letter; we do NOT pick a structure.
            ++local.unknownResidues;
        }
        const std::string symbol(1, letter);
        MonomerInstance instance;
        instance.sourceText = symbol;
        const MonomerDefinition* found = scratch.library().find(type, symbol);
        if (found) {
            instance.definition = *found;
        } else {
            // A single-letter ambiguity: 'B', 'J', 'Z' and 'X' are FASTA OR codes
            // with no single structure, and any other letter simply has no
            // monomer. Both are preserved without invented chemistry.
            instance.definition = unknownMonomer(type, symbol);
            if (letter != kUnknownPeptideAnalog && letter != kUnknownNucleicAnalog)
                ++local.unknownResidues;
            SequenceDiagnostic d(core::ErrorCode::Unsupported,
                std::string("FASTA letter '") + letter +
                "' has no monomer in the library and was preserved without a structure", i);
            local.warnings.push_back(d);
        }
        MonomerInstanceId newId = kInvalidMonomerInstanceId;
        core::Error inserted = scratch.insertMonomerInstance(
            polymerIndex, scratch.polymer(polymerIndex)->monomers.size(), instance, &newId);
        if (!inserted.ok()) {
            if (errors) errors->push_back(SequenceDiagnostic(inserted.code, inserted.message, i));
            return inserted;
        }
        ++local.residues;
    }
    if (records.size() > 1) {
        SequenceDiagnostic d(core::ErrorCode::Unsupported,
            "FASTA input had more than one record; only the first was placed",
            records[1].offset);
        local.warnings.push_back(d);
    }
    model = scratch;
    if (report) *report = local;
    if (errors) *errors = local.warnings;
    return core::Error();
}

// ----------------------------------------------------------------------------
// Writing
// ----------------------------------------------------------------------------
core::Error writeFasta(const SequenceModel& model, std::string* out,
                       FastaWriteReport* report, const FastaWriteOptions& options,
                       const std::string& header) {
    if (!out) return core::Error(core::ErrorCode::InvalidArgument, "null output");
    out->clear();
    // Count LIVE polymers. The guide supports one peptide or one RNA sequence and
    // explicitly not CHEM or BLOB (PDF page 187).
    size_t livePolymers = 0;
    const SequencePolymer* only = 0;
    for (size_t i = 0; i < model.polymerCount(); ++i) {
        const SequencePolymer* polymer = model.polymer(i);
        if (!polymer) continue;
        if (polymer->liveCount() == 0) continue;
        ++livePolymers;
        only = polymer;
    }
    FastaWriteReport local;
    local.polymersConsidered = livePolymers;
    if (livePolymers == 0) {
        if (report) *report = local;
        return core::Error(core::ErrorCode::NotFound, "the model holds no residues to write");
    }
    if (livePolymers > 1) {
        if (report) *report = local;
        return core::Error(core::ErrorCode::Unsupported,
            "Copy As FASTA supports a single peptide or RNA sequence; this model has more");
    }
    if (only->type == PolymerType::Chem || only->type == PolymerType::Blob) {
        if (report) *report = local;
        return core::Error(core::ErrorCode::Unsupported,
            "Copy As FASTA does not support CHEM or BLOB sequences");
    }
    if (model.connectionCount() != 0) {
        // The guide's FASTA copy warns the user to ensure no cyclic or bridge
        // bonds are selected (PDF page 187). A covalent cross-link therefore
        // makes a single-letter string lose information, and losing it silently
        // is what this refuses.
        size_t liveConnections = 0;
        for (size_t c = 0; c < model.connectionCount(); ++c) {
            const SequenceConnection* conn = model.connection(c);
            if (conn && conn->alive && conn->kind == ConnectionKind::Covalent) ++liveConnections;
        }
        if (liveConnections != 0) {
            if (report) *report = local;
            return core::Error(core::ErrorCode::Unsupported,
                "Copy As FASTA is undefined for a cross-linked or cyclic sequence");
        }
    }
    local.peptideKind = (only->type == PolymerType::Peptide);

    std::string letters;
    for (size_t m = 0; m < only->monomers.size(); ++m) {
        const MonomerInstance& instance = only->monomers[m];
        if (!instance.alive) continue;
        bool unknown = false;
        const char letter = fastaAnalogFor(instance.definition, &unknown);
        if (unknown) ++local.unknownAnalogs;
        letters.push_back(letter);
    }
    local.residues = letters.size();

    std::string result;
    if (options.writeHeader) {
        result += '>';
        result += header.empty() ? only->polymerId : header;
        result += '\n';
    }
    if (options.lineWidth == 0 || letters.size() <= options.lineWidth) {
        result += letters;
        result += '\n';
    } else {
        for (size_t i = 0; i < letters.size(); i += options.lineWidth) {
            result += letters.substr(i, options.lineWidth);
            result += '\n';
        }
    }
    *out = result;
    if (report) *report = local;
    return core::Error();
}

} // namespace helm
} // namespace r62
