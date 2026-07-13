# Working setup — Speedlink SL-6535 / DragonRise FFB (verified)

**Status: WORKING** (hardware validated 2026-07-13)

Controller: USB gamepad **VID_0079 & PID_0006** (Speedlink SL-6535 / DragonRise generic)  
Host: Windows with patched **Generic USB Gamepad Vibration Driver**

---

## What works

| Path | Tool / app | Result |
|------|------------|--------|
| Raw HID dual motors | `bin\HidRumbleTest.exe` left / right / both / proof | Independent motors; D stronger than B/C |
| DirectInput FFB (x360ce-style) | `bin\X360cePathTest.exe` | CreateEffect + rumble OK |
| DirectInput FFB (simple) | `bin\DiFfbTest.exe` | OK after COM fix |
| x360ce 4.17 motor test | Force Feedback test sliders | **Working** |
| Installer | `dist\...\Setup.exe` + patched MSI | Install / reinstall OK |

Steam **does not** use this COM path by itself. For Steam/XInput games, use **x360ce** (or similar) on top of this driver.

---

## Architecture (end state)

```text
Game (XInput rumble)
  → x360ce 4.x (ViGEm virtual Xbox pad)
  → DirectInput Force Feedback on real "USB Joystick"
  → COM CLSID {0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
  → GenericFFBDriver32.dll  (x360ce is 32-bit)
     or GenericFFBDriver64.dll (64-bit DI hosts)
  → HID output reports to the pad
```

Install location:

```text
C:\Windows\GenericFFBDriver\GenericFFBDriver32.dll
C:\Windows\GenericFFBDriver\GenericFFBDriver64.dll
```

OEM (unchanged identity):

```text
HKLM\...\Joystick\OEM\VID_0079&PID_0006\OEMForceFeedback
  CLSID = {0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
```

### HID packets (Linux hid-dr + stock GenericFFB)

```text
Rumble:  00 51 00 <weak/HF> 00 <strong/LF> 00 00
Commit:  00 FA FE 00 00 00 00 00
Stop:    00 F3 00 00 00 00 00 00
```

- **Wire byte 3** = weak / small / high-frequency motor  
- **Wire byte 5** = strong / large / low-frequency motor  
- Intensities `0x00`..`0xFE` (avoid weak `0x0A` → bump to `0x0B`, Linux quirk)  
- Stock driver and our replacement **re-send** rumble about every **10–15 ms** (single-shot fades)

---

## Install (end user)

1. Run:

   ```text
   dist\SPEED-LINK-SL-6535-BK_Driver_V1.0_FFB-Patched\Setup.exe
   ```

2. Next → Install → Finish (UAC: allow).  
3. If stock product was already installed, Setup **uninstalls then installs** (SecureRepair blocks repair of a modified MSI).  
4. Uninstall later: Windows **Apps** → “Generic USB Gamepad Vibration Driver”.

