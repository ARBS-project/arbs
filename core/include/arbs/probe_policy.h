// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - GetInfo retry policy. Port of ARBS-A ApexProbePolicy.java (f3051641). All methods are locked
// (Java synchronized): the input thread calls onInput while the engine thread runs the probe loop.
#pragma once

#include <cstdint>
#include <mutex>

namespace arbs {

class ProbePolicy {
public:
    static constexpr int MAX_ATTEMPTS = 10;
    static constexpr int64_t INPUT_WAIT_MS = 3000;
    static constexpr int64_t RETRY_INTERVAL_MS = 1000;
    static constexpr int64_t RESTART_GAP_MS = 2000;

    void begin();
    void resetAll();
    int nextAttempt();
    int attempt() const;
    bool onAttemptFailed();
    void onDefinitiveUnsupported();
    void onReady();
    bool isUnsupported() const;
    bool isReady() const;
    bool inputSeen() const;
    bool manualRetry();
    bool onInput(int64_t nowMs);

private:
    void beginLocked();
    mutable std::mutex mu_;
    int attempt_ = 0;
    bool unsupported_ = false;
    bool ready_ = false;
    int64_t lastInputMs_ = -1;
    bool inputSeen_ = false;
};

}  // namespace arbs
