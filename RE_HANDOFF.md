# DragonRise / Speedlink Force Feedback Reverse-Engineering Handoff

Date: 2026-07-13  
Workspace: `C:\Users\jatin\Downloads\SPEED-LINK-SL-6535-BK_Driver_V1.0`

## Objective

Reverse engineer the Force Feedback / rumble pipeline for a generic DragonRise USB gamepad:

- Hardware ID: `USB\VID_0079&PID_0006`
- Windows device: HID-compliant game controller
- Two physical vibration motors are present and verified working
- Default Microsoft HID driver works for input but exposes no usable standard FFB
- Speedlink/Generic driver setup causes DirectInput FFB capability to appear, but Steam/x360ce rumble does not work correctly
- Speedlink control panel can drive the motors

The requested long-term goal is a working replacement or patch for the DirectInput Force Feedback translation layer so Steam, x360ce, and DirectInput rumble can drive the controller correctly, preferably without a kernel driver.

## Important Constraint

No binary patch was applied. No system driver state was intentionally changed during this pass. The work so far is analysis only, plus creation of local analysis scripts/reports in the workspace.

## Package Contents

The driver package directory contains only three EXEs:

```text
Setup.exe
Driver\Setup.exe
Simplicheck\Simplicheck.exe
```

`Driver\Setup.exe` is the important installer. It appears to be an old InstallShield-style package with embedded registry strings and DLL names.

Hashes gathered:

```text
Setup.exe
  SHA256: 87AAE36066CC1EECC355D1A4355B2DFEC651AD4E25ED71C95631FF06FC5A1701
  Signature: Not signed

Driver\Setup.exe
  SHA256: 11550B733F6B11299363396FF25933F7EC9553BFE84FCADF9157E54918F4AE22
  Signature: Not signed

Simplicheck\Simplicheck.exe
  SHA256: B15A66FDCD6E7E32C47C045A9E60BA24AFCCCEAE2591739DC985D97313206192
  Signature: valid, simplitec GmbH
```

## Live Device State

The connected controller was present as:

```text
USB\VID_0079&PID_0006\5&11CCA10D&0&3
HID\VID_0079&PID_0006\6&109BD56D&0&0000
```

PnP properties showed the active driver stack is Microsoft inbox HID:

```text
USB device:
  DriverInfPath: input.inf
  DriverInfSection: HID_Inst.NT
  Service: HidUsb
  DriverProvider: Microsoft
  DriverDesc: USB Input Device

HID game controller:
  DriverInfPath: input.inf
  DriverInfSection: HID_Raw_Inst.NT
  DriverProvider: Microsoft
  DriverDesc: HID-compliant game controller
```

The device enum registry keys had:

```text
UpperFilters: empty
LowerFilters: empty
```

Conclusion: no HID filter driver is installed or bound. No Speedlink `.sys` driver is in the live device stack.

## SetupAPI Evidence

`C:\Windows\INF\setupapi.dev.log` confirms that Windows installs this controller using Microsoft `input.inf` and sometimes reflects `hidgamepad.inf` metadata:

```text
HID\VID_0079&PID_0006 -> input.inf [HID_Raw_Inst.NT]
USB\VID_0079&PID_0006 -> input.inf [HID_Inst.NT]
```

Searches of `C:\Windows\INF` and `pnputil /enum-drivers` found no matching Speedlink third-party INF for `VID_0079&PID_0006`.

## Installer Strings

Focused strings from `Driver\Setup.exe` showed the original Speedlink installer intended to register:

```text
<PROGRAMFILES>\VID_0079&PID_0006
<PROGRAMFILES64>\VID_0079&PID_0006

CLSID\{B4FE8B02-40D0-438A-B4C2-DE4522951071}
USB GAMEPAD Feedback Support DLL
InProcServer32 = <TARGETDIR>\GaJoyFF.dll

CLSID\{B4FE8B03-40D0-438A-B4C2-DE4522951071}
USB GAMEPAD Property Sheet Support DLL
InProcServer32 = <TARGETDIR>\GAJoyPS.dll

SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\DirectInput\VID_0079&PID_0006
SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_0079&PID_0006
ConfigCLSID
OEMForceFeedback
Effects
Constant
Ramp Force
Square Wave
Sine Wave
Triangle Wave
Sawtooth Up Wave
Sawtooth Down Wave
Spring
Damper
Inertia
Friction
CustomForce
```

