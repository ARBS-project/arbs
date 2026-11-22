// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden.h"

#include "mini_test.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace golden {

namespace {

std::vector<std::string> splitWs(const std::string& s) {
    std::vector<std::string> v;
    std::istringstream is(s);
    std::string t;
    while (is >> t) v.push_back(t);
    return v;
}

std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> v;
    if (s.empty()) return v;
    size_t start = 0;
    for (;;) {
        const size_t p = s.find(',', start);
        if (p == std::string::npos) {
            v.push_back(s.substr(start));
            break;
        }
        v.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return v;
}

uint64_t parseHex64(const std::string& h) { return std::stoull(h, nullptr, 16); }

double bitsToDouble(uint64_t b) {
    double d;
    std::memcpy(&d, &b, sizeof d);
    return d;
}

float bitsToFloat(uint32_t b) {
    float f;
    std::memcpy(&f, &b, sizeof f);
    return f;
}

// Monotonic integer key of a float for ulp distance.
long long floatOrder(uint32_t b) {
    return (b & 0x80000000u) ? -static_cast<long long>(b & 0x7FFFFFFFu) : static_cast<long long>(b);
}

void addErr(Stats& st, const std::string& m) {
    if (st.firstErrors.size() < 8) st.firstErrors.push_back(m);
}

// Compares one OUT~ line element-wise. Returns false on structural mismatch.
void compareTol(const std::string& prefix, const std::string& g, const std::string& p, Stats& st, const Policy& pol, long long lineNo) {
    const std::vector<std::string> gt = splitWs(g), pt = splitWs(p);
    if (gt.size() != pt.size()) {
        st.structural++;
        addErr(st, "line " + std::to_string(lineNo) + ": token count differs");
        return;
    }
    bool lineIntDiff = false, lineIntOver = false;
    for (size_t i = 1; i < gt.size(); i++) {
        const size_t ge = gt[i].find('='), pe = pt[i].find('=');
        if (ge == std::string::npos || pe == std::string::npos) {
            if (gt[i] != pt[i]) {
                st.structural++;
                addErr(st, "line " + std::to_string(lineNo) + ": token '" + gt[i] + "' vs '" + pt[i] + "'");
            }
            continue;
        }
        if (gt[i].substr(0, ge) != pt[i].substr(0, pe)) {
            st.structural++;
            addErr(st, "line " + std::to_string(lineNo) + ": key differs");
            continue;
        }
        const std::vector<std::string> gv = splitComma(gt[i].substr(ge + 1)), pv = splitComma(pt[i].substr(pe + 1));
        if (gv.size() != pv.size()) {
            st.structural++;
            addErr(st, "line " + std::to_string(lineNo) + ": list length " + std::to_string(gv.size()) + " vs " + std::to_string(pv.size()));
            continue;
        }
        for (size_t k = 0; k < gv.size(); k++) {
            const std::string& a = gv[k];
            const std::string& b = pv[k];
            if (a.rfind("d:", 0) == 0 && b.rfind("d:", 0) == 0) {
                st.dblValues++;
                const double x = bitsToDouble(parseHex64(a.substr(2))), y = bitsToDouble(parseHex64(b.substr(2)));
                const double diff = std::fabs(x - y);
                const double rel = diff == 0 ? 0 : diff / (std::max)(std::fabs(x), std::fabs(y));
                if (rel > st.dblMaxRel) st.dblMaxRel = rel;
                if (diff != 0) {
                    st.dblValuesDiff++;
                    if (st.dblDiffSamples.size() < 4) {
                        char msg[200];
                        std::snprintf(msg, sizeof msg, "line %lld %s[%zu]: java %.17g cpp %.17g rel %.3g", lineNo,
                                      gt[i].substr(0, ge).c_str(), k, x, y, rel);
                        st.dblDiffSamples.push_back(msg);
                    }
                }
                if (!(diff <= pol.relTol || rel <= pol.relTol)) {
                    st.dblOverTol++;
                    addErr(st, "line " + std::to_string(lineNo) + ": double " + mt::str(x) + " vs " + mt::str(y));
                }
            } else if (a.rfind("f:", 0) == 0 && b.rfind("f:", 0) == 0) {
                st.fltValues++;
                const long long u = std::llabs(floatOrder(static_cast<uint32_t>(parseHex64(a.substr(2)))) -
                                               floatOrder(static_cast<uint32_t>(parseHex64(b.substr(2)))));
                if (u != 0) st.fltValuesDiff++;
                if (u > st.fltMaxUlp) st.fltMaxUlp = u;
                if (u > pol.ulpTol) st.fltOverTol++;
            } else {
                long long x, y;
                try {
                    x = std::stoll(a);
                    y = std::stoll(b);
                } catch (...) {
                    st.structural++;
                    addErr(st, "line " + std::to_string(lineNo) + ": not a number '" + a + "' / '" + b + "'");
                    continue;
                }
                st.intValues++;
                const long long d = std::llabs(x - y);
                if (d != 0) {
                    st.intValuesDiff++;
                    lineIntDiff = true;
                }
                if (d > st.intMaxAbs) st.intMaxAbs = d;
                if (d > pol.intTol) lineIntOver = true;
            }
        }
    }
    if (prefix == "OUT~1") {
        if (lineIntDiff) st.intLinesDiff++;
        if (lineIntOver) {
            st.intLinesOverTol++;
            addErr(st, "line " + std::to_string(lineNo) + ": int diff over tolerance");
        }
    }
}

std::string prefixOf(const std::string& l) {
    const size_t sp = l.find(' ');
    return sp == std::string::npos ? l : l.substr(0, sp);
}

}  // namespace

