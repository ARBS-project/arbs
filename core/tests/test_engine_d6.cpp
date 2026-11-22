// SPDX-License-Identifier: GPL-3.0-or-later
// D6 engine scenarios (B2_DESIGN.md 3-5, 13 scripts) with FakeClock/FakePort in Inline mode (deterministic, virtual
// time, no device). Expected bytes/times are derived from ARBS-A ApexHaptics.java (f3051641); the Java line of each
// behaviour is named in the scenario comment. Plus one real-thread smoke test (std::thread + steady clock, ~0.3 s).
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "arbs/engine.h"
#include "arbs/java_compat.h"
#include "arbs/protocol.h"
#include "fakes.h"
#include "mini_test.h"

using namespace arbs;
using fake::MS;
namespace pr = arbs::protocol;

namespace {

struct Rig {
    fake::FakeClock clock;
    fake::CaptureLog log{&clock};
    Engine eng{clock, log};
    fake::FakePort* port = nullptr;
    const int64_t t0;

    Rig() : t0(clock.nowNs()) {}
    int64_t at(int64_t relMs) const { return t0 + relMs * MS; }
    fake::FakePort& attach() {
        auto p = std::make_unique<fake::FakePort>(clock);
        port = p.get();
        eng.attach(std::move(p), Engine::StartMode::Inline);
        return *port;
    }
    void event(int64_t relMs, std::function<void()> fn) { clock.at(at(relMs), std::move(fn)); }
    void run(int64_t untilRelMs) {
        clock.setLimit(at(untilRelMs), [this] { eng.requestTerminate("test end"); });
        eng.runLoop();
    }
    int64_t rel(int64_t t) const { return (t - t0) / MS; }
    std::vector<fake::Written> writes(int cmd, int64_t fromRelMs, int64_t toRelMs) const {
        std::vector<fake::Written> v;
        for (const fake::Written& w : port->writes)
            if ((cmd < 0 || w.cmd() == cmd) && w.t >= at(fromRelMs) && w.t < at(toRelMs)) v.push_back(w);
        return v;
    }
    void sessionStart(const Engine::Config& c) {
        eng.setControllerNumber(0);
        eng.onSessionStart(c);
    }
    // PCM: one 5 ms packet (240 frames) every 5 ms from [fromRelMs] for [count] packets.
    void pcm(int64_t fromRelMs, int count, bool silent, int seqBase = 0, double amp = 8000.0) {
        for (int k = 0; k < count; k++) {
            clock.at(at(fromRelMs) + k * 5 * MS, [this, k, silent, seqBase, amp] {
                std::vector<int16_t> p(480, 0);
                if (!silent) {
                    for (int i = 0; i < 240; i++) {
                        const double v = amp * std::sin(2.0 * jc::kPi * 100.0 * ((seqBase + k) * 240 + i) / 48000.0);
                        const int16_t s = static_cast<int16_t>(jc::javaRound(v));
                        p[2 * static_cast<size_t>(i)] = s;
                        p[2 * static_cast<size_t>(i) + 1] = s;
                    }
                }
                eng.onPcm(0, static_cast<uint32_t>(seqBase + k), p.data(), 240);
            });
        }
    }
};

Engine::Config cfgDefault() { return Engine::Config(); }

bool sameFrame(const fake::Written& w, const pr::Frame& f) { return w.f == f; }

bool triggerChannelNeutral(const fake::Written& w) {
    for (int i = 0; i < 8; i++)
        if (w.f[static_cast<size_t>(5 + i * 3)] != 0x80) return false;
    return true;
}

void note(int n, const std::string& s) {
    char b[16];
    std::snprintf(b, sizeof b, "%02d", n);
    mt::report().push_back(std::string("engine scenario ") + b + ": " + s);
}

}  // namespace

// ① :727-737 — wait for the first input report up to 3 s, then GetInfo anyway (drain read 10 ms first).
TEST(engine_d6, s01_input_wait_then_getinfo) {
    {
        Rig r;
        r.attach();
        r.run(3200);
        const auto gi = r.writes(pr::CMD_GET_INFO, 0, 3200);
        CHECK_EQ(gi.size(), static_cast<size_t>(1));
        CHECK_EQ(r.rel(gi[0].t), 3010);  // 150 x 20 ms wait + 10 ms drain read
        CHECK(sameFrame(gi[0], pr::buildGetInfo()));
        CHECK_EQ(r.log.count("입력 리포트 없이 대기 만료"), 1);
        CHECK(r.eng.state() == Engine::State::NONE);  // terminated by the test (READY -> NONE in finally)
    }
    {
        Rig r;
        r.attach();
        r.event(500, [&] { r.eng.onInputSeen(); });
        r.run(800);
        const auto gi = r.writes(pr::CMD_GET_INFO, 0, 800);
        CHECK_EQ(gi.size(), static_cast<size_t>(1));
        CHECK_EQ(r.rel(gi[0].t), 510);
        CHECK_EQ(r.log.count("첫 입력 리포트 수신(500ms 후)"), 1);
    }
    note(1, "no input -> GetInfo at +3010 ms; input at +500 -> GetInfo at +510 ms");
}

