#include "effect_map.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// DI_FFNOMINALMAX
static constexpr int32_t kNom = 10000;

static int32_t ClampNom(int32_t v)
{
    if (v < -kNom) return -kNom;
    if (v > kNom) return kNom;
    return v;
}

static int32_t AbsNom(int32_t v)
{
    v = ClampNom(v);
    return v < 0 ? -v : v;
}

static uint8_t ToMotor(int32_t nom /*0..10000*/)
{
    if (nom <= 0) return 0;
    if (nom > kNom) nom = kNom;
    int m = (int)((nom * 254LL) / kNom);
    if (m > 254) m = 254;
    if (nom > 0 && m == 0) m = 1;
    return (uint8_t)m;
}

const char* FfbClassName(FfbEffectClass c)
{
    switch (c) {
    case FfbEffectClass::Constant: return "Constant";
    case FfbEffectClass::Ramp: return "Ramp";
    case FfbEffectClass::Sine: return "Sine";
    case FfbEffectClass::Square: return "Square";
    case FfbEffectClass::Triangle: return "Triangle";
    case FfbEffectClass::SawtoothUp: return "SawtoothUp";
    case FfbEffectClass::SawtoothDown: return "SawtoothDown";
    case FfbEffectClass::Spring: return "Spring";
    case FfbEffectClass::Damper: return "Damper";
    case FfbEffectClass::Inertia: return "Inertia";
    case FfbEffectClass::Friction: return "Friction";
    case FfbEffectClass::Custom: return "Custom";
    default: return "Unknown";
    }
}

FfbEffectClass FfbInferClass(uint32_t cbTypeSpecific, uint32_t internalType)
{
    // Prefer structural size (reliable across clients).
    if (cbTypeSpecific == 4)
        return FfbEffectClass::Constant;
    if (cbTypeSpecific == 8)
        return FfbEffectClass::Ramp;
    if (cbTypeSpecific == 16) {
        // Periodic — refine with internal type id when available.
        switch (internalType & 0xFF) {
        case 2: return FfbEffectClass::Square;
        case 3: return FfbEffectClass::Sine;
        case 4: return FfbEffectClass::Triangle;
        case 5: return FfbEffectClass::SawtoothUp;
        case 6: return FfbEffectClass::SawtoothDown;
        default: return FfbEffectClass::Sine;
        }
    }
    if (cbTypeSpecific >= 24 && cbTypeSpecific < 64) {
        switch (internalType & 0xFF) {
        case 7: return FfbEffectClass::Spring;
        case 8: return FfbEffectClass::Damper;
        case 9: return FfbEffectClass::Inertia;
        case 10: return FfbEffectClass::Friction;
        default: return FfbEffectClass::Spring;
        }
    }
    if (cbTypeSpecific >= 16 && cbTypeSpecific != 16)
        return FfbEffectClass::Custom;

    switch (internalType & 0xFF) {
    case 0: return FfbEffectClass::Constant;
    case 1: return FfbEffectClass::Ramp;
    case 2: return FfbEffectClass::Square;
    case 3: return FfbEffectClass::Sine;
    case 4: return FfbEffectClass::Triangle;
    case 5: return FfbEffectClass::SawtoothUp;
    case 6: return FfbEffectClass::SawtoothDown;
    case 7: return FfbEffectClass::Spring;
    case 8: return FfbEffectClass::Damper;
    case 9: return FfbEffectClass::Inertia;
    case 10: return FfbEffectClass::Friction;
    case 11: return FfbEffectClass::Custom;
    default: return FfbEffectClass::Unknown;
    }
}

