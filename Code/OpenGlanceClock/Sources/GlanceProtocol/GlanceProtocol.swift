import Foundation

public enum ProtocolError: LocalizedError, Equatable {
    case malformed, unsupportedWireType, invalidSettings, invalidText, invalidDuration
    public var errorDescription: String? {
        switch self {
        case .malformed: return "The clock returned an incomplete or malformed message."
        case .unsupportedWireType: return "This clock uses a message format this version cannot safely edit."
        case .invalidSettings: return "The response could not be recognised as clock settings. No settings were changed."
        case .invalidText: return "Use 1–64 characters: English letters, numbers and basic punctuation."
        case .invalidDuration: return "Choose a timer between 1 minute and 12 hours."
        }
    }
}

/// A small wire-format reader that retains every original field, including unknown fields.
/// We intentionally do not reconstruct a clock's settings from guessed defaults.
public struct WireMessage {
    public struct Field {
        public let number: Int
        public let wireType: Int
        public let encoded: Data
        public let integer: UInt64?
    }
    public let fields: [Field]

    public init(_ data: Data) throws {
        let bytes = Array(data)
        guard bytes.count <= 65536 else { throw ProtocolError.malformed }
        var cursor = 0
        var output: [Field] = []
        func varint() throws -> UInt64 {
            var value: UInt64 = 0
            for index in 0..<10 {
                guard cursor < bytes.count else { throw ProtocolError.malformed }
                let byte = bytes[cursor]; cursor += 1
                if index == 9 && byte > 1 { throw ProtocolError.malformed }
                value |= UInt64(byte & 0x7f) << (index * 7)
                if byte & 0x80 == 0 { return value }
            }
            throw ProtocolError.malformed
        }
        func skip(_ count: Int) throws {
            guard count >= 0, count <= bytes.count - cursor else { throw ProtocolError.malformed }
            cursor += count
        }
        while cursor < bytes.count {
            let start = cursor
            let key = try varint()
            let number = Int(key >> 3), type = Int(key & 7)
            guard number > 0, number <= 536870911 else { throw ProtocolError.malformed }
            var integer: UInt64?
            switch type {
            case 0: integer = try varint()
            case 1: try skip(8)
            case 2:
                let length = try varint()
                guard length <= UInt64(bytes.count - cursor) else { throw ProtocolError.malformed }
                try skip(Int(length))
            case 5: try skip(4)
            default: throw ProtocolError.unsupportedWireType
            }
            output.append(Field(number: number, wireType: type, encoded: Data(bytes[start..<cursor]), integer: integer))
        }
        fields = output
    }

    public func integer(_ number: Int) -> UInt64? { fields.last { $0.number == number }?.integer }
    public var encoded: Data { fields.reduce(into: Data()) { $0.append($1.encoded) } }

    public func replacingIntegers(_ changes: [Int: UInt64]) -> Data {
        var data = Data(), written = Set<Int>()
        for field in fields {
            if let value = changes[field.number] {
                if written.insert(field.number).inserted { data.append(Self.integer(field.number, value)) }
            } else { data.append(field.encoded) }
        }
        for number in changes.keys.sorted() where !written.contains(number) {
            data.append(Self.integer(number, changes[number]!))
        }
        return data
    }

    public static func varint(_ value: UInt64) -> Data {
        var value = value, bytes: [UInt8] = []
        repeat {
            var byte = UInt8(value & 0x7f); value >>= 7
            if value > 0 { byte |= 0x80 }
            bytes.append(byte)
        } while value > 0
        return Data(bytes)
    }
    public static func integer(_ field: Int, _ value: UInt64) -> Data {
        varint(UInt64(field << 3)) + varint(value)
    }
    public static func bytes(_ field: Int, _ value: Data) -> Data {
        varint(UInt64(field << 3 | 2)) + varint(UInt64(value.count)) + value
    }
}

public struct AutomaticBrightnessRange: Equatable {
    public let low: Int
    public let high: Int
    public init(low: Int, high: Int) throws {
        guard (0...255).contains(low), (0...255).contains(high), low <= high else { throw ProtocolError.invalidSettings }
        self.low = low; self.high = high
    }
    public var encoded: UInt64 { UInt64(low) << 16 | UInt64(high) << 8 }
}

