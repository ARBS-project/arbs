// SPDX-License-Identifier: GPL-3.0-or-later
// D4 ported unit tests: ARBS-A ApexProtocolTest, ApexSchedulerTest, ApexProbePolicyTest, ApexJitterBufferTest,
// ApexArrivalStatsTest, ArbsSessionStatsTest (app/src/test/java/com/limelight/arbs, f3051641). Same assertions.
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include "arbs/arrival_stats.h"
#include "arbs/java_compat.h"
#include "arbs/jitter_buffer.h"
#include "arbs/pcm_pipeline.h"
#include "arbs/probe_policy.h"
#include "arbs/protocol.h"
#include "arbs/scheduler.h"
#include "arbs/session_stats.h"
#include "mini_test.h"

using namespace arbs;
namespace pr = arbs::protocol;

namespace {
constexpr int64_t MS = 1000000LL;
constexpr int64_t S = 1000000000LL;

void assertFrame(const pr::Frame& f, int cmd, std::initializer_list<int> payload) {
    CHECK_EQ(static_cast<int>(f.size()), 32);
    CHECK_EQ(f[0], 0x5A);
    CHECK_EQ(f[1], 0xA5);
    CHECK_EQ(static_cast<int>(f[2]), cmd);
    CHECK_EQ(static_cast<int>(f[3]), static_cast<int>(payload.size()) + 2);
    int i = 0;
    for (int v : payload) CHECK_EQ(static_cast<int>(f[static_cast<size_t>(4 + i++)]), v);
    CHECK_EQ(pr::checksum(f.data()), static_cast<int>(f[31]));
}

bool contains(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }
bool startsWith(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }
bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
}  // namespace

// ---------------------------------------------------------------- ApexProtocolTest
TEST(port_protocol, gripEnableMatchesAsb) {
    const pr::Frame f = pr::gripRealtimeEnable();
    assertFrame(f, 0x53, {0x01, 0x12, 0x02, 0x40, 0x00});
    CHECK_EQ(f[31], 0xAF);
}

TEST(port_protocol, disablePairMatchesAsb) {
    const auto d = pr::disablePair();
    CHECK_EQ(d.size(), static_cast<size_t>(2));
    assertFrame(d[0], 0x53, {0x01, 0x02, 0x00});
    assertFrame(d[1], 0x53, {0x01, 0x12, 0x00});
}

TEST(port_protocol, getInfoFrame) {
    const pr::Frame f = pr::buildGetInfo();
    assertFrame(f, 0x01, {});
    CHECK_EQ(f[31], 0x03);
}

TEST(port_protocol, neutralFrameIsGripOnly0x9B) {
    const pr::Frame f = pr::neutralFrame();
    CHECK_EQ(f[4], 0x9B);
    for (int i = 0; i < 8; i++) {
        CHECK_EQ(f[static_cast<size_t>(5 + i * 3)], 0x80);
        CHECK_EQ(f[static_cast<size_t>(6 + i * 3)], 0x80);
        CHECK_EQ(f[static_cast<size_t>(7 + i * 3)], 0x80);
    }
    CHECK_EQ(f[3], 27);
    CHECK_EQ(pr::checksum(f.data()), static_cast<int>(f[31]));
}

TEST(port_protocol, gripFrameEncodesLeftRightAndKeepsTriggerOff) {
    const int l[8] = {1, -1, 60, -60, 0, 5, -5, 30};
    const int r[8] = {-2, 2, -60, 60, 0, -5, 5, -30};
    const pr::Frame f = pr::gripFrame(l, r);
    CHECK_EQ(f[4], 0x9B);
    for (int i = 0; i < 8; i++) {
        CHECK_EQ(f[static_cast<size_t>(5 + i * 3)], 0x80);
        CHECK_EQ(static_cast<int>(f[static_cast<size_t>(6 + i * 3)]), l[i] + 128);
        CHECK_EQ(static_cast<int>(f[static_cast<size_t>(7 + i * 3)]), r[i] + 128);
    }
}

TEST(port_protocol, amplitudeAbove127IsRejected) {
    CHECK_EQ(pr::MAX_AMPLITUDE, 127);
    int ok[8] = {0};
    int bad[8] = {0};
    bad[3] = 128;
    CHECK_THROWS(pr::gripFrame(bad, ok));
    bad[3] = -128;
    CHECK_THROWS(pr::gripFrame(ok, bad));
    bad[3] = 127;
    pr::Frame f = pr::gripFrame(bad, bad);
    CHECK_EQ(f[6 + 3 * 3], 255);
    bad[3] = -127;
    f = pr::gripFrame(bad, bad);
    CHECK_EQ(f[6 + 3 * 3], 1);
}

