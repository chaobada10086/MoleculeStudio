#ifndef STUDIO_CHEM_ITALIC_HPP
#define STUDIO_CHEM_ITALIC_HPP
// Automatic italic styling for chemical names entered with the text tool,
// following the house rules in tasks_1031 (化学缩写斜体清单, IUPAC 2013 + 化学学报
// 3.18(5)). Only descriptor / locator / placeholder LETTERS become italic:
// digits, brackets, hyphens, commas, primes, element symbols inside formulae
// (H2O, NH3, Ir(ppy)3) and structure abbreviations (Me, Et, Ph, Boc, ppy,
// cyclo, neo, nor, seco ...) stay upright.
//
// The pass is a single forward scan over the codepoints with bounded work (a
// label is short); a long caption is left alone rather than parsed. It only ever
// REPORTS which characters need italics — the caller ORs the italic bit in, so a
// manual Ctrl+I on any character is never removed.
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chemitalic {

inline bool isDigit(uint32_t c) { return c >= 0x30 && c <= 0x39; }
inline bool isLower(uint32_t c) { return c >= 0x61 && c <= 0x7A; }
inline bool isUpper(uint32_t c) { return c >= 0x41 && c <= 0x5A; }
inline bool isAlpha(uint32_t c) { return isLower(c) || isUpper(c); }
// U+002D and the typographic hyphens a paste from Word or a PDF can carry.
inline bool isHyphen(uint32_t c) { return c == 0x2D || c == 0xFF0D || (c >= 0x2010 && c <= 0x2013); }
// Prime marks stay upright even directly after an italic letter (P-16.9).
inline bool isPrime(uint32_t c) { return c == 0x27 || c == 0x2032 || c == 0x2033 || c == 0x2019; }
inline bool isOpen(uint32_t c) { return c == '(' || c == '[' || c == '{' || c == 0xFF08 || c == 0xFF3B; }
inline bool isClose(uint32_t c) { return c == ')' || c == ']' || c == '}' || c == 0xFF09 || c == 0xFF3D; }
// Half-width bracket of a stereo group is always '('; the full-width form is common
// when the label is typed with a Chinese IME.
inline bool isParen(uint32_t c) { return c == '(' || c == 0xFF08; }
inline bool isSpace(uint32_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xA0 || c == 0x3000; }
// A token boundary: the character before a name component.
inline bool isBreak(uint32_t c) {
    return isHyphen(c) || isPrime(c) || isOpen(c) || isClose(c) || isSpace(c) ||
           c == ',' || c == ';' || c == ':' || c == '.' || c == 0xB7 || c == 0x2022 ||
           c == '/' || c == '=' || c == '+' || c == '*' || c == '&' || c == '~' ||
           c == 0xFF0C || c == 0xFF1B || c == 0xFF1A || c == 0xFF0E;
}
// Greek locants and descriptors (α β γ δ η κ λ μ ω ξ Δ Λ Ξ). Eta (hapto) and mu
// (bridging) are marked △ in the list but italic in mainstream journals.
inline bool isItalicGreek(uint32_t c) {
    switch (c) {
        case 0x0394: case 0x039B: case 0x039E:
        case 0x03B1: case 0x03B2: case 0x03B3: case 0x03B4: case 0x03B7:
        case 0x03BA: case 0x03BB: case 0x03BC: case 0x03BE: case 0x03C9:
            return true;
        default:
            return false;
    }
}

inline bool wordAt(const std::vector<uint32_t>& c, size_t p, const char* word, size_t length) {
    if (p + length > c.size()) return false;
    for (size_t i = 0; i < length; ++i)
        if (c[p + i] != uint32_t((unsigned char)word[i])) return false;
    return true;
}

inline size_t wordLength(const char* word) {
    size_t n = 0;
    while (word[n]) ++n;
    return n;
}

// Substitutive prefixes that are written italic and end in a hyphen:
// n-Bu, sec-Bu, tert-Bu, t-Bu, i-Pr, s-Bu, o-xylene, m-CPBA, p-TsOH, iso-Pr,
// cis-, trans-, syn-, anti-, sp-, sc-, ac-, ap-, endo-, exo-, fac-, mer-,
// meso-, rac-, rel-, retro-, ambo- plus the coordination polyhedron symbols.
inline bool italicPrefixAt(const std::vector<uint32_t>& c, size_t p, size_t* length) {
    static const char* const words[] = {
        "ortho", "retro", "trans", "ambo", "anti", "meta", "para", "endo", "exo", "meso", "tert", "TBPY",
        "cis", "fac", "mer", "rac", "rel", "syn", "sec", "iso", "SPY",
        "sp", "sc", "ac", "ap", "OC",
        "n", "t", "i", "s", "o", "m", "p", "R", "S", "E", "Z", "M"
    };
    size_t best = 0;
    for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); ++w) {
        size_t n = wordLength(words[w]);
        if (n <= best || !wordAt(c, p, words[w], n)) continue;
        size_t q = p + n;
        bool ok = false;
        if (q < c.size() && isHyphen(c[q])) ok = true;
        // "(rac)-", "(R)-", "(meso)" keep the parentheses upright.
        else if (n > 1 && q < c.size() && isClose(c[q]) &&
                 (q + 1 >= c.size() || isHyphen(c[q + 1]) || isClose(c[q + 1]) || isSpace(c[q + 1])))
            ok = true;
        if (ok) best = n;
    }
    *length = best;
    return best != 0;
}

