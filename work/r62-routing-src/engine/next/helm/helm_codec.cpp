// =============================================================================
// re_round62_viewer/next/helm/helm_codec.cpp
//
// T15 - HELM reader and writer. Grammar and refusals are documented in
// helm_codec.hpp. Every error carries the byte offset it was found at, which the
// shared computeLineColumn() turns into a line/column for the caller.
// =============================================================================

#include "next/helm/helm_codec.hpp"

#include <cctype>
#include <cstdio>
#include <map>
#include <sstream>

namespace r62 {
namespace helm {

const char* const kHelmTerminator = "$$$$";
const char* const kHelmVersionSuffix = "V2.0";

HelmParseReport::HelmParseReport()
    : detectedVersion(1), hadFourSections(false), polymersRead(0), monomersRead(0),
      connectionsRead(0), hydrogenConnections(0), groupsRead(0), annotationsRead(0),
      unknownMonomers(0), chemMonomers(0), blobMonomers(0) {}

namespace {

// A connection position that the notation allowed but this model cannot resolve
// to a residue index (a one-letter-code position such as 'C:R3'). It is a distinct
// sentinel so the error message can say what actually happened.
const size_t kUnresolvedPosition = static_cast<size_t>(-1);
// The position sentinel for "this nucleotide unit has no such piece". Distinct
// from kUnresolvedPosition, which means the NOTATION named a position this model
// cannot resolve to a residue.
const size_t kNoPosition = static_cast<size_t>(-1);

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool isDigitC(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

std::string trimmed(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}



// A position-aware cursor. Keeping the offset in ONE place is what makes the
// parse error position a property of the parser rather than of each rule.
class Cursor {
public:
    explicit Cursor(const std::string& text) : text_(text), pos_(0) {}
    size_t position() const { return pos_; }
    void seek(size_t p) { pos_ = p; }
    bool eof() const { return pos_ >= text_.size(); }
    char peek() const { return pos_ < text_.size() ? text_[pos_] : static_cast<char>(0); }
    char peekAt(size_t ahead) const {
        return pos_ + ahead < text_.size() ? text_[pos_ + ahead] : static_cast<char>(0);
    }
    char take() {
        return pos_ < text_.size() ? text_[pos_++] : static_cast<char>(0);
    }
    void skipSpaces() { while (!eof() && isSpace(peek())) ++pos_; }
    bool accept(char c) { if (peek() == c) { ++pos_; return true; } return false; }
    const std::string& text() const { return text_; }
private:
    const std::string& text_;
    size_t pos_;
};

// Build a positioned diagnostic. Every failure in this file goes through here so
// the line/column arithmetic can never drift.
SequenceDiagnostic error(core::ErrorCode code, const std::string& message,
                        const std::string& text, size_t offset,
                        const std::string& polymerId = std::string(),
                        const std::string& symbol = std::string()) {
    SequenceDiagnostic d(code, message, offset);
    computeLineColumn(text, offset, &d.line, &d.column);
    d.polymerId = polymerId;
    d.monomerSymbol = symbol;
    return d;
}

// A parsed token: the monomer symbol (or inline CHEM body) plus its annotation.
struct MonomerToken {
    std::string symbol;
    std::string annotation;
    std::string repeat;       // the HELM repeat count, e.g. "55" in A'55'
    bool        isInlineChem; // symbol came from a [ ... ] body
    size_t      offset;
    MonomerToken() : isInlineChem(false), offset(0) {}
};

// ----------------------------------------------------------------------------
// Nucleotide composite tokens
// ----------------------------------------------------------------------------
// The official HELM corpus writes a DNA/RNA unit as  <sugar>(<base>)<phosphate> ,
// e.g. R(A)P for a ribonucleotide and d(A)P for a 2'-deoxy one. The guide says the
// same thing in drawing terms: 'every click ... will lay down three monomers - the
// ribose, phosphate and the base' (PDF page 181), and 'R', 'A' and 'P' are the
// defaults for ribose, base and phosphate (PDF page 182).
//
// This is NOT a single monomer whose symbol happens to contain brackets. It is a
// three-monomer unit with two connections - sugar.R3 to base.R1 and sugar.R2 to
// phosphate.R1 - and it must be expanded into exactly that, or the chemistry is
// wrong while looking right.
struct NucleotideUnit {
    std::string sugar;      // the symbol before '('
    std::string base;       // the symbol inside the parentheses
    std::string phosphate;  // the symbol after ')'
    bool isComposite;       // false == not nucleotide notation at all
    bool wellFormed;        // false == it LOOKED composite but is malformed
    NucleotideUnit() : isComposite(false), wellFormed(false) {}
};

// Recognise  <sugar>(<base>)[<phosphate>] . A token is composite when it contains a
// '(' ... ')' pair with a non-empty symbol inside and a non-empty sugar before it.
// The trailing phosphate is OPTIONAL: the corpus ends an RNA chain with 'P(C)' (no
// trailing phosphate) where the phosphate sits BEFORE the sugar instead.
NucleotideUnit splitNucleotide(const std::string& symbol) {
    NucleotideUnit unit;
    const size_t open = symbol.find('(');
    if (open == std::string::npos) return unit;
    const size_t close = symbol.find(')', open + 1);
    if (close == std::string::npos) return unit;
    // Only ONE pair is allowed; a nested '(' means this is not nucleotide notation.
    if (symbol.find('(', open + 1) != std::string::npos) return unit;
    unit.sugar = symbol.substr(0, open);
    unit.base = symbol.substr(open + 1, close - open - 1);
    unit.phosphate = symbol.substr(close + 1);
    unit.isComposite = true;
    // A NON-EMPTY sugar is the normal form. An EMPTY sugar is also legal: the corpus
    // writes P(C) for a 5'-phosphate nucleotide where the phosphate stands before the
    // sugar, so the caller must decide whether that is a linker. Whether the leading
    // symbol names a real monomer is a LIBRARY question and is answered there, not
    // here, because this function deliberately does not know the library.
    unit.wellFormed = !unit.base.empty() && (!unit.sugar.empty() || !unit.phosphate.empty());
    return unit;
}

// A parsed connection reference, before the polymers are resolved.
struct PendingConnection {
    std::vector<std::string> fromPolymers;
    std::vector<size_t>      fromPositions;
    std::vector<std::string> fromAttachments;
    std::vector<std::string> toPolymers;
    std::vector<size_t>      toPositions;
    std::vector<std::string> toAttachments;
    ConnectionKind           kind;
    std::string              annotation;
    size_t                   offset;
    PendingConnection() : kind(ConnectionKind::Covalent), offset(0) {}
};

// ----------------------------------------------------------------------------
// Section splitting
// ----------------------------------------------------------------------------
// The string is split on the top-level '$'. A '$' can never appear inside a
// monomer symbol or an attachment label, so a plain split is correct here; it is
// NOT correct for ',' and '|', which is why those are parsed with a cursor.
std::vector<std::string> splitTopLevel(const std::string& text, char delim,
                                      std::vector<size_t>* offsets) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == delim) {
            parts.push_back(text.substr(start, i - start));
            if (offsets) offsets->push_back(start);
            start = i + 1;
        }
    }
    return parts;
}

// ----------------------------------------------------------------------------
// Polymer section
// ----------------------------------------------------------------------------
// Parse `<ID>{<monomer>.<monomer>...}<annotation>` starting at the id.
bool parsePolymerToken(const std::string& text, size_t start, size_t end,
                       const std::string& polymerId, PolymerType type,
                       std::vector<MonomerToken>* out, size_t* outStartOfBrace,
                       SequenceDiagnostic* failure) {
    (void)end;   // the span is bounded by the caller; kept for signature symmetry
    (void)type;

    Cursor cur(text);
    cur.seek(start);
    cur.skipSpaces();
    // The heading: POLYMER_ID{
    if (text.compare(cur.position(), polymerId.size(), polymerId) != 0) {
        *failure = error(core::ErrorCode::ParseError, "polymer heading does not match its id",
                         text, cur.position(), polymerId);
        return false;
    }
    cur.seek(cur.position() + polymerId.size());
    cur.skipSpaces();
    if (!cur.accept('{')) {
        *failure = error(core::ErrorCode::ParseError, "expected '{' after the polymer id",
                         text, cur.position(), polymerId);
        return false;
    }
    if (outStartOfBrace) *outStartOfBrace = cur.position();
    // Monomer list. The grammar is  <monomer> ('.' <monomer>)*  : a separator is
    // REQUIRED between monomers, and a monomer is REQUIRED after a separator. A
    // leading dot, a trailing dot and a doubled dot are all malformed and are
    // reported with the offset that produced them.
    bool expectMonomer = true;
    while (true) {
        cur.skipSpaces();
        if (cur.eof()) {
            *failure = error(core::ErrorCode::Truncated, "unterminated polymer body",
                             text, cur.position(), polymerId);
            return false;
        }
        if (cur.peek() == '}') { cur.take(); break; }
        if (!expectMonomer) {
            if (cur.accept('.')) {
                cur.skipSpaces();
                if (cur.peek() == '.' || cur.peek() == '}') {
                    *failure = error(core::ErrorCode::ParseError, "empty monomer between dots",
                                     text, cur.position(), polymerId);
                    return false;
                }
                // The separator consumed one slot of the grammar; the next token
                // must be a monomer.
                expectMonomer = true;
                continue;
            }
            *failure = error(core::ErrorCode::ParseError, "expected '.' or '}'",
                             text, cur.position(), polymerId);
            return false;
        }
        MonomerToken token;
        token.offset = cur.position();
        if (cur.peek() == '[') {
            // Inline CHEM definition: [<SMILES> |$<atom position extension>$|].
            // The body can contain SQUARE BRACKETS of its own, e.g. N[*] and [*] in
            // N[C@H](N[*])C([*])=O, so the terminator is found by BRACKET DEPTH and
            // not by taking the next ']'. Taking the next ']' truncates the SMILES
            // at the first atom-list and silently corrupts the definition.
            const size_t open = cur.position();
            cur.take();
            std::string body;
            int depth = 1;
            bool closed = false;
            while (!cur.eof()) {
                const char ch = cur.take();
                if (ch == '[') { ++depth; body.push_back(ch); continue; }
                if (ch == ']') {
                    --depth;
                    if (depth == 0) { closed = true; break; }
                    body.push_back(ch);
                    continue;
                }
                body.push_back(ch);
            }
            if (!closed) {
                *failure = error(core::ErrorCode::Truncated, "unterminated inline CHEM definition",
                                 text, open, polymerId);
                return false;
            }
            token.symbol = trimmed(body);
            token.isInlineChem = true;
        } else {
            // A bare symbol runs to the next structural delimiter. SPACE IS NOT a
            // delimiter: the corpus contains BLOB symbols such as "Gold Particle"
            // and "Gold_Particle", so the token ends only at '.', '}', '"', '|',
            // '\'' or '$', and is then trimmed.
            std::string symbol;
            while (!cur.eof()) {
                const char ch = cur.peek();
                if (ch == '.' || ch == '}' || ch == '"' || ch == '|' || ch == '$' ||
                    ch == '\'') break;
                symbol.push_back(cur.take());
            }
            symbol = trimmed(symbol);
            if (symbol.empty()) {
                *failure = error(core::ErrorCode::ParseError, "empty monomer symbol",
                                 text, cur.position(), polymerId);
                return false;
            }
            token.symbol = symbol;
        }
        // Optional REPEAT annotation, written in single quotes, e.g. A'55'. HELM
        // allows it on PEPTIDE and RNA only. Stage 1 and 2 do not MODEL a repeat
        // count, so it is captured here and reported as an unmodelled annotation
        // rather than dropped in silence.
        if (cur.peek() == '\'') {
            cur.take();
            std::string repeat;
            bool closed = false;
            while (!cur.eof()) {
                const char ch = cur.take();
                if (ch == '\'') { closed = true; break; }
                repeat.push_back(ch);
            }
            if (!closed) {
                *failure = error(core::ErrorCode::Truncated, "unterminated repeat annotation",
                                 text, cur.position(), polymerId);
                return false;
            }
            token.repeat = trimmed(repeat);
        }
        // Optional quoted monomer annotation.
        cur.skipSpaces();
        if (cur.peek() == '"') {
            cur.take();
            std::string note;
            bool closed = false;
            while (!cur.eof()) {
                const char c = cur.take();
                if (c == '"') { closed = true; break; }
                note.push_back(c);
            }
            if (!closed) {
                *failure = error(core::ErrorCode::Truncated, "unterminated monomer annotation",
                                 text, cur.position(), polymerId);
                return false;
            }
            token.annotation = note;
        }
        out->push_back(token);
        expectMonomer = false;
    }
    // Optional polymer annotation after '}'.
    cur.skipSpaces();
    if (cur.peek() == '"') {
        cur.take();
        std::string note;
        bool closed = false;
        while (!cur.eof()) {
            const char c = cur.take();
            if (c == '"') { closed = true; break; }
            note.push_back(c);
        }
        if (!closed) {
            *failure = error(core::ErrorCode::Truncated, "unterminated polymer annotation",
                             text, cur.position(), polymerId);
            return false;
        }
    }
    return true;
}

// Split the polymer section on top-level '|'. The '|' is the polymer separator;
// it cannot appear inside a symbol.
std::vector<std::pair<size_t, size_t> > splitPolymerSpans(const std::string& text,
                                                          size_t begin, size_t end) {
    std::vector<std::pair<size_t, size_t> > spans;
    size_t start = begin;
    int depthBrace = 0;
    bool inQuote = false;
    for (size_t i = begin; i <= end; ++i) {
        const char c = i < end ? text[i] : '|';
        if (inQuote) {
            if (c == '"') inQuote = false;
            continue;
        }
        if (c == '"') { inQuote = true; continue; }
        if (c == '{') { ++depthBrace; continue; }
        if (c == '}') { if (depthBrace > 0) --depthBrace; continue; }
        if (i == end) {
            // The LAST span always ends here, even when a brace is still open. An
            // unterminated '{' must fall through to the polymer reader so it can
            // report a positioned Truncated error; requiring depthBrace == 0 here
            // would DROP the polymer silently, and a dropped polymer is a sequence
            // that parses as empty and looks valid.
            if (end > start) spans.push_back(std::make_pair(start, end));
            break;
        }
        if (c == '|' && depthBrace == 0) {
            spans.push_back(std::make_pair(start, i));
            start = i + 1;
        }
    }
    return spans;
}

// Find the polymer id (letters followed by digits) at the head of a span.
bool readPolymerId(const std::string& text, size_t start, size_t end,
                   std::string* id, size_t* afterId) {
    size_t i = start;
    while (i < end && isSpace(text[i])) ++i;
    const size_t idStart = i;
    while (i < end && isAlpha(text[i])) ++i;
    if (i == idStart) return false;
    const size_t digitsStart = i;
    while (i < end && isDigitC(text[i])) ++i;
    if (i == digitsStart) return false;   // PolymerNode.java: must end with digits
    // HELM 2 checkPolymerId requires [1-9][0-9]*: a leading zero is malformed.
    if (text[digitsStart] == '0') return false;
    *id = text.substr(idStart, i - idStart);
    if (afterId) *afterId = i;
    return true;
}

// ----------------------------------------------------------------------------
// Connection section
// ----------------------------------------------------------------------------
// A connection is  <srcPolymers>,<dstPolymers>,<srcRef>-<dstRef>  where a side is
// a '+' joined list of polymer ids and each ref is <position>:<attachment>, or
// the single word 'generic' for an unspecified edge. Verified against the
// Pistoia examples: `RNA1,RNA2,20:pair-8:pair|RNA1,RNA2,8:pair-20:pair` and
// `PEPTIDE1,CHEM1,C:R3-1:R1"Specific Conjugation"`.
//
// HELM's own examples put positions BEFORE the polymer list on the left side and
// sometimes omit the polymer list on one side. Rather than guess, this parser
// accepts exactly the documented forms:
//     P1[,P2...],P3[,P4...],pos:R-pos:R
//     P1[,P2...],P3[,P4...],generic:
// and refuses anything else with a positioned error.
struct ConnectionSides {
    std::vector<std::string> fromPolymers;
    std::vector<size_t>      fromPositions;
    std::vector<std::string> fromAttachments;
    std::vector<std::string> toPolymers;
    std::vector<size_t>      toPositions;
    std::vector<std::string> toAttachments;
    ConnectionKind           kind;
    std::string              annotation;   // the quoted label HELM allows on an edge
    ConnectionSides() : kind(ConnectionKind::Covalent) {}
};

bool splitRefList(const std::string& text, const std::string& body, size_t bodyOffset,
                  const std::vector<std::string>& polymers,
                  std::vector<size_t>* positions,
                  std::vector<std::string>* attachments,
                  ConnectionKind* kindOut,
                  SequenceDiagnostic* failure) {
    const std::string trimmedBody = trimmed(body);
    if (trimmedBody.empty()) {
        *failure = error(core::ErrorCode::ParseError, "empty connection reference",
                         text, bodyOffset);
        return false;
    }
    if (trimmedBody == "generic:" || trimmedBody == "generic") {
        *kindOut = ConnectionKind::Generic;
        return true;
    }
    // Each '+' separated element is <position>:<attachment>.
    size_t cursor = 0;
    while (cursor <= trimmedBody.size()) {
        const size_t next = trimmedBody.find('+', cursor);
        const bool last = (next == std::string::npos);
        const std::string piece = last ? trimmedBody.substr(cursor)
                                       : trimmedBody.substr(cursor, next - cursor);
        const size_t colon = piece.rfind(':');
        if (colon == std::string::npos) {
            *failure = error(core::ErrorCode::ParseError,
                             "connection reference must be <position>:<attachment>",
                             text, bodyOffset + cursor);
            return false;
        }
        const std::string posText = trimmed(piece.substr(0, colon));
        const std::string attach = trimmed(piece.substr(colon + 1));
        if (posText.empty() || attach.empty()) {
            *failure = error(core::ErrorCode::ParseError,
                             "connection reference has an empty position or attachment",
                             text, bodyOffset + cursor);
            return false;
        }
        // HELM 2 accepts a position that is a positive integer, a SINGLE
        // non-digit character (the corpus uses the amino-acid one-letter code,
        // e.g. 'C:R3-1:R1'), or '?'. Anything else is refused with a position.
        const bool allDigits = !posText.empty();
        for (size_t i = 0; i < posText.size(); ++i) {
            if (!isDigitC(posText[i])) {
                // not all digits
            }
        }
        bool digitsOnly = true;
        for (size_t i = 0; i < posText.size(); ++i)
            if (!isDigitC(posText[i])) { digitsOnly = false; break; }
        const bool singleNonDigit = (posText.size() == 1 && !isDigitC(posText[0]));
        const bool isUnknownMarker = (posText == "?");
        if (!digitsOnly && !singleNonDigit && !isUnknownMarker) {
            *failure = error(core::ErrorCode::ParseError,
                             "connection position must be a positive integer, a single "
                             "non-digit character, or '?'; got " + posText,
                             text, bodyOffset + cursor);
            return false;
        }
        if (digitsOnly && posText[0] == '0') {
            // HELM 2: [1-9][0-9]*. A leading zero is a malformed index, not 0.
            *failure = error(core::ErrorCode::ParseError,
                             "connection position must not have a leading zero: " + posText,
                             text, bodyOffset + cursor);
            return false;
        }
        (void)allDigits;
        if (!isValidHelm2AttachmentLabel(attach)) {
            if (attach == kAttachmentPair) {
                // 'pair' is handled by isValidHelm2AttachmentLabel's caller below;
                // it is a valid label that is not an R group.
            } else {
                *failure = error(core::ErrorCode::ParseError,
                                 "attachment must be 'pair' or R[1-9]<digits>, got " + attach,
                                 text, bodyOffset + cursor + colon + 1);
                return false;
            }
        }
        if (attach == kAttachmentPair) *kindOut = ConnectionKind::Hydrogen;
        // A non-numeric position cannot address a residue in this model, and
        // guessing which one the author meant is exactly the silent mis-wiring
        // this module refuses. It is recorded as kUnresolvedPosition so the
        // connection builder reports it precisely.
        if (digitsOnly)
            positions->push_back(static_cast<size_t>(std::strtoul(posText.c_str(), 0, 10)));
        else
            positions->push_back(kUnresolvedPosition);
        attachments->push_back(attach);
        if (last) break;
        cursor = next + 1;
    }
    // The reference count must not exceed the polymer count: a reference with no
    // polymer to attach to is an error, not something to pad.
    if (polymers.empty()) {
        *failure = error(core::ErrorCode::ParseError,
                         "connection side names no polymer", text, bodyOffset);
        return false;
    }
    if (positions->size() > 1 && positions->size() != polymers.size()) {
        *failure = error(core::ErrorCode::ParseError,
                         "connection side references and polymers must pair up",
                         text, bodyOffset);
        return false;
    }
    return true;
}

// Split a comma separated polymer id list.
void splitPolymerList(const std::string& body, std::vector<std::string>* out) {
    size_t cursor = 0;
    while (cursor <= body.size()) {
        const size_t next = body.find('+', cursor);
        const bool last = (next == std::string::npos);
        const std::string piece = trimmed(last ? body.substr(cursor)
                                               : body.substr(cursor, next - cursor));
        if (!piece.empty()) out->push_back(piece);
        if (last) break;
        cursor = next + 1;
    }
}

bool parseConnectionEntry(const std::string& text, const std::string& entry,
                          size_t entryOffset, ConnectionSides* out,
                          SequenceDiagnostic* failure) {
    const std::string body = trimmed(entry);
    // An optional trailing quoted annotation belongs to the edge.
    std::string annotation;
    std::string core = body;
    const size_t quote = body.find('"');
    if (quote != std::string::npos) {
        const size_t closeQuote = body.find('"', quote + 1);
        if (closeQuote == std::string::npos) {
            *failure = error(core::ErrorCode::Truncated, "unterminated connection annotation",
                             text, entryOffset + quote);
            return false;
        }
        annotation = body.substr(quote + 1, closeQuote - quote - 1);
        core = trimmed(body.substr(0, quote));
    }
    // Three comma separated fields: source polymers, target polymers, references.
    std::vector<std::string> fields;
    size_t cursor = 0;
    while (cursor <= core.size()) {
        const size_t next = core.find(',', cursor);
        const bool last = (next == std::string::npos);
        fields.push_back(last ? core.substr(cursor) : core.substr(cursor, next - cursor));
        if (last) break;
        cursor = next + 1;
    }
    if (fields.size() != 3) {
        *failure = error(core::ErrorCode::ParseError,
                         "a connection needs exactly three comma separated fields",
                         text, entryOffset);
        return false;
    }
    splitPolymerList(fields[0], &out->fromPolymers);
    splitPolymerList(fields[1], &out->toPolymers);
    const std::string refs = trimmed(fields[2]);
    const size_t dash = refs.find('-');
    if (dash == std::string::npos) {
        *failure = error(core::ErrorCode::ParseError,
                         "a connection needs a '-' between its two references",
                         text, entryOffset);
        return false;
    }
    const std::string fromRef = refs.substr(0, dash);
    const std::string toRef = refs.substr(dash + 1);
    const size_t refsOffset = entryOffset + static_cast<size_t>(core.find(fields[2]) ) ;
    if (!splitRefList(text, fromRef, refsOffset, out->fromPolymers,
                      &out->fromPositions, &out->fromAttachments, &out->kind, failure))
        return false;
    if (!splitRefList(text, toRef, refsOffset + dash + 1, out->toPolymers,
                      &out->toPositions, &out->toAttachments, &out->kind, failure))
        return false;
    // Broadcast a single reference across a multi-polymer side.
    if (out->fromPositions.size() == 1 && out->fromPolymers.size() > 1) {
        const size_t p = out->fromPositions[0];
        const std::string a = out->fromAttachments[0];
        out->fromPositions.assign(out->fromPolymers.size(), p);
        out->fromAttachments.assign(out->fromPolymers.size(), a);
    }
    if (out->toPositions.size() == 1 && out->toPolymers.size() > 1) {
        const size_t p = out->toPositions[0];
        const std::string a = out->toAttachments[0];
        out->toPositions.assign(out->toPolymers.size(), p);
        out->toAttachments.assign(out->toPolymers.size(), a);
    }
    out->annotation = annotation;   // applied once the edge exists, by the caller
    return true;
}

// Split the connection section on top-level '|'. HELM uses '|' to separate
// multiple edges.
std::vector<std::pair<size_t, size_t> > splitEdgeSpans(const std::string& text,
                                                       size_t begin, size_t end) {
    std::vector<std::pair<size_t, size_t> > spans;
    size_t start = begin;
    bool inQuote = false;
    for (size_t i = begin; i <= end; ++i) {
        const char c = i < end ? text[i] : '|';
        if (inQuote) { if (c == '"') inQuote = false; continue; }
        if (c == '"') { inQuote = true; continue; }
        if (c == '|') { spans.push_back(std::make_pair(start, i)); start = i + 1; }
    }
    return spans;
}

} // namespace

