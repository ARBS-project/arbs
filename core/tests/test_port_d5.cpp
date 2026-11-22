// SPDX-License-Identifier: GPL-3.0-or-later
// D5 ported unit tests: ARBS-A ApexPcmPipelineTest, ApexGateFilterTest, ApexParityTest, ApexRumbleSynthTest,
// ApexTriggerTest (app/src/test/java/com/limelight/arbs, f3051641). Same assertions and tolerances.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "arbs/java_compat.h"
#include "arbs/pcm_pipeline.h"
#include "arbs/protocol.h"
#include "arbs/rumble_synth.h"
#include "arbs/trigger_bridge.h"
#include "arbs/trigger_effect.h"
#include "mini_test.h"

using namespace arbs;
using arbs::jc::javaRound;
using arbs::jc::kPi;
namespace pr = arbs::protocol;
using TE = arbs::TriggerEffect;

namespace {
constexpr int64_t MS = 1000000LL;

int16_t s16cast(int64_t v) { return static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint64_t>(v))); }  // Java (short)

std::vector<int16_t> tone(double hz, double amp, int startFrame, int frames, bool left, bool right) {
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; i++) {
        const double v = std::sin(2.0 * kPi * hz * (startFrame + i) / 48000.0) * amp;
        const int16_t s = s16cast(javaRound(v));
        pcm[2 * static_cast<size_t>(i)] = left ? s : 0;
        pcm[2 * static_cast<size_t>(i) + 1] = right ? s : 0;
    }
    return pcm;
}

int process(PcmPipeline& p, const std::vector<int16_t>& pcm, int frames) { return p.process(pcm.data(), pcm.size(), frames); }

std::array<int, 2> peaks(PcmPipeline& p, double hz, double amp, bool left, bool right) {
    int l[8], r[8];
    const int skip = 40;
    int seen = 0, pl = 0, pr_ = 0;
    for (int blk = 0; blk < 40; blk++) {
        process(p, tone(hz, amp, blk * 240, 240, left, right), 240);
        int n;
        while ((n = p.pop(l, r, 8)) > 0) {
            for (int i = 0; i < n; i++) {
                if (seen++ < skip) continue;
                pl = (std::max)(pl, std::abs(l[i]));
                pr_ = (std::max)(pr_, std::abs(r[i]));
            }
        }
    }
    return {pl, pr_};
}

double sum(const std::vector<float>& h) {
    double s = 0;
    for (float v : h) s += v;
    return s;
}
}  // namespace

// ---------------------------------------------------------------- ApexPcmPipelineTest
TEST(port_pcm, filterDcGainIsOne) { CHECK_NEAR(sum(PcmPipeline::designFor(321)), 1.0, 1e-4); }

TEST(port_pcm, decimationProduces48To1) {
    PcmPipeline p;
    CHECK_EQ(process(p, std::vector<int16_t>(960), 480), 10);
    CHECK_EQ(process(p, std::vector<int16_t>(94), 47), 0);
    CHECK_EQ(process(p, std::vector<int16_t>(2), 1), 1);
}

TEST(port_pcm, passband100HzKeepsLevel) {
    PcmPipeline p;
    p.setCap(60);
    const auto pk = peaks(p, 100.0, 10000, true, true);
    const double expect = 10000.0 / 32768 * 60;
    CHECK_MSG(std::fabs(pk[0] - expect) <= 2.0, "peak=" + std::to_string(pk[0]));
    CHECK(std::fabs(pk[1] - expect) <= 2.0);
}

TEST(port_pcm, passband300HzMostlyKept) {
    PcmPipeline p;
    p.setCap(60);
    const auto pk = peaks(p, 300.0, 10000, true, true);
    CHECK(pk[0] >= 10000.0 / 32768 * 60 * 0.65);
}

TEST(port_pcm, stopbandAliasCandidatesAreSuppressed) {
    for (double hz : {3000.0, 1010.0, 5000.0}) {
        PcmPipeline p;
        p.setCap(60);
        const auto pk = peaks(p, hz, 20000, true, true);
        CHECK_MSG(pk[0] <= 1, std::to_string(hz) + "Hz leaked " + std::to_string(pk[0]));
    }
}

TEST(port_pcm, channelsStayIndependent) {
    PcmPipeline p;
    p.setCap(60);
    auto pk = peaks(p, 150.0, 10000, true, false);
    CHECK(pk[0] >= 15);
    CHECK_EQ(pk[1], 0);
    PcmPipeline q;
    q.setCap(60);
    pk = peaks(q, 150.0, 10000, false, true);
    CHECK_EQ(pk[0], 0);
    CHECK(pk[1] >= 15);
}

TEST(port_pcm, scaleFormulaAndCaps) {
    PcmPipeline p;
    p.setAsbCurve(false);
    p.setCap(60);
    CHECK_EQ(p.scale(5000.0f), 9);
    CHECK_EQ(p.scale(8192.0f), 15);
    CHECK_EQ(p.scale(-5000.0f), -9);
    CHECK_EQ(p.scale(16384.0f), 30);
    const int full = p.scale(32767.0f);
    CHECK_MSG(full >= 55 && full <= 60, "full=" + std::to_string(full));
    CHECK_EQ(p.scale(-32767.0f), -full);
    CHECK(std::abs(p.scale(-32768.0f)) <= 60);
    p.setCap(40);
    CHECK_EQ(p.scale(16384.0f), 20);
    const int f40 = p.scale(32767.0f);
    CHECK(f40 >= 36 && f40 <= 40);
    CHECK_EQ(p.scale(5000.0f), 6);
    p.setCap(0);
    CHECK_EQ(p.scale(32767.0f), 0);
}

