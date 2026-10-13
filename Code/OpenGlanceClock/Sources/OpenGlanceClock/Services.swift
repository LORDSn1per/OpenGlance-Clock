import Foundation
import Combine
import GlanceProtocol

/// Names and choices shared by the views (same lists as the bridge web page).
enum Glance {
    static let sounds = ["None", "Waves", "Rise", "Charging", "Steps", "Radar", "Bells", "Bye", "Hello", "Flowers",
                         "Circles", "Complete", "Popcorn", "Break", "Opening", "High", "Shine", "Extension"]
    static let animations: [(Int, String)] = [(1, "Pulse"), (2, "Wave"), (10, "Fire"), (11, "Wheel"), (12, "Flower"), (15, "Sun"), (17, "Cloud"), (0, "None")]
    static let colours: [(Int, String)] = [(18, "Turquoise"), (23, "Lime"), (15, "Blue"), (5, "Red"), (12, "White")]
    static let dateFormats: [(Int, String)] = [(0, "Off"), (1, "24 Jan"), (2, "24 Tue"), (3, "Jan 24"), (4, "Tue 24")]
    static let days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    static func daysText(_ d: Int) -> String {
        switch d {
        case 0: return "Once"
        case 127: return "Every day"
        case 31: return "Weekdays"
        case 96: return "Weekend"
        default: return days.enumerated().filter { d & (1 << $0.offset) != 0 }.map(\.element).joined(separator: " ")
        }
    }
}

private func loadJSON<T: Decodable>(_ key: String, _ fallback: T) -> T {
    guard let data = UserDefaults.standard.data(forKey: key), let value = try? JSONDecoder().decode(T.self, from: data) else { return fallback }
    return value
}
private func saveJSON<T: Encodable>(_ key: String, _ value: T) {
    if let data = try? JSONEncoder().encode(value) { UserDefaults.standard.set(data, forKey: key) }
}

// MARK: Alarms

/// The clock cannot report its alarms, so the list lives on this Mac. Every
/// change replaces the whole list on the clock (as the original app did).
@MainActor
final class AlarmStore: ObservableObject {
    @Published private(set) var alarms: [AlarmEntry] = loadJSON("alarms", [])
    @Published private(set) var pending = UserDefaults.standard.bool(forKey: "alarmsPending")
    private let clock: BluetoothController
    private var retry: Timer?

    init(clock: BluetoothController) {
        self.clock = clock
        clock.onClockReady.append { [weak self] in self?.syncIfPending() }
        // Bluetooth sends are one at a time; retry if another send was in progress.
        retry = Timer.scheduledTimer(withTimeInterval: 10, repeats: true) { [weak self] _ in
            Task { @MainActor in if let self, self.pending, self.clock.isConnected, !self.clock.isBusy { self.syncIfPending() } }
        }
    }
    func save(_ alarm: AlarmEntry) {
        if let i = alarms.firstIndex(where: { $0.id == alarm.id }) { alarms[i] = alarm }
        else if alarms.count < 4 { alarms.append(alarm) }
        changed()
    }
    func setEnabled(_ alarm: AlarmEntry, _ enabled: Bool) {
        guard let i = alarms.firstIndex(where: { $0.id == alarm.id }) else { return }
        alarms[i].enabled = enabled; changed()
    }
    func delete(_ alarm: AlarmEntry) { alarms.removeAll { $0.id == alarm.id }; changed() }
    func deleteAll() { alarms = []; changed() }
    private func changed() {
        saveJSON("alarms", alarms)
        pending = true; UserDefaults.standard.set(true, forKey: "alarmsPending")
        syncIfPending()
    }
    func syncIfPending() {
        guard pending else { return }
        guard let data = try? GlanceCommands.alarms(alarms.filter(\.enabled)) else { clock.log("Alarm list is not valid; nothing was sent", error: true); return }
        let active = alarms.filter(\.enabled).count
        clock.sendQuietly(data, title: active == 0 ? "All alarms on the clock cleared" : "Alarms on the clock replaced: \(active) active") { [weak self] ok in
            guard ok, let self else { return }
            self.pending = false; UserDefaults.standard.set(false, forKey: "alarmsPending")
        }
    }
}

// MARK: Hourly chime

@MainActor
final class ChimeService: ObservableObject {
    @Published var on = UserDefaults.standard.bool(forKey: "chimeOn") { didSet { UserDefaults.standard.set(on, forKey: "chimeOn") } }
    @Published var sound = UserDefaults.standard.object(forKey: "chimeSound") as? Int ?? 6 { didSet { UserDefaults.standard.set(sound, forKey: "chimeSound") } }
    @Published var quietFrom = UserDefaults.standard.object(forKey: "quietFrom") as? Int ?? 22 { didSet { UserDefaults.standard.set(quietFrom, forKey: "quietFrom") } }
    @Published var quietTo = UserDefaults.standard.object(forKey: "quietTo") as? Int ?? 7 { didSet { UserDefaults.standard.set(quietTo, forKey: "quietTo") } }
    private let clock: BluetoothController
    private var timer: Timer?
    private var lastHour = Calendar.current.component(.hour, from: Date())   // never chime for the hour already under way

