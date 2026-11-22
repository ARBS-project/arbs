// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - trigger effect reception -> 8 ms trigger sample blocks + route (ASB Mix policy).
// Port of ARBS-A ApexTriggerBridge.java (f3051641); ASB basis Apex6HapticBridge.cpp handleTrigger :616-685,
// renderBlock :782-1049, Apex6DualArbiter::next :253-352. No no-update release (A1-5): release only by Off + 100 ms or clear().
// Called from the control thread, the input thread and the engine thread: all public methods are locked.
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "arbs/protocol.h"
#include "arbs/trigger_effect.h"

namespace arbs {

class TriggerBridge {
public:
    static constexpr int64_t CLEAR_HOLD_MS = 100;
    static constexpr int ENTRY_WINDOW = 3, YIELD_SILENT = 8, OWNER_HOLD = 32, MIX_RELEASE = 63, MIX_PEAK = 127;
    static constexpr int L = protocol::ROUTE_LEFT, R = protocol::ROUTE_RIGHT, BOTH = protocol::ROUTE_BOTH,
                         MUTE = protocol::ROUTE_MUTE;

    struct Out {
        bool enabled = false;
        int route = MUTE;
        int samples[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    };

    using Stats = std::array<int64_t, 17>;

    void setStrengthPercent(int p);
    void setCeilingPercent(int p);
    int getStrengthPercent() const;
    void setPosition(int left, int right);
    // [0]=rx L [1]=rx R [2]=applied [3]=stops [4]=unsupported [5]=malformed [6]=dedup [7]=reserved(0) [8]=weapon breaks
    // [9]=bow breaks [10]=limited samples [11..14]=frames L/R/Both/Mix [15,16]=last type L/R
    Stats stats() const;
    bool anyEffect() const;
    void clear();
    // Returns a log line, or nullopt for a deduplicated effect (Java null). [payload] = 10 bytes (nullptr = zeros).
    std::optional<std::string> onEffect(bool left, int type, const uint8_t* payload, int64_t nowNs);
    void render(int64_t nowNs, Out& out);

private:
    void noteChange(int s);
    void changeOwner(int side, int64_t stamp);

    mutable std::mutex mu_;
    std::array<TriggerEffect, 2> eff_{};
    std::array<TriggerEffect, 2> last_{};
    std::array<TriggerEffect::State, 2> st_{};
    std::array<int, 2> pos_{};
    std::array<bool, 2> pendingClear_{};
    std::array<int64_t, 2> pendingClearAt_{};
    std::array<bool, 2> feedbackSeen_{};
    std::array<int64_t, 2> feedbackSeenAt_{};
    std::array<int64_t, 2> gen_{}, genSeen_{};
    int64_t sampleOffset_ = 0;
    int strengthPercent_ = 100;
    int ceilingPercent_ = 100;
    int64_t leftSeen_ = 0, rightSeen_ = 0, leftSince_ = 0, rightSince_ = 0, lastChange_ = 0, dualBlock_ = 0;
    int owner_ = MUTE, preempted_ = MUTE;
    bool episode_ = false;
    int64_t updatesL_ = 0, updatesR_ = 0, activeUpdates_ = 0, stops_ = 0, unsupported_ = 0, malformed_ = 0, dedup_ = 0,
            weaponBreaks_ = 0, bowBreaks_ = 0;
    int64_t limitedSamples_ = 0, framesL_ = 0, framesR_ = 0, framesBoth_ = 0, mixFrames_ = 0;
    int lastTypeL_ = 0, lastTypeR_ = 0;
};

}  // namespace arbs
