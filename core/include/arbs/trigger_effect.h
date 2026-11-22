// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - DualSense trigger effect -> APEX trigger channel samples (ASB Mix-path realtime samples).
// Port of ARBS-A ApexTriggerEffect.java (f3051641); ASB basis Apex6TriggerEffect.cpp:237-336 (decode), :338-495 (render),
// Apex6TriggerEffect.h:30-73 (constants). 0x23/0x27 periodic and legacy effects are reported as unsupported.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace arbs {

struct TriggerDecoded;

struct TriggerEffect {
    enum class Type { OFF = 0, FEEDBACK = 1, VIBRATION = 2, WEAPON = 3, BOW = 4, RHYTHM = 5 };
    static constexpr int OK = 0, UNSUPPORTED = 1, INVALID = 2;

    static constexpr int MAX_DRIVE = 127;
    static constexpr int NATIVE_MAX_DRIVE = 96;
    static constexpr double CARRIER_HZ = 85.0;
    static constexpr double BURST_HZ = 95.0;
    static constexpr int PULSE_TRAIN_MAX_HZ = 30;
    static constexpr int DETENT_BASE_DRIVE = 56, DETENT_PER_STRENGTH = 8, DETENT_MAX_DRIVE = 100;
    static constexpr int DETENT_HOLD = 14, DETENT_RELEASE = 10;
    static constexpr double DETENT_REARM_MARGIN = 20.0, DETENT_MIN_POSITION = 10.0, CONTINUOUS_RELEASE_POSITION = 8.0;
    static constexpr int64_t DETENT_FEEDBACK_WINDOW_MS = 500;
    static constexpr int PULSE_TRAIN_BASE_DRIVE = 32;
    static constexpr double PULSE_TRAIN_DRIVE_STEP = 19.0;
    static constexpr int BURST_ATTACK = 1, BURST_HOLD = 40, BURST_RELEASE = 10;
    static constexpr int BURST_MIN_DRIVE = 70, BURST_MAX_DRIVE = 127, BURST_FULL_FORCE = 48, FORCE_PER_STRENGTH = 8;
    static constexpr double FEEDBACK_DRIVE_PER_LEVEL = 8.0, VIBRATION_DRIVE_PER_LEVEL = 10.0, BOW_DRIVE_PER_LEVEL = 6.0;
    static constexpr int REARM_MARGIN = 4, RHYTHM_RELEASE_MARGIN = 6;

    Type type = Type::OFF;
    std::array<int, 10> zoneStrengths{};
    int startZone = 0, endZone = 0, strength = 0, frequency = 0, snapStrength = 0;
    bool detent = false;
    int rhythmDrive = 0, rhythmPeriod = 0, rhythmFrom = 0, rhythmTo = 255;
    int pulseAttack = BURST_ATTACK, pulseHold = BURST_HOLD, pulseRelease = BURST_RELEASE;

    bool same(const TriggerEffect& o) const;
    bool isOff() const { return type == Type::OFF; }
    std::string describe() const;
    int maxStrength() const;

    using Decoded = TriggerDecoded;  // {effect, code}; defined after this struct (needs the complete type)
    // [payload] = 10 bytes from Moonlight (nullptr allowed = zeros).
    static Decoded decode(int type, const uint8_t* payload);

    // Render state of one trigger (Apex6TriggerRenderState).
    struct State {
        double phase = 0, amplitude = 0;
        int burstAttack = BURST_ATTACK, burstHold = BURST_HOLD, burstRelease = BURST_RELEASE;
        int64_t burstsStarted = 0;
        int trainElapsed = 0;
        int burstElapsed = 0, burstLength = 0;
        int previousPosition = 0, burstDrive = 0;
        bool hasPreviousPosition = false, hasAmplitude = false, armed = true, burstInFrame = false, trainActive = false,
             detentPending = false;
        void reset();
        void retarget();
        void setDetentPending(bool v) { detentPending = v; }
        bool burstActive() const { return burstInFrame || burstLength != 0; }
    };

    static int positionFor(int zone);
    static double clamp(double v, double lo, double hi);
    static double zoneLevel(const std::array<int, 10>& strengths, double position);
    static int burstDriveForForce(int force);
    static double shapeValue(int attack, int hold, int release, double hz, int index);
    // 8 samples (1 kHz, 8 ms). [sampleOffset] < 0 = continuous phase not anchored.
    static void render(const TriggerEffect& e, int position, State& s, int64_t sampleOffset, int out[8]);
    static int scaleStrength(int sample, int percent);
    static int limitCeiling(int sample, int ceilingPercent);
};

struct TriggerDecoded {
    TriggerEffect effect;
    int code = TriggerEffect::OK;
};

}  // namespace arbs
