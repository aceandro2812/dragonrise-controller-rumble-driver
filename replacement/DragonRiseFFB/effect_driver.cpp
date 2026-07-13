#include "effect_driver.h"
#include "ffb_log.h"

#include <algorithm>

static bool g_forceDryRun = false;

extern "C" void FfbEnableDriverDryRun(bool enable)
{
    g_forceDryRun = enable;
}

EffectDriver::EffectDriver()
    : refCount_(1)
    , deviceId_(0)
    , deviceGain_(10000)
    , actuatorsOn_(true)
    , paused_(false)
    , nextHandle_(1)
    , worker_(nullptr)
    , workerStop_(nullptr)
{
    FfbLogInit();
    workerStop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    InterlockedIncrement(&g_objCount);
    FfbLogf("EffectDriver ctor objCount=%ld", g_objCount);
}

EffectDriver::~EffectDriver()
{
    StopWorker();
    if (workerStop_) {
        CloseHandle(workerStop_);
        workerStop_ = nullptr;
    }
    hid_.Close();
    InterlockedDecrement(&g_objCount);
    FfbLogf("EffectDriver dtor objCount=%ld", g_objCount);
}

void EffectDriver::EnsureWorkerLocked()
{
    if (worker_ || !workerStop_)
        return;
    ResetEvent(workerStop_);
    worker_ = CreateThread(nullptr, 0, WorkerThunk, this, 0, nullptr);
}

void EffectDriver::StopWorker()
{
    HANDLE thr = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        thr = worker_;
        worker_ = nullptr;
        if (workerStop_)
            SetEvent(workerStop_);
    }
    if (thr) {
        WaitForSingleObject(thr, 3000);
        CloseHandle(thr);
    }
}

DWORD WINAPI EffectDriver::WorkerThunk(LPVOID param)
{
    reinterpret_cast<EffectDriver*>(param)->WorkerLoop();
    return 0;
}

void EffectDriver::WorkerLoop()
{
    for (;;) {
        DWORD wait = WaitForSingleObject(workerStop_, 10);
        if (wait == WAIT_OBJECT_0)
            break;
        std::lock_guard<std::mutex> lock(mu_);
        AggregateAndSendLocked();
    }
}

