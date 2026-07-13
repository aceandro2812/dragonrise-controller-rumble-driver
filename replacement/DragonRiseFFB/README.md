# GenericFFBDriver — Drop-in FFB Effect Driver (fixed)

Implements `IDirectInputEffectDriver` for DragonRise / Speedlink pads  
(`USB\VID_0079&PID_0006`) as a **transparent replacement** for stock:

- `GenericFFBDriver32.dll`
- `GenericFFBDriver64.dll`
- CLSID `{0AB5665A-4549-4FD0-A952-5A2B9699BDA8}`
- ProgID `GenericFFBDriver.FFBDriver`
- Export `RegisterVibrationDriver` (MSI custom action)

## Build

```bat
replacement\DragonRiseFFB\build.bat all
```

Outputs:

- `bin\GenericFFBDriver32.dll`
- `bin\GenericFFBDriver64.dll`

## Package into installer

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_patched_package.ps1
```

Produces:

```text
dist\SPEED-LINK-SL-6535-BK_Driver_V1.0_FFB-Patched\
  Setup.exe
  FFB\GenericUSBGamepadVibration.msi   ← stock MSI UI/uninstall, fixed DLLs
```

## HID packets

Same as reverse-engineered stock:

| Purpose | Bytes |
|--------|--------|
| Rumble | `00 51 00 <A> 00 <B> 00 00` |
| Commit | `00 FA FE 00 00 00 00 00` |
| Stop   | `00 F3 00 00 00 00 00 00` |

## Lifecycle fixes

Real `DownloadEffect` / `StartEffect` / `StopEffect` / `DestroyEffect` / `SetGain`.  
Default both motors equal (no stick-axis motor bug).
