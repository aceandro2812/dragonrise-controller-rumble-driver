// Elevated bootstrap Setup.exe for the patched Speedlink / Generic FFB package.
// UX: Welcome -> Install -> Finish (3 steps). Runs the patched MSI with full UI
// when possible; falls back to quiet install of embedded relative MSI path.
//
// Build:
//   %WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /optimize+
//     /target:winexe /platform:anycpu /win32manifest:app.manifest
//     /out:Setup.exe SetupBootstrap.cs

using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using System.Reflection;

[assembly: AssemblyTitle("SPEEDLINK Force Feedback Driver Setup")]
[assembly: AssemblyDescription("Patched FFB installer for VID_0079&PID_0006")]
[assembly: AssemblyCompany("Patched package")]
[assembly: AssemblyProduct("Generic USB Gamepad Vibration Driver (FFB Fixed)")]
[assembly: AssemblyVersion("1.0.0.0")]

static class Program
{
    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new WizardForm());
    }
}

sealed class WizardForm : Form
{
    readonly Label title;
    readonly Label body;
    readonly Button back;
    readonly Button next;
    readonly Button cancel;
    readonly ProgressBar progress;
    int page; // 0 welcome, 1 ready, 2 done

    public WizardForm()
    {
        Text = "SPEEDLINK / DragonRise Force Feedback Setup";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(520, 320);
        Font = new Font("Segoe UI", 9f);

        title = new Label
        {
            AutoSize = false,
            Font = new Font("Segoe UI", 12f, FontStyle.Bold),
            Location = new Point(24, 20),
            Size = new Size(470, 32)
        };
        body = new Label
        {
            AutoSize = false,
            Location = new Point(24, 60),
            Size = new Size(470, 160)
        };
        progress = new ProgressBar
        {
            Location = new Point(24, 230),
            Size = new Size(470, 18),
            Style = ProgressBarStyle.Marquee,
            Visible = false
        };
        back = new Button { Text = "< Back", Location = new Point(220, 270), Size = new Size(90, 28), Enabled = false };
        next = new Button { Text = "Next >", Location = new Point(320, 270), Size = new Size(90, 28) };
        cancel = new Button { Text = "Cancel", Location = new Point(420, 270), Size = new Size(90, 28) };

        back.Click += (s, e) => { if (page > 0) { page--; ShowPage(); } };
        next.Click += (s, e) => OnNext();
        cancel.Click += (s, e) => Close();
        AcceptButton = next;
        CancelButton = cancel;

        Controls.Add(title);
        Controls.Add(body);
        Controls.Add(progress);
        Controls.Add(back);
        Controls.Add(next);
        Controls.Add(cancel);
        ShowPage();
    }

    string PackageRoot
    {
        get
        {
            string exe = Application.StartupPath;
            // Prefer sibling FFB folder; also allow running from tools/
            return exe;
        }
    }

    string FindMsi()
    {
        string[] candidates = {
            Path.Combine(PackageRoot, "FFB", "GenericUSBGamepadVibration.msi"),
            Path.Combine(PackageRoot, "GenericUSBGamepadVibration.msi"),
            Path.Combine(PackageRoot, "..", "dist", "patched", "GenericUSBGamepadVibration.msi"),
        };
        foreach (var c in candidates)
        {
            try
            {
                string full = Path.GetFullPath(c);
                if (File.Exists(full))
                    return full;
            }
            catch { }
        }
        return null;
    }

    void ShowPage()
    {
        progress.Visible = false;
        back.Enabled = page == 1;
        cancel.Enabled = page < 2;
        next.Enabled = true;

        if (page == 0)
        {
            title.Text = "Welcome";
            body.Text =
                "This setup installs the Force Feedback (vibration) translation layer " +
                "for USB game controllers with hardware ID:\r\n\r\n" +
                "    USB\\VID_0079&PID_0006\r\n\r\n" +
                "It replaces only the user-mode DirectInput effect-driver COM DLL.\r\n" +
                "It does not install or modify kernel HID/USB drivers.\r\n\r\n" +
                "Click Next to continue.";
            next.Text = "Next >";
        }
        else if (page == 1)
        {
            title.Text = "Ready to install";
            string msi = FindMsi();
            body.Text =
                "The following component will be installed:\r\n\r\n" +
                "  • Generic USB Gamepad Vibration Driver (FFB fixed)\r\n" +
                "  • COM CLSID {0AB5665A-4549-4FD0-A952-5A2B9699BDA8}\r\n" +
                "  • Files: %SystemRoot%\\GenericFFBDriver\\GenericFFBDriver32/64.dll\r\n\r\n" +
                (msi != null
                    ? "Package:\r\n  " + msi
                    : "ERROR: MSI not found next to Setup.exe (FFB\\GenericUSBGamepadVibration.msi)") +
                "\r\n\r\nClick Install to begin.";
            next.Text = "Install";
            next.Enabled = msi != null;
        }
        else
        {
            title.Text = "Completed";
            body.Text =
                "Installation finished.\r\n\r\n" +
                "You can close this wizard. Uninstall via Windows Settings → Apps, " +
                "or \"Generic USB Gamepad Vibration Driver\" in Programs and Features.\r\n\r\n" +
                "Optional: run Driver\\Setup.exe from the original Speedlink media " +
                "if you also need the legacy control-panel package.";
            next.Text = "Finish";
            back.Enabled = false;
            cancel.Enabled = false;
        }
    }

