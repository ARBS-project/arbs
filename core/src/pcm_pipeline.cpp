// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexPcmPipeline.java (f3051641). Line references: AA:ApexPcmPipeline.java:<line>.
#include "arbs/pcm_pipeline.h"

#include "arbs/java_compat.h"
#include "arbs/protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace arbs {

using Lock = std::lock_guard<std::mutex>;

std::vector<float> PcmPipeline::design(int taps) {  // :56-71
    std::vector<float> h(static_cast<size_t>(taps));
    const int m = (taps - 1) / 2;
    const double wc = 2.0 * jc::kPi * CUTOFF_HZ / SAMPLE_RATE;
    double sum = 0;
    std::vector<double> t(static_cast<size_t>(taps));
    for (int i = 0; i < taps; i++) {
        const int n = i - m;
        const double ideal = n == 0 ? wc / jc::kPi : std::sin(wc * n) / (jc::kPi * n);
        const double w = 0.54 - 0.46 * std::cos(2.0 * jc::kPi * i / (taps - 1));
        t[static_cast<size_t>(i)] = ideal * w;
        sum += t[static_cast<size_t>(i)];
    }
    for (int i = 0; i < taps; i++) h[static_cast<size_t>(i)] = static_cast<float>(t[static_cast<size_t>(i)] / sum);
    return h;
}

const std::vector<float>& PcmPipeline::designFor(int taps) {  // :27-35 (C++11 thread-safe statics replace the lock)
    static const std::vector<float> h161 = design(161);
    static const std::vector<float> h321 = design(321);
    static const std::vector<float> h641 = design(641);
    if (taps == 161) return h161;
    if (taps == 641) return h641;
    return h321;
}

void PcmPipeline::setGain(float g) {  // :74
    Lock l(mu_);
    gain_ = (std::max)(0.1f, (std::min)(8.0f, g));
}

float PcmPipeline::getGain() const {  // :76
    Lock l(mu_);
    return gain_;
}

void PcmPipeline::setTaps(int n) {  // :79-85
    Lock l(mu_);
    int t = TAPS;
    for (int c : TAP_CHOICES) {
        if (c == n) t = c;
    }
    taps_ = t;
    injected_.clear();
    hh_ = designFor(t).data();
    resetFilterLocked();
}

int PcmPipeline::getTaps() const {  // :87
    Lock l(mu_);
    return taps_;
}

void PcmPipeline::setAsbCurve(bool on) {  // :92
    Lock l(mu_);
    asbCurve_ = on;
}

bool PcmPipeline::isAsbCurve() const {  // :94
    Lock l(mu_);
    return asbCurve_;
}

void PcmPipeline::setCap(int c) {  // :97
    Lock l(mu_);
    cap_ = (std::max)(0, (std::min)(protocol::MAX_AMPLITUDE, c));
}

int PcmPipeline::getCap() const {  // :99
    Lock l(mu_);
    return cap_;
}

void PcmPipeline::resetFilterLocked() {  // :102-111
    bufL_.fill(0.0f);
    bufR_.fill(0.0f);
    pos_ = 0;
    phase_ = 0;
    gateQuietMsL_ = gateQuietMsR_ = 0;
    gateClosedL_ = gateClosedR_ = false;
    gateGainL_ = gateGainR_ = 1.0f;
    silentMs_ = 0;
}

void PcmPipeline::resetFilter() {
    Lock l(mu_);
    resetFilterLocked();
}

void PcmPipeline::setGateThreshold(float pctFs) {  // :123-129
    Lock l(mu_);
    gateThresholdPct_ = (std::max)(0.0f, (std::min)(20.0f, pctFs));
    if (gateThresholdPct_ == 0.0f) {
        gateClosedL_ = gateClosedR_ = false;
        gateQuietMsL_ = gateQuietMsR_ = 0;
    }
}

float PcmPipeline::getGateThreshold() const {  // :131
    Lock l(mu_);
    return gateThresholdPct_;
}

double PcmPipeline::getSilentMs() const {  // :138
    Lock l(mu_);
    return silentMs_;
}