public struct ClockSettings: Equatable {
    public let raw: Data
    public let brightness: Int
    public let automaticBrightnessLow: Int
    public let automaticBrightnessHigh: Int
    public let nightMode: Bool
    public let digitalTime: Bool
    public let twelveHour: Bool
    public let hourPoints: Bool
    public let muted: Bool
    public let dateFormat: Int
    /// Settings field 18 (int32 minutes, firmware 1.6.7+). See ClockTimeCorrection.
    public let timeOffsetMinutes: Int
    public static let editableFields: Set<Int> = [2, 4, 5, 9, 10, 11, 12]
    public static let timeOffsetField = 18

    public init(data: Data) throws {
        var payload = data
        if payload.starts(with: Data([68, 97, 116, 97, 0])) { payload = Data(payload.dropFirst(5)) }
        else if payload.first == 5 { payload = Data(payload.dropFirst()) }
        let message = try WireMessage(payload)
        let recognised = message.fields.filter { [2, 4, 5, 9, 10, 11, 12, 13].contains($0.number) && $0.wireType == 0 }
        guard recognised.count >= 3 else { throw ProtocolError.invalidSettings }
        for field in message.fields where [2, 3, 4, 9, 11, 12].contains(field.number) {
            guard field.wireType == 0, let value = field.integer, value <= 1 else { throw ProtocolError.invalidSettings }
        }
        guard (message.integer(5) ?? 0) <= 4 else { throw ProtocolError.invalidSettings }
        raw = payload
        brightness = Int((message.integer(10) ?? 0) & 255)
        automaticBrightnessLow = Int(((message.integer(10) ?? 0) >> 16) & 255)
        automaticBrightnessHigh = Int(((message.integer(10) ?? 0) >> 8) & 255)
        nightMode = (message.integer(2) ?? 0) != 0
        digitalTime = (message.integer(11) ?? 0) != 0
        twelveHour = (message.integer(12) ?? 0) != 0
        hourPoints = (message.integer(9) ?? 0) != 0
        muted = (message.integer(4) ?? 0) != 0
        dateFormat = Int(message.integer(5) ?? 0)
        timeOffsetMinutes = message.integer(Self.timeOffsetField).map { Int(Int32(truncatingIfNeeded: $0)) } ?? 0
    }

    /// Replaces only field 18; every other field, known or unknown, is resent unchanged.
    /// Firmware decodes each write from scratch, so dropping field 18 would reset it to 0
    /// and shift the clock back; that is why the full original message is always kept.
    public func patched(timeOffsetMinutes minutes: Int) throws -> Data {
        guard ClockTimeCorrection.offsetRange.contains(minutes) else { throw ProtocolError.invalidSettings }
        let message = try WireMessage(raw)
        return Data([5, 0, 0, 0]) + message.replacingIntegers([Self.timeOffsetField: UInt64(bitPattern: Int64(minutes))])
    }

    public func patched(_ changes: [Int: UInt64], automaticRange: AutomaticBrightnessRange? = nil) throws -> Data {
        guard (!changes.isEmpty || automaticRange != nil), Set(changes.keys).isSubset(of: Self.editableFields), automaticRange == nil || changes[10] == nil else { throw ProtocolError.invalidSettings }
        for (field, value) in changes {
            if field == 10 { guard value <= 255 else { throw ProtocolError.invalidSettings } }
            else if field == 5 { guard value <= 4 else { throw ProtocolError.invalidSettings } }
            else { guard value <= 1 else { throw ProtocolError.invalidSettings } }
        }
        let message = try WireMessage(raw)
        var merged = changes
        if let brightness = changes[10] {
            merged[10] = ((message.integer(10) ?? 0) & ~UInt64(255)) | brightness
        }
        if let automaticRange {
            merged[10] = ((message.integer(10) ?? 0) & ~UInt64(0xffffff)) | automaticRange.encoded
        }
        return Data([5, 0, 0, 0]) + message.replacingIntegers(merged)
    }

