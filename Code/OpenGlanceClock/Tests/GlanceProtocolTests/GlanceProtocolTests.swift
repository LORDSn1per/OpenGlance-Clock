import Foundation
import GlanceProtocol

// Standalone checks run with Command Line Tools, which do not include XCTest.
func XCTAssertEqual<T: Equatable>(_ actual: T, _ expected: T, file: StaticString = #filePath, line: UInt = #line) {
        precondition(actual == expected, "Expected \(expected), got \(actual)", file: (file), line: line)
}
func XCTAssertTrue(_ value: Bool, file: StaticString = #filePath, line: UInt = #line) {
    precondition(value, "Check failed", file: (file), line: line)
}
func XCTAssertThrowsError<T>(_ expression: @autoclosure () throws -> T, file: StaticString = #filePath, line: UInt = #line) {
    do { _ = try expression() } catch { return }
    preconditionFailure("Expected rejection", file: (file), line: line)
}
func XCTAssertNil<T>(_ value: T?, file: StaticString = #filePath, line: UInt = #line) {
    precondition(value == nil, "Expected nil", file: (file), line: line)
}
func XCTUnwrap<T>(_ value: T?) throws -> T {
    guard let value else { throw ProtocolError.malformed }; return value
}

final class GlanceProtocolTests {
    func testDiscoveryDoesNotInventClockIdentity() {
        XCTAssertEqual(ClockDiscovery.isGlance(name: nil, advertisedServices: []), false)
        XCTAssertEqual(ClockDiscovery.isGlance(name: "", advertisedServices: ["180F"]), false)
        XCTAssertEqual(ClockDiscovery.isGlance(name: "Headphones", advertisedServices: []), false)
        XCTAssertTrue(ClockDiscovery.isGlance(name: "GlanceClock", advertisedServices: []))
        XCTAssertTrue(ClockDiscovery.isGlance(name: nil, advertisedServices: [GlanceCommands.serviceUUID.uppercased()]))
        for _ in 1...100 { XCTAssertEqual(ClockDiscovery.isGlance(name: nil, advertisedServices: ["180F"]), false) }
    }
    func settings(_ brightness: UInt64 = 128) -> Data {
        WireMessage.integer(2, 0) + WireMessage.integer(4, 1) + WireMessage.integer(10, brightness)
    }
    func testPreservesOpaqueBrightnessBitsAndUnknownFields() throws {
        let unknown = WireMessage.bytes(30, Data([0, 255, 8]))
        let original = try ClockSettings(data: settings(2016768) + unknown)
        XCTAssertEqual(original.brightness, 0)
        let packet = try original.patched([10: 180, 2: 1])
        XCTAssertEqual(Array(packet.prefix(4)), [5, 0, 0, 0])
        let payload = Data(packet.dropFirst(4))
        let message = try WireMessage(payload)
        XCTAssertEqual(message.integer(10), 2016768 | 180)
        XCTAssertTrue(payload.suffix(unknown.count) == unknown)
        XCTAssertTrue(try ClockSettings(data: payload).matches([10: 180, 2: 1]))
    }
    func testSettingsValidation() throws {
        XCTAssertThrowsError(try ClockSettings(data: Data([8, 1])))
        XCTAssertThrowsError(try ClockSettings(data: settings()).patched([3: 1]))
        XCTAssertThrowsError(try ClockSettings(data: settings()).patched([10: 256]))
        XCTAssertEqual(try ClockSettings(data: Data([68,97,116,97,0]) + settings()).brightness, 128)
    }
    func testAutomaticBrightnessRangeFromOriginalApp() throws {
        let range = try AutomaticBrightnessRange(low: 30, high: 220)
        XCTAssertEqual(range.encoded, 2022400)
        let unknown = WireMessage.bytes(30, Data([1, 2, 255]))
        let original = try ClockSettings(data: settings(0xab000000 | 128) + unknown)
        let packet = try original.patched([:], automaticRange: range)
        let updated = try ClockSettings(data: Data(packet.dropFirst(4)))
        XCTAssertEqual(updated.brightness, 0)
        XCTAssertEqual(updated.automaticBrightnessLow, 30)
        XCTAssertEqual(updated.automaticBrightnessHigh, 220)
        XCTAssertEqual(try WireMessage(updated.raw).integer(10), 0xab000000 | 2022400)
        XCTAssertTrue(updated.raw.suffix(unknown.count) == unknown)
        XCTAssertTrue(updated.matches([:], automaticRange: range))
        XCTAssertEqual(updated.matches([:], automaticRange: try AutomaticBrightnessRange(low: 31, high: 220)), false)
        let manual = try ClockSettings(data: Data(updated.patched([10: 200]).dropFirst(4)))
        XCTAssertEqual(manual.brightness, 200)
        XCTAssertEqual(manual.automaticBrightnessLow, 30)
        XCTAssertEqual(manual.automaticBrightnessHigh, 220)
        XCTAssertThrowsError(try AutomaticBrightnessRange(low: -1, high: 220))
        XCTAssertThrowsError(try AutomaticBrightnessRange(low: 30, high: 256))
        XCTAssertThrowsError(try AutomaticBrightnessRange(low: 200, high: 100))
        XCTAssertThrowsError(try original.patched([10: 128], automaticRange: range))
        for value in [0, 255] {
            let fixed = try AutomaticBrightnessRange(low: value, high: value)
            XCTAssertTrue(try ClockSettings(data: Data(original.patched([:], automaticRange: fixed).dropFirst(4))).matches([:], automaticRange: fixed))
        }
    }
    func testNoticeGoldenPacket() throws {
        XCTAssertEqual(try GlanceCommands.notice("Hi"), Data([2,16,0,0,8,1,16,0,24,18,34,6,8,0,18,2,72,105]))
        XCTAssertThrowsError(try GlanceCommands.notice("Hello 👋"))
        XCTAssertThrowsError(try GlanceCommands.notice(""))
        XCTAssertThrowsError(try GlanceCommands.notice(String(repeating: "x", count: 65)))
    }
    func testTimerDurationIsInInterval() throws {
        let packet = try GlanceCommands.timer(seconds: 300, finalText: "Done")
        XCTAssertEqual(Array(packet.prefix(12)), [3,0,0,0,8,0,18,5,16,172,2,24])
        XCTAssertThrowsError(try GlanceCommands.timer(seconds: 59))
        XCTAssertThrowsError(try GlanceCommands.timer(seconds: 43201))
        XCTAssertEqual(GlanceCommands.startCalibration, Data([43]))
        XCTAssertEqual(GlanceCommands.confirmCalibration, Data([44]))
    }
    func testMalformedPackets() {
        for bytes: [UInt8] in [[8,128], [18,5,1], [0], [11], [8,255,255,255,255,255,255,255,255,255,2]] {
            XCTAssertThrowsError(try WireMessage(Data(bytes)))
        }
    }
    func testCurrentTimeAndSydneyDaylightSaving() throws {
        let date = try XCTUnwrap(ISO8601DateFormatter().date(from: "2026-10-11T00:00:00Z"))
        XCTAssertEqual(CurrentTime.value(at: date, timeZone: TimeZone(secondsFromGMT: 0)!), Data([234,7,10,11,0,0,0,7,0,0]))
        XCTAssertEqual(CurrentTime.localInformation(at: date, timeZone: TimeZone(identifier: "Australia/Sydney")!), Data([40,4]))
        XCTAssertEqual(CurrentTime.localInformation(at: date, timeZone: TimeZone(secondsFromGMT: -18000)!), Data([236,0]))
        let winter = try XCTUnwrap(ISO8601DateFormatter().date(from: "2026-07-01T00:00:00Z"))
        XCTAssertEqual(CurrentTime.localInformation(at: winter, timeZone: TimeZone(identifier: "Australia/Sydney")!), Data([40,0]))
        XCTAssertEqual(CurrentTime.value(at: winter, timeZone: TimeZone(secondsFromGMT: 0)!, reason: 1).last, 1)
    }
    func testManualTimeAdvancesWithoutMacWallClockDependency() {
        let manual = Date(timeIntervalSince1970: 1_000_000)
        var source = ClockTimeSource()
        XCTAssertEqual(source.date(now: manual, uptime: 10), manual)
        source.useManualTime(manual, uptime: 20)
        XCTAssertEqual(source.date(now: Date(timeIntervalSince1970: 9_000_000), uptime: 80), manual.addingTimeInterval(60))
        XCTAssertEqual(source.date(now: manual, uptime: 15), manual)
        source.useMacTime()
        XCTAssertEqual(source.date(now: manual.addingTimeInterval(500), uptime: 90), manual.addingTimeInterval(500))
    }
    func testClockHandStateFromOriginalApp() throws {
        let moving = try ClockHandState(data: Data([128]))
        XCTAssertTrue(moving.homing); XCTAssertEqual(moving.awaitingConfirmation, false)
        let waiting = try ClockHandState(data: Data([0, 4]))
        XCTAssertTrue(waiting.awaitingConfirmation); XCTAssertEqual(waiting.homing, false)
        let failed = try ClockHandState(data: Data([64, 8]))
        XCTAssertTrue(failed.homingFailure); XCTAssertTrue(failed.motorFailure)
        XCTAssertThrowsError(try ClockHandState(data: Data()))
        XCTAssertThrowsError(try ClockHandState(data: Data([0, 0, 0])))
    }
    func testAlarmsFromOriginalApp() throws {
        let wake = AlarmEntry(hour: 7, minute: 30, days: 31, sound: 6, text: "Wake")
        XCTAssertEqual(try GlanceCommands.alarms([wake]), Data([4, 47, 0, 0, 0x0A, 0x12, 0x18, 0x1F, 0x22, 0x04, 0x08, 0x07, 0x10, 0x1E, 0x28, 0x06, 0x32, 0x06, 0x12, 0x04, 87, 97, 107, 101]))
        XCTAssertEqual(try GlanceCommands.alarms([]), Data([21, 0, 0, 0]))
        XCTAssertEqual(Array(try GlanceCommands.alarms([AlarmEntry(hour: 7, minute: 30, days: 0, sound: 6, text: "Wake")]))[6], 0x22)
        let big = AlarmEntry(hour: 23, minute: 59, days: 127, sound: 17, text: String(repeating: "x", count: 16))
        XCTAssertTrue(try GlanceCommands.alarms([big, big, big, big]).count <= 155)
        XCTAssertThrowsError(try GlanceCommands.alarms([big, big, big, big, big]))
        XCTAssertThrowsError(try GlanceCommands.alarms([AlarmEntry(hour: 24)]))
        XCTAssertThrowsError(try GlanceCommands.alarms([AlarmEntry(text: String(repeating: "x", count: 17))]))
        XCTAssertEqual(GlanceCommands.alarmStop, Data([20, 0, 0, 0]))
        XCTAssertTrue((try? GlanceCommands.notice("14:00", animation: 0, sound: 17, color: 12)) != nil)
        XCTAssertThrowsError(try GlanceCommands.notice("14:00", animation: 0, sound: 18, color: 12))
    }
    func testWeatherScenesMatchBridge() throws {
        let f = try HourlyForecast(openMeteo: Data(sydneyFixture.utf8))
        XCTAssertEqual(f.hours.count, 24)
        XCTAssertEqual(f.chance[0], 98)
        let now = Date(timeIntervalSince1970: 1791868500)
        let t = try XCTUnwrap(WeatherScenes.temperature(f, now: now, utcOffset: 39600, fahrenheit: false, coldAt: 5, hotAt: 35))
        XCTAssertEqual(t.timestamp, 1791867600 + 39600)
        XCTAssertEqual(t.values[0], 19); XCTAssertEqual(t.values[1], 18); XCTAssertEqual(t.min, 14); XCTAssertEqual(t.max, 19)
        XCTAssertEqual(t.minColor, WeatherScenes.palette(9.0 / 30)); XCTAssertEqual(t.maxColor, WeatherScenes.palette(14.0 / 30))
        XCTAssertEqual(t.template, Data([0xC2, 0x8F, 0x08, 0xC2, 0xB0, 67]))
        let cmd = GlanceCommands.forecast(slot: 1, scene: t)
        XCTAssertEqual(Array(cmd.prefix(4)), [7, 16, 8, 1])
        XCTAssertEqual(GlanceCommands.forecast(slot: 1, scene: t, ringOnly: false)[2], 24)
        let m = try WireMessage(Data(cmd.dropFirst(4)))
        XCTAssertEqual(m.integer(1), UInt64(1791867600 + 39600)); XCTAssertEqual(m.integer(6), 19); XCTAssertEqual(m.integer(7), 14)
        let r = try XCTUnwrap(WeatherScenes.rain(f, now: now, utcOffset: 39600, minMm: 0.3, minChance: 40, lookahead: 6))
        XCTAssertEqual(r.values[0], 0); XCTAssertEqual(r.values[1], 98)
        XCTAssertNil(WeatherScenes.rain(f, now: now, utcOffset: 0, minMm: 50, minChance: 0, lookahead: 12))
        XCTAssertNil(WeatherScenes.rain(f, now: now, utcOffset: 0, minMm: 0.3, minChance: 40, lookahead: 1))
        XCTAssertNil(WeatherScenes.temperature(f, now: now.addingTimeInterval(30 * 3600), utcOffset: 0, fahrenheit: false, coldAt: 5, hotAt: 35))
        var cold = t; cold.values[0] = -5; cold.min = -5
        XCTAssertEqual(Int32(truncatingIfNeeded: Int64(bitPattern: try XCTUnwrap(WireMessage(cold.encoded).integer(7)))), -5)
        XCTAssertEqual(WeatherScenes.palette(-1), 0x2060FF); XCTAssertEqual(WeatherScenes.palette(2), 0xFF3000)
        XCTAssertEqual(GlanceCommands.deleteScene(slot: 2), Data([33, 0, 0, 2]))
    }
    func testClockTimeOffsetFromFirmware167() throws {
        // Field 18 is int32: negatives use the 10-byte two's-complement varint, as nanopb expects.
        let unknown = WireMessage.bytes(30, Data([7, 7]))
        let original = try ClockSettings(data: settings() + unknown)
        XCTAssertEqual(original.timeOffsetMinutes, 0)
        let back = try original.patched(timeOffsetMinutes: -90)
        XCTAssertTrue(back.suffix(12) == Data([0x90, 0x01, 0xA6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01]))
        let stored = try ClockSettings(data: Data(back.dropFirst(4)))
        XCTAssertEqual(stored.timeOffsetMinutes, -90)
        XCTAssertEqual(stored.brightness, 128)
        XCTAssertTrue(stored.raw.prefix(settings().count + unknown.count) == settings() + unknown)
        let forward = try ClockSettings(data: Data(stored.patched(timeOffsetMinutes: 65).dropFirst(4)))
        XCTAssertEqual(forward.timeOffsetMinutes, 65)
        XCTAssertEqual(try WireMessage(forward.raw).fields.filter { $0.number == 18 }.count, 1)
        // Ordinary settings changes must keep field 18, or the firmware would shift the clock back.
        let toggled = try ClockSettings(data: Data(forward.patched([2: 1]).dropFirst(4)))
        XCTAssertEqual(toggled.timeOffsetMinutes, 65)
        XCTAssertEqual(try ClockSettings(data: Data(forward.patched([:], automaticRange: AutomaticBrightnessRange(low: 30, high: 220)).dropFirst(4))).timeOffsetMinutes, 65)
        XCTAssertThrowsError(try original.patched(timeOffsetMinutes: 1_000_001))
        XCTAssertThrowsError(try original.patched([18: 5]))

        XCTAssertEqual(ClockTimeCorrection.supportsOffset(firmware: "1.6.7"), true)
        XCTAssertEqual(ClockTimeCorrection.supportsOffset(firmware: "1.6.8"), true)
        XCTAssertEqual(ClockTimeCorrection.supportsOffset(firmware: "v2.0"), true)
        XCTAssertEqual(ClockTimeCorrection.supportsOffset(firmware: "1.5.10"), false)
        XCTAssertEqual(ClockTimeCorrection.supportsOffset(firmware: "1.6.6"), false)
        XCTAssertNil(ClockTimeCorrection.supportsOffset(firmware: nil))
        XCTAssertNil(ClockTimeCorrection.supportsOffset(firmware: "Glance"))

        var sydney = Calendar(identifier: .gregorian); sydney.timeZone = TimeZone(identifier: "Australia/Sydney")!
        let now = try XCTUnwrap(sydney.date(from: DateComponents(year: 2026, month: 10, day: 14, hour: 0, minute: 2, second: 40)))
        XCTAssertEqual(ClockTimeCorrection.minutes(macNow: now, clockShows: DateComponents(hour: 23, minute: 58), calendar: sydney), 4)
        XCTAssertEqual(ClockTimeCorrection.minutes(macNow: now, clockShows: DateComponents(hour: 1, minute: 2), calendar: sydney), -60)
        XCTAssertEqual(ClockTimeCorrection.minutes(macNow: now, clockShows: DateComponents(hour: 0, minute: 2), calendar: sydney), 0)
        XCTAssertEqual(ClockTimeCorrection.minutes(macNow: now, clockShows: DateComponents(year: 2026, month: 10, day: 12, hour: 0, minute: 0), calendar: sydney), 2 * 1440 + 2)
        // Across the 4 October 2026 Sydney DST start, wall-clock minutes are compared.
        let afterDST = try XCTUnwrap(sydney.date(from: DateComponents(year: 2026, month: 10, day: 4, hour: 3, minute: 10)))
        XCTAssertEqual(ClockTimeCorrection.minutes(macNow: afterDST, clockShows: DateComponents(hour: 2, minute: 10), calendar: sydney), 60)
        XCTAssertNil(ClockTimeCorrection.minutes(macNow: now, clockShows: DateComponents(hour: 24, minute: 0), calendar: sydney))
        // Manual target instead of Mac time.
        XCTAssertEqual(ClockTimeCorrection.minutes(macNow: now, clockShows: DateComponents(hour: 9, minute: 0), target: DateComponents(hour: 9, minute: 30), calendar: sydney), 30)
        XCTAssertEqual(ClockTimeCorrection.minutes(macNow: now, clockShows: DateComponents(hour: 23, minute: 50), target: DateComponents(hour: 0, minute: 10), calendar: sydney), 20)
        XCTAssertEqual(ClockTimeCorrection.minutes(macNow: now, clockShows: DateComponents(hour: 12, minute: 0),
                                                   target: DateComponents(year: 2026, month: 10, day: 15, hour: 12, minute: 0), calendar: sydney), 1440)
        XCTAssertNil(ClockTimeCorrection.minutes(macNow: now, clockShows: DateComponents(hour: 9, minute: 0), target: DateComponents(hour: 9, minute: 60), calendar: sydney))
        for (text, h, m) in [("18:05", 18, 5), ("6:05 pm", 18, 5), ("6.05PM", 18, 5), ("12:00am", 0, 0), ("12pm", 12, 0), ("1805", 18, 5), ("905", 9, 5), ("7", 7, 0), (" 07:30 ", 7, 30)] {
            let c = try XCTUnwrap(ClockTimeCorrection.parseTime(text))
            XCTAssertEqual(c.hour, h); XCTAssertEqual(c.minute, m)
        }
        // Estimate: offset 62 after a bridge sync (baseline 0) → clock is Mac + 62 min.
        XCTAssertEqual(ClockTimeCorrection.estimatedClockTime(macNow: now, offset: 62, baseline: 0), now.addingTimeInterval(62 * 60))
        // Owner typed a reading 3 min slow at offset 62 and set Mac time: offset 62 − 65 = −3, clock − Mac = 0.
        let learned = ClockTimeCorrection.baseline(offset: -3, clockMinusMac: 0)
        XCTAssertEqual(ClockTimeCorrection.estimatedClockTime(macNow: now, offset: -3, baseline: learned), now)
        XCTAssertEqual(ClockTimeCorrection.estimatedClockTime(macNow: now, offset: 7, baseline: learned), now.addingTimeInterval(600))
        for bad in ["", "25:00", "12:60", "13pm", "0am", "7:5", "abc", "12345", "1:2:3"] { XCTAssertNil(ClockTimeCorrection.parseTime(bad)) }
        XCTAssertEqual(ClockTimeCorrection.describe(-61), "back 1 hour 1 minute")
        XCTAssertEqual(ClockTimeCorrection.describe(2882), "forward 2 days 2 minutes")
    }

}