STDMETHODIMP EffectDriver::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDirectInputEffectDriver) {
        *ppv = static_cast<IDirectInputEffectDriver*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EffectDriver::AddRef()
{
    return (ULONG)InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) EffectDriver::Release()
{
    LONG c = InterlockedDecrement(&refCount_);
    if (c == 0)
        delete this;
    return (ULONG)c;
}

STDMETHODIMP EffectDriver::DeviceID(DWORD dwDirectInputVersion, DWORD dwExternalID,
                                    DWORD fBegin, DWORD dwInternalID, LPVOID pvData)
{
    FfbLogf("DeviceID enter ver=0x%X ext=%u begin=%u id=%u pv=%p",
            dwDirectInputVersion, dwExternalID, fBegin, dwInternalID, pvData);

    std::lock_guard<std::mutex> lock(mu_);

    if (fBegin) {
        deviceId_ = dwInternalID;
        if (g_forceDryRun) {
            hid_.OpenDryRun();
            EnsureWorkerLocked();
            FfbLogf("DeviceID dry-run deviceId=%u", dwInternalID);
        } else if (pvData) {
            auto* init = reinterpret_cast<DIHIDFFINITINFO*>(pvData);
            FfbLogf("DeviceID init dwSize=%u expect=%zu iface=%p",
                    init->dwSize, sizeof(DIHIDFFINITINFO), init->pwszDeviceInterface);
            // Be tolerant: some hosts pass smaller structs; only require a path pointer.
            if (init->pwszDeviceInterface && init->pwszDeviceInterface[0]) {
                HRESULT hr = hid_.Open(init->pwszDeviceInterface);
                if (SUCCEEDED(hr))
                    EnsureWorkerLocked();
                FfbLogf("DeviceID open hr=0x%08lX path=%ls", (unsigned long)hr,
                        init->pwszDeviceInterface);
            } else {
                FfbLogf("DeviceID: no device interface path yet");
            }
        } else {
            FfbLogf("DeviceID: pvData null on begin");
        }
    } else {
        effects_.clear();
        StopWorker(); // stop thread before closing HID
        // recreate stop event for a future attach
        if (!workerStop_)
            workerStop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        else
            ResetEvent(workerStop_);
        hid_.Close();
        deviceId_ = 0;
        FfbLogf("DeviceID detach");
    }
    FfbLogf("DeviceID leave");
    return S_OK;
}

STDMETHODIMP EffectDriver::GetVersions(LPDIDRIVERVERSIONS pversions)
{
    if (!pversions || pversions->dwSize < sizeof(DIDRIVERVERSIONS))
        return DIERR_INVALIDPARAM;
    pversions->dwFirmwareRevision = 1;
    pversions->dwHardwareRevision = 1;
    pversions->dwFFDriverVersion = 0x00010001; // 1.1 fixed
    return S_OK;
}

STDMETHODIMP EffectDriver::Escape(DWORD, DWORD, LPDIEFFESCAPE)
{
    return DIERR_UNSUPPORTED;
}

STDMETHODIMP EffectDriver::SetGain(DWORD /*dwId*/, DWORD dwGain)
{
    std::lock_guard<std::mutex> lock(mu_);
    if (dwGain > 10000)
        dwGain = 10000;
    deviceGain_ = dwGain;
    for (auto& kv : effects_)
        kv.second.params.deviceGain = deviceGain_;
    AggregateAndSendLocked();
    FfbLogf("SetGain %u", dwGain);
    return S_OK;
}

STDMETHODIMP EffectDriver::SendForceFeedbackCommand(DWORD /*dwId*/, DWORD dwCommand)
{
    std::lock_guard<std::mutex> lock(mu_);
    FfbLogf("SendForceFeedbackCommand 0x%X", dwCommand);

    switch (dwCommand) {
    case DISFFC_RESET:
        effects_.clear();
        paused_ = false;
        actuatorsOn_ = true;
        hid_.Stop();
        break;
    case DISFFC_STOPALL:
        for (auto& kv : effects_)
            kv.second.playing = false;
        AggregateAndSendLocked();
        break;
    case DISFFC_PAUSE:
        paused_ = true;
        hid_.Stop();
        break;
    case DISFFC_CONTINUE:
        paused_ = false;
        AggregateAndSendLocked();
        break;
    case DISFFC_SETACTUATORSON:
        actuatorsOn_ = true;
        AggregateAndSendLocked();
        break;
    case DISFFC_SETACTUATORSOFF:
        actuatorsOn_ = false;
        hid_.Stop();
        break;
    default:
        return DIERR_INVALIDPARAM;
    }
    return S_OK;
}

STDMETHODIMP EffectDriver::GetForceFeedbackState(DWORD /*dwId*/, LPDIDEVICESTATE pds)
{
    // DI sometimes probes with minimal size; never read/write past what caller provided.
    if (!pds || pds->dwSize < 3 * sizeof(DWORD))
        return DIERR_INVALIDPARAM;

    std::lock_guard<std::mutex> lock(mu_);
    ExpireEffectsLocked(GetTickCount());

    DWORD state = DIGFFS_POWERON;
    if (actuatorsOn_)
        state |= DIGFFS_ACTUATORSON;
    else
        state |= DIGFFS_ACTUATORSOFF;
    if (paused_)
        state |= DIGFFS_PAUSED;
    if (effects_.empty())
        state |= DIGFFS_EMPTY;

    bool anyPlaying = false;
    for (const auto& kv : effects_) {
        if (kv.second.playing) {
            anyPlaying = true;
            break;
        }
    }
    if (!anyPlaying)
        state |= DIGFFS_STOPPED;

    pds->dwState = state;
    pds->dwLoad = anyPlaying ? 1 : 0;
    FfbLogf("GetForceFeedbackState state=0x%X load=%u", state, pds->dwLoad);
    return S_OK;
}

void EffectDriver::ApplyDieffectToParams(const DIEFFECT* peff, DWORD internalType, DWORD flags,
                                         FfbEffectParams* io) const
{
    if (!peff || !io)
        return;

    io->deviceGain = deviceGain_;

    if (flags & DIEP_GAIN || io->effectGain == 0)
        io->effectGain = peff->dwGain ? peff->dwGain : 10000;
    if (io->effectGain > 10000)
        io->effectGain = 10000;

    if (flags & DIEP_DURATION || io->durationUs == 0) {
        if (peff->dwDuration == INFINITE || peff->dwDuration == 0xFFFFFFFF)
            io->durationUs = 0xFFFFFFFF;
        else
            io->durationUs = peff->dwDuration;
    }

    if ((flags & DIEP_STARTDELAY) && peff->dwSize >= sizeof(DIEFFECT))
        io->startDelayUs = peff->dwStartDelay;

    if (flags & DIEP_AXES || flags & DIEP_ALLPARAMS) {
        io->cAxes = peff->cAxes;
        io->dieffFlags = peff->dwFlags;
    }
    if ((flags & DIEP_DIRECTION || flags & DIEP_ALLPARAMS) && peff->rglDirection) {
        if (peff->cAxes >= 1)
            io->dir0 = peff->rglDirection[0];
        if (peff->cAxes >= 2)
            io->dir1 = peff->rglDirection[1];
        io->dieffFlags = peff->dwFlags;
    }

    if (flags & DIEP_ENVELOPE || flags & DIEP_ALLPARAMS) {
        if (peff->lpEnvelope && peff->lpEnvelope->dwSize >= sizeof(DIENVELOPE)) {
            io->envelope.present = true;
            io->envelope.attackLevel = peff->lpEnvelope->dwAttackLevel;
            io->envelope.attackTimeUs = peff->lpEnvelope->dwAttackTime;
            io->envelope.fadeLevel = peff->lpEnvelope->dwFadeLevel;
            io->envelope.fadeTimeUs = peff->lpEnvelope->dwFadeTime;
        } else if (flags & DIEP_ENVELOPE) {
            io->envelope.present = false;
        }
    }

    if (flags & DIEP_TYPESPECIFICPARAMS || flags & DIEP_ALLPARAMS ||
        io->klass == FfbEffectClass::Unknown) {
        // Preserve periodic subclass if internal type known.
        FfbEffectClass hint = FfbInferClass(peff->cbTypeSpecificParams, internalType);
        FfbFillFromTypeParams(io, peff->cbTypeSpecificParams, peff->lpvTypeSpecificParams);
        if (hint != FfbEffectClass::Unknown && hint != FfbEffectClass::Constant)
            io->klass = hint;
        // Re-apply internal type for periodic subtypes after fill.
        if (peff->cbTypeSpecificParams == 16)
            io->klass = FfbInferClass(16, internalType);
        if (peff->cbTypeSpecificParams >= 24 && peff->cbTypeSpecificParams < 64)
            io->klass = FfbInferClass(peff->cbTypeSpecificParams, internalType);
    }
}

DWORD EffectDriver::AllocateHandleLocked()
{
    for (;;) {
        DWORD h = nextHandle_++;
        if (h == 0)
            continue;
        if (effects_.find(h) == effects_.end())
            return h;
    }
}

void EffectDriver::ExpireEffectsLocked(DWORD now)
{
    for (auto& kv : effects_) {
        auto& e = kv.second;
        if (!e.playing)
            continue;
        if (e.params.durationUs == 0xFFFFFFFF || e.params.durationUs == 0)
            continue;
        DWORD elapsedMs = now - e.startTick;
        DWORD totalMs = (e.params.startDelayUs / 1000) + (e.params.durationUs / 1000);
        if (totalMs == 0)
            totalMs = 1;
        if (elapsedMs >= totalMs) {
            if (e.iterations == 0xFFFFFFFF || e.iterations > 1) {
                if (e.iterations != 0xFFFFFFFF)
                    e.iterations--;
                e.startTick = now;
            } else {
                e.playing = false;
                FfbLogf("effect %u expired", e.handle);
            }
        }
    }
}

void EffectDriver::AggregateAndSendLocked()
{
    if (!hid_.IsOpen())
        return;
    if (!actuatorsOn_ || paused_) {
        hid_.Stop();
        return;
    }

    DWORD now = GetTickCount();
    ExpireEffectsLocked(now);

    uint8_t maxLf = 0, maxHf = 0;
    for (auto& kv : effects_) {
        auto& e = kv.second;
        if (!e.playing || !e.downloaded)
            continue;

        e.params.deviceGain = deviceGain_;
        DWORD elapsedMs = now - e.startTick;
        uint32_t elapsedUs = elapsedMs * 1000u;
        FfbMotorSample s = FfbSampleMotors(e.params, elapsedUs);

        FfbLogEffect("sample", e.handle, FfbClassName(e.params.klass),
                     e.params.magnitude, e.params.durationUs, e.params.effectGain,
                     s.lowFreq, s.highFreq, elapsedUs);

        if (s.lowFreq > maxLf) maxLf = s.lowFreq;
        if (s.highFreq > maxHf) maxHf = s.highFreq;
    }

    hid_.SendRumble(maxLf, maxHf);
}

STDMETHODIMP EffectDriver::DownloadEffect(DWORD /*dwId*/, DWORD dwInternalEffectType,
                                          LPDWORD pdwEffect, LPCDIEFFECT peff, DWORD dwFlags)
{
    if (!pdwEffect || !peff)
        return DIERR_INVALIDPARAM;
    if (peff->dwSize < sizeof(DIEFFECT_DX5))
        return DIERR_INVALIDPARAM;

    std::lock_guard<std::mutex> lock(mu_);

    DWORD handle = *pdwEffect;
    if (handle == 0) {
        handle = AllocateHandleLocked();
        *pdwEffect = handle;
    }

    EffectState& e = effects_[handle];
    e.handle = handle;
    e.downloaded = true;

    // On first download, set defaults then apply flags.
    if (e.params.klass == FfbEffectClass::Unknown && !(dwFlags & DIEP_TYPESPECIFICPARAMS) &&
        !(dwFlags & DIEP_ALLPARAMS)) {
        // Still try to parse if params present.
        dwFlags |= DIEP_TYPESPECIFICPARAMS | DIEP_DURATION | DIEP_GAIN;
    }

    ApplyDieffectToParams(peff, dwInternalEffectType, dwFlags | DIEP_ALLPARAMS, &e.params);

    FfbMotorSample preview = FfbSampleMotors(e.params, e.params.startDelayUs);
    FfbLogEffect("DownloadEffect", handle, FfbClassName(e.params.klass),
                 e.params.magnitude, e.params.durationUs, e.params.effectGain,
                 preview.lowFreq, preview.highFreq, 0);

    if (dwFlags & DIEP_START) {
        e.playing = true;
        e.startTick = GetTickCount();
        e.iterations = 1;
    } else if (e.playing && !(dwFlags & DIEP_NORESTART)) {
        // Parameter update while playing — keep envelope clock.
    }

    // x360ce and some hosts CreateEffect+Download without DIEP_START, then call
    // Start() which may not reach us. If a non-zero effect is fully specified,
    // arm it so the worker can emit HID (StartEffect still works when called).
    if (!e.playing && e.params.magnitude != 0 && e.params.klass != FfbEffectClass::Unknown) {
        e.playing = true;
        e.startTick = GetTickCount();
        e.iterations = 1;
    }

    EnsureWorkerLocked();
    AggregateAndSendLocked();
    return S_OK;
}

STDMETHODIMP EffectDriver::DestroyEffect(DWORD /*dwId*/, DWORD dwEffect)
{
    std::lock_guard<std::mutex> lock(mu_);
    effects_.erase(dwEffect);
    AggregateAndSendLocked();
    FfbLogf("DestroyEffect %u", dwEffect);
    return S_OK;
}

STDMETHODIMP EffectDriver::StartEffect(DWORD /*dwId*/, DWORD dwEffect, DWORD dwMode, DWORD dwCount)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = effects_.find(dwEffect);
    if (it == effects_.end() || !it->second.downloaded)
        return DIERR_INVALIDPARAM;

    if (dwMode & DIES_SOLO) {
        for (auto& kv : effects_)
            kv.second.playing = false;
    }

    it->second.playing = true;
    it->second.startTick = GetTickCount();
    it->second.iterations = (dwCount == 0) ? 1 : dwCount;
    EnsureWorkerLocked();
    AggregateAndSendLocked();
    FfbLogf("StartEffect %u mode=0x%X count=%u", dwEffect, dwMode, dwCount);
    return S_OK;
}