// Heteroatom attachment locants (N,N-dimethyl, O-methyl, S-adenosyl, P-phenyl,
// As-...). The element symbol is italic only when it marks the attachment point,
// never inside a formula (NH3, H2O, Ir(ppy)3).
inline bool italicLocatorAt(const std::vector<uint32_t>& c, size_t p, size_t* length) {
    static const char* const symbols[] = { "As", "N", "O", "S", "P" };
    for (size_t s = 0; s < sizeof(symbols) / sizeof(symbols[0]); ++s) {
        size_t n = wordLength(symbols[s]);
        if (!wordAt(c, p, symbols[s], n)) continue;
        size_t q = p + n;
        // "κ2-N,N" ends the label with the coordinating atom list.
        if (q == c.size()) { if (p > 0 && c[p - 1] == ',') { *length = n; return true; } continue; }
        if (isHyphen(c[q]) || c[q] == ',' || isPrime(c[q]) || c[q] == '*') { *length = n; return true; }
    }
    return false;
}

// (R), (S), (E), (Z), (M), (P), (r), (s), (Ra), (Sa), (Rp), (Sp), (R,R), (R*).
inline bool stereoToken(const std::string& t) {
    return t == "R" || t == "S" || t == "E" || t == "Z" || t == "M" || t == "P" ||
           t == "r" || t == "s" || t == "Ra" || t == "Sa" || t == "Rp" || t == "Sp";
}

