# x360ce 4.x + DragonRise / Speedlink rumble

**Status: WORKING** (verified 2026-07-13) — see also `docs\WORKING_SETUP.md`.

## Stack

```text
Game / x360ce motor test
  → XInput vibration
  → x360ce maps to DirectInput Force Feedback
  → GenericFFBDriver32.dll  (x360ce 4.x is 32-bit)
  → HID 00 51 00 <weak> 00 <strong> 00 00  + commit
```

App used in validation: **x360ce 4.17.15** (`x360ce.exe`, PE machine x86).

---

## One-shot prep

```powershell
cd <repo>
powershell -ExecutionPolicy Bypass -File tools\fix_x360ce_rumble.ps1
```

This re-registers COM (32+64), checks OEM CLSID, and runs `bin\X360cePathTest.exe` (should rumble ~3 s).

**Close Steam completely** before x360ce Force Feedback tests (exclusive acquire).

---

## x360ce UI — working values for this pad

| Setting | Value |
|--------|--------|
| Device | Real **USB Joystick** (VID_0079&PID_0006), not virtual Xbox |
| Enable mapped device | ON |
| ViGEmBus | Installed (Issues tab) |
| Enable Force Feedback | **ON** |
| **Force Type** | **Type 2** |
| Overall strength | 100 |
| Left / Right motor strength | 100 |
| Left / Right direction | **Positive (+1)** — not 0 |
| Swap motors | OFF (unless sides feel reversed) |

Then use **Test Left Motor** / **Test Right Motor**.

### Why Type 2 and Positive directions?

From x360ce `ForceFeedbackState.cs` (upstream):

- SpeedLink-style pads need **both axes** listed on each effect for separate motors (**Type 2**).  
- Directions on the first axis should be **Positive** or the force does nothing useful.  
- CreateEffect requires the device **acquired exclusive**.

---

## Prerequisites

1. Patched driver installed (`Setup.exe` from `dist\...\FFB-Patched\`).  
2. Files present:

   ```text
   C:\Windows\GenericFFBDriver\GenericFFBDriver32.dll
   C:\Windows\GenericFFBDriver\GenericFFBDriver64.dll
   ```

3. OEM:

   ```text
   ...\OEM\VID_0079&PID_0006\OEMForceFeedback\CLSID
     = {0AB5665A-4549-4FD0-A952-5A2B9699BDA8}
   ```

4. Optional checks:

   ```text
   bin\HidRumbleTest.exe left
   bin\X360cePathTest.exe
   ```

---

## In-game

1. Add game in x360ce / install its libraries as documented by x360ce.  
2. Steam: **Steam Input = Disabled** for that game while using x360ce.  
3. Enable vibration in game options.

Steam alone does **not** call this DirectInput FFB COM; a bridge (x360ce) is required for XInput titles.

---

## Troubleshooting

| Symptom | Fix |
|--------|-----|
| No rumble on test sliders | Type **2**, directions **Positive**, FF **ON**, Steam closed |
| `0x80040154` Class not registered | 32-bit COM: `SysWOW64\regsvr32 /s ...\GenericFFBDriver32.dll`; use post–DI-crash-fix DLL build; restart x360ce |
| Path test rumbles, x360ce does not | Wrong device selected, Type/direction, or Steam still running |
| Buttons OK, no rumble in game | Fix sliders first; Steam Input off; match 32/64 game hooks |
| Exclusive / access denied | Close Steam, second x360ce, other remappers |

Re-register both bitnesses (Admin):

```bat
%SystemRoot%\SysWOW64\regsvr32.exe /s %SystemRoot%\GenericFFBDriver\GenericFFBDriver32.dll
%SystemRoot%\System32\regsvr32.exe /s %SystemRoot%\GenericFFBDriver\GenericFFBDriver64.dll
```

Debug: `DRFFB_LOG=1` → `%TEMP%\DragonRiseFFB.log`

---

## Related docs

- `docs\WORKING_SETUP.md` — full verified setup + fix history  
- `docs\PATCHED_INSTALLER.md` — package / rebuild  
- `docs\VALIDATION_REPORT.md` — automated suite + hardware gates  