However, the currently installed live COM server is not named `GaJoyFF.dll`; it is `GenericFFBDriver*.dll`.

## Live DirectInput / COM Registration

The live DirectInput OEM Force Feedback registration:

```text
HKLM\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_0079&PID_0006\OEMForceFeedback
  CLSID = {0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
  Attributes = 00000000E8030000E8030000
```

COM registration:

```text
HKLM\SOFTWARE\Classes\CLSID\{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
  (Default) = FFBDriver Class
  ProgId = GenericFFBDriver.FFBDriver
  InProcServer32 = C:\WINDOWS\GenericFFBDriver\GenericFFBDriver64.dll
  ThreadingModel = Both

HKLM\SOFTWARE\Classes\Wow6432Node\CLSID\{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
  (Default) = FFBDriver Class
  ProgId = GenericFFBDriver.FFBDriver
  InProcServer32 = C:\WINDOWS\GenericFFBDriver\GenericFFBDriver32.dll
  ThreadingModel = Both
```

Installed DLLs:

```text
C:\WINDOWS\GenericFFBDriver\GenericFFBDriver64.dll
  Size: 352768
  LastWriteTime: 2017-01-17 23:51:48
  SHA256: 44572068A0AC850B06402BCB4F09470646B30BDCF0BBAE9E3538897FBC6C179B
  Signature: Not signed

C:\WINDOWS\GenericFFBDriver\GenericFFBDriver32.dll
  Size: 265216
  LastWriteTime: 2017-01-17 23:52:00
  SHA256: D9275A8EC2F274DBEC23F2063A60002B9DA3867A6E5B521A8EC9F5E2D9A6C32B
  Signature: Not signed
```

## Registered Effect GUIDs

Registry under:

```text
HKLM\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_0079&PID_0006\OEMForceFeedback\Effects
```

Contains standard DirectInput effect GUIDs:

```text
{13541C20-8E33-11D0-9AD0-00A0C9A06E35} Constant
  Attributes = 0000000001860000ED030000ED03000030000000

{13541C21-8E33-11D0-9AD0-00A0C9A06E35} Ramp Force
{13541C22-8E33-11D0-9AD0-00A0C9A06E35} Square Wave

{13541C23-8E33-11D0-9AD0-00A0C9A06E35} Sine Wave
  Attributes = 0300000003860000EF030000EF03000030000000

{13541C24-8E33-11D0-9AD0-00A0C9A06E35} Triangle Wave
{13541C25-8E33-11D0-9AD0-00A0C9A06E35} Sawtooth Up Wave
{13541C26-8E33-11D0-9AD0-00A0C9A06E35} Sawtooth Down Wave
{13541C27-8E33-11D0-9AD0-00A0C9A06E35} Spring
{13541C28-8E33-11D0-9AD0-00A0C9A06E35} Damper
{13541C29-8E33-11D0-9AD0-00A0C9A06E35} Inertia
{13541C2A-8E33-11D0-9AD0-00A0C9A06E35} Friction
{13541C2B-8E33-11D0-9AD0-00A0C9A06E35} CustomForce
```

Observation: only Constant and Sine have meaningful binary `Attributes`; most other effect keys are just names. This may already mislead clients that enumerate many effect types.

## HID Capabilities

A C#/PowerShell HID query was run against the live HID interface. It reported:

```text
Path:
  \\?\hid#vid_0079&pid_0006#6&109bd56d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}

VID: 0x0079
PID: 0x0006
Version: 0x0107
UsagePage: 0x0001
Usage: 0x0004
InputReportByteLength: 9
OutputReportByteLength: 8
FeatureReportByteLength: 0
NumberOutputValueCaps: 1
NumberFeatureValueCaps: 0
```

Conclusion: the controller does not expose a full HID PID force-feedback descriptor. It exposes a simple 8-byte HID output report. Therefore, DirectInput FFB support must be a user-mode shim translating DirectInput effects into vendor/simple HID output reports.

## DLL Exports

Exports from both DLLs:

```text
DllCanUnloadNow
DllGetClassObject
DllRegisterServer
DllUnregisterServer
RegisterVibrationDriver
```

64-bit RVAs:

```text
DllCanUnloadNow          RVA 0x2190
DllGetClassObject        RVA 0x2120
DllRegisterServer        RVA 0x21A0
DllUnregisterServer      RVA 0x2210
RegisterVibrationDriver  RVA 0x2AB0
```

32-bit RVAs:

