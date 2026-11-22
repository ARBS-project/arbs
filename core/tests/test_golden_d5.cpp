// SPDX-License-Identifier: GPL-3.0-or-later
// D5 golden replay: PCM filter coefficients, PCM pipeline (C++ coefficients and injected Java coefficients),
// rumble synth, trigger effect/bridge.
// Tolerance policy (task B2 D4-D6 instruction, planner ruling pending on widening):
//   pcm_*.txt OUT~1 (filter samples): +-1 LSB allowed ("filter +-1 LSB"); the injected-coefficient run must be exact.
//   rumble.txt / trigger.txt OUT~1 (sine-based samples): STRICT (0) is the verdict; the golden-prefix (+-1) verdict is
//   reported next to it but not used.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "arbs/pcm_pipeline.h"
#include "arbs/rumble_synth.h"
#include "arbs/trigger_bridge.h"
#include "arbs/trigger_effect.h"
#include "golden.h"
#include "mini_test.h"

using namespace arbs;
using golden::b01;

namespace {

const char* kPcmFiles[] = {
    "pcm_sine50_a30000_cap127_g1_asb_321.txt",
    "pcm_sine100_a8000_cap127_g1_asb_321.txt",
    "pcm_sine300_a16000_mixed_reset_cap127_g1_asb_321.txt",
    "pcm_sine1000_a20000_cap127_g1_asb_321.txt",
    "pcm_sine5000_a32767_cap127_g1_asb_321.txt",
    "pcm_step_mixed_cap40_g4_tanh_161.txt",
    "pcm_impulse_cap127_g1_asb_641.txt",
    "pcm_silence_burst_gate2_cap127_g1_asb_321.txt",
    "pcm_noise_lcg_limit24_pop2_trim_clear_cap127_g1_asb_321.txt",
    "pcm_noise_gate2_mixed_cap60_g2_asb_321.txt",
};

void record(const golden::Stats& st) {
    mt::report().push_back(st.summary());
    for (const std::string& e : st.firstErrors) mt::report().push_back("    " + e);
    for (const std::string& e : st.dblDiffSamples) mt::report().push_back("    double differs (within tolerance unless counted overTol): " + e);
}

// Java coefficients from pcm_coeffs.txt (float bits), by tap count.
std::map<int, std::vector<float>> javaCoefficients() {
    std::map<int, std::vector<float>> m;
    const std::vector<std::string> lines = golden::readLines("pcm_coeffs.txt");
    int taps = 0;
    for (const std::string& l : lines) {
        if (l.rfind("CASE coeffs ", 0) == 0) taps = static_cast<int>(golden::kvInt(l, "taps"));
        if (l.rfind("OUT~u h=", 0) == 0) {
            std::vector<float> h;
            const std::string list = l.substr(8);
            size_t s = 0;
            while (s < list.size()) {
                size_t e = list.find(',', s);
                if (e == std::string::npos) e = list.size();
                const std::string tok = list.substr(s, e - s);  // f:xxxxxxxx
                const uint32_t bits = static_cast<uint32_t>(std::stoul(tok.substr(2), nullptr, 16));
                float f;
                std::memcpy(&f, &bits, sizeof f);
                h.push_back(f);
                s = e + 1;
            }
            m[taps] = h;
        }
    }
    return m;
}

// ------------------------------------------------------------------ pcm_coeffs.txt
struct CoeffReplayer : golden::Replayer {
    void onCase(const std::string& l) override {
        const int taps = static_cast<int>(golden::kvInt(l, "taps"));
        const std::vector<float>& h = PcmPipeline::designFor(taps);
        std::string s = "OUT~u h=";
        for (size_t i = 0; i < h.size(); i++) {
            if (i > 0) s.push_back(',');
            s += golden::fb(h[i]);
        }
        emit(s);
    }
    void onIn(const std::string&) override { throw std::runtime_error("no IN expected"); }
};

// ------------------------------------------------------------------ pcm_<case>.txt
struct PcmReplayer : golden::Replayer {
    std::unique_ptr<PcmPipeline> p;
    int popMax = 0;
    const std::map<int, std::vector<float>>* inject = nullptr;
    void onCase(const std::string& l) override {
        p = std::make_unique<PcmPipeline>();
        p->setCap(static_cast<int>(golden::kvInt(l, "cap")));
        p->setGain(golden::kvFloatBits(l, "gain"));
        p->setGateThreshold(golden::kvFloatBits(l, "gate"));
        p->setAsbCurve(golden::kvInt(l, "asb") != 0);
        const int taps = static_cast<int>(golden::kvInt(l, "taps"));
        p->setTaps(taps);
        p->setLimit(static_cast<int>(golden::kvInt(l, "limit")));
        popMax = static_cast<int>(golden::kvInt(l, "popMax"));
        if (inject != nullptr) p->setCoefficientsForTest(inject->at(taps));
    }
    void onIn(const std::string& l) override {
        if (l.rfind("IN pcm ", 0) == 0) {
            const int frames = static_cast<int>(golden::kvInt(l, "frames"));
            const std::vector<uint8_t> b = golden::hexBytes(golden::lastToken(l));
            std::vector<int16_t> pcm(b.size() / 2);
            for (size_t i = 0; i < pcm.size(); i++) pcm[i] = static_cast<int16_t>(static_cast<uint16_t>(b[2 * i] | (b[2 * i + 1] << 8)));
            const int outs = p->process(pcm.data(), pcm.size(), frames);
            emit("OUT! outs=" + std::to_string(outs) + " size=" + std::to_string(p->queueSize()));
            int ql[PcmPipeline::CAPACITY], qr[PcmPipeline::CAPACITY];
            const int k = p->pop(ql, qr, popMax);
            emit("OUT~1 q l=" + golden::ints(ql, k) + " r=" + golden::ints(qr, k));
            emit("OUT! popped=" + std::to_string(k) + " size=" + std::to_string(p->queueSize()));
            emit("OUT~d silentMs=" + golden::db(p->getSilentMs()));
        } else if (l == "IN reset") {
            p->resetFilter();
        } else if (l.rfind("IN trim ", 0) == 0) {
            const int d = p->trimTo(static_cast<int>(golden::kvInt(l, "keep")));
            emit("OUT! trimmed=" + std::to_string(d) + " size=" + std::to_string(p->queueSize()));
        } else if (l == "IN clear") {
            p->clearQueue();
            emit("OUT! size=" + std::to_string(p->queueSize()));
        } else if (l == "IN levels") {
            const int64_t dropped = p->getAndResetDropped();
            const int peak = p->getAndResetPeak();
            emit("OUT! dropped=" + std::to_string(dropped) + " peak=" + std::to_string(peak));
            const std::array<double, 7> lv = p->getAndResetLevels();
            std::string s = "OUT~d levels=";
            for (size_t i = 0; i < lv.size(); i++) {
                if (i > 0) s.push_back(',');
                s += golden::db(lv[i]);
            }
            emit(s);
        } else {
            throw std::runtime_error("unknown IN");
        }
    }
};

// ------------------------------------------------------------------ rumble.txt
struct RumbleReplayer : golden::Replayer {
    std::unique_ptr<RumbleSynth> s;
    void onCase(const std::string&) override { s = std::make_unique<RumbleSynth>(); }
    void state(const std::string& pre) {
        emit(pre + "low8=" + std::to_string(s->low8()) + " high8=" + std::to_string(s->high8()) + " active=" + b01(s->isActive()));
    }
    void onIn(const std::string& l) override {
        if (l.rfind("IN amp ", 0) == 0) {
            emit("OUT! a=" + std::to_string(RumbleSynth::amplitude(static_cast<int>(golden::kvInt(l, "v8")),
                                                                  static_cast<int>(golden::kvInt(l, "cap")))));
        } else if (l.rfind("IN set ", 0) == 0) {
            s->set(static_cast<int>(golden::kvInt(l, "low")), static_cast<int>(golden::kvInt(l, "high")), golden::kvInt(l, "t"));
            state("OUT! ");
        } else if (l.rfind("IN timeout ", 0) == 0) {
            const bool to = s->checkTimeout(golden::kvInt(l, "t"));
            state(std::string("OUT! r=") + b01(to) + " ");
        } else if (l.rfind("IN mix ", 0) == 0) {
            std::vector<int> lv = golden::kvInts(l, "l"), rv = golden::kvInts(l, "r");
            const int used = s->mix(lv.data(), rv.data(), 8, static_cast<int>(golden::kvInt(l, "cap")));
            emit("OUT~1 l=" + golden::ints(lv.data(), 8) + " r=" + golden::ints(rv.data(), 8));
            emit("OUT! used=" + std::to_string(used));
        } else if (l == "IN clear") {
            s->clear();
            state("OUT! ");
        } else if (l.rfind("IN next ", 0) == 0) {
            const bool left = golden::kv(l, "side") == "L";
            const int cap = static_cast<int>(golden::kvInt(l, "cap"));
            emit("OUT~1 v=" + std::to_string(left ? s->nextLeft(cap) : s->nextRight(cap)));
        } else {
            throw std::runtime_error("unknown IN");
        }
    }
};

// ------------------------------------------------------------------ trigger.txt
std::string effectLine(const TriggerEffect::Decoded& d) {
    const TriggerEffect& e = d.effect;
    return "OUT! code=" + std::to_string(d.code) + " type=" + std::to_string(static_cast<int>(e.type)) + " sz=" + std::to_string(e.startZone) +
           " ez=" + std::to_string(e.endZone) + " str=" + std::to_string(e.strength) + " freq=" + std::to_string(e.frequency) +
           " snap=" + std::to_string(e.snapStrength) + " det=" + b01(e.detent) + " zones=" + golden::ints(e.zoneStrengths.data(), 10) +
           " rd=" + std::to_string(e.rhythmDrive) + " rp=" + std::to_string(e.rhythmPeriod) + " rf=" + std::to_string(e.rhythmFrom) +
           " rt=" + std::to_string(e.rhythmTo) + " pa=" + std::to_string(e.pulseAttack) + " ph=" + std::to_string(e.pulseHold) +
           " pr=" + std::to_string(e.pulseRelease);
}

struct TriggerReplayer : golden::Replayer {
    std::unique_ptr<TriggerBridge> b;
    TriggerBridge::Out out;
    void onCase(const std::string&) override { b = std::make_unique<TriggerBridge>(); }
    void onIn(const std::string& l) override {
        if (l.rfind("IN decode ", 0) == 0) {
            const int type = static_cast<int>(std::stoi(golden::kv(l, "type").substr(2), nullptr, 16));
            const std::vector<uint8_t> p = golden::hexBytes(golden::kv(l, "p"));
            emit(effectLine(TriggerEffect::decode(type, p.data())));
        } else if (l.rfind("IN scale ", 0) == 0) {
            emit("OUT! v=" + std::to_string(TriggerEffect::scaleStrength(static_cast<int>(golden::kvInt(l, "s")), static_cast<int>(golden::kvInt(l, "pct")))));
        } else if (l.rfind("IN ceil ", 0) == 0) {
            emit("OUT! v=" + std::to_string(TriggerEffect::limitCeiling(static_cast<int>(golden::kvInt(l, "s")), static_cast<int>(golden::kvInt(l, "pct")))));
        } else if (l.rfind("IN posfor ", 0) == 0) {
            emit("OUT! v=" + std::to_string(TriggerEffect::positionFor(static_cast<int>(golden::kvInt(l, "z")))));
        } else if (l.rfind("IN burstdrive ", 0) == 0) {
            emit("OUT! v=" + std::to_string(TriggerEffect::burstDriveForForce(static_cast<int>(golden::kvInt(l, "f")))));
        } else if (l.rfind("IN zonelevel ", 0) == 0) {
            const std::vector<int> z = golden::kvInts(l, "zones");
            std::array<int, 10> zs{};
            for (size_t i = 0; i < 10 && i < z.size(); i++) zs[i] = z[i];
            emit("OUT~d v=" + golden::db(TriggerEffect::zoneLevel(zs, golden::kvDoubleBits(l, "pos"))));
        } else if (l.rfind("IN shape ", 0) == 0) {
            emit("OUT~d v=" + golden::db(TriggerEffect::shapeValue(static_cast<int>(golden::kvInt(l, "a")), static_cast<int>(golden::kvInt(l, "h")),
                                                                   static_cast<int>(golden::kvInt(l, "r")), golden::kvDoubleBits(l, "hz"),
                                                                   static_cast<int>(golden::kvInt(l, "i")))));
        } else if (l.rfind("IN strength ", 0) == 0) {
            b->setStrengthPercent(static_cast<int>(golden::kvInt(l, "p")));
        } else if (l.rfind("IN ceiling ", 0) == 0) {
            b->setCeilingPercent(static_cast<int>(golden::kvInt(l, "p")));
        } else if (l.rfind("IN eff ", 0) == 0) {
            const bool left = golden::kv(l, "side") == "L";
            const int type = static_cast<int>(std::stoi(golden::kv(l, "type").substr(2), nullptr, 16));
            const std::vector<uint8_t> p = golden::hexBytes(golden::kv(l, "p"));
            const auto r = b->onEffect(left, type, p.data(), golden::kvInt(l, "t"));
            emit(std::string("OUT! ret=") + (r ? "str" : "null"));
        } else if (l.rfind("IN pos ", 0) == 0) {
            b->setPosition(static_cast<int>(golden::kvInt(l, "l")), static_cast<int>(golden::kvInt(l, "r")));
        } else if (l.rfind("IN render ", 0) == 0) {
            b->render(golden::kvInt(l, "t"), out);
            emit(std::string("OUT! en=") + b01(out.enabled) + " route=" + std::to_string(out.route));
            emit("OUT~1 s=" + golden::ints(out.samples, 8));
        } else if (l == "IN any") {
            emit(std::string("OUT! v=") + b01(b->anyEffect()));
        } else if (l == "IN stats") {
            const TriggerBridge::Stats s = b->stats();
            std::string v;
            for (size_t i = 0; i < s.size(); i++) {
                if (i > 0) v.push_back(',');
                v += std::to_string(s[i]);
            }
            emit("OUT! v=" + v);
        } else if (l == "IN clear") {
            b->clear();
        } else {
            throw std::runtime_error("unknown IN");
        }
    }
};

golden::Policy pol(long long intTol) {
    golden::Policy p;
    p.intTol = intTol;
    return p;
}

}  // namespace