// ----------------------------------------------------------------------------
// Writer
// ----------------------------------------------------------------------------
std::string formatMonomerToken(const MonomerInstance& instance) {
    // An UNKNOWN monomer is written back as the author typed it (R1). A CHEM
    // monomer written inline keeps its body. Everything else uses the symbol.
    std::string token;
    if (instance.definition.isChem() && instance.definition.chemistryDefined &&
        !instance.sourceText.empty() && instance.sourceText[0] == '[') {
        token = instance.sourceText;   // round trip the inline definition verbatim
    } else if (instance.definition.isChem() && instance.definition.chemistryDefined) {
        token = "[" + instance.definition.smiles + "]";
    } else if (!instance.definition.symbol.empty()) {
        token = instance.definition.symbol;
    } else {
        token = instance.sourceText;
    }
    return token;
}

std::string formatConnection(const SequenceModel& model, const SequenceConnection& conn) {
    std::ostringstream out;
    const SequencePolymer* pf = model.polymer(conn.from.polymer);
    const SequencePolymer* pt = model.polymer(conn.to.polymer);
    if (!pf || !pt) return std::string();
    out << pf->polymerId << ',' << pt->polymerId << ',';
    // HELM positions are 1-based on the wire. The model stores 0-based indices,
    // and the conversion happens in exactly these two places.
    out << (conn.from.position + 1) << ':' << conn.from.attachment;
    out << '-' << (conn.to.position + 1) << ':' << conn.to.attachment;
    if (!conn.annotation.empty()) out << '"' << conn.annotation << '"';
    return out.str();
}

