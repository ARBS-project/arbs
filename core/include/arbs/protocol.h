// SPDX-License-Identifier: GPL-3.0-or-later
// ARBS Core - APEX 6 Pro IF2 command frames. Port of ARBS-A ApexProtocol.java (android/arbs-moonlight f3051641).
// Byte facts come from ApexSenseBridge d831575 (src/flydigi/Apex6Protocol.cpp:33-54,100-175, Apex5Device.cpp:741-767);
// ASB license note: its LICENSE says GPL-3.0-or-later while its README says MIT; only protocol facts are used.
// Only GetInfo (0x01), 0x53 (grip/trigger enable/disable) and 0x57 (grip + optional trigger channel) can be built.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace arbs {
namespace protocol {

constexpr int FRAME_SIZE = 32;
constexpr int MAGIC0 = 0x5A;
constexpr int MAGIC1 = 0xA5;
constexpr int CMD_GET_INFO = 0x01;
constexpr int CMD_MOTOR_ROUTE = 0x53;
constexpr int CMD_REALTIME = 0x57;
constexpr int ROUTE_MUTE = 3;
constexpr int MAX_AMPLITUDE = 127;
constexpr int SUBFRAMES = 8;
constexpr int FRAME_MS = 8;
constexpr int ENABLE_WAIT_MS = 94;
constexpr int ROUTE_LEFT = 0, ROUTE_RIGHT = 1, ROUTE_BOTH = 2;

using Frame = std::array<uint8_t, FRAME_SIZE>;

// Sum of bytes 2..30 & 0xFF. [f] must hold at least 31 bytes.
int checksum(const uint8_t* f);

Frame buildGetInfo();
Frame gripRealtimeEnable();                 // 0x53 01 12 02 40 00
std::array<Frame, 2> disablePair();         // 0x53 01 02 00, 01 12 00
Frame triggerEnableBoth();                  // 0x53 01 02 03 00 40
std::array<Frame, 2> triggerSideEnable(int route);   // throws std::invalid_argument unless LEFT/RIGHT

// realtimeFrame(l,r,t,on,route) with explicit lengths (Java checks array length == 8).
// trigger may be nullptr. Throws std::invalid_argument exactly where Java throws IllegalArgumentException.
Frame realtimeFrame(const int* left, size_t leftLen, const int* right, size_t rightLen,
                    const int* trigger, size_t triggerLen, bool triggerOn, int route);
inline Frame realtimeFrame(const int* left8, const int* right8, const int* trigger8, bool triggerOn, int route) {
    return realtimeFrame(left8, SUBFRAMES, right8, SUBFRAMES, trigger8, trigger8 ? SUBFRAMES : 0, triggerOn, route);
}
Frame gripFrame(const int* left, size_t leftLen, const int* right, size_t rightLen);
inline Frame gripFrame(const int* left8, const int* right8) { return gripFrame(left8, SUBFRAMES, right8, SUBFRAMES); }
Frame neutralFrame();

// 32 bytes and 0x53 or 0x57 (GetInfo goes over a separate path). f may be nullptr.
bool isSendable(const uint8_t* f, size_t len);

struct Info {
    int deviceType = 0, connectionMode = 0, battery = 0, features = 0;
    bool checksumOk = false;
    bool isPro() const { return deviceType == 0x96 || deviceType == 0x98; }
    bool hasGripHaptics() const { return (features & 0x80) != 0; }
    bool hasTriggerHaptics() const { return (features & 0x10) != 0; }  // B5 D-B5-1 (ASB Apex5Identity.h:57-60)
    std::string describe() const;
};

// GetInfo response from a raw report (leading 0x00 report ID allowed). nullopt if not a GetInfo response.
std::optional<Info> parse(const uint8_t* raw, size_t len);

// "AA BB CC" upper-case hex (log helper).
std::string hex(const uint8_t* b, size_t len);

}  // namespace protocol
}  // namespace arbs