// One entry per codepoint: 1 means the character is written italic.
inline std::vector<uint8_t> mask(const std::vector<uint32_t>& c) {
    const size_t n = c.size();
    std::vector<uint8_t> out(n, 0);
    if (n == 0 || n > 4096) return out;
    std::vector<int> bracket(n, 0);
    int depth = 0;
    for (size_t i = 0; i < n; ++i) {
        if (c[i] == '[') ++depth;
        bracket[i] = depth;
        if (c[i] == ']') --depth;
    }
    for (size_t i = 0; i < n; ++i) {
        const uint32_t ch = c[i];
        if (isItalicGreek(ch)) out[i] = 1;
        // A stereo group is recognised wherever its parenthesis sits, including
        // directly after a name: poly(trans-...) and methyl (S)-lactate.
        if (isParen(ch)) {
            size_t q = i + 1;
            std::vector<std::pair<size_t, size_t> > spans;
            bool complete = false;
            for (;;) {
                size_t save = q;
                while (q < n && isDigit(c[q])) ++q;   // locant: (1R,2S), (2E,4Z)
                size_t start = q;
                while (q < n && isAlpha(c[q])) ++q;
                size_t len = q - start;
                if (len == 0 || len > 2) { q = save; break; }
                std::string token;
                for (size_t k = start; k < q; ++k) token += char(c[k]);
                if (!stereoToken(token)) { q = save; break; }
                spans.push_back(std::make_pair(start, q));
                if (q < n && c[q] == '*') { out[q] = 1; ++q; }
                if (q < n && c[q] == ',') { ++q; continue; }
                complete = true;
                break;
            }
            if (!spans.empty() && complete && q < n && isClose(c[q]))
                for (size_t s = 0; s < spans.size(); ++s)
                    for (size_t k = spans[s].first; k < spans[s].second; ++k) out[k] = 1;
            continue;
        }
        if (i == 0 || isBreak(c[i - 1])) {
            size_t length = 0;
            if (italicPrefixAt(c, i, &length) || italicLocatorAt(c, i, &length)) {
                for (size_t k = i; k < i + length; ++k) out[k] = 1;
                continue;
            }
        }
        // Indicated hydrogen: 1H-indole, 2H-pyran, 9H-carbazole. The ¹H of NMR
        // spectra has no name after it and stays upright.
        if (isDigit(ch)) {
            size_t q = i;
            while (q < n && isDigit(c[q])) ++q;
            if (q + 2 < n && c[q] == 'H' && isHyphen(c[q + 1]) && isAlpha(c[q + 2])) out[q] = 1;
        }
        // Fused-ring locant inside brackets: benzo[a]pyrene, naphtho[2,3-b]furan.
        // The letter of a ring position such as 3a stays upright (bracket == 0).
        if (bracket[i] > 0 && isLower(ch) &&
            (i == 0 || isOpen(c[i - 1]) || c[i - 1] == ',' || isHyphen(c[i - 1])) &&
            (i + 1 >= n || isClose(c[i + 1]) || c[i + 1] == ',' || isHyphen(c[i + 1])))
            out[i] = 1;
        // Enantiomeric/diastereomeric excess and ratio abbreviations are italic
        // when they stand alone: 96% ee, 12:1 dr, 99% de. The boundary test keeps
        // ordinary words such as "see" upright.
        {
            static const char* const ratios[] = {"ee", "de", "dr", "er"};
            bool boundary = i == 0 || isBreak(c[i - 1]) || c[i - 1] == '%';
            if (boundary) {
                for (size_t r = 0; r < sizeof(ratios) / sizeof(ratios[0]); ++r) {
                    size_t length = wordLength(ratios[r]);
                    if (!wordAt(c, i, ratios[r], length)) continue;
                    if (i + length < c.size() && isAlpha(c[i + length])) continue;
                    for (size_t k = 0; k < length; ++k) out[i + k] = 1;
                    break;
                }
            }
        }
    }
    return out;
}

inline std::vector<uint32_t> codepoints(const std::wstring& text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t c = uint32_t(uint16_t(text[i]));
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size()) {
            uint32_t low = uint32_t(uint16_t(text[i + 1]));
            if (low >= 0xDC00 && low <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00); ++i; }
        }
        out.push_back(c);
    }
    return out;
}

inline std::vector<uint32_t> codepointsUtf8(const std::string& text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        unsigned char b = (unsigned char)text[i];
        uint32_t c = b;
        size_t extra = 0;
        if (b >= 0xF0) { c = b & 0x07; extra = 3; }
        else if (b >= 0xE0) { c = b & 0x0F; extra = 2; }
        else if (b >= 0xC0) { c = b & 0x1F; extra = 1; }
        ++i;
        for (size_t k = 0; k < extra && i < text.size(); ++k, ++i) c = (c << 6) | ((unsigned char)text[i] & 0x3F);
        out.push_back(c);
    }
    return out;
}

// The mask aligned to UTF-16 code units, so the RichEdit character loop can use
// it directly (surrogate pairs simply share their lead's flag).
inline std::vector<uint8_t> maskUtf16(const std::wstring& text) {
    std::vector<uint32_t> cps = codepoints(text);
    std::vector<uint8_t> m = mask(cps);
    std::vector<uint8_t> out;
    out.reserve(text.size());
    size_t k = 0;
    for (size_t i = 0; i < text.size() && k < m.size(); ++i, ++k) {
        out.push_back(m[k]);
        if (cps[k] > 0xFFFF && i + 1 < text.size()) { out.push_back(m[k]); ++i; }
    }
    while (out.size() < text.size()) out.push_back(0);
    return out;
}

inline std::vector<uint8_t> maskUtf8(const std::string& text) { return mask(codepointsUtf8(text)); }

// "N,N-dimethylformamide" -> "^^.............." (one character per codepoint).
inline std::string patternUtf8(const std::string& text) {
    std::vector<uint32_t> cps = codepointsUtf8(text);
    std::vector<uint8_t> m = mask(cps);
    std::string out;
    out.reserve(m.size());
    for (size_t i = 0; i < m.size(); ++i) out += m[i] ? '^' : '.';
    return out;
}

}  // namespace chemitalic
#endif