```text
DllCanUnloadNow          RVA 0x18A0
DllGetClassObject        RVA 0x1840
DllRegisterServer        RVA 0x18B0
DllUnregisterServer      RVA 0x1910
RegisterVibrationDriver  RVA 0x20B0
```

## Imports of Interest

Both DLLs import:

```text
KERNEL32.dll!CreateFileW
KERNEL32.dll!WriteFile
KERNEL32.dll!CreateThread
KERNEL32.dll!GetTickCount
KERNEL32.dll!CloseHandle
HID.DLL!HidD_SetOutputReport
ADVAPI32.dll registry APIs
```

Important note: `WriteFile` xrefs found in the automated report are mostly CRT/file-output style paths, not the HID motor path. The actual motor path uses `HidD_SetOutputReport`.

## Analysis Tools Added

Python packages installed:

```text
capstone
pefile
```

Local files created:

```text
tools\analyze_ffb_pe.py
tools\dump_ffb_function.py
ffb64.analysis.json
ffb32.analysis.json
```

`tools\analyze_ffb_pe.py` parses PE imports/exports, disassembles `.text`, finds import xrefs, and records interesting strings.  
`tools\dump_ffb_function.py` dumps key 64-bit functions and referenced data.

These files are meant as reproducible analysis artifacts for the next agent.

## DirectInput Header Reference

The Windows SDK header exists locally:

```text
C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared\dinputd.h
```

Relevant interface:

```c
IID_IDirectInputEffectDriver =
  {02538130-898F-11D0-9AD0-00A0C9A06E35}

IDirectInputEffectDriver methods:
  QueryInterface
  AddRef
  Release
  DeviceID
  GetVersions
  Escape
  SetGain
  SendForceFeedbackCommand
  GetForceFeedbackState
  DownloadEffect
  DestroyEffect
  StartEffect
  StopEffect
  GetEffectStatus
```

The DLL string table contains:

```text
.?AV?$InterfaceImpl@UIDirectInputEffectDriver@@@@
.?AUIDirectInputEffectDriver@@
```

So it implements the DirectInput effect-driver COM interface.

## COM Object / Vtable Notes

The useful vtable region in `GenericFFBDriver64.dll` was observed around `0x180045c50`.

Likely `IDirectInputEffectDriver` vtable beginning at `0x180045c50`:

```text
0x180045c50:
  00  0x1800027e0  QueryInterface-ish
  01  0x180002440  AddRef thunk
  02  0x180002420  Release thunk
  03  0x180002860  DeviceID
  04  0x1800028b0  GetVersions
  05  0x180002230  Escape stub, returns S_OK
  06  0x180002230  SetGain stub, returns S_OK
  07  0x1800028d0  SendForceFeedbackCommand
  08  0x180002230  GetForceFeedbackState stub, returns S_OK
  09  0x180002940  DownloadEffect wrapper
  10  0x180002230  DestroyEffect stub, returns S_OK
  11  0x180002230  StartEffect stub, returns S_OK
  12  0x180002230  StopEffect stub, returns S_OK
  13  0x180002230  GetEffectStatus stub, returns S_OK
```

This strongly suggests a broken/minimal implementation:

- `DownloadEffect` is where all actual effect parsing happens.
- `StartEffect`, `StopEffect`, and `DestroyEffect` are stubs.
- `SetGain` is a stub.
- `GetForceFeedbackState` is probably a stub.

This can explain app compatibility problems if Steam/x360ce expects the normal DirectInput lifecycle:

1. `DownloadEffect`
2. `StartEffect`
3. `StopEffect`
4. `DestroyEffect`

The installed DLL appears to treat `DownloadEffect` as "parse and start/update immediately" and ignores most lifecycle methods.

## Key HID Motor Worker Function

64-bit function at:

```text
VA 0x180002e80
```

This opens the HID device and loops over effect slots. Important calls:

```text
CreateFileW xref:
  0x180002ee9

HidD_SetOutputReport xrefs:
  0x180003023
  0x180003037
  0x1800030d7
```

At `0x180002ee9`, `CreateFileW` uses:

```text
dwDesiredAccess     = 0xC0000000  GENERIC_READ | GENERIC_WRITE
dwShareMode         = 3           FILE_SHARE_READ | FILE_SHARE_WRITE
dwCreationDisposition = 3         OPEN_EXISTING
dwFlagsAndAttributes = 0x40000000 FILE_FLAG_OVERLAPPED
```

