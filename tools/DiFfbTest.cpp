// DirectInput Force Feedback smoke test for generic pads (x360ce path).
// cl /O2 /EHsc /DUNICODE /D_UNICODE DiFfbTest.cpp /Fe:DiFfbTest.exe dinput8.lib dxguid.lib ole32.lib user32.lib

#define DIRECTINPUT_VERSION 0x0800
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <stdio.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

static GUID g_devGuid;
static bool g_found = false;
static wchar_t g_name[260];

static void PH(const char* w, HRESULT hr)
{
    printf("  %-32s 0x%08lX%s\n", w, (unsigned long)hr, SUCCEEDED(hr) ? " OK" : "");
    fflush(stdout);
}

static BOOL CALLBACK EnumCb(const DIDEVICEINSTANCEW* di, VOID*)
{
    DWORD t = GET_DIDEVICE_TYPE(di->dwDevType);
    wprintf(L"  [%s] type=%u  %s\n", di->tszInstanceName, t, di->tszProductName);
    if (t != DI8DEVTYPE_JOYSTICK && t != DI8DEVTYPE_GAMEPAD &&
        t != DI8DEVTYPE_1STPERSON && t != DI8DEVTYPE_DRIVING)
        return DIENUM_CONTINUE;
    // Prefer anything; last matching joystick wins if multiple — take first
    if (!g_found) {
        g_devGuid = di->guidInstance;
        wcsncpy_s(g_name, di->tszInstanceName, _TRUNCATE);
        g_found = true;
    }
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK EnumAxisCb(const DIDEVICEOBJECTINSTANCEW* o, VOID* ctx)
{
    if (o->dwType & DIDFT_FFACTUATOR) {
        wprintf(L"  FFACTUATOR: %s\n", o->tszName);
        int* n = (int*)ctx;
        (*n)++;
    }
    return DIENUM_CONTINUE;
}

int wmain()
{
    SetEnvironmentVariableW(L"DRFFB_LOG", L"1");
    printf("DirectInput FFB test (%s)\n\n", sizeof(void*) == 8 ? "x64" : "x86");

    HRESULT hr = CoInitialize(nullptr);
    PH("CoInitialize", hr);

    IDirectInput8W* di = nullptr;
    hr = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                            IID_IDirectInput8W, (void**)&di, nullptr);
    PH("DirectInput8Create", hr);
    if (FAILED(hr) || !di)
        return 2;

    printf("Devices:\n");
    hr = di->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumCb, nullptr, DIEDFL_ATTACHEDONLY);
    PH("EnumDevices", hr);
    if (!g_found) {
        printf("No game controller attached.\n");
        di->Release();
        return 3;
    }
    wprintf(L"\nUsing: %s\n", g_name);

    IDirectInputDevice8W* dev = nullptr;
    hr = di->CreateDevice(g_devGuid, &dev, nullptr);
    PH("CreateDevice", hr);
    if (FAILED(hr) || !dev) {
        di->Release();
        return 4;
    }

    hr = dev->SetDataFormat(&c_dfDIJoystick2);
    PH("SetDataFormat", hr);

    // Hidden owner window — GetConsoleWindow() is NULL when stdout is redirected.
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"DiFfbTest", WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    hr = dev->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    PH("SetCooperativeLevel", hr);

    hr = dev->Acquire();
    PH("Acquire", hr);

    printf("calling GetCapabilities sizeof=%zu\n", sizeof(DIDEVCAPS));
    fflush(stdout);

    DIDEVCAPS caps;
    ZeroMemory(&caps, sizeof(caps));
    caps.dwSize = sizeof(DIDEVCAPS);
    hr = dev->GetCapabilities(&caps);
    PH("GetCapabilities", hr);
    printf("  axes=%u buttons=%u flags=0x%08X FF=%s\n",
           caps.dwAxes, caps.dwButtons, caps.dwFlags,
           (caps.dwFlags & DIDC_FORCEFEEDBACK) ? "YES" : "NO");

    int nAct = 0;
    printf("FF actuators:\n");
    dev->EnumObjects(EnumAxisCb, &nAct, DIDFT_AXIS);
    printf("  count=%d\n", nAct);

    if (!(caps.dwFlags & DIDC_FORCEFEEDBACK)) {
        printf("\n*** NO DIDC_FORCEFEEDBACK — DirectInput/x360ce cannot use FFB. ***\n");
        printf("Reinstall patched MSI / check OEMForceFeedback CLSID.\n");
        dev->Unacquire();
        dev->Release();
        di->Release();
        return 5;
    }

    DWORD axes[2] = { DIJOFS_X, DIJOFS_Y };
    LONG dirs[2] = { 1, 0 };
    DICONSTANTFORCE cf;
    cf.lMagnitude = 10000;

    DIEFFECT eff;
    ZeroMemory(&eff, sizeof(eff));
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwGain = 10000;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.cAxes = (nAct >= 2) ? 2 : 1;
    eff.rgdwAxes = axes;
    eff.rglDirection = dirs;
    eff.cbTypeSpecificParams = sizeof(cf);
    eff.lpvTypeSpecificParams = &cf;

    IDirectInputEffect* effect = nullptr;
    hr = dev->CreateEffect(GUID_ConstantForce, &eff, &effect, nullptr);
    PH("CreateEffect(ConstantForce)", hr);

    if (FAILED(hr) || !effect) {
        // try without OBJECTOFFSETS
        eff.dwFlags = DIEFF_CARTESIAN;
        hr = dev->CreateEffect(GUID_ConstantForce, &eff, &effect, nullptr);
        PH("CreateEffect retry", hr);
    }

    if (FAILED(hr) || !effect) {
        printf("\nCreateEffect failed — x360ce FFB will fail the same way.\n");
        dev->Unacquire();
        dev->Release();
        di->Release();
        return 6;
    }

    hr = effect->Download();
    PH("Download", hr);
    hr = effect->Start(1, 0);
    PH("Start", hr);

    printf("\n>>> Constant force running 4 seconds — feel for rumble <<<\n");
    Sleep(4000);

    effect->Stop();
    PH("Stop", S_OK);

    effect->Unload();
    effect->Release();
    dev->Unacquire();
    dev->Release();
    di->Release();
    CoUninitialize();

    printf("\nDone. Check %%TEMP%%\\DragonRiseFFB.log if silent.\n");
    return 0;
}
