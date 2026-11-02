#ifndef R62_NEXT_CORE_SERIALIZATION_HPP
#define R62_NEXT_CORE_SERIALIZATION_HPP
// =============================================================================
// re_round62_viewer/next/core/serialization.hpp
//
// T01 - persistence boundary. T02 owns the real reader/writer pipeline; this
// header declares the format contract and provides the reference implementation
// that the acceptance tests exercise.
//
// FORMAT  "T01DOC" - a line-oriented UTF-8 text format.
// -----------------------------------------------------------------------------
// Why text and not binary: the existing .cdxedit format is text, the whole
// project debugs from logs and diffs, and a 5,000-atom file is ~300 KB. A binary
// variant is T02's decision; the VERSION NUMBER is the compatibility contract,
// not the encoding.
//
//   line 1:  T01DOC <formatVersion> <documentId> <revision> <structureSerial>
//   line 2:  D <bondLength> <lineWidth> <textSize> <fontHex> <nameHex>
//   then, in any order, one record per line:
//     A <id> <x> <y> <element> <alive> <charge> <radical> <isotope>
//       <implicitH> <nodeType> <showLabel> <aromatic> <sourceId> [<labelHex>]
//     B <id> <begin> <end> <order> <display> <semantics> <stereo> <alive> <sourceId>
//     T <id> <x> <y> <size> <role> <alive> <sourceId> <formatFlags> <align>
//       <colorRGBA> <fontHex> <runCount> <run0Flags> <run0Size> <run0FontHex> <run0Utf8Hex> ...
//     P <id> <alive> <closed> <fillEnabled> <fillRGBA> <sourceId> <width> <color>
//       <dash> <pointCount> then <x> <y> <kind> triples
//     N <kindOrdinal> <id> <alive> <presetId> <scale> <rotation> <fillRGBA>
//       <sourceId> <width> <color> <dash> <anchor0..3> then 4 <x> <y> pairs
//     G <id> <alive> <zIndex> <sourceId> <labelHex> <memberCount> then kind/id pairs
//     X <scope> <kindOrdinal> <objectId> <sourceId> <keyHex> <valueHex>   (extension)
//     R <textId> <runCount> then <flags> <size> <fontHex> <utf8Hex> per run
//
// All floating point values are written with 17 significant digits so a
// round trip is exact for IEEE-754 doubles. IDs are decimal. Strings are
// hex-encoded UTF-8 so a record never contains a newline or a separator.
//
// VERSIONING RULE: a reader MUST refuse a formatVersion greater than
// kNativeFormatVersion, and MUST migrate any smaller version it still supports.
// Adding a record type that older readers may skip requires a minor bump;
// changing the meaning of an existing field requires a major bump.
// =============================================================================

#include "document.hpp"
#include "commands.hpp"

#include <iosfwd>
#include <string>

namespace r62 {
namespace core {

enum {
    kFormatVersionLegacy1 = 1,   // == kNativeFormatVersion: current
    kFormatVersionCurrent = kNativeFormatVersion
};

const char* const kNativeMagic = "T01DOC";
const char* const kLegacyMagic = "CDXEDIT";

struct SaveOptions {
    bool includeExtensionPayload;   // default true
    bool includeDeadSlots;          // default true: dead slots keep ids stable
    SaveOptions() : includeExtensionPayload(true), includeDeadSlots(true) {}
};

struct LoadReport {
    int         formatVersion;
    bool        migratedFromLegacy;
    uint64_t    atomsRead;
    uint64_t    bondsRead;
    uint64_t    textsRead;
    uint64_t    extensionsRead;
    std::string sourceMagic;
    LoadReport() : formatVersion(0), migratedFromLegacy(false), atomsRead(0),
                   bondsRead(0), textsRead(0), extensionsRead(0) {}
};

// --- native format ---------------------------------------------------------
Error saveDocument(const Document& document, std::ostream& output,
                   const SaveOptions& options = SaveOptions());
Error saveDocument(const Document& document, const std::string& path,
                   const SaveOptions& options = SaveOptions());
Error loadDocument(Document& document, std::istream& input, LoadReport* report = 0);
Error loadDocument(Document& document, const std::string& path, LoadReport* report = 0);

// Serialize to a string (used by tests and by the migration demo).
std::string serializeToString(const Document& document,
                              const SaveOptions& options = SaveOptions());

// --- legacy CDXEDIT 1 migration --------------------------------------------
// Reads the previous independent editor format into the new model. The mapping
// is deliberately explicit and is documented in MIGRATION.md:
//   * W <strokeWidth>  -> Defaults::lineWidth
//   * A id x y element alive -> AtomRecord, sourceId = 0
//   * B id a b order alive   -> BondRecord with order = the SAME integer,
//                              display = Normal, semantics = Normal,
//                              stereo = None, sourceId = 0
//     The legacy format only ever wrote 1/2/3, so nothing is lost and no
//     unknown semantics are invented.
//   * T id x y size alive <hex> -> TextRecord + one plain TextRun
//   * The legacy record ORDER is preserved exactly, because the legacy model
//     assigned ids by push_back and the new slab reproduces the trailing-dead-run
//     reuse rule. Slots therefore keep their original ids.
// Unknown legacy record types are preserved in the document extension bag rather
// than rejected, so a future legacy version can still be read.
Error loadLegacyCdxedit(Document& document, std::istream& input, LoadReport* report = 0);
Error loadLegacyCdxedit(Document& document, const std::string& path,
                        LoadReport* report = 0);

// Detect the format of a buffer without consuming a Document.
Error detectFormat(const std::string& bytes, LoadReport* report);

// --- hex helpers shared with the migrator and the tests ---------------------
std::string hexEncode(const std::string& value);
Error       hexDecode(const std::string& value, std::string* out);

} // namespace core
} // namespace r62

#endif // R62_NEXT_CORE_SERIALIZATION_HPP
