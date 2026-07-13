#define INITGUID
#include <windows.h>
#include <objbase.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <dinputd.h>

// Stock CLSID used by Generic USB Gamepad Vibration Driver / OEM FFB registration.
// {0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
DEFINE_GUID(CLSID_GenericFFBDriver,
    0x0AB5665A, 0x4549, 0x4FD0, 0xA9, 0x52, 0x5A, 0x2B, 0x96, 0x99, 0xBD, 0xA8);

#include "effect_driver.h"

#include <strsafe.h>
#include <msi.h>

#pragma comment(lib, "msi.lib")

LONG g_serverLocks = 0;
LONG g_objCount = 0;
HINSTANCE g_hInst = nullptr;

// Must match stock GenericFFBDriver identity so apps/OEM registry stay unchanged.
static const wchar_t kClsidStr[] = L"{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}";
static const wchar_t kProgId[] = L"GenericFFBDriver.FFBDriver";
static const wchar_t kDesc[] = L"FFBDriver Class";
static const wchar_t kOemKey[] =
    L"SYSTEM\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\"
    L"Joystick\\OEM\\VID_0079&PID_0006\\OEMForceFeedback";
static const wchar_t kAxesKey[] =
    L"SYSTEM\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\"
    L"Joystick\\OEM\\VID_0079&PID_0006\\Axes";

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    if (g_serverLocks == 0 && g_objCount == 0)
        return S_OK;
    return S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_GenericFFBDriver)
        return CLASS_E_CLASSNOTAVAILABLE;

    ClassFactory* f = new (std::nothrow) ClassFactory();
    if (!f)
        return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

