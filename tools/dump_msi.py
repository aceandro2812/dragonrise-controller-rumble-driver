"""Dump key tables from an MSI using WindowsInstaller COM."""
import sys
import win32com.client  # may not exist

def main(path):
    try:
        installer = win32com.client.Dispatch("WindowsInstaller.Installer")
    except Exception:
        # pure ctypes COM is painful; use subprocess + VBScript
        return dump_via_vbs(path)

    db = installer.OpenDatabase(path, 0)
    tables = [
        "Property", "File", "Component", "Registry", "CustomAction",
        "Directory", "Feature", "Class", "ProgId", "Media",
        "InstallExecuteSequence", "CreateFolder", "DuplicateFile",
        "MoveFile", "RemoveFile", "SelfReg", "Shortcut",
    ]
    for t in tables:
        print(f"\n===== {t} =====")
        try:
            view = db.OpenView(f"SELECT * FROM `{t}`")
            view.Execute(None)
            # column names
            colinfo = view.ColumnInfo(0)
            ncols = colinfo.FieldCount
            names = [colinfo.StringData(i) for i in range(1, ncols + 1)]
            print("COLS:", ", ".join(names))
            while True:
                rec = view.Fetch()
                if rec is None:
                    break
                parts = []
                for i, name in enumerate(names, 1):
                    try:
                        val = rec.StringData(i)
                    except Exception:
                        try:
                            val = str(rec.IntegerData(i))
                        except Exception:
                            val = "?"
                    parts.append(f"{name}={val}")
                print(" | ".join(parts))
        except Exception as e:
            print("FAIL:", e)


def dump_via_vbs(path):
    import os
    import subprocess
    import tempfile

    vbs = r'''
Option Explicit
Dim installer, db, view, rec, i, ncols, names, line, t, tables
Set installer = CreateObject("WindowsInstaller.Installer")
Set db = installer.OpenDatabase(WScript.Arguments(0), 0)
tables = Array("Property","File","Component","Registry","CustomAction","Directory","Feature","Class","ProgId","Media","InstallExecuteSequence","SelfReg","CreateFolder","Shortcut","FeatureComponents","MsiFileHash")
For Each t In tables
  WScript.Echo ""
  WScript.Echo "=====" & t & "====="
  On Error Resume Next
  Set view = db.OpenView("SELECT * FROM `" & t & "`")
  If Err.Number <> 0 Then
    WScript.Echo "FAIL: " & Err.Description
    Err.Clear
  Else
    view.Execute
    Set rec = view.ColumnInfo(0)
    ncols = rec.FieldCount
    names = ""
    For i = 1 To ncols
      If i > 1 Then names = names & ", "
      names = names & rec.StringData(i)
    Next
    WScript.Echo "COLS: " & names
    Do
      Set rec = view.Fetch
      If rec Is Nothing Then Exit Do
      line = ""
      For i = 1 To ncols
        If i > 1 Then line = line & " | "
        line = line & Split(names, ", ")(i-1) & "=" & rec.StringData(i)
      Next
      WScript.Echo line
    Loop
  End If
  On Error GoTo 0
Next
'''
    fd, vbs_path = tempfile.mkstemp(suffix=".vbs")
    os.close(fd)
    with open(vbs_path, "w", encoding="ascii", errors="replace") as f:
        f.write(vbs)
    try:
        out = subprocess.check_output(
            ["cscript", "//Nologo", vbs_path, path],
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
        )
        print(out)
    finally:
        os.remove(vbs_path)


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else r"analysis\GenericUSBGamepadVibration.msi"
    dump_via_vbs(path)
