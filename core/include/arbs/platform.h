// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - platform boundary (B2 design docs/analysis/B2_DESIGN.md section 2-2).
// The Core never touches a device, a thread priority API or a wall clock directly; everything goes through these three
// interfaces. Windows (ARBS-W) and the unit tests (FakeClock/FakePort) provide the implementations.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace arbs {

enum class IoResult { Ok, Timeout, Disconnected, Error };

// Single monotonic clock. "uptimeMillis" of ARBS-A = nowNs() / 1'000'000.
struct IClock {
    virtual ~IClock() = default;
    virtual int64_t nowNs() = 0;
    // Waking late is allowed, waking early is not.
    virtual void sleepUntilNs(int64_t deadlineNs) = 0;
};

// One dongle IF2. Only the engine thread calls these (the caller serialises writes).
struct IApexPort {
    virtual ~IApexPort() = default;
    // 32-byte frame. The Windows implementation prepends report ID 0x00 (33 bytes on the wire).
    virtual IoResult writeFrame(const uint8_t frame[32], int timeoutMs) = 0;
    // Raw report as received (a leading 0x00 report ID is allowed; ApexProtocol::parse accepts both).
    virtual IoResult readFrame(uint8_t* buf, size_t cap, size_t* outLen, int timeoutMs) = 0;
    // Optional: drop buffered input (e.g. right after opening). The engine itself drains with readFrame like ARBS-A.
    virtual void clearInput() {}
    // MMCSS / priority (ARBS-A Process.setThreadPriority(URGENT_AUDIO), ApexHaptics.java:423).
    virtual void onEngineThreadStart() {}
    virtual void onEngineThreadEnd() {}
};

struct ILog {
    virtual ~ILog() = default;
    virtual void info(const std::string& utf8) = 0;
    // ArbsLog.perSecondAllowed(): 20 MB rule, verbose override.
    virtual bool perSecondAllowed() = 0;
    // ArbsLog.verbose (ApexHaptics.setVerboseLog, ApexHaptics.java:273).
    virtual void setVerbose(bool) {}
};

}  // namespace arbs
