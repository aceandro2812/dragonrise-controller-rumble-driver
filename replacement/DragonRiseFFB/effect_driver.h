#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <dinputd.h>

#include <map>
#include <mutex>

#include "effect_map.h"
#include "hid_rumble.h"

// Stock GenericFFBDriver CLSID (drop-in replacement).
// {0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
EXTERN_C const GUID CLSID_GenericFFBDriver;

// Test hook: force dry-run HID (no hardware) for validation suite.
extern "C" void FfbEnableDriverDryRun(bool enable);

class EffectDriver : public IDirectInputEffectDriver {
public:
    EffectDriver();
    virtual ~EffectDriver();

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(DeviceID)(DWORD dwDirectInputVersion, DWORD dwExternalID, DWORD fBegin,
                        DWORD dwInternalID, LPVOID pvData) override;
    STDMETHOD(GetVersions)(LPDIDRIVERVERSIONS pversions) override;
    STDMETHOD(Escape)(DWORD dwId, DWORD dwEffect, LPDIEFFESCAPE pesc) override;
    STDMETHOD(SetGain)(DWORD dwId, DWORD dwGain) override;
    STDMETHOD(SendForceFeedbackCommand)(DWORD dwId, DWORD dwCommand) override;
    STDMETHOD(GetForceFeedbackState)(DWORD dwId, LPDIDEVICESTATE pds) override;
    STDMETHOD(DownloadEffect)(DWORD dwId, DWORD dwInternalEffectType, LPDWORD pdwEffect,
                              LPCDIEFFECT peff, DWORD dwFlags) override;
    STDMETHOD(DestroyEffect)(DWORD dwId, DWORD dwEffect) override;
    STDMETHOD(StartEffect)(DWORD dwId, DWORD dwEffect, DWORD dwMode, DWORD dwCount) override;
    STDMETHOD(StopEffect)(DWORD dwId, DWORD dwEffect) override;
    STDMETHOD(GetEffectStatus)(DWORD dwId, DWORD dwEffect, LPDWORD pdwStatus) override;

private:
    struct EffectState {
        DWORD handle = 0;
        bool downloaded = false;
        bool playing = false;
        DWORD iterations = 1;
        DWORD startTick = 0; // GetTickCount when started
        FfbEffectParams params;
    };

    void ApplyDieffectToParams(const DIEFFECT* peff, DWORD internalType, DWORD flags,
                               FfbEffectParams* io) const;
    void AggregateAndSendLocked();
    void ExpireEffectsLocked(DWORD now);
    DWORD AllocateHandleLocked();
    void EnsureWorkerLocked();
    void StopWorker();
    static DWORD WINAPI WorkerThunk(LPVOID param);
    void WorkerLoop();

    LONG refCount_;
    mutable std::mutex mu_;
    HidRumbleDevice hid_;
    DWORD deviceId_;
    DWORD deviceGain_;
    bool actuatorsOn_;
    bool paused_;
    DWORD nextHandle_;
    std::map<DWORD, EffectState> effects_;
    HANDLE worker_;
    HANDLE workerStop_;
};

class ClassFactory : public IClassFactory {
public:
    ClassFactory();
    virtual ~ClassFactory();

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(CreateInstance)(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    STDMETHOD(LockServer)(BOOL fLock) override;

private:
    LONG refCount_;
};

extern LONG g_serverLocks;
extern LONG g_objCount;
extern HINSTANCE g_hInst;