void FfbFillFromTypeParams(FfbEffectParams* p, uint32_t cb, const void* typeParams)
{
    if (!p)
        return;
    p->klass = FfbInferClass(cb, 0);
    if (!typeParams || cb < 4)
        return;

    const auto* b = reinterpret_cast<const uint8_t*>(typeParams);
    if (cb == 4) {
        int32_t m = 0;
        std::memcpy(&m, b, 4);
        p->klass = FfbEffectClass::Constant;
        p->magnitude = ClampNom(m);
        return;
    }
    if (cb == 8) {
        int32_t s = 0, e = 0;
        std::memcpy(&s, b, 4);
        std::memcpy(&e, b + 4, 4);
        p->klass = FfbEffectClass::Ramp;
        p->rampStart = ClampNom(s);
        p->rampEnd = ClampNom(e);
        p->magnitude = AbsNom((s / 2) + (e / 2));
        return;
    }
    if (cb == 16) {
        // DIPERIODIC
        uint32_t mag = 0, period = 0;
        int32_t off = 0, phase = 0;
        std::memcpy(&mag, b + 0, 4);
        std::memcpy(&off, b + 4, 4);
        std::memcpy(&phase, b + 8, 4);
        std::memcpy(&period, b + 12, 4);
        if (p->klass < FfbEffectClass::Sine || p->klass > FfbEffectClass::SawtoothDown)
            p->klass = FfbEffectClass::Sine;
        p->magnitude = (int32_t)(std::min)(mag, (uint32_t)kNom);
        p->offset = ClampNom(off);
        p->phase = phase;
        p->periodUs = period ? period : 100000;
        return;
    }
    if (cb >= 24) {
        // DICONDITION (first condition)
        int32_t offset = 0, cp = 0, cn = 0, db = 0;
        uint32_t sp = 0, sn = 0;
        std::memcpy(&offset, b + 0, 4);
        std::memcpy(&cp, b + 4, 4);
        std::memcpy(&cn, b + 8, 4);
        std::memcpy(&sp, b + 12, 4);
        std::memcpy(&sn, b + 16, 4);
        std::memcpy(&db, b + 20, 4);
        if (p->klass < FfbEffectClass::Spring || p->klass > FfbEffectClass::Friction)
            p->klass = FfbEffectClass::Spring;
        p->coeffPos = ClampNom(cp);
        p->coeffNeg = ClampNom(cn);
        p->saturationPos = (std::min)(sp, (uint32_t)kNom);
        p->saturationNeg = (std::min)(sn, (uint32_t)kNom);
        p->deadBand = AbsNom(db);
        p->magnitude = (std::max)(AbsNom(cp), AbsNom(cn));
        return;
    }
    // Fallback: first LONG
    int32_t m = 0;
    std::memcpy(&m, b, 4);
    p->magnitude = ClampNom(m);
    p->klass = FfbEffectClass::Custom;
}

double FfbEnvelopeScale(const FfbEffectParams& p, uint32_t elapsedUs)
{
    // Base gain chain: effect * device → 0..1
    double g = (p.effectGain / 10000.0) * (p.deviceGain / 10000.0);
    if (g < 0) g = 0;
    if (g > 1) g = 1;

    if (!p.envelope.present)
        return g;

    const double attackL = p.envelope.attackLevel / 10000.0;
    const double fadeL = p.envelope.fadeLevel / 10000.0;
    const uint32_t atk = p.envelope.attackTimeUs;
    const uint32_t fade = p.envelope.fadeTimeUs;
    const bool infinite = (p.durationUs == 0xFFFFFFFF || p.durationUs == 0);
    const uint32_t dur = infinite ? 0 : p.durationUs;

    double env = 1.0;
    if (atk > 0 && elapsedUs < atk) {
        // attackLevel → 1.0 over attackTime
        double t = (double)elapsedUs / (double)atk;
        env = attackL + (1.0 - attackL) * t;
    } else if (!infinite && fade > 0 && dur > fade && elapsedUs > dur - fade) {
        double t = (double)(elapsedUs - (dur - fade)) / (double)fade;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        env = 1.0 + (fadeL - 1.0) * t;
    }

    if (env < 0) env = 0;
    if (env > 1) env = 1;
    return env * g;
}