TEST(port_protocol, everySendableFrameIsOnly0x53Or0x57) {
    CHECK(pr::isSendable(pr::gripRealtimeEnable().data(), 32));
    CHECK(pr::isSendable(pr::neutralFrame().data(), 32));
    for (const auto& d : pr::disablePair()) CHECK(pr::isSendable(d.data(), 32));
    CHECK(!pr::isSendable(pr::buildGetInfo().data(), 32));
    const pr::Frame en = pr::gripRealtimeEnable();
    CHECK(en[4] == 0x01 && en[5] == 0x12 && en[6] == 0x02);
    for (const auto& d : pr::disablePair()) CHECK(d[6] == 0x00);
    uint8_t fake[31] = {0};
    CHECK(!pr::isSendable(fake, 31));
    uint8_t unknown[32] = {0};
    unknown[2] = 0x11;
    CHECK(!pr::isSendable(unknown, 32));
    unknown[2] = 0x12;
    CHECK(!pr::isSendable(unknown, 32));
}

TEST(port_protocol, parseGetInfoResponse) {
    uint8_t f[32] = {0};
    f[0] = 0x5A;
    f[1] = 0xA5;
    f[2] = 0x01;
    f[3] = 0x1E;
    f[5] = 0x96;
    f[6] = 0x02;
    f[11] = 0x08;
    f[29] = 0x90;
    f[31] = static_cast<uint8_t>(pr::checksum(f));
    auto info = pr::parse(f, 32);
    CHECK(info.has_value());
    CHECK(info->checksumOk);
    CHECK(info->isPro());
    CHECK(info->hasGripHaptics());
    uint8_t with[33] = {0};
    std::memcpy(with + 1, f, 32);
    CHECK(pr::parse(with, 33).has_value());
    f[31] ^= 0x55;
    CHECK(!pr::parse(f, 32)->checksumOk);
    f[31] = 0;
    f[5] = 0x95;
    f[29] = 0x10;
    f[31] = static_cast<uint8_t>(pr::checksum(f));
    auto np = pr::parse(f, 32);
    CHECK(!np->isPro());
    CHECK(!np->hasGripHaptics());
    uint8_t other[32];
    std::memcpy(other, f, 32);
    other[2] = 0x53;
    CHECK(!pr::parse(other, 32).has_value());
    const uint8_t tiny[3] = {1, 2, 3};
    CHECK(!pr::parse(tiny, 3).has_value());
}

// ---------------------------------------------------------------- ApexSchedulerTest
TEST(port_scheduler, onTimeWaitsUntilSlotThenIsDue) {
    Scheduler s;
    Scheduler::Decision d;
    s.start(0);
    s.poll(0, d);
    CHECK_EQ(d.waitNs, 0);
    s.consume();
    s.poll(3 * MS, d);
    CHECK_EQ(d.waitNs, 5 * MS);
    s.poll(8 * MS, d);
    CHECK_EQ(d.waitNs, 0);
    CHECK(!d.catchingUp);
}

TEST(port_scheduler, averageRateIsMaintainedDespiteSlowTransfers) {
    Scheduler s;
    Scheduler::Decision d;
    int64_t now = 0;
    s.start(now);
    int sent = 0, i = 0;
    while (now < 1000 * MS) {
        s.poll(now, d);
        if (d.waitNs > 0) {
            now += d.waitNs;
            continue;
        }
        s.consume();
        sent++;
        i++;
        now += (i % 5 == 0 ? 12 : 1) * MS;
    }
    CHECK_MSG(sent >= 123 && sent <= 126, "sent=" + std::to_string(sent));
    CHECK_EQ(s.getAndResetMissed(), 0);
}

TEST(port_scheduler, lateByOneToThreeSlotsCatchesUpWithoutSkipping) {
    Scheduler s;
    Scheduler::Decision d;
    s.start(0);
    s.consume();
    s.poll(8 * MS + 20 * MS, d);
    CHECK_EQ(d.waitNs, 0);
    CHECK(d.catchingUp);
    CHECK_EQ(d.skipped, 0);
    s.consume();
    s.poll(28 * MS, d);
    CHECK(d.catchingUp);
    s.consume();
    s.poll(28 * MS, d);
    CHECK_EQ(d.waitNs, 0);
    CHECK(!d.catchingUp);
    CHECK_EQ(s.getAndResetResyncs(), 0);
}