    init(clock: BluetoothController) {
        self.clock = clock
        timer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in Task { @MainActor in self?.tick() } }
    }
    func isQuiet(_ hour: Int) -> Bool {
        if quietFrom == quietTo { return false }
        return quietFrom < quietTo ? (hour >= quietFrom && hour < quietTo) : (hour >= quietFrom || hour < quietTo)
    }
    private func tick() {
        let now = Date(), c = Calendar.current.dateComponents([.hour, .minute], from: now)
        guard let hour = c.hour, c.minute == 0, hour != lastHour else { return }
        lastHour = hour
        let previousHour = (hour + 23) % 24
        guard on, !isQuiet(hour), !clock.calibrationBusy, !(clock.timerEnd.map { $0 > now } ?? false) else { return }
        let text = clock.settings?.twelveHour == true ? "\(hour % 12 == 0 ? 12 : hour % 12)\(hour < 12 ? "AM" : "PM")" : String(format: "%02d:00", hour)
        if let data = try? GlanceCommands.notice(text, animation: 0, sound: sound, color: 12) {
            clock.sendQuietly(data, title: "Hourly chime \(text)") { [weak self] ok in
                if !ok, let self, self.clock.isConnected { self.lastHour = previousHour }   // try again a few seconds later this minute
            }
        }
    }
}

// MARK: Weather

/// Fetches Open-Meteo every 30 minutes while the app is open and keeps one
/// weather ring (slot 1) on the clock, exactly like the ESP32 bridge.
@MainActor
final class WeatherService: ObservableObject {
    struct Place: Codable, Equatable { var name: String; var latitude: Double; var longitude: Double }
    struct Options: Codable, Equatable {
        var on = false, temperature = true, text = false, fahrenheit = false
        var rainMode = 1          // 0 off, 1 alternate, 2 instead of temperature
        var cycle = 20, rainHours = 6, rainChance = 40, coldAt = 5, hotAt = 35
        var rainMm = 0.3
    }
    @Published var options: Options = loadJSON("weatherOptions", Options()) { didSet { saveJSON("weatherOptions", options); if options != oldValue { applyChange(refetch: options.fahrenheit != oldValue.fahrenheit || (options.on && !oldValue.on)) } } }
    @Published var place: Place? = loadJSON("weatherPlace", Optional<Place>.none) { didSet { saveJSON("weatherPlace", place); applyChange(refetch: true) } }
    @Published private(set) var forecast: HourlyForecast?
    @Published private(set) var fetched: Date?
    @Published private(set) var sent: Date?
    @Published private(set) var error: String?
    @Published private(set) var showing = 0       // 0 nothing, 1 temperature, 2 rain
    @Published private(set) var rainDue = false
    private let clock: BluetoothController
    private var fetchTimer: Timer?, tickTimer: Timer?
    private var phaseRain = false, nextSwap = Date.distantPast, lastHour = -1
    private var lastCommand: Data?
    private var oldSlotCleanup = true
    private var retryPending = false