TEST(port_pcm, capCannotExceedHardLimit) {
    PcmPipeline p;
    p.setCap(127);
    CHECK_EQ(p.getCap(), 127);
    p.setCap(300);
    CHECK_EQ(p.getCap(), 127);
    CHECK(p.scale(32767.0f) <= 127);
    p.setCap(-5);
    CHECK_EQ(p.getCap(), 0);
    p.setCap(61);
    CHECK_EQ(p.getCap(), 61);
}

TEST(port_pcm, gainScalesBeforeCap) {
    PcmPipeline p;
    p.setAsbCurve(false);
    p.setCap(60);
    p.setGain(0.5f);
    CHECK_EQ(p.scale(8192.0f), 8);
    p.setGain(2.0f);
    CHECK_EQ(p.scale(8192.0f), 30);
    CHECK(p.scale(32767.0f) <= 60);
    p.setGain(100.0f);
    CHECK_NEAR(p.getGain(), 8.0f, 1e-6);
}

TEST(port_pcm, queueDropsOldestBeyond24) {
    PcmPipeline p;
    for (int i = 0; i < 20; i++) process(p, tone(100.0, 10000, i * 240, 240, true, true), 240);
    CHECK_EQ(p.queueSize(), PcmPipeline::QUEUE_LIMIT);
    CHECK_EQ(p.getAndResetDropped(), 100 - PcmPipeline::QUEUE_LIMIT);
    CHECK_EQ(p.getAndResetDropped(), 0);
    int l[8], r[8];
    CHECK_EQ(p.pop(l, r, 8), 8);
    CHECK_EQ(p.queueSize(), PcmPipeline::QUEUE_LIMIT - 8);
    p.clearQueue();
    CHECK_EQ(p.queueSize(), 0);
}

TEST(port_pcm, resetFilterForgetsHistory) {
    PcmPipeline p;
    p.setCap(60);
    process(p, std::vector<int16_t>(1920, 20000), 960);
    p.clearQueue();
    p.resetFilter();
    process(p, std::vector<int16_t>(96), 48);
    int l[8], r[8];
    CHECK_EQ(p.pop(l, r, 8), 1);
    CHECK_EQ(l[0], 0);
    CHECK_EQ(r[0], 0);
}

TEST(port_pcm, outputPeakTracksAndResets) {
    PcmPipeline p;
    p.setCap(60);
    process(p, std::vector<int16_t>(1920, 20000), 960);
    const int pk = p.getAndResetPeak();
    CHECK_MSG(pk >= 37 && pk <= 41, "peak=" + std::to_string(pk));
    CHECK_EQ(p.getAndResetPeak(), 0);
}

TEST(port_pcm, limiterNeverExceedsCapForAnyInputAndGain) {
    for (int cap : {10, 40, 60}) {
        for (float g : {0.5f, 1.0f, 2.0f, 4.0f}) {
            PcmPipeline p;
            p.setCap(cap);
            p.setGain(g);
            for (int v = -32768; v <= 32767; v += 97) CHECK(std::abs(p.scale(static_cast<float>(v))) <= cap);
        }
    }
}

TEST(port_pcm, smallSignalsStayProportionalBelowKnee) {
    PcmPipeline p;
    p.setAsbCurve(false);
    p.setCap(60);
    for (int v = 0; v <= 26000; v += 1300) CHECK_EQ(static_cast<int64_t>(p.scale(static_cast<float>(v))), javaRound(v / 32768.0 * 60));
    CHECK_NEAR(2 * p.scale(3000.0f), p.scale(6000.0f), 1);
}

TEST(port_pcm, limiterIsMonotonicAndSmoothAboveKnee) {
    PcmPipeline p;
    p.setAsbCurve(false);
    p.setCap(60);
    int prev = -1;
    for (int v = 0; v <= 32767; v += 64) {
        const int o = p.scale(static_cast<float>(v));
        CHECK(o >= prev);
        prev = o;
    }
}

TEST(port_pcm, levelStatsReportLimiterAndRms) {
    PcmPipeline p;
    p.setAsbCurve(false);
    p.setCap(60);
    for (int i = 0; i < 100; i++) p.scale(10000.0f);
    const auto a = p.getAndResetLevels();
    CHECK_NEAR(a[0], 100, 0);
    CHECK_NEAR(a[1], 0, 0);
    CHECK_NEAR(a[2], 0, 0);
    CHECK_NEAR(a[3], 30.5, 0.1);
    CHECK_NEAR(a[4], 18, 0.5);
    for (int i = 0; i < 100; i++) p.scale(32767.0f);
    const auto b = p.getAndResetLevels();
    CHECK_NEAR(b[1], 100, 0);
    CHECK_NEAR(p.getAndResetLevels()[0], 0, 0);
}

