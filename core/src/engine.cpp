// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexHaptics.java (f3051641) without android.hardware.usb. Line references: AA:ApexHaptics.java:<line>.
// Full Java line <-> C++ line map: docs/research/B2_CORE_MAP.md.
#include "arbs/engine.h"

#include "arbs/java_compat.h"

#include <algorithm>
#include <chrono>
#include <exception>

namespace arbs {

namespace {
constexpr int64_t kMs = 1000000LL;
long long ll(int64_t v) { return static_cast<long long>(v); }
double avgMs(int64_t sumNs, int64_t n) { return n == 0 ? 0 : static_cast<double>(sumNs) / 1e6 / static_cast<double>(n); }  // :713
}  // namespace

const char* Engine::stateName(State s) {
    switch (s) {
        case State::NONE: return "NONE";
        case State::PROBING: return "PROBING";
        case State::READY: return "READY";
        default: return "UNSUPPORTED";
    }
}

Engine::Engine(IClock& clock, ILog& log) : clock_(clock), log_(log) {
    pipe_.setLimit(JitterBuffer::REQUIRED_QUEUE);  // :45
}

Engine::~Engine() {
    requestTerminate("Engine 소멸");
    if (thread_.joinable()) thread_.join();
    if (zombie_.joinable()) zombie_.join();
}

// ---------------------------------------------------------------- external API

void Engine::setControllerNumber(int n) {  // :92-95
    apexNumber_ = n;
    log_.info("APEX 컨트롤러 번호 = " + std::to_string(n));
}

void Engine::attach(std::unique_ptr<IApexPort> port, StartMode mode) {  // :98-122
    std::lock_guard<std::recursive_mutex> g(apiMu_);
    detach("재연결");
    if (attachCount_++ > 0) stats_.add(SessionStats::RECONNECTS, 1);
    if (!port) {  // :111-115 (Windows: IF2 not found / not openable)
        state_ = State::UNSUPPORTED;
        log_.info("APEX 햅틱 불가: IF2 를 찾지 못함 - 아무것도 보내지 않음");
        return;
    }
    if (zombie_.joinable()) zombie_.join();  // C++ only: a previous thread that outlived the 1.5 s join still uses port_
    port_ = std::move(port);
    policy_.resetAll();
    retryRequested_ = false;
    startThread(mode);
}

void Engine::startThread(StartMode mode) {  // :124-131
    terminate_ = false;
    active_ = false;
    state_ = State::PROBING;
    mode_ = mode;
    if (mode == StartMode::Inline) return;  // the caller runs runLoop()
    {
        std::lock_guard<std::mutex> lk(doneMu_);
        done_ = false;
    }
    threadAlive_ = true;
    thread_ = std::thread([this] { threadMain(); });
}

void Engine::threadMain() {
    runBody();
    threadAlive_ = false;
    {
        std::lock_guard<std::mutex> lk(doneMu_);
        done_ = true;
    }
    doneCv_.notify_all();
}

void Engine::runLoop() {
    inlineRunning_ = true;
    runBody();
    inlineRunning_ = false;
}

void Engine::onInputSeen() {  // :139-144
    if (policy_.onInput(nowMs())) {
        retryRequested_ = true;
        log_.info("APEX 입력 리포트 재개 감지 -> 준비 재시도 요청");
    }
}

std::string Engine::manualRetry() {  // :147-167
    std::lock_guard<std::recursive_mutex> g(apiMu_);
    if (!engineAlive()) {
        if (!port_) {
            log_.info("APEX 다시 연결: 동글 미연결(NONE) - 할 수 없음");
            return "동글 미연결";
        }
        if (thread_.joinable()) thread_.join();  // finished thread object (C++ needs an explicit join)
        policy_.begin();
        retryRequested_ = false;
        log_.info("APEX 다시 연결(수동): 엔진 스레드 재시작");
        startThread(mode_);
        return "재시작";
    }
    if (state_ == State::UNSUPPORTED && policy_.manualRetry()) {
        retryRequested_ = true;
        log_.info("APEX 다시 연결(수동): 준비 재시도 요청");
        return "재시도 요청";
    }
    log_.info(std::string("APEX 다시 연결: 상태=") + stateName(state_) + " - 재시도 불필요");
    return std::string("상태 ") + stateName(state_);
}

std::string Engine::statusText() const {  // :170-175
    const State s = state_;
    if (s == State::READY && userStopped_) return "USER_STOP";
    if (s == State::PROBING) return "PROBING " + std::to_string(policy_.attempt()) + "/" + std::to_string(ProbePolicy::MAX_ATTEMPTS);
    return stateName(s);
}

void Engine::requestTerminate(const std::string& reason) {
    setStopReason(reason);
    terminate_ = true;
}

void Engine::detach(const std::string& reason) {  // :178-200
    std::thread t;
    {
        std::lock_guard<std::recursive_mutex> g(apiMu_);
        t = std::move(thread_);
    }
    if (!t.joinable()) {
        if (inlineRunning_) {  // inline runLoop on this very thread: cannot wait, only request (test seam)
            requestTerminate(reason);
            return;
        }
        state_ = State::NONE;
        if (zombie_.joinable()) {  // B5 D-B5-3: a thread that outlived the 1.5 s join may still use port_
            log_.info("APEX 이전 엔진 스레드(1.5초 초과) 종료 대기 후 포트 정리: " + reason);
            zombie_.join();
        }
        port_.reset();
        return;
    }
    log_.info("APEX 엔진 종료 요청: " + reason);
    setStopReason(reason);
    terminate_ = true;
    bool finished;
    {
        std::unique_lock<std::mutex> lk(doneMu_);
        finished = doneCv_.wait_for(lk, std::chrono::milliseconds(1500), [this] { return done_; });
    }
    if (finished) {
        t.join();
    } else {
        log_.info("APEX 엔진 스레드가 1.5초 안에 끝나지 않음 - 다음 연결 전에 정리");
        if (zombie_.joinable()) zombie_.join();
        zombie_ = std::move(t);
    }
    state_ = State::NONE;
    apexNumber_ = -1;
    if (finished) port_.reset();
}

std::string Engine::stopReason() const {
    std::lock_guard<std::mutex> g(reasonMu_);
    return stopReason_;
}

void Engine::setStopReason(const std::string& r) {
    std::lock_guard<std::mutex> g(reasonMu_);
    stopReason_ = r;
}

void Engine::onSessionStart(const Config& c) {  // :210-239
    log_.setVerbose(c.verbose);
    triggerOn_ = c.triggerHaptics && c.triggerStrength > 0;
    trig_.setStrengthPercent(c.triggerStrength);
    trig_.setCeilingPercent(100);  // ASB default --apex6-trigger-ceiling 100 (BridgeOptions.h:57)
    trigEffectLogged_ = 0;
    pipe_.setCap(c.cap);
    pipe_.setGain(c.gain);
    pipe_.setGateThreshold(c.gatePct);
    pipe_.setAsbCurve(c.asbCurve);
    pipe_.setTaps(c.taps);
    silenceMode_ = c.silence >= 1 && c.silence <= 4 ? c.silence : SILENCE_KEEPALIVE_50;
    keepaliveInterval_ = silenceMode_ == SILENCE_KEEPALIVE_200 ? 200 : KEEPALIVE_MS;
    suspendCount_ = 0;
    resumeCount_ = 0;
    jitter_.setFixed(c.jitterFixedMs);
    arrival_.reset();
    userStopped_ = false;
    stats_.reset(nowNs());  // A1-5
    stats_.batteryStart(lastProbeBattery_);
    rumbleBase_ = totRumbleCalls_;
    trigBase_ = trig_.stats();
    summaryEmitted_ = false;
    sessionRunning_ = true;
    const int taps = pipe_.getTaps();
    log_.info(jc::fmt(
        "APEX 세션 시작: 상태=%s 진폭 상한=%d(하드 상한 %d) 이득 G=%.2f 곡선=%s 필터=%d탭(%.1fms) 게이트=%s 무음 처리=모드%d(keepalive %lldms) 지터버퍼=%s",
        stateName(state_), pipe_.getCap(), protocol::MAX_AMPLITUDE, static_cast<double>(c.gain), c.asbCurve ? "ASB" : "tanh", taps,
        (taps - 1) / 2.0 / PcmPipeline::SAMPLE_RATE * 1000.0,
        c.gatePct > 0 ? jc::fmt("%.0f%%FS", static_cast<double>(c.gatePct)).c_str() : "끔", silenceMode_.load(),
        ll(keepaliveInterval_), c.jitterFixedMs > 0 ? ("고정 " + std::to_string(c.jitterFixedMs) + "ms").c_str() : "자동(p95+4ms)"));
    log_.info(std::string("APEX 설정 적용(세션 시작 시점): 트리거 햅틱=") +
              (triggerOn_ ? "켬 세기 " + std::to_string(c.triggerStrength) + "%(상한 127, ASB 기준)" : std::string("끔")) +
              (triggerOn_ && !hwTrigger_ ? " (기기 features 0x10 없음 -> 실제 끔)" : ""));
}

void Engine::onTriggerPosition(int left255, int right255) {  // :242-244
    if (trigOn()) trig_.setPosition(left255, right255);
}

void Engine::onTriggerEffects(uint16_t controller, uint8_t eventFlags, uint8_t typeLeft, uint8_t typeRight, const uint8_t* left,
                              const uint8_t* right) {  // :247-254
    if (!trigOn() || !sessionRunning_) return;
    const int num = apexNumber_;
    if (num >= 0 && static_cast<int>(controller) != num) return;
    const int64_t now = nowNs();
    if ((eventFlags & 0x08) != 0) logTriggerEvent(trig_.onEffect(true, typeLeft, left, now));
    if ((eventFlags & 0x04) != 0) logTriggerEvent(trig_.onEffect(false, typeRight, right, now));
}

void Engine::logTriggerEvent(const std::optional<std::string>& s) {  // :256-262
    if (!s) return;
    if (trigEffectLogged_ < 300) {
        trigEffectLogged_++;
        log_.info("APEX " + *s);
    }
}

void Engine::onSessionEnd(const std::string& reason) {  // :264-270
    if (!onSessionEndBegin(reason)) return;
    waitInactive(600);
    onSessionEndFinish(reason);
}

bool Engine::onSessionEndBegin(const std::string& reason) {  // :265-267
    if (!sessionRunning_ && !active_) return false;
    sessionRunning_ = false;
    setStopReason("세션 종료(" + reason + ")");
    return true;
}

void Engine::onSessionEndFinish(const std::string& reason) {  // :269 (already emitted by the engine -> ignored)
    emitSessionSummary("세션 종료(" + reason + ")");
}

void Engine::setVerboseLog(bool v) { log_.setVerbose(v); }  // :273

void Engine::onMoonlightRtt(int ms) {  // :276-278
    if (sessionRunning_ && ms > 0) stats_.addRtt(ms);
}

void Engine::onCrash() {  // :281-284. Crash path never blocks: try-locks only, external counters not re-synced.
    std::unique_lock<std::mutex> g(summaryMu_, std::try_to_lock);
    if (!g.owns_lock()) return;
    if (!sessionRunning_ || summaryEmitted_) return;
    std::string line;
    stats_.markCrash();
    if (stats_.trySessionLine(nowNs(), "크래시", line)) {
        summaryEmitted_ = true;
        log_.info(line);
    }
}

void Engine::syncExternalCounters() {  // :286-291
    stats_.set(SessionStats::RUMBLE_CALLS, totRumbleCalls_ - rumbleBase_);
    const TriggerBridge::Stats c = trig_.stats();
    stats_.set(SessionStats::TRIG_APPLIED, c[2] - trigBase_[2]);
    stats_.set(SessionStats::TRIG_STOPS, c[3] - trigBase_[3]);
}

void Engine::emitSessionSummary(const std::string& reason) {  // :293-298
    std::lock_guard<std::mutex> g(summaryMu_);
    if (summaryEmitted_) return;
    summaryEmitted_ = true;
    syncExternalCounters();
    log_.info(stats_.sessionLine(nowNs(), reason));
}

std::string Engine::sessionText() const { return sessionRunning_ ? "연결됨" : "없음"; }  // :300

std::string Engine::apexText() const {  // :302
    return std::string(stateName(state_)) + (active_ ? "/출력중" : "/대기") + (userStopped_ ? "/사용자정지" : "");
}

int Engine::queryBattery() {  // :305-323
    try {
        const protocol::Frame req = protocol::buildGetInfo();
        if (writeOut(req, 200) != protocol::FRAME_SIZE) return -1;
        const int64_t t0 = nowMs();
        int tries = 0;
        while (nowMs() - t0 < 300 && tries < 32) {
            tries++;
            const int n = readIn(50);
            if (n > 0) {
                const std::optional<protocol::Info> info = protocol::parse(rxBuf_.data(), static_cast<size_t>(n));
                if (info && info->checksumOk) return info->battery;
            }
        }
    } catch (...) {
    }
    return -1;
}

void Engine::onPause() {  // :325-329
    paused_ = true;
    setStopReason("onPause");
    waitInactive(600);
}

void Engine::onResume() { paused_ = false; }  // :331-333

void Engine::userStop() {  // :336-340
    userStopped_ = true;
    setStopReason("사용자 정지");
    waitInactive(600);
}

void Engine::userResume() {  // :342-345
    userStopped_ = false;
    log_.info("APEX 재개 요청(사용자)");
}

void Engine::waitInactive(int64_t ms) {  // :347-358 (real-time wait on the caller thread)
    if (inlineRunning_) return;  // inline (test) mode: the engine runs on this thread, waiting would never end
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (active_ && threadAlive_ && std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void Engine::onRumble(uint16_t low16, uint16_t high16) {  // :364-368
    totRumbleCalls_++;
    if (!active_) totRumbleInactive_++;
    rumble_.set(low16, high16, nowNs());
}

void Engine::onPcm(uint16_t controller, uint32_t sequence, const int16_t* pcm, int frames) {  // :373-397
    totCalls_++;
    arrival_.add(nowNs());
    if (!active_) {
        winInactive_++;
        totInactive_++;
        return;
    }
    const int num = apexNumber_;
    if (num >= 0 && static_cast<int>(controller) != num) {
        winForeign_++;
        totForeign_++;
        return;
    }
    totDelivered_++;
    if (haveSeq_ && sequence != lastSeq_ + 1u) {
        pipe_.resetFilter();
        winGaps_++;
    }
    haveSeq_ = true;
    lastSeq_ = sequence;
    lastPcmNs_ = nowNs();
    winPackets_++;
    if (pcm != nullptr && frames > 0) pipe_.process(pcm, static_cast<size_t>(frames) * 2, frames);
}

std::string Engine::statusLine() const {  // :403-406
    return jc::fmt("엔진=%s 활성=%s 사용자정지=%s APEX번호=%d onPcm호출=%lld 전달=%lld 비활성폐기=%lld 타컨트롤러=%lld", stateName(state_),
                   active_ ? "true" : "false", userStopped_ ? "true" : "false", apexNumber_.load(), ll(totCalls_), ll(totDelivered_),
                   ll(totInactive_), ll(totForeign_));
}

// ---------------------------------------------------------------- engine thread

int Engine::readIn(int timeoutMs) {  // bulkTransfer(epIn, ...) -> n > 0 or -1
    size_t got = 0;
    const IoResult r = port_->readFrame(rxBuf_.data(), rxBuf_.size(), &got, timeoutMs);
    return r == IoResult::Ok && got > 0 ? static_cast<int>(got) : -1;
}

int Engine::writeOut(const protocol::Frame& f, int timeoutMs) {  // bulkTransfer(epOut, ...) -> 32 or -1
    return port_->writeFrame(f.data(), timeoutMs) == IoResult::Ok ? protocol::FRAME_SIZE : -1;
}

void Engine::runBody() {  // :416-616
    bool priority = false;
    try {
        if (port_ && probeUntilReady()) {
            state_ = State::READY;
            log_.info("APEX 준비됨(GetInfo 통과). 세션 진행 + 포그라운드일 때 활성화됩니다");
            try {  // :421-424 audio-class priority (failure ignored)
                port_->onEngineThreadStart();
                priority = true;
            } catch (...) {
            }
            int64_t statsAt = nowNs();
            int64_t lastSendNs = 0;
            int zeroRun = 0;
            bool timeoutLogged = false;
            int l[8] = {0};
            int r[8] = {0};
            Scheduler::Decision dec;
            JitterBuffer::Result jr;
            while (!terminate_) {
                const bool want = state_ == State::READY && sessionRunning_ && !paused_ && !userStopped_;
                if (want && !active_) {
                    if (!activate()) break;
                    sched_.start(nowNs());
                    lastSendNs = 0;
                    zeroRun = 0;
                    timeoutLogged = false;
                    statsAt = nowNs();
                    jitter_.reset(statsAt);
                } else if (!want && active_) {
                    deactivate(currentStopReason());
                }
                if (!active_) {
                    sleepMs(15);
                    continue;
                }
                if (suspended_) {
                    // mode 3: grip disabled; PCM still received (latest 16 samples kept); signal/rumble -> re-enable
                    if (pipe_.getSilentMs() < 50 || rumble_.isActive() || (trigOn() && trig_.anyEffect())) {
                        log_.info("APEX 무음 해제: 신호 재개 -> 그립 재활성(0x53 활성 + " + std::to_string(protocol::ENABLE_WAIT_MS) +
                                  "ms 대기, 직전 샘플 보존)");
                        if (trigOn() && !sendFrame(protocol::triggerEnableBoth(), "0x53 트리거 재활성")) {
                            log_.info("APEX 트리거 재활성 전송 실패 -> 엔진 중단");
                            break;
                        }
                        trigTarget_ = protocol::ROUTE_BOTH;
                        trigRelease_ = 0;
                        if (!sendFrame(protocol::gripRealtimeEnable(), "0x53 재활성")) {
                            log_.info("APEX 재활성 전송 실패 -> 엔진 중단");
                            break;
                        }
                        sleepMs(protocol::ENABLE_WAIT_MS);
                        suspended_ = false;
                        resumeCount_++;
                        sched_.start(nowNs());
                        lastSendNs = 0;
                        zeroRun = 0;
                        jitter_.reset(nowNs());
                        trimBlockedUntilNs_ = nowNs() + 150 * kMs;
                    } else {
                        pipe_.trimTo(16);
                        sleepNs(8 * kMs);
                    }
                    continue;
                }
                // ---- one tick (8 ms slot, absolute deadline)
                int64_t now = nowNs();
                const int64_t late = now - sched_.target();
                sched_.poll(now, dec);
                if (dec.waitNs > 0) {
                    clock_.sleepUntilNs(now + dec.waitNs);  // LockSupport.parkNanos(dec.waitNs)
                    continue;
                }
                if (dec.skipped == 0 && !dec.catchingUp) {
                    sleepErrSum_ += late;
                    sleepErrMax_ = (std::max)(sleepErrMax_, late);
                    sleepErrN_++;
                }
                sched_.consume();
                const bool catchingUp = dec.catchingUp;
                now = nowNs();
                if (rumble_.checkTimeout(now)) {
                    stats_.add(SessionStats::RUMBLE_TIMEOUTS, 1);
                    log_.info("APEX rumble 타임아웃(3초 무갱신) -> 진동 정지");
                }
                if (now - lastPcmNs_ > NO_PCM_NEUTRAL_MS * kMs) {
                    if (pipe_.queueSize() > 0) pipe_.clearQueue();
                    jitter_.reset(now);
                    if (!timeoutLogged && lastPcmNs_ != 0) {
                        timeoutLogged = true;
                        log_.info("APEX: PCM 100ms 무수신 -> 중립 프레임");
                    }
                } else {
                    timeoutLogged = false;
                }
                // (Java :510 "idle" local is unused)
                const int avail = pipe_.queueSize();
                int n = 0;
                jitter_.decide(avail, now, jr);
                if (jr.trimTo >= 0 && now >= trimBlockedUntilNs_) pipe_.trimTo(jr.trimTo);
                if (jr.underrun) underruns_++;
                if (jr.play) {
                    n = pipe_.pop(l, r, 8);
                    qDepthSum_ += avail;
                    qDepthN_++;
                }
                if (n < 8) {
                    for (int i = n; i < 8; i++) {
                        l[i] = 0;
                        r[i] = 0;
                    }
                }
                if (catchingUp && n == 0 && !rumble_.isActive() && !(trigOn() && trig_.anyEffect())) {
                    continue;  // catching up with nothing to send: skip this slot
                }
                if (rumble_.mix(l, r, 8, pipe_.getCap()) > 0) rumbleFrames_++;
                bool allZero = true;
                for (int i = 0; i < 8; i++) {
                    if (l[i] != 0 || r[i] != 0) {
                        allZero = false;
                        break;
                    }
                }
                // A1-4: trigger channel (same order as ASB renderBlock/run)
                bool trigEnabled = false;
                int trigRoute = protocol::ROUTE_MUTE;
                if (trigOn()) {
                    trig_.render(now, trigOut_);
                    trigEnabled = trigOut_.enabled;
                    trigRoute = trigOut_.route;
                    if (trigEnabled && trigRoute != trigTarget_) {
                        if (!sendTriggerRoute(trigRoute)) {
                            log_.info("APEX 트리거 route 전환 전송 실패 -> 정지 시퀀스 후 엔진 중단");
                            break;
                        }
                        trigTarget_ = trigRoute;
                        trigRetargets_++;
                    }
                    if (trigEnabled) {
                        trigRelease_ = TRIGGER_RELEASE_FRAMES;
                        trigFramesWin_++;
                        stats_.add(SessionStats::TRIG_FRAMES, 1);
                    } else if (trigRelease_ > 0) {
                        trigRelease_--;
                        trigEnabled = true;  // release delay 4 frames: zero samples on Both
                        trigRoute = protocol::ROUTE_BOTH;
                    }
                    if (trigEnabled) allZero = false;
                }
                if (allZero) zeroRun++;
                else zeroRun = 0;

                if (silenceMode_ == SILENCE_SUSPEND && allZero && zeroRun > IDLE_ENTER_FRAMES && !rumble_.isActive() &&
                    !(trigOn() && trig_.anyEffect()) && pipe_.getSilentMs() >= SUSPEND_AFTER_MS) {
                    log_.info("APEX 긴 무음(" + std::to_string(SUSPEND_AFTER_MS) + "ms) -> 그립 비활성(정지 시퀀스, 세션·PCM 수신은 유지)");
                    sendStopSequence();
                    suspended_ = true;
                    suspendCount_++;
                    continue;
                }
                const bool send = !allZero || zeroRun <= IDLE_ENTER_FRAMES || lastSendNs == 0 ||
                                  (silenceMode_ != SILENCE_STOP_SENDING && (now - lastSendNs) >= keepaliveInterval_ * kMs);
                if (!send) silenceSkipped_++;
                if (send) {
                    const protocol::Frame f = trigEnabled ? protocol::realtimeFrame(l, r, trigOut_.samples, true, trigRoute)
                                                          : protocol::gripFrame(l, r);
                    const int64_t x0 = nowNs();
                    const bool ok = sendFrame(f, "0x57");
                    const int64_t xd = nowNs() - x0;
                    xferSum_ += xd;
                    xferMax_ = (std::max)(xferMax_, xd);
                    xferN_++;
                    if (!ok) {
                        log_.info("APEX 0x57 전송 실패 -> 정지 시퀀스 후 엔진 중단");
                        break;
                    }
                    lastSendNs = now;
                    sentFrames_++;
                    if (allZero) {
                        neutralFrames_++;
                        if (zeroRun > IDLE_ENTER_FRAMES) idleSent_++;
                    } else {
                        driveFrames_++;
                    }
                    if (!catchingUp) {
                        const int64_t r0 = nowNs();
                        rxPoll();
                        const int64_t rd = nowNs() - r0;
                        rxSum_ += rd;
                        rxMax_ = (std::max)(rxMax_, rd);
                        rxN_++;
                    }
                }

                if (now - statsAt >= 1000 * kMs) {
                    logStats(now - statsAt);
                    statsAt = now;
                }
            }
        }
    } catch (const std::exception& e) {  // :609-610
        log_.info(std::string("APEX 엔진 예외: ") + e.what());
    } catch (...) {
        log_.info("APEX 엔진 예외: (unknown)");
    }
    // finally :611-615
    try {
        if (active_) deactivate(terminate_ ? stopReason() : std::string("엔진 오류"));
    } catch (...) {
    }
    if (priority) {
        try {
            port_->onEngineThreadEnd();
        } catch (...) {
        }
    }
    const State s = state_;
    if (s == State::PROBING || s == State::READY) state_ = State::NONE;
    log_.info("APEX 엔진 스레드 종료");
}

std::string Engine::currentStopReason() const {  // :618-623
    const std::string reason = stopReason();
    if (!sessionRunning_) return reason.empty() ? std::string("세션 종료") : reason;
    if (paused_) return "onPause";
    if (userStopped_) return "사용자 정지";
    return reason;
}

void Engine::logStats(int64_t windowNs) {  // :630-689
    const double sec = jc::dmax(0.001, static_cast<double>(windowNs) / 1e9);
    arrival_.snapshotAndReset(nowNs(), arrSnap_);
    jitter_.adaptFromP95(arrSnap_.p95Ms, arrSnap_.packets, nowNs());
    const int packets = winPackets_.exchange(0);
    const int gaps = winGaps_.exchange(0);
    const int foreign = winForeign_.exchange(0);
    const int64_t drop = pipe_.getAndResetDropped();
    const int peak = pipe_.getAndResetPeak();
    stats_.add(SessionStats::PCM_PKTS, packets);
    stats_.add(SessionStats::PCM_GAPS, gaps);
    stats_.add(SessionStats::DROPPED, drop);
    stats_.add(SessionStats::UNDERRUNS, underruns_);
    stats_.add(SessionStats::GRIP_FRAMES, sentFrames_);
    stats_.silenceMs(arrSnap_.maxSilenceMs);
    syncExternalCounters();
    const int64_t nowStats = nowNs();
    if (stats_.minuteDue(nowStats)) log_.info(stats_.minuteLine(nowStats, sessionText(), apexText()));
    const bool perSec = log_.perSecondAllowed();
    if (packets == 0 && driveFrames_ == 0 && rumbleFrames_ == 0 && sentFrames_ < 120 && gaps == 0) {
        if (++quietTicks_ < 5) {  // quiet window: log only every 5 s
            resetWindow();
            return;
        }
    }
    quietTicks_ = 0;
    const int64_t missed = sched_.getAndResetMissed();
    const int64_t resyncs = sched_.getAndResetResyncs();
    const int64_t catchups = sched_.getAndResetCatchups();
    if (perSec) {
        log_.info(jc::fmt(
            "APEX 송신: %.0f프레임/초(구동 %lld, 중립 %lld, rumble %lld) 큐=%d 폐기=%lld 언더런=%lld PCM=%d패킷 갭리셋=%d 타컨트롤러=%d 출력peak=%d/127 | "
            "bulk(0x57) 평균/최대=%.2f/%.2fms rx폴 평균/최대=%.2f/%.2fms 슬립오차 평균/최대=%.2f/%.2fms 놓친슬롯=%lld 재동기=%lld 따라잡기=%lld "
            "rumble호출=%lld(L=%d H=%d) 지터버퍼 목표=%dms 재충전=%lld 초과폐기=%lld",
            static_cast<double>(sentFrames_) / sec, ll(driveFrames_), ll(neutralFrames_), ll(rumbleFrames_), pipe_.queueSize(), ll(drop),
            ll(underruns_), packets, gaps, foreign, peak, avgMs(xferSum_, xferN_), static_cast<double>(xferMax_) / 1e6,
            avgMs(rxSum_, rxN_), static_cast<double>(rxMax_) / 1e6, avgMs(sleepErrSum_, sleepErrN_),
            static_cast<double>(sleepErrMax_) / 1e6, ll(missed), ll(resyncs), ll(catchups), ll(totRumbleCalls_), rumble_.low8(),
            rumble_.high8(), jitter_.getTarget(), ll(jitter_.getAndResetUnderruns()), ll(jitter_.getAndResetTrims())));
    }
    const std::array<double, 7> lv = pipe_.getAndResetLevels();
    const double filterMs = (pipe_.getTaps() - 1) / 2.0 / PcmPipeline::SAMPLE_RATE * 1000.0;
    const double avgQ = qDepthN_ == 0 ? static_cast<double>(pipe_.queueSize())
                                      : static_cast<double>(qDepthSum_) / static_cast<double>(qDepthN_);
    // real delay = filter group delay + average queue depth at playout (1 sample = 1 ms) + 8 ms frame
    const double est = filterMs + avgQ + protocol::FRAME_MS;
    if (packets > 0) stats_.addExtraDelay(est);
    if (perSec) {
        log_.info(jc::fmt(
            "APEX 레벨: 리미터동작 %.1f%% 상한도달 %.1f%% 게이트닫힘 %.1f%% 입력RMS %.1f%%FS 출력RMS %.1f(상한 대비 %.0f%%) 상한=%d 이득=%.2f 게이트=%.0f%% | "
            "PCM도착간격 평균/p95/최대=%.1f/%.1f/%.1fms 최대무수신=%.1fms | 무음 모드%d: 무음중 송신 %lld 생략 %lld(keepalive %lldms) 비활성 %lld회 재활성 %lld회 | "
            "ARBS추가지연=%.1fms (필터 %.1f + 재생시점 큐 평균 %.1f + 프레임 %d) 버퍼목표=%dms%s",
            lv[1], lv[2], lv[6], lv[3], lv[4], lv[5], pipe_.getCap(), static_cast<double>(pipe_.getGain()),
            static_cast<double>(pipe_.getGateThreshold()), arrSnap_.avgMs, arrSnap_.p95Ms, arrSnap_.maxMs, arrSnap_.maxSilenceMs,
            silenceMode_.load(), ll(idleSent_), ll(silenceSkipped_), ll(keepaliveInterval_), ll(suspendCount_), ll(resumeCount_), est,
            filterMs, avgQ, protocol::FRAME_MS, jitter_.getTarget(), jitter_.getFixed() > 0 ? "(고정)" : "(자동 p95+4)"));
    }
    qDepthSum_ = 0;
    qDepthN_ = 0;
    silenceSkipped_ = 0;
    resetWindow();
    logTriggerStats(sec);
}

void Engine::logTriggerStats(double sec) {  // :692-711
    if (!trigOn()) return;
    const TriggerBridge::Stats c = trig_.stats();
    const int64_t frames = trigFramesWin_;
    trigFramesWin_ = 0;
    bool changed = frames > 0;
    for (size_t i = 0; i < c.size(); i++) {
        if (c[i] != trigPrev_[i]) changed = true;
    }
    if (!changed) return;
    if (!log_.perSecondAllowed()) {
        trigPrev_ = c;
        return;
    }
    log_.info(jc::fmt(
        "APEX 트리거: 수신 L=%lld R=%lld(적용 %lld, 해제 %lld, 중복 %lld, 미지원 %lld, 형식오류 %lld) 마지막 type L=0x%02llX R=0x%02llX | "
        "송신 트리거 프레임 %.0f/초(누계 L %lld R %lld Both %lld Mix %lld, route 전환 %lld) | 무기/활 break %lld 상한제한 샘플 %lld | 세기 %d%%",
        ll(c[0] - trigPrev_[0]), ll(c[1] - trigPrev_[1]), ll(c[2] - trigPrev_[2]), ll(c[3] - trigPrev_[3]), ll(c[6] - trigPrev_[6]),
        ll(c[4] - trigPrev_[4]), ll(c[5] - trigPrev_[5]), ll(c[15]), ll(c[16]), static_cast<double>(frames) / sec, ll(c[11]), ll(c[12]),
        ll(c[13]), ll(c[14]), ll(trigRetargets_), ll(c[8] + c[9] - trigPrev_[8] - trigPrev_[9]), ll(c[10] - trigPrev_[10]),
        trig_.getStrengthPercent()));
    trigPrev_ = c;
}

void Engine::resetWindow() {  // :717-724
    sentFrames_ = 0;
    driveFrames_ = 0;
    neutralFrames_ = 0;
    rumbleFrames_ = 0;
    underruns_ = 0;
    xferSum_ = xferMax_ = xferN_ = rxSum_ = rxMax_ = rxN_ = sleepErrSum_ = sleepErrMax_ = sleepErrN_ = 0;
}

bool Engine::probeUntilReady() {  // :727-770
    // 1) wait for the first input report (max 3 s, then send GetInfo anyway)
    const int64_t w0 = nowMs();
    if (!policy_.inputSeen()) {
        log_.info("APEX 준비: 첫 IF0 입력 리포트 대기(최대 " + std::to_string(ProbePolicy::INPUT_WAIT_MS) + "ms)");
        while (!terminate_ && !policy_.inputSeen() && nowMs() - w0 < ProbePolicy::INPUT_WAIT_MS) sleepMs(20);
        log_.info(policy_.inputSeen() ? "APEX 준비: 첫 입력 리포트 수신(" + std::to_string(nowMs() - w0) + "ms 후)"
                                      : std::string("APEX 준비: 입력 리포트 없이 대기 만료 - 그래도 GetInfo 송신"));
    }
    // 2) GetInfo retry (1 s interval, max 10). UNSUPPORTED only when all fail; afterwards wait for a retry request.
    while (!terminate_) {
        state_ = State::PROBING;
        int result;
        do {
            const int n = policy_.nextAttempt();
            if (n > 1) stats_.add(SessionStats::RETRIES, 1);
            log_.info("APEX GetInfo 시도 " + std::to_string(n) + "/" + std::to_string(ProbePolicy::MAX_ATTEMPTS));
            result = probe();
            if (result == 0) {
                policy_.onReady();
                return true;
            }
            if (result == 2) {
                policy_.onDefinitiveUnsupported();
                break;
            }
            if (terminate_) return false;
            if (!policy_.onAttemptFailed()) break;
            const int64_t s0 = nowMs();
            while (!terminate_ && nowMs() - s0 < ProbePolicy::RETRY_INTERVAL_MS) sleepMs(20);
        } while (!terminate_);
        if (terminate_) return false;
        state_ = State::UNSUPPORTED;
        log_.info("APEX 햅틱 불가(UNSUPPORTED): " + std::to_string(policy_.attempt()) +
                  "회 시도 실패. [APEX 다시 연결] 또는 동글/컨트롤러 재연결(입력 재개)로 재시도합니다");
        retryRequested_ = false;
        while (!terminate_ && !retryRequested_) sleepMs(100);
        if (terminate_) return false;
        retryRequested_ = false;
        log_.info("APEX 준비 재시도 시작(처음부터)");
    }
    return false;
}

int Engine::probe() {  // :773-816. 0 = pass, 1 = retry, 2 = definitive unsupported, 3 = terminated
    int drained = 0;
    const int64_t t0 = nowMs();
    while (drained < 128 && nowMs() - t0 < 200) {
        if (readIn(10) <= 0) break;
        drained++;
    }
    const protocol::Frame req = protocol::buildGetInfo();
    const int sent = writeOut(req, 1000);
    log_.info("APEX GetInfo 송신(IF2, 32바이트 1회): 반환 " + std::to_string(sent) + ", 큐 비우기 " + std::to_string(drained) + "건");
    if (sent != protocol::FRAME_SIZE) {
        log_.info("APEX GetInfo 전송 실패(반환 " + std::to_string(sent) + ") - 재시도 대상");
        return 1;
    }
    const int64_t start = nowMs();
    std::optional<protocol::Info> info;
    int count = 0;
    while (nowMs() - start < 600 && !info && count < 64 && !terminate_) {
        const int left = static_cast<int>((std::max)(static_cast<int64_t>(1), (std::min)(static_cast<int64_t>(100), 600 - (nowMs() - start))));
        const int n = readIn(left);
        if (n > 0) {
            count++;
            info = protocol::parse(rxBuf_.data(), static_cast<size_t>(n));
        }
    }
    if (terminate_) return 3;
    if (!info) {
        log_.info("APEX GetInfo 응답 없음/형식 불일치(수신 " + std::to_string(count) + "건) - 재시도 대상");
        return 1;
    }
    log_.info("APEX GetInfo: " + info->describe());
    if (info->checksumOk) lastProbeBattery_ = info->battery;
    if (!info->checksumOk) {
        log_.info("APEX GetInfo 체크섬 불일치 - 재시도 대상");
        return 1;
    }
    if (!info->isPro() || !info->hasGripHaptics()) {
        log_.info("APEX 햅틱 불가: Pro/그립햅틱 아님 - 더 이상 아무것도 보내지 않음");
        return 2;
    }
    // B5 D-B5-1: features 0x10 (trigger haptics) decided per device at every probe pass; grip continues either way.
    hwTrigger_ = info->hasTriggerHaptics();
    if (!hwTrigger_) {
        log_.info(jc::fmt("APEX 트리거 햅틱 없음: features=0x%02X(0x10 꺼짐) - 트리거 채널 활성·트리거 프레임 보내지 않음, 그립은 유지",
                          info->features & 0xFF));
    }
    return 0;
}

bool Engine::activate() {  // :818-854
    suspended_ = false;
    idleSent_ = 0;
    pipe_.resetFilter();
    pipe_.clearQueue();
    pipe_.getAndResetDropped();
    pipe_.getAndResetPeak();
    winPackets_ = 0;
    winGaps_ = 0;
    winForeign_ = 0;
    winInactive_ = 0;
    haveSeq_ = false;
    lastPcmNs_ = 0;
    sentFrames_ = driveFrames_ = neutralFrames_ = underruns_ = rumbleFrames_ = 0;
    xferSum_ = xferMax_ = xferN_ = rxSum_ = rxMax_ = rxN_ = sleepErrSum_ = sleepErrMax_ = sleepErrN_ = 0;
    sched_.getAndResetMissed();
    sched_.getAndResetResyncs();
    sched_.getAndResetCatchups();
    quietTicks_ = 0;
    trigTarget_ = protocol::ROUTE_BOTH;
    trigRelease_ = 0;
    trigFramesWin_ = 0;
    if (trigOn()) {
        log_.info("APEX 트리거 채널 활성: 0x53 `01 02 03 00 40` 1건(ASB buildMotorEnable 순서: 트리거 -> 그립)");
        if (!sendFrame(protocol::triggerEnableBoth(), "0x53 트리거 활성")) {
            log_.info("APEX 트리거 활성 명령 전송 실패 -> 정지 시퀀스");
            sendStopSequence();
            return false;
        }
    }
    log_.info("APEX 활성화: 0x53 그립 실시간 활성 1건 송신 후 94ms 대기");
    if (!sendFrame(protocol::gripRealtimeEnable(), "0x53 활성")) {
        log_.info("APEX 활성 명령 전송 실패 -> 정지 시퀀스");
        sendStopSequence();
        return false;
    }
    sleepMs(protocol::ENABLE_WAIT_MS);
    active_ = true;
    return true;
}

void Engine::deactivate(const std::string& reason) {  // :856-869
    log_.info("APEX 정지 시퀀스 시작: " + reason);
    suspended_ = false;
    rumble_.clear();  // no stale rumble after resume
    trig_.clear();    // A1-4: the `01 02 00` of the stop sequence also disables the trigger channel
    trigRelease_ = 0;
    if (!sessionRunning_) stats_.batteryEnd(queryBattery());  // A1-5: one GetInfo before the stop sequence (failure ignored)
    const int ok = sendStopSequence();
    active_ = false;
    log_.info("APEX 정지 완료: 전송 성공 " + std::to_string(ok) + "/5, PCM 비활성 중 수신 " + std::to_string(winInactive_.load()) + "패킷");
    if (!sessionRunning_) emitSessionSummary(reason);
    winInactive_ = 0;
    drainRx();
}

int Engine::sendStopSequence() {  // :872-880 (ASB order: neutral 0x57 x3 @8 ms -> 0x53 01 02 00 -> 01 12 00)
    int ok = 0;
    for (int i = 0; i < 3; i++) {
        if (sendFrame(protocol::neutralFrame(), "정지 중립")) ok++;
        sleepMs(protocol::FRAME_MS);
    }
    for (const protocol::Frame& f : protocol::disablePair()) {
        if (sendFrame(f, "정지 0x53")) ok++;
    }
    return ok;
}

bool Engine::sendTriggerRoute(int route) {  // :883-887 (ASB setApex6TriggerSide, Apex5Device.cpp:690-700)
    if (route == protocol::ROUTE_BOTH) return sendFrame(protocol::triggerEnableBoth(), "0x53 트리거 Both");
    for (const protocol::Frame& f : protocol::triggerSideEnable(route)) {
        if (!sendFrame(f, "0x53 트리거 한쪽")) return false;
    }
    return true;
}

bool Engine::sendFrame(const protocol::Frame& f, const char* label) {  // :889-907
    if (!protocol::isSendable(f.data(), f.size())) {
        log_.info(std::string("거부: 허용되지 않은 프레임(") + label + ")");
        return false;
    }
    try {
        const IoResult r = port_->writeFrame(f.data(), 100);
        if (r != IoResult::Ok) {
            log_.info(std::string("APEX 송신 이상(") + label + "): 반환 -1(" +
                      (r == IoResult::Timeout ? "timeout" : r == IoResult::Disconnected ? "disconnected" : "error") + ")");
            stats_.add(SessionStats::USB_ERRORS, 1);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        log_.info(std::string("APEX 송신 예외(") + label + "): " + e.what());
        stats_.add(SessionStats::USB_ERRORS, 1);
        return false;
    }
}

void Engine::rxPoll() {  // :910-921 (read IN for 1 ms after a send and drop it; first 20 logged)
    try {
        const int n = readIn(1);
        if (n > 0) {
            rxTotal_++;
            if (rxLogged_ < MAX_RX_LOG) {
                rxLogged_++;
                log_.info("APEX IF2 수신[" + std::to_string(rxLogged_) + "] len=" + std::to_string(n) + " " +
                          protocol::hex(rxBuf_.data(), static_cast<size_t>((std::min)(n, 12))));
            }
        }
    } catch (...) {
    }
}

void Engine::drainRx() {  // :923-937
    try {
        int k = 0;
        const int64_t t0 = nowMs();
        while (k < 8 && nowMs() - t0 < 100) {
            const int n = readIn(20);
            if (n <= 0) break;
            k++;
            if (rxLogged_ < MAX_RX_LOG) {
                rxLogged_++;
                log_.info("APEX IF2 수신(정지 후)[" + std::to_string(k) + "] len=" + std::to_string(n) + " " +
                          protocol::hex(rxBuf_.data(), static_cast<size_t>((std::min)(n, 12))));
            }
        }
    } catch (...) {
    }
}

}  // namespace arbs