    init(clock: BluetoothController) {
        self.clock = clock
        clock.onClockReady.append { [weak self] in self?.lastCommand = nil; self?.send(user: false) }
        fetchTimer = Timer.scheduledTimer(withTimeInterval: 30 * 60, repeats: true) { [weak self] _ in Task { @MainActor in await self?.fetch() } }
        tickTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in Task { @MainActor in self?.tick() } }
        Task { await fetch() }
    }

    private func applyChange(refetch: Bool) {
        if refetch { Task { await fetch(user: true) } } else { send(user: true) }
    }

    func fetch(user: Bool = false) async {
        guard options.on, let place else { return }
        var c = URLComponents(string: "https://api.open-meteo.com/v1/forecast")!
        c.queryItems = [.init(name: "latitude", value: String(format: "%.4f", place.latitude)), .init(name: "longitude", value: String(format: "%.4f", place.longitude)),
                        .init(name: "hourly", value: "temperature_2m,precipitation_probability,precipitation"), .init(name: "current", value: "temperature_2m"),
                        .init(name: "forecast_hours", value: "36"), .init(name: "timeformat", value: "unixtime")]
        if options.fahrenheit { c.queryItems?.append(.init(name: "temperature_unit", value: "fahrenheit")) }
        do {
            let (data, response) = try await URLSession.shared.data(from: c.url!)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { throw URLError(.badServerResponse) }
            forecast = try HourlyForecast(openMeteo: data)
            fetched = Date(); error = nil
            send(user: user)
        } catch {
            self.error = "Weather update failed: \(error.localizedDescription)"
            clock.log("Weather update failed", error.localizedDescription, error: true)
        }
    }

    static func search(_ query: String) async throws -> [Place] {
        struct R: Decodable { struct P: Decodable { let name: String; let latitude: Double; let longitude: Double; let admin1: String?; let country: String? }; let results: [P]? }
        var c = URLComponents(string: "https://geocoding-api.open-meteo.com/v1/search")!
        c.queryItems = [.init(name: "name", value: query), .init(name: "count", value: "8"), .init(name: "language", value: "en")]
        let (data, _) = try await URLSession.shared.data(from: c.url!)
        return (try JSONDecoder().decode(R.self, from: data).results ?? []).map {
            Place(name: [$0.name, $0.admin1, $0.country].compactMap { $0 }.filter { !$0.isEmpty }.joined(separator: ", "), latitude: $0.latitude, longitude: $0.longitude)
        }
    }

    private func tick() {
        let now = Date()
        if retryPending && clock.isConnected && !clock.isBusy { retryPending = false; send(user: false) }
        let hour = Calendar.current.component(.hour, from: now)
        if lastHour < 0 { lastHour = hour }
        else if hour != lastHour && Calendar.current.component(.minute, from: now) >= 1 { lastHour = hour; send(user: false) }  // advance the window
        if options.on && options.rainMode == 1 && options.temperature && rainDue && now >= nextSwap {
            phaseRain.toggle()
            nextSwap = now.addingTimeInterval(TimeInterval(options.cycle))
            send(user: false)
        }
    }

    func send(user: Bool) {
        guard clock.isConnected, clock.settings != nil || clock.demoMode, !clock.calibrationBusy else { return }
        if clock.isBusy { retryPending = true; return }
        let now = Date(), offset = TimeZone.current.secondsFromGMT(for: now)
        var temp: ForecastScene?, rain: ForecastScene?
        if let f = forecast {
            if options.temperature { temp = WeatherScenes.temperature(f, now: now, utcOffset: offset, fahrenheit: options.fahrenheit, coldAt: options.coldAt, hotAt: options.hotAt) }
            if options.rainMode != 0 { rain = WeatherScenes.rain(f, now: now, utcOffset: offset, minMm: options.rainMm, minChance: options.rainChance, lookahead: options.rainHours) }
        }
        let due = rain != nil
        if !due { phaseRain = false }
        var show = 0
        if options.on {
            if due && (options.rainMode == 2 || temp == nil) { show = 2 }
            else if due && options.rainMode == 1 { show = phaseRain ? 2 : 1 }
            else if temp != nil { show = 1 }
        }
        if options.on && oldSlotCleanup {
            oldSlotCleanup = false
            clock.sendQuietly(GlanceCommands.deleteScene(slot: 2), title: "Remove any second weather scene", logSuccess: false)
        }
        let command: Data? = show == 0
            ? (showing != 0 || lastCommand == nil && options.on ? GlanceCommands.deleteScene(slot: 1) : nil)
            : GlanceCommands.forecast(slot: 1, scene: show == 2 ? rain! : temp!, ringOnly: !options.text)
        let dueChanged = due != rainDue
        rainDue = due
        guard let command, command != lastCommand || user else { showing = show; return }
        let label = show == 2 ? "Rain forecast" : show == 1 ? "Temperature forecast" : "Weather removed"
        clock.sendQuietly(command, title: label, logSuccess: user || dueChanged || (show == 0) != (showing == 0)) { [weak self] ok in
            guard let self else { return }
            guard ok else { self.retryPending = self.clock.isConnected; return }
            self.lastCommand = command; self.showing = show; self.sent = Date()
            if user && show != 0 { self.clock.sendQuietly(GlanceCommands.refreshDisplay, title: "Refresh display", logSuccess: false) }
        }
    }

    /// Next 12 hours for the preview.
    func previewHours(now: Date = Date()) -> [(label: String, temp: Double, rain: Int)] {
        guard let f = forecast, let start = f.startIndex(at: now) else { return [] }
        let df = DateFormatter(); df.dateFormat = "HH"
        return (0..<12).compactMap { i in
            let k = start + i
            guard k < f.hours.count else { return nil }
            return (df.string(from: f.hours[k]), f.temperature[k],
                    WeatherScenes.rainValue(rain: f.rain[k], chance: f.chance[k], minMm: options.rainMm, minChance: options.rainChance))
        }
    }

    func loadDemo(_ forecast: HourlyForecast) { self.forecast = forecast; fetched = Date(); showing = 1 }
}
