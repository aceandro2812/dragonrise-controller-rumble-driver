// HID rumble packet tester for DragonRise / Speedlink VID_0079&PID_0006
// Builds with .NET Framework csc.exe (no Visual Studio project required).
//
// Usage:
//   HidRumbleTest.exe list
//   HidRumbleTest.exe left  [ms=2000] [intensity=0xFE]   // strong channel only
//   HidRumbleTest.exe right [ms=2000] [intensity=0xFE]   // weak channel only
//   HidRumbleTest.exe both  [ms=1500] [intensity=0xFE]
//   HidRumbleTest.exe proof                               // independence experiment
//   HidRumbleTest.exe stop
//   HidRumbleTest.exe raw <hex bytes...>
//   HidRumbleTest.exe sequence
//
// IMPORTANT: stock GenericFFBDriver re-sends rumble every ~10ms. A single
// SetOutputReport often decays to a weak blip on these controllers. This tool
// continuously refreshes during holds (same pattern as the real driver).
//
// Wire (Linux hid-dr / stock GenericFFB):
//   00 51 00 <weak> 00 <strong> 00 00 + commit 00 FA FE ...
//   Stop: 00 F3 00 00 00 00 00 00

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.Win32.SafeHandles;

class HidRumbleTest
{
    const int DIGCF_PRESENT = 0x00000002;
    const int DIGCF_DEVICEINTERFACE = 0x00000010;
    const uint GENERIC_READ = 0x80000000;
    const uint GENERIC_WRITE = 0x40000000;
    const uint FILE_SHARE_READ = 0x00000001;
    const uint FILE_SHARE_WRITE = 0x00000002;
    const uint OPEN_EXISTING = 3;
    const uint FILE_FLAG_OVERLAPPED = 0x40000000;
    const int INVALID_HANDLE_VALUE = -1;