TEST(golden_d5, pcm_coeffs) {
    CoeffReplayer r;
    const golden::Stats st = golden::run("pcm_coeffs.txt", r, pol(0), "coeff<=1ulp");
    record(st);
    CHECK_MSG(st.pass(), st.summary());
}

TEST(golden_d5, pcm_cpp_coefficients_pm1) {
    // C++ designed coefficients; samples +-1 LSB (filter rule), everything else exact.
    long long maxAbs = 0, diffValues = 0;
    for (const char* f : kPcmFiles) {
        PcmReplayer r;
        const golden::Stats st = golden::run(f, r, pol(1), "cpp-coeffs +-1");
        record(st);
        maxAbs = (std::max)(maxAbs, st.intMaxAbs);
        diffValues += st.intValuesDiff;
        CHECK_MSG(st.pass(), st.summary());
    }
    mt::report().push_back("pcm (C++ coefficients): max|d| = " + std::to_string(maxAbs) + ", differing samples = " + std::to_string(diffValues));
}

TEST(golden_d5, pcm_injected_java_coefficients_exact) {
    const std::map<int, std::vector<float>> java = javaCoefficients();
    CHECK_EQ(java.size(), static_cast<size_t>(3));
    for (const char* f : kPcmFiles) {
        PcmReplayer r;
        r.inject = &java;
        const golden::Stats st = golden::run(f, r, pol(0), "injected-java-coeffs exact");
        record(st);
        CHECK_MSG(st.pass(), st.summary());
        CHECK_EQ(st.intValuesDiff, 0LL);
    }
}

TEST(golden_d5, rumble_strict) {
    RumbleReplayer r;
    const golden::Stats st = golden::run("rumble.txt", r, pol(0), "strict");
    record(st);
    mt::report().push_back(std::string("rumble.txt golden-prefix(+-1) verdict: ") + (st.intMaxAbs <= 1 && st.exactMismatch == 0 && st.structural == 0 ? "OK" : "MISMATCH"));
    CHECK_MSG(st.pass(), st.summary());
}

TEST(golden_d5, trigger_strict) {
    TriggerReplayer r;
    const golden::Stats st = golden::run("trigger.txt", r, pol(0), "strict");
    record(st);
    mt::report().push_back(std::string("trigger.txt golden-prefix(+-1) verdict: ") + (st.intMaxAbs <= 1 && st.exactMismatch == 0 && st.structural == 0 ? "OK" : "MISMATCH"));
    CHECK_MSG(st.pass(), st.summary());
}
