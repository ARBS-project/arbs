// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexProtocol.java (f3051641). Line references: AA:ApexProtocol.java:<line>.
#include "arbs/protocol.h"

#include "arbs/java_compat.h"

#include <cstdlib>
#include <initializer_list>
#include <stdexcept>

namespace arbs {
namespace protocol {

int checksum(const uint8_t* f) {  // :34-38
    int sum = 0;
    for (int i = 2; i <= 30; i++) sum += f[i];
    return sum & 0xFF;
}

namespace {

Frame cmd(int command, const int* payload, size_t n) {  // :40-53
    if (command != CMD_GET_INFO && command != CMD_MOTOR_ROUTE && command != CMD_REALTIME) {
        throw std::invalid_argument("허용되지 않은 명령");
    }
    if (n > 27) throw std::invalid_argument("payload 길이");
    Frame f{};
    f[0] = static_cast<uint8_t>(MAGIC0);
    f[1] = static_cast<uint8_t>(MAGIC1);
    f[2] = static_cast<uint8_t>(command);
    f[3] = static_cast<uint8_t>(n + 2);
    for (size_t i = 0; i < n; i++) f[4 + i] = static_cast<uint8_t>(payload[i]);
    f[31] = static_cast<uint8_t>(checksum(f.data()));
    return f;
}

Frame cmd(int command, std::initializer_list<int> p) { return cmd(command, p.begin(), p.size()); }

}  // namespace

Frame buildGetInfo() { return cmd(CMD_GET_INFO, nullptr, 0); }  // :56-58

Frame gripRealtimeEnable() { return cmd(CMD_MOTOR_ROUTE, {0x01, 0x12, 0x02, 0x40, 0x00}); }  // :61-63

std::array<Frame, 2> disablePair() {  // :66-71
    return {cmd(CMD_MOTOR_ROUTE, {0x01, 0x02, 0x00}), cmd(CMD_MOTOR_ROUTE, {0x01, 0x12, 0x00})};
}

Frame gripFrame(const int* left, size_t leftLen, const int* right, size_t rightLen) {  // :74-76
    return realtimeFrame(left, leftLen, right, rightLen, nullptr, 0, false, ROUTE_MUTE);
}

Frame triggerEnableBoth() { return cmd(CMD_MOTOR_ROUTE, {0x01, 0x02, 0x03, 0x00, 0x40}); }  // :82-84

std::array<Frame, 2> triggerSideEnable(int route) {  // :90-98
    if (route != ROUTE_LEFT && route != ROUTE_RIGHT) throw std::invalid_argument("한쪽 트리거만");
    const int target = route == ROUTE_RIGHT ? 0x01 : 0x00;
    const int other = target == 0x01 ? 0x00 : 0x01;
    return {cmd(CMD_MOTOR_ROUTE, {0x01, target, 0x03, 0x00, 0x40}), cmd(CMD_MOTOR_ROUTE, {0x01, other, 0x00})};
}

Frame realtimeFrame(const int* left, size_t leftLen, const int* right, size_t rightLen,
                    const int* trigger, size_t triggerLen, bool triggerOn, int route) {  // :104-120
    if (left == nullptr || right == nullptr || leftLen != SUBFRAMES || rightLen != SUBFRAMES) {
        throw std::invalid_argument("서브프레임은 8개");
    }
    if (triggerOn && (trigger == nullptr || triggerLen != SUBFRAMES)) throw std::invalid_argument("트리거 서브프레임은 8개");
    if (route < 0 || route > ROUTE_MUTE || (!triggerOn && route != ROUTE_MUTE)) throw std::invalid_argument("route");
    int p[25];
    p[0] = 0x80 | 0x10 | 0x08 | (triggerOn ? 0x04 : 0) | route;
    for (int i = 0; i < SUBFRAMES; i++) {
        if (std::abs(left[i]) > MAX_AMPLITUDE || std::abs(right[i]) > MAX_AMPLITUDE ||
            (triggerOn && std::abs(trigger[i]) > MAX_AMPLITUDE)) {
            throw std::invalid_argument("진폭 상한 초과");
        }
        p[1 + i * 3] = triggerOn ? trigger[i] + 128 : 0x80;
        p[2 + i * 3] = left[i] + 128;
        p[3 + i * 3] = right[i] + 128;
    }
    return cmd(CMD_REALTIME, p, 25);
}

Frame neutralFrame() {  // :122-124
    const int z[SUBFRAMES] = {0, 0, 0, 0, 0, 0, 0, 0};
    return gripFrame(z, z);
}

bool isSendable(const uint8_t* f, size_t len) {  // :127-131
    if (f == nullptr || len != static_cast<size_t>(FRAME_SIZE)) return false;
    const int c = f[2];
    return c == CMD_MOTOR_ROUTE || c == CMD_REALTIME;
}

std::string Info::describe() const {  // :148-152
    return jc::fmt("DeviceType=0x%02X Pro=%s connectionMode=0x%02X 배터리=%d features=0x%02X 그립햅틱(0x80)=%s 체크섬=%s",
                   deviceType, isPro() ? "예" : "아니오", connectionMode, battery, features,
                   hasGripHaptics() ? "예" : "아니오", checksumOk ? "일치" : "불일치");
}

std::optional<Info> parse(const uint8_t* raw, size_t len) {  // :156-166
    if (raw == nullptr) return std::nullopt;
    size_t off;
    if (len >= 33 && raw[0] == 0 && raw[1] == MAGIC0) off = 1;
    else if (len > 0 && raw[0] == MAGIC0) off = 0;
    else return std::nullopt;
    if (len < off + FRAME_SIZE) return std::nullopt;
    const uint8_t* f = raw + off;
    if (f[1] != MAGIC1 || f[2] != CMD_GET_INFO) return std::nullopt;
    Info info;
    info.deviceType = f[5];
    info.connectionMode = f[6];
    info.battery = f[11] & 0x0F;
    info.features = f[29];
    info.checksumOk = f[31] == checksum(f);
    return info;
}

std::string hex(const uint8_t* b, size_t len) {  // :168-175
    std::string s;
    static const char* digits = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        if (i > 0) s.push_back(' ');
        s.push_back(digits[b[i] >> 4]);
        s.push_back(digits[b[i] & 0x0F]);
    }
    return s;
}

}  // namespace protocol
}  // namespace arbs