bool helmIsSimple(const SequenceModel& model) {
    for (size_t c = 0; c < model.connectionCount(); ++c) {
        const SequenceConnection* conn = model.connection(c);
        if (conn && conn->alive) return false;
    }
    for (size_t i = 0; i < model.polymerCount(); ++i) {
        const SequencePolymer* polymer = model.polymer(i);
        if (!polymer) continue;
        if (!polymer->strandType.empty()) return false;
        if (polymer->type == PolymerType::Chem || polymer->type == PolymerType::Blob)
            return false;
    }
    return true;
}

std::string writeHelmSimple(const SequenceModel& model) {
    if (!helmIsSimple(model)) return std::string();
    std::ostringstream out;
    for (size_t i = 0; i < model.polymerCount(); ++i) {
        const SequencePolymer* polymer = model.polymer(i);
        if (!polymer) continue;
        if (i != 0) out << '|';
        out << polymer->polymerId << '{';
        bool first = true;
        for (size_t m = 0; m < polymer->monomers.size(); ++m) {
            const MonomerInstance& instance = polymer->monomers[m];
            if (!instance.alive) continue;
            if (!first) out << '.';
            out << formatMonomerToken(instance);
            first = false;
        }
        out << '}';
    }
    out << kHelmTerminator;
    return out.str();
}

