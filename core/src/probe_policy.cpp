// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexProbePolicy.java (f3051641). Line references: AA:ApexProbePolicy.java:<line>.
#include "arbs/probe_policy.h"

namespace arbs {

using Lock = std::lock_guard<std::mutex>;

void ProbePolicy::beginLocked() {  // :22-26
    attempt_ = 0;
    unsupported_ = false;
    ready_ = false;
}

void ProbePolicy::begin() {
    Lock g(mu_);
    beginLocked();
}

void ProbePolicy::resetAll() {  // :29-33
    Lock g(mu_);
    beginLocked();
    lastInputMs_ = -1;
    inputSeen_ = false;
}

int ProbePolicy::nextAttempt() {  // :36
    Lock g(mu_);
    return ++attempt_;
}

int ProbePolicy::attempt() const {  // :38
    Lock g(mu_);
    return attempt_;
}

bool ProbePolicy::onAttemptFailed() {  // :41-47
    Lock g(mu_);
    if (attempt_ >= MAX_ATTEMPTS) {
        unsupported_ = true;
        return false;
    }
    return true;
}

void ProbePolicy::onDefinitiveUnsupported() {  // :50
    Lock g(mu_);
    unsupported_ = true;
}

void ProbePolicy::onReady() {  // :52-55
    Lock g(mu_);
    ready_ = true;
    unsupported_ = false;
}

bool ProbePolicy::isUnsupported() const {  // :57
    Lock g(mu_);
    return unsupported_;
}

bool ProbePolicy::isReady() const {  // :59
    Lock g(mu_);
    return ready_;
}

bool ProbePolicy::inputSeen() const {  // :61
    Lock g(mu_);
    return inputSeen_;
}

bool ProbePolicy::manualRetry() {  // :64-68
    Lock g(mu_);
    if (!unsupported_) return false;
    beginLocked();
    return true;
}

bool ProbePolicy::onInput(int64_t nowMs) {  // :71-77
    Lock g(mu_);
    const bool restart = unsupported_ && lastInputMs_ >= 0 && nowMs - lastInputMs_ >= RESTART_GAP_MS;
    lastInputMs_ = nowMs;
    inputSeen_ = true;
    if (restart) beginLocked();
    return restart;
}

}  // namespace arbs