// ② :739-768 — no answer: 10 attempts 1.61 s apart -> UNSUPPORTED; input after a >= 2 s gap restarts probing.
TEST(engine_d6, s02_ten_failures_unsupported_then_input_retry) {
    Rig r;
    fake::FakePort& p = r.attach();
    p.respond = false;
    r.eng.onInputSeen();  // lastInput = t0
    std::string st2000, st15500;
    Engine::State s15500 = Engine::State::NONE;
    r.event(2000, [&] { st2000 = r.eng.statusText(); });
    r.event(5000, [&] { r.eng.onInputSeen(); });  // PROBING: must not restart
    r.event(15500, [&] {
        st15500 = r.eng.statusText();
        s15500 = r.eng.state();
    });
    r.event(16000, [&] { r.eng.onInputSeen(); });  // UNSUPPORTED + gap >= 2 s -> restart
    r.run(16500);
    const auto gi = r.writes(pr::CMD_GET_INFO, 0, 16500);
    CHECK_EQ(gi.size(), static_cast<size_t>(11));
    for (size_t k = 0; k < 10; k++) CHECK_EQ(r.rel(gi[k].t), 10 + static_cast<int64_t>(k) * 1610);
    CHECK_EQ(r.rel(gi[10].t), 16010);
    CHECK_EQ(st2000, std::string("PROBING 2/10"));
    CHECK(s15500 == Engine::State::UNSUPPORTED);
    CHECK_EQ(st15500, std::string("UNSUPPORTED"));
    CHECK_EQ(r.eng.stats().get(SessionStats::RETRIES), 9);
    CHECK_EQ(p.count(0x53) + p.count(0x57), 0);
    CHECK_EQ(r.log.count("UNSUPPORTED"), 1);
    CHECK_EQ(r.log.count("입력 리포트 재개 감지"), 1);
    note(2, "GetInfo x10 at +10+1610k ms, UNSUPPORTED at 15.1 s, input restart -> 11th GetInfo at +16010 ms, RETRIES=9, no 0x53/0x57");
}

// ③ :811-814 — answer OK but not Pro (DeviceType 0x90) -> UNSUPPORTED, nothing else is ever sent.
TEST(engine_d6, s03_non_pro_sends_nothing) {
    Rig r;
    fake::FakePort& p = r.attach();
    p.deviceType = 0x90;
    r.sessionStart(cfgDefault());
    r.eng.onInputSeen();
    Engine::State s1000 = Engine::State::NONE;
    r.event(1000, [&] { s1000 = r.eng.state(); });
    r.run(2000);
    CHECK_EQ(p.count(pr::CMD_GET_INFO), 1);
    CHECK_EQ(p.count(0x53) + p.count(0x57), 0);
    CHECK(s1000 == Engine::State::UNSUPPORTED);
    CHECK_EQ(r.log.count("Pro/그립햅틱 아님"), 1);
    note(3, "DeviceType 0x90 -> 1 GetInfo only, UNSUPPORTED, no 0x53/0x57");
}

// ④ :818-854 — activation: [trigger on] 0x53 01 02 03 00 40 -> 0x53 grip enable -> 94 ms -> 0x57 every 8 ms.
TEST(engine_d6, s04_activation_order_and_8ms_slots) {
    Rig r;
    fake::FakePort& p = r.attach();
    r.sessionStart(cfgDefault());
    r.eng.onInputSeen();
    r.pcm(0, 300, false);  // 1.5 s of 100 Hz PCM
    r.run(1500);
    CHECK(p.writes.size() >= 4);
    CHECK(p.writes[0].isGetInfo());
    CHECK_EQ(r.rel(p.writes[0].t), 10);
    CHECK(sameFrame(p.writes[1], pr::triggerEnableBoth()));
    CHECK(sameFrame(p.writes[2], pr::gripRealtimeEnable()));
    CHECK_EQ(r.rel(p.writes[1].t), 12);
    CHECK_EQ(r.rel(p.writes[2].t), 12);
    CHECK(p.writes[3].is57());
    CHECK_EQ((p.writes[3].t - p.writes[2].t) / MS, 94);
    const auto f = r.writes(0x57, 0, 1106 + 1);
    CHECK_EQ(f.size(), static_cast<size_t>(126));  // +106 .. +1106 every 8 ms
    int drive = 0;
    for (size_t k = 0; k < f.size(); k++) {
        CHECK_EQ(r.rel(f[k].t), 106 + static_cast<int64_t>(k) * 8);
        CHECK_EQ(f[k].flags(), 0x9B);
        if (!f[k].gripNeutral()) drive++;
    }
    CHECK(drive > 100);
    CHECK_EQ(p.threadStarts, 1);
    CHECK_EQ(p.threadEnds, 1);
    note(4, "GetInfo +10, 0x53 trigger+grip +12, first 0x57 +106 (94 ms), 126 frames exactly 8 ms apart, " + std::to_string(drive) + " drive frames");
}

// ⑤ :499-508 — 100 ms without PCM -> queue cleared, jitter reset, neutral frames, logged once.
TEST(engine_d6, s05_pcm_timeout_clears_queue) {
    Rig r;
    fake::FakePort& p = r.attach();
    Engine::Config c = cfgDefault();
    c.jitterFixedMs = 80;  // keep samples queued (never reaches the 80 ms playout target)
    r.sessionStart(c);
    r.eng.onInputSeen();
    r.pcm(200, 12, false);  // +200..+255 ms, 60 samples
    int q361 = -1, q363 = -1;
    r.event(361, [&] { q361 = r.eng.pipe().queueSize(); });
    r.event(363, [&] { q363 = r.eng.pipe().queueSize(); });
    r.run(800);
    CHECK_EQ(q361, 60);
    CHECK_EQ(q363, 0);
    CHECK_EQ(r.log.count("PCM 100ms 무수신 -> 중립 프레임"), 1);
    CHECK_EQ(r.rel(r.log.firstTime("PCM 100ms 무수신")), 362);  // first slot with now - lastPcm(255) > 100
    for (const auto& w : r.writes(0x57, 0, 800)) CHECK(w.gripNeutral());
    (void)p;
    note(5, "queue 60 at +361 ms -> 0 at +363 ms (slot +362 = last PCM +255 + 107 ms), logged once, all frames neutral");
}

