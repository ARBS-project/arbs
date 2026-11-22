// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexScheduler.java (f3051641). Line references: AA:ApexScheduler.java:<line>.
#include "arbs/scheduler.h"

namespace arbs {

void Scheduler::start(int64_t nowNs) {  // :27-30
    t0_ = nowNs;
    slot_ = 0;
}

Scheduler::Decision& Scheduler::poll(int64_t nowNs, Decision& out) {  // :37-59
    const int64_t diff = target() - nowNs;
    out.skipped = 0;
    out.catchingUp = false;
    if (diff > 0) {
        out.waitNs = diff;
        return out;
    }
    out.waitNs = 0;
    const int64_t lagSlots = (-diff) / SLOT_NS;
    if (lagSlots > MAX_CATCHUP) {
        out.skipped = static_cast<int>(lagSlots);
        missedTotal_ += lagSlots;
        resyncTotal_++;
        t0_ = nowNs;
        slot_ = 0;
    } else if (lagSlots >= 1) {
        out.catchingUp = true;
        catchupTotal_++;
    }
    return out;
}

int64_t Scheduler::getAndResetMissed() {  // :64
    const int64_t v = missedTotal_;
    missedTotal_ = 0;
    return v;
}

int64_t Scheduler::getAndResetResyncs() {  // :66
    const int64_t v = resyncTotal_;
    resyncTotal_ = 0;
    return v;
}

int64_t Scheduler::getAndResetCatchups() {  // :68
    const int64_t v = catchupTotal_;
    catchupTotal_ = 0;
    return v;
}

}  // namespace arbs
