// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - PCM packet arrival gap statistics. Port of ARBS-A ApexArrivalStats.java (f3051641).
// add() runs on the PCM callback thread, snapshotAndReset() on the engine thread (locked, Java synchronized).
#pragma once

#include <array>
#include <cstdint>
#include <mutex>

namespace arbs {

class ArrivalStats {
public:
    static constexpr int MAX_SAMPLES = 1024;

    struct Snapshot {
        int packets = 0;
        double avgMs = 0, p95Ms = 0, maxMs = 0, maxSilenceMs = 0;
    };

    void add(int64_t nowNs);
    Snapshot& snapshotAndReset(int64_t nowNs, Snapshot& out);
    void reset();

private:
    std::mutex mu_;
    std::array<int64_t, MAX_SAMPLES> gaps_{};
    int n_ = 0;
    int64_t lastNs_ = 0;  // 0 = none
    int64_t sumNs_ = 0, maxNs_ = 0;
    int total_ = 0;
};

}  // namespace arbs