    static readonly Guid GUID_DEVINTERFACE_HID = new Guid("4D1E55B2-F16F-11CF-88CB-001111000030");

    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVICE_INTERFACE_DATA
    {
        public int cbSize;
        public Guid InterfaceClassGuid;
        public int Flags;
        public IntPtr Reserved;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    struct SP_DEVICE_INTERFACE_DETAIL_DATA
    {
        public int cbSize;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)]
        public string DevicePath;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct HIDD_ATTRIBUTES
    {
        public int Size;
        public ushort VendorID;
        public ushort ProductID;
        public ushort VersionNumber;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid ClassGuid, IntPtr Enumerator, IntPtr hwndParent, int Flags);

    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr DeviceInfoSet, IntPtr DeviceInfoData,
        ref Guid InterfaceClassGuid, int MemberIndex, ref SP_DEVICE_INTERFACE_DATA DeviceInterfaceData);

    [DllImport("setupapi.dll", CharSet = CharSet.Auto, SetLastError = true)]
    static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr DeviceInfoSet,
        ref SP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
        ref SP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData,
        int DeviceInterfaceDetailDataSize, out int RequiredSize, IntPtr DeviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);

    [DllImport("hid.dll", SetLastError = true)]
    static extern bool HidD_GetAttributes(SafeFileHandle HidDeviceObject, ref HIDD_ATTRIBUTES Attributes);

    [DllImport("hid.dll", SetLastError = true)]
    static extern void HidD_GetHidGuid(out Guid HidGuid);

    [DllImport("hid.dll", SetLastError = true)]
    static extern bool HidD_SetOutputReport(SafeFileHandle HidDeviceObject, byte[] ReportBuffer, int ReportBufferLength);

    [DllImport("hid.dll", SetLastError = true)]
    static extern bool HidD_GetPreparsedData(SafeFileHandle HidDeviceObject, out IntPtr PreparsedData);

    [DllImport("hid.dll", SetLastError = true)]
    static extern bool HidD_FreePreparsedData(IntPtr PreparsedData);

    [DllImport("hid.dll", SetLastError = true)]
    static extern int HidP_GetCaps(IntPtr PreparsedData, out HIDP_CAPS Capabilities);

    [StructLayout(LayoutKind.Sequential)]
    struct HIDP_CAPS
    {
        public ushort Usage;
        public ushort UsagePage;
        public ushort InputReportByteLength;
        public ushort OutputReportByteLength;
        public ushort FeatureReportByteLength;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)]
        public ushort[] Reserved;
        public ushort NumberLinkCollectionNodes;
        public ushort NumberInputButtonCaps;
        public ushort NumberInputValueCaps;
        public ushort NumberInputDataIndices;
        public ushort NumberOutputButtonCaps;
        public ushort NumberOutputValueCaps;
        public ushort NumberOutputDataIndices;
        public ushort NumberFeatureButtonCaps;
        public ushort NumberFeatureValueCaps;
        public ushort NumberFeatureDataIndices;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    static extern SafeFileHandle CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    class HidDevice
    {
        public string Path;
        public ushort Vid, Pid, Version;
        public ushort UsagePage, Usage;
        public ushort OutputReportLength;
    }

    static List<HidDevice> EnumerateDragonRise()
    {
        var results = new List<HidDevice>();
        Guid hidGuid;
        HidD_GetHidGuid(out hidGuid);
        IntPtr hDevInfo = SetupDiGetClassDevs(ref hidGuid, IntPtr.Zero, IntPtr.Zero,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (hDevInfo == IntPtr.Zero || hDevInfo.ToInt64() == INVALID_HANDLE_VALUE)
            return results;

        try
        {
            int index = 0;
            while (true)
            {
                var ifData = new SP_DEVICE_INTERFACE_DATA();
                ifData.cbSize = Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA));
                if (!SetupDiEnumDeviceInterfaces(hDevInfo, IntPtr.Zero, ref hidGuid, index, ref ifData))
                    break;
                index++;

                var detail = new SP_DEVICE_INTERFACE_DETAIL_DATA();
                // 32-bit: 4+4? On x64 cbSize is 8, on x86 it is 6 (or 5 on some). Use architecture check.
                detail.cbSize = IntPtr.Size == 8 ? 8 : (IntPtr.Size == 4 ? 6 : 8);
                int required;
                if (!SetupDiGetDeviceInterfaceDetail(hDevInfo, ref ifData, ref detail,
                    Marshal.SizeOf(detail), out required, IntPtr.Zero))
                    continue;

                string path = detail.DevicePath;
                if (string.IsNullOrEmpty(path))
                    continue;
                if (path.IndexOf("vid_0079", StringComparison.OrdinalIgnoreCase) < 0)
                    continue;
                if (path.IndexOf("pid_0006", StringComparison.OrdinalIgnoreCase) < 0)
                    continue;

                using (var handle = OpenHid(path))
                {
                    if (handle.IsInvalid)
                        continue;
                    var attrs = new HIDD_ATTRIBUTES { Size = Marshal.SizeOf(typeof(HIDD_ATTRIBUTES)) };
                    if (!HidD_GetAttributes(handle, ref attrs))
                        continue;

                    var dev = new HidDevice
                    {
                        Path = path,
                        Vid = attrs.VendorID,
                        Pid = attrs.ProductID,
                        Version = attrs.VersionNumber,
                        OutputReportLength = 8
                    };

                    IntPtr preparsed;
                    if (HidD_GetPreparsedData(handle, out preparsed))
                    {
                        try
                        {
                            // HIDP_STATUS_SUCCESS == 0x00110000
                            const int HIDP_STATUS_SUCCESS = 0x00110000;
                            HIDP_CAPS caps;
                            if (HidP_GetCaps(preparsed, out caps) == HIDP_STATUS_SUCCESS)
                            {
                                dev.UsagePage = caps.UsagePage;
                                dev.Usage = caps.Usage;
                                if (caps.OutputReportByteLength > 0)
                                    dev.OutputReportLength = caps.OutputReportByteLength;
                            }
                        }
                        finally
                        {
                            HidD_FreePreparsedData(preparsed);
                        }
                    }
                    results.Add(dev);
                }
            }
        }
        finally
        {
            SetupDiDestroyDeviceInfoList(hDevInfo);
        }
        return results;
    }

    static SafeFileHandle OpenHid(string path)
    {
        // Match GenericFFBDriver: GENERIC_READ|WRITE, share R|W, OPEN_EXISTING, OVERLAPPED
        return CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            IntPtr.Zero, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, IntPtr.Zero);
    }

    // weak = HF/small (byte3), strong = LF/large (byte5) — Linux hid-dr mapping
    static byte[] RumblePacket(byte weakHf, byte strongLf)
    {
        if (weakHf == 0x0A) weakHf = 0x0B; // Linux quirk
        return new byte[] { 0x00, 0x51, 0x00, weakHf, 0x00, strongLf, 0x00, 0x00 };
    }

    static byte[] CommitPacket()
    {
        return new byte[] { 0x00, 0xFA, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00 };
    }

    static byte[] StopPacket()
    {
        return new byte[] { 0x00, 0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    }

    static void PrintHex(string label, byte[] data)
    {
        var sb = new StringBuilder();
        foreach (var b in data)
            sb.AppendFormat("{0:X2} ", b);
        Console.WriteLine("{0}: {1}", label, sb.ToString().Trim());
    }

    static bool SendReport(SafeFileHandle h, byte[] report, string label, bool quiet)
    {
        if (!quiet)
            PrintHex(label, report);
        bool ok = HidD_SetOutputReport(h, report, report.Length);
        if (!ok && !quiet)
            Console.WriteLine("  HidD_SetOutputReport FAILED, Win32={0}", Marshal.GetLastWin32Error());
        else if (!quiet)
            Console.WriteLine("  OK");
        return ok;
    }

    static bool SendReport(SafeFileHandle h, byte[] report, string label)
    {
        return SendReport(h, report, label, false);
    }

    // One rumble+commit (or stop). Matches stock GenericFFBDriver packet pair.
    // strongLf → wire[5], weakHf → wire[3]
    static bool SendRumbleOnce(SafeFileHandle h, byte strongLf, byte weakHf, bool quiet)
    {
        if (strongLf == 0 && weakHf == 0)
            return SendReport(h, StopPacket(), "STOP", quiet);
        bool ok = SendReport(h, RumblePacket(weakHf, strongLf), "RUMBLE", quiet);
        ok = SendReport(h, CommitPacket(), "COMMIT", quiet) && ok;
        return ok;
    }

    static bool SendRumble(SafeFileHandle h, byte strongLf, byte weakHf)
    {
        Console.WriteLine("  channels: strong/LF(wire5)=0x{0:X2}  weak/HF(wire3)=0x{1:X2}",
            strongLf, weakHf);
        return SendRumbleOnce(h, strongLf, weakHf, false);
    }

    // Stock worker loop ~10ms. Keep motors at full power for the whole hold.
    static void HoldRumble(SafeFileHandle h, byte strongLf, byte weakHf, int ms)
    {
        Console.WriteLine("  HOLD {0} ms with ~15ms refresh (stock driver pattern)...", ms);
        var sw = System.Diagnostics.Stopwatch.StartNew();
        int pulses = 0;
        bool first = true;
        while (sw.ElapsedMilliseconds < ms)
        {
            SendRumbleOnce(h, strongLf, weakHf, quiet: !first);
            first = false;
            pulses++;
            Thread.Sleep(15);
        }
        SendRumbleOnce(h, 0, 0, quiet: false);
        Console.WriteLine("  ({0} refresh pulses)", pulses);
    }

    // Alternate wire layout: 00 51 00 <A> 00 <B> treating A=strong (pre-fix mapping)
    static byte[] RumblePacketLegacyAB(byte byte3, byte byte5)
    {
        if (byte3 == 0x0A) byte3 = 0x0B;
        return new byte[] { 0x00, 0x51, 0x00, byte3, 0x00, byte5, 0x00, 0x00 };
    }

    static void HoldRawLayout(SafeFileHandle h, byte b3, byte b5, int ms, string label)
    {
        Console.WriteLine("  {0}: wire 00 51 00 {1:X2} 00 {2:X2} 00 00  for {3} ms", label, b3, b5, ms);
        var sw = System.Diagnostics.Stopwatch.StartNew();
        bool first = true;
        while (sw.ElapsedMilliseconds < ms)
        {
            var pkt = RumblePacketLegacyAB(b3, b5);
            SendReport(h, pkt, "RUMBLE", !first);
            SendReport(h, CommitPacket(), "COMMIT", true);
            first = false;
            Thread.Sleep(15);
        }
        SendReport(h, StopPacket(), "STOP", false);
    }

    static byte ParseIntensity(string[] args, int index, byte defaultValue)
    {
        if (args.Length <= index) return defaultValue;
        string s = args[index];
        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            return Convert.ToByte(s.Substring(2), 16);
        return Convert.ToByte(s);
    }

    static int ParseMs(string[] args, int index, int defaultValue)
    {
        if (args.Length <= index) return defaultValue;
        return int.Parse(args[index]);
    }

    static int Main(string[] args)
    {
        Console.WriteLine("DragonRise HID Rumble Tester (VID_0079&PID_0006)");
        Console.WriteLine();

        if (args.Length == 0)
        {
            Console.WriteLine("Commands: list | left | right | both | proof | stop | raw <hex...> | sequence");
            return 1;
        }

        string cmd = args[0].ToLowerInvariant();
        var devices = EnumerateDragonRise();

        if (cmd == "list")
        {
            if (devices.Count == 0)
            {
                Console.WriteLine("No VID_0079&PID_0006 HID interfaces found. Plug in the controller.");
                return 2;
            }
            for (int i = 0; i < devices.Count; i++)
            {
                var d = devices[i];
                Console.WriteLine("[{0}] VID={1:X4} PID={2:X4} ver={3:X4} usage={4:X4}/{5:X4} outLen={6}",
                    i, d.Vid, d.Pid, d.Version, d.UsagePage, d.Usage, d.OutputReportLength);
                Console.WriteLine("    {0}", d.Path);
            }
            return 0;
        }

        if (devices.Count == 0)
        {
            Console.WriteLine("ERROR: Controller not found (VID_0079&PID_0006).");
            return 2;
        }

        // Prefer the game-controller interface: UsagePage 0x01 Usage 0x04 (Joystick) or 0x05 (Game Pad)
        HidDevice target = devices[0];
        foreach (var d in devices)
        {
            if (d.UsagePage == 0x0001 && (d.Usage == 0x0004 || d.Usage == 0x0005))
            {
                target = d;
                break;
            }
            if (d.OutputReportLength >= 8)
                target = d;
        }

        Console.WriteLine("Using: {0}", target.Path);
        Console.WriteLine("Usage {0:X4}/{1:X4}, OutputReportLength={2}",
            target.UsagePage, target.Usage, target.OutputReportLength);

        using (var h = OpenHid(target.Path))
        {
            if (h.IsInvalid)
            {
                Console.WriteLine("CreateFile failed, Win32={0}", Marshal.GetLastWin32Error());
                return 3;
            }

            switch (cmd)
            {
                case "left":
                case "lf":
                case "strong":
                case "a":
                {
                    byte inten = ParseIntensity(args, 2, 0xFE);
                    int ms = ParseMs(args, 1, 2000);
                    Console.WriteLine("LEFT/STRONG channel only → wire[5]=0x{0:X2} wire[3]=00", inten);
                    HoldRumble(h, inten, 0, ms);
                    break;
                }
                case "right":
                case "hf":
                case "weak":
                case "b":
                {
                    byte inten = ParseIntensity(args, 2, 0xFE);
                    int ms = ParseMs(args, 1, 2000);
                    Console.WriteLine("RIGHT/WEAK channel only → wire[3]=0x{0:X2} wire[5]=00", inten);
                    HoldRumble(h, 0, inten, ms);
                    break;
                }
                case "both":
                {
                    byte inten = ParseIntensity(args, 2, 0xFE);
                    int ms = ParseMs(args, 1, 1500);
                    Console.WriteLine("BOTH channels → wire[3]=wire[5]=0x{0:X2}", inten);
                    HoldRumble(h, inten, inten, ms);
                    break;
                }
                case "proof":
                {
                    // Deterministic independence experiment. User answers by feel.
                    Console.WriteLine("=== MOTOR INDEPENDENCE PROOF ===");
                    Console.WriteLine("Hold pad in both hands, off the desk. Continuous refresh @ max.");
                    Console.WriteLine("Write down 1-5 strength for each step (1=off, 5=violent).\n");

                    Console.WriteLine("[A] STOP baseline (should be silent)");
                    SendRumbleOnce(h, 0, 0, false);
                    Thread.Sleep(800);

                    Console.WriteLine("\n[B] ONLY wire[5]=FE  (Linux STRONG / large)");
                    HoldRumble(h, 0xFE, 0x00, 2500);
                    Thread.Sleep(700);

                    Console.WriteLine("\n[C] ONLY wire[3]=FE  (Linux WEAK / small)");
                    HoldRumble(h, 0x00, 0xFE, 2500);
                    Thread.Sleep(700);

                    Console.WriteLine("\n[D] BOTH wire[3]=FE and wire[5]=FE");
                    HoldRumble(h, 0xFE, 0xFE, 2500);
                    Thread.Sleep(700);

                    Console.WriteLine("\n[E] BOTH at half (0x80) — compare to B/C/D");
                    HoldRumble(h, 0x80, 0x80, 2000);
                    Thread.Sleep(700);

                    Console.WriteLine("\n[F] Legacy byte3-only FE (old 'left' mapping)");
                    HoldRawLayout(h, 0xFE, 0x00, 2000, "legacy A");
                    Thread.Sleep(700);

                    Console.WriteLine("\n[G] Legacy byte5-only FE (old 'right' mapping)");
                    HoldRawLayout(h, 0x00, 0xFE, 2000, "legacy B");
                    Thread.Sleep(500);

                    Console.WriteLine("\n=== HOW TO READ RESULTS ===");
                    Console.WriteLine("If motors are INDEPENDENT:");
                    Console.WriteLine("  - B and C feel DIFFERENT (thump vs buzz), and");
                    Console.WriteLine("  - D feels STRONGER than B or C alone.");
                    Console.WriteLine("If motors are WIRED / FIRMWARE-GANGED together:");
                    Console.WriteLine("  - B, C, F, G feel the SAME character + strength, and");
                    Console.WriteLine("  - D is NOT stronger than B (same single-channel drive).");
                    Console.WriteLine("Shell coupling alone: same sides shake, but B≠C by feel.");
                    break;
                }
                case "stop":
                    SendRumble(h, 0, 0);
                    break;
                case "raw":
                {
                    if (args.Length < 2)
                    {
                        Console.WriteLine("raw requires hex bytes");
                        return 1;
                    }
                    var buf = new List<byte>();
                    for (int i = 1; i < args.Length; i++)
                    {
                        string s = args[i];
                        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                            s = s.Substring(2);
                        buf.Add(Convert.ToByte(s, 16));
                    }
                    SendReport(h, buf.ToArray(), "RAW");
                    break;
                }
                case "sequence":
                {
                    Console.WriteLine("Continuous-refresh sequence at 0xFE. Prefer: proof");
                    Console.WriteLine("--- STRONG only ---");
                    HoldRumble(h, 0xFE, 0, 2000);
                    Thread.Sleep(500);
                    Console.WriteLine("--- WEAK only ---");
                    HoldRumble(h, 0, 0xFE, 2000);
                    Thread.Sleep(500);
                    Console.WriteLine("--- BOTH ---");
                    HoldRumble(h, 0xFE, 0xFE, 2000);
                    break;
                }
                case "nocommit":
                {
                    // Test whether commit packet is required
                    byte inten = ParseIntensity(args, 1, 0xFE);
                    Console.WriteLine("Sending rumble WITHOUT commit packet");
                    SendReport(h, RumblePacket(inten, inten), "RUMBLE-NO-COMMIT");
                    Thread.Sleep(1000);
                    SendReport(h, StopPacket(), "STOP");
                    break;
                }
                default:
                    Console.WriteLine("Unknown command: {0}", cmd);
                    return 1;
            }
        }
        return 0;
    }
}
