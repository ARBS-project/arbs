// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - APEX haptics engine (state machine). Port of ARBS-A ApexHaptics.java (f3051641) without the Android USB
// parts: USB bulkTransfer -> IApexPort, SystemClock/System.nanoTime -> IClock, ArbsLog -> ILog, daemon Thread ->
// std::thread with a 1.5 s bounded join. Line-by-line map: docs/research/B2_CORE_MAP.md.
//
// All device I/O is owned by the engine thread; other threads only flip flags (atomics) or push into locked queues.
//   active condition = READY (GetInfo passed) && session running && !paused && !user stop
//   activate   = [trigger on: 0x53 01 02 03 00 40] -> 0x53 grip enable -> 94 ms -> 0x57 every 8 ms
//   deactivate = neutral 0x57 x3 (8 ms) -> 0x53 01 02 00 -> 01 12 00
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "arbs/arrival_stats.h"
#include "arbs/jitter_buffer.h"
#include "arbs/pcm_pipeline.h"
#include "arbs/platform.h"
#include "arbs/probe_policy.h"
#include "arbs/protocol.h"
#include "arbs/rumble_synth.h"
#include "arbs/scheduler.h"
#include "arbs/session_stats.h"
#include "arbs/trigger_bridge.h"

namespace arbs {

class Engine {
public:
    enum class State { NONE, PROBING, READY, UNSUPPORTED };
    static const char* stateName(State s);

    // Silence handling (A1-3e): 1 = neutral keepalive 50 ms, 2 = keepalive 200 ms, 3 = suspend after long silence,
    // 4 = stop sending while silent.
    static constexpr int SILENCE_KEEPALIVE_50 = 1, SILENCE_KEEPALIVE_200 = 2, SILENCE_SUSPEND = 3, SILENCE_STOP_SENDING = 4;
    static constexpr int64_t SUSPEND_AFTER_MS = 2000;
    static constexpr int64_t NO_PCM_NEUTRAL_MS = 100;
    static constexpr int64_t KEEPALIVE_MS = 50;
    static constexpr int IDLE_ENTER_FRAMES = 8;
    static constexpr int MAX_RX_LOG = 20;
    static constexpr int TRIGGER_RELEASE_FRAMES = 4;

    // ARBS-A onSessionStart arguments (ApexHaptics.java:210-211) + verbose (setVerboseLog, :273). Defaults = ASB defaults.
    struct Config {
        int cap = 127;
        float gain = 1.0f;
        int jitterFixedMs = 0;
        float gatePct = 0.0f;
        int silence = SILENCE_KEEPALIVE_50;
        bool asbCurve = true;
        int taps = 321;
        bool triggerHaptics = true;
        int triggerStrength = 100;
        bool verbose = false;
    };

    // Thread: normal use (a std::thread runs runLoop). Inline: nothing is started; the caller runs runLoop() itself
    // on its own thread (deterministic tests with a fake clock). In inline mode waitInactive() does not wait.
    enum class StartMode { Thread, Inline };

    Engine(IClock& clock, ILog& log);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    State state() const { return state_.load(); }
    bool isReady() const { return state_.load() == State::READY; }
    bool isActive() const { return active_.load(); }
    bool isUserStopped() const { return userStopped_.load(); }
    void setControllerNumber(int n);

    // :98-122. [port] == nullptr means "IF2 not found" (UNSUPPORTED, nothing is sent).
    void attach(std::unique_ptr<IApexPort> port, StartMode mode = StartMode::Thread);
    // :178-200. Sends the stop sequence (if active) via the engine thread and waits up to 1.5 s.
    void detach(const std::string& reason);
    // Non-blocking half of detach (terminate flag + reason). Safe to call from the engine thread (fake-clock events).
    void requestTerminate(const std::string& reason);

    void onInputSeen();                            // :139-144 onInputReport
    std::string manualRetry();                     // :147-167
    std::string statusText() const;                // :170-175
    void onSessionStart(const Config& cfg);        // :210-239
    void onTriggerPosition(int left255, int right255);  // :242-244
    // :247-254. eventFlags 0x08 = left, 0x04 = right. [left]/[right] = 10-byte payloads.
    void onTriggerEffects(uint16_t controller, uint8_t eventFlags, uint8_t typeLeft, uint8_t typeRight, const uint8_t* left,
                          const uint8_t* right);
    void onSessionEnd(const std::string& reason);  // :264-270 = onSessionEndBegin + waitInactive(600) + onSessionEndFinish
    bool onSessionEndBegin(const std::string& reason);  // false = nothing to end (Java early return)
    void onSessionEndFinish(const std::string& reason);
    void setVerboseLog(bool v);                    // :273
    void onMoonlightRtt(int ms);                   // :276-278
    void onCrash();                                // :281-284 (no blocking: try-lock only)
    void onPause();                                // :325-329
    void onResume();                               // :331-333
    void userStop();                               // :336-340
    void userResume();                             // :342-345
    void onRumble(uint16_t low16, uint16_t high16);  // :364-368
    // :373-397. [pcm] = interleaved L,R int16, [frames] pairs (copy/convert before calling; no blocking inside).
    void onPcm(uint16_t controller, uint32_t sequence, const int16_t* pcm, int frames);
    std::string statusLine() const;                // :403-406

    // Engine thread body (:416-616). Called by the internal thread, or directly in StartMode::Inline.
    void runLoop();

