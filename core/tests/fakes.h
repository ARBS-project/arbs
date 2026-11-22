// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core tests - FakeClock / FakePort / CaptureLog for deterministic engine scenarios (B2_DESIGN.md 3-5).
// FakeClock: virtual time; sleepUntilNs() jumps forward and runs the scheduled events (PCM, rumble, trigger, input,
// user actions) whose time has come, on the calling (engine) thread, in time order. No real waiting, no devices.
// FakePort: records every written frame with its virtual time; answers GetInfo per script; reads with nothing
// pending consume the full timeout of virtual time (like a blocking HID/bulk read).
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "arbs/platform.h"
#include "arbs/protocol.h"

namespace fake {

constexpr int64_t MS = 1000000LL;

class FakeClock : public arbs::IClock {
public:
    explicit FakeClock(int64_t startNs = 1000000000LL) : now_(startNs) {}
    int64_t nowNs() override { return now_; }
    void sleepUntilNs(int64_t deadlineNs) override { advanceTo(deadlineNs); }

    void at(int64_t tNs, std::function<void()> fn) { events_.emplace(std::make_pair(tNs, seq_++), std::move(fn)); }
    // Hard stop: when virtual time would pass [limitNs], [onLimit] runs once (e.g. engine.requestTerminate).
    void setLimit(int64_t limitNs, std::function<void()> onLimit) {
        limitNs_ = limitNs;
        limitHit_ = false;  // B5: allows a second run after a limit-terminated first run (re-attach scenarios)
        onLimit_ = std::move(onLimit);
    }
    bool limitHit() const { return limitHit_; }
    void advanceTo(int64_t deadline) {
        for (;;) {
            if (events_.empty()) break;
            auto it = events_.begin();
            if (it->first.first > deadline) break;
            if (it->first.first > now_) now_ = it->first.first;
            std::function<void()> fn = std::move(it->second);
            events_.erase(it);
            fn();
        }
        if (deadline > now_) now_ = deadline;
        if (!limitHit_ && limitNs_ > 0 && now_ > limitNs_) {
            limitHit_ = true;
            if (onLimit_) onLimit_();
        }
    }

private:
    int64_t now_;
    uint64_t seq_ = 0;
    std::map<std::pair<int64_t, uint64_t>, std::function<void()>> events_;
    int64_t limitNs_ = 0;
    bool limitHit_ = false;
    std::function<void()> onLimit_;
};

struct Written {
    int64_t t;
    arbs::protocol::Frame f;
    bool ok;
    int cmd() const { return f[2]; }
    bool is57() const { return f[2] == 0x57; }
    bool is53() const { return f[2] == 0x53; }
    bool isGetInfo() const { return f[2] == 0x01; }
    // 0x57: all grip bytes (left/right) are 0x80
    bool gripNeutral() const {
        for (int i = 0; i < 8; i++)
            if (f[static_cast<size_t>(6 + i * 3)] != 0x80 || f[static_cast<size_t>(7 + i * 3)] != 0x80) return false;
        return true;
    }
    int flags() const { return f[4]; }
};

class FakePort : public arbs::IApexPort {
public:
    explicit FakePort(FakeClock& c) : clock_(c) {}

    // GetInfo answer: device type / features / battery nibble. respond=false -> silence.
    bool respond = true;
    // B5 D-B5-1: default features 0x9F (ASB sample, grip 0x80 + trigger 0x10). 0x80 alone = no trigger haptics.
    int deviceType = 0x96, features = 0x9F, battery = 7;
    bool badChecksum = false;
    int64_t responseDelayNs = 2 * MS;
    // Write failure: fail the N-th 0x57 write (1-based), 0 = never.
    int fail57At = 0;
    std::vector<Written> writes;
    int threadStarts = 0, threadEnds = 0;

    arbs::IoResult writeFrame(const uint8_t frame[32], int /*timeoutMs*/) override {
        Written w;
        w.t = clock_.nowNs();
        std::memcpy(w.f.data(), frame, 32);
        w.ok = true;
        if (w.is57()) {
            n57_++;
            if (fail57At > 0 && n57_ == fail57At) w.ok = false;
        }
        writes.push_back(w);
        if (!w.ok) return arbs::IoResult::Error;
        if (w.isGetInfo() && respond) {
            std::array<uint8_t, 33> r{};
            r[0] = 0x00;  // report ID (Windows style, parse accepts it)
            uint8_t* f = r.data() + 1;
            f[0] = 0x5A;
            f[1] = 0xA5;
            f[2] = 0x01;
            f[3] = 0x1C;
            f[5] = static_cast<uint8_t>(deviceType);
            f[6] = 0x02;
            f[11] = static_cast<uint8_t>(0x30 | (battery & 0x0F));
            f[29] = static_cast<uint8_t>(features);
            f[31] = static_cast<uint8_t>(arbs::protocol::checksum(f) + (badChecksum ? 1 : 0));
            rx_.push_back({clock_.nowNs() + responseDelayNs, r});
        }
        return arbs::IoResult::Ok;
    }

    arbs::IoResult readFrame(uint8_t* buf, size_t cap, size_t* outLen, int timeoutMs) override {
        *outLen = 0;
        const int64_t until = clock_.nowNs() + static_cast<int64_t>(timeoutMs) * MS;
        if (!rx_.empty() && rx_.front().first <= until) {
            if (rx_.front().first > clock_.nowNs()) clock_.sleepUntilNs(rx_.front().first);
            const auto r = rx_.front().second;
            rx_.erase(rx_.begin());
            const size_t n = cap < r.size() ? cap : r.size();
            std::memcpy(buf, r.data(), n);
            *outLen = n;
            return arbs::IoResult::Ok;
        }
        clock_.sleepUntilNs(until);
        return arbs::IoResult::Timeout;
    }

    void onEngineThreadStart() override { threadStarts++; }
    void onEngineThreadEnd() override { threadEnds++; }

    // helpers
    std::vector<Written> since(int64_t t) const {
        std::vector<Written> v;
        for (const Written& w : writes)
            if (w.t >= t) v.push_back(w);
        return v;
    }
    int count(int cmd) const {
        int n = 0;
        for (const Written& w : writes)
            if (w.cmd() == cmd) n++;
        return n;
    }

private:
    FakeClock& clock_;
    int n57_ = 0;
    std::vector<std::pair<int64_t, std::array<uint8_t, 33>>> rx_;
};

class CaptureLog : public arbs::ILog {
public:
    std::vector<std::pair<int64_t, std::string>> lines;
    explicit CaptureLog(arbs::IClock* c = nullptr) : clock_(c) {}
    void info(const std::string& s) override { lines.emplace_back(clock_ ? clock_->nowNs() : 0, s); }
    bool perSecondAllowed() override { return true; }
    void setVerbose(bool v) override { verbose = v; }
    bool verbose = false;
    int count(const std::string& sub) const {
        int n = 0;
        for (const auto& l : lines)
            if (l.second.find(sub) != std::string::npos) n++;
        return n;
    }
    int64_t firstTime(const std::string& sub) const {
        for (const auto& l : lines)
            if (l.second.find(sub) != std::string::npos) return l.first;
        return -1;
    }
    std::string find(const std::string& sub) const {
        for (const auto& l : lines)
            if (l.second.find(sub) != std::string::npos) return l.second;
        return std::string();
    }

private:
    arbs::IClock* clock_;
};

}  // namespace fake
