// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexJitterBuffer.java (f3051641). Line references: AA:ApexJitterBuffer.java:<line>.
#include "arbs/jitter_buffer.h"

#include "arbs/java_compat.h"

#include <cmath>

namespace arbs {

void JitterBuffer::setFixed(int ms) {  // :47-50
    fixedMs_ = ms <= 0 ? 0 : jc::clampT(ms, MIN_TARGET_MS, MAX_TARGET_MS);
    if (fixedMs_ > 0) target_ = fixedMs_;
}

int64_t JitterBuffer::getAndResetUnderruns() {  // :58
    const int64_t v = underrunTotal_;
    underrunTotal_ = 0;
    return v;
}

int64_t JitterBuffer::getAndResetTrims() {  // :60
    const int64_t v = trimTotal_;
    trimTotal_ = 0;
    return v;
}

void JitterBuffer::reset(int64_t nowNs) {  // :63-66
    playing_ = false;
    lastUnderrunNs_ = nowNs;
}

void JitterBuffer::adaptFromP95(double p95Ms, int packets, int64_t nowNs) {  // :69-77
    if (fixedMs_ > 0 || packets < MIN_PACKETS_FOR_ADAPT) return;
    // (int) Math.max(MIN, Math.min(MAX, Math.ceil(p95) + 4)) - double arithmetic, then (int)
    const int desired = jc::d2i(jc::dmax(static_cast<double>(MIN_TARGET_MS),
                                         jc::dmin(static_cast<double>(MAX_TARGET_MS), std::ceil(p95Ms) + P95_MARGIN_MS)));
    if (desired > target_) {
        target_ = desired;
    } else if (desired < target_ && nowNs - lastUnderrunNs_ >= QUIET_BEFORE_DOWN_NS) {
        target_ = desired > target_ - DOWN_STEP_MS ? desired : target_ - DOWN_STEP_MS;
    }
}

JitterBuffer::Result& JitterBuffer::decide(int queue, int64_t nowNs, Result& out) {  // :80-103
    out.play = false;
    out.trimTo = -1;
    out.underrun = false;
    if (queue > target_ + OVERFLOW_MS) {
        out.trimTo = target_ + KEEP_MARGIN_MS;
        trimTotal_++;
        queue = out.trimTo;
    }
    if (!playing_) {
        if (queue >= target_) playing_ = true;
        else return out;
    }
    if (queue < FRAME) {
        playing_ = false;
        out.underrun = true;
        underrunTotal_++;
        lastUnderrunNs_ = nowNs;
        if (fixedMs_ == 0) target_ = target_ + STEP_UP_MS < MAX_TARGET_MS ? target_ + STEP_UP_MS : MAX_TARGET_MS;
        return out;
    }
    out.play = true;
    return out;
}

}  // namespace arbs