// ---------------------------------------------------------------- ApexGateFilterTest
namespace {
std::vector<int> feed(PcmPipeline& p, double hz, double amp, int blocks, int& startFrame) {
    std::vector<int> out;
    int l[8], r[8];
    for (int b = 0; b < blocks; b++) {
        std::vector<int16_t> pcm(480);
        if (amp != 0) {
            pcm = tone(hz, amp, startFrame, 240, true, true);
        }
        startFrame += 240;
        process(p, pcm, 240);
        int n;
        while ((n = p.pop(l, r, 8)) > 0) {
            for (int i = 0; i < n; i++) out.push_back((std::max)(std::abs(l[i]), std::abs(r[i])));
        }
    }
    return out;
}

int maxFrom(const std::vector<int>& v, size_t from, size_t to = static_cast<size_t>(-1)) {
    int m = 0;
    for (size_t i = from; i < v.size() && i < to; i++) m = (std::max)(m, v[i]);
    return m;
}

double gainAt(int taps, double hz) {
    const std::vector<float>& h = PcmPipeline::designFor(taps);
    double re = 0, im = 0;
    const double w = 2 * kPi * hz / PcmPipeline::SAMPLE_RATE;
    for (size_t i = 0; i < h.size(); i++) {
        re += h[i] * std::cos(w * static_cast<double>(i));
        im -= h[i] * std::sin(w * static_cast<double>(i));
    }
    return std::hypot(re, im);
}

double overshoot(int taps) {
    double c = 0, mx = 0;
    for (float v : PcmPipeline::designFor(taps)) {
        c += v;
        mx = (std::max)(mx, c);
    }
    return mx - 1;
}
}  // namespace

TEST(port_gate, filterChoicesHaveExpectedDelayAndUnityDc) {
    const double expect[3] = {1.67, 3.33, 6.67};
    for (int i = 0; i < 3; i++) {
        const int t = PcmPipeline::TAP_CHOICES[i];
        const double ms = (t - 1) / 2.0 / PcmPipeline::SAMPLE_RATE * 1000.0;
        CHECK_NEAR(ms, expect[i], 0.01);
        CHECK_NEAR(sum(PcmPipeline::designFor(t)), 1.0, 1e-4);
    }
    CHECK_EQ(PcmPipeline::TAPS, 321);
    PcmPipeline p;
    CHECK_EQ(p.getTaps(), 321);
    p.setTaps(161);
    CHECK_EQ(p.getTaps(), 161);
    p.setTaps(641);
    CHECK_EQ(p.getTaps(), 641);
    p.setTaps(999);
    CHECK_EQ(p.getTaps(), 321);
}

TEST(port_gate, responseAndOvershootReport) {
    for (int taps : PcmPipeline::TAP_CHOICES) {
        std::string sb = "FILTER taps=" + std::to_string(taps) + " gain@Hz:";
        for (double f : {50.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 1010.0, 3000.0, 5000.0}) {
            sb += arbs::jc::fmt(" %.0f=%.3f", f, gainAt(taps, f));
        }
        sb += arbs::jc::fmt(" | step overshoot %.2f%%", overshoot(taps) * 100);
        mt::report().push_back(sb);
        CHECK(gainAt(taps, 100) > 0.9);
        CHECK(gainAt(taps, 300) > 0.65);
        CHECK(gainAt(taps, 1010) < 0.01 && gainAt(taps, 3000) < 0.001);
        CHECK(overshoot(taps) < 0.09);
    }
    CHECK(gainAt(321, 600) < gainAt(161, 600));
    CHECK(gainAt(641, 600) < gainAt(321, 600));
}

TEST(port_gate, gateClosesQuietInputAfter50msAndFadesOut) {
    PcmPipeline p;
    p.setCap(60);
    p.setGain(8.0f);
    p.setGateThreshold(2.0f);
    int st = 0;
    const std::vector<int> v = feed(p, 150, 328 * 1.414, 100, st);
    CHECK(v.size() > 400);
    CHECK(maxFrom(v, 0, 50) > 0);
    CHECK_EQ(maxFrom(v, 70), 0);
    const auto lv = p.getAndResetLevels();
    CHECK(lv[6] > 70);
}

TEST(port_gate, gateReopensQuicklyOnLoudInputAndOffKeepsQuietSignal) {
    PcmPipeline p;
    p.setCap(60);
    p.setGateThreshold(2.0f);
    int st = 0;
    feed(p, 0, 0, 40, st);
    const std::vector<int> loud = feed(p, 100, 10000, 8, st);
    CHECK(maxFrom(loud, 10) >= 10);
    PcmPipeline q;
    q.setCap(60);
    q.setGain(8.0f);
    q.setGateThreshold(0.0f);
    int st2 = 0;
    const std::vector<int> quiet = feed(q, 150, 328 * 1.414, 100, st2);
    CHECK(maxFrom(quiet, 100) > 0);
}

TEST(port_gate, loudSignalPassesGateOpen) {
    PcmPipeline p;
    p.setCap(60);
    p.setGateThreshold(2.0f);
    int st = 0;
    const std::vector<int> v = feed(p, 100, 10000, 100, st);
    CHECK(maxFrom(v, 40) >= 15);
    CHECK_NEAR(p.getAndResetLevels()[6], 0, 0.0001);
}

TEST(port_gate, highGainNeverExceedsCapEvenWithManyLimiterHits) {
    for (int cap : {20, 40, 60, 100, 127}) {
        for (bool asb : {true, false}) {
            for (float g : {1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f}) {
                PcmPipeline p;
                p.setAsbCurve(asb);
                p.setCap(cap);
                p.setGain(g);
                for (int v = -32768; v <= 32767; v += 31) CHECK(std::abs(p.scale(static_cast<float>(v))) <= cap);
                const auto lv = p.getAndResetLevels();
                if (g >= 6.0f) CHECK(lv[1] > 50);
            }
        }
    }
    PcmPipeline p;
    p.setGain(100.0f);
    CHECK_NEAR(p.getGain(), 8.0f, 1e-6);
}

