#!/usr/bin/env python3
"""
Rebuild Generic USB Gamepad Vibration Driver MSI with our drop-in DLLs.

Inputs:
  - Original MSI (copied from Windows Installer cache or vendor)
  - bin/GenericFFBDriver32.dll
  - bin/GenericFFBDriver64.dll

Output:
  - dist/patched/GenericUSBGamepadVibration.msi

The MSI keeps ProductCode, custom action RegisterVibrationDriver, SelfReg,
and install paths. Only cabinet payload + File sizes change.
"""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


FILE_64_KEY = "_C378259A8D8C4FB880EAEC27ECC1720F"
FILE_32_KEY = "_CACB481DB8B74D96B26F50DCA0C3F56A"
CAB_STREAM = "_0E4E919088C332B15EB2315C679EE837"


def find_msidb() -> Path:
    roots = [
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Windows Kits"
        / "10"
        / "bin",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "Windows Kits"
        / "10"
        / "bin",
    ]
    for root in roots:
        if not root.exists():
            continue
        for p in root.rglob("MsiDb.exe"):
            return p
    raise FileNotFoundError("MsiDb.exe not found (install Windows SDK)")


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd))
    subprocess.check_call(cmd, **kw)


def update_file_sizes_vbs(msi: Path, size64: int, size32: int) -> None:
    vbs = f'''
Option Explicit
Dim installer, db, view, rec
Set installer = CreateObject("WindowsInstaller.Installer")
Set db = installer.OpenDatabase("{msi.as_posix()}", 1)
Set view = db.OpenView("UPDATE `File` SET `FileSize`={size64} WHERE `File`='{FILE_64_KEY}'")
view.Execute
view.Close
Set view = db.OpenView("UPDATE `File` SET `FileSize`={size32} WHERE `File`='{FILE_32_KEY}'")
view.Execute
view.Close
' Drop file hashes so modified payload is not rejected
On Error Resume Next
Set view = db.OpenView("DELETE FROM `MsiFileHash`")
view.Execute
view.Close
On Error GoTo 0
' Remove digital signature tables if present so package remains installable
On Error Resume Next
Set view = db.OpenView("DELETE FROM `MsiDigitalSignature`")
view.Execute
view.Close
Set view = db.OpenView("DELETE FROM `MsiDigitalSignatureEx`")
view.Execute
view.Close
On Error GoTo 0
db.Commit
'''
    with tempfile.NamedTemporaryFile("w", suffix=".vbs", delete=False, encoding="ascii") as f:
        f.write(vbs)
        vbs_path = f.name
    try:
        run(["cscript", "//Nologo", vbs_path])
    finally:
        os.unlink(vbs_path)


def strip_ole_signature_streams(msi: Path) -> None:
    """Best-effort: leave MSI unsigned after modification."""
    try:
        import olefile  # type: ignore
    except ImportError:
        return
    # olefile cannot easily delete streams in-place; VBS delete is enough for tables.
    # Embedded DigitalSignature stream may remain but Windows still installs unsigned MSIs.
    _ = olefile