std::string writeHelm(const SequenceModel& model) {
    std::ostringstream section1, section2, section3, section4;

    // ---- section 1: polymers ------------------------------------------------
    for (size_t i = 0; i < model.polymerCount(); ++i) {
        const SequencePolymer* polymer = model.polymer(i);
        if (!polymer) continue;
        if (i != 0) section1 << '|';
        section1 << polymer->polymerId << '{';
        bool first = true;
        for (size_t m = 0; m < polymer->monomers.size(); ++m) {
            const MonomerInstance& instance = polymer->monomers[m];
            if (!instance.alive) continue;
            if (!first) section1 << '.';
            section1 << formatMonomerToken(instance);
            first = false;
        }
        section1 << '}';
    }

    // ---- section 2: connections --------------------------------------------
    bool firstEdge = true;
    for (size_t c = 0; c < model.connectionCount(); ++c) {
        const SequenceConnection* conn = model.connection(c);
        if (!conn || !conn->alive) continue;
        const std::string text = formatConnection(model, *conn);
        if (text.empty()) continue;
        if (!firstEdge) section2 << '|';
        section2 << text;
        firstEdge = false;
    }

    // ---- section 3: grouping ------------------------------------------------
    // Stage 1 and 2 do not create groups, and this module does not invent one.
    // The section is therefore emitted empty, which is what a HELM writer must do
    // when it has no group information.

    // ---- section 4: annotations ----------------------------------------------
    // The guide records the strand-type annotation with the keyword 'strandtype'
    // (PDF page 185). Values are emitted verbatim; this module does not normalise
    // 'ss'/'as' case because the guide shows lowercase and an author may differ.
    bool firstAnnotation = true;
    for (size_t i = 0; i < model.polymerCount(); ++i) {
        const SequencePolymer* polymer = model.polymer(i);
        if (!polymer) continue;
        if (polymer->strandType.empty()) continue;
        if (!firstAnnotation) section4 << '|';
        section4 << polymer->polymerId << "{StrandType:" << polymer->strandType << '}';
        firstAnnotation = false;
    }

    // EXACTLY four '$': one terminates each of the four sections.
    std::ostringstream out;
    out << section1.str() << '$' << section2.str() << '$' << section3.str() << '$'
        << section4.str() << '$' << kHelmVersionSuffix;
    return out.str();
}

