#pragma once

#include <stdint.h>

// Pure translation layer: DirectInput-style effect params → dual rumble motors.
// Motor A (byte 3) = low-frequency / large
// Motor B (byte 5) = high-frequency / small
// Intensities are 0..254 (matches GenericFFBDriver cap 0xFE).

enum class FfbEffectClass : int {
    Unknown = 0,
    Constant,
    Ramp,
    Sine,
    Square,
    Triangle,
    SawtoothUp,
    SawtoothDown,
    Spring,
    Damper,
    Inertia,
    Friction,
    Custom,
};

struct FfbEnvelope {
    bool present = false;
    uint32_t attackLevel = 0;   // 0..10000
    uint32_t attackTimeUs = 0;
    uint32_t fadeLevel = 0;
    uint32_t fadeTimeUs = 0;
};

struct FfbEffectParams {
    FfbEffectClass klass = FfbEffectClass::Unknown;

    // Common
    uint32_t durationUs = 0xFFFFFFFF; // INFINITE
    uint32_t startDelayUs = 0;
    uint32_t effectGain = 10000;      // 0..10000
    uint32_t deviceGain = 10000;      // 0..10000
    uint32_t dieffFlags = 0;
    uint32_t cAxes = 0;
    int32_t dir0 = 0;
    int32_t dir1 = 0;

    // Constant / shared magnitude in DI units (can be signed for constant)
    int32_t magnitude = 0;            // 0..10000 after abs, or signed for constant
    // Ramp
    int32_t rampStart = 0;
    int32_t rampEnd = 0;
    // Periodic
    uint32_t periodUs = 100000;       // default 100ms
    int32_t phase = 0;                // DI_DEGREES * 100? actually 0..35999 centidegrees
    int32_t offset = 0;
    // Condition
    int32_t coeffPos = 0;
    int32_t coeffNeg = 0;
    uint32_t saturationPos = 10000;
    uint32_t saturationNeg = 10000;
    int32_t deadBand = 0;

    FfbEnvelope envelope;
};

struct FfbMotorSample {
    uint8_t lowFreq = 0;   // strong / large motor (wire byte 5)
    uint8_t highFreq = 0;  // weak / small motor (wire byte 3)
    int32_t envScaled = 0; // 0..10000 after envelope*gains
    double waveform = 0;   // 0..1 sample of periodic shape
};

// Infer class from type-specific param size + driver internal type id.
FfbEffectClass FfbInferClass(uint32_t cbTypeSpecific, uint32_t internalType);

// Fill params from raw DIEFFECT fields (caller provides already-copied scalars).
void FfbFillFromTypeParams(FfbEffectParams* p, uint32_t cb, const void* typeParams);

// Envelope multiplier at elapsedUs into the effect (after start delay), 0..1.
double FfbEnvelopeScale(const FfbEffectParams& p, uint32_t elapsedUs);

// Waveform sample 0..1 for periodic classes at elapsedUs.
double FfbWaveformSample(const FfbEffectParams& p, uint32_t elapsedUs);

// Instantaneous motor intensities at elapsedUs (from effect start, including delay phase).
// During start delay, both motors are 0.
FfbMotorSample FfbSampleMotors(const FfbEffectParams& p, uint32_t elapsedUs);

// Validate HID rumble / stop / commit packets (format only).
bool FfbIsValidRumblePacket(const uint8_t pkt[8], uint8_t* outA = nullptr, uint8_t* outB = nullptr);
bool FfbIsValidCommitPacket(const uint8_t pkt[8]);
bool FfbIsValidStopPacket(const uint8_t pkt[8]);

const char* FfbClassName(FfbEffectClass c);
