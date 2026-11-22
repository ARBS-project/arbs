// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexRumbleSynth.java (f3051641). Line references: AA:ApexRumbleSynth.java:<line>.
// ASB basis: Apex6HapticBridge.cpp:84-92,931-944 (65/150 Hz, max 112).
#include "arbs/rumble_synth.h"

#include "arbs/java_compat.h"
#include "arbs/protocol.h"

#include <algorithm>
#include <cmath>

namespace arbs {

void RumbleSynth::set(int low16, int high16, int64_t nowNs) {  // :22-27
    const int lo = static_cast<int>((static_cast<uint32_t>(low16) >> 8) & 0xFF);
    const int hi = static_cast<int>((static_cast<uint32_t>(high16) >> 8) & 0xFF);
    updatedNs_.store(nowNs);
    packed_.store((lo << 8) | hi);
}

bool RumbleSynth::checkTimeout(int64_t nowNs) {  // :38-44
    if (packed_.load() != 0 && nowNs - updatedNs_.load() > TIMEOUT_NS) {
        packed_.store(0);
        return true;
    }
    return false;
}

int RumbleSynth::amplitude(int value8, int cap) {  // :50-53
    const int c = (std::max)(0, (std::min)(ASB_RUMBLE_MAX, (std::min)(protocol::MAX_AMPLITUDE, cap)));
    const int v = (std::max)(0, (std::min)(255, value8));
    return jc::l2i(jc::javaRound(v / 255.0 * c));
}

int RumbleSynth::nextLeft(int cap) { return next(true, low8(), cap); }    // :56-58

int RumbleSynth::nextRight(int cap) { return next(false, high8(), cap); }  // :60-62

int RumbleSynth::next(bool left, int v8, int cap) {  // :64-72
    double ph = left ? phaseL_ : phaseR_;
    const int amp = amplitude(v8, cap);
    const int s = jc::l2i(jc::javaRound(std::sin(ph * 2.0 * jc::kPi) * amp));
    ph += (left ? LOW_HZ : HIGH_HZ) / 1000.0;
    ph -= std::floor(ph);
    if (left) phaseL_ = ph;
    else phaseR_ = ph;
    return (std::max)(-protocol::MAX_AMPLITUDE, (std::min)(protocol::MAX_AMPLITUDE, s));
}

int RumbleSynth::mix(int* l, int* r, int count, int cap) {  // :78-94
    bool pcmL = false, pcmR = false;
    for (int i = 0; i < count; i++) {
        if (l[i] != 0) pcmL = true;
        if (r[i] != 0) pcmR = true;
    }
    int used = 0;
    if (!pcmL && low8() != 0) {
        for (int i = 0; i < count; i++) l[i] = nextLeft(cap);
        used++;
    }
    if (!pcmR && high8() != 0) {
        for (int i = 0; i < count; i++) r[i] = nextRight(cap);
        used++;
    }
    return used;
}

}  // namespace arbs
