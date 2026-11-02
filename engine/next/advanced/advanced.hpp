#ifndef R62_NEXT_ADVANCED_ADVANCED_HPP
#define R62_NEXT_ADVANCED_ADVANCED_HPP
// =============================================================================
// next/advanced/advanced.hpp  --  T17 umbrella and version handshake
//
// The four main-toolbar commands this module owns:
//   chooseStructurePerspectiveTool        structure perspective
//   chooseFragmentTool_MassFragmentation  explicit, non-predictive fragmentation
//   chooseFragmentTool_Synthesis          reaction inference (native-backed only)
//   chooseFragmentTool_Retrosynthesis     reaction inference (native-backed only)
//
// plus the arrangement / layout services those tools need and the versioned
// asynchronous mechanism that keeps a stale result off a new document.
//
// Include the individual header that matches the job; this umbrella is for
// coverage, the demo and the host hand-off.
// =============================================================================

#include "next/advanced/arrange.hpp"
#include "next/advanced/async.hpp"
#include "next/advanced/cleanup.hpp"
#include "next/advanced/common.hpp"
#include "next/advanced/fragments.hpp"
#include "next/advanced/persist.hpp"
#include "next/advanced/perspective.hpp"

namespace r62 {
namespace advanced {

enum {
    kAdvancedMajor    = 1,
    kAdvancedMinor    = 0,
    kAdvancedRevision = 0,
    kAdvancedVersion  = kAdvancedMajor * 10000 + kAdvancedMinor * 100 + kAdvancedRevision
};

inline bool requestAdvancedCompatibility(int major, int minor) {
    return major == kAdvancedMajor && minor <= kAdvancedMinor;
}

// The T01 version this module compiled against. A host should compare this with
// core::kInterfaceVersion and refuse to wire the tools on a mismatch.
inline int compiledCoreInterfaceVersion() { return core::kInterfaceVersion; }

} // namespace advanced
} // namespace r62

#endif // R62_NEXT_ADVANCED_ADVANCED_HPP