// ⑥ :561-571 — silence: mode 1 keepalive 50 ms (56 ms on the 8 ms grid), mode 2 200 ms, mode 4 none, mode 3 suspend/resume.
TEST(engine_d6, s06_silence_modes) {
    auto relTimes = [](Rig& r, int64_t to) {
        std::vector<int64_t> v;
        for (const auto& w : r.writes(0x57, 0, to)) v.push_back(r.rel(w.t) - 106);
        return v;
    };
    {
        Rig r;
        r.attach();
        r.sessionStart(cfgDefault());
        r.eng.onInputSeen();
        r.run(500);
        const std::vector<int64_t> v = relTimes(r, 500);
        const std::vector<int64_t> e = {0, 8, 16, 24, 32, 40, 48, 56, 112, 168, 224, 280, 336, 392};
        CHECK_EQ(mt::str(v.size()), mt::str(e.size()));
        CHECK(v == e);
    }
    {
        Rig r;
        r.attach();
        Engine::Config c = cfgDefault();
        c.silence = Engine::SILENCE_KEEPALIVE_200;
        r.sessionStart(c);
        r.eng.onInputSeen();
        r.run(800);
        const std::vector<int64_t> e = {0, 8, 16, 24, 32, 40, 48, 56, 256, 456, 656};
        CHECK(relTimes(r, 800) == e);
    }
    {
        Rig r;
        r.attach();
        Engine::Config c = cfgDefault();
        c.silence = Engine::SILENCE_STOP_SENDING;
        r.sessionStart(c);
        r.eng.onInputSeen();
        r.run(1500);
        const std::vector<int64_t> e = {0, 8, 16, 24, 32, 40, 48, 56};
        CHECK(relTimes(r, 1500) == e);
    }
    {
        Rig r;
        fake::FakePort& p = r.attach();
        Engine::Config c = cfgDefault();
        c.silence = Engine::SILENCE_SUSPEND;
        c.triggerHaptics = false;
        r.sessionStart(c);
        r.eng.onInputSeen();
        r.pcm(200, 560, true);             // silent PCM +200 .. +2995 ms
        r.pcm(3000, 100, false, 560);      // loud PCM from +3000 ms
        r.run(3500);
        // stop sequence at +2202 (first slot after silentMs reached 2000 ms at +2195)
        const auto s = r.writes(-1, 2202, 2227);
        CHECK_EQ(s.size(), static_cast<size_t>(5));
        if (s.size() == 5) {
            CHECK_EQ(r.rel(s[0].t), 2202);
            CHECK_EQ(r.rel(s[1].t), 2210);
            CHECK_EQ(r.rel(s[2].t), 2218);
            CHECK(sameFrame(s[2], pr::neutralFrame()));
            CHECK(sameFrame(s[3], pr::disablePair()[0]));
            CHECK(sameFrame(s[4], pr::disablePair()[1]));
            CHECK_EQ(r.rel(s[4].t), 2226);
        }
        CHECK_EQ(r.writes(-1, 2227, 3002).size(), static_cast<size_t>(0));  // suspended: nothing sent
        const auto re = r.writes(-1, 3002, 3097);
        CHECK(re.size() >= 2);
        if (re.size() >= 2) {
            CHECK(sameFrame(re[0], pr::gripRealtimeEnable()));
            CHECK_EQ(r.rel(re[0].t), 3002);
            CHECK(re[1].is57());
            CHECK_EQ(r.rel(re[1].t), 3096);
        }
        CHECK_EQ(r.log.count("긴 무음"), 1);
        CHECK_EQ(r.log.count("무음 해제"), 1);
        (void)p;
    }
    note(6, "mode1 0..56 then every 56 ms; mode2 then every 200 ms; mode4 8 frames only; mode3 stop seq +2202..+2226, silent until resume enable +3002, 0x57 +3096");
}

// ⑦ :495-498 — rumble held 3 s without update -> stopped (RUMBLE_TIMEOUTS=1).
TEST(engine_d6, s07_rumble_three_second_timeout) {
    Rig r;
    r.attach();
    r.sessionStart(cfgDefault());
    r.eng.onInputSeen();
    r.event(300, [&] { r.eng.onRumble(0xFFFF, 0); });
    r.run(4000);
    const auto f = r.writes(0x57, 300, 4000);
    int64_t lastDrive = -1, firstNeutralAfter = -1;
    int maxLeft = 0, drive = 0;
    bool rightQuiet = true;
    for (const auto& w : f) {
        if (!w.gripNeutral()) {
            lastDrive = r.rel(w.t);
            drive++;
            for (int i = 0; i < 8; i++) {
                maxLeft = (std::max)(maxLeft, std::abs(static_cast<int>(w.f[static_cast<size_t>(6 + i * 3)]) - 128));
                if (w.f[static_cast<size_t>(7 + i * 3)] != 0x80) rightQuiet = false;
            }
        } else if (lastDrive >= 0 && firstNeutralAfter < 0) {
            firstNeutralAfter = r.rel(w.t);
        }
    }
    CHECK_EQ(r.rel(f.front().t), 306);
    CHECK_EQ(lastDrive, 3298);
    CHECK_EQ(firstNeutralAfter, 3306);
    CHECK_EQ(drive, 375);  // +306 .. +3298 every 8 ms
    CHECK(rightQuiet);
    CHECK(maxLeft <= 112 && maxLeft >= 100);
    CHECK_EQ(r.eng.stats().get(SessionStats::RUMBLE_TIMEOUTS), 1);
    CHECK_EQ(r.log.count("rumble 타임아웃"), 1);
    CHECK_EQ(r.rel(r.log.firstTime("rumble 타임아웃")), 3306);
    note(7, "rumble at +300 -> 375 drive frames +306..+3298 (left only, peak " + std::to_string(maxLeft) + "), timeout at +3306");
}

