// Standalone checks for the Windows Flasher core. Same cases as the Mac FlasherChecks, plus COM ports
// and the Windows wording of esptool/pyserial errors. Optional argument: the Firmware folder.
using System.Text;
using FlasherCore;

int failures = 0;
void Check(bool ok, string what, [System.Runtime.CompilerServices.CallerLineNumber] int line = 0)
{
    if (!ok) { failures++; Console.WriteLine($"FAIL line {line}: {what}"); }
}
byte[] Image(byte chip = 5, bool appMagic = true, string? marker = "#OGBRIDGE-FW-ESP32C3:1.02;", int size = 4096)
{
    var b = new byte[size];
    b[0] = 0xE9; b[12] = chip;
    if (appMagic) { b[32] = 0x32; b[33] = 0x54; b[34] = 0xCD; b[35] = 0xAB; } else b[32] = 0x50;
    if (marker is not null) Encoding.ASCII.GetBytes(marker).CopyTo(b, 1000);
    return b;
}
bool Rejects(byte[] data, BridgeFirmware.Problem problem)
{
    try { _ = new BridgeFirmware(data); return false; } catch (BridgeFirmware.InvalidFirmware e) { return e.Kind == problem; }
}
string? VersionOf(byte[] data) { try { return new BridgeFirmware(data).Version; } catch { return null; } }

// 1. Image validation
Check(VersionOf(Image()) == "1.02", "valid OTA image and version");
var decoy = Image();
Encoding.ASCII.GetBytes("#OGBRIDGE-FW-ESP32C3:\0").CopyTo(decoy, 200);
Check(VersionOf(decoy) == "1.02", "decoy marker before the real version stamp");
Check(Rejects(Array.Empty<byte>(), BridgeFirmware.Problem.Empty), "empty");
Check(Rejects(new byte[] { 0x7F, 0x45, 0x4C, 0x46 }.Concat(new byte[60]).ToArray(), BridgeFirmware.Problem.NotESPImage), "ELF/other file");
Check(Rejects(Image(chip: 0), BridgeFirmware.Problem.WrongChip), "ESP32 (not C3)");
Check(Rejects(Image(appMagic: false), BridgeFirmware.Problem.FullImage), "merged/full image");
Check(Rejects(Image(marker: null), BridgeFirmware.Problem.NotOpenGlance), "unrelated ESP32-C3 app");
Check(Rejects(Image(marker: "#OGBRIDGE-FW-ESP32C3:x;"), BridgeFirmware.Problem.NotOpenGlance), "malformed version");
Check(Rejects(Image(size: BridgeFirmware.AppPartitionSize + 1), BridgeFirmware.Problem.TooLarge), "too large");

// 2. Flash plan and esptool arguments
var plan = new FlashPlan(("C:\\b.bin", 20_000), ("C:\\p.bin", 3_072), ("C:\\o.bin", 8_192), ("C:\\a.bin", 1_000_000));
var esptoolArgs = plan.EsptoolArguments("COM5", eraseAll: false);
Check(esptoolArgs.SequenceEqual(new[] { "--chip", "esp32c3", "--port", "COM5", "--baud", "921600", "--before", "default-reset", "--after", "hard-reset",
    "write-flash", "0x0", "C:\\b.bin", "0x8000", "C:\\p.bin", "0xe000", "C:\\o.bin", "0x10000", "C:\\a.bin" }), "argument order and offsets");
Check(plan.EsptoolArguments("COM5", eraseAll: true).Contains("--erase-all"), "erase option");
Check(!esptoolArgs.Contains("--erase-all"), "no erase by default");
Check(Math.Abs(plan.OverallProgress(0x10000, 0) - 31_264.0 / 1_031_264) < 1e-9, "app start progress");
Check(plan.OverallProgress(0x1F0000, 100) == 1, "complete");
Check(plan.OverallProgress(0x0, 50) > 0, "bootloader progress");

// 3. esptool output parsing
var line = "Writing at 0x0001c000 [=====>                        ]  12.3% 106496/870806 bytes...";
Check(EsptoolOutput.ProgressIn(line)?.Address == 0x1c000, "progress address");
Check(EsptoolOutput.ProgressIn(line)?.Percent == 12.3, "progress percent");
Check(EsptoolOutput.ProgressIn("Writing at 0x00400000 [==============================] 100.0% 890764/890764 bytes...")?.Percent == 100, "final line");
Check(EsptoolOutput.ProgressIn("Hash of data verified.") is null, "non-progress line");
Check(EsptoolOutput.StripAnsi("\u001B[32mWriting\u001B[0m at") == "Writing at", "ANSI stripping");
Check(EsptoolOutput.FriendlyError("A fatal error occurred: Failed to connect to ESP32-C3: No serial data received.").Contains("BOOT"), "boot-mode advice");
Check(EsptoolOutput.FriendlyError("could not open port /dev/cu.x: [Errno 16] Resource busy").Contains("in use"), "busy port advice (macOS)");
Check(EsptoolOutput.FriendlyError("could not open port 'COM5': PermissionError(13, 'Access is denied.', None, 5)").Contains("in use"), "busy port advice (Windows)");
Check(EsptoolOutput.FriendlyError("could not open port 'COM9': FileNotFoundError(2, 'The system cannot find the file specified.', None, 2)").Contains("disappeared"), "missing port advice (Windows)");

// 4. Ports and boot banner
Check(SerialPorts.Order(new[] { "COM10", "COM3", "COM4", "com3", "LPT1" }).SequenceEqual(new[] { "COM3", "COM4", "COM10" }), "COM ports ordered numerically");
Check(SerialPorts.Choose(new[] { "COM1" }, new[] { "COM1", "COM5" }, "COM1") == "COM5", "newly plugged board selected");
Check(SerialPorts.Choose(new[] { "COM1", "COM5" }, new[] { "COM1", "COM5" }, "COM1") == "COM1", "owner's choice kept");
Check(SerialPorts.Choose(new[] { "COM1", "COM5" }, new[] { "COM1" }, "COM5") == "COM1", "unplugged board falls back");
Check(SerialPorts.Choose(Array.Empty<string>(), Array.Empty<string>(), "") == "", "no ports");
Check(BootBanner.Version("noise\n[log] OpenGlance Bridge 1.02 started\n") == "1.02", "boot banner");
Check(BootBanner.Version("OpenGlance Bridge 1.02 (#OGBRIDGE...)") is null, "banner requires 'started'");

// 5. Real release files, when the Firmware folder is passed as the first argument
var dir = args.Length > 0 ? args[0] : "/nonexistent";
var real = Directory.Exists(dir) ? Directory.GetFiles(dir, "* OTA.bin") : Array.Empty<string>();
foreach (var path in real)
{
    var name = Path.GetFileName(path);
    var expected = name.Replace("OpenGlance Bridge ", "").Replace(" OTA.bin", "");
    Check(VersionOf(File.ReadAllBytes(path)) == expected, $"release {name} validates as {expected}");
}

if (failures > 0) { Console.WriteLine($"FAILED: {failures} check(s)"); return 1; }
Console.WriteLine($"PASS: 5 check groups (image validation, flash plan, esptool parsing, COM ports/boot banner, {real.Length} real release file(s)).");
return 0;
