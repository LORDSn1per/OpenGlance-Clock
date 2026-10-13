import Foundation

/// A validated OpenGlance Bridge OTA image (the single per-release BIN).
public struct BridgeFirmware: Equatable {
    public static let appPartitionSize = 0x1E0000      // min_spiffs app0/app1
    public static let marker = Array("#OGBRIDGE-FW-ESP32C3:".utf8)

    public enum Problem: Error, Equatable, CustomStringConvertible {
        case empty, notESPImage, wrongChip, fullImage, notOpenGlance, tooLarge
        public var description: String {
            switch self {
            case .empty: return "The file is empty."
            case .notESPImage: return "This is not an ESP32 firmware image."
            case .wrongChip: return "This firmware is not built for the ESP32-C3."
            case .fullImage: return "This is a full USB image. Choose the small “OpenGlance Bridge … OTA.bin” file instead."
            case .notOpenGlance: return "This file is not OpenGlance Bridge firmware."
            case .tooLarge: return "The file is larger than the bridge's app partition."
            }
        }
    }

    public let version: String
    public let size: Int

    public init(data: Data) throws {
        let b = [UInt8](data)
        guard !b.isEmpty else { throw Problem.empty }
        guard b.count >= 36, b[0] == 0xE9 else { throw Problem.notESPImage }
        guard b[12] == 0x05, b[13] == 0x00 else { throw Problem.wrongChip }       // esp_image_header_t.chip_id
        guard b[32] == 0x32, b[33] == 0x54, b[34] == 0xCD, b[35] == 0xAB else { throw Problem.fullImage } // esp_app_desc_t magic
        guard b.count <= Self.appPartitionSize else { throw Problem.tooLarge }
        // The marker text also occurs inside the firmware's own upload checker,
        // so use the occurrence that is followed by "<digits.dots>;".
        var found: String?
        var from = 0
        while found == nil, let start = Self.find(Self.marker, in: b, from: from) {
            var v = ""
            var i = start + Self.marker.count
            while i < b.count, v.count < 8, b[i] == UInt8(ascii: ".") || (b[i] >= 48 && b[i] <= 57) { v.append(Character(UnicodeScalar(b[i]))); i += 1 }
            if !v.isEmpty, i < b.count, b[i] == UInt8(ascii: ";") { found = v }
            from = start + 1
        }
        guard let version = found else { throw Problem.notOpenGlance }
        self.version = version
        self.size = b.count
    }

    static func find(_ needle: [UInt8], in hay: [UInt8], from: Int = 0) -> Int? {
        guard needle.count <= hay.count else { return nil }
        let first = needle[0]
        var i = from
        while i <= hay.count - needle.count {
            if hay[i] == first && Array(hay[i..<(i + needle.count)]) == needle { return i }
            i += 1
        }
        return nil
    }
}

/// Where each piece goes on a bare ESP32-C3 (4 MB, min_spiffs layout).
public struct FlashPlan {
    public struct Segment: Equatable { public let offset: Int; public let path: String; public let size: Int }
    public let segments: [Segment]

    public init(bootloader: (String, Int), partitions: (String, Int), otadata: (String, Int), app: (String, Int)) {
        segments = [
            Segment(offset: 0x0, path: bootloader.0, size: bootloader.1),
            Segment(offset: 0x8000, path: partitions.0, size: partitions.1),
            Segment(offset: 0xE000, path: otadata.0, size: otadata.1),
            Segment(offset: 0x10000, path: app.0, size: app.1),
        ]
    }

    public func esptoolArguments(port: String, eraseAll: Bool, baud: Int = 921600) -> [String] {
        var args = ["--chip", "esp32c3", "--port", port, "--baud", String(baud),
                    "--before", "default-reset", "--after", "hard-reset", "write-flash"]
        if eraseAll { args.append("--erase-all") }
        for s in segments { args += [String(format: "0x%x", s.offset), s.path] }
        return args
    }

    /// Overall 0–1 progress from an esptool "Writing at 0x… NN.N%" update.
    public func overallProgress(address: Int, percent: Double) -> Double {
        let total = Double(segments.reduce(0) { $0 + $1.size })
        guard total > 0 else { return 0 }
        guard let index = segments.lastIndex(where: { address >= $0.offset }) else { return 0 }
        let done = Double(segments[..<index].reduce(0) { $0 + $1.size })
        let current = Double(segments[index].size) * min(max(percent, 0), 100) / 100
        return min(1, (done + current) / total)
    }
}

public enum EsptoolOutput {
    /// Removes ANSI escape sequences (colours, cursor moves) from terminal output.
    public static func stripANSI(_ s: String) -> String {
        s.replacingOccurrences(of: "\u{1B}\\[[0-9;?]*[ -/]*[@-~]", with: "", options: .regularExpression)
    }

    /// Finds "Writing at 0xADDR … NN.N%" in one line of esptool output.
    public static func progress(in line: String) -> (address: Int, percent: Double)? {
        guard let r = line.range(of: "Writing at 0x([0-9a-fA-F]+).*?([0-9]+(?:\\.[0-9]+)?) ?%", options: .regularExpression) else { return nil }
        let text = String(line[r])
        let hex = text.dropFirst("Writing at 0x".count).prefix { $0.isHexDigit }
        guard let address = Int(hex, radix: 16),
              let pctRange = text.range(of: "([0-9]+(?:\\.[0-9]+)?) ?%$", options: .regularExpression),
              let percent = Double(text[pctRange].replacingOccurrences(of: "%", with: "").trimmingCharacters(in: .whitespaces)) else { return nil }
        return (address, percent)
    }

    /// Plain-language explanation for common esptool failures.
    public static func friendlyError(_ log: String) -> String {
        let l = log.lowercased()
        if l.contains("no such file or directory") {
            return "The selected port disappeared. Reconnect the board, press Refresh and choose its port again."
        }
        if l.contains("resource busy") || l.contains("errno 16") || l.contains("could not exclusively lock") {
            return "The USB port is in use by another app (for example an Arduino serial monitor). Close it and try again."
        }
        if l.contains("no serial data received") || l.contains("wrong boot mode") || l.contains("failed to connect") {
            return "Couldn't talk to the ESP32-C3. Hold its BOOT button, tap RESET (or unplug and replug USB while holding BOOT), release BOOT, then try again."
        }
        if (l.contains("this chip is") && !l.contains("esp32-c3")) || l.contains("wrong chip") {
            return "The connected board is not an ESP32-C3."
        }
        return "Flashing failed. Open Details for the full log."
    }
}

public enum SerialPorts {
    /// USB serial ports that an ESP32-C3 can appear as (native USB, CH34x, CP210x, FTDI).
    public static func isCandidate(_ name: String) -> Bool {
        guard name.hasPrefix("cu.") else { return false }
        return ["cu.usbmodem", "cu.usbserial", "cu.wchusbserial", "cu.SLAB_USBtoUART"].contains { name.hasPrefix($0) }
    }
    public static func list(directory: String = "/dev") -> [String] {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: directory)) ?? []
        return names.filter(isCandidate).sorted().map { directory + "/" + $0 }
    }
}

public enum BootBanner {
    /// Version from "OpenGlance Bridge 1.02 started" printed at boot.
    public static func version(in text: String) -> String? {
        guard let r = text.range(of: "OpenGlance Bridge ([0-9]+\\.[0-9]+) started", options: .regularExpression) else { return nil }
        return String(text[r]).components(separatedBy: " ")[2]
    }
}