void PcmPipeline::gateUpdate(const int16_t* pcm, int frames) {  // :140-172
    if (frames <= 0) return;
    double sl = 0, sr = 0;
    for (int f = 0; f < frames; f++) {
        const double l = pcm[2 * f];
        const double r = pcm[2 * f + 1];
        sl += l * l;
        sr += r * r;
    }
    const double pl = 100.0 * std::sqrt(sl / frames) / 32768.0;
    const double pr = 100.0 * std::sqrt(sr / frames) / 32768.0;
    const double ms = frames * 1000.0 / SAMPLE_RATE;
    const bool quietAll = pl < SILENCE_PCT && pr < SILENCE_PCT;
    if (gateThresholdPct_ <= 0.0f) {
        if (quietAll) silentMs_ += ms;
        else silentMs_ = 0;
        return;
    }
    if (pl < gateThresholdPct_) {
        gateQuietMsL_ += ms;
        if (gateQuietMsL_ >= GATE_HOLD_MS) gateClosedL_ = true;
    } else {
        gateQuietMsL_ = 0;
        gateClosedL_ = false;
    }
    if (pr < gateThresholdPct_) {
        gateQuietMsR_ += ms;
        if (gateQuietMsR_ >= GATE_HOLD_MS) gateClosedR_ = true;
    } else {
        gateQuietMsR_ = 0;
        gateClosedR_ = false;
    }
    if (quietAll || (gateClosedL_ && gateClosedR_)) silentMs_ += ms;
    else silentMs_ = 0;
}

namespace {

float gateStep(float g, bool closed) {  // :174-177
    if (closed) return (std::max)(0.0f, g - 1.0f / PcmPipeline::GATE_FADE_OUTPUTS);
    return (std::min)(1.0f, g + 0.5f);
}

}  // namespace

void PcmPipeline::setLimit(int n) {  // :180
    Lock l(mu_);
    limit_ = (std::max)(1, (std::min)(CAPACITY, n));
}

int PcmPipeline::trimTo(int keep) {  // :183-191
    Lock l(mu_);
    const int k = (std::max)(0, keep);
    if (size_ <= k) return 0;
    const int d = size_ - k;
    head_ = (head_ + d) % CAPACITY;
    size_ = k;
    dropped_ += d;
    return d;
}

void PcmPipeline::clearQueue() {  // :193-196
    Lock l(mu_);
    head_ = 0;
    size_ = 0;
}

int PcmPipeline::process(const int16_t* pcm, size_t pcmLen, int frames) {  // :199-231
    Lock lk(mu_);
    int outs = 0;
    const int limit = (std::min)(frames, static_cast<int>(pcmLen / 2));
    gateUpdate(pcm, limit);
    for (int f = 0; f < limit; f++) {
        const float l = pcm[2 * f];
        const float r = pcm[2 * f + 1];
        bufL_[static_cast<size_t>(pos_)] = l;
        bufL_[static_cast<size_t>(pos_ + taps_)] = l;
        bufR_[static_cast<size_t>(pos_)] = r;
        bufR_[static_cast<size_t>(pos_ + taps_)] = r;
        const int newest = pos_;
        pos_ = pos_ + 1 == taps_ ? 0 : pos_ + 1;
        if (++phase_ == DECIMATION) {
            phase_ = 0;
            // time-ordered window buf[newest+1 .. newest+taps] (oldest -> newest); same float summation order as Java
            float accL = 0.0f, accR = 0.0f;
            const int base = newest + 1;
            for (int i = 0; i < taps_; i++) {
                const float h = hh_[i];
                accL += h * bufL_[static_cast<size_t>(base + i)];
                accR += h * bufR_[static_cast<size_t>(base + i)];
            }
            gateGainL_ = gateStep(gateGainL_, gateClosedL_);
            gateGainR_ = gateStep(gateGainR_, gateClosedR_);
            stGateOut_++;
            if (gateClosedL_ || gateClosedR_) stGateClosed_++;
            // Java evaluates call arguments left to right (scale(L) then scale(R)); C++ argument order is unspecified
            // (MSVC: right to left) and the stInSq sum is order-sensitive, so the order is made explicit.
            const int sL = scaleLocked(accL * gateGainL_);
            const int sR = scaleLocked(accR * gateGainR_);
            push(sL, sR);
            outs++;
        }
    }
    return outs;
}