TEST(port_scheduler, lateByMoreThanThreeSlotsSkipsAndResyncs) {
    Scheduler s;
    Scheduler::Decision d;
    s.start(0);
    s.poll(8 * MS * 1 + 50 * MS, d);
    CHECK_EQ(d.waitNs, 0);
    CHECK_EQ(d.skipped, 7);
    CHECK_EQ(s.getAndResetMissed(), 7);
    CHECK_EQ(s.getAndResetResyncs(), 1);
    CHECK_EQ(s.target(), 58 * MS);
    CHECK_EQ(s.getAndResetMissed(), 0);
}

TEST(port_scheduler, exactlyThreeSlotsLateStillCatchesUp) {
    Scheduler s;
    Scheduler::Decision d;
    s.start(0);
    s.poll(24 * MS, d);
    CHECK_EQ(d.skipped, 0);
    CHECK(d.catchingUp);
    s.poll(32 * MS + 1, d);
    CHECK_EQ(d.skipped, 4);
}

// ---------------------------------------------------------------- ApexProbePolicyTest
TEST(port_probe, tenFailuresNeededBeforeUnsupported) {
    ProbePolicy p;
    p.begin();
    for (int i = 1; i <= 9; i++) {
        CHECK_EQ(p.nextAttempt(), i);
        CHECK(p.onAttemptFailed());
        CHECK(!p.isUnsupported());
    }
    CHECK_EQ(p.nextAttempt(), 10);
    CHECK(!p.onAttemptFailed());
    CHECK(p.isUnsupported());
    CHECK(!p.isReady());
}

TEST(port_probe, successOnLaterAttemptIsReady) {
    ProbePolicy p;
    p.begin();
    p.nextAttempt();
    CHECK(p.onAttemptFailed());
    p.nextAttempt();
    CHECK(p.onAttemptFailed());
    p.nextAttempt();
    p.onReady();
    CHECK(p.isReady());
    CHECK(!p.isUnsupported());
}

TEST(port_probe, definitiveUnsupportedDoesNotRetry) {
    ProbePolicy p;
    p.begin();
    p.nextAttempt();
    p.onDefinitiveUnsupported();
    CHECK(p.isUnsupported());
}

TEST(port_probe, manualRetryOnlyFromUnsupportedAndRestartsAtOne) {
    ProbePolicy p;
    p.begin();
    CHECK(!p.manualRetry());
    for (int i = 0; i < 10; i++) {
        p.nextAttempt();
        p.onAttemptFailed();
    }
    CHECK(p.isUnsupported());
    CHECK(p.manualRetry());
    CHECK(!p.isUnsupported());
    CHECK_EQ(p.attempt(), 0);
    CHECK_EQ(p.nextAttempt(), 1);
}

TEST(port_probe, inputGapRestartsOnlyWhenUnsupported) {
    ProbePolicy p;
    p.resetAll();
    CHECK(!p.inputSeen());
    CHECK(!p.onInput(1000));
    CHECK(p.inputSeen());
    CHECK(!p.onInput(9000));
    p.begin();
    for (int i = 0; i < 10; i++) {
        p.nextAttempt();
        p.onAttemptFailed();
    }
    CHECK(p.isUnsupported());
    CHECK(!p.onInput(9500));
    CHECK(!p.onInput(10000));
    CHECK(p.onInput(12100));
    CHECK(!p.isUnsupported());
    CHECK_EQ(p.attempt(), 0);
}

TEST(port_probe, resetAllForgetsInput) {
    ProbePolicy p;
    p.onInput(100);
    CHECK(p.inputSeen());
    p.resetAll();
    CHECK(!p.inputSeen());
}

// ---------------------------------------------------------------- ApexJitterBufferTest
namespace {
JitterBuffer::Result d(JitterBuffer& b, int q, int64_t nowMs) {
    JitterBuffer::Result r;
    b.decide(q, nowMs * MS, r);
    return r;
}
}  // namespace

TEST(port_jitter, waitsUntilTargetFilledThenPlays) {
    JitterBuffer b;
    b.reset(0);
    const int t = b.getTarget();
    CHECK(!d(b, 5, 0).play);
    CHECK(!d(b, t - 1, 8).play);
    CHECK(!b.isPlaying());
    CHECK(d(b, t, 16).play);
    CHECK(b.isPlaying());
}

