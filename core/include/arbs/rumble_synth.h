// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - rumble -> grip sine (low = left 65 Hz, high = right 150 Hz, 1 ms per sample, 3 s no-update timeout).
// Port of ARBS-A ApexRumbleSynth.java (f3051641). set() runs on the control thread, the rest on the engine thread
// (Java volatile -> std::atomic).
#pragma once

#include <atomic>
#include <cstdint>

namespace arbs {

class RumbleSynth {
public:
    static constexpr double LOW_HZ = 65.0;
    static constexpr double HIGH_HZ = 150.0;
    static constexpr int64_t TIMEOUT_NS = 3000000000LL;
    static constexpr int ASB_RUMBLE_MAX = 112;

    void set(int low16, int high16, int64_t nowNs);
    void clear() { packed_.store(0); }
    int low8() const { return (packed_.load() >> 8) & 0xFF; }
    int high8() const { return packed_.load() & 0xFF; }
    bool isActive() const { return packed_.load() != 0; }
    bool checkTimeout(int64_t nowNs);
    static int amplitude(int value8, int cap);
    int nextLeft(int cap);
    int nextRight(int cap);
    // Per channel: PCM wins if non-zero, otherwise the rumble sine. Returns channels that used rumble (0..2).
    int mix(int* l, int* r, int count, int cap);

private:
    int next(bool left, int v8, int cap);
    std::atomic<int> packed_{0};          // (low8 << 8) | high8
    std::atomic<int64_t> updatedNs_{0};
    double phaseL_ = 0, phaseR_ = 0;      // engine thread only
};

}  // namespace arbs