// ⑧ :548-556 — trigger: route L enable pair, no release without update (> 5 s), Off -> 100 ms hold -> 4 release frames on Both.
TEST(engine_d6, s08_trigger_release_delay_and_no_timeout_release) {
    Rig r;
    fake::FakePort& p = r.attach();
    r.sessionStart(cfgDefault());
    r.eng.onInputSeen();
    r.eng.onTriggerPosition(200, 0);
    uint8_t fb[10] = {0xFF, 0x03, 0, 0, 0, 0, 0, 0, 0, 0};
    int64_t packed = 0;
    for (int z = 0; z < 10; z++) packed |= 3LL << (z * 3);
    for (int i = 0; i < 4; i++) fb[2 + i] = static_cast<uint8_t>((packed >> (8 * i)) & 0xFF);
    const uint8_t zero[10] = {0};
    r.event(300, [&] { r.eng.onTriggerEffects(0, 0x08, 0x21, 0x00, fb, zero); });
    r.event(5300, [&] { r.eng.onTriggerEffects(0, 0x08, 0x00, 0x00, zero, zero); });
    r.run(5600);
    // route switch to Left right before the first trigger frame
    const auto side = pr::triggerSideEnable(pr::ROUTE_LEFT);
    const auto w306 = r.writes(-1, 306, 307);
    CHECK_EQ(w306.size(), static_cast<size_t>(3));
    if (w306.size() == 3) {
        CHECK(sameFrame(w306[0], side[0]));
        CHECK(sameFrame(w306[1], side[1]));
        CHECK_EQ(w306[2].flags(), 0x9C);
    }
    // continuous trigger frames with no refresh for 5 s: never released
    const auto f = r.writes(0x57, 306, 5600);
    size_t last9C = 0;
    for (size_t k = 0; k < f.size(); k++)
        if (f[k].flags() == 0x9C) last9C = k;
    CHECK(r.rel(f[last9C].t) == 5394 || r.rel(f[last9C].t) == 5402);
    for (size_t k = 0; k <= last9C; k++) {
        CHECK_EQ(f[k].flags(), 0x9C);
        CHECK_EQ(r.rel(f[k].t), 306 + static_cast<int64_t>(k) * 8);
    }
    // exactly 4 release frames (Both, zero trigger samples) then grip-only
    CHECK(last9C + 5 < f.size());
    for (size_t k = last9C + 1; k <= last9C + 4 && k < f.size(); k++) {
        CHECK_EQ(f[k].flags(), 0x9E);
        CHECK(triggerChannelNeutral(f[k]));
        CHECK_EQ(f[k].t - f[k - 1].t, 8 * MS);
    }
    if (last9C + 5 < f.size()) CHECK_EQ(f[last9C + 5].flags(), 0x9B);
    CHECK_EQ(static_cast<int>(r.writes(0x53, 0, 5600).size()), 4);  // trigger-both + grip enable + Left pair
    CHECK_EQ(r.eng.trig().stats()[7], 0);
    CHECK_EQ(r.log.count("트리거 효과 해제 예약"), 1);
    (void)p;
    note(8, "Left pair + 0x9C at +306, 0x9C every 8 ms for 5 s without refresh, last 0x9C at +" + std::to_string(r.rel(f[last9C].t)) +
                ", then 4 x 0x9E (Both, trigger 0x80), then 0x9B");
}

// ⑨ :871-880 — stop sequence: neutral 0x57 x3 (8 ms) -> 0x53 01 02 00 -> 0x53 01 12 00 (user stop at +500).
TEST(engine_d6, s09_stop_sequence_bytes_and_timing) {
    Rig r;
    fake::FakePort& p = r.attach();
    r.sessionStart(cfgDefault());
    r.eng.onInputSeen();
    r.event(500, [&] { r.eng.userStop(); });
    std::string st1000;
    r.event(1000, [&] { st1000 = r.eng.statusText(); });
    r.run(1500);
    const auto s = r.writes(-1, 500, 1500);
    CHECK_EQ(s.size(), static_cast<size_t>(5));
    if (s.size() == 5) {
        for (int i = 0; i < 3; i++) {
            CHECK(sameFrame(s[static_cast<size_t>(i)], pr::neutralFrame()));
            CHECK_EQ(r.rel(s[static_cast<size_t>(i)].t), 506 + 8 * i);
        }
        CHECK(sameFrame(s[3], pr::disablePair()[0]));
        CHECK(sameFrame(s[4], pr::disablePair()[1]));
        CHECK_EQ(r.rel(s[3].t), 530);
        CHECK_EQ(r.rel(s[4].t), 530);
    }
    CHECK_EQ(r.log.count("정지 시퀀스 시작: 사용자 정지"), 1);
    CHECK_EQ(r.log.count("전송 성공 5/5"), 1);
    CHECK_EQ(st1000, std::string("USER_STOP"));
    CHECK_EQ(p.count(pr::CMD_GET_INFO), 1);  // session running: no battery GetInfo
    note(9, "neutral 0x57 at +506/+514/+522, 0x53 01 02 00 + 01 12 00 at +530, nothing after; USER_STOP");
}

