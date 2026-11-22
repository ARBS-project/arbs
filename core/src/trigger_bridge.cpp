// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexTriggerBridge.java (f3051641). Line references: AA:ApexTriggerBridge.java:<line>.
#include "arbs/trigger_bridge.h"

#include "arbs/java_compat.h"

#include <algorithm>
#include <limits>

namespace arbs {

using Lock = std::lock_guard<std::mutex>;
using Type = TriggerEffect::Type;

void TriggerBridge::setStrengthPercent(int p) {  // :42
    Lock g(mu_);
    strengthPercent_ = (std::max)(0, (std::min)(100, p));
}

void TriggerBridge::setCeilingPercent(int p) {  // :44
    Lock g(mu_);
    ceilingPercent_ = (std::max)(20, (std::min)(100, p));
}

int TriggerBridge::getStrengthPercent() const {  // :46
    Lock g(mu_);
    return strengthPercent_;
}

void TriggerBridge::setPosition(int left, int right) {  // :48-51
    Lock g(mu_);
    pos_[0] = left & 0xFF;
    pos_[1] = right & 0xFF;
}

TriggerBridge::Stats TriggerBridge::stats() const {  // :55-58
    Lock g(mu_);
    return Stats{updatesL_, updatesR_, activeUpdates_, stops_, unsupported_, malformed_, dedup_, 0 /* [7] reserved */,
                 weaponBreaks_, bowBreaks_, limitedSamples_, framesL_, framesR_, framesBoth_, mixFrames_, lastTypeL_, lastTypeR_};
}

bool TriggerBridge::anyEffect() const {  // :60
    Lock g(mu_);
    return !eff_[0].isOff() || !eff_[1].isOff() || pendingClear_[0] || pendingClear_[1];
}

void TriggerBridge::clear() {  // :63-76
    Lock g(mu_);
    for (size_t i = 0; i < 2; i++) {
        eff_[i] = TriggerEffect();
        last_[i] = TriggerEffect();
        st_[i].reset();
        pendingClear_[i] = false;
        feedbackSeen_[i] = false;
        gen_[i] = genSeen_[i] = 0;
    }
    sampleOffset_ = 0;
    leftSeen_ = rightSeen_ = leftSince_ = rightSince_ = lastChange_ = dualBlock_ = 0;
    owner_ = preempted_ = MUTE;
    episode_ = false;
}

std::optional<std::string> TriggerBridge::onEffect(bool left, int type, const uint8_t* payload, int64_t nowNs) {  // :79-128
    Lock g(mu_);
    const size_t s = left ? 0 : 1;
    if (left) {
        updatesL_++;
        lastTypeL_ = type & 0xFF;
    } else {
        updatesR_++;
        lastTypeR_ = type & 0xFF;
    }
    TriggerEffect::Decoded dec = TriggerEffect::decode(type, payload);
    TriggerEffect& next = dec.effect;
    if (dec.code == TriggerEffect::OK && next.type == Type::FEEDBACK) {
        feedbackSeen_[s] = true;
        feedbackSeenAt_[s] = nowNs;
    } else if (dec.code == TriggerEffect::OK && next.type == Type::VIBRATION) {
        if (feedbackSeen_[s] && nowNs - feedbackSeenAt_[s] < TriggerEffect::DETENT_FEEDBACK_WINDOW_MS * 1000000LL) next.detent = true;
    }
    const char* side = left ? "L" : "R";
    if (dec.code != TriggerEffect::OK) {
        if (dec.code == TriggerEffect::UNSUPPORTED) unsupported_++;
        else malformed_++;
    }
    const TriggerEffect target = eff_[s];
    if (dec.code != TriggerEffect::OK) {
        eff_[s] = TriggerEffect();
        pendingClear_[s] = false;
        noteChange(static_cast<int>(s));
        st_[s].retarget();
        return std::string("트리거 효과 ") + (dec.code == TriggerEffect::UNSUPPORTED ? "미지원(ARBS 미이식)" : "형식 오류") + "(" +
               side + ", type=0x" + jc::fmt("%02X", type & 0xFF) + ") -> Off 처리";
    }
    if (next.isOff() && !target.isOff()) {
        pendingClear_[s] = true;
        pendingClearAt_[s] = nowNs + CLEAR_HOLD_MS * 1000000LL;
        stops_++;
        return std::string("트리거 효과 해제 예약(") + side + ", " + std::to_string(CLEAR_HOLD_MS) + "ms 후)";
    }
    pendingClear_[s] = false;
    if (next.same(target)) {
        dedup_++;
        return std::nullopt;
    }
    eff_[s] = next;
    noteChange(static_cast<int>(s));
    st_[s].retarget();
    st_[s].setDetentPending(next.detent);
    if (next.isOff()) stops_++;
    else activeUpdates_++;
    return std::string("트리거 효과 적용(") + side + "): " + next.describe();
}

void TriggerBridge::noteChange(int si) {  // :130-136
    const size_t s = static_cast<size_t>(si);
    const TriggerEffect& e = eff_[s];
    if (e.isOff()) return;
    if (e.same(last_[s])) return;
    if (e.type != last_[s].type) gen_[s]++;
    last_[s] = e;
}

namespace {

int64_t since(int64_t seen, int64_t stamp) {  // :266-268
    return seen == 0 || seen > stamp ? (std::numeric_limits<int64_t>::max)() : stamp - seen;
}

}  // namespace

void TriggerBridge::render(int64_t nowNs, Out& out) {  // :150-257
    Lock g(mu_);
    out.enabled = false;
    out.route = MUTE;
    std::fill(out.samples, out.samples + 8, 0);
    for (size_t s = 0; s < 2; s++) {
        if (pendingClear_[s] && nowNs >= pendingClearAt_[s]) {
            pendingClear_[s] = false;
            eff_[s] = TriggerEffect();
            noteChange(static_cast<int>(s));
            st_[s].retarget();
        }
    }
    int r[2][8];
    const int64_t bl = st_[0].burstsStarted, br = st_[1].burstsStarted;
    TriggerEffect::render(eff_[0], pos_[0], st_[0], sampleOffset_, r[0]);
    TriggerEffect::render(eff_[1], pos_[1], st_[1], sampleOffset_, r[1]);
    sampleOffset_ += 8;
    const bool leftBurstFrame = st_[0].burstActive(), rightBurstFrame = st_[1].burstActive();
    if (st_[0].burstsStarted > bl) {
        if (eff_[0].type == Type::BOW) bowBreaks_++;
        else weaponBreaks_++;
    }
    if (st_[1].burstsStarted > br) {
        if (eff_[1].type == Type::BOW) bowBreaks_++;
        else weaponBreaks_++;
    }
    const bool newL = genSeen_[0] != gen_[0], newR = genSeen_[1] != gen_[1];
    genSeen_[0] = gen_[0];
    genSeen_[1] = gen_[1];
    bool leftActive = false, rightActive = false, identical = true;
    for (int i = 0; i < 8; i++) {
        const int a = TriggerEffect::scaleStrength(r[0][i], strengthPercent_);
        const int b = TriggerEffect::scaleStrength(r[1][i], strengthPercent_);
        const int la = TriggerEffect::limitCeiling(a, ceilingPercent_);
        const int lb = TriggerEffect::limitCeiling(b, ceilingPercent_);
        if (la != a) limitedSamples_++;
        if (lb != b) limitedSamples_++;
        r[0][i] = la;
        r[1][i] = lb;
        if (la != 0) leftActive = true;
        if (lb != 0) rightActive = true;
        if (la != lb) identical = false;
    }
    identical = leftActive && rightActive && identical;
    // ---- arbitration (Mix)
    const int64_t stamp = ++dualBlock_;
    const bool leftBurst = leftBurstFrame && leftActive, rightBurst = rightBurstFrame && rightActive;
    if (leftActive) {
        if (since(leftSeen_, stamp) > ENTRY_WINDOW) leftSince_ = stamp;
        leftSeen_ = stamp;
    }
    if (rightActive) {
        if (since(rightSeen_, stamp) > ENTRY_WINDOW) rightSince_ = stamp;
        rightSeen_ = stamp;
    }
    if (episode_ && (identical || since(leftSeen_, stamp) > MIX_RELEASE || since(rightSeen_, stamp) > MIX_RELEASE)) {
        episode_ = false;
        preempted_ = MUTE;
    }
    if (!episode_) {
        if (!identical && (leftActive || rightActive) && since(leftSeen_, stamp) <= ENTRY_WINDOW &&
            since(rightSeen_, stamp) <= ENTRY_WINDOW) {
            episode_ = true;
            int chosen = leftSince_ > rightSince_ ? L : R;
            const bool held = (owner_ == L || owner_ == R) && (owner_ == L ? leftActive : rightActive) &&
                              since(lastChange_, stamp) < OWNER_HOLD;
            if (leftBurst != rightBurst) {
                chosen = leftBurst ? L : R;
                preempted_ = chosen == L ? R : L;
            } else if (newL != newR) {
                chosen = newL ? L : R;
            } else if (held) {
                chosen = owner_;
            }
            changeOwner(chosen, stamp);
        }
    } else {
        const int other = owner_ == L ? R : L;
        const bool ownerBurst = owner_ == L ? leftBurst : rightBurst;
        const bool otherBurst = other == L ? leftBurst : rightBurst;
        const bool otherActive = other == L ? leftActive : rightActive;
        const bool otherNew = other == L ? newL : newR;
        const int64_t ownerSeen = owner_ == L ? since(leftSeen_, stamp) : since(rightSeen_, stamp);
        if (otherBurst && otherActive && !ownerBurst) {
            if (preempted_ == MUTE) preempted_ = owner_;
            changeOwner(other, stamp);
        } else if (preempted_ != MUTE && !ownerBurst) {
            const int back = preempted_;
            preempted_ = MUTE;
            changeOwner(back, stamp);
        } else if (otherNew && otherActive && !ownerBurst) {
            changeOwner(other, stamp);
        } else if (!ownerBurst && otherActive && ownerSeen > YIELD_SILENT && since(lastChange_, stamp) >= OWNER_HOLD) {
            changeOwner(other, stamp);
        }
    }
    int route = MUTE;
    if (episode_ || identical) route = BOTH;
    else if (rightActive && !leftActive) route = R;
    else if (leftActive) route = L;
    out.enabled = leftActive || rightActive;
    if (!out.enabled) return;
    if (episode_ && !identical) {
        route = BOTH;
        for (int i = 0; i < 8; i++) out.samples[i] = (std::max)(-MIX_PEAK, (std::min)(MIX_PEAK, r[0][i] + r[1][i]));
        mixFrames_++;
    } else if (route == BOTH) {
        std::copy(r[0], r[0] + 8, out.samples);
        framesBoth_++;
    } else {
        const int* src = route == R ? r[1] : r[0];
        std::copy(src, src + 8, out.samples);
        if (route == R) framesR_++;
        else framesL_++;
    }
    out.route = route;
}

void TriggerBridge::changeOwner(int side, int64_t stamp) {  // :259-264
    if (owner_ != side) {
        owner_ = side;
        lastChange_ = stamp;
    }
}

}  // namespace arbs
