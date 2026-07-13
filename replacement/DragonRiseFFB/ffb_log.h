#pragma once

#include <stdint.h>

// Debug logging for DirectInput → HID translation.
// Enable with /DDRFFB_DEBUG=1 at compile time, or set env DRFFB_LOG=1 at runtime.

void FfbLogInit();
void FfbLogf(const char* fmt, ...);
void FfbLogEffect(const char* stage, uint32_t handle, const char* klass,
                  int32_t magnitude, uint32_t durationUs, uint32_t gain,
                  uint8_t lf, uint8_t hf, uint32_t elapsedUs);
void FfbLogPacket(const char* tag, const uint8_t pkt[8]);

bool FfbLogEnabled();
