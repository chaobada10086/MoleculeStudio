#ifndef R62_NEXT_CHEMISTRY_TEMPLATE_HPP
#define R62_NEXT_CHEMISTRY_TEMPLATE_HPP
// =============================================================================
// re_round62_viewer/next/chemistry/template.hpp
//
// T06 - ABBREVIATION EXPAND/CONTRACT AND THE FUNCTIONAL-GROUP TEMPLATE SET.
//
// THE T13 BOUNDARY (read this before adding a template)
// -----------------------------------------------------------------------------
// T13 owns the TEMPLATE SERVICE and its template ID namespace.  T06 must not
// hard-code a parallel set of IDs, because two ID namespaces for the same concept
// drift and then a saved document references the wrong group.
//
// The design is therefore:
//   * AbbreviationTable is DATA keyed by a template id STRING.
//   * ITemplateService is an interface T13 (or a test) implements.
//   * BuiltinAbbreviationTable is the DEFAULT content T06 ships so the module is
//     runnable on its own.  Its ids are the provisional strings T13 is asked to
//     adopt; when T13 provides a real service, callers swap it in and nothing else
//     changes.
//   * InjectedTemplateService adapts a plain list so a test can prove the swap.
//
// So T06 depends on the INTERFACE, and the concrete default is replaceable.  The
// exact ids T06 uses are listed in INTERFACES.md so T13 can confirm or rename them.
//
// EXPAND vs CONTRACT, AND WHY THEY ARE NOT INVERSE STRING OPERATIONS
// -----------------------------------------------------------------------------
// Expanding Ph replaces one atom carrying the label Ph with a six-carbon ring plus
// a bond to the original neighbour.  Contracting that back must find the ring,
// delete its atoms and internal bonds, reconnect the attachment bond to the new
// label atom and restore the Ph label - and it must REFUSE when the subgraph is not
// the shape the template expects.  A blind bond deletion would destroy chemistry
// the user did not ask to touch.  Each template therefore carries an explicit
// CONTRACTION MATCHER: a small, checkable description of the subgraph that may be
// contracted, with a refusal reason.
//
// Expansion is a document edit and MUST go through a transaction so it is one undo
// step (see edit.hpp).  This file only PLANS: it produces an ExpansionPlan of
// "create these atoms/bonds, then delete these" which the caller executes.
// =============================================================================

#include "next/core/contracts.hpp"
#include "next/chemistry/label.hpp"

#include <string>
#include <vector>

