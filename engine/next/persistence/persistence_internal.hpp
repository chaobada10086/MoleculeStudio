#ifndef R62_NEXT_PERSISTENCE_PERSISTENCE_INTERNAL_HPP
#define R62_NEXT_PERSISTENCE_PERSISTENCE_INTERNAL_HPP
// =============================================================================
// re_round62_viewer/next/persistence/persistence_internal.hpp
//
// T02 - INTERNAL helpers shared by the reader and the writer.
//
// NOT a public interface. Nothing outside next/persistence/ may include this
// header: it exposes the CDXML encoders, which are an implementation detail of
// the writer and are free to change. The public surface is persistence.hpp.
// =============================================================================

#include "persistence.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace tinyxml2 { class XMLElement; }

namespace r62 {
namespace persistence {
namespace cdxml {

extern const char* const kPredefinedEntities[];

// Error helper. Every message is prefixed so a caller can tell a CDXML refusal
// from an I/O failure at a glance.
core::Error fail(const std::string& message, core::ErrorCode code);

core::Error checkEntities(const std::string& bytes);

core::Error parseId(const char* input, int32_t* out);
core::Error parseNumber(const char* input, const char* name, double* out);
core::Error parseInteger(const char* input, const char* name, long long minimum,
                          long long maximum, long long* out);
core::Error parsePosition(const char* input, core::Vec2* out);
bool        parseBoundingBox(const char* input, core::Box* out);
std::string lower(const std::string& value);

// --- enum tables (reject-on-unknown) --------------------------------------
// Every table returns false for a value it does not know, so the caller refuses
// rather than guessing a multiplicity or a display style.
bool bondOrderFromCdxml(const char* text, core::BondOrder* order,
                        core::BondSemantics* semantics);
bool bondDisplayFromCdxml(const char* text, core::BondDisplay* display);
bool bondStereoFromCdxml(const char* text, core::BondStereo* stereo);
bool nodeTypeFromCdxml(const char* text, core::NodeType* type);
bool radicalFromCdxml(const char* text, uint8_t* radical);
bool textAlignFromCdxml(const char* text, core::TextAlign* align, bool* known);

// --- face flags ------------------------------------------------------------
// The vendor CDXString bit values. Named constants so the reader and the writer
// cannot drift apart and so the bits this model cannot represent are explicit.
enum FaceBits {
    kFacePlain       = 0x00,
    kFaceBold        = 0x01,
    kFaceItalic      = 0x02,
    kFaceUnderline   = 0x04,
    kFaceOutline     = 0x08,   // no core::TextFlags bit
    kFaceShadow      = 0x10,   // no core::TextFlags bit
    kFaceSubscript   = 0x20,
    kFaceSuperscript = 0x40,
    kFaceFormula     = 0x60,   // subscript | superscript
    kFaceUnmappedMask = kFaceOutline | kFaceShadow
};

uint16_t textFlagsFromFace(int face);
int      faceFromTextFlags(uint16_t flags);

// --- colour ----------------------------------------------------------------
// CDXML writes colours as a signed 32-bit CDX value (bytes R,G,B,A from the
// least significant one) or as a name. The RAW text is also kept by the caller
// so an unrecognised spelling cannot be silently changed by a round trip.
bool colorFromCdxml(const char* text, uint32_t* rgba);
std::string colorToCdxml(uint32_t rgba);

// --- escaping --------------------------------------------------------------
void escapeAttribute(const std::string& value, std::string* out);
void escapeContent(const std::string& value, std::string* out);

// --- doubles ---------------------------------------------------------------
// 17 significant digits, the same spelling the T01 native format uses, so a
// coordinate survives a round trip bit for bit.
std::string numberToText(double value);

// --- attribute tables ------------------------------------------------------
// The set of attributes the reader PROMOTES for one element. Anything else is an
// unknown attribute: preserved, or refused, but never dropped.
// A small, ALLOCATION-FREE attribute allow-list.
//
// The obvious implementation (a std::vector<std::string>) allocates once per entry, and
// the reader builds one per element: measured at 250,000 small allocations for a
// 5,000-atom file, which dominated the read. A fixed array of borrowed const char*
// pointers removes that entirely; the table never outlives the call that builds it.
class AttrTable {
public:
    enum { kMaxNames = 48 };
    AttrTable() : count_(0) {}
    void allow(const char* name) {
        if (count_ < kMaxNames) names_[count_++] = name;
    }
    bool isAllowed(const char* name) const {
        if (!name) return false;
        for (size_t i = 0; i < count_; ++i)
            if (std::strcmp(names_[i], name) == 0) return true;
        return false;
    }
    size_t size() const { return count_; }
private:
    const char* names_[kMaxNames];
    size_t      count_;
};

bool onlyWhitespace(const tinyxml2::XMLElement* element);
void signatureOf(const tinyxml2::XMLElement* element, std::string* out);

} // namespace cdxml

// ---------------------------------------------------------------------------
// Edit-detection signatures (r62::persistence, NOT r62::persistence::cdxml)
// ---------------------------------------------------------------------------
// Computed from the CURRENT model state, in exactly the formatting the writer
// emits, so "signature unchanged" implies "the generated bytes equal the
// preserved bytes for every promoted field". The reader stores the value it saw
// at load time; the writer recomputes and compares, so the two sides cannot
// drift apart without a test noticing.
std::string atomSignature(const core::Document& document, const core::AtomRecord& atom);
std::string bondSignature(const core::BondRecord& bond);
std::string textSignature(const core::Document& document, const core::TextRecord& text);

// Extension keys in the pst.* namespace this module owns.
std::string signatureKey(core::ObjectKind kind, core::ObjectId id);
std::string signatureKeyName(core::ObjectKind kind);
std::string pathSignature(const core::Document& document, const core::PathRecord& path);
std::string annotationSignature(const core::AnnotationRecord& annotation);

} // namespace persistence
} // namespace r62

#endif // R62_NEXT_PERSISTENCE_PERSISTENCE_INTERNAL_HPP