// ⑩ :856-869, :293-298 — session end: one battery GetInfo, then the stop sequence, then one session summary.
TEST(engine_d6, s10_session_end_battery_then_summary) {
    Rig r;
    fake::FakePort& p = r.attach();
    r.eng.onInputSeen();
    r.event(50, [&] { r.sessionStart(cfgDefault()); });  // after the probe (battery 7 known)
    r.event(500, [&] {
        p.battery = 5;
        r.eng.onSessionEndBegin("테스트");
    });
    int summariesAt1099 = -1;
    r.event(1099, [&] { summariesAt1099 = r.log.count("ARBS 세션 요약"); });
    r.event(1100, [&] { r.eng.onSessionEndFinish("테스트"); });
    r.run(1500);
    const auto s = r.writes(-1, 500, 1500);
    CHECK_EQ(s.size(), static_cast<size_t>(6));
    if (s.size() == 6) {
        CHECK(s[0].isGetInfo());
        CHECK_EQ(r.rel(s[0].t), 503);
        for (int i = 0; i < 3; i++) {
            CHECK(sameFrame(s[static_cast<size_t>(1 + i)], pr::neutralFrame()));
            CHECK_EQ(r.rel(s[static_cast<size_t>(1 + i)].t), 505 + 8 * i);
        }
        CHECK(sameFrame(s[4], pr::disablePair()[0]));
        CHECK(sameFrame(s[5], pr::disablePair()[1]));
        CHECK_EQ(r.rel(s[5].t), 529);
    }
    CHECK_EQ(summariesAt1099, 1);
    CHECK_EQ(r.log.count("ARBS 세션 요약"), 1);
    const std::string sum = r.log.find("ARBS 세션 요약");
    CHECK_MSG(sum.find("(세션 종료(테스트))") != std::string::npos, sum);
    CHECK_MSG(sum.find("시작 7 종료 5") != std::string::npos, sum);
    CHECK_EQ(r.rel(r.log.firstTime("ARBS 세션 요약")), 529);
    note(10, "GetInfo +503 (battery 5), neutral x3 +505.., 0x53 pair +529, one summary '시작 7 종료 5' at +529, finish call ignored");
}

// ⑪ :580-583, :611-612 — write failure -> stop sequence -> engine loop ends by itself; [다시 연결] restarts it.
TEST(engine_d6, s11_write_failure_stops_and_ends_loop) {
    Rig r;
    fake::FakePort& p = r.attach();
    p.fail57At = 20;
    r.sessionStart(cfgDefault());
    r.eng.onInputSeen();
    r.run(5000);
    CHECK(!r.clock.limitHit());  // the loop ended on its own
    size_t fi = 0;
    bool found = false;
    for (size_t i = 0; i < p.writes.size(); i++)
        if (!p.writes[i].ok) {
            fi = i;
            found = true;
            break;
        }
    CHECK(found);
    CHECK_EQ(p.writes.size(), fi + 6);
    if (found && p.writes.size() == fi + 6) {
        const int64_t tf = p.writes[fi].t;
        for (int i = 0; i < 3; i++) {
            CHECK(sameFrame(p.writes[fi + 1 + static_cast<size_t>(i)], pr::neutralFrame()));
            CHECK_EQ(p.writes[fi + 1 + static_cast<size_t>(i)].t - tf, i * 8 * MS);
        }
        CHECK(sameFrame(p.writes[fi + 4], pr::disablePair()[0]));
        CHECK(sameFrame(p.writes[fi + 5], pr::disablePair()[1]));
    }
    CHECK(r.eng.state() == Engine::State::NONE);
    CHECK_EQ(r.eng.stats().get(SessionStats::USB_ERRORS), 1);
    CHECK_EQ(r.log.count("0x57 전송 실패"), 1);
    CHECK_EQ(r.log.count("정지 시퀀스 시작: 엔진 오류"), 1);
    CHECK_EQ(r.log.count("엔진 스레드 종료"), 1);
    const int64_t tEnd = r.clock.nowNs();
    CHECK_EQ(r.eng.manualRetry(), std::string("재시작"));
    r.clock.setLimit(tEnd + 1000 * MS, [&] { r.eng.requestTerminate("test end 2"); });
    r.eng.runLoop();
    int gi = 0, en = 0;
    for (const auto& w : p.writes)
        if (w.t > tEnd) {
            if (w.isGetInfo()) gi++;
            if (sameFrame(w, pr::gripRealtimeEnable())) en++;
        }
    CHECK_EQ(gi, 1);
    CHECK_EQ(en, 1);
    note(11, "20th 0x57 fails -> neutral x3 + 0x53 pair, loop ends (state NONE, USB_ERRORS=1); manualRetry -> '재시작' -> GetInfo + enable again");
}

// ⑫ :336-345 — user stop / resume.
TEST(engine_d6, s12_user_stop_resume) {
    Rig r;
    fake::FakePort& p = r.attach();
    r.sessionStart(cfgDefault());
    r.eng.onInputSeen();
    std::string st700, st1200;
    r.event(500, [&] { r.eng.userStop(); });
    r.event(700, [&] { st700 = r.eng.statusText(); });
    r.event(1000, [&] { r.eng.userResume(); });
    r.event(1200, [&] { st1200 = r.eng.statusText(); });
    r.run(1500);
    CHECK_EQ(r.writes(-1, 531, 1000).size(), static_cast<size_t>(0));
    const auto re = r.writes(-1, 1000, 1095);
    CHECK(re.size() >= 3);
    if (re.size() >= 3) {
        CHECK(sameFrame(re[0], pr::triggerEnableBoth()));
        CHECK(sameFrame(re[1], pr::gripRealtimeEnable()));
        CHECK_EQ(r.rel(re[1].t), 1000);
        CHECK(re[2].is57());
        CHECK_EQ(r.rel(re[2].t), 1094);
    }
    CHECK_EQ(st700, std::string("USER_STOP"));
    CHECK_EQ(st1200, std::string("READY"));
    CHECK_EQ(r.log.count("APEX 재개 요청(사용자)"), 1);
    (void)p;
    note(12, "stop seq at +506..+530, silent until resume at +1000 (0x53 trigger + grip), 0x57 again at +1094; USER_STOP -> READY");
}

