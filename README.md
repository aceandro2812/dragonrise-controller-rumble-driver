# Speedlink SL-6535 / DragonRise FFB patched driver

**Status: working** (HID + DirectInput + x360ce Force Feedback).

Drop-in replacement for the Generic USB Gamepad Vibration Driver (`VID_0079` & `PID_0006`) so dual-motor rumble works correctly via DirectInput and tools like x360ce.

## Install

```text
dist\SPEED-LINK-SL-6535-BK_Driver_V1.0_FFB-Patched\Setup.exe
```

See [docs/WORKING_SETUP.md](docs/WORKING_SETUP.md) for full setup, including **x360ce Force Type 2** settings.

## Docs

| Doc | Contents |
|------|----------|
| [docs/WORKING_SETUP.md](docs/WORKING_SETUP.md) | Verified setup, x360ce settings, fix history |
| [docs/X360CE_RUMBLE.md](docs/X360CE_RUMBLE.md) | x360ce 4.x rumble guide |
| [docs/PATCHED_INSTALLER.md](docs/PATCHED_INSTALLER.md) | Installer package / rebuild |
| [docs/VALIDATION_REPORT.md](docs/VALIDATION_REPORT.md) | Automated suite + hardware gates |
| [RE_HANDOFF.md](RE_HANDOFF.md) | Reverse-engineering notes |

## Author / contributor

- **GitHub:** [aceandro2812](https://github.com/aceandro2812)
- **Email:** jatin096@gmail.com

## License / disclaimer

This is an unofficial community fix for legacy DragonRise / Speedlink vibration COM DLLs. Use at your own risk. Not affiliated with Speedlink, DragonRise, or Microsoft.