int PcmPipeline::scale(float y) {  // :244
    Lock l(mu_);
    return scaleLocked(y);
}

int PcmPipeline::scaleLocked(float y) {  // :244-277
    const int lim = (std::min)(cap_, protocol::MAX_AMPLITUDE);
    const double in = y / 32768.0;
    const double knee = KNEE * lim;
    bool limited = false;
    double o;
    const double gain = gain_;  // float -> double promotion as in Java (gain * a, gain - 1.0)
    if (asbCurve_) {
        const double a = (std::min)(1.0, std::fabs(in));
        const double yn = gain * a / (1.0 + (gain - 1.0) * a);
        o = yn * lim;
        limited = lim > 0 && o > knee;
    } else {
        const double x = in * lim * gain;
        const double ax = std::fabs(x);
        o = ax;
        if (lim > 0 && ax > knee) {
            const double room = lim - knee;
            o = knee + room * std::tanh((ax - knee) / room);
            limited = true;
        }
    }
    const double x = in < 0 ? -1 : 1;
    int v = jc::l2i(jc::javaRound(x < 0 ? -o : o));
    v = (std::max)(-lim, (std::min)(lim, v));
    stN_++;
    if (limited) stLimited_++;
    if (lim > 0 && std::abs(v) >= lim) stClipped_++;
    stInSq_ += in * in;
    stOutSq_ += static_cast<double>(v) * v;
    return v;
}

std::array<double, 7> PcmPipeline::getAndResetLevels() {  // :280-290
    Lock l(mu_);
    const double n = static_cast<double>((std::max)(static_cast<int64_t>(1), stN_));
    const int lim = (std::min)(cap_, protocol::MAX_AMPLITUDE);
    const double outRms = std::sqrt(stOutSq_ / n);
    std::array<double, 7> r = {static_cast<double>(stN_),
                               100.0 * static_cast<double>(stLimited_) / n,
                               100.0 * static_cast<double>(stClipped_) / n,
                               100.0 * std::sqrt(stInSq_ / n),
                               outRms,
                               lim > 0 ? 100.0 * outRms / lim : 0,
                               100.0 * static_cast<double>(stGateClosed_) /
                                   static_cast<double>((std::max)(static_cast<int64_t>(1), stGateOut_))};
    stGateOut_ = stGateClosed_ = 0;
    stN_ = stLimited_ = stClipped_ = 0;
    stInSq_ = stOutSq_ = 0;
    return r;
}

void PcmPipeline::push(int l, int r) {  // :292-304
    while (size_ >= limit_) {
        head_ = (head_ + 1) % CAPACITY;
        size_--;
        dropped_++;
    }
    const int idx = (head_ + size_) % CAPACITY;
    qL_[static_cast<size_t>(idx)] = l;
    qR_[static_cast<size_t>(idx)] = r;
    size_++;
    const int a = (std::max)(std::abs(l), std::abs(r));
    if (a > peak_) peak_ = a;
}

int PcmPipeline::queueSize() const {  // :306
    Lock l(mu_);
    return size_;
}

int PcmPipeline::pop(int* l, int* r, int max) {  // :309-318
    Lock lk(mu_);
    const int n = (std::min)(max, size_);
    for (int i = 0; i < n; i++) {
        l[i] = qL_[static_cast<size_t>(head_)];
        r[i] = qR_[static_cast<size_t>(head_)];
        head_ = (head_ + 1) % CAPACITY;
    }
    size_ -= n;
    return n;
}

int64_t PcmPipeline::getAndResetDropped() {  // :320-324
    Lock l(mu_);
    const int64_t d = dropped_;
    dropped_ = 0;
    return d;
}

int PcmPipeline::getAndResetPeak() {  // :326-330
    Lock l(mu_);
    const int p = peak_;
    peak_ = 0;
    return p;
}

void PcmPipeline::setCoefficientsForTest(const std::vector<float>& h) {
    Lock l(mu_);
    if (h.empty() || h.size() > static_cast<size_t>(MAX_TAPS)) throw std::invalid_argument("coefficient count");
    injected_ = h;
    taps_ = static_cast<int>(h.size());
    hh_ = injected_.data();
    resetFilterLocked();
}

}  // namespace arbs