// ⑬ :652 — 1-minute summary from the 1 s stats tick.
TEST(engine_d6, s13_one_minute_summary) {
    Rig r;
    r.attach();
    r.sessionStart(cfgDefault());  // stats reset at t0
    r.eng.onInputSeen();
    r.run(125000);
    CHECK_EQ(r.log.count("ARBS 1분 요약"), 2);
    std::vector<int64_t> t;
    for (const auto& l : r.log.lines)
        if (l.second.find("ARBS 1분 요약") != std::string::npos) t.push_back(r.rel(l.first));
    CHECK(t.size() == 2 && t[0] == 60106 && t[1] == 120106);
    const std::string m = r.log.find("ARBS 1분 요약");
    CHECK_MSG(m.find("경과 00:01:00 세션=연결됨 APEX=READY/출력중") != std::string::npos, m);
    note(13, "1-minute lines at +60106 and +120106 ms (1 s stats tick from activation +106)");
}

// ---------------------------------------------------------------- real-thread smoke test (not one of the 13)
namespace {
struct SteadyClock : IClock {
    int64_t nowNs() override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void sleepUntilNs(int64_t d) override {
        const int64_t n = nowNs();
        if (d > n) std::this_thread::sleep_for(std::chrono::nanoseconds(d - n));
    }
};

struct Recorder {
    std::mutex mu;
    std::vector<pr::Frame> frames;
    std::vector<std::array<uint8_t, 33>> rx;
    std::atomic<int> starts{0}, ends{0};
};

struct ThreadPort : IApexPort {
    std::shared_ptr<Recorder> rec;
    explicit ThreadPort(std::shared_ptr<Recorder> r) : rec(std::move(r)) {}
    IoResult writeFrame(const uint8_t frame[32], int) override {
        std::lock_guard<std::mutex> g(rec->mu);
        pr::Frame f;
        std::memcpy(f.data(), frame, 32);
        rec->frames.push_back(f);
        if (frame[2] == 0x01) {
            std::array<uint8_t, 33> a{};
            uint8_t* x = a.data() + 1;
            x[0] = 0x5A; x[1] = 0xA5; x[2] = 0x01; x[5] = 0x96; x[11] = 0x07; x[29] = 0x80;
            x[31] = static_cast<uint8_t>(pr::checksum(x));
            rec->rx.push_back(a);
        }
        return IoResult::Ok;
    }
    IoResult readFrame(uint8_t* buf, size_t cap, size_t* outLen, int timeoutMs) override {
        {
            std::lock_guard<std::mutex> g(rec->mu);
            if (!rec->rx.empty()) {
                const size_t n = (std::min)(cap, rec->rx.front().size());
                std::memcpy(buf, rec->rx.front().data(), n);
                rec->rx.erase(rec->rx.begin());
                *outLen = n;
                return IoResult::Ok;
            }
        }
        *outLen = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return IoResult::Timeout;
    }
    void onEngineThreadStart() override { rec->starts++; }
    void onEngineThreadEnd() override { rec->ends++; }
};

struct NullLog : ILog {
    std::mutex mu;
    std::vector<std::string> lines;
    void info(const std::string& s) override {
        std::lock_guard<std::mutex> g(mu);
        lines.push_back(s);
    }
    bool perSecondAllowed() override { return true; }
};
}  // namespace

TEST(engine_thread, smoke_attach_session_detach_real_time) {
    SteadyClock clock;
    NullLog log;
    auto rec = std::make_shared<Recorder>();
    const auto t0 = std::chrono::steady_clock::now();
    {
        Engine eng(clock, log);
        eng.attach(std::make_unique<ThreadPort>(rec));
        eng.onInputSeen();
        Engine::Config c;
        eng.onSessionStart(c);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(eng.isActive());
        const auto d0 = std::chrono::steady_clock::now();
        eng.detach("smoke 종료");
        const auto dms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - d0).count();
        CHECK_MSG(dms < 1500, "detach took " + std::to_string(dms) + " ms");
        CHECK(eng.state() == Engine::State::NONE);
        mt::report().push_back("engine thread smoke: detach joined in " + std::to_string(dms) + " ms");
    }
    std::lock_guard<std::mutex> g(rec->mu);
    CHECK(rec->frames.size() > 10);
    CHECK(rec->frames.front()[2] == 0x01);
    const size_t n = rec->frames.size();
    CHECK(rec->frames[n - 2] == pr::disablePair()[0]);
    CHECK(rec->frames[n - 1] == pr::disablePair()[1]);
    CHECK_EQ(rec->starts.load(), 1);
    CHECK_EQ(rec->ends.load(), 1);
    (void)t0;
}

