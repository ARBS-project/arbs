// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core tests - golden vector reader/comparator (format: docs/research/B2_BUILD.md D3, B2_DESIGN.md 3-3/3-4).
//   OUT!  exact string match
//   OUT~1 integers, |diff| <= intTol (pcm_*: 1 = "filter +-1 LSB"; rumble/trigger: 0 = strict, see Policy)
//   OUT~d doubles (d:bits), relative or absolute <= 1e-9
//   OUT~u floats (f:bits), ulp distance reported, pass if <= ulpTol
//   TXT   never compared
// The C++ side replays CASE/IN lines through a Replayer that emits its own OUT lines in the same text format.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace golden {

struct Policy {
    long long intTol = 0;      // OUT~1
    double relTol = 1e-9;      // OUT~d
    long long ulpTol = 1;      // OUT~u
};

struct Stats {
    std::string file;
    std::string mode;          // e.g. "strict", "injected"
    long long cases = 0;
    long long inLines = 0;
    long long exactLines = 0, exactMismatch = 0;
    long long intLines = 0, intLinesDiff = 0, intValues = 0, intValuesDiff = 0, intMaxAbs = 0, intLinesOverTol = 0;
    long long dblLines = 0, dblValues = 0, dblValuesDiff = 0, dblOverTol = 0;
    std::vector<std::string> dblDiffSamples;  // up to 4 non-identical doubles (within or over tolerance)
    double dblMaxRel = 0;
    long long fltLines = 0, fltValues = 0, fltValuesDiff = 0, fltMaxUlp = 0, fltOverTol = 0;
    long long structural = 0;  // missing/extra/prefix/shape errors
    std::vector<std::string> firstErrors;  // up to 8 samples
    bool pass() const { return exactMismatch == 0 && intLinesOverTol == 0 && dblOverTol == 0 && fltOverTol == 0 && structural == 0; }
    std::string summary() const;
};

class Replayer {
public:
    virtual ~Replayer() = default;
    virtual void onCase(const std::string& line) = 0;
    virtual void onIn(const std::string& line) = 0;
    virtual void onEnd() {}
    void emit(const std::string& s) { out_.push_back(s); }
    std::deque<std::string> out_;
};

// Runs [file] (path inside the golden dir) through [r]. Returns the statistics (never throws on mismatch).
Stats run(const std::string& file, Replayer& r, const Policy& p, const std::string& mode);

// Reads all lines of a golden file (LF). Throws if missing.
std::vector<std::string> readLines(const std::string& file);

// ---- parsing helpers
std::string kv(const std::string& line, const std::string& key);  // value of " key=" (up to the next space); throws if absent
bool hasKey(const std::string& line, const std::string& key);
long long kvInt(const std::string& line, const std::string& key);
std::vector<int> kvInts(const std::string& line, const std::string& key);   // comma list, empty allowed
double kvDoubleBits(const std::string& line, const std::string& key);       // d:xxxxxxxxxxxxxxxx
float kvFloatBits(const std::string& line, const std::string& key);         // f:xxxxxxxx
std::vector<uint8_t> hexBytes(const std::string& hex);                       // lowercase hex without spaces
std::string lastToken(const std::string& line);

// ---- formatting helpers (same text format as ArbsGoldenDumpTest.java)
std::string db(double d);
std::string fb(float f);
std::string ints(const int* a, int n);
std::string hex(const uint8_t* b, size_t n);
const char* b01(bool b);

// SHA-256 of a file (lowercase hex) - used to check MANIFEST.txt.
std::string sha256File(const std::string& path);

}  // namespace golden