The HID path string is stored globally as a wide/string object, populated via `DeviceID`/DirectInput device path logic.

## Exact HID Output Report Construction

At `0x180002fec` through `0x180003023`, the DLL constructs a dynamic 8-byte report on the stack:

```asm
mov r8d, 8
mov word ptr [rsp + 0x60], 0x5100
lea rdx, [rsp + 0x60]
mov byte ptr [rsp + 0x62], r15b   ; zero
mov byte ptr [rsp + 0x63], bl     ; motor A
mov byte ptr [rsp + 0x64], r15b   ; zero
mov byte ptr [rsp + 0x65], dil    ; motor B
mov word ptr [rsp + 0x66], r15w   ; zero
call HidD_SetOutputReport(handle, &report, 8)
```

Therefore the dynamic rumble report is:

```text
Byte 0: 0x00
Byte 1: 0x51
Byte 2: 0x00
Byte 3: motor A intensity
Byte 4: 0x00
Byte 5: motor B intensity
Byte 6: 0x00
Byte 7: 0x00
```

Likely semantic names:

```text
report[0] = HID report ID, zero/no report ID
report[1] = command 0x51
report[2] = zero
report[3] = left or low-frequency motor intensity
report[4] = zero
report[5] = right or high-frequency motor intensity
report[6] = zero
report[7] = zero
```

The exact left/right physical mapping still needs live testing. Based on code, one motor comes from `bl`, the other from `dil`.

When both motor intensities are zero, the code instead sends a static zero/stop packet loaded from `0x180045dc8`:

```text
00 F3 00 00 00 00 00 00
```

At shutdown/exit, it sends another fixed packet from `0x180045dc8` or nearby:

```text
00 F3 00 00 00 00 00 00
```

The dynamic path also sends a second fixed report immediately after the dynamic report:

```asm
mov qword ptr [rsp + 0x68], 0xfefa00
lea rdx, [rsp + 0x68]
mov r8d, 8
call HidD_SetOutputReport
```

That second packet is:

```text
00 FA FE 00 00 00 00 00
```

So active vibration update sequence appears to be:

```text
1. 00 51 00 <motorA> 00 <motorB> 00 00
2. 00 FA FE 00 00 00 00 00
```

Stop/off sequence appears to be:

```text
00 F3 00 00 00 00 00 00
```

This is the most important packet-level result.

## Effect Slot Data Structure

The worker at `0x180002e80` scans 5 effect slots.

Slot base appears around:

```text
0x180053f10
```

Each slot is `0x18` bytes:

```text
offset +0x00: DWORD effect_id
offset +0x04: DWORD end_tick_or_next_tick
offset +0x08: DWORD stop_tick_or_duration_end, -1 for infinite
offset +0x0C: BYTE motorA
offset +0x0D: BYTE motorB
offset +0x0E: padding/unused
offset +0x0F: padding/unused
offset +0x10: QWORD active/running flag
```

Worker logic summary:

```c
loop:
  lock
  if stop flag set: exit

  now = GetTickCount()
  maxA = 0
  maxB = 0

  for each of 5 slots:
    if slot.effect_id == 0:
      continue

    if slot.active == 0:
      if slot.start_or_due_tick <= now:
        slot.active = 1
        if slot.stop_tick != -1:
          slot.stop_tick = now + (slot.stop_tick - slot.start_tick)
        maxA = max(maxA, slot.motorA)
        maxB = max(maxB, slot.motorB)
    else:
      if slot.stop_tick != -1 && slot.stop_tick <= now:
        slot.effect_id = 0
      else:
        maxA = max(maxA, slot.motorA)
        maxB = max(maxB, slot.motorB)

  if maxA/maxB changed:
    if maxA == 0 && maxB == 0:
      send 00 F3 00 00 00 00 00 00
    else:
      send 00 51 00 maxA 00 maxB 00 00
      send 00 FA FE 00 00 00 00 00

  wait/sleep roughly 10 ms
```

Aggregation logic uses `max()` of active effects rather than mixing/summing.

## Effect Parser Function

Important function:

```text
VA 0x180003120
```

Called by `DownloadEffect` wrapper:

```text
0x180002940:
  rdx = qword ptr [rsp + 0x50]  ; LPCDIEFFECT argument
  ecx = r8d                     ; effect id/handle
  call 0x180003120
```

Parser behavior observed:

```asm
cmp dword ptr [rsi + 0x38], 4
mov bl, 0xfe
...
if cbTypeSpecificParams == 4:
    rax = [rsi + 0x40]              ; lpvTypeSpecificParams
    read DWORD [rax]                ; e.g. DICONSTANTFORCE.lMagnitude
    convert magnitude to 0..254-ish
else:
    intensity = 0xFE
```

Then direction/axis logic:

```asm
mov r8d, dword ptr [rsi + 0x1c]     ; cAxes
if cAxes == 1:
    axis = *(DWORD*)[rsi + 0x28]    ; rgdwAxes[0]
    if axis == -1:
        global/default motorA = intensity
    else if axis == 1:
        global/default motorB = intensity
else:
    if cAxes >= 1:
        if rgdwAxes[0] > 0: motorA = intensity
    if cAxes >= 2:
        if rgdwAxes[1] > 0: motorB = intensity
```

Then it writes to slot:

```asm
slot.motorA = bl
slot.motorB = dil
slot.effect_id = effect_id
slot.start_tick = now + converted_start_delay
slot.stop_tick = start_tick + converted_duration, unless duration == INFINITE
slot.active = 1
```

Timing conversion uses magic constant:

```text
0x10624dd3
```

This is consistent with converting DirectInput microseconds to milliseconds:

```c
milliseconds = microseconds / 1000
```

## Strong Explanation for Stick-Movement-Causes-Rumble Bug

The installed driver maps DirectInput effect axes/direction vectors to motor selection/intensity in a very crude way:

- For one-axis effects:
  - axis `-1` selects motor A
  - axis `1` selects motor B
- For two-axis effects:
  - positive first axis enables motor A
  - positive second axis enables motor B

This is not a robust rumble mapping. It treats force-vector direction/axes as if they were motor routing. Some DirectInput clients or wrappers may pass joystick axis IDs, direction vectors, or stale axis state in a way that causes the driver to change motor values based on analog-stick-related data. This matches the observed behavior where moving analog sticks caused rumble.

In other words: the DLL is not a proper force-feedback engine. It is a simplistic DirectInput-to-two-motor shim, and it misuses axes/direction as motor routing.

## Why Steam/x360ce Likely Fail

Main reasons:

1. The controller does not expose native HID PID FFB, only an 8-byte output report.

2. FFB capability is advertised through registry and COM only.

3. The COM effect driver is minimal/broken:
   - most lifecycle methods are stubs returning `S_OK`;
   - `StartEffect`, `StopEffect`, `DestroyEffect`, and `SetGain` do not appear to manage the slots;
   - `DownloadEffect` immediately parses and starts/updates the effect.

4. The driver primarily understands 4-byte type-specific params, which fits `DICONSTANTFORCE`, but many clients use periodic effects, envelopes, ramps, or separate `StartEffect`.

5. It registers many effect GUIDs but only really has logic that looks like Constant Force plus a default fallback intensity. Other effects may be malformed or meaningless.

6. It routes motor intensity via DirectInput axes/direction vectors, which is wrong for generic rumble use.

7. Steam may not use this legacy DirectInput OEM FFB path at all for generic HID controllers, or may expect a normal lifecycle and therefore never triggers the broken `DownloadEffect` behavior in a compatible way.

8. x360ce can detect FFB because the registry advertises it, but its test effect may not map into the specific `DownloadEffect`/axis pattern this DLL expects.

## Current Best Technical Hypothesis

The correct rumble packets are probably:

```text
Active:
  00 51 00 LL 00 RR 00 00
  00 FA FE 00 00 00 00 00

Stop:
  00 F3 00 00 00 00 00 00
```

Where `LL` and `RR` are two independent motor intensities in range `0x00..0xFE` or `0x00..0xFF`.

The exact physical left/right/large/small motor mapping is not yet confirmed. A safe live test would send:

```text
00 51 00 FF 00 00 00 00
00 FA FE 00 00 00 00 00
```

then stop:

```text
00 F3 00 00 00 00 00 00
```

and separately:

```text
00 51 00 00 00 FF 00 00
00 FA FE 00 00 00 00 00
```

then stop.

No such live motor packet test was performed during the first pass (see progress below).

## Progress — 2026-07-13 (patched installer)

### Analysis result (installer)

