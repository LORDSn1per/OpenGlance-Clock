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
            clock.announceDisplayCommand(90)
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
    /// True while the seconds pixel owns the ring slot; weather waits (as on the bridge).
    var ringTaken: () -> Bool = { false }

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

    /// The seconds pixel released the slot (it deleted its scene): send the weather ring again.
    func ringReleased() { lastCommand = nil; showing = 0; send(user: false) }

    func send(user: Bool) {
        guard !ringTaken() else { return }
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

// MARK: Seconds pixel

/// A single ticking pixel on the ring, drawn by the clock from ~10-second scenes (SecondsScene),
/// while this app is open and connected. A port of the bridge's updateComet(): same scenes, pauses,
/// slot sharing with weather and error latching. Never changes the clock's settings or time.
@MainActor
final class SecondsService: ObservableObject {
    @Published private(set) var on = UserDefaults.standard.bool(forKey: "secondsOn")
    @Published private(set) var color = UserDefaults.standard.object(forKey: "secondsColor") as? Int ?? 18
    @Published private(set) var rings = UserDefaults.standard.object(forKey: "secondsRings") as? Int ?? SecondsScene.outerRing
    @Published private(set) var status = "Off — weather ring available"
    @Published private(set) var running = false
    @Published private(set) var failed = false
    private let clock: BluetoothController
    private let alarms: AlarmStore
    private let chime: ChimeService
    private weak var weather: WeatherService?
    private var timer: Timer?
    private var ownsSlot = false, nextStep: Int64 = -1, inFlight = false, wasTimer = false, releasing = false
    private var holdUntil = Date.distantPast, lastStateRead = Date.distantPast

    init(clock: BluetoothController, alarms: AlarmStore, chime: ChimeService, weather: WeatherService) {
        self.clock = clock; self.alarms = alarms; self.chime = chime; self.weather = weather
        if !SecondsScene.validColor(color) { color = 18 }
        if !SecondsScene.validRings(rings) { rings = SecondsScene.outerRing }
        weather.ringTaken = { [weak self] in self?.on ?? false }
        clock.onDisplayCommand.append { [weak self] quiet in self?.suspend(hold: quiet) }
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in Task { @MainActor in self?.tick() } }
    }

    /// Saves the options. Like the bridge's /api/comet: pause first, clear any error, start again.
    func apply(on: Bool, color: Int, rings: Int) {
        guard SecondsScene.validColor(color), SecondsScene.validRings(rings) else { return }
        suspend()
        self.on = on; self.color = color; self.rings = rings; failed = false; nextStep = -1
        UserDefaults.standard.set(on, forKey: "secondsOn"); UserDefaults.standard.set(color, forKey: "secondsColor"); UserDefaults.standard.set(rings, forKey: "secondsRings")
        clock.log(on ? "Seconds on, colour \(SecondsScene.colours.first { $0.id == color }?.name ?? String(color)), LEDs \(rings)" : "Seconds off; restoring weather")
        if !on { weather?.ringReleased() }   // queued after the delete above
    }
    /// Removes the seconds scene before a disconnect or quit, so no stopped pixel is left on the ring.
    func release(then done: @escaping () -> Void) {
        releasing = true; nextStep = -1
        guard ownsSlot, clock.isConnected, !clock.demoMode else { done(); return }
        var finished = false
        let finish = { if !finished { finished = true; done() } }
        clock.sendQuietly(GlanceCommands.deleteScene(slot: SecondsScene.slot), title: "Remove seconds scene", logSuccess: false) { [weak self] ok in
            if ok { self?.ownsSlot = false }
            finish()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { finish() }
    }
    func retry() { apply(on: on, color: color, rings: rings) }

    private func setStatus(_ text: String, running: Bool = false) {
        if status != text { status = text }
        if self.running != running { self.running = running }
    }
    /// Removes our scene (if any) and optionally holds off for `hold` seconds.
    func suspend(hold: TimeInterval = 0) {
        if hold > 0 { holdUntil = max(holdUntil, Date().addingTimeInterval(hold)) }
        nextStep = -1
        guard ownsSlot, clock.isConnected, !clock.demoMode else { return }
        ownsSlot = false
        clock.sendQuietly(GlanceCommands.deleteScene(slot: SecondsScene.slot), title: "Pause seconds", logSuccess: false) { [weak self] ok in
            guard let self else { return }
            if !ok && self.clock.isConnected { self.ownsSlot = true; self.latchFailure("Pausing the seconds failed") }
        }
    }
    private func latchFailure(_ why: String) {
        failed = true; running = false
        setStatus("Stopped after a Bluetooth error — press Retry")
        clock.log("Seconds stopped", why + ". Press Retry on the Display tab.", error: true)
    }

    private func tick() {
        if releasing { if !clock.isConnected { releasing = false }; return }   // until the disconnect lands
        guard !inFlight else { return }
        if !on {
            if ownsSlot { suspend() }
            if !failed { setStatus("Off — weather ring available") }
            return
        }
        if clock.demoMode { setStatus("Demo · seconds preview only, nothing is sent"); return }
        if failed { setStatus("Stopped after a Bluetooth error — press Retry"); return }
        guard clock.isConnected, clock.settings != nil else {
            ownsSlot = false; nextStep = -1   // a new connection starts with a fresh scene
            setStatus("Waiting for the clock"); return
        }
        let now = Date()
        // Read-only state checks: quicker while paused, slower while playing.
        let blockedState = clock.handState == nil || clock.calibrationBusy || clock.doNotDisturbActive
        if now.timeIntervalSince(lastStateRead) >= (blockedState ? 2 : 5) { lastStateRead = now; clock.readHandState() }
        guard let updated = clock.handStateUpdated, now.timeIntervalSince(updated) <= 12 else {
            suspend(); setStatus("Paused — waiting for fresh clock status"); return
        }
        let c = Calendar.current.dateComponents([.hour, .minute, .weekday], from: now)
        let day = ((c.weekday ?? 1) + 5) % 7   // Monday = 0
        let alarmDue = alarms.alarms.contains { $0.enabled && $0.hour == c.hour && $0.minute == c.minute && ($0.days == 0 || $0.days & (1 << day) != 0) }
        let chimeDue = c.minute == 0 && ((clock.settings?.builtInChimeSound ?? 0) != 0 || (chime.on && !chime.isQuiet(c.hour ?? 0)))
        let timerRunning = clock.timerEnd.map { $0 > now } ?? false
        if wasTimer && !timerRunning { holdUntil = max(holdUntil, now.addingTimeInterval(15)) }   // timer-complete sound/animation
        wasTimer = timerRunning
        let hands = clock.calibrationBusy, dnd = clock.doNotDisturbActive
        if hands || timerRunning || dnd || alarmDue || chimeDue || holdUntil > now {
            suspend()
            setStatus(hands ? "Paused for hand calibration" : timerRunning ? "Paused for the timer" : dnd ? "Paused — clock Do Not Disturb"
                      : alarmDue || chimeDue ? "Paused for an alarm or hourly chime" : "Paused for a message or timer finish")
            return
        }
        // Still inside the steps the current scene draws by itself (about 10 s)?
        if ownsSlot && nextStep >= 0 && Self.step(at: now) < nextStep && Self.step(at: now) > nextStep - 2 * Int64(SecondsScene.pixels) { return }
        guard clock.canReadFace else { suspend(); setStatus("Unavailable: clock display status cannot be read"); return }
        inFlight = true
        // Read the active face before each scene; an unknown face fails closed.
        clock.readFace { [weak self] face in
            guard let self else { return }
            guard let face else {
                self.inFlight = false
                if self.clock.isConnected { self.suspend(); self.latchFailure("Could not read the active clock face") }   // a dropped link just resets
                return
            }
            if SecondsScene.alertFace(face) { self.inFlight = false; self.suspend(hold: 5); self.setStatus("Paused for the clock's alert display"); return }
            self.sendScene()
        }
    }
    private static func frame(at date: Date) -> Int {
        let t = date.timeIntervalSince1970, whole = t.rounded(.down)
        return Int(whole.truncatingRemainder(dividingBy: 60)) * SecondsScene.fps + min(49, Int((t - whole) * Double(SecondsScene.fps)))
    }
    private static func step(at date: Date) -> Int64 {
        Int64((date.timeIntervalSince1970 / 60).rounded(.down)) * Int64(SecondsScene.pixels) + Int64(SecondsScene.pixelAt(frame: frame(at: date)))
    }
    private func sendScene() {
        let now = Date(), frame = Self.frame(at: now), step = Self.step(at: now)
        let continuation = ownsSlot && step == nextStep
        guard on, !failed, let scene = SecondsScene.scene(frame: frame, color: color, continuation: continuation, rings: rings) else { inFlight = false; return }
        let starting = !ownsSlot
        let finish: (Bool) -> Void = { [weak self] ok in
            guard let self else { return }
            self.inFlight = false
            guard ok else {
                guard self.clock.isConnected else { self.ownsSlot = false; self.nextStep = -1; return }   // link dropped: start fresh on reconnect
                self.ownsSlot = true; self.latchFailure("A seconds scene was not acknowledged"); return   // it may have acted; Off/Retry cleans up
            }
            self.ownsSlot = true; self.nextStep = step + Int64(scene.endPixel - SecondsScene.pixelAt(frame: frame))
            self.setStatus("Playing · single pixel, new scene about every \(SecondsScene.sceneSteps[self.rings] == 8 ? "10" : "7.5") s", running: true)
            if starting { self.clock.log("Seconds started", "Ring only, silent. Runs while this app is open.") }
        }
        let write = { [weak self] in
            guard let self else { return }
            self.clock.sendQuietly(scene.data, title: "Seconds scene", logSuccess: false) { [weak self] ok in
                guard let self else { return }
                if ok && starting {
                    // Start once (command 31, the reference's silent start; not 35, the cloud-fetch animation).
                    self.clock.sendQuietly(Data([31, 0, 0, 0]), title: "Start seconds scene", logSuccess: false, completion: finish)
                } else { finish(ok) }
            }
        }
        guard starting else { write(); return }
        // Weather forecasts are stored apart from custom scenes, so remove both weather slots first.
        clock.sendQuietly(GlanceCommands.deleteScene(slot: 2), title: "Remove old weather scene (slot 2)", logSuccess: false) { [weak self] ok in
            guard let self else { return }
            guard ok else { finish(false); return }
            self.clock.sendQuietly(GlanceCommands.deleteScene(slot: 1), title: "Remove old weather scene (slot 1)", logSuccess: false) { ok in
                if ok { write() } else { finish(false) }
            }
        }
    }
}