TEST(port_gate, gainThreeGivesAboutThreeTimesForSmallSignals) {
    PcmPipeline p;
    p.setCap(40);
    const int g1 = p.scale(2000.0f);
    p.setGain(3.0f);
    const int g3 = p.scale(2000.0f);
    CHECK_NEAR(g1 * 3.0, g3, 1.0);
}

// ---------------------------------------------------------------- ApexParityTest
namespace {
int asbScale(int sample, int gainPercent) {
    const int64_t gain = (std::max)(static_cast<int64_t>(100), (std::min)(static_cast<int64_t>(200), static_cast<int64_t>(gainPercent)));
    const int64_t fullScale = sample >= 0 ? 32767 : 32768;
    if (gain == 100) return static_cast<int>(static_cast<int64_t>(sample) * 127 / fullScale);
    const int64_t magnitude = std::llabs(static_cast<int64_t>(sample));
    const int64_t denominator = fullScale * 100 + (gain - 100) * magnitude;
    return static_cast<int>(static_cast<int64_t>(sample) * 127 * gain / denominator);
}

void asbLike(PcmPipeline& p, float g) {
    p.setAsbCurve(true);
    p.setCap(127);
    p.setGain(g);
}
}  // namespace

TEST(port_parity, gainOneCap127MatchesAsbDefaultWithin1) {
    PcmPipeline p;
    asbLike(p, 1.0f);
    for (int v = -32768; v <= 32767; v += 211) CHECK_NEAR(asbScale(v, 100), p.scale(static_cast<float>(v)), 1.0);
    CHECK_EQ(p.scale(32767.0f), 127);
    CHECK_EQ(p.scale(-32768.0f), -127);
}

TEST(port_parity, asbCurveEndpointsAndSlopeMatchForGain2) {
    PcmPipeline p;
    asbLike(p, 2.0f);
    for (int v = -32768; v <= 32767; v += 173) CHECK_NEAR(asbScale(v, 200), p.scale(static_cast<float>(v)), 1.0);
    CHECK_EQ(p.scale(32767.0f), 127);
}

TEST(port_parity, asbCurveSmallSignalSlopeIsGainAndOutputNeverExceedsCap) {
    for (float g : {1.0f, 2.0f, 4.0f, 8.0f}) {
        PcmPipeline p;
        asbLike(p, g);
        p.setCap(100);
        const double expect = g * 0.01 / (1 + (g - 1) * 0.01) * 100;
        CHECK_NEAR(expect, p.scale(327.68f), 0.6);
        for (int v = -32768; v <= 32767; v += 97) CHECK(std::abs(p.scale(static_cast<float>(v))) <= 100);
    }
}

TEST(port_parity, asbCurveIsMonotonicAndOdd) {
    PcmPipeline p;
    asbLike(p, 4.0f);
    int prev = -200;
    for (int v = -32768; v <= 32767; v += 64) {
        const int o = p.scale(static_cast<float>(v));
        CHECK(o >= prev);
        prev = o;
    }
    CHECK_EQ(-p.scale(9000.0f), p.scale(-9000.0f));
}

TEST(port_parity, defaultsFollowTheTaskSpec) {
    PcmPipeline p;
    CHECK(p.isAsbCurve());
    CHECK_NEAR(p.getGateThreshold(), 0.0f, 0.0);
    CHECK_EQ(p.getTaps(), 321);
    CHECK_EQ(p.getCap(), PcmPipeline::DEFAULT_CAP);
    CHECK_EQ(PcmPipeline::DEFAULT_CAP, 40);
}

TEST(port_parity, silentMsCountsQuietBlocksAndResetsOnSignal) {
    PcmPipeline p;
    const std::vector<int16_t> quiet(480);
    for (int i = 0; i < 400; i++) process(p, quiet, 240);
    CHECK(p.getSilentMs() >= 1990);
    process(p, std::vector<int16_t>(480, 3000), 240);
    CHECK_NEAR(p.getSilentMs(), 0.0, 0.001);
    const std::vector<int16_t> faint(480, 65);
    for (int i = 0; i < 20; i++) process(p, faint, 240);
    CHECK(p.getSilentMs() >= 95);
}

TEST(port_parity, rumbleAmplitudeMatchesAsb112AtFullCap) {
    CHECK_EQ(RumbleSynth::amplitude(255, 127), 112);
    CHECK_EQ(RumbleSynth::amplitude(128, 127), 56);
}

// ---------------------------------------------------------------- ApexRumbleSynthTest
namespace {
std::array<std::vector<int>, 2> runRumble(RumbleSynth& s, int cap, int frames) {
    std::array<std::vector<int>, 2> o{std::vector<int>(static_cast<size_t>(frames) * 8), std::vector<int>(static_cast<size_t>(frames) * 8)};
    int l[8], r[8];
    for (int f = 0; f < frames; f++) {
        std::fill(l, l + 8, 0);
        std::fill(r, r + 8, 0);
        s.mix(l, r, 8, cap);
        std::copy(l, l + 8, o[0].begin() + f * 8);
        std::copy(r, r + 8, o[1].begin() + f * 8);
    }
    return o;
}

int peak(const int* a, size_t n) {
    int p = 0;
    for (size_t i = 0; i < n; i++) p = (std::max)(p, std::abs(a[i]));
    return p;
}
int peak(const std::vector<int>& a) { return peak(a.data(), a.size()); }

int zeroCrossings(const std::vector<int>& a) {
    int c = 0;
    for (size_t i = 1; i < a.size(); i++)
        if ((a[i - 1] < 0 && a[i] >= 0) || (a[i - 1] > 0 && a[i] <= 0)) c++;
    return c;
}
}  // namespace

