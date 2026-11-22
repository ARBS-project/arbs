// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core tests - minimal self-contained test framework (no external downloads). Header only.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mt {

struct Case {
    std::string group;
    std::string name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Reg {
    Reg(const char* g, const char* n, void (*f)()) { registry().push_back(Case{g, n, f}); }
};

struct Failure : std::runtime_error {
    explicit Failure(const std::string& m) : std::runtime_error(m) {}
};

inline long long& checks() {
    static long long c = 0;
    return c;
}

// Golden data directory (set by main from argv).
inline std::string& goldenDir() {
    static std::string d = "tests/golden";
    return d;
}

// Free-form report lines printed after the run (golden tables, engine scenario summaries).
inline std::vector<std::string>& report() {
    static std::vector<std::string> r;
    return r;
}

[[noreturn]] inline void fail(const char* file, int line, const std::string& msg) {
    std::ostringstream os;
    os << file << ":" << line << ": " << msg;
    throw Failure(os.str());
}

// Non-constexpr pass-through so that checks on compile-time constants do not raise C4127 under /W4.
inline bool truth(bool b) { return b; }

template <typename T>
std::string str(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

}  // namespace mt

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST(group, name)                                                                  \
    static void MT_CAT(group, MT_CAT(_, name))();                                          \
    static mt::Reg MT_CAT(reg_, MT_CAT(group, MT_CAT(_, name)))(#group, #name,             \
                                                                &MT_CAT(group, MT_CAT(_, name))); \
    static void MT_CAT(group, MT_CAT(_, name))()

#define CHECK(c)                                              \
    do {                                                      \
        mt::checks()++;                                       \
        if (!mt::truth(static_cast<bool>(c))) mt::fail(__FILE__, __LINE__, "CHECK(" #c ")"); \
    } while (0)

#define CHECK_MSG(c, m)                                                     \
    do {                                                                    \
        mt::checks()++;                                                     \
        if (!mt::truth(static_cast<bool>(c))) mt::fail(__FILE__, __LINE__, std::string("CHECK(" #c ") ") + (m)); \
    } while (0)

#define CHECK_EQ(a, b)                                                                                        \
    do {                                                                                                      \
        mt::checks()++;                                                                                       \
        const auto va_ = (a);                                                                                 \
        const auto vb_ = (b);                                                                                 \
        if (!mt::truth(va_ == vb_)) mt::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ") " + mt::str(va_) + " != " + mt::str(vb_)); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                    \
    do {                                                                                                         \
        mt::checks()++;                                                                                          \
        const double va_ = static_cast<double>(a);                                                               \
        const double vb_ = static_cast<double>(b);                                                               \
        if (!(std::fabs(va_ - vb_) <= (eps)))                                                                    \
            mt::fail(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b ") " + mt::str(va_) + " vs " + mt::str(vb_));  \
    } while (0)

#define CHECK_THROWS(expr)                                                     \
    do {                                                                       \
        mt::checks()++;                                                        \
        bool thrown_ = false;                                                  \
        try {                                                                  \
            (void)(expr);                                                      \
        } catch (const std::invalid_argument&) {                               \
            thrown_ = true;                                                    \
        }                                                                      \
        if (!thrown_) mt::fail(__FILE__, __LINE__, "CHECK_THROWS(" #expr ")"); \
    } while (0)