double FfbWaveformSample(const FfbEffectParams& p, uint32_t elapsedUs)
{
    double period = (double)(p.periodUs ? p.periodUs : 100000);
    // phase: DirectInput uses hundredths of a degree (0..35999)
    double phaseFrac = (p.phase % 36000) / 36000.0;
    if (phaseFrac < 0) phaseFrac += 1.0;
    double t = std::fmod((elapsedUs / period) + phaseFrac, 1.0);
    if (t < 0) t += 1.0;

    switch (p.klass) {
    case FfbEffectClass::Sine:
        // full-wave rectified feel for rumble (motors cannot push negative)
        return std::fabs(std::sin(2.0 * M_PI * t));
    case FfbEffectClass::Square:
        return (t < 0.5) ? 1.0 : 0.0;
    case FfbEffectClass::Triangle:
        if (t < 0.5)
            return t * 2.0;
        return (1.0 - t) * 2.0;
    case FfbEffectClass::SawtoothUp:
        return t;
    case FfbEffectClass::SawtoothDown:
        return 1.0 - t;
    default:
        return 1.0;
    }
}

// Split a nominal intensity into LF/HF using effect class + period.
static void SplitLfHf(const FfbEffectParams& p, int32_t nom, uint8_t* lf, uint8_t* hf)
{
    if (nom <= 0) {
        *lf = 0;
        *hf = 0;
        return;
    }

    double lfW = 0.65;
    double hfW = 0.35;

    switch (p.klass) {
    case FfbEffectClass::Constant:
        // Steady thump: mostly large motor, light high buzz.
        lfW = 0.85;
        hfW = 0.30;
        break;
    case FfbEffectClass::Ramp:
        lfW = 0.80;
        hfW = 0.35;
        break;
    case FfbEffectClass::Sine:
    case FfbEffectClass::Triangle:
    case FfbEffectClass::SawtoothUp:
    case FfbEffectClass::SawtoothDown:
    case FfbEffectClass::Square: {
        // Short period → high-frequency motor; long period → low-frequency motor.
        double ms = p.periodUs / 1000.0;
        if (ms <= 30.0) {
            lfW = 0.20;
            hfW = 0.95;
        } else if (ms >= 250.0) {
            lfW = 0.95;
            hfW = 0.25;
        } else {
            // Blend between 30ms and 250ms
            double u = (ms - 30.0) / (250.0 - 30.0);
            lfW = 0.20 + 0.75 * u;
            hfW = 0.95 - 0.70 * u;
        }
        if (p.klass == FfbEffectClass::Square) {
            // Square is harsher → a bit more HF edge.
            hfW = (std::min)(1.0, hfW + 0.10);
        }
        break;
    }
    case FfbEffectClass::Spring:
    case FfbEffectClass::Damper:
    case FfbEffectClass::Inertia:
    case FfbEffectClass::Friction:
        // Condition forces: subtle dual presence — never stick-driven.
        // Softer overall; mostly LF "weight".
        lfW = 0.55;
        hfW = 0.15;
        nom = (int32_t)(nom * 0.45); // conditions are not full rumble blasts
        break;
    case FfbEffectClass::Custom:
        lfW = 0.70;
        hfW = 0.40;
        break;
    default:
        break;
    }

    // Optional cartesian balance: only when BOTH axes non-zero direction
    // and not a pure zero vector. Does NOT read live stick — only effect dir.
    if (p.cAxes >= 2 && (p.dieffFlags & 0x10 /*DIEFF_CARTESIAN*/)) {
        double ax = std::fabs((double)p.dir0);
        double ay = std::fabs((double)p.dir1);
        double sum = ax + ay;
        if (sum > 1.0) {
            // X → HF bias, Y → LF bias (common dual-rumble convention)
            double xw = ax / sum;
            double yw = ay / sum;
            lfW *= (0.35 + 0.65 * yw);
            hfW *= (0.35 + 0.65 * xw);
        }
    }

    int32_t lfNom = (int32_t)(nom * lfW + 0.5);
    int32_t hfNom = (int32_t)(nom * hfW + 0.5);
    if (lfNom > kNom) lfNom = kNom;
    if (hfNom > kNom) hfNom = kNom;
    *lf = ToMotor(lfNom);
    *hf = ToMotor(hfNom);
}