namespace r62 {
namespace chemistry {

// ---------------------------------------------------------------------------
// Functional-group template description
// ---------------------------------------------------------------------------
// A template is DATA, not code.  The atoms are placed relative to an attachment
// point in DOCUMENT units and an angle, exactly like a ring template.
struct TemplateAtom {
    int         element;        // 1..118, or 0 for a labelled non-element node
    std::string labelText;      // used when element == 0 (e.g. an inner Ph node)
    double      dx, dy;         // offset from the attachment point, document units
    int         hydrogenCount;  // -1 == compute, else explicit
    int         charge;
    TemplateAtom() : element(6), dx(0.0), dy(0.0), hydrogenCount(-1), charge(0) {}
    TemplateAtom(int e, double x, double y)
        : element(e), dx(x), dy(y), hydrogenCount(-1), charge(0) {}
};

struct TemplateBond {
    int        from;            // index into TemplateAtom, or -1 for the attachment
    int        to;              // index into TemplateAtom
    core::BondOrder order;
    core::BondDisplay display;
    TemplateBond() : from(-1), to(0), order(core::BondOrder::Single),
                     display(core::BondDisplay::Normal) {}
    TemplateBond(int f, int t, core::BondOrder o,
                 core::BondDisplay d = core::BondDisplay::Normal)
        : from(f), to(t), order(o), display(d) {}
};

// Contraction matcher.  Deliberately a small structural test, not a subgraph
// isomorphism engine: a template knows exactly what IT created, and refusing a
// shape it did not create is safer than accepting one it might have.
struct ContractionRule {
    int atomCount;              // number of atoms the template owns
    int bondCount;              // number of bonds among them (excluding attachment)
    bool requireAllSingleBonds; // template internal bonds must all be single
    bool requireNoExtraSubstituents; // no atom may bond outside the group
    bool requireExactRing;      // atomCount atoms form the declared ring size
    std::string refuseReason;   // detail attached to a refusal
    ContractionRule()
        : atomCount(0), bondCount(0), requireAllSingleBonds(false),
          requireNoExtraSubstituents(true), requireExactRing(false) {}
};

struct GroupTemplate {
    std::string  templateId;    // namespaced; owned by T13
    std::string  abbreviation;  // "Ph"
    std::string  expansion;     // canonical formula text, e.g. C6H5
    int          attachElement; // element of the atom the abbreviation sits on
    int          charge;        // net charge of the abbreviated group
    std::vector<TemplateAtom> atoms;
    std::vector<TemplateBond> bonds;
    ContractionRule contraction;
    std::string  source;        // provenance string for coverage.csv
    GroupTemplate() : attachElement(6), charge(0) {}
};

// ---------------------------------------------------------------------------
// Template service interface (T13 boundary)
// ---------------------------------------------------------------------------
class ITemplateService {
public:
    virtual ~ITemplateService() {}
    virtual bool findAbbreviation(const std::string& text, GroupTemplate* out) const = 0;
    virtual bool findTemplate(const std::string& templateId, GroupTemplate* out) const = 0;
    virtual std::vector<AbbreviationEntry> abbreviations() const = 0;
    virtual size_t size() const = 0;
    virtual const char* providerName() const = 0;
};

// ---------------------------------------------------------------------------
// Built-in table (the default provider T06 ships)
// ---------------------------------------------------------------------------
// Carries the abbreviations from the verified ChemDraw atom-hotspot table that are
// STRUCTURAL GROUPS with a real expansion.  Element-symbol hotkeys (c to C, n to N,
// b to Br) are NOT here: those are ELEMENT edits handled by edit.hpp, and putting
// them in the template table would make Shift+c and Cl collide with a group id.
class BuiltinAbbreviationTable : public ITemplateService {
public:
    BuiltinAbbreviationTable();
    virtual bool findAbbreviation(const std::string& text, GroupTemplate* out) const;
    virtual bool findTemplate(const std::string& templateId, GroupTemplate* out) const;
    virtual std::vector<AbbreviationEntry> abbreviations() const;
    virtual size_t size() const { return templates_.size(); }
    virtual const char* providerName() const { return "builtin"; }

private:
    std::vector<GroupTemplate> templates_;
};

// Adapts a caller-supplied vector.  Used by the tests to prove the id namespace is
// genuinely injected and not baked into the interpret logic.
class InjectedTemplateService : public ITemplateService {
public:
    explicit InjectedTemplateService(const std::vector<GroupTemplate>& templates)
        : templates_(templates) {}
    virtual bool findAbbreviation(const std::string& text, GroupTemplate* out) const;
    virtual bool findTemplate(const std::string& templateId, GroupTemplate* out) const;
    virtual std::vector<AbbreviationEntry> abbreviations() const;
    virtual size_t size() const { return templates_.size(); }
    virtual const char* providerName() const { return "injected"; }

private:
    std::vector<GroupTemplate> templates_;
};

// ---------------------------------------------------------------------------
// Expansion plan
// ---------------------------------------------------------------------------
// Pure data: what an expansion WOULD create.  The caller places these through the
// command layer inside ONE transaction, which is what makes expansion a single
// undo step and what gives the expand/contract/undo acceptance test something
// deterministic to check.
struct PlannedAtom {
    int         element;
    std::string labelText;
    core::Vec2  position;
    int         hydrogenCount;
    int         charge;
    PlannedAtom() : element(6), hydrogenCount(-1), charge(0) {}
};

struct PlannedBond {
    int             fromTemplateIndex;  // -1 == the existing attachment atom
    int             toTemplateIndex;
    core::ObjectId  existingAtom;       // used when fromTemplateIndex == -1
    core::BondOrder order;
    core::BondDisplay display;
    PlannedBond() : fromTemplateIndex(-1), toTemplateIndex(0),
                    existingAtom(core::kInvalidObjectId),
                    order(core::BondOrder::Single),
                    display(core::BondDisplay::Normal) {}
};

struct ExpansionPlan {
    std::string              templateId;
    std::string              abbreviation;
    core::ObjectId           attachmentAtom;
    std::vector<PlannedAtom> atoms;     // in template index order
    std::vector<PlannedBond> bonds;
    int                      labelAtomIndex;  // the atom that carries the label
    bool                     ok;
    std::string              error;
    ExpansionPlan() : attachmentAtom(core::kInvalidObjectId),
                      labelAtomIndex(0), ok(false) {}
};

// Plan an expansion of the abbreviation on attachmentAtom.  directionDeg is the
// angle the group grows along in the core::direction() convention; bondLength is
// the document bond length so the plan is scale-correct.
ExpansionPlan planExpansion(const ITemplateService& service,
                            const std::string& abbreviation,
                            core::ObjectId attachmentAtom,
                            double directionDeg,
                            double bondLength);

// ---------------------------------------------------------------------------
// Contraction matching
// ---------------------------------------------------------------------------
// A description of the CURRENT subgraph, supplied by the caller from the document,
// because this module does not hold a Document.  The caller gathers it with a
// bounded neighbourhood walk from the attachment atom.
struct SubgraphAtom {
    core::ObjectId id;
    int            element;
    std::string    labelText;
    int            externalBondCount;  // bonds leaving the candidate group
    SubgraphAtom() : id(core::kInvalidObjectId), element(6), externalBondCount(0) {}
};

struct SubgraphBond {
    core::ObjectId from;
    core::ObjectId to;
    int            order;
    SubgraphBond() : from(core::kInvalidObjectId), to(core::kInvalidObjectId),
                    order(1) {}
};

struct Subgraph {
    std::vector<SubgraphAtom> atoms;
    std::vector<SubgraphBond> bonds;
    std::vector<core::ObjectId> attachments;
};

struct ContractionMatch {
    bool         ok;
    std::string  templateId;
    std::string  abbreviation;
    std::vector<core::ObjectId> atomsToDelete;
    std::vector<core::ObjectId> bondsToDelete;
    core::ObjectId labelAtom;
    core::ObjectId attachmentAtom;
    std::string  refuseReason;
    ContractionMatch() : ok(false), labelAtom(core::kInvalidObjectId),
                         attachmentAtom(core::kInvalidObjectId) {}
};

// Test whether the abbreviation may be contracted from the supplied subgraph.
// REFUSES (ok == false with a reason) when the shape does not match, when an atom
// of the group has an extra substituent, or when a bond order differs.
ContractionMatch planContraction(const ITemplateService& service,
                                 const std::string& abbreviation,
                                 const Subgraph& subgraph);

} // namespace chemistry
} // namespace r62

#endif // R62_NEXT_CHEMISTRY_TEMPLATE_HPP
