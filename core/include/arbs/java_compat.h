// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - Java semantics helpers (B2_DESIGN.md section 2-4 #7, #12, #13).
// Java casts saturate and map NaN to 0; C++ casts of out-of-range doubles are undefined, so every double->integer
// conversion in the Core goes through these helpers. java.lang.Math.round differs from std::round for negative ties
// (Java -2.5 -> -2, C++ -> -3), so javaRound() is used wherever ARBS-A calls Math.round.
#pragma once

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

namespace arbs {
namespace jc {

// Same IEEE double as java.lang.Math.PI.
constexpr double kPi = 3.141592653589793;

// Java (long) cast of a double: NaN -> 0, saturating.
inline int64_t d2l(double v) {
    if (std::isnan(v)) return 0;
    if (v >= 9223372036854775807.0) return (std::numeric_limits<int64_t>::max)();
    if (v <= -9223372036854775808.0) return (std::numeric_limits<int64_t>::min)();
    return static_cast<int64_t>(v);
}

// Java (int) cast of a double: NaN -> 0, saturating.
inline int32_t d2i(double v) {
    if (std::isnan(v)) return 0;
    if (v >= 2147483647.0) return (std::numeric_limits<int32_t>::max)();
    if (v <= -2147483648.0) return (std::numeric_limits<int32_t>::min)();
    return static_cast<int32_t>(v);
}

// Java (int) cast of a long: keep the low 32 bits (two's complement wrap).
inline int32_t l2i(int64_t v) {
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(v)));
}

// java.lang.Math.round(double) (Java 7+): mathematically floor(x + 1/2), NaN -> 0, saturating.
// x - floor(x) is exact for |x| < 2^52, so the comparison with 0.5 has no rounding error.
inline int64_t javaRound(double x) {
    if (std::isnan(x)) return 0;
    if (!(std::fabs(x) < 4503599627370496.0)) return d2l(x);  // |x| >= 2^52: already an integer (or inf)
    const double f = std::floor(x);
    return d2l((x - f >= 0.5) ? f + 1.0 : f);
}

// ApexTriggerEffect.lround (ApexTriggerEffect.java:238-240): (long)(v < 0 ? ceil(v - 0.5) : floor(v + 0.5)).
inline int64_t lroundJ(double v) {
    return d2l(v < 0 ? std::ceil(v - 0.5) : std::floor(v + 0.5));
}

template <typename T>
inline T clampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Java Math.max/Math.min for doubles without NaN handling differences that matter here (inputs are finite).
inline double dmax(double a, double b) { return a >= b ? a : b; }
inline double dmin(double a, double b) { return a <= b ? a : b; }

// String.format(Locale.US, ...) replacement. NOTE: printf rounds the exact binary value (ties to even on exact
// ties) while java.util.Formatter rounds the decimal expansion HALF_UP; log strings are therefore never compared,
// only numeric fields (golden session_stats.txt asserts there are no ties).
inline std::string fmt(const char* f, ...) {
    char stackBuf[512];
    va_list ap;
    va_start(ap, f);
    va_list ap2;
    va_copy(ap2, ap);
    const int n = std::vsnprintf(stackBuf, sizeof(stackBuf), f, ap);
    va_end(ap);
    std::string out;
    if (n < 0) {
        va_end(ap2);
        return out;
    }
    if (static_cast<size_t>(n) < sizeof(stackBuf)) {
        out.assign(stackBuf, static_cast<size_t>(n));
    } else {
        out.resize(static_cast<size_t>(n) + 1);
        std::vsnprintf(&out[0], out.size(), f, ap2);
        out.resize(static_cast<size_t>(n));
    }
    va_end(ap2);
    return out;
}

}  // namespace jc
}  // namespace arbs