std::string Stats::summary() const {
    std::ostringstream os;
    os << file << " [" << mode << "] " << (pass() ? "OK" : "MISMATCH") << " cases=" << cases << " in=" << inLines
       << " | OUT! " << exactLines << " mism=" << exactMismatch << " | OUT~1 lines=" << intLines << " diffLines=" << intLinesDiff
       << " values=" << intValues << " diffValues=" << intValuesDiff << " max|d|=" << intMaxAbs << " overTol=" << intLinesOverTol
       << " | OUT~d values=" << dblValues << " diff=" << dblValuesDiff << " maxRel=" << dblMaxRel << " overTol=" << dblOverTol << " | OUT~u values=" << fltValues
       << " diff=" << fltValuesDiff << " maxUlp=" << fltMaxUlp << " | structural=" << structural;
    return os.str();
}

std::vector<std::string> readLines(const std::string& file) {
    const std::string path = mt::goldenDir() + "/" + file;
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("golden file missing: " + path);
    std::vector<std::string> lines;
    std::string l;
    while (std::getline(in, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    return lines;
}

Stats run(const std::string& file, Replayer& r, const Policy& pol, const std::string& mode) {
    Stats st;
    st.file = file;
    st.mode = mode;
    const std::vector<std::string> lines = readLines(file);
    long long lineNo = 0;
    auto leftover = [&](const char* where) {
        while (!r.out_.empty()) {
            st.structural++;
            addErr(st, "line " + std::to_string(lineNo) + ": extra produced output before " + where + ": " + r.out_.front().substr(0, 80));
            r.out_.pop_front();
        }
    };
    for (const std::string& l : lines) {
        lineNo++;
        if (l.empty() || l[0] == '#') continue;
        const std::string pre = prefixOf(l);
        try {
            if (pre == "CASE") {
                leftover("CASE");
                st.cases++;
                r.onCase(l);
            } else if (pre == "IN") {
                leftover("IN");
                st.inLines++;
                r.onIn(l);
            } else if (pre == "END") {
                leftover("END");
                r.onEnd();
            } else if (pre == "TXT") {
                // informational, never compared
            } else if (pre == "OUT!" || pre == "OUT~1" || pre == "OUT~d" || pre == "OUT~u") {
                if (pre == "OUT!") st.exactLines++;
                else if (pre == "OUT~1") st.intLines++;
                else if (pre == "OUT~d") st.dblLines++;
                else st.fltLines++;
                if (r.out_.empty()) {
                    st.structural++;
                    addErr(st, "line " + std::to_string(lineNo) + ": no produced output for " + l.substr(0, 80));
                    continue;
                }
                const std::string p = r.out_.front();
                r.out_.pop_front();
                if (prefixOf(p) != pre) {
                    st.structural++;
                    addErr(st, "line " + std::to_string(lineNo) + ": prefix " + prefixOf(p) + " vs " + pre);
                    continue;
                }
                if (pre == "OUT!") {
                    if (p != l) {
                        st.exactMismatch++;
                        addErr(st, "line " + std::to_string(lineNo) + ": '" + l.substr(0, 120) + "' vs '" + p.substr(0, 120) + "'");
                    }
                } else {
                    compareTol(pre, l, p, st, pol, lineNo);
                }
            } else {
                st.structural++;
                addErr(st, "line " + std::to_string(lineNo) + ": unknown record " + pre);
            }
        } catch (const std::exception& e) {
            st.structural++;
            addErr(st, "line " + std::to_string(lineNo) + ": exception " + e.what());
        }
    }
    leftover("EOF");
    return st;
}

std::string kv(const std::string& line, const std::string& key) {
    const std::string pat = " " + key + "=";
    const size_t p = line.find(pat);
    if (p == std::string::npos) throw std::runtime_error("key " + key + " missing");
    const size_t s = p + pat.size();
    const size_t e = line.find(' ', s);
    return line.substr(s, e == std::string::npos ? std::string::npos : e - s);
}

bool hasKey(const std::string& line, const std::string& key) { return line.find(" " + key + "=") != std::string::npos; }

long long kvInt(const std::string& line, const std::string& key) { return std::stoll(kv(line, key)); }

std::vector<int> kvInts(const std::string& line, const std::string& key) {
    std::vector<int> v;
    for (const std::string& t : splitComma(kv(line, key))) v.push_back(std::stoi(t));
    return v;
}

double kvDoubleBits(const std::string& line, const std::string& key) {
    const std::string v = kv(line, key);
    if (v.rfind("d:", 0) != 0) throw std::runtime_error("not d: bits");
    return bitsToDouble(parseHex64(v.substr(2)));
}

float kvFloatBits(const std::string& line, const std::string& key) {
    const std::string v = kv(line, key);
    if (v.rfind("f:", 0) != 0) throw std::runtime_error("not f: bits");
    return bitsToFloat(static_cast<uint32_t>(parseHex64(v.substr(2))));
}

std::vector<uint8_t> hexBytes(const std::string& h) {
    std::vector<uint8_t> b;
    b.reserve(h.size() / 2);
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(static_cast<uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
    return b;
}

std::string lastToken(const std::string& line) {
    const size_t p = line.rfind(' ');
    return p == std::string::npos ? line : line.substr(p + 1);
}

std::string db(double d) {
    uint64_t b;
    std::memcpy(&b, &d, sizeof b);
    char buf[32];
    std::snprintf(buf, sizeof buf, "d:%016llx", static_cast<unsigned long long>(b));
    return buf;
}

std::string fb(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof b);
    char buf[16];
    std::snprintf(buf, sizeof buf, "f:%08x", static_cast<unsigned>(b));
    return buf;
}

std::string ints(const int* a, int n) {
    std::string s;
    for (int i = 0; i < n; i++) {
        if (i > 0) s.push_back(',');
        s += std::to_string(a[i]);
    }
    return s;
}

std::string hex(const uint8_t* b, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s.push_back(d[b[i] >> 4]);
        s.push_back(d[b[i] & 15]);
    }
    return s;
}

const char* b01(bool b) { return b ? "1" : "0"; }

// ---- SHA-256 (FIPS 180-4), standard C++ only
namespace {
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64] = {0};
    size_t blen = 0;
    uint64_t total = 0;
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) | (uint32_t(p[4 * i + 2]) << 8) | uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + mj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const uint8_t* p, size_t n) {
        total += n;
        while (n > 0) {
            const size_t take = (std::min)(n, size_t(64) - blen);
            std::memcpy(buf + blen, p, take);
            blen += take;
            p += take;
            n -= take;
            if (blen == 64) {
                block(buf);
                blen = 0;
            }
        }
    }
    std::string final() {
        const uint64_t bits = total * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (blen != 56) update(&zero, 1);
        uint8_t lb[8];
        for (int i = 0; i < 8; i++) lb[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        update(lb, 8);
        std::string s;
        char x[9];
        for (uint32_t v : h) {
            std::snprintf(x, sizeof x, "%08x", static_cast<unsigned>(v));
            s += x;
        }
        return s;
    }
};
}  // namespace

std::string sha256File(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    Sha256 s;
    char b[65536];
    while (in) {
        in.read(b, sizeof b);
        const std::streamsize n = in.gcount();
        if (n > 0) s.update(reinterpret_cast<const uint8_t*>(b), static_cast<size_t>(n));
    }
    return s.final();
}

}  // namespace golden