| Binary | Type | Role |
|--------|------|------|
| Root `Setup.exe` | Macromedia Director menu | Launches optional components; **not** the FFB path |
| `Driver\Setup.exe` | InstallShield 12 | Legacy `GaJoyFF.dll` / control panel (`CLSID {B4FE8B02-…}`) |
| **Generic USB Gamepad Vibration Driver MSI** | VS Deployment Project MSI | **Actual live FFB**: installs `GenericFFBDriver32/64.dll` to `%SystemRoot%\GenericFFBDriver\`, SelfReg + `RegisterVibrationDriver` custom action |

No kernel INF/sys is involved. FFB is user-mode COM only.

### Patched package deliverable

```text
dist\SPEED-LINK-SL-6535-BK_Driver_V1.0_FFB-Patched\
  Setup.exe                               # Next → Install → Finish
  FFB\GenericUSBGamepadVibration.msi      # same ProductCode, fixed DLLs
  Driver\Setup.exe                        # original InstallShield (optional)
  README.txt
```

- Stock CLSID preserved: `{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}`
- Same file names and install directory
- Same MSI uninstall product `{50CD8B4D-CD82-49D1-9E0A-2B7887448068}`
- Rebuild: `tools\build_patched_package.ps1`
- Docs: `docs\PATCHED_INSTALLER.md`

### How the MSI was patched

1. Build fixed `GenericFFBDriver32/64.dll` (exports include `RegisterVibrationDriver`)
2. Extract embedded cabinet stream from original MSI
3. Rebuild cabinet with replacement PE files (same internal file keys)
4. Replace OLE stream via `tools\msi_replace_stream.exe`
5. Update `File.FileSize`; clear `MsiFileHash`

## Progress — 2026-07-13 (continuation)

### Done

1. **Registry backup** (before any OEM/COM changes):
   - `registry_backup\OEM_VID_0079_PID_0006.reg`
   - `registry_backup\CLSID_GenericFFB_64.reg`
   - `registry_backup\CLSID_GenericFFB_32.reg`

2. **HID rumble test utility** built and run successfully against live device:
   - Source: `tools\HidRumbleTest.cs`
   - Build: `tools\build_hid_test.ps1` → `bin\HidRumbleTest.exe`
   - Live path: `\\?\hid#vid_0079&pid_0006#6&109bd56d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}`
   - All of the following returned `HidD_SetOutputReport` **OK**:
     - `00 51 00 FE 00 00 00 00` + `00 FA FE 00 00 00 00 00` (motor A)
     - `00 51 00 00 00 FE 00 00` + commit (motor B)
     - both motors
     - `00 F3 00 00 00 00 00 00` (stop)
     - rumble **without** commit also accepted by the stack (API OK; whether motors move without commit needs human observation)

3. **Replacement COM DLL** implemented and built:
   - Source: `replacement\DragonRiseFFB\`
   - Outputs: `bin\DragonRiseFFB64.dll`, `bin\DragonRiseFFB32.dll`
   - New CLSID: `{B1D0F8A2-3C4E-4F61-9A7B-2E5C8D1F0A94}`
   - ProgID: `DragonRiseFFB.FFBDriver`
   - Implements full lifecycle: Download/Start/Stop/Destroy/SetGain/SendForceFeedbackCommand
   - ~10 ms worker thread for duration expiry
   - Default motor mapping: both motors equal (no stick-axis bug)
   - Build: `replacement\DragonRiseFFB\build.bat all`
   - Register (admin): `replacement\DragonRiseFFB\register.ps1` and `-ActivateOem`
   - Docs: `replacement\DragonRiseFFB\README.md`

### Still needs human / live verification

1. **Physical motor map**: run `bin\HidRumbleTest.exe sequence` and note which physical motor vibrates for A (byte3) vs B (byte5). API path is proven; tactile mapping is not recorded yet.
2. **Commit packet necessity**: sequence includes a no-commit trial; confirm by feel whether motors spin without `00 FA FE ...`.
3. **COM not yet registered/activated** on this machine (intentionally — requires admin and OEM retarget). Stock GenericFFB still owns OEM CLSID.
4. **x360ce / DirectInput sample** testing after `-ActivateOem`.
5. **Steam**: still likely needs XInput/Steam Input bridge; DI OEM path alone may not fix Steam rumble.
6. Optional registry-only experiment (Constant-only effects) not performed.

### Quick next commands

```powershell
# Feel motors / confirm A vs B
bin\HidRumbleTest.exe sequence
bin\HidRumbleTest.exe left
bin\HidRumbleTest.exe right