static HRESULT SetRegString(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                            const wchar_t* data)
{
    HKEY hKey = nullptr;
    LONG err = RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                               KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hKey, nullptr);
    if (err != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(err);
    err = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(data),
                         (DWORD)((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(err);
}

static HRESULT SetRegBinary(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                            const BYTE* data, DWORD cb)
{
    HKEY hKey = nullptr;
    LONG err = RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                               KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hKey, nullptr);
    if (err != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(err);
    err = RegSetValueExW(hKey, valueName, 0, REG_BINARY, data, cb);
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(err);
}

static HRESULT GetModulePath(wchar_t* buf, DWORD cch)
{
    DWORD n = GetModuleFileNameW(g_hInst, buf, cch);
    if (n == 0 || n >= cch)
        return E_FAIL;
    return S_OK;
}

static HRESULT RegisterOemForceFeedback()
{
    // OEMForceFeedback attributes: sample period / min time resolution (stock values)
    static const BYTE ffAttrs[] = {
        0x00, 0x00, 0x00, 0x00, 0xE8, 0x03, 0x00, 0x00, 0xE8, 0x03, 0x00, 0x00
    };
    HRESULT hr = SetRegBinary(HKEY_LOCAL_MACHINE, kOemKey, L"Attributes", ffAttrs, sizeof(ffAttrs));
    if (FAILED(hr))
        return hr;
    hr = SetRegString(HKEY_LOCAL_MACHINE, kOemKey, L"CLSID", kClsidStr);
    if (FAILED(hr))
        return hr;

    // Axes FF attributes (stock)
    static const BYTE axisFf[] = { 0x0A, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
    static const BYTE axis0[] = { 0x01, 0x81, 0x00, 0x00, 0x01, 0x00, 0x30, 0x00 };
    static const BYTE axis1[] = { 0x01, 0x81, 0x00, 0x00, 0x01, 0x00, 0x31, 0x00 };
    wchar_t a0[300], a1[300];
    StringCchPrintfW(a0, 300, L"%s\\0", kAxesKey);
    StringCchPrintfW(a1, 300, L"%s\\1", kAxesKey);
    SetRegBinary(HKEY_LOCAL_MACHINE, a0, L"FFAttributes", axisFf, sizeof(axisFf));
    SetRegBinary(HKEY_LOCAL_MACHINE, a0, L"Attributes", axis0, sizeof(axis0));
    SetRegBinary(HKEY_LOCAL_MACHINE, a1, L"FFAttributes", axisFf, sizeof(axisFf));
    SetRegBinary(HKEY_LOCAL_MACHINE, a1, L"Attributes", axis1, sizeof(axis1));

    struct EffectEntry {
        const wchar_t* guid;
        const wchar_t* name;
        const BYTE* attrs;
        DWORD attrLen;
    };
    static const BYTE constAttrs[] = {
        0x00, 0x00, 0x00, 0x00, 0x01, 0x86, 0x00, 0x00, 0xED, 0x03, 0x00, 0x00,
        0xED, 0x03, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00
    };
    static const BYTE sineAttrs[] = {
        0x03, 0x00, 0x00, 0x00, 0x03, 0x86, 0x00, 0x00, 0xEF, 0x03, 0x00, 0x00,
        0xEF, 0x03, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00
    };

    const EffectEntry effects[] = {
        { L"{13541C20-8E33-11D0-9AD0-00A0C9A06E35}", L"Constant", constAttrs, sizeof(constAttrs) },
        { L"{13541C21-8E33-11D0-9AD0-00A0C9A06E35}", L"Ramp Force", nullptr, 0 },
        { L"{13541C22-8E33-11D0-9AD0-00A0C9A06E35}", L"Square Wave", nullptr, 0 },
        { L"{13541C23-8E33-11D0-9AD0-00A0C9A06E35}", L"Sine Wave", sineAttrs, sizeof(sineAttrs) },
        { L"{13541C24-8E33-11D0-9AD0-00A0C9A06E35}", L"Triangle Wave", nullptr, 0 },
        { L"{13541C25-8E33-11D0-9AD0-00A0C9A06E35}", L"Sawtooth Up Wave", nullptr, 0 },
        { L"{13541C26-8E33-11D0-9AD0-00A0C9A06E35}", L"Sawtooth Down Wave", nullptr, 0 },
        { L"{13541C27-8E33-11D0-9AD0-00A0C9A06E35}", L"Spring", nullptr, 0 },
        { L"{13541C28-8E33-11D0-9AD0-00A0C9A06E35}", L"Damper", nullptr, 0 },
        { L"{13541C29-8E33-11D0-9AD0-00A0C9A06E35}", L"Inertia", nullptr, 0 },
        { L"{13541C2A-8E33-11D0-9AD0-00A0C9A06E35}", L"Friction", nullptr, 0 },
        { L"{13541C2B-8E33-11D0-9AD0-00A0C9A06E35}", L"CustomForce", nullptr, 0 },
    };

    wchar_t effectsRoot[400];
    StringCchPrintfW(effectsRoot, 400, L"%s\\Effects", kOemKey);
    SetRegString(HKEY_LOCAL_MACHINE, effectsRoot, nullptr, L"");

    for (const auto& e : effects) {
        wchar_t ek[450];
        StringCchPrintfW(ek, 450, L"%s\\%s", effectsRoot, e.guid);
        SetRegString(HKEY_LOCAL_MACHINE, ek, nullptr, e.name);
        if (e.attrs && e.attrLen)
            SetRegBinary(HKEY_LOCAL_MACHINE, ek, L"Attributes", e.attrs, e.attrLen);
    }
    return S_OK;
}

STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH];
    HRESULT hr = GetModulePath(path, MAX_PATH);
    if (FAILED(hr))
        return hr;

    // Register this module under the architecture-appropriate view of HKCR.
    // 32-bit regsvr32 writes Wow6432Node; 64-bit writes native.
    // Avoid KEY_WOW64_64KEY here so the COM registration matches the bitness of this DLL.
    {
        HKEY hKey = nullptr;
        wchar_t key[256];
        StringCchPrintfW(key, 256, L"CLSID\\%s", kClsidStr);
        LONG err = RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
        if (err != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(err);
        RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(kDesc),
                       (DWORD)((wcslen(kDesc) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);

        wchar_t inproc[300];
        StringCchPrintfW(inproc, 300, L"CLSID\\%s\\InProcServer32", kClsidStr);
        err = RegCreateKeyExW(HKEY_CLASSES_ROOT, inproc, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
        if (err != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(err);
        RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(path),
                       (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
        const wchar_t both[] = L"Both";
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(both), sizeof(both));
        RegCloseKey(hKey);

        wchar_t prog[300];
        StringCchPrintfW(prog, 300, L"CLSID\\%s\\ProgID", kClsidStr);
        err = RegCreateKeyExW(HKEY_CLASSES_ROOT, prog, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
        if (err == ERROR_SUCCESS) {
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(kProgId),
                           (DWORD)((wcslen(kProgId) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }

        err = RegCreateKeyExW(HKEY_CLASSES_ROOT, kProgId, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
        if (err == ERROR_SUCCESS) {
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(kDesc),
                           (DWORD)((wcslen(kDesc) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
        wchar_t progClsid[300];
        StringCchPrintfW(progClsid, 300, L"%s\\CLSID", kProgId);
        err = RegCreateKeyExW(HKEY_CLASSES_ROOT, progClsid, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
        if (err == ERROR_SUCCESS) {
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(kClsidStr),
                           (DWORD)((wcslen(kClsidStr) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    }

    // OEM keys are native (64-bit) view — DI reads them there.
    return RegisterOemForceFeedback();
}

static void DeleteKeyTree(HKEY root, const wchar_t* subKey)
{
    // RegDeleteTree available Vista+
    RegDeleteTreeW(root, subKey);
}

STDAPI DllUnregisterServer()
{
    wchar_t key[256];
    StringCchPrintfW(key, 256, L"CLSID\\%s", kClsidStr);
    DeleteKeyTree(HKEY_CLASSES_ROOT, key);
    DeleteKeyTree(HKEY_CLASSES_ROOT, kProgId);

    // Leave OEM hardware metadata? Uninstall of original leaves COM but MSI removes files.
    // Clear CLSID pointer so orphaned OEM does not load missing DLL.
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kOemKey, 0, KEY_WRITE | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"CLSID");
        RegCloseKey(hKey);
    }
    return S_OK;
}

static UINT RunRegSvr(const wchar_t* regsvrPath, const wchar_t* dllPath)
{
    wchar_t cmd[1024];
    StringCchPrintfW(cmd, 1024, L"\"%s\" /s \"%s\"", regsvrPath, dllPath);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    wchar_t mutableCmd[1024];
    StringCchCopyW(mutableCmd, 1024, cmd);
    if (!CreateProcessW(nullptr, mutableCmd, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return GetLastError();
    }
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0 ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
}

// MSI custom action entry (Type 3089) + drop-in compatible export.
// Registers both 32-bit and 64-bit InProc servers via regsvr32, matching stock behavior.
extern "C" UINT __stdcall RegisterVibrationDriver(MSIHANDLE /*hInstall*/)
{
    wchar_t winDir[MAX_PATH];
    if (!GetWindowsDirectoryW(winDir, MAX_PATH))
        return ERROR_INSTALL_FAILURE;

    wchar_t dll32[MAX_PATH], dll64[MAX_PATH];
    StringCchPrintfW(dll32, MAX_PATH, L"%s\\GenericFFBDriver\\GenericFFBDriver32.dll", winDir);
    StringCchPrintfW(dll64, MAX_PATH, L"%s\\GenericFFBDriver\\GenericFFBDriver64.dll", winDir);

    wchar_t sysDir[MAX_PATH], sysWow[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    sysWow[0] = 0;
    // GetSystemWow64DirectoryW is only meaningful on 64-bit Windows.
    typedef UINT(WINAPI* PFN_GSW64)(LPWSTR, UINT);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto pfn = k32 ? reinterpret_cast<PFN_GSW64>(GetProcAddress(k32, "GetSystemWow64DirectoryW")) : nullptr;
    if (pfn)
        pfn(sysWow, MAX_PATH);

    wchar_t reg64[MAX_PATH], reg32[MAX_PATH];
    // On 64-bit OS: System32 regsvr32 is 64-bit, SysWOW64 is 32-bit.
    StringCchPrintfW(reg64, MAX_PATH, L"%s\\regsvr32.exe", sysDir);
    if (sysWow[0])
        StringCchPrintfW(reg32, MAX_PATH, L"%s\\regsvr32.exe", sysWow);
    else
        StringCchCopyW(reg32, MAX_PATH, reg64);

    UINT rc = ERROR_SUCCESS;
    if (GetFileAttributesW(dll32) != INVALID_FILE_ATTRIBUTES) {
        UINT r = RunRegSvr(reg32, dll32);
        if (r != ERROR_SUCCESS)
            rc = r;
    }
    if (GetFileAttributesW(dll64) != INVALID_FILE_ATTRIBUTES) {
        UINT r = RunRegSvr(reg64, dll64);
        if (r != ERROR_SUCCESS)
            rc = r;
    }
    return rc;
}
