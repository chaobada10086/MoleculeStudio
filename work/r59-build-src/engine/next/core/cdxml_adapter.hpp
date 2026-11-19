#ifndef R62_NEXT_CORE_CDXML_ADAPTER_HPP
#define R62_NEXT_CORE_CDXML_ADAPTER_HPP
// =============================================================================
// re_round62_viewer/next/core/cdxml_adapter.hpp
//
// T01 - adapter boundary between CDXML and the shared document model.
//
// SCOPE AND HONESTY
// -----------------------------------------------------------------------------
// This adapter reads the SAME documented subset the legacy editor_cdxml.hpp read
// (element nodes, 1/2/3-order solid bonds, plain s-run text, fonttable, pages and
// fragments) and it RECORDS everything it did not promote into the common
// extension container. Recording is not the same as lossless round-tripping:
//   * it can only preserve what this adapter actually parsed;
//   * it does not preserve comments, sibling order of unknown elements, or DTD
//     details;
//   * writing back is a NEW document, so attribute order and formatting change.
// The extension container is the PREREQUISITE for lossless work, not the result.
// Anything this adapter cannot represent is REFUSED, never silently stripped: a
// silent strip of chemical semantics is worse than a refusal.
// =============================================================================

#include "document.hpp"

#include <string>

namespace r62 {
namespace core {

struct CdxmlImportOptions {
    // Record unknown attributes/elements into the extension bag. Default true.
    bool recordUnknowns;
    // Refuse (default) rather than import-less when a semantic feature is found
    // that the model could represent but this adapter does not yet map.
    bool refuseUnsupportedSemantics;
    CdxmlImportOptions() : recordUnknowns(true), refuseUnsupportedSemantics(true) {}
};

struct CdxmlImportReport {
    uint64_t nodesImported;
    uint64_t bondsImported;
    uint64_t textsImported;
    uint64_t fontsSeen;
    uint64_t unknownAttributesRecorded;
    uint64_t unknownElementsRecorded;
    std::vector<std::string> refusals;   // populated when the import fails
    CdxmlImportReport()
        : nodesImported(0), bondsImported(0), textsImported(0), fontsSeen(0),
          unknownAttributesRecorded(0), unknownElementsRecorded(0) {}
};

// Import CDXML bytes into an EMPTY document. On failure the document is left
// untouched (the caller passes a scratch document and swaps it in).
Error importCdxml(const std::string& bytes, Document& document,
                  CdxmlImportReport* report = 0,
                  const CdxmlImportOptions& options = CdxmlImportOptions());

Error importCdxmlFile(const std::string& path, Document& document,
                      CdxmlImportReport* report = 0,
                      const CdxmlImportOptions& options = CdxmlImportOptions());

// Export the subset this module can write. Documented as LOSSY for anything the
// adapter did not model, and deliberately not called a round trip.
Error exportCdxml(const Document& document, std::ostream& output);
Error exportCdxml(const Document& document, const std::string& path);

} // namespace core
} // namespace r62

#endif // R62_NEXT_CORE_CDXML_ADAPTER_HPP
