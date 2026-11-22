// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - 48 kHz S16LE stereo PCM -> low-pass (~400 Hz, Hamming sinc FIR) + 48:1 decimation -> 1 kHz grip samples.
// Port of ARBS-A ApexPcmPipeline.java (f3051641). All public methods are locked (Java synchronized: PCM callback
// thread <-> engine thread). Float accumulation order is kept (x64 MSVC /fp:precise, SSE2, no FMA contraction).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace arbs {

class PcmPipeline {
public:
    static constexpr int DECIMATION = 48;
    static constexpr int TAP_CHOICES[3] = {161, 321, 641};
    static constexpr int TAPS = 321;
    static constexpr int MAX_TAPS = 641;
    static constexpr double CUTOFF_HZ = 400.0;
    static constexpr double SAMPLE_RATE = 48000.0;
    static constexpr int QUEUE_LIMIT = 24;
    static constexpr int DEFAULT_CAP = 40;
    static constexpr int CAPACITY = 128;
    static constexpr double GATE_HOLD_MS = 50.0;
    static constexpr int GATE_FADE_OUTPUTS = 10;
    static constexpr double SILENCE_PCT = 0.3;
    static constexpr double KNEE = 0.8;

    // Hamming-window sinc design (double, then float). Not cached.
    static std::vector<float> design(int taps);
    // Cached coefficients for 161/321/641; any other value -> 321 (Java designFor).
    static const std::vector<float>& designFor(int taps);

    void setGain(float g);
    float getGain() const;
    void setTaps(int n);
    int getTaps() const;
    void setAsbCurve(bool on);
    bool isAsbCurve() const;
    void setCap(int c);
    int getCap() const;
    void resetFilter();
    void setGateThreshold(float pctFs);
    float getGateThreshold() const;
    double getSilentMs() const;
    void setLimit(int n);
    int trimTo(int keep);
    void clearQueue();
    // [pcm] = interleaved L,R S16 samples, [pcmLen] = number of int16 values, [frames] = requested frame pairs.
    int process(const int16_t* pcm, size_t pcmLen, int frames);
    int scale(float y);
    // [samples, limiter %, cap reached %, input RMS %FS, output RMS, output RMS % of cap, gate closed %], then reset.
    std::array<double, 7> getAndResetLevels();
    int queueSize() const;
    int pop(int* l, int* r, int max);
    int64_t getAndResetDropped();
    int getAndResetPeak();

    // TEST ONLY (B2_DESIGN.md 3-4 #4 "coefficient injection"): use the given coefficients (e.g. Java's float bits from
    // golden pcm_coeffs.txt) instead of designFor(). Length must be 1..MAX_TAPS. Resets the filter like setTaps().
    void setCoefficientsForTest(const std::vector<float>& h);

private:
    void resetFilterLocked();
    void gateUpdate(const int16_t* pcm, int frames);
    int scaleLocked(float y);
    void push(int l, int r);

    mutable std::mutex mu_;
    std::array<float, MAX_TAPS * 2> bufL_{};
    std::array<float, MAX_TAPS * 2> bufR_{};
    int taps_ = TAPS;
    const float* hh_ = designFor(TAPS).data();
    std::vector<float> injected_;
    int pos_ = 0;
    int phase_ = 0;
    float gain_ = 1.0f;
    int cap_ = DEFAULT_CAP;
    std::array<int, CAPACITY> qL_{};
    std::array<int, CAPACITY> qR_{};
    int limit_ = QUEUE_LIMIT;
    int head_ = 0, size_ = 0;
    int64_t dropped_ = 0;
    int peak_ = 0;
    bool asbCurve_ = true;
    float gateThresholdPct_ = 0.0f;
    double gateQuietMsL_ = 0, gateQuietMsR_ = 0;
    bool gateClosedL_ = false, gateClosedR_ = false;
    float gateGainL_ = 1.0f, gateGainR_ = 1.0f;
    int64_t stGateOut_ = 0, stGateClosed_ = 0;
    double silentMs_ = 0;
    int64_t stN_ = 0, stLimited_ = 0, stClipped_ = 0;
    double stInSq_ = 0, stOutSq_ = 0;
};

}  // namespace arbs