    void OnNext()
    {
        if (page == 0)
        {
            page = 1;
            ShowPage();
            return;
        }
        if (page == 1)
        {
            string msi = FindMsi();
            if (msi == null)
            {
                MessageBox.Show(this, "MSI package not found.", Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            next.Enabled = false;
            back.Enabled = false;
            cancel.Enabled = false;
            progress.Visible = true;
            body.Text = "Installing, please wait…";
            Application.DoEvents();

            // Full MSI UI (Welcome/Progress/Finish of the MSI itself is suppressed:
            // we own the outer UX). Use basic+progress UI.
            int code = RunMsi(msi);
            progress.Visible = false;
            if (code != 0 && code != 3010 /* reboot required */)
            {
                MessageBox.Show(this,
                    "Installer returned exit code " + code + ".\r\n\r\n" +
                    ExplainMsiCode(code) + "\r\n\r\n" +
                    "If elevation was denied, re-run Setup.exe as Administrator.\r\n" +
                    "If Defender quarantined the package, restore it and allow the folder.",
                    Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            page = 2;
            ShowPage();
            return;
        }
        Close();
    }

    // Same ProductCode as stock Generic USB Gamepad Vibration Driver.
    const string ProductCode = "{50CD8B4D-CD82-49D1-9E0A-2B7887448068}";

    static bool ProductInstalled()
    {
        try
        {
            Type t = Type.GetTypeFromProgID("WindowsInstaller.Installer");
            if (t == null) return false;
            object installer = Activator.CreateInstance(t);
            // Installer.ProductState(product) -> 5 = installed
            object state = t.InvokeMember(
                "ProductState",
                System.Reflection.BindingFlags.GetProperty,
                null,
                installer,
                new object[] { ProductCode });
            return state != null && Convert.ToInt32(state) == 5;
        }
        catch
        {
            return false;
        }
    }

    static int RunElevatedMsiexec(string arguments)
    {
        var psi = new ProcessStartInfo
        {
            FileName = Path.Combine(Environment.SystemDirectory, "msiexec.exe"),
            Arguments = arguments,
            UseShellExecute = true,
            Verb = "runas",
        };
        try
        {
            using (var p = Process.Start(psi))
            {
                if (p == null) return -1;
                p.WaitForExit();
                return p.ExitCode;
            }
        }
        catch (System.ComponentModel.Win32Exception)
        {
            // User cancelled UAC
            return 1602;
        }
    }

    static int RunMsi(string msiPath)
    {
        string log = Path.Combine(Path.GetTempPath(), "ffb_patched_install.log");
        // Stock product uses the same ProductCode. Windows SecureRepair blocks
        // "repair/reinstall" from a modified MSI (signature/hash no longer match),
        // which surfaces as 1603. Uninstall the stock product first, then install fresh.
        if (ProductInstalled())
        {
            int ux = RunElevatedMsiexec("/x " + ProductCode + " /qb");
            // 0 = removed, 1605 = product not found (race) — both OK to proceed
            if (ux != 0 && ux != 1605 && ux != 1602)
            {
                // Still try install; worst case we report the install code.
            }
            if (ux == 1602)
                return 1602;
        }

        string args = "/i \"" + msiPath + "\" ALLUSERS=1 /qb /L*v \"" + log + "\"";
        return RunElevatedMsiexec(args);
    }

    static string ExplainMsiCode(int code)
    {
        switch (code)
        {
            case 0: return "Success.";
            case 3010: return "Success (reboot required).";
            case 1602: return "Cancelled (UAC or user).";
            case 1603:
                return "Fatal error during install (often a blocked custom action or file in use).\r\n" +
                       "Log: %TEMP%\\ffb_patched_install.log";
            case 1618: return "Another install is already running. Wait and retry.";
            case 1619: return "MSI package could not be opened.";
            case 1625:
                return "Blocked by system policy / Windows Defender (invalid digital signature).\r\n" +
                       "This build should be unsigned — if you still see this, allow the MSI in Defender.";
            case 1638: return "Another version of this product is already installed.";
            case 1718:
                return "Package rejected by digital signature policy (HashMismatch).\r\n" +
                       "Rebuild the patched MSI so signature streams are stripped.";
            default:
                return "See %TEMP%\\ffb_patched_install.log for details.";
        }
    }
}