    // Public parts as in ARBS-A (public final fields).
    PcmPipeline& pipe() { return pipe_; }
    RumbleSynth& rumble() { return rumble_; }
    TriggerBridge& trig() { return trig_; }
    SessionStats& stats() { return stats_; }
    const JitterBuffer& jitterForTest() const { return jitter_; }

private:
    void startThread(StartMode mode);
    void threadMain();
    void runBody();
    int readIn(int timeoutMs);
    int writeOut(const protocol::Frame& f, int timeoutMs);
    std::string stopReason() const;
    void setStopReason(const std::string& r);
    void logTriggerEvent(const std::optional<std::string>& s);
    void syncExternalCounters();
    void emitSessionSummary(const std::string& reason);
    std::string sessionText() const;
    std::string apexText() const;
    int queryBattery();
    void waitInactive(int64_t ms);
    std::string currentStopReason() const;
    void logStats(int64_t windowNs);
    void logTriggerStats(double sec);
    void resetWindow();
    bool probeUntilReady();
    int probe();
    bool activate();
    void deactivate(const std::string& reason);
    int sendStopSequence();
    bool sendTriggerRoute(int route);
    bool sendFrame(const protocol::Frame& f, const char* label);
    void rxPoll();
    void drainRx();
    int64_t nowNs() { return clock_.nowNs(); }
    int64_t nowMs() { return clock_.nowNs() / 1000000LL; }
    void sleepMs(int64_t ms) { clock_.sleepUntilNs(clock_.nowNs() + ms * 1000000LL); }
    void sleepNs(int64_t ns) { clock_.sleepUntilNs(clock_.nowNs() + ns); }
    bool engineAlive() const { return threadAlive_.load() || inlineRunning_.load(); }

    IClock& clock_;
    ILog& log_;

    PcmPipeline pipe_;
    JitterBuffer jitter_;
    ArrivalStats arrival_;
    ArrivalStats::Snapshot arrSnap_;
    RumbleSynth rumble_;
    Scheduler sched_;
    TriggerBridge trig_;
    SessionStats stats_;
    ProbePolicy policy_;

    std::atomic<bool> summaryEmitted_{true};
    std::atomic<int> lastProbeBattery_{-1};
    int attachCount_ = 0;
    int64_t rumbleBase_ = 0;
    TriggerBridge::Stats trigBase_{};
    TriggerBridge::Out trigOut_;
    std::atomic<bool> triggerOn_{false};
    // B5 D-B5-1: device features 0x10 (set by each probe pass on the engine thread). Trigger use = setting AND device.
    std::atomic<bool> hwTrigger_{true};
    bool trigOn() const { return triggerOn_ && hwTrigger_; }
    int trigTarget_ = protocol::ROUTE_BOTH, trigRelease_ = 0;
    int64_t trigRetargets_ = 0, trigFramesWin_ = 0;
    TriggerBridge::Stats trigPrev_{};
    std::atomic<int> trigEffectLogged_{0};

    std::atomic<State> state_{State::NONE};
    std::atomic<bool> sessionRunning_{false}, paused_{false}, userStopped_{false}, terminate_{false}, active_{false};
    std::atomic<int> apexNumber_{-1};
    std::atomic<int64_t> lastPcmNs_{0};
    std::atomic<bool> retryRequested_{false};
    std::atomic<int> silenceMode_{SILENCE_KEEPALIVE_50};
    std::atomic<int64_t> keepaliveInterval_{KEEPALIVE_MS};

    std::unique_ptr<IApexPort> port_;
    StartMode mode_ = StartMode::Thread;

    // threads
    std::recursive_mutex apiMu_;       // Java "synchronized" on attach/manualRetry
    std::mutex summaryMu_;             // Java "synchronized" emitSessionSummary (separate lock: see B2_CORE_MAP.md)
    mutable std::mutex reasonMu_;
    std::string stopReason_;
    std::thread thread_;
    std::thread zombie_;               // a thread that did not finish within 1.5 s (joined before the next attach)
    std::atomic<bool> threadAlive_{false};
    std::atomic<bool> inlineRunning_{false};
    std::mutex doneMu_;
    std::condition_variable doneCv_;
    bool done_ = true;

    // PCM callback thread
    std::atomic<bool> haveSeq_{false};
    std::atomic<uint32_t> lastSeq_{0};
    std::atomic<int> winPackets_{0}, winGaps_{0}, winForeign_{0}, winInactive_{0};
    std::atomic<int64_t> totCalls_{0}, totDelivered_{0}, totInactive_{0}, totForeign_{0};
    std::atomic<int64_t> totRumbleCalls_{0}, totRumbleInactive_{0};

    // engine thread only
    int64_t sentFrames_ = 0, driveFrames_ = 0, neutralFrames_ = 0, underruns_ = 0, rxTotal_ = 0, rumbleFrames_ = 0;
    int64_t xferSum_ = 0, xferMax_ = 0, xferN_ = 0, rxSum_ = 0, rxMax_ = 0, rxN_ = 0, sleepErrSum_ = 0, sleepErrMax_ = 0,
            sleepErrN_ = 0;
    int rxLogged_ = 0;
    std::array<uint8_t, 64> rxBuf_{};
    int64_t qDepthSum_ = 0, qDepthN_ = 0, silenceSkipped_ = 0, idleSent_ = 0;
    std::atomic<bool> suspended_{false};
    std::atomic<int64_t> suspendCount_{0}, resumeCount_{0};
    int64_t trimBlockedUntilNs_ = 0;
    int quietTicks_ = 0;
};

}  // namespace arbs
