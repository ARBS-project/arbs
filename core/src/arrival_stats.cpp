// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexArrivalStats.java (f3051641). Line references: AA:ApexArrivalStats.java:<line>.
#include "arbs/arrival_stats.h"

#include "arbs/java_compat.h"

#include <algorithm>
#include <cmath>

namespace arbs {

void ArrivalStats::add(int64_t nowNs) {  // :21-30
    std::lock_guard<std::mutex> g(mu_);
    total_++;
    if (lastNs_ != 0) {
        const int64_t gap = nowNs - lastNs_;
        sumNs_ += gap;
        if (gap > maxNs_) maxNs_ = gap;
        if (n_ < MAX_SAMPLES) gaps_[static_cast<size_t>(n_++)] = gap;
    }
    lastNs_ = nowNs;
}

ArrivalStats::Snapshot& ArrivalStats::snapshotAndReset(int64_t nowNs, Snapshot& out) {  // :33-53
    std::lock_guard<std::mutex> g(mu_);
    out.packets = total_;
    const int cnt = n_;
    out.avgMs = cnt == 0 ? 0 : static_cast<double>(sumNs_) / 1e6 / cnt;
    out.maxMs = static_cast<double>(maxNs_) / 1e6;
    const int64_t tail = lastNs_ == 0 ? 0 : (std::max)(static_cast<int64_t>(0), nowNs - lastNs_);
    out.maxSilenceMs = static_cast<double>((std::max)(maxNs_, tail)) / 1e6;
    if (cnt == 0) {
        out.p95Ms = 0;
    } else {
        std::array<int64_t, MAX_SAMPLES> c = gaps_;
        std::sort(c.begin(), c.begin() + cnt);
        // (int) Math.min(cnt - 1, Math.ceil(cnt * 0.95) - 1): double min, then (int)
        const int idx = jc::d2i(jc::dmin(static_cast<double>(cnt - 1), std::ceil(cnt * 0.95) - 1));
        out.p95Ms = static_cast<double>(c[static_cast<size_t>((std::max)(0, idx))]) / 1e6;
    }
    n_ = 0;
    sumNs_ = 0;
    maxNs_ = 0;
    total_ = 0;
    return out;
}

void ArrivalStats::reset() {  // :56-63
    std::lock_guard<std::mutex> g(mu_);
    n_ = 0;
    sumNs_ = 0;
    maxNs_ = 0;
    total_ = 0;
    lastNs_ = 0;
}

}  // namespace arbs