// ---------------------------------------------------------------- B5 D-B5-1: APEX features 0x10 (trigger haptics)
// analysis/B5.md 1-4 T1~T4. features without 0x10 -> no trigger channel enable / no trigger frames, grip continues.
namespace {
void b5Fb(uint8_t fb[10]) {  // same weapon effect as s08
    std::memset(fb, 0, 10);
    fb[0] = 0xFF;
    fb[1] = 0x03;
    int64_t packed = 0;
    for (int z = 0; z < 10; z++) packed |= 3LL << (z * 3);
    for (int i = 0; i < 4; i++) fb[2 + i] = static_cast<uint8_t>((packed >> (8 * i)) & 0xFF);
}

bool b5IsTrigger53(const fake::Written& w) {
    if (!w.is53()) return false;
    const auto l = pr::triggerSideEnable(pr::ROUTE_LEFT);
    const auto rr = pr::triggerSideEnable(pr::ROUTE_RIGHT);
    return sameFrame(w, pr::triggerEnableBoth()) || sameFrame(w, l[0]) || sameFrame(w, l[1]) || sameFrame(w, rr[0]) ||
           sameFrame(w, rr[1]);
}

struct B5Count {
    int trig53 = 0, gripEnable = 0, f57 = 0, f57Trig = 0, f57TrigBytes = 0, drive = 0;
};

B5Count b5Count(const fake::FakePort& p) {
    B5Count c;
    for (const fake::Written& w : p.writes) {
        if (b5IsTrigger53(w)) c.trig53++;
        if (sameFrame(w, pr::gripRealtimeEnable())) c.gripEnable++;
        if (w.is57()) {
            c.f57++;
            if ((w.flags() & 0x04) != 0) c.f57Trig++;
            if (!triggerChannelNeutral(w)) c.f57TrigBytes++;
            if (!w.gripNeutral()) c.drive++;
        }
    }
    return c;
}

uint8_t b5EffectFb[10];
const uint8_t b5Zero[10] = {0};

// Session start at [sessionAtRelMs] (0 = before the probe), trigger position right after it, weapon effect at +300,
// 100 Hz PCM for 1.5 s. Probe passes at +12 (GetInfo +10).
B5Count b5TriggerScenario(Rig& r, int features, int64_t sessionAtRelMs) {
    fake::FakePort& p = r.attach();
    p.features = features;
    if (sessionAtRelMs <= 0) {
        r.sessionStart(cfgDefault());
        r.eng.onTriggerPosition(200, 0);
    } else {
        r.event(sessionAtRelMs, [&r] {
            r.sessionStart(cfgDefault());
            r.eng.onTriggerPosition(200, 0);
        });
    }
    r.eng.onInputSeen();
    b5Fb(b5EffectFb);
    r.event(300, [&r] { r.eng.onTriggerEffects(0, 0x08, 0x21, 0x00, b5EffectFb, b5Zero); });
    r.pcm(0, 300, false);
    r.run(1600);
    return b5Count(p);
}
}  // namespace

TEST(engine_d6, b5_t0_info_has_trigger_haptics_bits) {
    pr::Info i;
    const int yes[] = {0x9F, 0x90, 0x10, 0xFF};
    for (int f : yes) {
        i.features = f;
        CHECK_MSG(i.hasTriggerHaptics(), "features " + std::to_string(f));
    }
    const int no[] = {0x80, 0x8F, 0x00, 0xEF};
    for (int f : no) {
        i.features = f;
        CHECK_MSG(!i.hasTriggerHaptics(), "features " + std::to_string(f));
    }
    i.features = 0x80;
    CHECK(i.hasGripHaptics());
    note(20, "Info::hasTriggerHaptics: 0x9F/0x90/0x10/0xFF true, 0x80/0x8F/0x00/0xEF false");
}

TEST(engine_d6, b5_t1_features_9f_trigger_unchanged) {
    Rig r;
    const B5Count c = b5TriggerScenario(r, 0x9F, 0);
    const fake::FakePort& p = *r.port;
    CHECK(p.writes.size() > 3);
    if (p.writes.size() > 3) {
        CHECK(sameFrame(p.writes[1], pr::triggerEnableBoth()));
        CHECK(sameFrame(p.writes[2], pr::gripRealtimeEnable()));
    }
    const auto side = pr::triggerSideEnable(pr::ROUTE_LEFT);
    const auto w306 = r.writes(-1, 306, 307);
    CHECK_EQ(w306.size(), static_cast<size_t>(3));
    if (w306.size() == 3) {
        CHECK(sameFrame(w306[0], side[0]));
        CHECK(sameFrame(w306[1], side[1]));
        CHECK_EQ(w306[2].flags(), 0x9C);
    }
    CHECK_EQ(c.trig53, 3);  // Both + Left pair
    CHECK(c.f57Trig > 100);
    CHECK(c.drive > 100);
    CHECK_EQ(r.log.count("트리거 햅틱 없음"), 0);
    CHECK_EQ(r.log.count("0x10 없음"), 0);
    note(21, "features 0x9F: 0x53 trigger Both + grip at +12, Left pair + 0x9C at +306, " + std::to_string(c.f57Trig) +
                 " trigger frames, no 'no trigger haptics' line");
}

TEST(engine_d6, b5_t2_features_80_no_trigger_grip_kept) {
    Rig r;
    const B5Count c = b5TriggerScenario(r, 0x80, 0);
    CHECK_EQ(c.trig53, 0);
    CHECK_EQ(c.gripEnable, 1);
    CHECK(c.f57 > 100);
    CHECK_EQ(c.f57Trig, 0);
    CHECK_EQ(c.f57TrigBytes, 0);
    CHECK(c.drive > 100);
    CHECK_EQ(r.log.count("APEX 트리거 햅틱 없음: features=0x80(0x10 꺼짐)"), 1);
    CHECK_EQ(r.log.count("APEX 트리거 채널 활성: 0x53"), 0);
    CHECK_EQ(r.eng.trig().stats()[0], 0);  // effects not even received
    CHECK(r.eng.state() == Engine::State::NONE);
    note(22, "features 0x80: 0 trigger 0x53, " + std::to_string(c.f57) + " x 0x57 all flags&0x04=0 / trigger bytes 0x80, " +
                 std::to_string(c.drive) + " grip drive frames, 1 log line");
}

TEST(engine_d6, b5_t3_features_80_session_after_probe) {
    Rig r;
    const B5Count c = b5TriggerScenario(r, 0x80, 200);
    CHECK_EQ(c.trig53, 0);
    CHECK_EQ(c.gripEnable, 1);
    CHECK_EQ(c.f57Trig, 0);
    CHECK_EQ(c.f57TrigBytes, 0);
    CHECK(c.drive > 100);
    CHECK_EQ(r.log.count("트리거 햅틱 없음"), 1);
    CHECK_EQ(r.log.count("(기기 features 0x10 없음 -> 실제 끔)"), 1);
    CHECK_EQ(r.eng.trig().stats()[0], 0);
    note(23, "features 0x80, session start +200 (after probe): setting does not revive the trigger, session line notes it");
}

