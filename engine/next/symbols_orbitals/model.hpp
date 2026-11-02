#ifndef R62_NEXT_SYMBOLS_ORBITALS_MODEL_HPP
#define R62_NEXT_SYMBOLS_ORBITALS_MODEL_HPP
// =============================================================================
// re_round62_viewer/next/symbols_orbitals/model.hpp
//
// T12 - HOW A CHEMICAL SYMBOL / PUSH / ORBITAL LIVES IN THE T01 DOCUMENT.
//
// -----------------------------------------------------------------------------
// THE SLAB MAPPING (no new public type is invented)
// -----------------------------------------------------------------------------
// T01 froze exactly the slabs that fit these objects:
//
//   symbol  -> ObjectKind::Symbol  in the symbols()  slab (AnnotationRecord)
//   push    -> ObjectKind::Arrow   in the arrows()   slab (AnnotationRecord)
//   orbital -> ObjectKind::Orbital in the orbitals() slab (AnnotationRecord)
//
// AnnotationRecord gives geometry[4], presetId, scale, rotation, LineStyle and
// fillRGBA - enough to place and style the object - but not enough for the rich
// parameter bundle (sign, meaning, charge delta, per-lobe phase, push head).
// Those go in the OBJECT EXTENSION BAG under one versioned key each.
//
// A PUSH IS STORED AS AN ARROW because [GUIDE p158] says the electron-transfer
// arrow can be modified "by using either the Arrow tool, or the Electron Pushing
// Tool" - i.e. it IS an arrow object to ChemDraw, not a different class. Storing
// it in the arrows() slab means T10 arrow readers and the existing delete/hover
// behaviour apply unchanged.
//
// -----------------------------------------------------------------------------
// GRAPHIC RECORD vs CHEMICAL ATTRIBUTE: THE CENTRAL SEPARATION
// -----------------------------------------------------------------------------
// The acceptance criterion says "graphic records and atom chemical properties are
// separate, but may be linked through T06 binding rules", and that "moving a
// symbol near an atom must NOT change the charge without evidence".
//
// The design that delivers that:
//
//   * The SYMBOL record has a position but NO atom id by default. An unbound
//     symbol is a pure graphic.
//   * BINDING is a SEPARATE, EXPLICIT step. bindSymbolToAtom() records the atom
//     id in the extension bag AND applies the chemistry; unbindSymbol() removes
//     the atom id AND restores the atom to the state captured at bind time. Both
//     are single transactions, so Undo restores BOTH sides.
//   * Proximity alone does nothing. The ONLY automatic-looking rule is the
//     guide-documented lone-pair/diradical reading [GUIDE p242], and even that is
//     an OPT-IN flag on the bind call, never applied by a move.
//
// The before-state of the atom is captured INTO the symbol extension bag at bind
// time (key so.bind.1). A bind command therefore writes BOTH records inside one
// transaction, and an unbind can restore the exact prior atom state even if the
// atom was edited in between.
// =============================================================================

#include "next/symbols_orbitals/geometry.hpp"
#include "next/symbols_orbitals/preset.hpp"
#include "next/core/document.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace symbols {

// The extension keys. Bump the trailing number if an encoding changes.
extern const char* const kSymbolParamsKey;   // "so.symbol.params.1"
extern const char* const kOrbitalParamsKey;  // "so.orbital.params.1"
extern const char* const kPushParamsKey;     // "so.push.params.1"
extern const char* const kBindKey;           // "so.bind.1"

// ---------------------------------------------------------------------------
// Encoding: a compact "k=v;k=v;..." record
// ---------------------------------------------------------------------------
// Chosen over a binary blob because it survives a text diff (how a
// lost-parameter bug is diagnosed), is stable across compilers (no struct layout
// in the document) and ignores unknown keys on read, so an older reader degrades
// rather than refusing.
//
// A malformed record returns ok=false and leaves the out value at its default -
// NEVER half-updated - so a corrupt document cannot produce a half-configured
// object.
std::string encodeSymbolParams(const SymbolParams& p);
SymbolParams decodeSymbolParams(const std::string& text, bool* ok);

std::string encodeOrbitalParams(const OrbitalParams& p);
OrbitalParams decodeOrbitalParams(const std::string& text, bool* ok);

std::string encodePushParams(const PushParams& p);
PushParams decodePushParams(const std::string& text, bool* ok);

// A revision counter so the renderer can tell unchanged from changed without
// diffing the whole struct.
uint64_t symbolParamsSignature(const SymbolParams& p);
uint64_t orbitalParamsSignature(const OrbitalParams& p);

// ---------------------------------------------------------------------------
// AnnotationRecord adapters
// ---------------------------------------------------------------------------
core::Error toAnnotationRecord(const SymbolParams& p, double documentLineWidth,
                               core::AnnotationRecord* out);
core::Error toAnnotationRecord(const OrbitalParams& p, double documentLineWidth,
                               core::AnnotationRecord* out);
core::Error toAnnotationRecord(const PushParams& p, double documentLineWidth,
                               core::AnnotationRecord* out);

// Read the parameters back. `ok` is false when the bag has no usable record; the
// geometry from the record is still returned so an object imported from CDXML
// before T12 existed (or written by another module) is never invisible.
SymbolParams  symbolFromAnnotationRecord(const core::AnnotationRecord& r,
                                         const core::ExtensionBag& bag, bool* ok);
OrbitalParams orbitalFromAnnotationRecord(const core::AnnotationRecord& r,
                                          const core::ExtensionBag& bag, bool* ok);