TEST(port_rumble, amplitudeUsesUpper8BitsAndCap) {
    CHECK_EQ(RumbleSynth::amplitude(0, 60), 0);
    CHECK_EQ(RumbleSynth::amplitude(255, 60), 60);
    CHECK_EQ(RumbleSynth::amplitude(255, 40), 40);
    CHECK_EQ(RumbleSynth::amplitude(128, 60), 30);
    CHECK_EQ(RumbleSynth::amplitude(255, 127), 112);
    CHECK_EQ(RumbleSynth::amplitude(255, 112), 112);
    CHECK_EQ(RumbleSynth::amplitude(255, 100), 100);
    CHECK_EQ(RumbleSynth::amplitude(255, -3), 0);
}

TEST(port_rumble, upperByteOfSixteenBitValueIsUsed) {
    RumbleSynth s;
    s.set(0xFFFF, 0x8000, 0);
    CHECK_EQ(s.low8(), 255);
    CHECK_EQ(s.high8(), 128);
    s.set(0x00FF, 0x0100, 0);
    CHECK_EQ(s.low8(), 0);
    CHECK_EQ(s.high8(), 1);
}

TEST(port_rumble, lowIs65HzLeftAndHighIs150HzRight) {
    RumbleSynth s;
    s.set(0xFF00, 0xFF00, 0);
    const auto o = runRumble(s, 60, 125);
    CHECK(peak(o[0]) >= 58 && peak(o[0]) <= 60);
    CHECK(peak(o[1]) >= 58 && peak(o[1]) <= 60);
    const int zl = zeroCrossings(o[0]), zr = zeroCrossings(o[1]);
    CHECK_MSG(zl >= 120 && zl <= 140, std::to_string(zl));
    CHECK_MSG(zr >= 285 && zr <= 315, std::to_string(zr));
}

TEST(port_rumble, onlyLowSetLeavesRightSilent) {
    RumbleSynth s;
    s.set(0x8000, 0, 0);
    const auto o = runRumble(s, 60, 50);
    CHECK(peak(o[0]) >= 28);
    CHECK_EQ(peak(o[1]), 0);
}

TEST(port_rumble, neverExceedsHardCap) {
    RumbleSynth s;
    s.set(0xFFFF, 0xFFFF, 0);
    const auto o = runRumble(s, 127, 200);
    CHECK(peak(o[0]) <= 112 && peak(o[0]) >= 108);
    CHECK(peak(o[1]) <= 112 && peak(o[1]) >= 108);
}

TEST(port_rumble, pcmNonZeroChannelWinsPerChannel) {
    RumbleSynth s;
    s.set(0xFF00, 0xFF00, 0);
    int l[8] = {0, 0, 5, 0, 0, 0, 0, 0};
    int r[8] = {0};
    CHECK_EQ(s.mix(l, r, 8, 60), 1);
    CHECK_EQ(l[2], 5);
    CHECK_EQ(l[0], 0);
    CHECK(peak(r, 8) > 0);
}

TEST(port_rumble, silentPcmFallsBackToRumbleAndZeroRumbleLeavesPcm) {
    RumbleSynth s;
    int l[8] = {0}, r[8] = {0};
    CHECK_EQ(s.mix(l, r, 8, 60), 0);
    CHECK_EQ(peak(l, 8), 0);
    s.set(0xFF00, 0, 0);
    CHECK_EQ(s.mix(l, r, 8, 60), 1);
    CHECK(peak(l, 8) > 0);
}

TEST(port_rumble, timesOutAfterThreeSecondsWithoutUpdate) {
    RumbleSynth s;
    s.set(0xFF00, 0xFF00, 1000000000LL);
    CHECK(!s.checkTimeout(1000000000LL + 2900000000LL));
    CHECK(s.isActive());
    CHECK(s.checkTimeout(1000000000LL + 3100000000LL));
    CHECK(!s.isActive());
    CHECK(!s.checkTimeout(1000000000LL + 9000000000LL));
}

TEST(port_rumble, refreshResetsTimeoutAndZeroStopsImmediately) {
    RumbleSynth s;
    s.set(0xFF00, 0, 0);
    s.set(0xFF00, 0, 2500000000LL);
    CHECK(!s.checkTimeout(5000000000LL));
    s.set(0, 0, 5100000000LL);
    CHECK(!s.isActive());
}

// ---------------------------------------------------------------- ApexTriggerTest
namespace {
std::array<uint8_t, 10> payload(std::initializer_list<int> v) {
    std::array<uint8_t, 10> p{};
    size_t i = 0;
    for (int x : v) p[i++] = static_cast<uint8_t>(x);
    return p;
}
}  // namespace

