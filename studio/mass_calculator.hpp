#pragma once

#include "formula.hpp"
#include <array>
#include <cmath>

namespace studio { namespace mass {
struct Adduct {
    const char* name;
    int charge;
    double monoDelta;
    double avgDelta;
    bool common;
};

inline const std::array<Adduct, 18>& adducts() {
    static const std::array<Adduct, 18> formulas = {{
        {"[M]+•",       1,  -0.000548579909, -0.000548579909, false},
        {"[M+H]+",      1,   1.007276466621,  1.00794,       true},
        {"[M+Na]+",     1,  22.989220702,    22.98977,       true},
        {"[M+K]+",      1,  38.963157906,    39.0983,        true},
        {"[M+NH4]+",    1,  18.033825554,    18.03846,       true},
        {"[M+H-H2O]+",  1, -17.003288231,   -17.00734,       false},
        {"[M+2H]2+",    2,   2.014552933,     2.01588,       false},
        {"[M+3H]3+",    3,   3.021829400,     3.02382,       false},
        {"[M+ACN+H]+",  1,  42.033825554,    42.06102,       false},
        {"[M+MeOH+H]+", 1,  33.033491208,    33.05010,       false},
        {"[M]-•",      -1,   0.000548579909,  0.000548579909, false},
        {"[M-H]-",     -1,  -1.007276466621, -1.00794,       true},
        {"[M+Cl]-",    -1,  34.969401262,    35.453,         true},
        {"[M+HCOO]-",  -1,  44.998202715,    45.01744,       true},
        {"[M+CH3COO]-",-1,  59.013853563,    59.04454,       true},
        {"[M+Br]-",    -1,  78.918886180,    79.904,         false},
        {"[M+NO3]-",   -1,  61.988366190,    62.00490,       false},
        {"[M-2H]2-",   -2,  -2.014552933,    -2.01588,       false}
    }};
    return formulas;
}

inline double mz(const Formula& formula, const Adduct& adduct, bool average = false) {
    const double mass = average ? formula.averageMass + adduct.avgDelta : formula.exactMass + adduct.monoDelta;
    return mass / std::abs(adduct.charge);
}
}}