FfbMotorSample FfbSampleMotors(const FfbEffectParams& p, uint32_t elapsedUs)
{
    FfbMotorSample s {};

    if (elapsedUs < p.startDelayUs)
        return s;

    const uint32_t t = elapsedUs - p.startDelayUs;
    const bool infinite = (p.durationUs == 0xFFFFFFFF || p.durationUs == 0);
    if (!infinite && t >= p.durationUs)
        return s;

    double env = FfbEnvelopeScale(p, t);
    int32_t baseNom = 0;

    switch (p.klass) {
    case FfbEffectClass::Constant:
        baseNom = AbsNom(p.magnitude);
        break;
    case FfbEffectClass::Ramp: {
        double u = 0.0;
        if (!infinite && p.durationUs > 0)
            u = (double)t / (double)p.durationUs;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
        double v = p.rampStart + (p.rampEnd - p.rampStart) * u;
        baseNom = AbsNom((int32_t)(v + (v >= 0 ? 0.5 : -0.5)));
        break;
    }
    case FfbEffectClass::Sine:
    case FfbEffectClass::Square:
    case FfbEffectClass::Triangle:
    case FfbEffectClass::SawtoothUp:
    case FfbEffectClass::SawtoothDown: {
        s.waveform = FfbWaveformSample(p, t);
        int32_t mag = AbsNom(p.magnitude);
        int32_t off = AbsNom(p.offset);
        // Offset biases the envelope; waveform modulates magnitude.
        baseNom = (int32_t)(mag * s.waveform + off * 0.25 + 0.5);
        if (baseNom > kNom) baseNom = kNom;
        break;
    }
    case FfbEffectClass::Spring:
    case FfbEffectClass::Damper:
    case FfbEffectClass::Inertia:
    case FfbEffectClass::Friction: {
        // No axis position available in effect driver → coefficient magnitude only.
        // Saturation caps the force.
        int32_t c = (std::max)(AbsNom(p.coeffPos), AbsNom(p.coeffNeg));
        uint32_t sat = (std::max)(p.saturationPos, p.saturationNeg);
        if (sat > 0 && (uint32_t)c > sat)
            c = (int32_t)sat;
        // Deadband reduces gentle conditions slightly.
        if (p.deadBand > 0)
            c = (int32_t)(c * (1.0 - (std::min)(0.5, p.deadBand / 10000.0)));
        baseNom = c;
        break;
    }
    case FfbEffectClass::Custom:
    default:
        baseNom = AbsNom(p.magnitude);
        break;
    }

    int32_t scaled = (int32_t)(baseNom * env + 0.5);
    if (scaled > kNom) scaled = kNom;
    s.envScaled = scaled;
    SplitLfHf(p, scaled, &s.lowFreq, &s.highFreq);
    return s;
}

bool FfbIsValidRumblePacket(const uint8_t pkt[8], uint8_t* outA, uint8_t* outB)
{
    if (!pkt) return false;
    if (pkt[0] != 0x00 || pkt[1] != 0x51 || pkt[2] != 0x00 || pkt[4] != 0x00 ||
        pkt[6] != 0x00 || pkt[7] != 0x00)
        return false;
    if (pkt[3] > 0xFE || pkt[5] > 0xFE)
        return false;
    // outA = strong/LF (byte5), outB = weak/HF (byte3) — semantic, not wire order
    if (outA) *outA = pkt[5];
    if (outB) *outB = pkt[3];
    return true;
}

bool FfbIsValidCommitPacket(const uint8_t pkt[8])
{
    if (!pkt) return false;
    static const uint8_t k[8] = { 0x00, 0xFA, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00 };
    return std::memcmp(pkt, k, 8) == 0;
}

bool FfbIsValidStopPacket(const uint8_t pkt[8])
{
    if (!pkt) return false;
    static const uint8_t k[8] = { 0x00, 0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    return std::memcmp(pkt, k, 8) == 0;
}