PushParams    pushFromAnnotationRecord(const core::AnnotationRecord& r,
                                       const core::ExtensionBag& bag, bool* ok);

// ---------------------------------------------------------------------------
// Document adapters
// ---------------------------------------------------------------------------
// These are the ONLY functions here that touch a core::Document. They are
// deliberately plain: no command, no transaction, no history. A caller wraps them
// in core::commands / core::CommandLog, which is where "one gesture == one
// transaction" is enforced (T01 R1).
core::Error addSymbol(core::Document& d, const SymbolParams& p,
                      double documentLineWidth, core::ObjectId* outId);
core::Error writeSymbol(core::Document& d, core::ObjectId id, const SymbolParams& p,
                        double documentLineWidth);
core::Error readSymbol(const core::Document& d, core::ObjectId id,
                       SymbolParams* out, bool* ok);

core::Error addOrbital(core::Document& d, const OrbitalParams& p,
                       double documentLineWidth, core::ObjectId* outId);
core::Error writeOrbital(core::Document& d, core::ObjectId id, const OrbitalParams& p,
                         double documentLineWidth);
core::Error readOrbital(const core::Document& d, core::ObjectId id,
                        OrbitalParams* out, bool* ok);

core::Error addPush(core::Document& d, const PushParams& p,
                    double documentLineWidth, core::ObjectId* outId);
core::Error writePush(core::Document& d, core::ObjectId id, const PushParams& p,
                      double documentLineWidth);
core::Error readPush(const core::Document& d, core::ObjectId id,
                     PushParams* out, bool* ok);

// ---------------------------------------------------------------------------
// BINDING: the graphic/chemistry seam
// ---------------------------------------------------------------------------
struct SymbolBinding {
    core::ObjectId atom;              // kInvalidObjectId when unbound
    int8_t  appliedCharge;            // charge value written by this symbol (-128 = none)
    int8_t  appliedRadical;           // radical value written by this symbol (-1 = none)
    int8_t  priorCharge;              // atom charge before bind
    int8_t  priorRadical;             // atom radical before bind
    int8_t  priorNodeType;            // atom nodeType before bind (-1 = not captured)
    SymbolBinding()
        : atom(core::kInvalidObjectId), appliedCharge(-128), appliedRadical(-1),
          priorCharge(0), priorRadical(0), priorNodeType(-1) {}
    bool bound() const { return atom != core::kInvalidObjectId; }
};

std::string encodeBinding(const SymbolBinding& b);
SymbolBinding decodeBinding(const std::string& text, bool* ok);

// Options for an explicit bind. All default to "do nothing chemical".
struct BindOptions {
    // [GUIDE p242] "The lone pair symbol is interpreted as a DIRADICAL when it is
    // placed next to an unlabeled carbon atom." That is the ONLY documented
    // proximity-driven chemical reading, and it is NEVER applied automatically:
    // the caller must opt in, and the atom must actually be an unlabeled carbon.
    bool lonePairOnUnlabeledCarbonIsDiradical;
    // Apply the symbol own charge/radical meaning to the atom. When false the bind
    // is a pure GRAPHIC link and no chemistry is written.
    bool applyChemistry;
    BindOptions()
        : lonePairOnUnlabeledCarbonIsDiradical(false), applyChemistry(true) {}
};

// Bind symbolId to atomId and (per options) apply the symbol chemistry. Both the
// symbol record and the atom are touched, so ONE enclosing transaction makes Undo
// restore both sides. The caller must already be inside a transaction (or will
// open one around this plus the rest of the gesture): this does NOT open one.
core::Error bindSymbolToAtom(core::Document& d, core::ObjectId symbolId,
                             core::ObjectId atomId, const BindOptions& options,
                             SymbolBinding* outBinding);

// Remove the binding and restore the atom to the state captured at bind time.
core::Error unbindSymbol(core::Document& d, core::ObjectId symbolId);

// Read the binding. bound() is false for an unbound symbol. `ok` is false when
// the bag entry is absent or malformed.
SymbolBinding symbolBinding(const core::Document& d, core::ObjectId symbolId, bool* ok);

// True when the symbol kind/meaning would change the named atom at all.
bool symbolCarriesChemistry(const SymbolParams& p);

// The chemistry a symbol WOULD write to an atom, given the options.
struct ChemistryDelta {
    bool changeCharge;   int charge;
    bool changeRadical;  int radical;
    bool changeNodeType; int nodeType;
    ChemistryDelta()
        : changeCharge(false), charge(0), changeRadical(false), radical(0),
          changeNodeType(false), nodeType(0) {}
    bool empty() const { return !changeCharge && !changeRadical && !changeNodeType; }
};

ChemistryDelta chemistryForBinding(const SymbolParams& symbol,
                                   const core::AtomRecord& atom,
                                   const BindOptions& options);

// ---------------------------------------------------------------------------
// Attachment rank (the diamond sequential numbers [GUIDE p243])
// ---------------------------------------------------------------------------
// "only the diamond tool shows rank numbers. As you add diamond symbols to a
// structure, the points will have sequential numbers." The rank is DERIVED from
// creation order among diamond symbols in the document, not stored on the atom -
// so adding or deleting a diamond renumbers naturally and there is no second
// source of truth to desynchronise.
int symbolAttachmentRank(const core::Document& d, core::ObjectId symbolId, bool* ok);

} // namespace symbols
} // namespace r62

#endif // R62_NEXT_SYMBOLS_ORBITALS_MODEL_HPP