TEST(port_trigger, triggerEnableBytesMatchAsb) {
    const pr::Frame f = pr::triggerEnableBoth();
    CHECK_EQ(f[2], 0x53);
    CHECK_EQ(f[3], 7);
    CHECK(f[4] == 0x01 && f[5] == 0x02 && f[6] == 0x03 && f[7] == 0x00 && f[8] == 0x40);
    CHECK_EQ(pr::checksum(f.data()), static_cast<int>(f[31]));
    const auto left = pr::triggerSideEnable(pr::ROUTE_LEFT);
    CHECK(left[0][4] == 0x01 && left[0][5] == 0x00 && left[0][6] == 0x03 && left[0][7] == 0x00 && left[0][8] == 0x40);
    CHECK(left[1][4] == 0x01 && left[1][5] == 0x01 && left[1][6] == 0x00);
    const auto right = pr::triggerSideEnable(pr::ROUTE_RIGHT);
    CHECK_EQ(right[0][5], 0x01);
    CHECK_EQ(right[1][5], 0x00);
    CHECK_THROWS(pr::triggerSideEnable(pr::ROUTE_BOTH));
    CHECK(pr::isSendable(f.data(), 32));
    CHECK(pr::isSendable(left[0].data(), 32) && pr::isSendable(left[1].data(), 32));
}

TEST(port_trigger, realtimeFrameWithTriggerSetsFlagRouteAndChannel) {
    const int l[8] = {0}, r[8] = {0}, t[8] = {1, -2, 3, -4, 5, -6, 127, -127};
    pr::Frame f = pr::realtimeFrame(l, r, t, true, pr::ROUTE_BOTH);
    CHECK_EQ(f[2], 0x57);
    CHECK_EQ(f[4], 0x80 | 0x10 | 0x08 | 0x04 | 2);
    CHECK_EQ(f[5], 128 + 1);
    CHECK_EQ(f[6], 128);
    CHECK_EQ(f[5 + 18], 128 + 127);
    CHECK_EQ(f[5 + 21], 128 - 127);
    f = pr::realtimeFrame(l, r, t, true, pr::ROUTE_LEFT);
    CHECK_EQ(f[4], 0x80 | 0x10 | 0x08 | 0x04 | 0);
    f = pr::realtimeFrame(l, r, t, true, pr::ROUTE_RIGHT);
    CHECK_EQ(f[4], 0x80 | 0x10 | 0x08 | 0x04 | 1);
    CHECK(pr::gripFrame(l, r) == pr::realtimeFrame(l, r, nullptr, false, pr::ROUTE_MUTE));
    CHECK_EQ(pr::neutralFrame()[4], 0x9B);
}

TEST(port_trigger, realtimeFrameRejectsBadArguments) {
    const int z[8] = {0}, big[8] = {0, 0, 0, 0, 0, 0, 0, 128};
    CHECK_THROWS(pr::realtimeFrame(z, z, big, true, pr::ROUTE_LEFT));
    CHECK_THROWS(pr::realtimeFrame(z, z, nullptr, true, pr::ROUTE_LEFT));
    CHECK_THROWS(pr::realtimeFrame(z, z, z, false, pr::ROUTE_LEFT));
    CHECK_THROWS(pr::realtimeFrame(z, z, z, true, 4));
}

TEST(port_trigger, decodeOffWeaponBowFeedbackVibrationAndUnsupported) {
    CHECK(TE::decode(0x00, payload({}).data()).effect.type == TE::Type::OFF);
    CHECK(TE::decode(0x05, payload({}).data()).effect.type == TE::Type::OFF);
    const auto w = TE::decode(0x25, payload({0x44, 0x00, 7}).data());
    CHECK_EQ(w.code, TE::OK);
    CHECK(w.effect.type == TE::Type::WEAPON);
    CHECK_EQ(w.effect.startZone, 2);
    CHECK_EQ(w.effect.endZone, 6);
    CHECK_EQ(w.effect.strength, 8);
    CHECK_EQ(TE::decode(0x25, payload({0x04, 0x00, 7}).data()).code, TE::INVALID);
    CHECK_EQ(TE::decode(0x21, payload({0x00, 0x04, 1}).data()).code, TE::INVALID);
    const auto b = TE::decode(0x22, payload({0x82, 0x00, 34}).data());
    CHECK(b.effect.type == TE::Type::BOW);
    CHECK_EQ(b.effect.strength, 3);
    CHECK_EQ(b.effect.snapStrength, 5);
    int64_t packed = 0;
    for (int z = 1; z < 10; z++) packed |= 3LL << (z * 3);
    const auto f = TE::decode(0x21, payload({0xFE, 0x03, static_cast<int>(packed & 0xFF), static_cast<int>((packed >> 8) & 0xFF),
                                             static_cast<int>((packed >> 16) & 0xFF), static_cast<int>((packed >> 24) & 0xFF)})
                                         .data());
    CHECK(f.effect.type == TE::Type::FEEDBACK);
    CHECK_EQ(f.effect.zoneStrengths[5], 4);
    CHECK_EQ(f.effect.zoneStrengths[0], 0);
    const auto v = TE::decode(0x26, payload({0xFE, 0x03, 0x49, 0x92, 0x24, 0x09, 0, 0, 130}).data());
    CHECK(v.effect.type == TE::Type::VIBRATION);
    CHECK_EQ(v.effect.frequency, 130);
    const auto p = TE::decode(0x26, payload({0xFE, 0x03, 0, 0, 0, 0, 0, 0, 20}).data());
    CHECK(p.effect.type == TE::Type::RHYTHM);
    CHECK_EQ(p.effect.rhythmPeriod, 50);
    CHECK(TE::decode(0x26, payload({0xFE, 0x03, 0, 0, 0, 0, 0, 0, 0}).data()).effect.type == TE::Type::OFF);
    CHECK_EQ(TE::decode(0x23, payload({1, 2, 3}).data()).code, TE::UNSUPPORTED);
    CHECK_EQ(TE::decode(0x01, payload({1, 2, 3}).data()).code, TE::UNSUPPORTED);
    CHECK_EQ(TE::decode(0x02, payload({1, 2, 3}).data()).code, TE::UNSUPPORTED);
}

