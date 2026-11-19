#ifndef R62_NEXT_TEMPLATES_QUERIES_PERSIST_HPP
#define R62_NEXT_TEMPLATES_QUERIES_PERSIST_HPP
// =============================================================================
// re_round62_viewer/next/templates_queries/persist.hpp
//
// T13 - BINDING QUERY AND POLYMER SEMANTICS TO DOCUMENT OBJECTS.
//
// -----------------------------------------------------------------------------
// WHERE THE DATA GOES, AND WHAT THAT GUARANTEES
// -----------------------------------------------------------------------------
// T01 gives every object an ExtensionBag and the native serialization writes every
// extension entry verbatim (next/core/serialization.hpp).  T13 therefore stores
// query and polymer semantics as extension entries under the "user.t13." prefix:
//
//     user.t13.query.atommap        atom map number
//     user.t13.query.freesite       free site count
//     user.t13.query.elementlist    encoded ElementList
//     user.t13.query.stereoflag     encoded StereoFlag (on the flag object)
//     user.t13.query.altgroup       encoded AlternativeGroup definition
//     user.t13.polymer.params.1     encoded PolymerBracket
//
// THE HONEST LIMIT: core::isPersistentExtensionKey() only blesses the "cdxml."
// prefix.  A "user.*" key IS written and read by the NATIVE format, so "save,
// reopen, semantics intact" is satisfied for .t01doc.  It is NOT claimed to
// survive a CDXML round trip, because the CDXML adapter does not know these keys
// and this module does not modify that adapter.  RESULTS.md states this in the
// capability matrix instead of implying full CDXML query support.
// =============================================================================

#include "next/core/commands.hpp"
#include "next/core/document.hpp"
#include "next/templates_queries/polymer.hpp"
#include "next/templates_queries/query.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace templates_queries {

extern const char* const kKeyElementList;   // "user.t13.query.elementlist"
extern const char* const kKeyAltGroupDef;   // "user.t13.query.altgroup"
extern const char* const kKeyStereoFlagData;// "user.t13.query.stereoflag"

// ---- readers ---------------------------------------------------------------
// Read a free-site count.  Returns false when the atom carries none, so "absent"
// and "zero" are distinguishable (guide: *0 is a real value meaning "as drawn").
bool readFreeSiteCount(const core::Document& document, core::ObjectId atom, int* out);
bool readAtomMapNumber(const core::Document& document, core::ObjectId atom, int* out);
bool readElementList(const core::Document& document, core::ObjectId atom,
                     ElementList* out);
bool readPolymerBracket(const core::Document& document, core::ObjectId bracket,
                        PolymerBracket* out);
bool readStereoFlag(const core::Document& document, core::ObjectRef holder,
                    StereoFlag* out);
bool readAlternativeGroup(const core::Document& document, core::ObjectRef holder,
                          AlternativeGroup* out);

// ---- writers ---------------------------------------------------------------
// Each writer returns ONE command, so a user action is one undo step.  A writer
// for a datum this module cannot encode returns Unsupported with a reason rather
// than storing something lossy.
core::Error writeElementListCommand(core::ObjectId atom, const ElementList& list,
                                   core::Command* out);
core::Error writePolymerBracketCommand(const core::Document& document,
                                       core::ObjectId bracket,
                                       const PolymerBracket& params,
                                       core::Command* out);
core::Error writeStereoFlagCommand(core::ObjectRef holder, const StereoFlag& flag,
                                   core::Command* out);
core::Error writeAlternativeGroupCommand(core::ObjectRef holder,
                                         const AlternativeGroup& group,
                                         core::Command* out);

// ---- encoders / decoders for the query records ------------------------------
// Exposed so the tests can prove a round trip independently of the document.
std::string encodeElementList(const ElementList& list);
bool        decodeElementList(const std::string& text, ElementList* out);
std::string encodeStereoFlag(const StereoFlag& flag);
bool        decodeStereoFlag(const std::string& text, StereoFlag* out);
std::string encodeAlternativeGroup(const AlternativeGroup& group);
bool        decodeAlternativeGroup(const std::string& text, AlternativeGroup* out);

} // namespace templates_queries
} // namespace r62

#endif // R62_NEXT_TEMPLATES_QUERIES_PERSIST_HPP
