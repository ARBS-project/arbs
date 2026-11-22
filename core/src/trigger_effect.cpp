// SPDX-License-Identifier: GPL-3.0-or-later
// Port of ARBS-A ApexTriggerEffect.java (f3051641). Line references: AA:ApexTriggerEffect.java:<line>.
#include "arbs/trigger_effect.h"

#include "arbs/java_compat.h"

#include <algorithm>
#include <cmath>

namespace arbs {

using Type = TriggerEffect::Type;

bool TriggerEffect::same(const TriggerEffect& o) const {  // :44-52 (pulseAttack/pulseRelease not compared, as in Java)
    if (type != o.type || startZone != o.startZone || endZone != o.endZone || strength != o.strength ||
        frequency != o.frequency || snapStrength != o.snapStrength || detent != o.detent || rhythmDrive != o.rhythmDrive ||
        rhythmPeriod != o.rhythmPeriod || rhythmFrom != o.rhythmFrom || rhythmTo != o.rhythmTo || pulseHold != o.pulseHold) {
        return false;
    }
    return zoneStrengths == o.zoneStrengths;
}

std::string TriggerEffect::describe() const {  // :77-86
    switch (type) {
        case Type::OFF: return "Off";
        case Type::FEEDBACK:
            return "Feedback(0x21) zone " + std::to_string(startZone) + ".." + std::to_string(endZone) + " maxStr " +
                   std::to_string(maxStrength());
        case Type::VIBRATION:
            return "Vibration(0x26) zone " + std::to_string(startZone) + ".." + std::to_string(endZone) + " maxStr " +
                   std::to_string(maxStrength()) + " " + std::to_string(frequency) + "Hz" + (detent ? " detent" : "");
        case Type::WEAPON:
            return "Weapon(0x25) zone " + std::to_string(startZone) + ".." + std::to_string(endZone) + " str " +
                   std::to_string(strength);
        case Type::BOW:
            return "Bow(0x22) zone " + std::to_string(startZone) + ".." + std::to_string(endZone) + " str " +
                   std::to_string(strength) + " snap " + std::to_string(snapStrength);
        default:
            return "PulseTrain(0x26<=30Hz) period " + std::to_string(rhythmPeriod) + "ms drive " + std::to_string(rhythmDrive) +
                   " from " + std::to_string(rhythmFrom);
    }
}

int TriggerEffect::maxStrength() const {  // :88-92
    int m = 0;
    for (int v : zoneStrengths) m = (std::max)(m, v);
    return m;
}

int TriggerEffect::positionFor(int zone) { return (zone * 256 + 9) / 10; }  // :102-104

namespace {

TriggerEffect::Decoded& fail(TriggerEffect::Decoded& d, int code) {  // :174-178
    d.code = code;
    d.effect = TriggerEffect();
    return d;
}

}  // namespace

TriggerEffect::Decoded TriggerEffect::decode(int type, const uint8_t* payload) {  // :110-172
    Decoded d;
    int b[11] = {0};
    b[0] = type & 0xFF;
    if (payload != nullptr) {
        for (int i = 0; i < 10; i++) b[1 + i] = payload[i];
    }
    TriggerEffect& r = d.effect;
    d.code = OK;
    if (b[0] == 0x00 || b[0] == 0x05) return d;
    if (b[0] == 0x21 || b[0] == 0x26 || b[0] == 0x25 || b[0] == 0x22) {
        const int mask = b[1] | (b[2] << 8);
        if ((mask & ~0x03FF) != 0) return fail(d, INVALID);
        if (mask == 0 || (b[0] == 0x26 && b[9] == 0)) return d;
        int first = 10, last = 0, active = 0;
        for (int zone = 0; zone < 10; zone++) {
            if ((mask & (1 << zone)) == 0) continue;
            first = (std::min)(first, zone);
            last = zone;
            active++;
        }
        r.startZone = first;
        r.endZone = last;
        if (b[0] == 0x25 || b[0] == 0x22) {
            const bool bow = b[0] == 0x22;
            const int forcePair = b[3] | (b[4] << 8);
            if (active != 2 || first >= last || (bow && (last > 8 || (forcePair & ~0x3F) != 0))) return fail(d, INVALID);
            r.type = bow ? Type::BOW : Type::WEAPON;
            r.strength = (b[3] & 0x07) + 1;
            if (bow) r.snapStrength = ((forcePair >> 3) & 0x07) + 1;
            return d;
        }
        const int64_t packed = static_cast<int64_t>(b[3]) | (static_cast<int64_t>(b[4]) << 8) |
                               (static_cast<int64_t>(b[5]) << 16) | (static_cast<int64_t>(b[6]) << 24);
        for (int zone = 0; zone < 10; zone++) {
            if ((mask & (1 << zone)) != 0) {
                r.zoneStrengths[static_cast<size_t>(zone)] = static_cast<int>((packed >> (zone * 3)) & 0x07) + 1;
            }
        }
        if (b[0] == 0x26 && b[9] <= PULSE_TRAIN_MAX_HZ) {
            const int strongest = r.maxStrength();
            TriggerEffect train;
            train.type = Type::RHYTHM;
            const int period = (std::max)(1, jc::l2i(jc::lroundJ(1000.0 / b[9])));
            int hold = BURST_HOLD;
            const int fit = period * 7 / 10;
            const int total = BURST_ATTACK + BURST_HOLD + BURST_RELEASE;
            if (total > fit) {
                const int tail = BURST_ATTACK + BURST_RELEASE;
                hold = fit > tail + 4 ? fit - tail : 4;
            }
            train.pulseHold = hold;
            train.rhythmPeriod = (std::max)(period, BURST_ATTACK + hold + BURST_RELEASE);
            train.rhythmDrive = jc::l2i((std::min)(static_cast<int64_t>(MAX_DRIVE),
                                                   jc::lroundJ(PULSE_TRAIN_BASE_DRIVE + PULSE_TRAIN_DRIVE_STEP * (strongest - 1.0))));
            train.rhythmFrom = positionFor(first);
            train.rhythmTo = 255;
            d.effect = train;
            return d;
        }
        r.type = b[0] == 0x21 ? Type::FEEDBACK : Type::VIBRATION;
        r.frequency = b[0] == 0x26 ? b[9] : 0;
        return d;
    }
    return fail(d, UNSUPPORTED);
}

// ---------------------------------------------------------------- render state

void TriggerEffect::State::reset() {  // :193-207
    phase = 0;
    amplitude = 0;
    burstAttack = BURST_ATTACK;
    burstHold = BURST_HOLD;
    burstRelease = BURST_RELEASE;
    burstsStarted = 0;
    trainElapsed = 0;
    burstElapsed = 0;
    burstLength = 0;
    previousPosition = 0;
    burstDrive = 0;
    hasPreviousPosition = hasAmplitude = burstInFrame = trainActive = detentPending = false;
    armed = true;
}

void TriggerEffect::State::retarget() {  // :210-231
    const double ph = phase, am = amplitude;
    const bool ha = hasAmplitude && am > 0.0;
    const int ba = burstAttack, bh = burstHold, br = burstRelease, be = burstElapsed, bl = burstLength, bd = burstDrive;
    const int64_t bs = burstsStarted;
    const int pp = previousPosition;
    const bool hp = hasPreviousPosition, ar = armed;
    reset();
    phase = ph;
    amplitude = am;
    hasAmplitude = ha;
    burstAttack = ba;
    burstHold = bh;
    burstRelease = br;
    burstElapsed = be;
    burstLength = bl;
    burstDrive = bd;
    burstsStarted = bs;
    previousPosition = pp;
    hasPreviousPosition = hp;
    armed = ar;
}

double TriggerEffect::clamp(double v, double lo, double hi) { return jc::dmax(lo, jc::dmin(hi, v)); }  // :242-244

double TriggerEffect::zoneLevel(const std::array<int, 10>& strengths, double position) {  // :246-262
    const double coordinate = position * (10.0 / 256.0);
    const int zone = jc::d2i(clamp(coordinate, 0.0, 10 - 0.001));
    const double level = strengths[static_cast<size_t>(zone)];
    if (level == 0.0) return 0.0;
    const double offset = coordinate - (zone + 0.5);
    if (offset < 0.0) {
        if (zone == 0) return level;
        const double neighbour = strengths[static_cast<size_t>(zone - 1)];
        const double edge = neighbour > 0.0 ? (level + neighbour) / 2.0 : 0.0;
        return edge + (level - edge) * (offset + 0.5) / 0.5;
    }
    if (zone + 1 >= 10) return level;
    const double neighbour = strengths[static_cast<size_t>(zone + 1)];
    const double edge = neighbour > 0.0 ? (level + neighbour) / 2.0 : 0.0;
    return level + (edge - level) * offset / 0.5;
}

int TriggerEffect::burstDriveForForce(int force) {  // :264-268
    if (force == 0) return 0;
    const double fraction =
        clamp((force - static_cast<double>(FORCE_PER_STRENGTH)) / (BURST_FULL_FORCE - FORCE_PER_STRENGTH), 0.0, 1.0);
    return jc::l2i(jc::lroundJ(BURST_MIN_DRIVE + (BURST_MAX_DRIVE - BURST_MIN_DRIVE) * fraction));
}

double TriggerEffect::shapeValue(int attack, int hold, int release, double hz, int index) {  // :271-279
    const int body = attack + hold + release;
    double level;
    if (index < attack) level = jc::dmin(1.0, static_cast<double>(index + 1) / attack);
    else if (index < attack + hold) level = 1.0;
    else if (index < body) level = 1.0 + (0.0 - 1.0) * (static_cast<double>(index - attack - hold + 1) / release);
    else level = 0.0;
    return std::sin(2.0 * jc::kPi * hz * index / 1000.0) * level;
}

namespace {

int driveSample(double value) {  // :281-283
    const int64_t v = jc::lroundJ(value);
    return jc::l2i((std::max)(static_cast<int64_t>(-TriggerEffect::MAX_DRIVE), (std::min)(static_cast<int64_t>(TriggerEffect::MAX_DRIVE), v)));
}

void startBurst(TriggerEffect::State& s, int attack, int hold, int release, int drive) {  // :285-295
    const int total = attack + hold + release;
    if (drive == 0 || total == 0) return;
    s.burstAttack = attack;
    s.burstHold = hold;
    s.burstRelease = release;
    s.burstDrive = (std::min)(drive, 255);
    s.burstElapsed = 0;
    s.burstLength = (std::min)(total, 65535);
    s.burstsStarted++;
}

bool crossedBreakPoint(TriggerEffect::State& s, double position, double rearm, double trigger) {  // :297-302
    if (position <= rearm) s.armed = true;
    if (!s.hasPreviousPosition || !s.armed || s.previousPosition >= trigger || position < trigger) return false;
    s.armed = false;
    return true;
}

int burstSample(TriggerEffect::State& s) {  // :304-315
    const int body = s.burstAttack + s.burstHold + s.burstRelease;
    const int index = s.burstElapsed;
    const double value =
        TriggerEffect::shapeValue(s.burstAttack, s.burstHold, s.burstRelease, TriggerEffect::BURST_HZ, index) * s.burstDrive;
    if (index < body) s.burstInFrame = true;
    s.burstElapsed++;
    if (s.burstElapsed >= s.burstLength) {
        s.burstElapsed = 0;
        s.burstLength = 0;
    }
    return driveSample(value);
}

int rhythmSample(const TriggerEffect& e, int elapsed) {  // :317-324
    if (e.rhythmPeriod == 0) return 0;
    const int cycle = elapsed % e.rhythmPeriod;
    const int total = e.pulseAttack + e.pulseHold + e.pulseRelease;
    double value = 0.0;
    if (cycle < total) {
        value = TriggerEffect::shapeValue(e.pulseAttack, e.pulseHold, e.pulseRelease, TriggerEffect::BURST_HZ, cycle) * e.rhythmDrive;
    }
    return driveSample(value);
}

int sample(TriggerEffect::State& s, double frequency, double amplitude) {  // :326-331
    const int64_t value = jc::lroundJ(std::sin(s.phase * 2.0 * jc::kPi) * amplitude);
    s.phase += frequency / 1000.0;
    s.phase -= std::floor(s.phase);
    return jc::l2i((std::max)(static_cast<int64_t>(-TriggerEffect::MAX_DRIVE), (std::min)(static_cast<int64_t>(TriggerEffect::MAX_DRIVE), value)));
}

// levelCurve(level) = level^1 (kApex6TriggerLevelExponent = 1).
double zoneLevelPositive(const std::array<int, 10>& strengths, double position) {  // :426-429
    const double l = TriggerEffect::zoneLevel(strengths, position);
    return l > 0.0 ? l : 0.0;
}

}  // namespace

void TriggerEffect::render(const TriggerEffect& e, int position, State& s, int64_t sampleOffset, int out[8]) {  // :337-423
    double amplitude = 0.0;
    double frequency = CARRIER_HZ;
    bool continuous = false;
    s.burstInFrame = false;
    const double current = position;
    switch (e.type) {
        case Type::OFF:
            break;
        case Type::FEEDBACK:
            amplitude = FEEDBACK_DRIVE_PER_LEVEL * zoneLevelPositive(e.zoneStrengths, current);
            continuous = true;
            break;
        case Type::VIBRATION: {
            continuous = true;
            if (e.detent) {
                const double detentStart = jc::dmax(DETENT_MIN_POSITION, positionFor(e.startZone));
                const bool installedWhilePressed = s.detentPending && current >= DETENT_MIN_POSITION;
                if (installedWhilePressed) s.detentPending = false;
                if (crossedBreakPoint(s, current, jc::dmax(0.0, detentStart - DETENT_REARM_MARGIN), detentStart) ||
                    installedWhilePressed) {
                    const int strongest = e.maxStrength();
                    startBurst(s, BURST_ATTACK, DETENT_HOLD, DETENT_RELEASE,
                               (std::min)(DETENT_MAX_DRIVE, DETENT_BASE_DRIVE + DETENT_PER_STRENGTH * strongest));
                }
            }
            if (e.frequency == 0) break;
            amplitude = VIBRATION_DRIVE_PER_LEVEL * zoneLevelPositive(e.zoneStrengths, current);
            frequency = e.frequency;
            break;
        }
        case Type::WEAPON: {
            const double start = positionFor(e.startZone), end = positionFor(e.endZone);
            if (crossedBreakPoint(s, current, start + REARM_MARGIN, end)) {
                startBurst(s, BURST_ATTACK, BURST_HOLD, BURST_RELEASE, burstDriveForForce(e.strength * FORCE_PER_STRENGTH));
            }
            break;
        }
        case Type::BOW: {
            const double start = positionFor(e.startZone), end = positionFor(e.endZone);
            if (crossedBreakPoint(s, current, start + REARM_MARGIN, end)) {
                startBurst(s, BURST_ATTACK, BURST_HOLD, BURST_RELEASE, burstDriveForForce(e.snapStrength * FORCE_PER_STRENGTH));
            }
            if (s.armed && current < end && end > start) {
                const double draw = clamp((current - start) / (end - start), 0.0, 1.0);
                amplitude = e.strength * BOW_DRIVE_PER_LEVEL * draw;
            }
            break;
        }
        default: {  // RHYTHM
            const double from = e.rhythmFrom, to = e.rhythmTo;
            const bool inside = current >= from && current <= to;
            const bool staying = current + RHYTHM_RELEASE_MARGIN >= from && current <= to + RHYTHM_RELEASE_MARGIN;
            if (!s.trainActive && inside) {
                s.trainActive = true;
                s.trainElapsed = 0;
            } else if (s.trainActive && !staying) {
                s.trainActive = false;
            }
            break;
        }
    }
    s.previousPosition = position;
    s.hasPreviousPosition = true;
    if (continuous && sampleOffset >= 0) {
        s.phase = std::fmod((sampleOffset % 1000LL) * frequency / 1000.0, 1.0);
    }
    if (continuous && current < CONTINUOUS_RELEASE_POSITION) amplitude = 0.0;
    const double target = std::isfinite(amplitude) ? clamp(amplitude, 0.0, NATIVE_MAX_DRIVE) : 0.0;
    double rampStart = s.hasAmplitude ? s.amplitude : target;
    for (int i = 0; i < 8; i++) {
        if (s.burstLength != 0) {
            out[i] = burstSample(s);
            rampStart = 0.0;
            continue;
        }
        if (e.type == Type::RHYTHM && s.trainActive) {
            out[i] = rhythmSample(e, s.trainElapsed++);
            rampStart = 0.0;
            continue;
        }
        const double a = rampStart + (target - rampStart) * static_cast<double>(i + 1) / 8.0;
        out[i] = sample(s, frequency, a);
    }
    s.amplitude = s.burstInFrame ? 0.0 : target;
    s.hasAmplitude = true;
}

int TriggerEffect::scaleStrength(int sample, int percent) {  // :434-436
    return sample * (std::min)(percent, 100) / 100;
}

int TriggerEffect::limitCeiling(int sample, int ceilingPercent) {  // :439-450
    const int percent = (std::max)(20, (std::min)(100, ceilingPercent));
    if (percent >= 100 || sample == 0) return sample;
    const double ceiling = 127.0 * percent / 100.0;
    const double knee = ceiling * 0.5;
    const double magnitude = std::abs(static_cast<double>(sample));
    if (magnitude <= knee) return sample;
    const double excess = (magnitude - knee) / (ceiling - knee);
    const double limited = knee + (ceiling - knee) * excess / (1.0 + excess);
    const int rounded = (std::min)(jc::d2i(limited + 0.5), jc::d2i(ceiling + 0.5));
    return sample < 0 ? -rounded : rounded;
}

}  // namespace arbs