TEST(port_trigger, weaponBurstFiresOncePerPullAndRearms) {
    const TE e = TE::decode(0x25, payload({0x44, 0x00, 7}).data()).effect;
    TE::State s;
    int out[8];
    int burstPeak = 0;
    for (int pos : {0, 20, 60, 100, 140, 160, 200, 255, 255, 255}) {
        TE::render(e, pos, s, -1, out);
        burstPeak = (std::max)(burstPeak, peak(out, 8));
        CHECK(peak(out, 8) <= 127);
    }
    CHECK_EQ(s.burstsStarted, 1);
    CHECK(burstPeak >= 70);
    for (int pos : {200, 100, 40, 0, 60, 160, 200}) TE::render(e, pos, s, -1, out);
    CHECK_EQ(s.burstsStarted, 2);
}

TEST(port_trigger, burstLengthIsAttackHoldReleaseThen0) {
    const TE e = TE::decode(0x25, payload({0x44, 0x00, 7}).data()).effect;
    TE::State s;
    int out[8];
    TE::render(e, 0, s, -1, out);
    TE::render(e, 200, s, -1, out);
    int nonZeroFrames = 0;
    for (int i = 0; i < 12; i++) {
        if (peak(out, 8) > 0) nonZeroFrames++;
        TE::render(e, 200, s, -1, out);
    }
    CHECK(nonZeroFrames >= 6 && nonZeroFrames <= 8);
    for (int i = 0; i < 4; i++) TE::render(e, 200, s, -1, out);
    CHECK_EQ(peak(out, 8), 0);
}

TEST(port_trigger, feedbackAmplitudeFollowsStrengthAndPosition) {
    int64_t packed = 0;
    for (int z = 0; z < 10; z++) packed |= 3LL << (z * 3);
    const TE e = TE::decode(0x21, payload({0xFF, 0x03, static_cast<int>(packed & 0xFF), static_cast<int>((packed >> 8) & 0xFF),
                                           static_cast<int>((packed >> 16) & 0xFF), static_cast<int>((packed >> 24) & 0xFF)})
                                       .data())
                     .effect;
    TE::State s;
    int out[8];
    int pk = 0;
    for (int i = 0; i < 40; i++) {
        TE::render(e, 255, s, i * 8LL, out);
        if (i > 5) pk = (std::max)(pk, peak(out, 8));
    }
    CHECK(pk >= 28 && pk <= 33);
    for (int i = 0; i < 6; i++) TE::render(e, 0, s, (40 + i) * 8LL, out);
    CHECK_EQ(peak(out, 8), 0);
}

TEST(port_trigger, vibrationNeverExceedsNativeDriveAndPulseTrainRuns) {
    const TE e = TE::decode(0x26, payload({0xFE, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 130}).data()).effect;
    TE::State s;
    int out[8];
    int pk = 0;
    for (int i = 0; i < 60; i++) {
        TE::render(e, 255, s, i * 8LL, out);
        pk = (std::max)(pk, peak(out, 8));
    }
    CHECK(pk <= TE::NATIVE_MAX_DRIVE && pk > 40);
    const TE train = TE::decode(0x26, payload({0xFE, 0x03, 0x49, 0x92, 0x24, 0x09, 0, 0, 10}).data()).effect;
    TE::State ts;
    int tp = 0;
    for (int i = 0; i < 40; i++) {
        TE::render(train, 255, ts, -1, out);
        tp = (std::max)(tp, peak(out, 8));
    }
    CHECK(tp > 20 && tp <= 127);
}

TEST(port_trigger, strengthAndCeilingFollowAsbFormulas) {
    CHECK_EQ(TE::scaleStrength(100, 50), 50);
    CHECK_EQ(TE::scaleStrength(-100, 50), -50);
    CHECK_EQ(TE::scaleStrength(1, 50), 0);
    CHECK_EQ(TE::scaleStrength(127, 100), 127);
    CHECK_EQ(TE::scaleStrength(127, 500), 127);
    CHECK_EQ(TE::limitCeiling(100, 100), 100);
    CHECK_EQ(TE::limitCeiling(127, 50), 56);
    CHECK_EQ(TE::limitCeiling(-127, 50), -56);
    CHECK_EQ(TE::limitCeiling(20, 50), 20);
}

