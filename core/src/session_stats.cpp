// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ArbsSessionStats.java (f3051641). Line references: AA:ArbsSessionStats.java:<line>.
#include "arbs/session_stats.h"

#include "arbs/java_compat.h"

#include <algorithm>

namespace arbs {

using Lock = std::lock_guard<std::mutex>;

bool SessionStats::perSecondAllowed(int64_t logFileBytes, bool verbose) {  // :29-31
    return verbose || logFileBytes <= PER_SECOND_LIMIT_BYTES;
}

void SessionStats::reset(int64_t nowNs) {  // :33-46
    Lock g(mu_);
    const int64_t keepRetries = cum_[RETRIES], keepReconnects = cum_[RECONNECTS];
    cum_.fill(0);
    minuteBase_.fill(0);
    cum_[RETRIES] = minuteBase_[RETRIES] = keepRetries;
    cum_[RECONNECTS] = minuteBase_[RECONNECTS] = keepReconnects;
    startNs_ = lastMinuteNs_ = nowNs;
    started_ = true;
    maxSilenceMs_ = 0;
    extraSum_ = minExtraSum_ = rttSum_ = minRttSum_ = 0;
    extraN_ = minExtraN_ = rttN_ = minRttN_ = 0;
    batteryStart_ = batteryEnd_ = -1;
    crashed_ = false;
}

void SessionStats::add(int idx, int64_t v) {  // :49
    Lock g(mu_);
    cum_[static_cast<size_t>(idx)] += v;
}

void SessionStats::set(int idx, int64_t absolute) {  // :52
    Lock g(mu_);
    cum_[static_cast<size_t>(idx)] = absolute;
}

int64_t SessionStats::get(int idx) const {  // :54
    Lock g(mu_);
    return cum_[static_cast<size_t>(idx)];
}

void SessionStats::silenceMs(double ms) {  // :56
    Lock g(mu_);
    if (ms > maxSilenceMs_) maxSilenceMs_ = ms;
}

void SessionStats::addExtraDelay(double ms) {  // :58
    Lock g(mu_);
    extraSum_ += ms;
    extraN_++;
    minExtraSum_ += ms;
    minExtraN_++;
}

void SessionStats::addRtt(double ms) {  // :60-63
    Lock g(mu_);
    if (ms < 0) return;
    rttSum_ += ms;
    rttN_++;
    minRttSum_ += ms;
    minRttN_++;
}

void SessionStats::batteryStart(int v) {  // :65
    Lock g(mu_);
    batteryStart_ = v;
}

void SessionStats::batteryEnd(int v) {  // :67
    Lock g(mu_);
    batteryEnd_ = v;
}

void SessionStats::markCrash() {  // :69
    Lock g(mu_);
    crashed_ = true;
}

bool SessionStats::minuteDue(int64_t nowNs) const {  // :71
    Lock g(mu_);
    return started_ && nowNs - lastMinuteNs_ >= MINUTE_NS;
}

namespace {

std::string hms(int64_t ns) {  // :73-76
    const int64_t s = (std::max)(static_cast<int64_t>(0), ns / 1000000000LL);
    return jc::fmt("%02lld:%02lld:%02lld", static_cast<long long>(s / 3600), static_cast<long long>((s / 60) % 60),
                   static_cast<long long>(s % 60));
}

std::string battery(int v) { return v < 0 ? std::string("n/a") : std::to_string(v); }  // :78

long long ll(int64_t v) { return static_cast<long long>(v); }

}  // namespace

std::string SessionStats::minuteLine(int64_t nowNs, const std::string& session, const std::string& apex) {  // :81-94
    Lock g(mu_);
    std::array<int64_t, N> d{};
    for (size_t i = 0; i < static_cast<size_t>(N); i++) {
        d[i] = cum_[i] - minuteBase_[i];
        minuteBase_[i] = cum_[i];
    }
    const std::string extra = minExtraN_ == 0 ? std::string("n/a") : jc::fmt("%.1fms", minExtraSum_ / static_cast<double>(minExtraN_));
    const std::string rtt = minRttN_ == 0 ? std::string("n/a") : jc::fmt("%.0fms", minRttSum_ / static_cast<double>(minRttN_));
    std::string s = jc::fmt(
        "ARBS 1분 요약: 경과 %s 세션=%s APEX=%s | PCM 패킷 %lld 갭 %lld | 송신 그립 %lld 트리거 %lld 프레임 | 폐기 %lld 언더런 %lld | "
        "rumble 호출 %lld 타임아웃 %lld | 트리거 효과 적용 %lld 해제 %lld | ARBS추가지연 평균 %s | Moonlight RTT 평균 %s | USB오류 %lld (최근 1분 구간)",
        hms(nowNs - startNs_).c_str(), session.c_str(), apex.c_str(), ll(d[PCM_PKTS]), ll(d[PCM_GAPS]), ll(d[GRIP_FRAMES]),
        ll(d[TRIG_FRAMES]), ll(d[DROPPED]), ll(d[UNDERRUNS]), ll(d[RUMBLE_CALLS]), ll(d[RUMBLE_TIMEOUTS]), ll(d[TRIG_APPLIED]),
        ll(d[TRIG_STOPS]), extra.c_str(), rtt.c_str(), ll(d[USB_ERRORS]));
    minExtraSum_ = 0;
    minExtraN_ = 0;
    minRttSum_ = 0;
    minRttN_ = 0;
    lastMinuteNs_ = nowNs;
    return s;
}

std::string SessionStats::sessionLineLocked(int64_t nowNs, const std::string& reason) const {  // :97-107
    const std::string extra = extraN_ == 0 ? std::string("n/a") : jc::fmt("%.1fms", extraSum_ / static_cast<double>(extraN_));
    const std::string rtt = rttN_ == 0 ? std::string("n/a") : jc::fmt("%.0fms", rttSum_ / static_cast<double>(rttN_));
    return jc::fmt(
        "ARBS 세션 요약: 총 시간 %s (%s) | PCM 패킷 %lld 갭 %lld 최대무수신 %.0fms | 송신 그립 %lld 트리거 %lld 프레임 | 폐기 %lld 언더런 %lld | "
        "rumble 호출 %lld 타임아웃 %lld | 트리거 효과 적용 %lld 해제 %lld | ARBS추가지연 평균 %s | Moonlight RTT 평균 %s | USB오류 %lld | "
        "재시도 %lld 재연결 %lld | APEX 배터리(GetInfo 원값) 시작 %s 종료 %s | 크래시 %d",
        hms(nowNs - startNs_).c_str(), reason.c_str(), ll(cum_[PCM_PKTS]), ll(cum_[PCM_GAPS]), maxSilenceMs_, ll(cum_[GRIP_FRAMES]),
        ll(cum_[TRIG_FRAMES]), ll(cum_[DROPPED]), ll(cum_[UNDERRUNS]), ll(cum_[RUMBLE_CALLS]), ll(cum_[RUMBLE_TIMEOUTS]),
        ll(cum_[TRIG_APPLIED]), ll(cum_[TRIG_STOPS]), extra.c_str(), rtt.c_str(), ll(cum_[USB_ERRORS]), ll(cum_[RETRIES]),
        ll(cum_[RECONNECTS]), battery(batteryStart_).c_str(), battery(batteryEnd_).c_str(), crashed_ ? 1 : 0);
}

std::string SessionStats::sessionLine(int64_t nowNs, const std::string& reason) const {
    Lock g(mu_);
    return sessionLineLocked(nowNs, reason);
}

bool SessionStats::trySessionLine(int64_t nowNs, const std::string& reason, std::string& out) const {
    std::unique_lock<std::mutex> g(mu_, std::try_to_lock);
    if (!g.owns_lock()) return false;
    out = sessionLineLocked(nowNs, reason);
    return true;
}

}  // namespace arbs
