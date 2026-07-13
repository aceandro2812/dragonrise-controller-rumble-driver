# Patched Speedlink / DragonRise FFB Installer

**Verified working** with real hardware and x360ce — see `docs\WORKING_SETUP.md`.

## What this package does

End-user experience:

1. Run `Setup.exe`
2. Click **Next** → **Install** → **Finish**
3. Force Feedback COM layer is installed and registered automatically

No PowerShell, no manual `regsvr32`, no registry editing.

For **XInput / many Steam games**, also configure **x360ce** as in `docs\X360CE_RUMBLE.md` (Force **Type 2**, directions **Positive**).

## What is **not** modified

- Microsoft HID / USB kernel drivers (`input.inf`, `HidUsb`)
- Device INF binding
- The physical controller firmware

Only the **user-mode DirectInput Force Feedback translation layer** is replaced.

## Architecture (after analysis)

| Piece | Role |
|-------|------|
| Root `Setup.exe` (original) | Multi-product CD menu (Director). Optional, not required for FFB. |
| `Driver\Setup.exe` | InstallShield package installing legacy `GaJoyFF.dll` / control panel (`CLSID {B4FE8B02-…}`). |
| **Generic USB Gamepad Vibration Driver MSI** | The component that actually provides live FFB on modern Windows: copies `GenericFFBDriver32/64.dll` to `%SystemRoot%\GenericFFBDriver\` and registers COM. |

Live OEM registration (unchanged identity):

```text
HKLM\...\Joystick\OEM\VID_0079&PID_0006\OEMForceFeedback
  CLSID = {0AB5665A-4549-4FD0-A952-5A2B9699BDA8}

HKCR\CLSID\{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
  InProcServer32 = %SystemRoot%\GenericFFBDriver\GenericFFBDriver64.dll   (native)
  Wow6432Node → GenericFFBDriver32.dll
```

The MSI custom action `RegisterVibrationDriver` runs `regsvr32 /s` on both DLLs after copy. Uninstall removes the files and unregisters via the same MSI product.

## Patch strategy (preferred)

**Do not hex-edit the stock DLL.**  
**Do not invent a new CLSID.**

1. Build corrected DLLs that export the same surface:
   - `DllGetClassObject`, `DllRegisterServer`, `DllUnregisterServer`, `DllCanUnloadNow`
   - `RegisterVibrationDriver` (MSI custom action)
2. Ship them under the **same file names**:
   - `GenericFFBDriver32.dll`
   - `GenericFFBDriver64.dll`
3. Keep the **same CLSID** `{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}` and ProgID `GenericFFBDriver.FFBDriver`.
4. Rebuild the MSI cabinet so the stock MSI UI / uninstall / SelfReg path install our binaries transparently.

DirectInput, x360ce, and any app using the OEM CLSID load our implementation without noticing.

## Package layout

```text
dist\SPEED-LINK-SL-6535-BK_Driver_V1.0_FFB-Patched\
  Setup.exe                          ← patched wizard (elevates, runs MSI)
  FFB\GenericUSBGamepadVibration.msi ← MSI with fixed DLLs inside
  Driver\Setup.exe                   ← original InstallShield (optional extras)
  Simplicheck\Simplicheck.exe        ← original
  Setup.original.exe                 ← original CD menu (reference)
  README.txt                         ← this document
```

## Rebuild from sources

Prerequisites:

- Visual Studio Build Tools (C++), Windows SDK 10
- Python 3
- .NET Framework 4.x (`csc.exe`)
- Original MSI available as `analysis\GenericUSBGamepadVibration.msi`  
  (or still installed so `C:\WINDOWS\Installer\*.msi` can be used)

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_patched_package.ps1
```

Steps performed:

1. `replacement\DragonRiseFFB\build.bat all` → `bin\GenericFFBDriver32/64.dll`
2. `tools\patch_ffb_msi.py` replaces the embedded cabinet payload and updates `File.FileSize`
3. Compiles `tools\SetupBootstrap.cs` → package `Setup.exe`
4. Assembles `dist\SPEED-LINK-SL-6535-BK_Driver_V1.0_FFB-Patched\`

## Behavioral fixes in the replacement DLL

Compared with stock `GenericFFBDriver`:

| Area | Stock | Patched |
|------|-------|---------|
| `StartEffect` / `StopEffect` / `DestroyEffect` | Stubs returning S_OK | Real effect lifecycle |
| `SetGain` | Stub | Applied |
| Axis routing | Crude axis IDs → motors (stick can trigger rumble) | Both motors by default; direction only balances |
| HID packets | `00 51 00 A 00 B 00 00` + commit `00 FA FE …`; stop `00 F3 …` | Same packets (RE-verified) |

## Uninstall / rollback

Use **Settings → Apps** → *Generic USB Gamepad Vibration Driver* (same product code as stock MSI):

`{50CD8B4D-CD82-49D1-9E0A-2B7887448068}`

That removes `%SystemRoot%\GenericFFBDriver\*.dll` and COM registration exactly like the original product.

Registry backups of pre-patch state live in `registry_backup\`.

## Validation checklist

After install:

- [ ] Device still uses Microsoft HID stack (`input.inf` / `HidUsb`)
- [ ] `C:\Windows\GenericFFBDriver\GenericFFBDriver64.dll` is the new build
- [ ] `OEMForceFeedback\CLSID` still `{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}`
- [ ] x360ce lists Force Feedback capabilities
- [ ] DirectInput rumble works
- [ ] Moving analog sticks does **not** start rumble by itself
- [ ] Optional: `bin\HidRumbleTest.exe sequence` still vibrates motors

## Source map

| Path | Purpose |
|------|---------|
| `replacement\DragonRiseFFB\` | C++ COM effect-driver source |
| `tools\patch_ffb_msi.py` | MSI cabinet replacement |
| `tools\SetupBootstrap.cs` | Outer Setup.exe wizard |
| `tools\build_patched_package.ps1` | One-shot rebuild |
| `tools\HidRumbleTest.cs` | Offline HID packet tester |
| `RE_HANDOFF.md` | Reverse-engineering notes |