Rebuild package from sources:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_patched_package.ps1
```

---

## x360ce 4.17 — settings that work (SpeedLink)

App used: `x360ce` **4.17.15** (32-bit PE).  
Prep script: `tools\fix_x360ce_rumble.ps1`

### Before opening x360ce

1. Fully quit **Steam** (tray → Exit).  
2. Fully quit any other remapper.  
3. Optional: run `tools\fix_x360ce_rumble.ps1` (re-registers COM + runs path test).

### Map device

1. Open x360ce.  
2. **Issues**: install **ViGEmBus** if missing.  
3. **Controller 1** → **Add** → select real pad (**USB Joystick**), **not** the virtual Xbox pad.  
4. Enable the mapped device. Map buttons (AutoMap OK).

### Force Feedback (required for this pad)

| Setting | Working value |
|--------|----------------|
| Enable Force Feedback | **ON** |
| **Force Type** | **Type 2** (SpeedLink needs both axes per effect; see x360ce source comment) |
| Overall strength | **100** |
| Left motor strength | **100** |
| Right motor strength | **100** |
| Left direction | **Positive (+1)** — not zero |
| Right direction | **Positive (+1)** — not zero |
| Force swap motor | **OFF** (try ON only if sides feel reversed) |

Then use **Test Left Motor** / **Test Right Motor** sliders.

### In games

1. Configure the game in x360ce (or drop libraries as the app documents).  
2. Steam: set that game’s **Steam Input** to **Disabled** when using x360ce.  
3. Enable vibration in the game options.

---

## Verification tools

| Binary | Purpose |
|--------|---------|
| `bin\HidRumbleTest.exe` | Pure HID: `left`, `right`, `both`, `proof`, `sequence` (continuous refresh) |
| `bin\X360cePathTest.exe` | Same CreateEffect style as x360ce (ObjectIds + exclusive) |
| `bin\DiFfbTest.exe` | Simple DirectInput ConstantForce test |
| `bin\ffb_validate.exe` | Automated COM / effect-map / HID format suite |
| `tools\fix_x360ce_rumble.ps1` | Re-register COM + run path test + print UI settings |

Debug driver log: set environment variable `DRFFB_LOG=1`, then exercise DI/x360ce.  
Log file: `%TEMP%\DragonRiseFFB.log`

---

## Key fixes that made this work

Documented so rebuilds do not regress.

### 1. MSI digital signature after cabinet replace

- Patching the MSI left a broken Authenticode stream → Windows rejected install (**1718 / 1625 / 1603**).  
- **Fix:** strip `\x05DigitalSignature` and `\x05MsiDigitalSignatureEx` after stream replace (`tools\msi_replace_stream.cpp`).  
- Use `L"\x0005" L"DigitalSignature"` (not `L"\x0005Digital..."` — C hex escape eats the `D`).

### 2. SecureRepair on reinstall

- Same ProductCode as stock + modified package → SecureRepair **1603**.  
- **Fix:** Setup uninstalls product `{50CD8B4D-CD82-49D1-9E0A-2B7887448068}` then installs fresh.

### 3. Dual-motor feel “both always / both light”

- Test tool sent **one** packet then slept; motors need **continuous refresh**.  
- Default intensity was too low for a while.  
- LF/HF wire order must match Linux **hid-dr** (weak @ byte3, strong @ byte5).  
- **Fix:** continuous hold in `HidRumbleTest`; never skip identical SendRumble in the COM worker; correct wire mapping.

### 4. DirectInput / x360ce “Class not registered” (0x80040154)

- Logging used `_wfopen_s(..., L"a+, ccs=UTF-8")` which **crashed the COM DLL** under DI load (`STATUS_STACK_BUFFER_OVERRUN`).  
- x360ce is **32-bit** → needs `GenericFFBDriver32.dll` registered via `SysWOW64\regsvr32`.  
- **Fix:** open log as plain `a+`; always regsvr **both** 32 and 64 after deploy.

### 5. x360ce Force Type / directions

- x360ce uses `EffectFlags.ObjectIds` and, for SpeedLink, **Type 2** (both axes on each effect).  
- Directions of **0** produce no useful force; use **Positive**.  
- Device must be acquired **exclusive** for CreateEffect (close Steam).

---

## Rebuild / redeploy developer notes

```bat
REM Driver
replacement\DragonRiseFFB\build.bat all

REM Hot-deploy (elevated)
copy /Y bin\GenericFFBDriver32.dll %SystemRoot%\GenericFFBDriver\
copy /Y bin\GenericFFBDriver64.dll %SystemRoot%\GenericFFBDriver\
%SystemRoot%\SysWOW64\regsvr32.exe /s %SystemRoot%\GenericFFBDriver\GenericFFBDriver32.dll
%SystemRoot%\System32\regsvr32.exe /s %SystemRoot%\GenericFFBDriver\GenericFFBDriver64.dll

REM Full package
powershell -ExecutionPolicy Bypass -File tools\build_patched_package.ps1
```

Sources of truth:

- `replacement\DragonRiseFFB\` — COM + HID + effect map  
- `tools\patch_ffb_msi.py`, `tools\msi_replace_stream.cpp` — MSI patch  
- `tools\SetupBootstrap.cs` — Setup.exe  
- `tools\HidRumbleTest.cs` — HID tester  
- `docs\X360CE_RUMBLE.md` — longer x360ce troubleshooting  
- `docs\PATCHED_INSTALLER.md` — package layout  

---

## Hardware gates (completed)

- [x] HID left/right/both independent feel  
- [x] DirectInput CreateEffect path  
- [x] x360ce Force Feedback test sliders  
- [ ] Optional: specific Steam games with x360ce + Steam Input disabled (user games vary)  
- [ ] Optional: uninstall clean check after long-term use  

**Verdict: FFB stack + x360ce path validated on real VID_0079&PID_0006 hardware.**
