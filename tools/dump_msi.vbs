Option Explicit
Dim installer, db, view, rec, i, ncols, names, line, t, tables, nameArr
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
    ReDim nameArr(ncols)
    names = ""
    For i = 1 To ncols
      nameArr(i) = rec.StringData(i)
      If i > 1 Then names = names & ", "
      names = names & nameArr(i)
    Next
    WScript.Echo "COLS: " & names
    Do
      Set rec = view.Fetch
      If rec Is Nothing Then Exit Do
      line = ""
      For i = 1 To ncols
        If i > 1 Then line = line & " | "
        On Error Resume Next
        line = line & nameArr(i) & "=" & rec.StringData(i)
        If Err.Number <> 0 Then
          Err.Clear
          line = line & nameArr(i) & "=" & CStr(rec.IntegerData(i))
        End If
        On Error GoTo 0
      Next
      WScript.Echo line
    Loop
  End If
  On Error GoTo 0
Next