def make_cabinet(files: list[tuple[Path, str]], cab_path: Path) -> None:
    """
    Build an MSZIP cabinet containing files under the given cabinet names.
    files: list of (source_path, name_inside_cab)
    """
    ddf = cab_path.with_suffix(".ddf")
    lines = [
        ".OPTION EXPLICIT",
        f'.Set CabinetNameTemplate={cab_path.name}',
        f'.Set DiskDirectoryTemplate={cab_path.parent}',
        ".Set CompressionType=MSZIP",
        ".Set UniqueFiles=ON",
        ".Set Cabinet=ON",
        ".Set Compress=ON",
        ".Set MaxDiskSize=0",
    ]
    # Work directory with renamed copies so makecab uses correct internal names.
    work = cab_path.parent / "_cab_stage"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    for src, name in files:
        dst = work / name
        shutil.copy2(src, dst)
        # Quote path for makecab
        lines.append(f'"{dst}" {name}')
    ddf.write_text("\n".join(lines) + "\n", encoding="ascii")
    run(["makecab", "/F", str(ddf)], cwd=str(cab_path.parent))
    if not cab_path.exists():
        # makecab may write to DiskDirectory1
        candidates = list(cab_path.parent.glob("*.cab"))
        raise RuntimeError(f"Cabinet not created at {cab_path}, found {candidates}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--original-msi",
        type=Path,
        default=None,
        help="Original Generic USB Gamepad Vibration Driver MSI",
    )
    ap.add_argument("--dll32", type=Path, required=True)
    ap.add_argument("--dll64", type=Path, required=True)
    ap.add_argument("--out-msi", type=Path, required=True)
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[1]
    if args.original_msi is None:
        candidates = [
            root / "analysis" / "GenericUSBGamepadVibration.msi",
            Path(r"C:\WINDOWS\Installer\27d46.msi"),
        ]
        for c in candidates:
            if c.exists():
                args.original_msi = c
                break
        if args.original_msi is None:
            print("ERROR: provide --original-msi", file=sys.stderr)
            return 1

    for p in (args.dll32, args.dll64, args.original_msi):
        if not p.exists():
            print(f"ERROR: missing {p}", file=sys.stderr)
            return 1

    msidb = find_msidb()
    args.out_msi.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="ffb_msi_") as td:
        td = Path(td)
        work_msi = td / "work.msi"
        shutil.copy2(args.original_msi, work_msi)

        # Extract embedded cabinet stream
        run([str(msidb), "-d", str(work_msi), "-x", CAB_STREAM], cwd=str(td))
        cab_extracted = td / CAB_STREAM
        if not cab_extracted.exists():
            print("ERROR: failed to extract cab stream", file=sys.stderr)
            return 1

        # Expand original to discover naming (already known)
        expand_dir = td / "expanded"
        expand_dir.mkdir()
        run(["expand", str(cab_extracted), "-F:*", str(expand_dir)])

        # Stage replacement files with MSI file keys as names
        stage = td / "stage"
        stage.mkdir()
        dst64 = stage / FILE_64_KEY
        dst32 = stage / FILE_32_KEY
        shutil.copy2(args.dll64, dst64)
        shutil.copy2(args.dll32, dst32)

        # Rebuild cabinet
        new_cab = td / "new.cab"
        ddf = td / "cabinet.ddf"
        ddf.write_text(
            "\n".join(
                [
                    ".OPTION EXPLICIT",
                    f".Set CabinetNameTemplate={new_cab.name}",
                    f".Set DiskDirectoryTemplate={td}",
                    ".Set CompressionType=MSZIP",
                    ".Set UniqueFiles=ON",
                    ".Set Cabinet=ON",
                    ".Set Compress=ON",
                    ".Set MaxDiskSize=0",
                    # Sequence order: 64-bit file first (Sequence=1), then 32-bit (Sequence=2)
                    f'"{dst64}" {FILE_64_KEY}',
                    f'"{dst32}" {FILE_32_KEY}',
                    "",
                ]
            ),
            encoding="ascii",
        )
        run(["makecab", "/F", str(ddf)])

        # Replace embedded cabinet stream via OLE structured storage helper.
        # (msidb -a cannot replace an existing stream; MSI names are encoded.)
        rep = Path(__file__).resolve().parent / "msi_replace_stream.exe"
        if not rep.exists():
            # Build helper if missing
            print("msi_replace_stream.exe missing — build tools/msi_replace_stream.cpp first")
            return 1
        run([str(rep), str(work_msi), ".", str(new_cab)])

        size64 = args.dll64.stat().st_size
        size32 = args.dll32.stat().st_size
        update_file_sizes_vbs(work_msi, size64, size32)

        shutil.copy2(work_msi, args.out_msi)
        print(f"Patched MSI written: {args.out_msi}")
        print(f"  GenericFFBDriver64.dll size={size64}")
        print(f"  GenericFFBDriver32.dll size={size32}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
