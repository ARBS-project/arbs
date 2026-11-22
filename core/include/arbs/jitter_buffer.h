// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - adaptive playout (jitter) buffer. Port of ARBS-A ApexJitterBuffer.java (f3051641). Engine thread only.
// 1 sample = 1 ms, frame = 8 samples. Auto target = p95 arrival gap + 4 ms (12..80), fixed delay optional.
#pragma once

#include <cstdint>

namespace arbs {

class JitterBuffer {
public:
    static constexpr int MIN_TARGET_MS = 12;
    static constexpr int MAX_TARGET_MS = 80;
    static constexpr int INITIAL_TARGET_MS = 24;
    static constexpr int STEP_UP_MS = 4;
    static constexpr int OVERFLOW_MS = 8;
    static constexpr int KEEP_MARGIN_MS = 2;
    static constexpr int P95_MARGIN_MS = 4;
    static constexpr int DOWN_STEP_MS = 2;
    static constexpr int64_t QUIET_BEFORE_DOWN_NS = 2000000000LL;
    static constexpr int MIN_PACKETS_FOR_ADAPT = 20;
    static constexpr int FRAME = 8;
    static constexpr int REQUIRED_QUEUE = MAX_TARGET_MS + OVERFLOW_MS + FRAME;  // 96

    struct Result {
        bool play = false;   // true: pop FRAME samples and send; false: silent frame
        int trimTo = -1;     // >=0: trim the queue to this many samples first
        bool underrun = false;
    };

    void setFixed(int ms);
    int getFixed() const { return fixedMs_; }
    int getTarget() const { return target_; }
    bool isPlaying() const { return playing_; }
    int64_t getAndResetUnderruns();
    int64_t getAndResetTrims();
    void reset(int64_t nowNs);
    void adaptFromP95(double p95Ms, int packets, int64_t nowNs);
    Result& decide(int queue, int64_t nowNs, Result& out);

private:
    int target_ = INITIAL_TARGET_MS;
    int fixedMs_ = 0;
    bool playing_ = false;
    int64_t lastUnderrunNs_ = 0;
    int64_t underrunTotal_ = 0;
    int64_t trimTotal_ = 0;
};

}  // namespace arbs
