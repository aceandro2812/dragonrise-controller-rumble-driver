#include "hid_rumble.h"
#include "effect_map.h"
#include "ffb_log.h"

#include <hidsdi.h>

#pragma comment(lib, "hid.lib")

static FfbPacketSink g_sink = nullptr;
static void* g_sinkCtx = nullptr;

void FfbSetPacketSink(FfbPacketSink sink, void* ctx)
{
    g_sink = sink;
    g_sinkCtx = ctx;
}

void FfbClearPacketSink()
{
    g_sink = nullptr;
    g_sinkCtx = nullptr;
}

HidRumbleDevice::HidRumbleDevice()
    : handle_(INVALID_HANDLE_VALUE)
    , dryRun_(false)
    , lastA_(0)
    , lastB_(0)
    , lastWasStop_(true)
{
    path_[0] = L'\0';
}

HidRumbleDevice::~HidRumbleDevice()
{
    Close();
}

void HidRumbleDevice::OpenDryRun()
{
    Close();
    dryRun_ = true;
    wcsncpy_s(path_, L"(dry-run)", _TRUNCATE);
    lastA_ = 0;
    lastB_ = 0;
    lastWasStop_ = true;
}

HRESULT HidRumbleDevice::Open(const wchar_t* deviceInterfacePath)
{
    if (!deviceInterfacePath || !deviceInterfacePath[0])
        return E_INVALIDARG;

    Close();
    dryRun_ = false;
    wcsncpy_s(path_, deviceInterfacePath, _TRUNCATE);

    handle_ = CreateFileW(
        path_,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (handle_ == INVALID_HANDLE_VALUE) {
        handle_ = CreateFileW(
            path_,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
    }

    if (handle_ == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    lastA_ = 0;
    lastB_ = 0;
    lastWasStop_ = true;
    FfbLogf("HID open %ls", path_);
    return S_OK;
}

void HidRumbleDevice::Close()
{
    if (handle_ != INVALID_HANDLE_VALUE || dryRun_) {
        uint8_t stop[8] = { 0x00, 0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        if (handle_ != INVALID_HANDLE_VALUE)
            HidD_SetOutputReport(handle_, stop, 8);
        if (g_sink)
            g_sink(stop, g_sinkCtx);
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    dryRun_ = false;
    path_[0] = L'\0';
}

HRESULT HidRumbleDevice::SendReport(const uint8_t report[8])
{
    FfbLogPacket("HID", report);

    // Always validate format before wire / sink.
    bool okFmt = FfbIsValidRumblePacket(report) || FfbIsValidCommitPacket(report) ||
                 FfbIsValidStopPacket(report);
    if (!okFmt) {
        FfbLogf("REJECT invalid HID packet");
        return E_INVALIDARG;
    }

    if (g_sink)
        g_sink(report, g_sinkCtx);

    if (dryRun_)
        return S_OK;

    if (handle_ == INVALID_HANDLE_VALUE)
        return E_HANDLE;

    if (!HidD_SetOutputReport(handle_, (PVOID)report, 8))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

HRESULT HidRumbleDevice::Stop()
{
    if (lastWasStop_)
        return S_OK;

    const uint8_t stop[8] = { 0x00, 0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    HRESULT hr = SendReport(stop);
    if (SUCCEEDED(hr)) {
        lastA_ = 0;
        lastB_ = 0;
        lastWasStop_ = true;
    }
    return hr;
}

HRESULT HidRumbleDevice::SendRumble(uint8_t lowFreq, uint8_t highFreq)
{
    if (lowFreq == 0 && highFreq == 0)
        return Stop();

    if (lowFreq > 0xFE) lowFreq = 0xFE;
    if (highFreq > 0xFE) highFreq = 0xFE;

    // Linux hid-dr quirk: weak==0x0A runs strong nearly at max — bump past it.
    if (highFreq == 0x0A)
        highFreq = 0x0B;

    // Do NOT skip identical intensities. Stock GenericFFBDriver re-sends about
    // every 10ms; on many DragonRise clones the motors decay without refresh,
    // so "same value" still needs a new HID report pair.

    // Wire: 00 51 00 <weak/HF> 00 <strong/LF> 00 00  (Linux hid-dr field[2]/[4])
    const uint8_t rumble[8] = {
        0x00, 0x51, 0x00, highFreq, 0x00, lowFreq, 0x00, 0x00
    };
    HRESULT hr = SendReport(rumble);
    if (FAILED(hr))
        return hr;

    const uint8_t commit[8] = {
        0x00, 0xFA, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    hr = SendReport(commit);
    if (SUCCEEDED(hr)) {
        lastA_ = lowFreq;
        lastB_ = highFreq;
        lastWasStop_ = false;
    }
    return hr;
}