TEST(port_jitter, underrunStopsPlaybackRaisesTargetAndRefills) {
    JitterBuffer b;
    b.reset(0);
    const int t0 = b.getTarget();
    CHECK(d(b, t0, 0).play);
    const JitterBuffer::Result r = d(b, 7, 8);
    CHECK(!r.play);
    CHECK(r.underrun);
    CHECK_EQ(b.getTarget(), t0 + JitterBuffer::STEP_UP_MS);
    CHECK(!d(b, t0, 16).play);
    CHECK(d(b, t0 + JitterBuffer::STEP_UP_MS, 24).play);
    CHECK_EQ(b.getAndResetUnderruns(), 1);
    CHECK_EQ(b.getAndResetUnderruns(), 0);
}

TEST(port_jitter, targetIsCappedAtMax) {
    JitterBuffer b;
    b.reset(0);
    int64_t t = 0;
    for (int i = 0; i < 60; i++) {
        d(b, b.getTarget(), t);
        d(b, 0, t + 1);
        t += 2;
    }
    CHECK_EQ(b.getTarget(), JitterBuffer::MAX_TARGET_MS);
}

TEST(port_jitter, overflowTrimsOldestAboveTargetPlus8) {
    JitterBuffer b;
    b.reset(0);
    const int target = b.getTarget();
    JitterBuffer::Result r = d(b, target + JitterBuffer::OVERFLOW_MS, 0);
    CHECK_EQ(r.trimTo, -1);
    r = d(b, target + JitterBuffer::OVERFLOW_MS + 1, 8);
    CHECK_EQ(r.trimTo, target + JitterBuffer::KEEP_MARGIN_MS);
    CHECK(r.play);
    CHECK_EQ(b.getAndResetTrims(), 1);
    CHECK_EQ(JitterBuffer::OVERFLOW_MS, 8);
}

TEST(port_jitter, autoTargetRisesImmediatelyAndLowersSlowlyWithinBounds) {
    JitterBuffer b;
    b.reset(0);
    b.adaptFromP95(60.0, 200, 10000 * MS);
    CHECK_EQ(b.getTarget(), 64);
    b.adaptFromP95(500.0, 200, 11000 * MS);
    CHECK_EQ(b.getTarget(), JitterBuffer::MAX_TARGET_MS);
    b.adaptFromP95(5.0, 200, 20000 * MS);
    CHECK_EQ(b.getTarget(), 78);
    for (int i = 0; i < 100; i++) b.adaptFromP95(5.0, 200, (21000LL + i * 1000) * MS);
    CHECK_EQ(b.getTarget(), JitterBuffer::MIN_TARGET_MS);
    CHECK_EQ(JitterBuffer::MIN_TARGET_MS, 12);
}

TEST(port_jitter, p95FormulaUsesCeilPlus4) {
    JitterBuffer b;
    b.reset(0);
    b.adaptFromP95(30.0, 200, 10000 * MS);
    CHECK_EQ(b.getTarget(), 34);
    b.adaptFromP95(34.2, 200, 11000 * MS);
    CHECK_EQ(b.getTarget(), 39);
}

TEST(port_jitter, tooFewPacketsDoesNotAdaptAndRecentUnderrunBlocksLowering) {
    JitterBuffer b;
    b.reset(0);
    const int t = b.getTarget();
    b.adaptFromP95(80.0, 5, 1000 * MS);
    CHECK_EQ(b.getTarget(), t);
    d(b, t, 1000);
    d(b, 0, 1008);
    const int after = b.getTarget();
    b.adaptFromP95(5.0, 200, 2000 * MS);
    CHECK_EQ(b.getTarget(), after);
    b.adaptFromP95(5.0, 200, 3100 * MS);
    CHECK_EQ(b.getTarget(), after - JitterBuffer::DOWN_STEP_MS);
}

TEST(port_jitter, resetRestartsPrebufferButKeepsTarget) {
    JitterBuffer b;
    b.reset(0);
    d(b, b.getTarget(), 0);
    d(b, 0, 8);
    const int t = b.getTarget();
    b.reset(100 * MS);
    CHECK(!b.isPlaying());
    CHECK_EQ(b.getTarget(), t);
}

