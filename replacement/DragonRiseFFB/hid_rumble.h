#pragma once

#include <windows.h>
#include <stdint.h>

// Optional packet sink for validation / dry-run (no HID required).
typedef void (*FfbPacketSink)(const uint8_t packet[8], void* ctx);

void FfbSetPacketSink(FfbPacketSink sink, void* ctx);
void FfbClearPacketSink();

// DragonRise / Speedlink VID_0079&PID_0006 rumble HID packets.
// Matches Windows GenericFFBDriver + Linux drivers/hid/hid-dr.c (0079:0006):
//
// Active update:
//   00 51 00 <weak/HF> 00 <strong/LF> 00 00
//   00 FA FE 00 00 00 00 00   (commit / enable)
// Stop:
//   00 F3 00 00 00 00 00 00
//
// Linux maps: field[2]=weak (small/HF), field[4]=strong (large/LF).
// With report-id 0 that is wire bytes 3 and 5 respectively.

class HidRumbleDevice {
public:
    HidRumbleDevice();
    ~HidRumbleDevice();

    HRESULT Open(const wchar_t* deviceInterfacePath);
    // Dry-run: no CreateFile; packets go to sink only (validation suite).
    void OpenDryRun();
    void Close();
    bool IsOpen() const { return dryRun_ || handle_ != INVALID_HANDLE_VALUE; }
    bool IsDryRun() const { return dryRun_; }

    // Intensity range 0..254 (0xFE), matching original driver / Linux hid-dr.
    // lowFreq  = strong/large motor  → wire byte 5
    // highFreq = weak/small motor    → wire byte 3
    HRESULT SendRumble(uint8_t lowFreq, uint8_t highFreq);
    HRESULT Stop();

    const wchar_t* Path() const { return path_; }

    // Last transmitted semantic intensities (LF, HF) — not raw wire order.
    uint8_t LastA() const { return lastA_; } // LF / strong
    uint8_t LastB() const { return lastB_; } // HF / weak

private:
    HRESULT SendReport(const uint8_t report[8]);

    HANDLE handle_;
    bool dryRun_;
    wchar_t path_[MAX_PATH * 2];
    uint8_t lastA_;
    uint8_t lastB_;
    bool lastWasStop_;
};
