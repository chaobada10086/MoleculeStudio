// =============================================================================
// re_round62_viewer/next/chemistry/element_data.cpp
//
// T06 - the periodic table columns and the isotope table.
//
// Every number in this file is either
//   (a) the conventional main-group valence an introductory drawing package
//       uses, or
//   (b) the IUPAC/NIST standard atomic weight / isotope exact mass.
// Nothing here is a computed chemistry result.  Where a value is not defined for
// an element the row carries -1 and a reason, and the caller is required to treat
// that as "no prediction available".
// =============================================================================

#include "next/chemistry/element_data.hpp"

#include <cstring>

namespace r62 {
namespace chemistry {
namespace {

// Reasons attached to rows with no conventional valence.  Kept short so they fit
// in a label tooltip; they exist so a caller can explain WHY no hydrogen count was
// produced instead of reporting a bare zero.
const char* kNoValenceMetal   = "transition/other metal: no single conventional valence";
const char* kNoValenceFBlock  = "f-block: multiple oxidation states, no single valence";
const char* kNoValenceNoble   = "noble gas: no conventional bonding valence";

#define EV(low, high) static_cast<int8_t>(low), static_cast<int8_t>(high)

// number, symbol, name, weight, valLow, valHigh, organicSubset, metal, note
const ElementInfo kElements[119] = {
    {   0, "?",  "",            0.0,       EV(-1,-1), 0, 0, "not an element" },
    {   1, "H",  "Hydrogen",    1.008,     EV( 1, 1), 1, 0, "" },
    {   2, "He", "Helium",      4.002602,  EV(-1,-1), 0, 0, kNoValenceNoble },
    {   3, "Li", "Lithium",     6.94,      EV( 1, 1), 0, 1, "" },
    {   4, "Be", "Beryllium",   9.0121831, EV( 2, 2), 0, 1, "" },
    {   5, "B",  "Boron",      10.81,      EV( 3, 3), 1, 0, "" },
    {   6, "C",  "Carbon",     12.011,     EV( 4, 4), 1, 0, "" },
    {   7, "N",  "Nitrogen",   14.007,     EV( 3, 5), 1, 0, "" },
    {   8, "O",  "Oxygen",     15.999,     EV( 2, 2), 1, 0, "" },
    {   9, "F",  "Fluorine",   18.998403163, EV(1, 1), 1, 0, "" },
    {  10, "Ne", "Neon",       20.1797,    EV(-1,-1), 0, 0, kNoValenceNoble },
    {  11, "Na", "Sodium",     22.98976928,EV( 1, 1), 0, 1, "" },
    {  12, "Mg", "Magnesium",  24.305,     EV( 2, 2), 0, 1, "" },
    {  13, "Al", "Aluminium",  26.9815385, EV( 3, 3), 0, 1, "" },
    {  14, "Si", "Silicon",    28.085,     EV( 4, 4), 0, 0, "" },
    {  15, "P",  "Phosphorus", 30.973761998, EV(3, 5), 1, 0, "" },
    {  16, "S",  "Sulfur",     32.06,      EV( 2, 6), 1, 0, "" },
    {  17, "Cl", "Chlorine",   35.45,      EV( 1, 7), 1, 0, "" },
    {  18, "Ar", "Argon",      39.948,     EV(-1,-1), 0, 0, kNoValenceNoble },
    {  19, "K",  "Potassium",  39.0983,    EV( 1, 1), 0, 1, "" },
    {  20, "Ca", "Calcium",    40.078,     EV( 2, 2), 0, 1, "" },
    {  21, "Sc", "Scandium",   44.955908,  EV(-1,-1), 0, 1, kNoValenceMetal },
    {  22, "Ti", "Titanium",   47.867,     EV(-1,-1), 0, 1, kNoValenceMetal },
    {  23, "V",  "Vanadium",   50.9415,    EV(-1,-1), 0, 1, kNoValenceMetal },
    {  24, "Cr", "Chromium",   51.9961,    EV(-1,-1), 0, 1, kNoValenceMetal },
    {  25, "Mn", "Manganese",  54.938044,  EV(-1,-1), 0, 1, kNoValenceMetal },
    {  26, "Fe", "Iron",       55.845,     EV(-1,-1), 0, 1, kNoValenceMetal },
    {  27, "Co", "Cobalt",     58.933194,  EV(-1,-1), 0, 1, kNoValenceMetal },
    {  28, "Ni", "Nickel",     58.6934,    EV(-1,-1), 0, 1, kNoValenceMetal },
    {  29, "Cu", "Copper",     63.546,     EV(-1,-1), 0, 1, kNoValenceMetal },
    {  30, "Zn", "Zinc",       65.38,      EV( 2, 2), 0, 1, "" },
    {  31, "Ga", "Gallium",    69.723,     EV( 3, 3), 0, 1, "" },
    {  32, "Ge", "Germanium",  72.630,     EV( 4, 4), 0, 0, "" },
    {  33, "As", "Arsenic",    74.921595,  EV( 3, 5), 0, 0, "" },
    {  34, "Se", "Selenium",   78.971,     EV( 2, 6), 0, 0, "" },
    {  35, "Br", "Bromine",    79.904,     EV( 1, 7), 1, 0, "" },
    {  36, "Kr", "Krypton",    83.798,     EV(-1,-1), 0, 0, kNoValenceNoble },
    {  37, "Rb", "Rubidium",   85.4678,    EV( 1, 1), 0, 1, "" },
    {  38, "Sr", "Strontium",  87.62,      EV( 2, 2), 0, 1, "" },
    {  39, "Y",  "Yttrium",    88.90584,   EV(-1,-1), 0, 1, kNoValenceMetal },
    {  40, "Zr", "Zirconium",  91.224,     EV(-1,-1), 0, 1, kNoValenceMetal },
    {  41, "Nb", "Niobium",    92.90637,   EV(-1,-1), 0, 1, kNoValenceMetal },
    {  42, "Mo", "Molybdenum", 95.95,      EV(-1,-1), 0, 1, kNoValenceMetal },
    {  43, "Tc", "Technetium", 98.0,       EV(-1,-1), 0, 1, kNoValenceMetal },
    {  44, "Ru", "Ruthenium", 101.07,      EV(-1,-1), 0, 1, kNoValenceMetal },
    {  45, "Rh", "Rhodium",   102.90550,   EV(-1,-1), 0, 1, kNoValenceMetal },
    {  46, "Pd", "Palladium", 106.42,      EV(-1,-1), 0, 1, kNoValenceMetal },
    {  47, "Ag", "Silver",    107.8682,    EV(-1,-1), 0, 1, kNoValenceMetal },
    {  48, "Cd", "Cadmium",   112.414,     EV( 2, 2), 0, 1, "" },
    {  49, "In", "Indium",    114.818,     EV( 3, 3), 0, 1, "" },
    {  50, "Sn", "Tin",       118.710,     EV( 2, 4), 0, 1, "" },
    {  51, "Sb", "Antimony",  121.760,     EV( 3, 5), 0, 0, "" },
    {  52, "Te", "Tellurium", 127.60,      EV( 2, 6), 0, 0, "" },
    {  53, "I",  "Iodine",    126.90447,   EV( 1, 7), 1, 0, "" },
    {  54, "Xe", "Xenon",     131.293,     EV(-1,-1), 0, 0, kNoValenceNoble },
    {  55, "Cs", "Caesium",   132.90545196,EV( 1, 1), 0, 1, "" },
    {  56, "Ba", "Barium",    137.327,     EV( 2, 2), 0, 1, "" },
    {  57, "La", "Lanthanum", 138.90547,   EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  58, "Ce", "Cerium",    140.116,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  59, "Pr", "Praseodymium", 140.90766,EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  60, "Nd", "Neodymium", 144.242,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  61, "Pm", "Promethium", 145.0,      EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  62, "Sm", "Samarium",  150.36,      EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  63, "Eu", "Europium",  151.964,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  64, "Gd", "Gadolinium", 157.25,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  65, "Tb", "Terbium",   158.92535,   EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  66, "Dy", "Dysprosium",162.500,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  67, "Ho", "Holmium",   164.93033,   EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  68, "Er", "Erbium",    167.259,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  69, "Tm", "Thulium",   168.93422,   EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  70, "Yb", "Ytterbium", 173.045,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  71, "Lu", "Lutetium",  174.9668,    EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  72, "Hf", "Hafnium",   178.49,      EV(-1,-1), 0, 1, kNoValenceMetal },
    {  73, "Ta", "Tantalum",  180.94788,   EV(-1,-1), 0, 1, kNoValenceMetal },
    {  74, "W",  "Tungsten",  183.84,      EV(-1,-1), 0, 1, kNoValenceMetal },
    {  75, "Re", "Rhenium",   186.207,     EV(-1,-1), 0, 1, kNoValenceMetal },
    {  76, "Os", "Osmium",    190.23,      EV(-1,-1), 0, 1, kNoValenceMetal },
    {  77, "Ir", "Iridium",   192.217,     EV(-1,-1), 0, 1, kNoValenceMetal },
    {  78, "Pt", "Platinum",  195.084,     EV(-1,-1), 0, 1, kNoValenceMetal },
    {  79, "Au", "Gold",      196.966569,  EV(-1,-1), 0, 1, kNoValenceMetal },
    {  80, "Hg", "Mercury",   200.592,     EV(-1,-1), 0, 1, kNoValenceMetal },
    {  81, "Tl", "Thallium",  204.38,      EV( 1, 3), 0, 1, "" },
    {  82, "Pb", "Lead",      207.2,       EV( 2, 4), 0, 1, "" },
    {  83, "Bi", "Bismuth",   208.98040,   EV( 3, 5), 0, 1, "" },
    {  84, "Po", "Polonium",  209.0,       EV( 2, 4), 0, 1, "" },
    {  85, "At", "Astatine",  210.0,       EV( 1, 7), 0, 0, "" },
    {  86, "Rn", "Radon",     222.0,       EV(-1,-1), 0, 0, kNoValenceNoble },
    {  87, "Fr", "Francium",  223.0,       EV( 1, 1), 0, 1, "" },
    {  88, "Ra", "Radium",    226.0,       EV( 2, 2), 0, 1, "" },
    {  89, "Ac", "Actinium",  227.0,       EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  90, "Th", "Thorium",   232.0377,    EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  91, "Pa", "Protactinium", 231.03588,EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  92, "U",  "Uranium",   238.02891,   EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  93, "Np", "Neptunium", 237.0,       EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  94, "Pu", "Plutonium", 244.0,       EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  95, "Am", "Americium", 243.0,       EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  96, "Cm", "Curium",    247.0,       EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  97, "Bk", "Berkelium", 247.0,       EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  98, "Cf", "Californium", 251.0,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    {  99, "Es", "Einsteinium", 252.0,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    { 100, "Fm", "Fermium",   257.0,       EV(-1,-1), 0, 1, kNoValenceFBlock },
    { 101, "Md", "Mendelevium", 258.0,     EV(-1,-1), 0, 1, kNoValenceFBlock },
    { 102, "No", "Nobelium",  259.0,       EV(-1,-1), 0, 1, kNoValenceFBlock },
    { 103, "Lr", "Lawrencium", 266.0,      EV(-1,-1), 0, 1, kNoValenceFBlock },
    { 104, "Rf", "Rutherfordium", 267.0,   EV(-1,-1), 0, 1, kNoValenceMetal },
    { 105, "Db", "Dubnium",   268.0,       EV(-1,-1), 0, 1, kNoValenceMetal },
    { 106, "Sg", "Seaborgium", 269.0,      EV(-1,-1), 0, 1, kNoValenceMetal },
    { 107, "Bh", "Bohrium",   270.0,       EV(-1,-1), 0, 1, kNoValenceMetal },
    { 108, "Hs", "Hassium",   277.0,       EV(-1,-1), 0, 1, kNoValenceMetal },
    { 109, "Mt", "Meitnerium", 278.0,      EV(-1,-1), 0, 1, kNoValenceMetal },
    { 110, "Ds", "Darmstadtium", 281.0,    EV(-1,-1), 0, 1, kNoValenceMetal },
    { 111, "Rg", "Roentgenium", 282.0,     EV(-1,-1), 0, 1, kNoValenceMetal },
    { 112, "Cn", "Copernicium", 285.0,     EV(-1,-1), 0, 1, kNoValenceMetal },
    { 113, "Nh", "Nihonium",  286.0,       EV(-1,-1), 0, 1, kNoValenceMetal },
    { 114, "Fl", "Flerovium", 289.0,       EV(-1,-1), 0, 1, kNoValenceMetal },
    { 115, "Mc", "Moscovium", 290.0,       EV(-1,-1), 0, 1, kNoValenceMetal },
    { 116, "Lv", "Livermorium", 293.0,     EV(-1,-1), 0, 1, kNoValenceMetal },
    { 117, "Ts", "Tennessine", 294.0,      EV(-1,-1), 0, 1, kNoValenceMetal },
    { 118, "Og", "Oganesson", 294.0,       EV(-1,-1), 0, 0, kNoValenceNoble }
};

#undef EV

// ---------------------------------------------------------------------------
// Isotope table
// ---------------------------------------------------------------------------
// A deliberately SMALL table.  It contains the isotopes a chemistry editor is
// actually asked for (deuterium, 13C, 14C, 15N, 18O, the halogens a mass-spec
// person checks) plus the monoisotopic isotope of every element that has a stable
// one.  Missing isotopes return "unknown to this module" and the caller must not
// fabricate a mass.
const IsotopeInfo kH[] = {
    { 1, 1.0078250319, 99.9885 }, { 2, 2.0141017779, 0.0115 }, { 3, 3.016049281, 0.0 }
};
const IsotopeInfo kHe[] = { { 3, 3.016029322, 0.000134 }, { 4, 4.002603254, 99.999866 } };
const IsotopeInfo kLi[] = { { 6, 6.015122887, 7.59 }, { 7, 7.016003437, 92.41 } };
const IsotopeInfo kBe[] = { { 9, 9.012183065, 100.0 } };
const IsotopeInfo kB[]  = { { 10, 10.012936954, 19.9 }, { 11, 11.00930536, 80.1 } };
const IsotopeInfo kC[]  = { { 12, 12.0000000, 98.93 }, { 13, 13.003354835, 1.07 }, { 14, 14.003241989, 0.0 } };
const IsotopeInfo kN[]  = { { 14, 14.003074004, 99.636 }, { 15, 15.000108899, 0.364 } };
const IsotopeInfo kO[]  = { { 16, 15.994914620, 99.757 }, { 17, 16.999131757, 0.038 }, { 18, 17.999159613, 0.205 } };
const IsotopeInfo kF[]  = { { 19, 18.998403163, 100.0 } };
const IsotopeInfo kNe[] = { { 20, 19.992440176, 90.48 }, { 22, 21.991385114, 9.25 } };
const IsotopeInfo kNa[] = { { 23, 22.989769282, 100.0 } };
const IsotopeInfo kMg[] = { { 24, 23.985041698, 78.99 }, { 26, 25.982592968, 11.01 } };
const IsotopeInfo kAl[] = { { 27, 26.98153853, 100.0 } };
const IsotopeInfo kSi[] = { { 28, 27.976926535, 92.223 }, { 29, 28.976494665, 4.685 } };
const IsotopeInfo kP[]  = { { 31, 30.973761998, 100.0 } };
const IsotopeInfo kS[]  = { { 32, 31.972071174, 94.99 }, { 34, 33.967867004, 4.25 } };
const IsotopeInfo kCl[] = { { 35, 34.968852683, 75.76 }, { 37, 36.965902602, 24.24 } };
const IsotopeInfo kAr[] = { { 40, 39.962383123, 99.6 } };
const IsotopeInfo kK[]  = { { 39, 38.963706487, 93.2581 }, { 41, 40.961825258, 6.7302 } };
const IsotopeInfo kCa[] = { { 40, 39.962590866, 96.941 } };
const IsotopeInfo kBr[] = { { 79, 78.9183376, 50.69 }, { 81, 80.9162897, 49.31 } };
const IsotopeInfo kI[]  = { { 127, 126.9044719, 100.0 } };
const IsotopeInfo kFe[] = { { 56, 55.93493633, 91.754 } };
const IsotopeInfo kCu[] = { { 63, 62.92959772, 69.15 }, { 65, 64.92778970, 30.85 } };
const IsotopeInfo kZn[] = { { 64, 63.92914201, 49.17 } };
const IsotopeInfo kSe[] = { { 80, 79.9165218, 49.61 } };
const IsotopeInfo kSn[] = { { 120, 119.90220163, 32.58 } };
const IsotopeInfo kPt[] = { { 195, 194.9647911, 33.78 } };

struct IsotopeRow {
    int number;
    const IsotopeInfo* list;
    int count;
};

#define ROW(n, a) { n, a, static_cast<int>(sizeof(a) / sizeof(IsotopeInfo)) }
const IsotopeRow kIsotopeRows[] = {
    ROW(1, kH), ROW(2, kHe), ROW(3, kLi), ROW(4, kBe), ROW(5, kB), ROW(6, kC),
    ROW(7, kN), ROW(8, kO), ROW(9, kF), ROW(10, kNe), ROW(11, kNa), ROW(12, kMg),
    ROW(13, kAl), ROW(14, kSi), ROW(15, kP), ROW(16, kS), ROW(17, kCl), ROW(18, kAr),
    ROW(19, kK), ROW(20, kCa), ROW(26, kFe), ROW(29, kCu), ROW(30, kZn),
    ROW(34, kSe), ROW(35, kBr), ROW(50, kSn), ROW(53, kI), ROW(78, kPt)
};
#undef ROW
const int kIsotopeRowCount = static_cast<int>(sizeof(kIsotopeRows) / sizeof(IsotopeRow));

std::string intToStr(int v) {
    if (v == 0) return "0";
    const bool neg = v < 0;
    unsigned int u = neg ? static_cast<unsigned int>(-(v + 1)) + 1u : static_cast<unsigned int>(v);
    std::string s;
    while (u) { s.insert(s.begin(), static_cast<char>('0' + (u % 10u))); u /= 10u; }
    if (neg) s.insert(s.begin(), '-');
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------
const ElementInfo& elementInfo(int number) {
    if (number < 0 || number > 118) return kElements[0];
    return kElements[number];
}

const ElementInfo* elementInfoBySymbol(const char* symbol, int len) {
    if (!symbol || len <= 0) return 0;
    for (int i = 1; i <= 118; ++i) {
        const char* s = kElements[i].symbol;
        int n = 0;
        while (s[n]) ++n;
        if (n == len && std::strncmp(s, symbol, static_cast<size_t>(len)) == 0)
            return &kElements[i];
    }
    return 0;
}

const ElementInfo* elementInfoBySymbol(const char* symbol) {
    if (!symbol) return 0;
    int n = 0;
    while (symbol[n]) ++n;
    return elementInfoBySymbol(symbol, n);
}

bool isOrganicSubset(int number) {
    if (number < 0 || number > 118) return false;
    return kElements[number].isOrganicSubset != 0;
}

bool hasConventionalValence(int number) {
    if (number < 1 || number > 118) return false;
    return kElements[number].valenceLow > 0;
}

const IsotopeInfo* isotopes(int number, int* count) {
    if (count) *count = 0;
    for (int i = 0; i < kIsotopeRowCount; ++i) {
        if (kIsotopeRows[i].number == number) {
            if (count) *count = kIsotopeRows[i].count;
            return kIsotopeRows[i].list;
        }
    }
    return 0;
}

bool knownIsotope(int number, int massNumber, IsotopeInfo* out) {
    int count = 0;
    const IsotopeInfo* list = isotopes(number, &count);
    for (int i = 0; i < count; ++i) {
        if (list[i].massNumber == massNumber) {
            if (out) *out = list[i];
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------
std::string chargeSuffix(int charge) {
    if (charge == 0) return std::string();
    if (charge == 1) return "+";
    if (charge == -1) return "-";
    std::string s = intToStr(charge > 0 ? charge : -charge);
    s += (charge > 0 ? '+' : '-');
    return s;
}

std::string isotopePrefix(int massNumber) {
    if (massNumber <= 0) return std::string();
    return intToStr(massNumber);
}

// ---------------------------------------------------------------------------
// Valence selection
// ---------------------------------------------------------------------------
int conventionalValenceFor(int number, int bondOrderSum) {
    if (number < 1 || number > 118) return -1;
    const ElementInfo& e = kElements[number];
    if (e.valenceLow <= 0) return -1;          // no conventional valence at all
    if (bondOrderSum <= e.valenceLow) return e.valenceLow;
    if (e.valenceHigh >= e.valenceLow && bondOrderSum <= e.valenceHigh) return e.valenceHigh;
    // Over the conventional range.  Return the high value so the caller can see
    // that the atom is over-coordinated (the hydrogen count will be negative and
    // a warning is emitted) instead of silently dropping the excess.
    return e.valenceHigh > 0 ? e.valenceHigh : e.valenceLow;
}

} // namespace chemistry
} // namespace r62