    public func matches(_ changes: [Int: UInt64], automaticRange: AutomaticBrightnessRange? = nil) -> Bool {
        guard let message = try? WireMessage(raw) else { return false }
        if let automaticRange, (message.integer(10) ?? 0) & 0xffffff != automaticRange.encoded { return false }
        return changes.allSatisfy {
            let actual = message.integer($0.key) ?? 0
            return ($0.key == 10 ? actual & 255 : actual) == $0.value
        }
    }
}

/// Setting the clock's time without the Current Time Service (which macOS will not let apps publish).
///
/// From static disassembly of Glance firmware 1.6.7 (glance_firmware_full_0601029A0012008c01060007):
/// Settings field 18 is an int32 offset in minutes. The clock's CTS handler sets its time to
/// `CTS time + field 18`, and when a Settings write changes field 18 from `old` to `new`, the
/// firmware moves its running time by `(new - old)` minutes (only once its time has been valid,
/// which it remembers in flash). Firmware 1.5.9/1.5.10 have no field 18 and would ignore it.
/// The clock cannot report its time over Bluetooth, so the correction comes from what the owner
/// sees on the clock. Seconds cannot be changed this way.
public enum ClockTimeCorrection {
    public static let offsetRange = -1_000_000...1_000_000   // about ±694 days
    public static let minimumFirmware = [1, 6, 7]