# Elevated PowerShell — register COM and point OEM at replacement
powershell -ExecutionPolicy Bypass -File replacement\DragonRiseFFB\register.ps1 -ActivateOem

# Rollback OEM to stock GenericFFB
powershell -ExecutionPolicy Bypass -File replacement\DragonRiseFFB\register.ps1 -RestoreOem
```

## What Was Not Completed (original list — status)

1. Confirm physical mapping of byte 3 and byte 5 to the two motors. — **packets OK; tactile map TBD**
2. Confirm whether `00 FA FE ...` is required. — **API accepts without it; feel TBD**
3. Confirm whether `00 F3 ...` is correct stop. — **sent OK; feel TBD**
4. Build HID test utility. — **DONE**
5. Registry-only effect GUID trim. — **not done**
6. Implement replacement `IDirectInputEffectDriver`. — **DONE (source+bin)**
7. Build x64 and x86. — **DONE**
8. Register under new CLSID / retarget OEM. — **scripts ready; not activated**
9. Test with x360ce and DirectInput sample. — **not done**
10. Steam path. — **not done**

## Smallest Fix Options, Current Ranking

### 1. Registry Fix

Potentially useful but unlikely to fully fix rumble.

Try reducing advertised effects to only:

```text
Constant Force
Sine Wave, only if replacement supports it
```

But registry alone cannot fix broken `StartEffect`/`StopEffect` handling or wrong packet mapping.

### 2. Configuration Fix

No clear config file found. Program directories expected from installer strings were missing:

```text
C:\Program Files\VID_0079&PID_0006
C:\Program Files (x86)\VID_0079&PID_0006
C:\Program Files\SPEEDLINK STRIKE Gamepad
C:\Program Files (x86)\SPEEDLINK STRIKE Gamepad
```

### 3. COM Registration Fix

Viable. Could register a replacement DLL under a new CLSID, then change:

```text
...\Joystick\OEM\VID_0079&PID_0006\OEMForceFeedback\CLSID
```

to point to the new CLSID.

Safer than overwriting the existing DLL.

### 4. DLL Wrapper

Possible but not ideal. Since the original logic is poor, wrapping it is less useful than replacing it.

### 5. DLL Patch

Possible but not recommended as first implementation. The installed DLL is unsigned and small enough to patch, but patching would need stable offsets and would still leave poor lifecycle behavior.

### 6. Full DLL Replacement

Best technical path.

Implement a clean `IDirectInputEffectDriver` COM server:

- same CLSID or new CLSID
- x64 and x86 builds
- `DllRegisterServer` / `DllUnregisterServer`
- `DllGetClassObject`
- `IClassFactory`
- `IDirectInputEffectDriver`
- open HID path from `DeviceID`
- maintain effect map keyed by DirectInput effect ID
- handle `DownloadEffect`, `StartEffect`, `StopEffect`, `DestroyEffect`, `SetGain`, `SendForceFeedbackCommand`
- convert effects to two motor intensities
- send `00 51 00 L 00 R 00 00` plus commit packet
- send stop packet when both motors zero

## Recommended Replacement Behavior

Do not emulate real spring/damper physics. This controller is only a rumble device.

Recommended mapping:

```text
Constant Force:
  intensity = abs(DICONSTANTFORCE.lMagnitude) / 10000.0
  route to both motors unless caller clearly specifies one axis/direction

Ramp:
  use average absolute magnitude of start/end, or current point if scheduling waveform

Sine/Square/Triangle/Sawtooth:
  use period and magnitude if implemented, but for compatibility a constant rumble with magnitude is acceptable

Spring/Damper/Inertia/Friction:
  for rumble compatibility either ignore or map coefficient magnitude to low-level rumble