TEST(engine_d6, b5_t4_reattach_80_then_9f_trigger_back) {
    Rig r;
    fake::FakePort& p1 = r.attach();
    p1.features = 0x80;
    r.sessionStart(cfgDefault());
    r.eng.onTriggerPosition(200, 0);
    r.eng.onInputSeen();
    r.pcm(0, 100, false);
    r.run(800);
    const B5Count c1 = b5Count(p1);  // p1 is released by detach below
    r.eng.detach("B5 재꽂음");
    fake::FakePort& p2 = r.attach();  // default features 0x9F
    r.eng.onInputSeen();
    b5Fb(b5EffectFb);
    r.event(1200, [&r] { r.eng.onTriggerPosition(200, 0); });  // after the second probe
    r.event(1300, [&r] { r.eng.onTriggerEffects(0, 0x08, 0x21, 0x00, b5EffectFb, b5Zero); });
    r.pcm(810, 200, false, 1000);
    r.run(2400);
    const B5Count c2 = b5Count(p2);
    CHECK_EQ(c1.trig53, 0);
    CHECK_EQ(c1.f57Trig, 0);
    CHECK(c1.f57 > 50);
    bool firstEnableIsTrigger = false;
    for (const fake::Written& w : p2.writes)
        if (w.is53()) {
            firstEnableIsTrigger = sameFrame(w, pr::triggerEnableBoth());
            break;
        }
    CHECK(firstEnableIsTrigger);
    CHECK(c2.trig53 >= 3);  // Both + Left pair
    CHECK(c2.f57Trig > 50);
    CHECK_EQ(r.log.count("트리거 햅틱 없음"), 1);  // only for the first device
    note(24, "0x80 device -> detach -> 0x9F device: second probe turns the trigger back on (" + std::to_string(c2.f57Trig) +
                 " trigger frames)");
}

// ---------------------------------------------------------------- B5 D-B5-3: zombie thread vs. port release
namespace {
struct ZombieRec {
    std::atomic<bool> slowNext{false}, writing{false}, destroyed{false}, destroyedWhileWriting{false};
};

struct SlowPort : IApexPort {
    std::shared_ptr<ZombieRec> z;
    std::mutex mu;
    std::vector<std::array<uint8_t, 33>> rx;
    explicit SlowPort(std::shared_ptr<ZombieRec> r) : z(std::move(r)) {}
    ~SlowPort() override {
        z->destroyedWhileWriting = z->writing.load();
        z->destroyed = true;
    }
    IoResult writeFrame(const uint8_t frame[32], int) override {
        z->writing = true;
        if (z->slowNext.exchange(false)) std::this_thread::sleep_for(std::chrono::milliseconds(1700));
        if (frame[2] == 0x01) {
            std::array<uint8_t, 33> a{};
            uint8_t* x = a.data() + 1;
            x[0] = 0x5A; x[1] = 0xA5; x[2] = 0x01; x[5] = 0x96; x[11] = 0x07; x[29] = 0x9F;
            x[31] = static_cast<uint8_t>(pr::checksum(x));
            std::lock_guard<std::mutex> g(mu);
            rx.push_back(a);
        }
        z->writing = false;
        return IoResult::Ok;
    }
    IoResult readFrame(uint8_t* buf, size_t cap, size_t* outLen, int timeoutMs) override {
        {
            std::lock_guard<std::mutex> g(mu);
            if (!rx.empty()) {
                const size_t n = (std::min)(cap, rx.front().size());
                std::memcpy(buf, rx.front().data(), n);
                rx.erase(rx.begin());
                *outLen = n;
                return IoResult::Ok;
            }
        }
        *outLen = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return IoResult::Timeout;
    }
};

int nullLogCount(NullLog& log, const std::string& sub) {
    std::lock_guard<std::mutex> g(log.mu);
    int n = 0;
    for (const std::string& s : log.lines)
        if (s.find(sub) != std::string::npos) n++;
    return n;
}
}  // namespace

TEST(engine_thread, b5_zombie_second_detach_waits_before_port_release) {
    SteadyClock clock;
    NullLog log;
    auto z = std::make_shared<ZombieRec>();
    {
        Engine eng(clock, log);
        eng.attach(std::make_unique<SlowPort>(z));
        eng.onInputSeen();
        Engine::Config c;
        eng.onSessionStart(c);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(eng.isActive());
        z->slowNext = true;  // the next write blocks 1.7 s (> 1.5 s detach wait)
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const auto d0 = std::chrono::steady_clock::now();
        eng.detach("B5 zombie 1");
        const auto ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - d0).count();
        CHECK_MSG(ms1 >= 1400, "first detach " + std::to_string(ms1) + " ms");
        CHECK(!z->destroyed.load());  // port kept for the thread that is still writing
        CHECK_EQ(nullLogCount(log, "1.5초 안에 끝나지 않음"), 1);
        const auto d1 = std::chrono::steady_clock::now();
        eng.detach("B5 zombie 2");  // no thread_ -> must join the zombie before port_.reset()
        const auto ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - d1).count();
        CHECK(z->destroyed.load());
        CHECK(!z->destroyedWhileWriting.load());
        CHECK_EQ(nullLogCount(log, "이전 엔진 스레드(1.5초 초과) 종료 대기 후 포트 정리: B5 zombie 2"), 1);
        CHECK_EQ(nullLogCount(log, "엔진 스레드 종료"), 1);
        CHECK(eng.state() == Engine::State::NONE);
        mt::report().push_back("engine thread B5 zombie: detach1 " + std::to_string(ms1) + " ms (zombie), detach2 waited " +
                               std::to_string(ms2) + " ms, port released after the thread ended");
    }
}
