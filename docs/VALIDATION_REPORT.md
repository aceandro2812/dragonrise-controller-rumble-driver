# Release Validation Report — DragonRise FFB Replacement

Date: 2026-07-13 (updated after hardware + x360ce success)  
Suite binary: `bin\ffb_validate.exe`  
Build: `validation\build_validate.bat` (or rebuild via cl as documented)

**Canonical success write-up:** `docs\WORKING_SETUP.md`  
**x360ce settings:** `docs\X360CE_RUMBLE.md`

## Automated suite result

```text
PASS: 74+   FAIL: 0
RESULT: validation suite PASSED (unit/COM/HID)
```

Hardware and x360ce motor-test gates: **PASS** (see section 6).

---

## 1. COM correctness

| Check | Result |
|-------|--------|
| `QueryInterface` IUnknown / IDirectInputEffectDriver | Pass |
| Wrong IID → `E_NOINTERFACE` | Pass |
| `AddRef` / `Release` symmetric | Pass |
| `CreateInstance` via `IClassFactory` | Pass |
| Object count restored after full release (no leak) | Pass |
| Multithreaded Download/Start/Stop/Destroy (3 threads × 40) | Pass |
| Worker thread + mutex (no deadlock in suite) | Pass |
| `DeviceID` attach/detach dry-run | Pass |
| `GetVersions` / `GetForceFeedbackState` / `GetEffectStatus` | Pass |
| `Escape` → `DIERR_UNSUPPORTED` | Pass |
| `SetGain`, `SendForceFeedbackCommand` (STOPALL/RESET) | Pass |
| Lifecycle: Download → Start → Stop → Destroy | Pass |

### Method map

| Method | Behavior |
|--------|----------|
| `DeviceID` | Opens HID path (or dry-run); starts 10 ms worker |
| `GetVersions` | Firmware/HW 1, driver 1.1 |
| `Escape` | Unsupported |
| `SetGain` | 0..10000 live re-sample |
| `SendForceFeedbackCommand` | RESET/STOPALL/PAUSE/CONTINUE/actuators |
| `GetForceFeedbackState` | POWERON + ACTUATORS + EMPTY/STOPPED/PAUSED |
| `DownloadEffect` | Stores full params; starts only if `DIEP_START` |
| `StartEffect` | Plays; honors `DIES_SOLO` |
| `StopEffect` / `DestroyEffect` | Real |
| `GetEffectStatus` | `DIEGES_PLAYING` when active |

---

## 2. Effect → dual-motor translation

Convention:

- **Motor A (HID byte 3)** = **low-frequency / large**
- **Motor B (HID byte 5)** = **high-frequency / small**
- Intensities **0..254** (0xFE cap, stock-compatible)
- **Not** “always both equal”
- **Not** driven by live stick axes (only optional effect direction balance)

| Effect | Mapping |
|--------|---------|
| **Constant** | `base = \|magnitude\| × effectGain × deviceGain × envelope`. Split ≈ **LF 85% / HF 30%** of base (thump + light buzz). |
| **Ramp** | Interpolate `lStart→lEnd` over duration; same LF/HF split as constant. |
| **Sine** | `\|sin(2π t/period + phase)\| × magnitude (+ offset bias)`. Period ≤30 ms → HF-dominant; ≥250 ms → LF-dominant; blend between. |
| **Square** | On/off waveform; HF bias (+~10%) for harsher edge. |
| **Triangle** | Triangle 0..1 × magnitude; period→LF/HF blend. |
| **Sawtooth Up/Down** | Rising/falling saw × magnitude; period→LF/HF blend. |
| **Spring** | Soft presence from `max(\|posCoeff\|,\|negCoeff\|)` × 0.45, saturation-capped; **LF~55% / HF~15%**. No position feedback. |
| **Damper** | Same soft condition mapping. |
| **Inertia** | Same soft condition mapping. |
| **Friction** | Same soft condition mapping. |
| **Custom** | Fallback magnitude; LF~70% / HF~40%. |

Envelope: attack ramps `attackLevel→1`, fade ramps `1→fadeLevel` near end.  
Aggregation: every ~10 ms, `LF = max(playing)`, `HF = max(playing)`.

Measured suite samples (gain=10000):

| Case | LF | HF |
|------|----|----|
| Constant 8000 | 172 | 60 |
| Sine period 20 ms | 45 | 217 |
| Sine period 400 ms | 217 | 57 |
| Spring 8000 | 50 | (soft) |

---

## 3. HID packets

Validated format gate (reject invalid before wire):

| Packet | Bytes |
|--------|--------|
| Rumble | `00 51 00 <LF> 00 <HF> 00 00` |
| Commit | `00 FA FE 00 00 00 00 00` |
| Stop | `00 F3 00 00 00 00 00 00` |

Suite dry-run capture: rumble+commit on start, stop on stop — all format-valid.

---

## 4. Original vs replacement surface

Both stock and replacement export:

- `DllCanUnloadNow`
- `DllGetClassObject`
- `DllRegisterServer`
- `DllUnregisterServer`
- `RegisterVibrationDriver`

Identity preserved:

- CLSID `{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}`
- ProgID `GenericFFBDriver.FFBDriver`
- Paths `%SystemRoot%\GenericFFBDriver\GenericFFBDriver32/64.dll`

**Note:** Stock effect mapping is *worse* (axis-as-motor, stub lifecycle). Replacement is intentionally **behavior-improved**, not byte-identical motor output.

---

## 5. Logging (debug build)

```bat
replacement\DragonRiseFFB\build.bat debug
```

Or runtime: set `DRFFB_LOG=1`.

Log file: `%TEMP%\DragonRiseFFB.log`

Lines include:

- stage (DownloadEffect / sample / HID)
- handle, class, magnitude, duration, gain
- elapsed time
- LF/HF intensities
- 8-byte HID packets with timestamps

---

## 6. Release criteria checklist

| Gate | Status |
|------|--------|
| Automated COM + effect + HID suite | **PASS** |
| No analog-stick-induced mapping in code | **PASS** (direction only from effect dir, not device state) |
| HID left/right/both independent motors | **PASS** (`HidRumbleTest` / proof) |
| DirectInput CreateEffect path | **PASS** (`X360cePathTest`, `DiFfbTest`) |
| x360ce 4.17 Force Feedback test sliders | **PASS** (Type 2 + Positive directions) |
| Installer install (patched MSI + Setup) | **PASS** |
| Legacy DI games (arbitrary titles) | Optional / title-dependent |
| Steam without x360ce | **N/A** — Steam does not use DI FFB COM for this pad |

### Verdict

**Code/unit + hardware HID + DirectInput + x360ce motor test: PASS.**  
Ship the patched package with `docs\WORKING_SETUP.md` and `docs\X360CE_RUMBLE.md` for end users who need XInput games.

Known engineering fixes required for that result (do not regress): MSI signature strip, Setup uninstall-then-install, continuous HID refresh, LF/HF wire map (hid-dr), no `ccs=UTF-8` in FFB log fopen, register both 32- and 64-bit COM.

---

## How to re-run

```bat
REM Rebuild driver
replacement\DragonRiseFFB\build.bat all

REM Build + run suite (from repo root after compiling validate)
bin\ffb_validate.exe
```

Sources under test:

- `replacement\DragonRiseFFB\effect_map.*` — pure translation
- `replacement\DragonRiseFFB\effect_driver.*` — COM
- `replacement\DragonRiseFFB\hid_rumble.*` — HID + format gate
- `validation\ffb_validate.cpp` — suite
