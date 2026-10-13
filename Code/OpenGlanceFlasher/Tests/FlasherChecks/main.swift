import Foundation
import FlasherCore

// Standalone checks (XCTest is unavailable with the Command Line Tools).
var failures = 0
func check(_ ok: Bool, _ what: String, line: Int = #line) {
    if !ok { failures += 1; print("FAIL line \(line): \(what)") }
}
func image(chip: UInt8 = 5, appMagic: Bool = true, marker: String? = "#OGBRIDGE-FW-ESP32C3:1.02;", size: Int = 4096) -> Data {
    var b = [UInt8](repeating: 0, count: size)
    b[0] = 0xE9; b[12] = chip
    if appMagic { b[32] = 0x32; b[33] = 0x54; b[34] = 0xCD; b[35] = 0xAB } else { b[32] = 0x50 }
    if let marker { let m = Array(marker.utf8); b.replaceSubrange(1000..<(1000 + m.count), with: m) }
    return Data(b)
}
func rejects(_ data: Data, _ problem: BridgeFirmware.Problem) -> Bool {
    do { _ = try BridgeFirmware(data: data); return false } catch { return (error as? BridgeFirmware.Problem) == problem }
}

// 1. Image validation
check((try? BridgeFirmware(data: image()))?.version == "1.02", "valid OTA image and version")
var decoy = [UInt8](image())
let fake = Array("#OGBRIDGE-FW-ESP32C3:".utf8) + [0]
decoy.replaceSubrange(200..<(200 + fake.count), with: fake)
check((try? BridgeFirmware(data: Data(decoy)))?.version == "1.02", "decoy marker before the real version stamp")
check(rejects(Data(), .empty), "empty")
check(rejects(Data([0x7F, 0x45, 0x4C, 0x46] + [UInt8](repeating: 0, count: 60)), .notESPImage), "ELF/other file")
check(rejects(image(chip: 0), .wrongChip), "ESP32 (not C3)")
check(rejects(image(appMagic: false), .fullImage), "merged/full image starts with the bootloader")
check(rejects(image(marker: nil), .notOpenGlance), "unrelated ESP32-C3 app")
check(rejects(image(marker: "#OGBRIDGE-FW-ESP32C3:x;"), .notOpenGlance), "malformed version")
check(rejects(image(size: BridgeFirmware.appPartitionSize + 1), .tooLarge), "too large")

// 2. Flash plan and esptool arguments
let plan = FlashPlan(bootloader: ("/b.bin", 20_000), partitions: ("/p.bin", 3_072), otadata: ("/o.bin", 8_192), app: ("/a.bin", 1_000_000))
let args = plan.esptoolArguments(port: "/dev/cu.usbmodem1", eraseAll: false)
check(args == ["--chip", "esp32c3", "--port", "/dev/cu.usbmodem1", "--baud", "921600", "--before", "default-reset", "--after", "hard-reset",
               "write-flash", "0x0", "/b.bin", "0x8000", "/p.bin", "0xe000", "/o.bin", "0x10000", "/a.bin"], "argument order and offsets")
check(plan.esptoolArguments(port: "p", eraseAll: true).contains("--erase-all"), "erase option")
check(!args.contains("--erase-all"), "no erase by default")
check(abs(plan.overallProgress(address: 0x10000, percent: 0) - 31_264.0 / 1_031_264) < 1e-9, "app start progress")
check(plan.overallProgress(address: 0x1F0000, percent: 100) == 1, "complete")
check(plan.overallProgress(address: 0x0, percent: 50) > 0, "bootloader progress")

// 3. esptool output parsing
let line = "Writing at 0x0001c000 [=====>                        ]  12.3% 106496/870806 bytes..."
check(EsptoolOutput.progress(in: line)?.address == 0x1c000, "progress address")
check(EsptoolOutput.progress(in: line)?.percent == 12.3, "progress percent")
check(EsptoolOutput.progress(in: "Writing at 0x00400000 [==============================] 100.0% 890764/890764 bytes...")?.percent == 100, "final line")
check(EsptoolOutput.progress(in: "Hash of data verified.") == nil, "non-progress line")
check(EsptoolOutput.stripANSI("\u{1B}[32mWriting\u{1B}[0m at") == "Writing at", "ANSI stripping")
check(EsptoolOutput.friendlyError("A fatal error occurred: Failed to connect to ESP32-C3: No serial data received.").contains("BOOT"), "boot-mode advice")
check(EsptoolOutput.friendlyError("could not open port /dev/cu.x: [Errno 16] Resource busy").contains("in use"), "busy port advice")
check(EsptoolOutput.friendlyError("Could not open /dev/cu.x, the port is busy or doesn't exist. ([Errno 2] could not open port /dev/cu.x: [Errno 2] No such file or directory").contains("disappeared"), "missing port advice (real esptool 5.3.1 text)")

// 4. Ports and boot banner
check(SerialPorts.isCandidate("cu.usbmodem21301") && SerialPorts.isCandidate("cu.wchusbserial110"), "USB ports")
check(!SerialPorts.isCandidate("cu.Bluetooth-Incoming-Port") && !SerialPorts.isCandidate("tty.usbmodem1") && !SerialPorts.isCandidate("cu.debug-console"), "non-USB ports excluded")
check(BootBanner.version(in: "noise\n[log] OpenGlance Bridge 1.02 started\n") == "1.02", "boot banner")
check(BootBanner.version(in: "OpenGlance Bridge 1.02 (#OGBRIDGE...)") == nil, "banner requires 'started'")

// 5. Real release files, when the Firmware folder is passed as the first argument
let firmwareDir = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "/nonexistent")
let real = ((try? FileManager.default.contentsOfDirectory(atPath: firmwareDir.path)) ?? []).filter { $0.hasSuffix(" OTA.bin") }
for name in real {
    let data = (try? Data(contentsOf: firmwareDir.appendingPathComponent(name))) ?? Data()
    let expected = name.replacingOccurrences(of: "OpenGlance Bridge ", with: "").replacingOccurrences(of: " OTA.bin", with: "")
    check((try? BridgeFirmware(data: data))?.version == expected, "release \(name) validates as \(expected)")
}

if failures > 0 { print("FAILED: \(failures) check(s)"); exit(1) }
print("PASS: 5 check groups (image validation, flash plan, esptool parsing, ports/boot banner, \(real.count) real release file(s)).")