@main enum ProtocolCheckRunner {
    static func main() throws {
        let checks = GlanceProtocolTests()
        checks.testDiscoveryDoesNotInventClockIdentity()
        try checks.testPreservesOpaqueBrightnessBitsAndUnknownFields()
        try checks.testSettingsValidation()
        try checks.testAutomaticBrightnessRangeFromOriginalApp()
        try checks.testNoticeGoldenPacket()
        try checks.testTimerDurationIsInInterval()
        checks.testMalformedPackets()
        try checks.testCurrentTimeAndSydneyDaylightSaving()
        checks.testManualTimeAdvancesWithoutMacWallClockDependency()
        try checks.testClockHandStateFromOriginalApp()
        try checks.testAlarmsFromOriginalApp()
        try checks.testWeatherScenesMatchBridge()
        try checks.testClockTimeOffsetFromFirmware167()
        print("PASS: 13 check groups (discovery filtering, settings preservation, validation, automatic brightness range, notices, timers, malformed packets, time zones/DST, advancing manual time, hand-state decoding, alarms, weather scenes, clock time offset).")
    }
}

let sydneyFixture = #"""
{"latitude":-33.848858,"longitude":151.19551,"generationtime_ms":0.07224082946777344,"utc_offset_seconds":0,"timezone":"GMT","timezone_abbreviation":"GMT","elevation":69.0,"current_units":{"time":"unixtime","interval":"seconds","temperature_2m":"°C"},"current":{"time":1791868500,"interval":900,"temperature_2m":18.6},"hourly_units":{"time":"unixtime","temperature_2m":"°C","precipitation_probability":"%","precipitation":"mm"},"hourly":{"time":[1791867600,1791871200,1791874800,1791878400,1791882000,1791885600,1791889200,1791892800,1791896400,1791900000,1791903600,1791907200,1791910800,1791914400,1791918000,1791921600,1791925200,1791928800,1791932400,1791936000,1791939600,1791943200,1791946800,1791950400],"temperature_2m":[18.8,18.0,18.1,17.5,17.2,16.6,16.9,15.7,15.3,15.2,15.1,14.9,14.7,14.6,14.5,14.4,14.6,14.9,14.7,15.1,15.4,16.0,16.3,16.5],"precipitation_probability":[98,98,99,100,100,100,99,98,98,97,92,80,63,47,30,14,2,0,0,2,2,2,2,1],"precipitation":[0.10,0.60,0.80,0.10,0.10,0.30,0.10,0.80,1.50,0.20,0.20,0.10,0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00]}}
"""#
