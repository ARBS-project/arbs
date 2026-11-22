// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - long-run counters and the "1분 요약"/"세션 요약" lines. Port of ARBS-A ArbsSessionStats.java (f3051641).
// Does not influence output; locked (Java synchronized).
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace arbs {

class SessionStats {
public:
    static constexpr int64_t PER_SECOND_LIMIT_BYTES = 20LL * 1024 * 1024;
    static constexpr int64_t MINUTE_NS = 60000000000LL;

    enum Index {
        PCM_PKTS = 0, PCM_GAPS = 1, DROPPED = 2, UNDERRUNS = 3, GRIP_FRAMES = 4, TRIG_FRAMES = 5,
        RUMBLE_CALLS = 6, RUMBLE_TIMEOUTS = 7, TRIG_APPLIED = 8, TRIG_STOPS = 9, USB_ERRORS = 10, RETRIES = 11,
        RECONNECTS = 12, N = 13
    };

    static bool perSecondAllowed(int64_t logFileBytes, bool verbose);

    void reset(int64_t nowNs);
    void add(int idx, int64_t v);
    void set(int idx, int64_t absolute);
    int64_t get(int idx) const;
    void silenceMs(double ms);
    void addExtraDelay(double ms);
    void addRtt(double ms);
    void batteryStart(int v);
    void batteryEnd(int v);
    void markCrash();
    bool minuteDue(int64_t nowNs) const;
    std::string minuteLine(int64_t nowNs, const std::string& session, const std::string& apex);
    std::string sessionLine(int64_t nowNs, const std::string& reason) const;
    // Crash path: does not block (returns false if the lock is busy).
    bool trySessionLine(int64_t nowNs, const std::string& reason, std::string& out) const;

private:
    std::string sessionLineLocked(int64_t nowNs, const std::string& reason) const;
    mutable std::mutex mu_;
    std::array<int64_t, N> cum_{};
    std::array<int64_t, N> minuteBase_{};
    int64_t startNs_ = 0, lastMinuteNs_ = 0;
    double maxSilenceMs_ = 0;
    double extraSum_ = 0, minExtraSum_ = 0;
    int64_t extraN_ = 0, minExtraN_ = 0;
    double rttSum_ = 0, minRttSum_ = 0;
    int64_t rttN_ = 0, minRttN_ = 0;
    int batteryStart_ = -1, batteryEnd_ = -1;
    bool crashed_ = false, started_ = false;
};

}  // namespace arbs