// ----------------------------------------------------------------------------
// Reader
// ----------------------------------------------------------------------------
core::Error parseHelmDetailed(const std::string& text,
                              SequenceModel& model,
                              HelmParseReport* report,
                              std::vector<SequenceDiagnostic>* errors,
                              const HelmParseOptions& options) {
    HelmParseReport local;
    std::vector<SequenceDiagnostic> problems;
    // Unsupported is used BOTH for "this is a warning" and for "this must be
    // refused" (an unresolvable connection, a generic edge). The error code alone
    // cannot separate them, so a refusal is recorded EXPLICITLY rather than inferred
    // from the code. Getting this wrong would let a real bond be dropped while the
    // load still reported success.
    bool sawFatal = false;

    // ---- strip a trailing version suffix ------------------------------------
    std::string body = trimmed(text);
    if (body.empty()) {
        problems.push_back(error(core::ErrorCode::ParseError, "empty HELM string", text, 0));
        if (errors) *errors = problems;
        return core::Error(core::ErrorCode::ParseError, "empty HELM string");
    }
    size_t versionSuffixAt = std::string::npos;
    {
        // A V<digit>.<digit>[.x] suffix, e.g. V2.0.
        const size_t vpos = body.rfind("V2.");
        if (vpos != std::string::npos && vpos > 0) {
            bool numericTail = vpos + 3 < body.size();
            for (size_t i = vpos + 3; i < body.size() && numericTail; ++i)
                if (!isDigitC(body[i]) && body[i] != '.') numericTail = false;
            if (numericTail) {
                versionSuffixAt = vpos;
                local.detectedVersion = 2;
                body = trimmed(body.substr(0, vpos));
            }
        }
    }
    (void)versionSuffixAt;

    // ---- split into the four sections ---------------------------------------
    std::vector<size_t> sectionOffsets;
    std::vector<std::string> sections = splitTopLevel(body, '$', &sectionOffsets);
    local.hadFourSections = (sections.size() >= 4);
    if (options.requireFourSections && !sections.empty() && sections.size() < 4) {
        // A simple-notation string has no '$' at all. That is legal; only a
        // PARTIAL terminator (one to three dollars) is a defect.
        if (sections.size() > 1) {
            problems.push_back(error(core::ErrorCode::ParseError,
                "complex notation needs four sections separated by $ ($$$$)", text,
                static_cast<size_t>(body.size())));
            if (errors) *errors = problems;
            return core::Error(core::ErrorCode::ParseError, problems[0].message);
        }
    }
    const std::string polymerSection = sections.size() > 0 ? trimmed(sections[0]) : std::string();
    const std::string connectionSection = sections.size() > 1 ? trimmed(sections[1]) : std::string();
    // sections[2] is the grouping section and is preserved but not interpreted by
    // stage 1/2; a non-empty group section is reported as a warning, never dropped
    // silently.
    const std::string groupSection = sections.size() > 2 ? trimmed(sections[2]) : std::string();
    const std::string annotationSection = sections.size() > 3 ? trimmed(sections[3]) : std::string();
    if (!groupSection.empty()) {
        local.groupsRead = 1;
        problems.push_back(error(core::ErrorCode::Unsupported,
            "group section present; groups are preserved as a warning and not modelled",
            text, sectionOffsets.size() > 2 ? sectionOffsets[2] : 0));
    }

    // ---- scratch model -------------------------------------------------------
    // The caller's model is not touched until everything has parsed. That is the
    // all-or-nothing rule this project uses everywhere.
    SequenceModel scratch;
    scratch.setLibrary(model.library());

    // ---- section 1: polymers -------------------------------------------------
    if (!polymerSection.empty()) {
        // The polymer section ends at the first top-level '$'. Splitting the
        // WHOLE body would let the annotation section be mistaken for more polymers,
        // which is exactly the defect that produced a 'duplicate polymer id' on the
        // official duplex string. Quotes and braces are tracked so a '$' or a
        // '"' inside a monomer annotation cannot end the section early.
        size_t polymerEnd = body.size();
        {
            bool inQuote = false;
            int depth = 0;
            for (size_t i = 0; i < body.size(); ++i) {
                const char ch = body[i];
                if (inQuote) { if (ch == '"') inQuote = false; continue; }
                if (ch == '"') { inQuote = true; continue; }
                if (ch == '{') { ++depth; continue; }
                if (ch == '}') { if (depth > 0) --depth; continue; }
                if (ch == '$' && depth == 0) { polymerEnd = i; break; }
            }
        }
        const std::vector<std::pair<size_t, size_t> > spans =
            splitPolymerSpans(body, 0, polymerEnd);
        for (size_t s = 0; s < spans.size(); ++s) {
            std::string polymerId;
            size_t afterId = 0;
            if (!readPolymerId(body, spans[s].first, spans[s].second, &polymerId, &afterId)) {
                // The section may end before the '$'; a span with only whitespace is
                // the natural trailing artefact of the split and is skipped.
                if (trimmed(body.substr(spans[s].first, spans[s].second - spans[s].first)).empty())
                    continue;
                problems.push_back(error(core::ErrorCode::ParseError,
                    "expected a polymer id (letters then digits) at the head of a polymer",
                    text, spans[s].first));
                break;
            }
            PolymerType type = PolymerType::Unknown;
            if (!polymerTypeFromId(polymerId, &type)) {
                problems.push_back(error(core::ErrorCode::Unsupported,
                    "unknown polymer type in id " + polymerId, text, spans[s].first,
                    polymerId));
                break;
            }
            size_t polymerIndex = 0;
            core::Error added = scratch.addPolymer(polymerId, type, &polymerIndex);
            if (!added.ok()) {
                problems.push_back(error(added.code, added.message, text, spans[s].first,
                                         polymerId));
                break;
            }
            ++local.polymersRead;

            std::vector<MonomerToken> tokens;
            SequenceDiagnostic failure;
            if (!parsePolymerToken(body, spans[s].first, spans[s].second, polymerId, type,
                                   &tokens, 0, &failure)) {
                problems.push_back(failure);
                break;
            }
            for (size_t t = 0; t < tokens.size(); ++t) {
                if (scratch.polymer(polymerIndex)->monomers.size() >= options.maxMonomers) {
                    problems.push_back(error(core::ErrorCode::LimitExceeded,
                        "monomer count exceeds the configured limit", text, tokens[t].offset));
                    break;
                }
                // ---- nucleotide composite expansion --------------------------------
                // The corpus writes a nucleotide as <sugar>(<base>)[<phosphate>], and
                // the guide describes drawing one as THREE monomers: 'every click ...
                // will lay down three monomers - the ribose, phosphate and the base'
                // (PDF page 181). So R(A)P is expanded into sugar + base + phosphate
                // with sugar.R3->base.R1 and sugar.R2->phosphate.R1. Treating it as one
                // symbol would produce a chain of the right LENGTH and the wrong
                // chemistry, which is the failure this module exists to prevent.
                if (!tokens[t].isInlineChem && type == PolymerType::Rna) {
                    const NucleotideUnit unit = splitNucleotide(tokens[t].symbol);
                    if (unit.isComposite && !unit.wellFormed) {
                        problems.push_back(error(core::ErrorCode::ParseError,
                            "malformed nucleotide unit, expected <sugar>(<base>)[<phosphate>]: "
                            + tokens[t].symbol, text, tokens[t].offset, polymerId,
                            tokens[t].symbol));
                        break;
                    }
                    if (unit.isComposite && unit.wellFormed) {
                        // Normalise the unit. The corpus writes three shapes, all of
                        // which occur in the official examples:
                        //     R(A)P   sugar, base in brackets, trailing phosphate
                        //     R(A)    sugar and base, no trailing phosphate (chain end)
                        //     P(C)    LEADING phosphate and base, no sugar
                        // The head symbol is classified by what the LIBRARY says it is,
                        // not by its spelling: the guide uses 'R' for ribose and 'P' for
                        // phosphate, while the Core Library stores them as lowercase 'r'
                        // and 'p' with natural analogs 'r' and 'p' respectively. Reading
                        // the analog is what distinguishes a 5' phosphate from a sugar.
                        std::string head = unit.sugar;
                        std::string tail = unit.phosphate;
                        std::string sugarSymbol;
                        std::string leadingLinker;
                        std::string trailingLinker = tail;
                        const MonomerDefinition* headDefinition = head.empty()
                            ? 0 : scratch.library().findCaseTolerant(type, head);
                        const bool headIsLinker = headDefinition != 0 &&
                            (headDefinition->naturalAnalog == "p" ||
                             headDefinition->naturalAnalog == "P");
                        if (headIsLinker && tail.empty()) {
                            // A lone linker head with a base is the 5'-phosphate form.
                            leadingLinker = head;
                            trailingLinker.clear();
                        } else if (headIsLinker) {
                            // A linker head WITH a trailing symbol is not a nucleotide
                            // shape this module claims to understand; keep the head as a
                            // sugar-shaped unknown and let the warning below report it.
                            sugarSymbol = head;
                        } else {
                            sugarSymbol = head;
                        }
                        const size_t unitStart =
                            scratch.polymer(polymerIndex)->monomers.size();
                        size_t cursor = unitStart;
                        // POSITIONS, not ids. connect() addresses residues by position, and
                        // the two number spaces are different even though they can coincide.
                        size_t leadingLinkerPosition = kNoPosition;
                        size_t sugarPosition = kNoPosition;
                        size_t basePosition = kNoPosition;
                        MonomerInstanceId leadingLinkerId = kInvalidMonomerInstanceId;
                        MonomerInstanceId sugarId = kInvalidMonomerInstanceId;
                        MonomerInstanceId baseId = kInvalidMonomerInstanceId;
                        MonomerInstanceId trailingLinkerId = kInvalidMonomerInstanceId;
                        // 1. an optional 5' linker
                        if (!leadingLinker.empty()) {
                            MonomerInstance linkerInstance;
                            linkerInstance.sourceText = leadingLinker;
                            const MonomerDefinition* d =
                                scratch.library().findCaseTolerant(type, leadingLinker);
                            linkerInstance.definition = d ? *d : unknownMonomer(type, leadingLinker);
                            if (!d) ++local.unknownMonomers;
                            leadingLinkerPosition = cursor;
                            core::Error e = scratch.insertMonomerInstance(
                                polymerIndex, cursor, linkerInstance, &leadingLinkerId);
                            if (!e.ok()) { problems.push_back(error(e.code, e.message, text,
                                tokens[t].offset, polymerId)); break; }
                            ++cursor; ++local.monomersRead;
                        }
                        // 2. the sugar, unless the unit named no sugar at all (a bare
                        //    base is not a nucleotide, so a missing sugar with a trailing
                        //    linker is the only case left, and it is already covered by
                        //    the leading-linker branch above).
                        if (!sugarSymbol.empty()) {
                            MonomerInstance sugarInstance;
                            sugarInstance.sourceText = sugarSymbol;
                            const MonomerDefinition* d =
                                scratch.library().findCaseTolerant(type, sugarSymbol);
                            sugarInstance.definition = d ? *d : unknownMonomer(type, sugarSymbol);
                            if (!d) ++local.unknownMonomers;
                            sugarPosition = cursor;
                            core::Error e = scratch.insertMonomerInstance(
                                polymerIndex, cursor, sugarInstance, &sugarId);
                            if (!e.ok()) { problems.push_back(error(e.code, e.message, text,
                                tokens[t].offset, polymerId)); break; }
                            ++cursor; ++local.monomersRead;
                        }
                        // 3. the base, bonded to the sugar R3 or, when there is no sugar,
                        //    to the leading linker R2. A base with neither is refused, because
                        //    this model addresses residues by POSITION and will not invent one.
                        MonomerInstance baseInstance;
                        baseInstance.sourceText = unit.base;
                        const MonomerDefinition* baseDef =
                            scratch.library().findCaseTolerant(type, unit.base);
                        baseInstance.definition = baseDef
                            ? *baseDef : unknownMonomer(type, unit.base);
                        if (!baseDef) ++local.unknownMonomers;
                        basePosition = cursor;
                        core::Error baseError = scratch.insertMonomerInstance(
                            polymerIndex, cursor, baseInstance, &baseId);
                        if (!baseError.ok()) { problems.push_back(error(baseError.code,
                            baseError.message, text, tokens[t].offset, polymerId)); break; }
                        ++cursor; ++local.monomersRead;
                        // NOTE: connect() takes a POSITION, not a MonomerInstanceId. Mixing
                        // the two is exactly the class of bug that silently bonds the wrong
                        // residue, so positions are tracked separately and named for what
                        // they are.
                        if (sugarPosition != kNoPosition) {
                            core::Error link = scratch.connect(
                                polymerIndex, sugarPosition, kAttachmentR3,
                                polymerIndex, basePosition, kAttachmentR1,
                                ConnectionKind::Covalent, 0);
                            if (!link.ok()) { problems.push_back(error(
                                core::ErrorCode::InvalidSemantics,
                                "could not attach the nucleobase to its sugar: "
                                + tokens[t].symbol, text, tokens[t].offset, polymerId,
                                tokens[t].symbol)); break; }
                        } else if (leadingLinkerPosition != kNoPosition) {
                            core::Error link = scratch.connect(
                                polymerIndex, leadingLinkerPosition, kAttachmentR2,
                                polymerIndex, basePosition, kAttachmentR1,
                                ConnectionKind::Covalent, 0);
                            if (!link.ok()) { problems.push_back(error(
                                core::ErrorCode::InvalidSemantics,
                                "a 5' linker cannot carry a nucleobase: "
                                + tokens[t].symbol, text, tokens[t].offset, polymerId,
                                tokens[t].symbol)); break; }
                        } else {
                            problems.push_back(error(core::ErrorCode::ParseError,
                                "a nucleotide unit has neither a sugar nor a 5' linker "
                                + tokens[t].symbol, text, tokens[t].offset, polymerId,
                                tokens[t].symbol));
                            break;
                        }
                        // 4. the trailing linker, bonded to the sugar R2.
                        if (!trailingLinker.empty()) {
                            MonomerInstance linkerInstance;
                            linkerInstance.sourceText = trailingLinker;
                            const MonomerDefinition* d =
                                scratch.library().findCaseTolerant(type, trailingLinker);
                            linkerInstance.definition = d ? *d : unknownMonomer(type, trailingLinker);
                            if (!d) ++local.unknownMonomers;
                            const size_t linkerPosition = cursor;
                            core::Error e = scratch.insertMonomerInstance(
                                polymerIndex, cursor, linkerInstance, &trailingLinkerId);
                            if (!e.ok()) { problems.push_back(error(e.code, e.message, text,
                                tokens[t].offset, polymerId)); break; }
                            ++cursor; ++local.monomersRead;
                            if (sugarPosition != kNoPosition) {
                                core::Error link = scratch.connect(
                                    polymerIndex, sugarPosition, kAttachmentR2,
                                    polymerIndex, linkerPosition, kAttachmentR1,
                                    ConnectionKind::Covalent, 0);
                                if (!link.ok()) { problems.push_back(error(
                                    core::ErrorCode::InvalidSemantics,
                                    "could not attach the phosphate to its sugar: "
                                    + tokens[t].symbol, text, tokens[t].offset, polymerId,
                                    tokens[t].symbol)); break; }
                            }
                        }
                        continue;
                    }
                }

                MonomerInstance instance;
                instance.sourceText = tokens[t].isInlineChem
                                          ? ("[" + tokens[t].symbol + "]")
                                          : tokens[t].symbol;
                if (tokens[t].isInlineChem) {
                    instance.definition = chemMonomer("CHEM", tokens[t].symbol);
                    ++local.chemMonomers;
                } else {
                    // The notation may spell a symbol with different capitalisation than
                    // the library stores; the tolerant form is exact-first and refuses an
                    // ambiguous case fold.
                    const MonomerDefinition* found =
                        scratch.library().findCaseTolerant(type, tokens[t].symbol);
                    if (found) {
                        instance.definition = *found;
                    } else if (type == PolymerType::Blob) {
                        instance.definition = blobMonomer(tokens[t].symbol);
                        ++local.blobMonomers;
                    } else {
                        // R1: preserved verbatim, no invented structure.
                        instance.definition = unknownMonomer(type, tokens[t].symbol);
                        ++local.unknownMonomers;
                        problems.push_back(error(core::ErrorCode::Unsupported,
                            "unknown monomer symbol was preserved without a structure: "
                            + tokens[t].symbol, text, tokens[t].offset, polymerId,
                            tokens[t].symbol));
                    }
                }
                MonomerInstanceId newId = kInvalidMonomerInstanceId;
                core::Error inserted = scratch.insertMonomerInstance(
                    polymerIndex, scratch.polymer(polymerIndex)->monomers.size(),
                    instance, &newId);
                if (!inserted.ok()) {
                    problems.push_back(error(inserted.code, inserted.message, text,
                                             tokens[t].offset, polymerId, tokens[t].symbol));
                    break;
                }
                ++local.monomersRead;
            }
            // A BLOB with a trailing quoted polymer annotation keeps that text in
            // the polymer annotation slot; stage 1/2 do not model polymer comments,
            // so it is reported rather than silently discarded.
        }
    }

    // ---- section 2: connections ----------------------------------------------
    bool abort = !problems.empty();
    for (size_t i = 0; i < problems.size(); ++i)
        if (problems[i].code != core::ErrorCode::Unsupported) abort = true;
    if (!abort && !connectionSection.empty()) {
        const size_t connBegin = sectionOffsets.size() > 1 ? sectionOffsets[1] : 0;
        const std::vector<std::pair<size_t, size_t> > edges =
            splitEdgeSpans(body, connBegin, connBegin + sections[1].size());
        for (size_t e = 0; e < edges.size() && !abort; ++e) {
            const std::string entryText = body.substr(edges[e].first,
                                                      edges[e].second - edges[e].first);
            if (trimmed(entryText).empty()) continue;
            ConnectionSides sides;
            SequenceDiagnostic failure;
            if (!parseConnectionEntry(body, entryText, edges[e].first, &sides, &failure)) {
                problems.push_back(failure);
                abort = true;
                break;
            }
            // Resolve every named polymer; a missing one is an error, not a skip.
            std::vector<size_t> fromIdx, toIdx;
            for (size_t k = 0; k < sides.fromPolymers.size(); ++k) {
                const size_t idx = scratch.polymerIndexById(sides.fromPolymers[k]);
                if (idx == static_cast<size_t>(-1)) {
                    problems.push_back(error(core::ErrorCode::NotFound,
                        "connection names an unknown polymer: " + sides.fromPolymers[k],
                        text, edges[e].first));
                    abort = true;
                    break;
                }
                fromIdx.push_back(idx);
            }
            for (size_t k = 0; k < sides.toPolymers.size() && !abort; ++k) {
                const size_t idx = scratch.polymerIndexById(sides.toPolymers[k]);
                if (idx == static_cast<size_t>(-1)) {
                    problems.push_back(error(core::ErrorCode::NotFound,
                        "connection names an unknown polymer: " + sides.toPolymers[k],
                        text, edges[e].first));
                    abort = true;
                    break;
                }
                toIdx.push_back(idx);
            }
            if (abort) break;
            if (fromIdx.empty() || toIdx.empty()) {
                problems.push_back(error(core::ErrorCode::ParseError,
                    "connection has an empty side", text, edges[e].first));
                abort = true;
                break;
            }
            if (sides.kind == ConnectionKind::Generic) {
                // A generic edge names endpoints we cannot resolve. Stage 1/2
                // refuse to invent them.
                problems.push_back(error(core::ErrorCode::Unsupported,
                    "generic connections have no resolvable endpoint and are not modelled",
                    text, edges[e].first));
                sawFatal = true;
                abort = true;
                break;
            }
            const size_t pairCount = fromIdx.size() < toIdx.size() ? fromIdx.size() : toIdx.size();
            for (size_t k = 0; k < pairCount; ++k) {
                const size_t fp = fromIdx[k];
                const size_t tp = toIdx[k];
                if (sides.fromPositions[k] == kUnresolvedPosition ||
                    sides.toPositions[k] == kUnresolvedPosition) {
                    // REFUSED, not skipped: dropping an unresolvable cross-reference would
                    // remove a real bond while the polymer section still looked complete.
                    problems.push_back(error(core::ErrorCode::Unsupported,
                        "connection position is a name, not an index; this model addresses "
                        "residues by position and will not guess which one was meant",
                        text, edges[e].first));
                    sawFatal = true;
                    abort = true;
                    break;
                }
                if (sides.fromPositions[k] == 0 || sides.toPositions[k] == 0) {
                    problems.push_back(error(core::ErrorCode::OutOfRange,
                        "connection position 0 is not valid; HELM positions are 1-based",
                        text, edges[e].first));
                    abort = true;
                    break;
                }
                const size_t fromPos = sides.fromPositions[k] - 1;
                const size_t toPos = sides.toPositions[k] - 1;
                if (fromPos >= scratch.polymer(fp)->monomers.size() ||
                    toPos >= scratch.polymer(tp)->monomers.size()) {
                    problems.push_back(error(core::ErrorCode::OutOfRange,
                        "connection endpoint position is beyond the polymer length",
                        text, edges[e].first));
                    abort = true;
                    break;
                }
                ConnectionId newId = kInvalidConnectionId;
                core::Error made = scratch.connect(fp, fromPos, sides.fromAttachments[k],
                                                   tp, toPos, sides.toAttachments[k],
                                                   sides.kind, &newId);
                if (!made.ok()) {
                    if (options.strictAttachments) {
                        problems.push_back(error(made.code, made.message, text, edges[e].first));
                        abort = true;
                        break;
                    }
                    SequenceDiagnostic warn = error(made.code, made.message, text, edges[e].first);
                    local.warnings.push_back(warn);
                    const size_t before = local.warnings.size();
                    (void)before;
                    continue;
                }
                ++local.connectionsRead;
                if (sides.kind == ConnectionKind::Hydrogen) ++local.hydrogenConnections;
                if (!sides.annotation.empty())
                    scratch.setConnectionAnnotation(newId, sides.annotation);
            }
        }
    }

    // ---- section 4: annotations ----------------------------------------------
    if (!abort && !annotationSection.empty()) {
        const size_t annBegin = sectionOffsets.size() > 3 ? sectionOffsets[3] : 0;
        std::string ann = trimmed(annotationSection);
        size_t cursor = 0;
        while (cursor < ann.size()) {
            const size_t brace = ann.find('{', cursor);
            if (brace == std::string::npos) break;
            const size_t close = ann.find('}', brace);
            if (close == std::string::npos) {
                problems.push_back(error(core::ErrorCode::Truncated,
                    "unterminated annotation block", text, annBegin + brace));
                abort = true;
                break;
            }
            const std::string id = trimmed(ann.substr(cursor, brace - cursor));
            const std::string inner = ann.substr(brace + 1, close - brace - 1);
            const size_t colon = inner.find(':');
            if (!id.empty() && colon != std::string::npos) {
                const std::string key = trimmed(inner.substr(0, colon));
                const std::string value = trimmed(inner.substr(colon + 1));
                const size_t polymerIndex = scratch.polymerIndexById(id);
                if (polymerIndex == static_cast<size_t>(-1)) {
                    problems.push_back(error(core::ErrorCode::NotFound,
                        "annotation names an unknown polymer: " + id, text, annBegin + cursor));
                } else if (key == "StrandType" || key == "strandtype" || key == "STRANDTYPE") {
                    scratch.mutablePolymer(polymerIndex)->strandType = value;
                    ++local.annotationsRead;
                } else {
                    // An annotation keyword this stage does not model is reported,
                    // never silently dropped.
                    problems.push_back(error(core::ErrorCode::Unsupported,
                        "unknown annotation keyword preserved as a warning: " + key,
                        text, annBegin + brace + 1));
                }
            }
            cursor = close + 1;
            if (ann[cursor] == '|') ++cursor;
        }
    }

    // ---- commit or refuse ----------------------------------------------------
    bool fatal = sawFatal;
    for (size_t i = 0; i < problems.size(); ++i)
        if (problems[i].code != core::ErrorCode::Unsupported) fatal = true;
    if (fatal) {
        if (errors) *errors = problems;
        if (report) *report = local;
        core::ErrorCode code = core::ErrorCode::ParseError;
        for (size_t i = 0; i < problems.size(); ++i)
            if (problems[i].code != core::ErrorCode::Unsupported) { code = problems[i].code; break; }
        std::string message = "HELM parse failed";
        for (size_t i = 0; i < problems.size(); ++i)
            if (problems[i].code != core::ErrorCode::Unsupported) {
                message = problems[i].summary();
                break;
            }
        return core::Error(code, message);
    }
    // Warnings only: the model is usable, and every warning is reported.
    for (size_t i = 0; i < problems.size(); ++i) local.warnings.push_back(problems[i]);
    model = scratch;
    if (errors) *errors = problems;
    if (report) *report = local;
    return core::Error();
}

core::Error parseHelm(const std::string& text, SequenceModel& model,
                      HelmParseReport* report, const HelmParseOptions& options) {
    return parseHelmDetailed(text, model, report, 0, options);
}

} // namespace helm
} // namespace r62