TEST(port_trigger, weaponThroughBridgeRoutesLeftAndStrengthScales) {
    TriggerBridge b;
    const auto log = b.onEffect(true, 0x25, payload({0x44, 0x00, 7}).data(), 0);
    CHECK(log.has_value());
    CHECK(log->find("Weapon") != std::string::npos);
    TriggerBridge::Out o;
    int64_t t = 0;
    int pk = 0;
    bool sawLeft = false;
    for (int pos : {0, 100, 200, 255, 255, 255, 255, 255, 255, 255}) {
        b.setPosition(pos, 0);
        t += 8 * MS;
        b.render(t, o);
        if (o.enabled) {
            CHECK_EQ(o.route, pr::ROUTE_LEFT);
            sawLeft = true;
            pk = (std::max)(pk, peak(o.samples, 8));
        }
    }
    CHECK(sawLeft);
    CHECK(pk >= 70 && pk <= 127);
    TriggerBridge half;
    half.setStrengthPercent(50);
    half.onEffect(true, 0x25, payload({0x44, 0x00, 7}).data(), 0);
    t = 0;
    int pk2 = 0;
    for (int pos : {0, 100, 200, 255, 255, 255, 255, 255}) {
        half.setPosition(pos, 0);
        t += 8 * MS;
        half.render(t, o);
        if (o.enabled) pk2 = (std::max)(pk2, peak(o.samples, 8));
    }
    CHECK(pk2 > 0 && pk2 <= pk / 2 + 1);
}

TEST(port_trigger, rightOnlyEffectRoutesRightAndBothIdenticalRoutesBoth) {
    TriggerBridge b;
    b.onEffect(false, 0x25, payload({0x44, 0x00, 7}).data(), 0);
    TriggerBridge::Out o;
    int64_t t = 0;
    bool right = false;
    for (int pos : {0, 100, 200, 255, 255, 255, 255}) {
        b.setPosition(0, pos);
        t += 8 * MS;
        b.render(t, o);
        if (o.enabled) {
            CHECK_EQ(o.route, pr::ROUTE_RIGHT);
            right = true;
        }
    }
    CHECK(right);
    TriggerBridge both;
    both.onEffect(true, 0x25, payload({0x44, 0x00, 7}).data(), 0);
    both.onEffect(false, 0x25, payload({0x44, 0x00, 7}).data(), 0);
    bool sawBoth = false;
    t = 0;
    for (int pos : {0, 100, 200, 255, 255, 255, 255}) {
        both.setPosition(pos, pos);
        t += 8 * MS;
        both.render(t, o);
        if (o.enabled) {
            CHECK_EQ(o.route, pr::ROUTE_BOTH);
            sawBoth = true;
        }
    }
    CHECK(sawBoth);
}

TEST(port_trigger, offIsClearedAfter100msAndDuplicatesAreDeduplicated) {
    TriggerBridge b;
    CHECK(b.onEffect(true, 0x25, payload({0x44, 0x00, 7}).data(), 0).has_value());
    CHECK(!b.onEffect(true, 0x25, payload({0x44, 0x00, 7}).data(), 10 * MS).has_value());
    CHECK(b.anyEffect());
    const auto off = b.onEffect(true, 0x05, payload({}).data(), 20 * MS);
    CHECK(off.has_value());
    CHECK(off->find("해제 예약") != std::string::npos);
    TriggerBridge::Out o;
    b.render(60 * MS, o);
    CHECK(b.anyEffect());
    b.render(130 * MS, o);
    CHECK(!b.anyEffect());
    const auto st = b.stats();
    CHECK_EQ(st[0], 3);
    CHECK_EQ(st[6], 1);
}

TEST(port_trigger, effectIsNotReleasedWithoutRefresh_asbParity) {
    TriggerBridge b;
    b.onEffect(true, 0x26, payload({0xFE, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 130}).data(), 0);
    TriggerBridge::Out o;
    b.setPosition(255, 0);
    b.render(3100 * MS, o);
    CHECK(b.anyEffect());
    b.render(60000 * MS, o);
    CHECK(b.anyEffect());
    CHECK(o.enabled);
    CHECK_EQ(b.stats()[7], 0);
    b.clear();
    CHECK(!b.anyEffect());
}

TEST(port_trigger, unsupportedEffectBecomesOffAndIsCountedNotSent) {
    TriggerBridge b;
    b.onEffect(true, 0x23, payload({1, 2, 3}).data(), 0);
    CHECK(!b.anyEffect());
    CHECK_EQ(b.stats()[4], 1);
    TriggerBridge::Out o;
    b.setPosition(255, 255);
    b.render(10 * MS, o);
    CHECK(!o.enabled);
    CHECK_EQ(peak(o.samples, 8), 0);
}

TEST(port_trigger, clearResetsEverything) {
    TriggerBridge b;
    b.onEffect(true, 0x26, payload({0xFE, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 130}).data(), 0);
    CHECK(b.anyEffect());
    b.clear();
    CHECK(!b.anyEffect());
    TriggerBridge::Out o;
    b.setPosition(255, 255);
    b.render(8 * MS, o);
    CHECK(!o.enabled);
}

TEST(port_trigger, allOutputsStayWithinHardCap) {
    TriggerBridge b;
    b.onEffect(true, 0x25, payload({0x44, 0x00, 7}).data(), 0);
    b.onEffect(false, 0x26, payload({0xFE, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 200}).data(), 0);
    TriggerBridge::Out o;
    int64_t t = 0;
    const int z[8] = {0};
    for (int i = 0; i < 400; i++) {
        const int pos = (i * 7) % 256;
        b.setPosition(pos, 255 - pos);
        t += 8 * MS;
        b.render(t, o);
        b.onEffect(true, 0x25, payload({0x44, 0x00, 7}).data(), t);
        b.onEffect(false, 0x26, payload({0xFE, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 200}).data(), t);
        for (int v : o.samples) CHECK(std::abs(v) <= 127);
        if (o.enabled) (void)pr::realtimeFrame(z, z, o.samples, true, o.route);
    }
}