TEST(port_jitter, queueCapacityCoversWorstCase) { CHECK(JitterBuffer::REQUIRED_QUEUE <= PcmPipeline::CAPACITY); }

TEST(port_jitter, burstyArrivalsPlaySmoothlyWithAdaptedTarget) {
    JitterBuffer b;
    b.reset(0);
    int queue = 0, plays = 0, underruns = 0;
    for (int slot = 0; slot < 500; slot++) {
        const int64_t now = slot * 8LL;
        if (slot % 5 == 0) queue += 40;
        if (slot % 125 == 124) b.adaptFromP95(40.0, 200, now * MS);
        const JitterBuffer::Result r = d(b, queue, now);
        if (r.trimTo >= 0) queue = r.trimTo;
        if (r.play) {
            queue -= 8;
            if (slot > 250) plays++;
        }
        if (r.underrun && slot > 250) underruns++;
    }
    CHECK_MSG(plays >= 235, "plays=" + std::to_string(plays));
    CHECK_EQ(underruns, 0);
}

TEST(port_jitter, fixedDelayNeverAdaptsButStillRefills) {
    JitterBuffer b;
    b.setFixed(48);
    CHECK_EQ(b.getTarget(), 48);
    b.reset(0);
    CHECK(!d(b, 47, 0).play);
    CHECK(d(b, 48, 8).play);
    const JitterBuffer::Result r = d(b, 0, 16);
    CHECK(r.underrun);
    CHECK_EQ(b.getTarget(), 48);
    b.adaptFromP95(5.0, 200, 30000 * MS);
    b.adaptFromP95(90.0, 200, 31000 * MS);
    CHECK_EQ(b.getTarget(), 48);
    b.setFixed(5);
    CHECK_EQ(b.getTarget(), 12);
    b.setFixed(500);
    CHECK_EQ(b.getTarget(), 80);
    b.setFixed(0);
    CHECK_EQ(b.getFixed(), 0);
}

// ---------------------------------------------------------------- ApexArrivalStatsTest
TEST(port_arrival, steadyFiveMsArrivals) {
    ArrivalStats s;
    int64_t t = 1000 * MS;
    for (int i = 0; i < 200; i++) {
        s.add(t);
        t += 5 * MS;
    }
    ArrivalStats::Snapshot r;
    s.snapshotAndReset(t - 5 * MS, r);
    CHECK_EQ(r.packets, 200);
    CHECK_NEAR(r.avgMs, 5.0, 1e-6);
    CHECK_NEAR(r.p95Ms, 5.0, 1e-6);
    CHECK_NEAR(r.maxMs, 5.0, 1e-6);
    CHECK_NEAR(r.maxSilenceMs, 5.0, 1e-6);
}

TEST(port_arrival, burstAndStallShowInP95AndMax) {
    ArrivalStats s;
    int64_t t = 1000 * MS;
    for (int i = 0; i < 100; i++) {
        s.add(t);
        t += (i == 50 ? 120 : 2) * MS;
    }
    ArrivalStats::Snapshot r;
    s.snapshotAndReset(t, r);
    CHECK_NEAR(r.maxMs, 120.0, 1e-6);
    CHECK_NEAR(r.p95Ms, 2.0, 1e-6);
    CHECK_NEAR(r.maxSilenceMs, 120.0, 1e-6);
    CHECK(r.avgMs > 2.0 && r.avgMs < 4.0);
}

TEST(port_arrival, tailSinceLastPacketCountsAsSilenceAndWindowResets) {
    ArrivalStats s;
    s.add(1000 * MS);
    s.add(1005 * MS);
    ArrivalStats::Snapshot r;
    s.snapshotAndReset(1505 * MS, r);
    CHECK_NEAR(r.maxMs, 5.0, 1e-6);
    CHECK_NEAR(r.maxSilenceMs, 500.0, 1e-6);
    s.snapshotAndReset(2005 * MS, r);
    CHECK_EQ(r.packets, 0);
    CHECK_NEAR(r.maxSilenceMs, 1000.0, 1e-6);
    s.reset();
    s.snapshotAndReset(2000 * MS, r);
    CHECK_NEAR(r.maxSilenceMs, 0.0, 1e-6);
}

