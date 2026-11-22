// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - 8 ms slot absolute-deadline scheduler. Port of ARBS-A ApexScheduler.java (f3051641). Engine thread only.
#pragma once

#include <cstdint>

namespace arbs {

class Scheduler {
public:
    static constexpr int64_t SLOT_NS = 8LL * 1000000LL;  // ApexProtocol.FRAME_MS * 1e6
    static constexpr int MAX_CATCHUP = 3;

    struct Decision {
        int64_t waitNs = 0;   // >0: sleep then poll again; 0: consume() and send
        int skipped = 0;      // slots skipped by a resync in this call
        bool catchingUp = false;
    };

    void start(int64_t nowNs);
    int64_t target() const { return t0_ + slot_ * SLOT_NS; }
    Decision& poll(int64_t nowNs, Decision& out);
    void consume() { slot_++; }
    int64_t getAndResetMissed();
    int64_t getAndResetResyncs();
    int64_t getAndResetCatchups();

private:
    int64_t t0_ = 0;
    int64_t slot_ = 0;
    int64_t missedTotal_ = 0;
    int64_t resyncTotal_ = 0;
    int64_t catchupTotal_ = 0;
};

}  // namespace arbs