STDMETHODIMP EffectDriver::StopEffect(DWORD /*dwId*/, DWORD dwEffect)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = effects_.find(dwEffect);
    if (it == effects_.end())
        return DIERR_INVALIDPARAM;
    it->second.playing = false;
    AggregateAndSendLocked();
    FfbLogf("StopEffect %u", dwEffect);
    return S_OK;
}

STDMETHODIMP EffectDriver::GetEffectStatus(DWORD /*dwId*/, DWORD dwEffect, LPDWORD pdwStatus)
{
    if (!pdwStatus)
        return E_POINTER;
    std::lock_guard<std::mutex> lock(mu_);
    ExpireEffectsLocked(GetTickCount());
    auto it = effects_.find(dwEffect);
    if (it == effects_.end())
        return DIERR_INVALIDPARAM;
    *pdwStatus = it->second.playing ? DIEGES_PLAYING : 0;
    return S_OK;
}

// --- ClassFactory ---

ClassFactory::ClassFactory() : refCount_(1)
{
    InterlockedIncrement(&g_objCount);
}

ClassFactory::~ClassFactory()
{
    InterlockedDecrement(&g_objCount);
}

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef()
{
    return (ULONG)InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) ClassFactory::Release()
{
    LONG c = InterlockedDecrement(&refCount_);
    if (c == 0)
        delete this;
    return (ULONG)c;
}

STDMETHODIMP ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv)
{
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;

    EffectDriver* drv = new (std::nothrow) EffectDriver();
    if (!drv)
        return E_OUTOFMEMORY;
    HRESULT hr = drv->QueryInterface(riid, ppv);
    drv->Release();
    return hr;
}

STDMETHODIMP ClassFactory::LockServer(BOOL fLock)
{
    if (fLock)
        InterlockedIncrement(&g_serverLocks);
    else
        InterlockedDecrement(&g_serverLocks);
    return S_OK;
}