    /// nil when the version string is missing or unrecognised.
    public static func supportsOffset(firmware: String?) -> Bool? {
        guard let firmware, let match = firmware.firstMatch(of: #/(\d+)\.(\d+)(?:\.(\d+))?/#) else { return nil }
        let version = [Int(match.1) ?? 0, Int(match.2) ?? 0, Int(match.3 ?? "0") ?? 0]
        return version.lexicographicallyPrecedes(minimumFirmware) == false
    }

    /// Minutes to add so the clock shows the Mac's current minute.
    /// `clockShows` needs hour and minute; with year/month/day it also corrects the date,
    /// otherwise the clock is assumed to be within 12 hours of the Mac.
    /// `target` is the time the clock should show; nil means the Mac's current minute. Missing dates
    /// default to the Mac's date, and when neither side gives a date the result is kept within ±12 hours.
    public static func minutes(macNow: Date, clockShows: DateComponents, target: DateComponents? = nil, calendar: Calendar = .current) -> Int? {
        func valid(_ c: DateComponents) -> Bool {
            guard let h = c.hour, let m = c.minute else { return false }
            return (0...23).contains(h) && (0...59).contains(m)
        }
        func hasDate(_ c: DateComponents) -> Bool { c.year != nil && c.month != nil && c.day != nil }
        guard valid(clockShows), target.map(valid) ?? true else { return nil }
        let today = calendar.dateComponents([.year, .month, .day, .hour, .minute], from: macNow)
        func resolve(_ c: DateComponents) -> DateComponents {
            var r = today
            r.hour = c.hour; r.minute = c.minute
            if hasDate(c) { r.year = c.year; r.month = c.month; r.day = c.day }
            return r
        }
        var mac = target.map(resolve) ?? today
        var shown = resolve(clockShows)
        let withDate = hasDate(clockShows) || (target.map(hasDate) ?? false)
        mac.second = 0; shown.second = 0
        // The clock keeps naive wall-clock time, so compare wall-clock minutes without a time zone
        // (a local calendar would move times that fall in a daylight-saving gap).
        var wall = Calendar(identifier: .gregorian); wall.timeZone = TimeZone(secondsFromGMT: 0)!
        mac.timeZone = nil; shown.timeZone = nil; mac.calendar = nil; shown.calendar = nil
        guard let macMinute = wall.date(from: mac), let shownMinute = wall.date(from: shown) else { return nil }
        var delta = Int((macMinute.timeIntervalSince(shownMinute) / 60).rounded())
        if !withDate {
            delta = ((delta % 1440) + 1440) % 1440
            if delta > 720 { delta -= 1440 }
        }
        return delta
    }

    /// Estimated clock reading. CTS (bridge) sets the clock to true time + offset, and every Mac change
    /// moves both the clock and the offset by the same amount, so clock − Mac = offset − baseline, where
    /// baseline is 0 after a bridge sync and is re-learned whenever the owner types what the clock shows.
    public static func estimatedClockTime(macNow: Date, offset: Int, baseline: Int) -> Date {
        macNow.addingTimeInterval(Double(offset - baseline) * 60)
    }
    /// The baseline to store after a set whose result is known: clock − Mac = `clockMinusMac` minutes.
    public static func baseline(offset: Int, clockMinusMac: Int) -> Int { offset - clockMinusMac }

    /// Parses a typed time such as "18:05", "6:05 pm", "6.05pm", "1805" or "7" into hour and minute.
    public static func parseTime(_ text: String) -> DateComponents? {
        var s = text.lowercased().replacingOccurrences(of: " ", with: "")
        var meridiem: String?
        for suffix in ["am", "pm", "a", "p"] where s.hasSuffix(suffix) {
            meridiem = String(suffix.prefix(1)); s.removeLast(suffix.count); break
        }
        var hour: Int?, minute: Int?
        let parts = s.split(omittingEmptySubsequences: false, whereSeparator: { $0 == ":" || $0 == "." })
        if parts.count == 2, (1...2).contains(parts[0].count), parts[1].count == 2 {
            hour = Int(parts[0]); minute = Int(parts[1])
        } else if parts.count == 1, s.allSatisfy(\.isNumber) {
            switch s.count {
            case 1, 2: hour = Int(s); minute = 0
            case 3, 4: hour = Int(s.dropLast(2)); minute = Int(s.suffix(2))
            default: return nil
            }
        }
        guard var h = hour, let m = minute, (0...59).contains(m) else { return nil }
        if let meridiem {
            guard (1...12).contains(h) else { return nil }
            h = h % 12 + (meridiem == "p" ? 12 : 0)
        }
        guard (0...23).contains(h) else { return nil }
        return DateComponents(hour: h, minute: m)
    }

    public static func describe(_ minutes: Int) -> String {
        if minutes == 0 { return "no change" }
        let size = abs(minutes), days = size / 1440, hours = (size % 1440) / 60, mins = size % 60
        var parts: [String] = []
        if days > 0 { parts.append("\(days) day" + (days == 1 ? "" : "s")) }
        if hours > 0 { parts.append("\(hours) hour" + (hours == 1 ? "" : "s")) }
        if mins > 0 { parts.append("\(mins) minute" + (mins == 1 ? "" : "s")) }
        return (minutes > 0 ? "forward " : "back ") + parts.joined(separator: " ")
    }
}

public enum ClockDiscovery {
    public static func isGlance(name: String?, advertisedServices: [String]) -> Bool {
        let namedClock = name?.localizedCaseInsensitiveContains("glance") ?? false
        let glanceService = advertisedServices.contains { $0.caseInsensitiveCompare(GlanceCommands.serviceUUID) == .orderedSame }
        return namedClock || glanceService
    }
}

public enum GlanceCommands {
    public static let serviceUUID = "5075f606-1e0e-11e7-93ae-92361f002671"
    public static let controlUUID = "5075fb2e-1e0e-11e7-93ae-92361f002671"