```

For x360ce/Steam-like rumble, the most important case is usually Constant Force or a simple periodic effect. Map unknown effects conservatively to both motors using available magnitude.

Avoid using live joystick axis positions or effect direction vectors as raw motor intensity. Direction may choose left vs right balance, but should not cause stick movement to trigger rumble.

## Proposed Clean HID Packet API

```c
void SendRumble(uint8_t left, uint8_t right) {
    if (left == 0 && right == 0) {
        uint8_t stop[8] = {0x00, 0xF3, 0x00, 0, 0, 0, 0, 0};
        HidD_SetOutputReport(hid, stop, 8);
        return;
    }

    uint8_t report[8] = {0x00, 0x51, 0x00, left, 0x00, right, 0x00, 0x00};
    uint8_t commit[8] = {0x00, 0xFA, 0xFE, 0, 0, 0, 0, 0};
    HidD_SetOutputReport(hid, report, 8);
    HidD_SetOutputReport(hid, commit, 8);
}
```

## Proposed Effect State Model

```c
struct EffectState {
    DWORD effect_id;
    GUID effect_guid;
    DIEFFECT effect_copy;
    bool downloaded;
    bool playing;
    DWORD iterations;
    DWORD start_tick;
    DWORD duration_ms;
    uint8_t left;
    uint8_t right;
};
```

Aggregate active effects:

```c
left = max(effect.left for playing effects)
right = max(effect.right for playing effects)
apply global gain
send only if changed or periodic timer tick requires update
```

## Architecture Diagram

```text
DirectInput app
  |
  | DirectInput8 / dinput.dll
  |
  | reads registry:
  | HKLM\...\Joystick\OEM\VID_0079&PID_0006\OEMForceFeedback
  |   CLSID = {0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
  |
  v
COM InProcServer32
  C:\WINDOWS\GenericFFBDriver\GenericFFBDriver64.dll
  or replacement DLL
  |
  | IDirectInputEffectDriver
  |   DeviceID
  |   DownloadEffect
  |   StartEffect
  |   StopEffect
  |   DestroyEffect
  |
  v
HID user-mode API
  CreateFileW("\\?\hid#vid_0079&pid_0006#...")
  HidD_SetOutputReport()
  |
  v
USB HID output report, 8 bytes
  00 51 00 LL 00 RR 00 00
  00 FA FE 00 00 00 00 00
  |
  v
DragonRise controller rumble motors
```

## Suggested Next Steps for Another Agent

1. Use `tools\dump_ffb_function.py` and `ffb64.analysis.json` to verify the packet facts.
2. Write a small HID test sender first, not a COM DLL.
3. Confirm motor byte mapping with controlled reports.
4. Confirm whether commit packet is required.
5. Export current registry keys before changing anything.
6. Try a registry-only experiment: remove or disable unsupported effect keys, leaving only Constant and maybe Sine. This may improve x360ce behavior but probably will not fix Steam.
7. Build replacement COM DLL in C++ using Windows SDK `dinputd.h`.
8. Use a new CLSID during development, then point `OEMForceFeedback\CLSID` to it.
9. Implement both x64 and x86. x360ce/older games may be 32-bit.
10. Test with a minimal DirectInput FFB test app before Steam.

## Commands/Methods That Were Useful

Read-only PnP enumeration:

```powershell
Get-PnpDevice -PresentOnly |
  Where-Object { $_.InstanceId -match 'VID_0079|PID_0006' -or $_.FriendlyName -match 'game|controller' } |
  Select-Object Status,Class,FriendlyName,InstanceId
```

Driver properties:

```powershell
Get-PnpDeviceProperty -InstanceId 'USB\VID_0079&PID_0006\5&11CCA10D&0&3'
Get-PnpDeviceProperty -InstanceId 'HID\VID_0079&PID_0006\6&109BD56D&0&0000'
```

Registry dump:

```cmd
reg query "HKLM\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_0079&PID_0006" /s
reg query "HKLM\SOFTWARE\Classes\CLSID\{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}" /s
reg query "HKLM\SOFTWARE\Classes\Wow6432Node\CLSID\{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}" /s
```

Run PE analysis:

```cmd
python tools\analyze_ffb_pe.py C:\WINDOWS\GenericFFBDriver\GenericFFBDriver64.dll --json-out ffb64.analysis.json
python tools\analyze_ffb_pe.py C:\WINDOWS\GenericFFBDriver\GenericFFBDriver32.dll --json-out ffb32.analysis.json
python tools\dump_ffb_function.py
```

## Bottom Line

This is not a kernel driver problem. The active HID driver is Microsoft `input.inf`/`HidUsb`.

The current FFB layer is a user-mode DirectInput OEM effect-driver COM DLL. It translates some DirectInput effect data into simple 8-byte HID output reports. The key dynamic rumble packet is:

```text
00 51 00 <motorA> 00 <motorB> 00 00
```

followed by:

```text
00 FA FE 00 00 00 00 00
```

Stop is probably:

```text
00 F3 00 00 00 00 00 00
```

The installed DLL is likely incompatible with Steam/x360ce because it has incomplete DirectInput lifecycle handling and crude axis-based motor mapping. A clean replacement COM DLL is the best path forward.