// ---------------------------------------------------------------- ArbsSessionStatsTest
TEST(port_session_stats, minuteLineShowsOnlyTheLastMinuteAndSessionLineShowsTotals) {
    SessionStats st;
    st.reset(0);
    st.batteryStart(80);
    st.add(SessionStats::PCM_PKTS, 1000);
    st.add(SessionStats::GRIP_FRAMES, 7500);
    st.add(SessionStats::TRIG_FRAMES, 300);
    st.add(SessionStats::RUMBLE_TIMEOUTS, 1);
    st.addExtraDelay(10);
    st.addExtraDelay(12);
    st.addRtt(20);
    st.silenceMs(150);
    CHECK(!st.minuteDue(59 * S));
    CHECK(st.minuteDue(60 * S));
    const std::string m1 = st.minuteLine(60 * S, "연결됨", "READY/출력중");
    CHECK_MSG(startsWith(m1, "ARBS 1분 요약: 경과 00:01:00"), m1);
    CHECK_MSG(contains(m1, "PCM 패킷 1000"), m1);
    CHECK_MSG(contains(m1, "그립 7500 트리거 300"), m1);
    CHECK_MSG(contains(m1, "평균 11.0ms"), m1);
    CHECK_MSG(contains(m1, "RTT 평균 20ms"), m1);
    st.add(SessionStats::PCM_PKTS, 500);
    const std::string m2 = st.minuteLine(120 * S, "연결됨", "READY/출력중");
    CHECK_MSG(contains(m2, "PCM 패킷 500"), m2);
    CHECK_MSG(contains(m2, "추가지연 평균 n/a"), m2);
    st.batteryEnd(78);
    const std::string se = st.sessionLine(125 * S, "세션 종료(stopConnection)");
    CHECK_MSG(startsWith(se, "ARBS 세션 요약: 총 시간 00:02:05"), se);
    CHECK_MSG(contains(se, "PCM 패킷 1500"), se);
    CHECK_MSG(contains(se, "최대무수신 150ms"), se);
    CHECK_MSG(contains(se, "시작 80 종료 78"), se);
    CHECK_MSG(endsWith(se, "크래시 0"), se);
    st.markCrash();
    CHECK(endsWith(st.sessionLine(126 * S, "크래시"), "크래시 1"));
}

TEST(port_session_stats, retriesAndReconnectsSurviveSessionReset) {
    SessionStats st;
    st.add(SessionStats::RETRIES, 2);
    st.add(SessionStats::RECONNECTS, 1);
    st.add(SessionStats::PCM_PKTS, 99);
    st.reset(S);
    CHECK_EQ(st.get(SessionStats::RETRIES), 2);
    CHECK_EQ(st.get(SessionStats::RECONNECTS), 1);
    CHECK_EQ(st.get(SessionStats::PCM_PKTS), 0);
    CHECK(contains(st.sessionLine(2 * S, "x"), "재시도 2 재연결 1"));
}

TEST(port_session_stats, unknownBatteryIsShownAsNa) {
    SessionStats st;
    st.reset(0);
    CHECK(contains(st.sessionLine(S, "x"), "시작 n/a 종료 n/a"));
}

TEST(port_session_stats, perSecondLinesAreSuppressedAbove20MbUnlessVerbose) {
    CHECK(SessionStats::perSecondAllowed(0, false));
    CHECK(SessionStats::perSecondAllowed(20LL * 1024 * 1024, false));
    CHECK(!SessionStats::perSecondAllowed(20LL * 1024 * 1024 + 1, false));
    CHECK(SessionStats::perSecondAllowed(30LL * 1024 * 1024, true));
}

// ---------------------------------------------------------------- java_compat (C++ only)
TEST(port_java_compat, javaRoundMatchesJavaMathRound) {
    using arbs::jc::javaRound;
    CHECK_EQ(javaRound(2.5), 3);
    CHECK_EQ(javaRound(-2.5), -2);   // std::round gives -3
    CHECK_EQ(javaRound(-0.5), 0);
    CHECK_EQ(javaRound(0.49999999999999994), 0);
    CHECK_EQ(javaRound(-1.5), -1);
    CHECK_EQ(javaRound(1e300), (std::numeric_limits<int64_t>::max)());
    CHECK_EQ(javaRound(std::nan("")), 0);
    CHECK_EQ(arbs::jc::lroundJ(-2.5), -3);  // ApexTriggerEffect.lround: away from zero
    CHECK_EQ(arbs::jc::lroundJ(2.5), 3);
    CHECK_EQ(arbs::jc::d2i(1e20), (std::numeric_limits<int32_t>::max)());
    CHECK_EQ(arbs::jc::l2i(0x100000005LL), 5);
}