    public static func textData(_ text: String) throws -> Data {
        let bytes = Array(text.utf8)
        guard !bytes.isEmpty, bytes.count <= 64, bytes.allSatisfy({ (32...126).contains($0) }) else { throw ProtocolError.invalidText }
        return WireMessage.integer(1, 0) + WireMessage.bytes(2, Data(bytes))
    }
    public static func notice(_ text: String, animation: Int = 1, sound: Int = 0, color: Int = 18) throws -> Data {
        guard [0, 1, 2, 10, 11, 12, 15, 17].contains(animation), (0...17).contains(sound), [5, 12, 15, 18, 23].contains(color) else { throw ProtocolError.malformed }
        return Data([2, 16, 0, 0]) + WireMessage.integer(1, UInt64(animation))
            + WireMessage.integer(2, UInt64(sound)) + WireMessage.integer(3, UInt64(color))
            + WireMessage.bytes(4, try textData(text))
    }
    public static func timer(seconds: Int, finalText: String = "Time's up!") throws -> Data {
        guard (60...43200).contains(seconds) else { throw ProtocolError.invalidDuration }
        // countdown (field 1) is the pre-timer count, NOT the timer duration.
        let interval = WireMessage.integer(2, UInt64(seconds)) + WireMessage.integer(3, 0)
        return Data([3, 0, 0, 0]) + WireMessage.integer(1, 0)
            + WireMessage.bytes(2, interval) + WireMessage.bytes(3, try textData(finalText))
    }
    public static let stopTimer = Data([10])
    public static let alarmStop = Data([20, 0, 0, 0])
    public static let refreshDisplay = Data([35])
    /// Whole alarm list as the original app sends it (APK 1.5.1 modules 819/1991):
    /// 4,47,0,0 + Alarms{alarm=1: AlarmData{days=3 (omitted for once), time=4{hours=1,minutes=2},
    /// sound=5, text=6{text=2}}}; an empty list is AlarmsClear (21,0,0,0). At most 4.
    public static func alarms(_ list: [AlarmEntry]) throws -> Data {
        guard list.count <= 4 else { throw ProtocolError.invalidSettings }
        if list.isEmpty { return Data([21, 0, 0, 0]) }
        var body = Data()
        for a in list {
            guard a.isValid else { throw ProtocolError.invalidSettings }
            var item = Data()
            if a.days != 0 { item += WireMessage.integer(3, UInt64(a.days)) }
            item += WireMessage.bytes(4, WireMessage.integer(1, UInt64(a.hour)) + WireMessage.integer(2, UInt64(a.minute)))
            item += WireMessage.integer(5, UInt64(a.sound))
            item += WireMessage.bytes(6, WireMessage.bytes(2, Data(a.text.utf8)))
            body += WireMessage.bytes(1, item)
        }
        return Data([4, 47, 0, 0]) + body
    }
    /// SaveForecastScene 7, priority 16, display (8 ring only keeps time/date; 24 ring + text), slot.
    public static func forecast(slot: UInt8, scene: ForecastScene, ringOnly: Bool = true) -> Data {
        Data([7, 16, ringOnly ? 8 : 24, slot]) + scene.encoded
    }
    public static func deleteScene(slot: UInt8) -> Data { Data([33, 0, 0, slot]) }
    public static let startCalibration = Data([43])
    public static let confirmCalibration = Data([44])
}

public struct ClockHandState {
    public let homing: Bool
    public let awaitingConfirmation: Bool
    public let homingFailure: Bool
    public let motorFailure: Bool
    public init(data: Data) throws {
        guard data.count == 1 || data.count == 2 else { throw ProtocolError.malformed }
        let bytes = Array(data)
        let bits = UInt16(bytes[0]) | (bytes.count == 2 ? UInt16(bytes[1]) << 8 : 0)
        homing = bits & 128 != 0; awaitingConfirmation = bits & 1024 != 0
        homingFailure = bits & 64 != 0; motorFailure = bits & 2048 != 0
    }
}

public struct ClockTimeSource {
    public private(set) var manualDate: Date?
    private var anchorUptime: TimeInterval = 0
    public init() {}
    public mutating func useMacTime() { manualDate = nil }
    public mutating func useManualTime(_ date: Date, uptime: TimeInterval = ProcessInfo.processInfo.systemUptime) {
        manualDate = date; anchorUptime = uptime
    }
    public func date(now: Date = Date(), uptime: TimeInterval = ProcessInfo.processInfo.systemUptime) -> Date {
        guard let manualDate else { return now }
        return manualDate.addingTimeInterval(max(0, uptime - anchorUptime))
    }
}

public enum CurrentTime {
    public static func value(at date: Date, timeZone: TimeZone = .current, reason: UInt8 = 0) -> Data {
        var calendar = Calendar(identifier: .gregorian); calendar.timeZone = timeZone
        let c = calendar.dateComponents([.year, .month, .day, .hour, .minute, .second, .weekday, .nanosecond], from: date)
        let year = c.year ?? 2000
        let fraction = min(255, max(0, Int(Double(c.nanosecond ?? 0) / 1_000_000_000 * 256)))
        let day = ((c.weekday ?? 1) + 5) % 7 + 1 // Monday=1, Sunday=7.
        return Data([UInt8(year & 255), UInt8((year >> 8) & 255), UInt8(c.month ?? 1), UInt8(c.day ?? 1), UInt8(c.hour ?? 0), UInt8(c.minute ?? 0), UInt8(c.second ?? 0), UInt8(day), UInt8(fraction), reason])
    }
    public static func localInformation(at date: Date, timeZone: TimeZone = .current) -> Data {
        let daylight = Int(timeZone.daylightSavingTimeOffset(for: date))
        let standard = timeZone.secondsFromGMT(for: date) - daylight
        let quarters = max(-48, min(56, standard / 900))
        let dst: UInt8 = daylight == 0 ? 0 : daylight == 1800 ? 2 : daylight == 3600 ? 4 : daylight == 7200 ? 8 : 255
        return Data([UInt8(bitPattern: Int8(quarters)), dst])
    }
}


public struct AlarmEntry: Codable, Equatable, Identifiable {
    public var id = UUID()
    public var hour: Int
    public var minute: Int
    public var days: Int          // Mon=1 … Sun=64; 0 = once
    public var sound: Int
    public var text: String
    public var enabled: Bool
    public init(hour: Int = 7, minute: Int = 0, days: Int = 31, sound: Int = 6, text: String = "", enabled: Bool = true) {
        self.hour = hour; self.minute = minute; self.days = days; self.sound = sound; self.text = text; self.enabled = enabled
    }
    public var isValid: Bool {
        (0...23).contains(hour) && (0...59).contains(minute) && (0...127).contains(days) && (1...17).contains(sound)
            && text.utf8.count <= 16 && text.utf8.allSatisfy { (32...126).contains($0) }
    }
}

// MARK: Weather (same rules as the ESP32 bridge's weather.h)

public struct ForecastScene: Equatable {
    public var timestamp: Int64 = 0          // local time expressed as a Unix timestamp
    public var maxColor: UInt32 = 0, minColor: UInt32 = 0
    public var max: Int32 = 0, min: Int32 = 0
    public var values: [Int16] = Array(repeating: 0, count: 24)
    public var template = Data()
    public var encoded: Data {
        var values = Data()
        for v in self.values { values.append(UInt8(truncatingIfNeeded: UInt16(bitPattern: v))); values.append(UInt8(truncatingIfNeeded: UInt16(bitPattern: v) >> 8)) }
        return WireMessage.integer(1, UInt64(bitPattern: timestamp)) + WireMessage.integer(4, UInt64(maxColor)) + WireMessage.integer(5, UInt64(minColor))
            + WireMessage.integer(6, UInt64(bitPattern: Int64(max))) + WireMessage.integer(7, UInt64(bitPattern: Int64(min)))
            + WireMessage.bytes(8, values) + WireMessage.bytes(9, template)
    }
}

public struct HourlyForecast: Equatable {
    public var hours: [Date] = []
    public var temperature: [Double] = []
    public var chance: [Int] = []
    public var rain: [Double] = []
    public var current: Double?
    public init() {}

