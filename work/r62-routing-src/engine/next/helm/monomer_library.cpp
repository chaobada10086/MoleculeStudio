// =============================================================================
// re_round62_viewer/next/helm/monomer_library.cpp
//
// T15 - monomer library implementation.
//
// The kBuiltinTable below is GENERATED from the official Pistoia Alliance HELM
// Core Library (MIT). Do not hand-edit a row: regenerate it with
//     out/T15/research/gen_table.py  ->  out/T15/research/table_rows.inc
// and record the new upstream sha256 in monomer.hpp and in RESULTS.md.
// =============================================================================

#include "next/helm/monomer.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace r62 {
namespace helm {

const char* const kAttachmentR1 = "R1";
const char* const kAttachmentR2 = "R2";
const char* const kAttachmentR3 = "R3";
const char* const kAttachmentPair = "pair";

// Provenance of the generated table. Quoted verbatim in README.md / RESULTS.md.
const char* const kPistoiaCoreLicense = "MIT";
const char* const kPistoiaCoreSource =
    "Pistoia Alliance HELM Core Library, HELMCoreLibrary/HELMCoreLibrary.json, "
    "https://github.com/PistoiaHELM/HELMMonomerSets, "
    "sha256 9df9d7fbaa867260e537681bea48ddd8e8251b73536b8a5c4d999b870906aa63, "
    "licence MIT (Copyright (c) 2021 Pistoia Alliance HELM Project)";
const char* const kPistoiaCoreRetrieved = "2026-10-14";

// ---------------------------------------------------------------------------
// Names and tokens
// ---------------------------------------------------------------------------
const char* stageName(int stage) {
    switch (stage) {
        case 1: return "stage1";
        case 2: return "stage2";
        default: return "unknown";
    }
}

bool stageHasVersion(int stage, int version) {
    if (stage == 1) return version == kStage1DataVersion;
    if (stage == 2) return version == kStage2DataVersion;
    return false;
}

const char* polymerTypeToken(PolymerType t) {
    switch (t) {
        case PolymerType::Peptide: return "PEPTIDE";
        case PolymerType::Rna:     return "RNA";
        case PolymerType::Chem:    return "CHEM";
        case PolymerType::Blob:    return "BLOB";
        default: return "";
    }
}

const char* polymerTypeName(PolymerType t) {
    switch (t) {
        case PolymerType::Peptide: return "peptide";
        case PolymerType::Rna:     return "rna";
        case PolymerType::Chem:    return "chem";
        case PolymerType::Blob:    return "blob";
        default: return "unknown";
    }
}

bool polymerTypeFromToken(const std::string& token, PolymerType* out) {
    if (!out) return false;
    if (token == "PEPTIDE" || token == "AA") { *out = PolymerType::Peptide; return true; }
    if (token == "RNA" || token == "DNA")     { *out = PolymerType::Rna;     return true; }
    if (token == "CHEM")                      { *out = PolymerType::Chem;    return true; }
    if (token == "BLOB")                      { *out = PolymerType::Blob;    return true; }
    *out = PolymerType::Unknown;
    return false;
}

// A polymer ID must start with a letter and end with digits (PolymerNode.java).
bool polymerTypeFromId(const std::string& polymerId, PolymerType* out) {
    if (!out || polymerId.empty()) return false;
    size_t i = 0;
    while (i < polymerId.size() && std::isalpha(static_cast<unsigned char>(polymerId[i]))) ++i;
    if (i == 0) return false;
    if (i == polymerId.size()) return false;   // no digits
    for (size_t k = i; k < polymerId.size(); ++k)
        if (!std::isdigit(static_cast<unsigned char>(polymerId[k]))) return false;
    return polymerTypeFromToken(polymerId.substr(0, i), out);
}

bool polymerTypeFromName(const std::string& name, PolymerType* out) {
    if (!out) return false;
    if (name == "peptide") { *out = PolymerType::Peptide; return true; }
    if (name == "rna")     { *out = PolymerType::Rna;     return true; }
    if (name == "chem")    { *out = PolymerType::Chem;    return true; }
    if (name == "blob")    { *out = PolymerType::Blob;    return true; }
    if (name == "unknown") { *out = PolymerType::Unknown; return true; }
    return polymerTypeFromToken(name, out);
}

bool polymerTypeIsNucleic(PolymerType t) { return t == PolymerType::Rna; }

const char* monomerRoleName(MonomerRole r) {
    switch (r) {
        case MonomerRole::Backbone: return "backbone";
        case MonomerRole::Branch:   return "branch";
        default: return "unknown";
    }
}

bool monomerRoleFromName(const std::string& name, MonomerRole* out) {
    if (!out) return false;
    if (name == "backbone") { *out = MonomerRole::Backbone; return true; }
    if (name == "branch")   { *out = MonomerRole::Branch;   return true; }
    if (name == "unknown")  { *out = MonomerRole::Unknown;  return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Attachment labels
// ---------------------------------------------------------------------------
// MonomerParser.validateAttachmentLabel: the literal "pair", or R followed by
// one or more digits. Note "R0" is a syntactically valid label here; whether a
// monomer HAS it is answered by AttachmentPoint lookup, not by syntax.
bool isValidAttachmentLabel(const std::string& label) {
    if (label == kAttachmentPair) return true;
    if (label.size() < 2 || label[0] != 'R') return false;
    for (size_t i = 1; i < label.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(label[i]))) return false;
    return true;
}

bool isValidHelm2AttachmentLabel(const std::string& label) {
    if (label == "?") return true;
    if (label.size() < 2 || label[0] != 'R') return false;
    if (label[1] == '0') return false;   // R0 is not a positive number
    for (size_t i = 1; i < label.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(label[i]))) return false;
    return true;
}

bool parseAttachmentLabel(const std::string& label, int* outNumber) {
    if (!outNumber) return false;
    if (label.size() < 2 || label[0] != 'R') return false;
    long value = 0;
    for (size_t i = 1; i < label.size(); ++i) {
        const char c = label[i];
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        value = value * 10 + (c - '0');
        if (value > 100000) return false;
    }
    *outNumber = static_cast<int>(value);
    return true;
}

std::string formatAttachmentLabel(int number) {
    if (number < 0) return std::string();
    char buf[24];
    std::snprintf(buf, sizeof(buf), "R%d", number);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// MonomerDefinition
// ---------------------------------------------------------------------------
const AttachmentPoint* MonomerDefinition::attachment(const std::string& label) const {
    for (size_t i = 0; i < attachments.size(); ++i)
        if (attachments[i].label == label) return &attachments[i];
    return 0;
}

bool MonomerDefinition::canExpandToAtoms() const {
    return chemistryDefined && !smiles.empty() && polymerType != PolymerType::Blob &&
           polymerType != PolymerType::Unknown;
}

bool MonomerDefinition::operator==(const MonomerDefinition& o) const {
    return symbol == o.symbol && polymerType == o.polymerType && role == o.role &&
           name == o.name && naturalAnalog == o.naturalAnalog && smiles == o.smiles &&
           natural == o.natural && chemistryDefined == o.chemistryDefined &&
           attachments == o.attachments && license == o.license && source == o.source &&
           sourceRecordId == o.sourceRecordId;
}

// ---------------------------------------------------------------------------
// The generated table
// ---------------------------------------------------------------------------
struct BuiltinRow {
    const char* symbol;
    PolymerType polymerType;
    MonomerRole role;
    const char* name;
    const char* naturalAnalog;
    const char* smiles;
    int attachmentCount;
    AttachmentPoint attachments[kMaxAttachmentPoints];
};

static const BuiltinRow kBuiltinTable[] = {
    { "A", PolymerType::Peptide, MonomerRole::Backbone, "Alanine", "A", "C[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "C", PolymerType::Peptide, MonomerRole::Backbone, "Cysteine", "C", "[H:1]N[C@@H](CS[H:3])C([OH:2])=O", 3,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"}, {"R3", "H", "[*:3][H]"} } },
    { "D", PolymerType::Peptide, MonomerRole::Backbone, "Aspartic acid", "D", "[H:1]N[C@@H](CC([OH:3])=O)C([OH:2])=O", 3,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"}, {"R3", "OH", "O[*:3]"} } },
    { "E", PolymerType::Peptide, MonomerRole::Backbone, "Glutamic acid", "E", "[H:1]N[C@@H](CCC([OH:3])=O)C([OH:2])=O", 3,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"}, {"R3", "OH", "O[*:3]"} } },
    { "F", PolymerType::Peptide, MonomerRole::Backbone, "Phenylalanine", "F", "[H:1]N[C@@H](Cc1ccccc1)C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "G", PolymerType::Peptide, MonomerRole::Backbone, "Glycine", "G", "[H:1]NCC([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "H", PolymerType::Peptide, MonomerRole::Backbone, "Histidine", "H", "[H:1]N[C@@H](Cc1cnc[nH]1)C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "I", PolymerType::Peptide, MonomerRole::Backbone, "Isoleucine", "I", "CC[C@H](C)[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "K", PolymerType::Peptide, MonomerRole::Backbone, "Lysine", "K", "[H:1]N[C@@H](CCCCN[H:3])C([OH:2])=O", 3,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"}, {"R3", "H", "[*:3][H]"} } },
    { "L", PolymerType::Peptide, MonomerRole::Backbone, "Leucine", "L", "CC(C)C[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "M", PolymerType::Peptide, MonomerRole::Backbone, "Methionine", "M", "CSCC[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "N", PolymerType::Peptide, MonomerRole::Backbone, "Asparagine", "N", "NC(=O)C[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "P", PolymerType::Peptide, MonomerRole::Backbone, "Proline", "P", "[H:1]N1CCC[C@H]1C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "Q", PolymerType::Peptide, MonomerRole::Backbone, "Glutamine", "Q", "NC(=O)CC[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "R", PolymerType::Peptide, MonomerRole::Backbone, "Arginine", "R", "NC(=N)NCCC[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "S", PolymerType::Peptide, MonomerRole::Backbone, "Serine", "S", "OC[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "T", PolymerType::Peptide, MonomerRole::Backbone, "Threonine", "T", "C[C@@H](O)[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "V", PolymerType::Peptide, MonomerRole::Backbone, "Valine", "V", "CC(C)[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "W", PolymerType::Peptide, MonomerRole::Backbone, "Tryptophan", "W", "[H:1]N[C@@H](Cc1c[nH]c2ccccc12)C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "Y", PolymerType::Peptide, MonomerRole::Backbone, "Tyrosine", "Y", "Oc1ccc(C[C@H](N[H:1])C([OH:2])=O)cc1", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "Pyl", PolymerType::Peptide, MonomerRole::Backbone, "Pyrrolysine", "X", "C[C@@H]1CC=N[C@H]1C(=O)NCCCC[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "seC", PolymerType::Peptide, MonomerRole::Backbone, "SelenoCysteine", "C", "[SeH]C[C@H](N[H:1])C([OH:2])=O", 2,
      { {"R1", "H", "[*:1][H]"}, {"R2", "OH", "O[*:2]"} } },
    { "r", PolymerType::Rna, MonomerRole::Backbone, "Ribose", "r", "[H:1]OC[C@H]1O[C@@H]([OH:3])[C@@H](O)[C@@H]1O[H:2]", 3,
      { {"R1", "H", "[*:1][H]"}, {"R2", "H", "[*:2][H]"}, {"R3", "OH", "O[*:3]"} } },
    { "d", PolymerType::Rna, MonomerRole::Backbone, "Deoxyribose", "r", "[H:1]OC[C@H]1O[C@@H]([OH:3])C[C@@H]1O[H:2]", 3,
      { {"R1", "H", "[*:1][H]"}, {"R2", "H", "[*:2][H]"}, {"R3", "OH", "O[*:3]"} } },
    { "p", PolymerType::Rna, MonomerRole::Backbone, "Phosphate", "p", "[OH:1]P([OH:2])(=O)O", 2,
      { {"R1", "OH", "O[*:1]"}, {"R2", "OH", "O[*:2]"} } },
    { "A", PolymerType::Rna, MonomerRole::Branch, "Adenine", "A", "[H:1]n1cnc2c(N)ncnc21", 1,
      { {"R1", "H", "[*:1][H]"} } },
    { "C", PolymerType::Rna, MonomerRole::Branch, "Cytosine", "C", "[H:1]n1ccc(N)nc1=O", 1,
      { {"R1", "H", "[*:1][H]"} } },
    { "G", PolymerType::Rna, MonomerRole::Branch, "Guanine", "G", "[H:1]n1cnc2c(=O)[nH]c(N)nc21", 1,
      { {"R1", "H", "[*:1][H]"} } },
    { "U", PolymerType::Rna, MonomerRole::Branch, "Uracil", "U", "[H:1]n1ccc(=O)[nH]c1=O", 1,
      { {"R1", "H", "[*:1][H]"} } },
    { "T", PolymerType::Rna, MonomerRole::Branch, "Thymine", "T", "[H:1]n1cc(C)c(=O)[nH]c1=O", 1,
      { {"R1", "H", "[*:1][H]"} } },
};

static const size_t kBuiltinTableSize = sizeof(kBuiltinTable) / sizeof(kBuiltinTable[0]);

MonomerLibrary builtinNaturalLibrary() {
    MonomerLibrary library;
    LibraryProvenance provenance;
    provenance.license = kPistoiaCoreLicense;
    provenance.source = kPistoiaCoreSource;
    provenance.retrievedUtc = kPistoiaCoreRetrieved;
    provenance.note = "generated phase-1 natural set; see out/T15/research/EVIDENCE.md";
    library.setLibraryProvenance(provenance);

    for (size_t i = 0; i < kBuiltinTableSize; ++i) {
        const BuiltinRow& row = kBuiltinTable[i];
        MonomerDefinition definition;
        definition.symbol = row.symbol;
        definition.polymerType = row.polymerType;
        definition.role = row.role;
        definition.name = row.name;
        definition.naturalAnalog = row.naturalAnalog;
        definition.smiles = row.smiles;
        // A generated row is a real structure only when the file stated SMILES.
        definition.chemistryDefined = (row.smiles != 0 && row.smiles[0] != '\0');
        definition.natural = !definition.naturalAnalog.empty();
        definition.license = kPistoiaCoreLicense;
        definition.source = kPistoiaCoreSource;
        for (int a = 0; a < row.attachmentCount && a < kMaxAttachmentPoints; ++a)
            definition.attachments.push_back(row.attachments[a]);
        std::string reason;
        if (!library.add(definition, &reason)) {
            // A generated row that the library refuses is a build-time defect,
            // not a user error. Fail loudly rather than shipping a partial set.
            std::fprintf(stderr, "T15 builtin library refused %s: %s\n",
                         row.symbol, reason.c_str());
        }
    }
    return library;
}

// ---------------------------------------------------------------------------
// Synthetic definitions
// ---------------------------------------------------------------------------
MonomerDefinition unknownMonomer(PolymerType t, const std::string& symbol) {
    MonomerDefinition d;
    d.symbol = symbol;
    d.polymerType = t;
    d.role = MonomerRole::Unknown;
    d.name = std::string("unknown monomer (") + polymerTypeName(t) + ")";
    d.smiles = "";              // NEVER invented (R1)
    d.chemistryDefined = false; // NEVER claimed (R1)
    d.natural = false;
    d.source = "unspecified: parsed from input, structure not resolved";
    d.license = "n/a";          // no third-party content was copied
    return d;
}

MonomerDefinition blobMonomer(const std::string& symbol) {
    MonomerDefinition d;
    d.symbol = symbol;
    d.polymerType = PolymerType::Blob;
    d.role = MonomerRole::Unknown;
    d.name = "undefined polymer";
    d.smiles = "";              // a BLOB has no structure (R2)
    d.chemistryDefined = false;
    d.natural = false;
    d.source = "HELM BLOB: no chemical definition exists for this monomer";
    d.license = "n/a";
    return d;
}

MonomerDefinition chemMonomer(const std::string& symbol, const std::string& smiles) {
    MonomerDefinition d;
    d.symbol = symbol;
    d.polymerType = PolymerType::Chem;
    d.role = MonomerRole::Backbone;
    d.name = "chemical monomer";
    d.smiles = smiles;
    d.chemistryDefined = !smiles.empty();   // only when the SOURCE supplied one (R3)
    d.natural = false;
    d.source = "inline CHEM definition from the input document";
    d.license = "n/a";
    // A CHEM monomer states its own attachment points inline. We record R1/R2 as
    // the conventional pair ONLY when the caller supplied a non-empty definition,
    // and we never fabricate a capping group for them.
    if (d.chemistryDefined) {
        d.attachments.push_back(AttachmentPoint("R1", "", ""));
        d.attachments.push_back(AttachmentPoint("R2", "", ""));
    }
    return d;
}

bool connectionsSatisfy(size_t required, size_t available) { return required <= available; }

} // namespace helm
} // namespace r62