    /// Parses an Open-Meteo response requested with timeformat=unixtime.
    public init(openMeteo data: Data) throws {
        struct Response: Decodable {
            struct Hourly: Decodable {
                let time: [Double]; let temperature_2m: [Double?]; let precipitation_probability: [Double?]; let precipitation: [Double?]
            }
            struct Current: Decodable { let temperature_2m: Double? }
            let hourly: Hourly; let current: Current?
        }
        let r = try JSONDecoder().decode(Response.self, from: data)
        let n = r.hourly.time.count
        guard n > 0, r.hourly.temperature_2m.count == n, r.hourly.precipitation_probability.count == n, r.hourly.precipitation.count == n else { throw ProtocolError.malformed }
        hours = r.hourly.time.map { Date(timeIntervalSince1970: $0) }
        var last = 0.0
        temperature = r.hourly.temperature_2m.map { v in if let v { last = v }; return last }
        chance = r.hourly.precipitation_probability.map { Int(($0 ?? 0).rounded()) }
        rain = r.hourly.precipitation.map { $0 ?? 0 }
        current = r.current?.temperature_2m
    }

    public func startIndex(at now: Date) -> Int? {
        hours.firstIndex { $0 <= now && now < $0.addingTimeInterval(3600) }
    }
}

public enum WeatherScenes {
    public static func mix(_ a: UInt32, _ b: UInt32, _ t: Double) -> UInt32 {
        let t = Swift.min(1, Swift.max(0, t))
        func ch(_ s: UInt32) -> UInt32 {
            let x = Double((a >> s) & 255), y = Double((b >> s) & 255)
            return UInt32((x + (y - x) * t).rounded()) << s
        }
        return ch(16) | ch(8) | ch(0)
    }
    /// Blue → green → amber → red across coldAt…hotAt.
    public static func palette(_ t: Double) -> UInt32 {
        let stops: [UInt32] = [0x2060FF, 0x00C070, 0xFFC000, 0xFF3000]
        if t <= 0 { return stops[0] }
        if t >= 1 { return stops[3] }
        let x = t * 3, i = Int(x)
        return mix(stops[i], stops[i + 1], x - Double(i))
    }

    public static func temperature(_ f: HourlyForecast, now: Date, utcOffset: Int, fahrenheit: Bool, coldAt: Int, hotAt: Int) -> ForecastScene? {
        guard let start = f.startIndex(at: now) else { return nil }
        var s = ForecastScene()
        s.timestamp = Int64(f.hours[start].timeIntervalSince1970) + Int64(utcOffset)
        var lo = 0, hi = 0
        for i in 0..<24 {
            let idx = Swift.min(start + i, f.hours.count - 1)
            let v = Int(((i == 0 ? f.current : nil) ?? f.temperature[idx]).rounded())
            s.values[i] = Int16(clamping: v)
            if i == 0 || v < lo { lo = v }
            if i == 0 || v > hi { hi = v }
        }
        if hi == lo { hi = lo + 1 }
        s.min = Int32(lo); s.max = Int32(hi)
        let span = hotAt > coldAt ? Double(hotAt - coldAt) : 1
        s.minColor = palette(Double(lo - coldAt) / span)
        s.maxColor = palette(Double(hi - coldAt) / span)
        s.template = Data([0xC2, 0x8F, 0x08, 0xC2, 0xB0, fahrenheit ? UInt8(ascii: "F") : UInt8(ascii: "C")])
        return s
    }

    public static func rainValue(rain: Double, chance: Int, minMm: Double, minChance: Int) -> Int {
        guard rain >= minMm - 1e-6, chance >= minChance else { return 0 }
        return Swift.min(100, Swift.max(10, chance))
    }

    /// Rain ring, or nil when no hour in the look-ahead qualifies (amount AND chance).
    public static func rain(_ f: HourlyForecast, now: Date, utcOffset: Int, minMm: Double, minChance: Int, lookahead: Int) -> ForecastScene? {
        guard let start = f.startIndex(at: now) else { return nil }
        var s = ForecastScene()
        s.timestamp = Int64(f.hours[start].timeIntervalSince1970) + Int64(utcOffset)
        var any = false
        for i in 0..<24 {
            let idx = start + i
            let v = idx < f.hours.count ? rainValue(rain: f.rain[idx], chance: f.chance[idx], minMm: minMm, minChance: minChance) : 0
            s.values[i] = Int16(v)
            if v > 0 && i < lookahead { any = true }
        }
        s.min = 0; s.max = 100
        s.minColor = 0x7FD4FF; s.maxColor = 0x0030FF
        s.template = Data([0xC2, 0x95, 0x08, UInt8(ascii: "%")])
        return any ? s : nil
    }
}
