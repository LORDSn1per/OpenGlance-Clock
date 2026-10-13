import SwiftUI
import AppKit
import GlanceProtocol

// Explicit wrapper alias supports Command Line Tools without SwiftUI macro plugins.
typealias ViewState<Value> = SwiftUI.State<Value>

// MARK: Theme (same tokens as the bridge web page, light and dark)

extension NSColor {
    convenience init(hex: UInt32) {
        self.init(srgbRed: CGFloat((hex >> 16) & 255) / 255, green: CGFloat((hex >> 8) & 255) / 255, blue: CGFloat(hex & 255) / 255, alpha: 1)
    }
}
extension Color {
    init(light: UInt32, dark: UInt32) {
        self.init(nsColor: NSColor(name: nil) { a in a.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? NSColor(hex: dark) : NSColor(hex: light) })
    }
    init(hex: UInt32) { self.init(nsColor: NSColor(hex: hex)) }
}
enum Theme {
    static let bg = Color(light: 0xEEF2F4, dark: 0x1C2328)
    static let card = Color(light: 0xFFFFFF, dark: 0x252E35)
    static let ink = Color(light: 0x23323D, dark: 0xE6ECEF)
    static let muted = Color(light: 0x5F707C, dark: 0x9FB0BA)
    static let line = Color(light: 0xD8E0E5, dark: 0x37434C)
    static let teal = Color(hex: 0x16897F)
    static let tealBg = Color(light: 0xE3F3F1, dark: 0x1D4441)
    static let tealInk = Color(light: 0x0F6F67, dark: 0x7FDCD1)
    static let warn = Color(light: 0xA35C00, dark: 0xF0B45A), warnBg = Color(light: 0xFFF4E0, dark: 0x3A2E17)
    static let err = Color(light: 0xB3261E, dark: 0xFF8A80), errBg = Color(light: 0xFDE9E7, dark: 0x3D2321)
    static let ok = Color(light: 0x1D7A3E, dark: 0x7ED69A), okBg = Color(light: 0xE5F4EA, dark: 0x1D3826)
}

struct Card<Content: View>: View {
    let title: String
    @ViewBuilder let content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.system(size: 15, weight: .semibold))
            content
        }
        .padding(16).frame(maxWidth: .infinity, alignment: .topLeading)
        .background(RoundedRectangle(cornerRadius: 14).fill(Theme.card))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(Theme.line))
    }
}
enum PillKind { case good, mid, bad, plain }
struct Pill: View {
    let text: String; let kind: PillKind
    var body: some View {
        let (fg, bg): (Color, Color) = switch kind {
        case .good: (Theme.ok, Theme.okBg)
        case .mid: (Theme.warn, Theme.warnBg)
        case .bad: (Theme.err, Theme.errBg)
        case .plain: (Theme.tealInk, Theme.tealBg)
        }
        HStack(spacing: 6) { Circle().fill(fg).frame(width: 8, height: 8); Text(text).font(.system(size: 13, weight: .semibold)) }
            .foregroundStyle(fg).padding(.horizontal, 10).padding(.vertical, 4).background(Capsule().fill(bg))
    }
}
struct OGButton: ButtonStyle {
    enum Kind { case primary, secondary, danger }
    var kind = Kind.primary
    @Environment(\.isEnabled) private var enabled
    func makeBody(configuration: Configuration) -> some View {
        let (fg, bg): (Color, Color) = switch kind {
        case .primary: (.white, Theme.teal)
        case .secondary: (Theme.tealInk, Theme.tealBg)
        case .danger: (Theme.err, Theme.errBg)
        }
        configuration.label.font(.system(size: 14, weight: .semibold)).foregroundStyle(fg)
            .padding(.horizontal, 14).padding(.vertical, 8)
            .background(RoundedRectangle(cornerRadius: 9).fill(bg).brightness(configuration.isPressed ? -0.06 : 0))
            .opacity(enabled ? 1 : 0.45)
    }
}
extension View {
    func og(_ kind: OGButton.Kind = .primary) -> some View { buttonStyle(OGButton(kind: kind)) }
    func note() -> some View { font(.system(size: 13)).foregroundStyle(Theme.muted).fixedSize(horizontal: false, vertical: true) }
}
struct SwitchRow: View {
    let title: String; var detail: String? = nil
    let isOn: Bool; let set: (Bool) -> Void
    var body: some View {
        HStack(alignment: .center) {
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.system(size: 14))
                if let detail { Text(detail).note() }
            }
            Spacer()
            Toggle("", isOn: Binding(get: { isOn }, set: set)).labelsHidden().toggleStyle(.checkbox)
        }
        .padding(.vertical, 6)
        .overlay(alignment: .bottom) { Rectangle().fill(Theme.line).frame(height: 1) }
    }
}
struct KV: View {
    let rows: [(String, String)]
    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 4) {
            ForEach(rows.indices, id: \.self) { i in
                GridRow { Text(rows[i].0).foregroundStyle(Theme.muted); Text(rows[i].1).textSelection(.enabled) }
            }
        }.font(.system(size: 14))
    }
}
struct Big: View { let text: String; var body: some View { Text(text).font(.system(size: 26, weight: .semibold)).monospacedDigit() } }
struct Logo: View {
    var size: CGFloat = 34
    var body: some View {
        if let url = Bundle.module.url(forResource: "Logo", withExtension: "png"), let image = NSImage(contentsOf: url) {
            Image(nsImage: image).resizable().interpolation(.high).frame(width: size, height: size).clipShape(RoundedRectangle(cornerRadius: size * 0.24))
        } else { Image(systemName: "clock").font(.system(size: size * 0.7)).foregroundStyle(Theme.teal) }
    }
}
struct SoundPicker: View {
    @Binding var sound: Int
    var allowNone = false
    let clock: BluetoothController
    var body: some View {
        HStack {
            Picker("", selection: $sound) {
                ForEach(allowNone ? 0..<18 : 1..<18, id: \.self) { Text(Glance.sounds[$0]).tag($0) }
            }.labelsHidden()
            Button("Test") { clock.testSound(sound) }.og(.secondary).disabled(sound == 0 || !clock.canControl)
        }
    }
}
struct LogList: View {
    let events: [ActivityItem]
    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            if events.isEmpty { Text("No activity yet.").note() }
            ForEach(events) { e in
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text(e.date.formatted(date: .omitted, time: .standard)).foregroundStyle(Theme.muted)
                    Text(e.detail.isEmpty ? e.title : "\(e.title) — \(e.detail)").foregroundStyle(e.isError ? Theme.err : Theme.ink)
                }
            }
        }
        .font(.system(size: 12.5, design: .monospaced)).textSelection(.enabled)
        .padding(10).frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 10).fill(Theme.bg))
    }
}

// MARK: App

@MainActor
final class AppModel: ObservableObject {
    let clock = BluetoothController()
    lazy var alarms = AlarmStore(clock: clock)
    lazy var chime = ChimeService(clock: clock)
    lazy var weather = WeatherService(clock: clock)
    lazy var seconds = SecondsService(clock: clock, alarms: alarms, chime: chime, weather: weather)
    /// Removes the seconds scene first, so no stopped pixel stays on the ring.
    func disconnect(forget: Bool = false) {
        seconds.release { [clock] in if forget { clock.forgetClock() } else { clock.disconnect() } }
    }
}

enum Tab: String, CaseIterable { case home = "Home", alerts = "Alerts", alarms = "Alarms", weather = "Weather", display = "Display", clock = "Clock" }

final class ApplicationDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

@main
struct OpenGlanceApp: App {
    @NSApplicationDelegateAdaptor(ApplicationDelegate.self) private var delegate
    @StateObject private var model = AppModel()
    var body: some Scene {
        WindowGroup("OpenGlance Clock", id: "main") {
            RootView(model: model, clock: model.clock, alarms: model.alarms, chime: model.chime, weather: model.weather, seconds: model.seconds)
                .onAppear { if !Screenshot.runIfRequested(model) { model.clock.connectSavedOnLaunch() } }
        }
        .defaultSize(width: 1100, height: 800)
        .commands {
            CommandGroup(replacing: .appTermination) {
                Button("Quit OpenGlance Clock") {
                    model.seconds.release {
                        model.clock.disconnect()
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { NSApp.terminate(nil) }
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { exit(EXIT_SUCCESS) }   // no documents to save
                    }
                }.keyboardShortcut("q")
            }
        }
    }
}

struct RootView: View {
    let model: AppModel
    @ObservedObject var clock: BluetoothController
    @ObservedObject var alarms: AlarmStore
    @ObservedObject var chime: ChimeService
    @ObservedObject var weather: WeatherService
    @ObservedObject var seconds: SecondsService
    @ViewState var tab: Tab = .home

    var body: some View {
        VStack(spacing: 0) {
            header
            tabBar
            ScrollView {
                VStack(spacing: 16) {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 360), spacing: 16, alignment: .top)], alignment: .leading, spacing: 16) {
                        switch tab {
                        case .home: HomeTab(model: model, clock: clock, weather: weather, seconds: seconds, go: { tab = $0 })
                        case .alerts: MessagesTab(clock: clock, chime: chime)
                        case .alarms: AlarmsTab(clock: clock, store: alarms)
                        case .weather: WeatherTab(weather: weather, secondsOn: seconds.on)
                        case .display: DisplayTab(clock: clock, seconds: seconds)
                        case .clock: ClockTab(model: model, clock: clock)
                        }
                    }
                    if tab == .home { Card(title: "Recent activity") { LogList(events: Array(clock.events.prefix(6))) } }
                    if tab == .clock { Card(title: "Activity") { LogList(events: clock.events) } }
                }.padding(18)
            }
        }
        .frame(minWidth: 820, minHeight: 640)
        .background(Theme.bg).foregroundStyle(Theme.ink).tint(Theme.teal)
        .alert("OpenGlance Clock", isPresented: Binding(get: { clock.errorMessage != nil }, set: { if !$0 { clock.errorMessage = nil } })) {
            Button("OK") { clock.errorMessage = nil }
        } message: { Text(clock.errorMessage ?? "") }
    }

    private var stateInfo: (String, PillKind) {
        if clock.demoMode { return ("Demo", .mid) }
        if clock.isConnected && clock.settings != nil { return ("Connected", .good) }
        if clock.isConnected || clock.isConnecting { return ("Connecting…", .mid) }
        if clock.isScanning { return ("Searching…", .mid) }
        return (clock.hasRememberedClock ? "Not connected" : "No clock paired", .bad)
    }
    private var header: some View {
        HStack(spacing: 12) {
            Logo()
            Text("OpenGlance").font(.system(size: 19, weight: .semibold))
            Spacer()
            Pill(text: "Clock: \(stateInfo.0)", kind: stateInfo.1)
            if let b = clock.battery { Pill(text: "Battery \(b)%", kind: b > 20 ? .good : .mid) }
            if seconds.on { Pill(text: seconds.running ? "Seconds on ring" : "Seconds paused", kind: .plain) }
            else if weather.options.on { Pill(text: weather.showing == 0 ? "Weather off" : weather.rainDue ? "Rain coming" : "Weather on ring", kind: .plain) }
        }
        .padding(.horizontal, 18).padding(.vertical, 12)
        .background(Theme.card).overlay(alignment: .bottom) { Rectangle().fill(Theme.line).frame(height: 1) }
    }
    private var tabBar: some View {
        HStack(spacing: 4) {
            ForEach(Tab.allCases, id: \.self) { t in
                Button { tab = t } label: {
                    Text(t.rawValue).font(.system(size: 14, weight: .semibold))
                        .foregroundStyle(tab == t ? Color.white : Theme.muted)
                        .padding(.horizontal, 14).padding(.vertical, 7)
                        .background(Capsule().fill(tab == t ? Theme.teal : Color.clear))
                        .contentShape(Capsule())
                }.buttonStyle(.plain)
            }
            Spacer()
            Text("v\(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev")").note()
        }
        .padding(.horizontal, 14).padding(.vertical, 8)
        .background(Theme.card).overlay(alignment: .bottom) { Rectangle().fill(Theme.line).frame(height: 1) }
    }
}

// MARK: Home

struct HomeTab: View {
    let model: AppModel
    @ObservedObject var clock: BluetoothController
    @ObservedObject var weather: WeatherService
    @ObservedObject var seconds: SecondsService
    let go: (Tab) -> Void
    var body: some View {
        if !clock.hasRememberedClock && !clock.demoMode {
            Card(title: "Pair your clock") {
                Text("No clock has been connected from this Mac yet.").note()
                Button("Pair a clock") { go(.clock) }.og()
            }
        }
        Card(title: "Clock") {
            Big(text: clock.demoMode ? "Demo" : clock.isConnected && clock.settings != nil ? "Connected" : clock.isConnecting ? "Connecting…" : "Not connected")
            Text(clock.status).note()
            KV(rows: [("Clock", clock.isConnected || clock.demoMode ? clock.clockName : (clock.rememberedName ?? "—")),
                      ("Battery", clock.battery.map { "\($0)%" } ?? "—"),
                      ("Hands", clock.handStatus)])
            HStack {
                Button("Connect") { clock.scan(reconnect: true) }.og(.secondary).disabled(clock.isConnected || clock.isConnecting || !clock.hasRememberedClock)
                Button("Disconnect") { model.disconnect() }.og(.secondary).disabled(!clock.isConnected)
                Button("Refresh") { clock.refreshSettings() }.og(.secondary).disabled(!clock.canControl || clock.demoMode)
            }
            Text("Setting the clock's time needs the OpenGlance Bridge; macOS does not allow it.").note()
        }
        Card(title: "Weather") {
            Big(text: weather.options.on ? weather.forecast?.current.map { "\(Int($0.rounded()))°\(weather.options.fahrenheit ? "F" : "C")" } ?? "—" : "—")
            Text(seconds.on ? "Paused while the seconds pixel uses the ring" : weather.options.on ? "\(weather.place?.name ?? "No location") · ring: \(weather.showing == 0 ? "nothing" : weather.rainDue ? "rain coming" : "temperature")" : "Weather ring is off").note()
            Button("Weather settings →") { go(.weather) }.buttonStyle(.plain).foregroundStyle(Theme.tealInk).font(.system(size: 14, weight: .semibold))
        }
    }
}

// MARK: Messages

struct MessagesTab: View {
    @ObservedObject var clock: BluetoothController
    @ObservedObject var chime: ChimeService
    @ViewState private var text = "Hello world"
    @ViewState private var animation = 1
    @ViewState private var colour = 18
    @ViewState private var sound = 0
    @ViewState private var minutes = 10
    var body: some View {
        Card(title: "Message") {
            Text("Text (up to 64 plain characters)").note()
            TextField("", text: $text).textFieldStyle(.roundedBorder)
            HStack {
                Picker("Animation", selection: $animation) { ForEach(Glance.animations, id: \.0) { Text($0.1).tag($0.0) } }
                Picker("Colour", selection: $colour) { ForEach(Glance.colours, id: \.0) { Text($0.1).tag($0.0) } }
            }
            Text("Sound").note()
            SoundPicker(sound: $sound, allowNone: true, clock: clock)
            Button("Send to clock") { clock.sendMessage(text, animation: animation, sound: sound, color: colour) }.og().disabled(!clock.canControl)
            Text("If no sound plays, check that Mute is off on the Display tab.").note()
        }
        Card(title: "Timer") {
            TimelineView(.periodic(from: .now, by: 1)) { context in
                let left = max(0, Int((clock.timerEnd ?? .distantPast).timeIntervalSince(context.date)))
                Big(text: left > 0 ? String(format: "%d:%02d", left / 60, left % 60) : "—")
            }
            Text("The countdown here is an estimate from when the timer was sent.").note()
            HStack {
                Stepper(value: $minutes, in: 1...720) { Text("\(minutes) min") }
                Button("Start") { clock.startTimer(minutes: minutes) }.og().disabled(!clock.canControl)
                Button("Stop") { clock.stopTimer() }.og(.secondary).disabled(!clock.canControl)
            }
            HStack {
                ForEach([5, 15, 30, 60], id: \.self) { m in
                    Button(m == 60 ? "1 hour" : "\(m) min") { minutes = m; clock.startTimer(minutes: m) }.og(.secondary).disabled(!clock.canControl)
                }
            }
        }
        Card(title: "App hourly chime") {
            SwitchRow(title: "Chime every hour", detail: "Shows the time with a sound, while this app is open", isOn: chime.on) { chime.on = $0 }
            Text("Sound").note()
            SoundPicker(sound: $chime.sound, clock: clock)
            Text("Quiet hours (no chime)").note()
            HStack {
                Picker("From", selection: $chime.quietFrom) { ForEach(0..<24, id: \.self) { Text(String(format: "%02d:00", $0)).tag($0) } }
                Picker("To", selection: $chime.quietTo) { ForEach(0..<24, id: \.self) { Text(String(format: "%02d:00", $0)).tag($0) } }
            }
            Text("Set both to the same hour to chime around the clock. The clock also mutes sounds in its own night mode.").note()
        }
        Card(title: "Clock’s built-in hourly chime") {
            Text("A separate sound saved in the clock. It can play with this app closed. The app’s quiet hours do not control it.").note()
            Picker("Saved sound", selection: Binding(get: { clock.settings?.builtInChimeSound ?? 0 }, set: { clock.changeSettings([15: $0]) })) {
                ForEach(0..<Glance.sounds.count, id: \.self) { n in Text(n == 0 ? "Off" : Glance.sounds[n]).tag(UInt64(n)) }
                if let value = clock.settings?.builtInChimeSound, value > 17 { Text("Unknown sound (\(value))").tag(value) }
            }.disabled(!clock.canControl || clock.settings == nil)
            if let value = clock.settings?.builtInChimeSound {
                Text("Read from clock: \(value == 0 ? "Off" : value <= 17 ? Glance.sounds[Int(value)] : "Unknown sound (\(value))")").note()
            } else { Text("Connect the clock to read its saved sound.").note() }
            Button("Read clock settings") { clock.refreshSettings() }.og(.secondary).disabled(!clock.canControl)
            Text("Off stops only this chime, not alarms, timers or messages. Each change is read back. Leave this Off when using the app chime to avoid two sounds.").note()
        }
    }
}

// MARK: Alarms

struct AlarmsTab: View {
    @ObservedObject var clock: BluetoothController
    @ObservedObject var store: AlarmStore
    @ViewState private var editing: AlarmEntry?
    @ViewState private var confirmClear = false
    var body: some View {
        Card(title: "Alarms") {
            if store.alarms.isEmpty { Text("No alarms saved on this Mac.").note() }
            ForEach(store.alarms) { a in
                HStack {
                    Text(String(format: "%02d:%02d", a.hour, a.minute)).font(.system(size: 20, weight: .semibold)).monospacedDigit()
                    Text("\(Glance.daysText(a.days)) · \(Glance.sounds[a.sound])\(a.text.isEmpty ? "" : " · “\(a.text)”")").note()
                    Spacer()
                    Toggle("", isOn: Binding(get: { a.enabled }, set: { store.setEnabled(a, $0) })).labelsHidden().toggleStyle(.checkbox)
                    Button("Edit") { editing = a }.og(.secondary)
                    Button("Delete") { store.delete(a) }.og(.danger)
                }
                .padding(.vertical, 4).overlay(alignment: .bottom) { Rectangle().fill(Theme.line).frame(height: 1) }
            }
            Button("Add alarm") { editing = AlarmEntry() }.og().disabled(store.alarms.count >= 4)
            Text(store.pending ? "Changes are waiting to be sent to the clock (they are sent when it connects)." : "The clock holds up to 4 alarms.").note()
        }
        if let alarm = editing {
            AlarmEditor(alarm: alarm, clock: clock, isNew: !store.alarms.contains { $0.id == alarm.id },
                        save: { store.save($0); editing = nil }, cancel: { editing = nil })
                .id(alarm.id)
        }
        Card(title: "Alarm ringing?") {
            Button("Stop the alarm now") { clock.stopAlarm() }.og().disabled(!clock.canControl)
            Text("The clock can't report which alarms it holds. Saving an alarm here replaces every alarm on the clock, including old ones set from the phone app.").note()
            Button("Delete all alarms on the clock") { confirmClear = true }.og(.danger)
        }
        .confirmationDialog("Delete every alarm on the clock and on this Mac?", isPresented: $confirmClear) {
            Button("Delete all alarms", role: .destructive) { store.deleteAll() }
        }
    }
}

struct AlarmEditor: View {
    @ViewState var alarm: AlarmEntry
    let clock: BluetoothController
    let isNew: Bool
    let save: (AlarmEntry) -> Void
    let cancel: () -> Void
    init(alarm: AlarmEntry, clock: BluetoothController, isNew: Bool, save: @escaping (AlarmEntry) -> Void, cancel: @escaping () -> Void) {
        _alarm = ViewState(initialValue: alarm); self.clock = clock; self.isNew = isNew; self.save = save; self.cancel = cancel
    }
    private var time: Binding<Date> {
        Binding(get: { Calendar.current.date(bySettingHour: alarm.hour, minute: alarm.minute, second: 0, of: Date()) ?? Date() },
                set: { let c = Calendar.current.dateComponents([.hour, .minute], from: $0); alarm.hour = c.hour ?? 0; alarm.minute = c.minute ?? 0 })
    }
    var body: some View {
        Card(title: isNew ? "New alarm" : "Edit alarm") {
            DatePicker("Time", selection: time, displayedComponents: .hourAndMinute)
            Text("Days").note()
            HStack {
                ForEach(0..<7, id: \.self) { i in
                    Toggle(Glance.days[i], isOn: Binding(get: { alarm.days & (1 << i) != 0 }, set: { alarm.days = $0 ? alarm.days | (1 << i) : alarm.days & ~(1 << i) }))
                        .toggleStyle(.checkbox)
                }
            }
            HStack {
                Button("Weekdays") { alarm.days = 31 }.og(.secondary)
                Button("Weekend") { alarm.days = 96 }.og(.secondary)
                Button("Every day") { alarm.days = 127 }.og(.secondary)
                Button("Once") { alarm.days = 0 }.og(.secondary)
            }
            Text("Sound").note()
            SoundPicker(sound: $alarm.sound, clock: clock)
            Text("Label on the display (up to 16 characters)").note()
            TextField("Wake up", text: Binding(get: { alarm.text }, set: { alarm.text = String($0.unicodeScalars.filter { (32...126).contains($0.value) }.prefix(16)) }))
                .textFieldStyle(.roundedBorder)
            HStack {
                Button("Save alarm") { save(alarm) }.og().disabled(!alarm.isValid)
                Button("Cancel", action: cancel).og(.secondary)
            }
            Text("“Once” sends the alarm with no repeat days, as the original app did for one-off alarms.").note()
        }
    }
}

// MARK: Weather

struct WeatherTab: View {
    @ObservedObject var weather: WeatherService
    var secondsOn = false
    @ViewState private var query = ""
    @ViewState private var results: [WeatherService.Place] = []
    @ViewState private var searching = false
    @ViewState private var mm = 0.3
    @ViewState private var chance = 40.0
    @ViewState private var coldAt = 5
    @ViewState private var hotAt = 35
    private var o: Binding<WeatherService.Options> { $weather.options }

    var body: some View {
        Card(title: "Weather on the clock") {
            SwitchRow(title: "Show weather on the ring", isOn: weather.options.on) { weather.options.on = $0 }
            SwitchRow(title: "Temperature ring (stays up all the time)", isOn: weather.options.temperature) { weather.options.temperature = $0 }
            SwitchRow(title: "Also show the temperature on the display", detail: "Off keeps the time and date on the display", isOn: weather.options.text) { weather.options.text = $0 }
            Picker("When rain is coming", selection: o.rainMode) {
                Text("Alternate rain and temperature").tag(1); Text("Show rain instead of temperature").tag(2); Text("Don't show rain").tag(0)
            }
            if weather.options.rainMode == 1 && weather.options.temperature {
                Picker("Switch rings every", selection: o.cycle) {
                    ForEach([(10, "10 seconds"), (20, "20 seconds"), (30, "30 seconds"), (60, "1 minute"), (120, "2 minutes"), (300, "5 minutes"), (600, "10 minutes")], id: \.0) { Text($0.1).tag($0.0) }
                }
            }
            if weather.options.rainMode != 0 {
                Text("Rain is coming when an hour has at least \((mm * 10).rounded() / 10, specifier: "%.1f") mm").note()
                Slider(value: $mm, in: 0.1...5, onEditingChanged: { editing in mm = (mm * 10).rounded() / 10; if !editing { weather.options.rainMm = mm } })
                Text("… and at least \(Int((chance / 5).rounded() * 5))% chance").note()
                Slider(value: $chance, in: 10...90, onEditingChanged: { editing in chance = (chance / 5).rounded() * 5; if !editing { weather.options.rainChance = Int(chance) } })
                Picker("… within the next", selection: o.rainHours) { ForEach([2, 3, 6, 12], id: \.self) { Text("\($0) hours").tag($0) } }
            }
            Button("Update forecast now") { Task { await weather.fetch(user: true) } }.og(.secondary).disabled(!weather.options.on || weather.place == nil)
            Text(status).note()
            if secondsOn { Text("Paused while the seconds pixel is on (Display tab). Your weather settings are kept.").font(.system(size: 13)).foregroundStyle(Theme.warn) }
            Text("Works while this app is open. The clock hides the rings by itself in night mode.").note()
        }
        .onAppear { mm = weather.options.rainMm; chance = Double(weather.options.rainChance); coldAt = weather.options.coldAt; hotAt = weather.options.hotAt }
        Card(title: "Next 12 hours") { WeatherPreview(weather: weather) }
        Card(title: "Location") {
            Text("Current: \(weather.place?.name ?? "not set")").note()
            HStack {
                TextField("Town or suburb", text: $query).textFieldStyle(.roundedBorder).onSubmit(search)
                Button("Search", action: search).og(.secondary).disabled(searching)
            }
            ForEach(results, id: \.name) { p in
                HStack { Text(p.name); Spacer(); Button("Use") { weather.place = p; weather.options.on = true; results = []; query = "" }.og(.secondary) }
            }
            Text("Weather data by Open-Meteo.com (CC BY 4.0).").note()
        }
        Card(title: "Temperature colours") {
            Picker("Units", selection: o.fahrenheit) { Text("Celsius").tag(false); Text("Fahrenheit").tag(true) }
            LinearGradient(colors: [Color(hex: 0x2060FF), Color(hex: 0x00C070), Color(hex: 0xFFC000), Color(hex: 0xFF3000)], startPoint: .leading, endPoint: .trailing)
                .frame(height: 12).clipShape(Capsule())
            HStack {
                Stepper("Blue at \(coldAt)°", value: $coldAt, in: -40...(hotAt - 1))
                Stepper("Red at \(hotAt)°", value: $hotAt, in: (coldAt + 1)...130)
            }
            Button("Save range") { weather.options.coldAt = coldAt; weather.options.hotAt = hotAt }.og(.secondary)
            Text("Temperatures are coloured blue → green → amber → red across this range. The clock blends between the colours of the day's lowest and highest temperature.").note()
        }
    }
    private var status: String {
        if !weather.options.on { return "Weather ring is off." }
        if let e = weather.error { return e }
        guard let f = weather.fetched else { return weather.place == nil ? "Choose a location below." : "Waiting for the first forecast…" }
        return "Forecast updated \(f.formatted(date: .omitted, time: .shortened))" + (weather.sent.map { " · sent to clock \($0.formatted(date: .omitted, time: .shortened))" } ?? "")
    }
    private func search() {
        let q = query.trimmingCharacters(in: .whitespaces)
        guard q.count >= 2 else { return }
        searching = true
        Task { results = (try? await WeatherService.search(q)) ?? []; searching = false }
    }
}

struct WeatherPreview: View {
    @ObservedObject var weather: WeatherService
    var body: some View {
        let hours = weather.previewHours()
        let o = weather.options
        Text("\(weather.place?.name ?? "No location")\(weather.forecast?.current.map { " · now \(Int($0.rounded()))°" } ?? "")").note()
        if hours.isEmpty { Text("No forecast yet.").note() } else {
            let lo = hours.map(\.temp).min()! - 1, hi = hours.map(\.temp).max()!
            bars(hours.map { h in (h.label, (h.temp - lo) / max(1, hi - lo), Color(hex: WeatherScenes.palette((h.temp - Double(o.coldAt)) / Double(max(1, o.hotAt - o.coldAt)))), "\(Int(h.temp.rounded()))°") }, height: 90)
            let any = hours.prefix(o.rainHours).contains { $0.rain > 0 }
            Text(o.rainMode == 0 ? "Rain ring is off" : any ? "Rain coming (\(String(format: "%.1f", o.rainMm)) mm and \(o.rainChance)% within \(o.rainHours) h) — rain ring in use"
                 : "No rain coming (\(String(format: "%.1f", o.rainMm)) mm and \(o.rainChance)% within \(o.rainHours) h) — temperature only").note().padding(.top, 6)
            bars(hours.map { h in (h.label, Double(h.rain) / 100, h.rain > 0 ? Color(hex: WeatherScenes.mix(0x7FD4FF, 0x0030FF, Double(h.rain) / 100)) : Theme.line, h.rain > 0 ? "\(h.rain)%" : "") }, height: 60)
        }
    }
    private func bars(_ items: [(String, Double, Color, String)], height: CGFloat) -> some View {
        HStack(alignment: .bottom, spacing: 4) {
            ForEach(items.indices, id: \.self) { i in
                VStack(spacing: 2) {
                    Text(items[i].3).font(.system(size: 10)).foregroundStyle(Theme.muted).lineLimit(1).fixedSize()
                    RoundedRectangle(cornerRadius: 4).fill(items[i].2).frame(height: max(3, height * (0.1 + 0.8 * min(1, max(0, items[i].1)))))
                    Text(items[i].0).font(.system(size: 10)).foregroundStyle(Theme.muted)
                }.frame(maxWidth: .infinity)
            }
        }.frame(height: height + 34, alignment: .bottom)
    }
}

// MARK: Display

struct DisplayTab: View {
    @ObservedObject var clock: BluetoothController
    @ObservedObject var seconds: SecondsService
    @ViewState private var manual = 128.0
    @ViewState private var low = 30.0
    @ViewState private var high = 220.0
    var body: some View {
        let s = clock.settings
        Card(title: "Display") {
            setting("Digital time on the display", s?.digitalTime ?? false, 11)
            setting("12-hour time", s?.twelveHour ?? false, 12)
            setting("Hour points always on", s?.hourPoints ?? false, 9)
            setting("Automatic night mode", s?.nightMode ?? false, 2)
            setting("Mute sounds", s?.muted ?? false, 4)
            Picker("Date on the display", selection: Binding(get: { s?.dateFormat ?? 0 }, set: { clock.changeSettings([5: UInt64($0)]) })) {
                ForEach(Glance.dateFormats, id: \.0) { Text($0.1).tag($0.0) }
            }.disabled(!clock.canControl)
        }
        Card(title: "Brightness") {
            let auto = (s?.brightness ?? 0) == 0
            SwitchRow(title: "Automatic (light sensor)", isOn: auto) { on in
                if on { clock.setAutomaticBrightness(low: Int(low), high: Int(high)) } else { clock.changeSettings([10: UInt64(manual)]) }
            }.disabled(!clock.canControl)
            if auto {
                Text("Minimum (darkest) \(Int(low))").note()
                Slider(value: $low, in: 0...255, onEditingChanged: { e in if !e { high = max(high, low); clock.setAutomaticBrightness(low: Int(low), high: Int(high)) } })
                Text("Maximum (brightest) \(Int(high))").note()
                Slider(value: $high, in: 0...255, onEditingChanged: { e in if !e { low = min(low, high); clock.setAutomaticBrightness(low: Int(low), high: Int(high)) } })
                Button("Restore 30–220 defaults") { low = 30; high = 220; clock.setAutomaticBrightness(low: 30, high: 220) }.og(.secondary)
                Text("Limits for the sensor-driven range, as in the original app. Changes are sent when you release a slider.").note()
            } else {
                Text("Fixed brightness \(Int(manual))").note()
                Slider(value: $manual, in: 1...255, onEditingChanged: { e in if !e { clock.changeSettings([10: UInt64(manual)]) } })
            }
        }
        .disabled(!clock.canControl)
        .onAppear(perform: sync).onChange(of: clock.settings) { _, _ in sync() }
        NightCard(clock: clock)
        SecondsCard(clock: clock, seconds: seconds)
    }
    private func setting(_ title: String, _ value: Bool, _ field: Int) -> some View {
        SwitchRow(title: title, isOn: value) { clock.changeSettings([field: $0 ? 1 : 0]) }.disabled(!clock.canControl)
    }
    private func sync() {
        guard let s = clock.settings else { return }
        if s.brightness > 0 { manual = Double(s.brightness) }
        low = Double(s.automaticBrightnessLow); high = Double(s.automaticBrightnessHigh)
    }
}

// MARK: Night settings (clock DND and Silent schedules)

struct NightCard: View {
    @ObservedObject var clock: BluetoothController
    @ViewState private var draft: [ClockSchedule.Kind: ClockSchedule] = [:]
    @ViewState private var dirty: Set<ClockSchedule.Kind> = []
    var body: some View {
        Card(title: "Night settings") {
            Text("Stored on the clock. **Do Not Disturb** hides the seconds, the weather ring and notifications. **Silent** only mutes sounds, so the seconds stay visible.").note()
            row(.doNotDisturb, "Do Not Disturb", save: "Save Do Not Disturb")
            row(.silent, "Silent (quiet hours)", save: "Save Silent")
            Text("Silent “Always” is the same as Mute sounds above. Timers and alarms may still sound while muted. For quiet nights with the seconds visible, turn Do Not Disturb off and schedule Silent for the same hours.").note()
        }
        .onAppear(perform: sync).onChange(of: clock.settings) { _, _ in sync() }
    }
    private func current(_ kind: ClockSchedule.Kind) -> ClockSchedule { draft[kind] ?? clock.settings?.schedule(kind) ?? ClockSchedule() }
    private func bind(_ kind: ClockSchedule.Kind, _ path: WritableKeyPath<ClockSchedule, Int>) -> Binding<Int> {
        Binding(get: { current(kind)[keyPath: path] },
                set: { var d = current(kind); d[keyPath: path] = $0; draft[kind] = d; dirty.insert(kind) })
    }
    private func hours(_ value: Binding<Int>) -> some View {
        Picker("", selection: value) { ForEach(0..<24, id: \.self) { Text(String(format: "%02d:00", $0)).tag($0) } }.labelsHidden().frame(width: 90)
    }
    @ViewBuilder private func row(_ kind: ClockSchedule.Kind, _ title: String, save: String) -> some View {
        let read = clock.settings?.schedule(kind)
        let editable = clock.canControl && (read?.known ?? false)
        let d = current(kind)
        Text(title).font(.system(size: 14, weight: .semibold)).padding(.top, 4)
        HStack(spacing: 8) {
            Picker("", selection: bind(kind, \.mode)) { Text("Off").tag(0); Text("Scheduled").tag(1); Text("Always").tag(2) }.labelsHidden().frame(width: 130)
            if d.mode == 1 { Text("from"); hours(bind(kind, \.from)); Text("to"); hours(bind(kind, \.till)) }
            Spacer()
        }.disabled(!editable)
        HStack(spacing: 10) {
            Button(save) { dirty.remove(kind); clock.setSchedule(kind, mode: d.mode, from: d.from, till: d.till) }
                .og(.secondary).disabled(!editable || (d.mode == 1 && d.from == d.till))
            Text(d.mode == 1 && d.from == d.till ? "Start and end hours must differ." : "Read from clock: \(read?.summary ?? "not read yet")").note()
        }
    }
    private func sync() {
        for kind in ClockSchedule.Kind.allCases where !dirty.contains(kind) { draft[kind] = clock.settings?.schedule(kind) }
    }
}

// MARK: Seconds pixel

struct SecondsCard: View {
    @ObservedObject var clock: BluetoothController
    @ObservedObject var seconds: SecondsService
    private var ledCount: Int { (0..<4).filter { seconds.rings >> $0 & 1 != 0 }.count }
    var body: some View {
        Card(title: "Seconds pixel") {
            SwitchRow(title: "Show a ticking seconds pixel", detail: "Runs while this app is open and connected", isOn: seconds.on) {
                seconds.apply(on: $0, color: seconds.color, rings: seconds.rings)
            }
            HStack(alignment: .top, spacing: 18) {
                SecondsPreview(color: SecondsScene.colours.first { $0.id == seconds.color }?.rgb ?? 0x40E0D0, rings: seconds.rings)
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Picker("Colour", selection: Binding(get: { seconds.color }, set: { seconds.apply(on: seconds.on, color: $0, rings: seconds.rings) })) {
                            ForEach(SecondsScene.colours, id: \.id) { Text($0.name).tag($0.id) }
                        }.frame(maxWidth: 230)
                        Circle().fill(Color(hex: SecondsScene.colours.first { $0.id == seconds.color }?.rgb ?? 0x40E0D0)).frame(width: 14, height: 14)
                    }
                    Text("LEDs that light").note()
                    HStack(spacing: 6) {
                        Text("Outer").note()
                        ForEach(0..<4, id: \.self) { bit in
                            let lit = seconds.rings >> bit & 1 != 0
                            Button { seconds.apply(on: seconds.on, color: seconds.color, rings: seconds.rings ^ (1 << bit)) } label: {
                                Text("\(bit + 1)").font(.system(size: 13, weight: .semibold)).frame(width: 30, height: 30)
                                    .foregroundStyle(lit ? Color.white : Theme.muted)
                                    .background(Circle().fill(lit ? Theme.teal : Theme.bg)).overlay(Circle().stroke(Theme.line))
                            }.buttonStyle(.plain).disabled(lit && ledCount == 1)
                                .help(lit && ledCount == 1 ? "Keep at least one LED lit" : bit == 0 ? "Outer ring" : bit == 3 ? "Nearest the centre" : "Ring \(bit + 1)")
                        }
                        Text("Centre").note()
                    }
                    Text("\(ledCount) LED\(ledCount == 1 ? "" : "s") · new clock scene about every \(SecondsScene.sceneSteps[seconds.rings] == 8 ? "10 s" : "7.5 s (a gap between lit LEDs needs two segments per step)")").note()
                    Text("One lap per minute · one step every 1.25 s · illustrative preview").note()
                }
            }
            HStack {
                Text(seconds.status).font(.system(size: 14, weight: .semibold))
                if seconds.failed { Button("Retry") { seconds.retry() }.og(.secondary) }
            }
            Text("Clock Do Not Disturb: \(clock.handState == nil ? "not read" : clock.doNotDisturbActive ? "active" : "inactive"). Automatic night mode is a separate setting.").note()
            Text("Pauses for Do Not Disturb, timers, alarms, chimes and calibration; messages get a 90-second pause. Uses the weather ring's slot; weather comes back when seconds are off. The clock redraws each new scene, so expect a small blip and a time/date switch about every 10 s.").note()
        }
    }
}

struct SecondsPreview: View {
    let color: UInt32
    let rings: Int
    var body: some View {
        TimelineView(.periodic(from: .now, by: 0.25)) { context in
            Canvas { g, size in
                let t = context.date.timeIntervalSince1970
                let head = SecondsScene.pixelAt(frame: Int(t.truncatingRemainder(dividingBy: 60) * Double(SecondsScene.fps)))
                let scale = min(size.width, size.height) / 160
                for (ring, r) in [64.0, 56, 48, 40].enumerated() {
                    for i in 0..<48 {
                        let a = Double(i) * .pi / 24 - .pi / 2
                        let lit = i == head && rings >> ring & 1 != 0
                        let c = CGPoint(x: (80 + r * cos(a)) * scale, y: (80 + r * sin(a)) * scale)
                        g.fill(Path(ellipseIn: CGRect(x: c.x - 2.6 * scale, y: c.y - 2.6 * scale, width: 5.2 * scale, height: 5.2 * scale)),
                               with: .color(lit ? Color(hex: color) : Theme.muted.opacity(0.25)))
                    }
                }
            }
        }
        .frame(width: 150, height: 150)
        .background(Circle().fill(Color(light: 0x1C2328, dark: 0x11171B)))
    }
}

// MARK: Clock (pairing, connection, calibration)

struct ClockTab: View {
    let model: AppModel
    @ObservedObject var clock: BluetoothController
    @ViewState private var confirmForget = false
    @ViewState private var confirmStart = false
    @ViewState private var confirmHands = false
    var body: some View {
        Card(title: "Pair a clock") {
            VStack(alignment: .leading, spacing: 4) {
                Text("1. Quit the bridge or phone app if either is connected to the clock.")
                Text("2. **Short-press** the clock's button to activate pairing mode (do not hold it).")
                Text("3. Press **Find clocks**, then **Connect** beside your clock.")
                Text("4. If macOS asks, enter the PIN shown on the clock.")
            }.font(.system(size: 14))
            HStack {
                Button(clock.isScanning ? "Searching…" : "Find clocks") { clock.scan() }.og().disabled(clock.isScanning || clock.isConnecting || clock.isConnected)
                if clock.isScanning { ProgressView().controlSize(.small) }
            }
            ScrollView {
                VStack(spacing: 0) {
                    ForEach(clock.nearby) { item in
                        HStack {
                            VStack(alignment: .leading) { Text(item.name).bold(); Text("\(item.signalLabel) · \(item.signal) dBm").note() }
                            Spacer()
                            Button("Connect") { clock.connect(item.id) }.og().disabled(clock.isConnecting || clock.isConnected)
                        }.padding(8).overlay(alignment: .bottom) { Rectangle().fill(Theme.line).frame(height: 1) }
                    }
                }
            }.frame(maxHeight: 200).opacity(clock.nearby.isEmpty ? 0 : 1).frame(height: clock.nearby.isEmpty ? 0 : nil)
            DisclosureGroup("Forget the saved clock") {
                Text("Removes the clock remembered by this app. It never resets the clock or clears its pairings.").note()
                Button("Forget clock") { confirmForget = true }.og(.danger).disabled(!clock.hasRememberedClock)
            }.foregroundStyle(Theme.tealInk)
            if !clock.isConnected && !clock.demoMode { Button("Explore demo (no Bluetooth)") { clock.setDemo(true) }.buttonStyle(.plain).foregroundStyle(Theme.tealInk) }
            if clock.demoMode { Button("Exit demo") { clock.setDemo(false) }.og(.secondary) }
        }
        .confirmationDialog("Forget the saved clock?", isPresented: $confirmForget) { Button("Forget", role: .destructive) { model.disconnect(forget: true) } }
        ClockTimeCard(clock: clock)
        Card(title: "Connection") {
            SwitchRow(title: "Connect to the saved clock automatically", detail: "Turn off while the OpenGlance Bridge is in use; the clock accepts one connection at a time.", isOn: clock.autoConnect) { clock.autoConnect = $0 }
        }
        Card(title: "Hand calibration") {
            Text("State reported by the clock: **\(clock.handStatus)**").font(.system(size: 14))
            Text("Calibration physically moves the hands. Confirm only when the clock has stopped, shows Confirm with a green segment, and **both hands point exactly to 12**.")
                .font(.system(size: 13)).foregroundStyle(Theme.warn).padding(10).frame(maxWidth: .infinity, alignment: .leading)
                .background(RoundedRectangle(cornerRadius: 10).fill(Theme.warnBg))
            HStack {
                Button("Start calibration") { confirmStart = true }.og(.secondary).disabled(!clock.canControl || clock.isCalibrating)
                Button("Confirm hands at 12") { confirmHands = true }.og().disabled(!clock.canControl || clock.isCalibrating || !clock.needsCalibrationConfirmation)
            }
            DisclosureGroup("Hands stop short of 12 (e.g. 12:14)? Reseating guide") {
                VStack(alignment: .leading, spacing: 4) {
                    Text("1. Only while the clock is stopped, showing Confirm and the green segment.")
                    Text("2. Pull the long minute hand straight off, then the short hour hand. Do not twist hands on the shaft.")
                    Text("3. Refit the hour hand pointing exactly at 12, then the minute hand at 12, keeping clearance from the fabric and between hands.")
                    Text("4. Never force anything. Then press Confirm hands at 12.")
                }.font(.system(size: 13))
            }.foregroundStyle(Theme.tealInk)
        }
        .confirmationDialog("Start calibration? The hands will move.", isPresented: $confirmStart) { Button("Start calibration") { clock.startCalibration() } }
        .confirmationDialog("Both hands point exactly to 12?", isPresented: $confirmHands) { Button("Confirm hands at 12") { clock.confirmCalibration() } }
    }
}

// MARK: Clock time (Settings field 18; macOS cannot serve the Current Time Service)

struct ClockTimeCard: View {
    @ObservedObject var clock: BluetoothController
    @ViewState private var shownText = ""
    @ViewState private var shownDate = Date()
    @ViewState private var shownDateWrong = false
    @ViewState private var manual = false
    @ViewState private var targetText = ""
    @ViewState private var targetDate = Date()
    @ViewState private var setTargetDate = false
    @ViewState private var pending: Int?
    @ViewState private var pendingKnown: Int?
    @ViewState private var confirmReset = false
    private var supported: Bool { clock.timeCorrectionSupported == true }
    private var enabled: Bool { clock.canControl && supported && !clock.calibrationBusy }
    private func components(_ text: String, date: Date, withDate: Bool) -> DateComponents? {
        guard var c = ClockTimeCorrection.parseTime(text) else { return nil }
        if withDate {
            let d = Calendar.current.dateComponents([.year, .month, .day], from: date)
            c.year = d.year; c.month = d.month; c.day = d.day
        }
        return c
    }
    private var target: DateComponents? { components(targetText, date: targetDate, withDate: setTargetDate) }
    private var typedShown: Bool { !shownText.trimmingCharacters(in: .whitespaces).isEmpty }
    private func estimate(at now: Date) -> Date? {
        clock.settings.map { ClockTimeCorrection.estimatedClockTime(macNow: now, offset: $0.timeOffsetMinutes, baseline: clock.timeBaseline) }
    }
    private struct Plan { var delta: Int?; var message: String; var clockMinusMac: Int? }
    /// What Set would do now. A blank "Clock shows now" uses the estimate.
    private func plan(at now: Date) -> Plan {
        let calendar = Calendar.current
        var shown: DateComponents
        let lead = typedShown ? "Using the clock time you typed. " : ""
        if typedShown {
            guard let typed = components(shownText, date: shownDate, withDate: shownDateWrong) else { return Plan(message: "Type the clock's time like 6:27 pm or 18:27.") }
            shown = typed
        } else {
            guard let estimated = estimate(at: now) else { return Plan(message: "Connect the clock first.") }
            shown = calendar.dateComponents([.hour, .minute], from: estimated)
            if shownDateWrong {
                let d = calendar.dateComponents([.year, .month, .day], from: shownDate)
                shown.year = d.year; shown.month = d.month; shown.day = d.day
            }
        }
        var wanted: DateComponents?
        if manual {
            guard !targetText.trimmingCharacters(in: .whitespaces).isEmpty else { return Plan(message: lead + "Type the time to set.") }
            guard let target else { return Plan(message: lead + "Type the time to set like 7:55 pm or 19:55.") }
            wanted = target
        }
        guard let delta = ClockTimeCorrection.minutes(macNow: now, clockShows: shown, target: wanted) else { return Plan(message: lead) }
        // The clock's reading relative to the Mac afterwards; recorded only when the owner typed the reading.
        var after: Int? = nil
        if typedShown {
            var mac = calendar.dateComponents([.hour, .minute], from: now)
            if wanted.map({ $0.year != nil }) == true {
                let d = calendar.dateComponents([.year, .month, .day], from: now)
                mac.year = d.year; mac.month = d.month; mac.day = d.day
            }
            after = wanted.map { ClockTimeCorrection.minutes(macNow: now, clockShows: mac, target: $0) ?? 0 } ?? 0
        }
        let move = delta == 0 ? (manual ? "The clock already shows that time." : "The clock already matches the Mac.") : "Set moves the clock \(ClockTimeCorrection.describe(delta))."
        return Plan(delta: delta, message: lead + move, clockMinusMac: after)
    }
    var body: some View {
        Card(title: "Clock time") {
            TimelineView(.periodic(from: .now, by: 1)) { context in
                KV(rows: [("This Mac", context.date.formatted(date: .abbreviated, time: .standard)),
                          ("Clock firmware", clock.firmwareVersion ?? (clock.isConnected ? "Not reported" : "—")),
                          ("Clock should show", estimate(at: context.date).map { $0.formatted(date: .omitted, time: .shortened) + (typedShown ? "" : " (estimate)") } ?? "—"),
                          ("Stored offset", clock.settings.map { "\($0.timeOffsetMinutes) min" } ?? "—")])
            }
            if clock.isConnected || clock.demoMode, !supported {
                Text(clock.firmwareVersion == nil
                     ? "This clock has not reported its firmware version, so time correction stays off. Reconnect to try again."
                     : "Time correction needs clock firmware 1.6.7 or later. Older firmware ignores the time offset.")
                    .font(.system(size: 13)).foregroundStyle(Theme.warn).padding(10).frame(maxWidth: .infinity, alignment: .leading)
                    .background(RoundedRectangle(cornerRadius: 10).fill(Theme.warnBg))
            }
            Text("Choose the time to show and press Set. The clock cannot report its time, so the app estimates it from the bridge's last sync and every change made here.").note()
            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 10) {
                GridRow {
                    Text("Set the clock to").font(.system(size: 14))
                    Picker("", selection: $manual) { Text("Mac time").tag(false); Text("Manual time").tag(true) }
                        .pickerStyle(.segmented).labelsHidden().frame(width: 220)
                }
                if manual {
                    GridRow {
                        Text("Time").font(.system(size: 14))
                        HStack(spacing: 10) {
                            TextField("e.g. 7:55 pm", text: $targetText).textFieldStyle(.roundedBorder).frame(width: 120)
                            Toggle("Also set the date", isOn: $setTargetDate).toggleStyle(.checkbox).font(.system(size: 13)).fixedSize()
                            if setTargetDate { DatePicker("", selection: $targetDate, displayedComponents: .date).labelsHidden() }
                        }
                    }
                }
            }
            TimelineView(.periodic(from: .now, by: 1)) { context in
                let current = plan(at: context.date)
                HStack {
                    Button("Set") { let p = plan(at: Date()); pendingKnown = p.clockMinusMac; pending = p.delta }.og().disabled(!enabled || (current.delta ?? 0) == 0)
                    Text(current.message).note()
                }
            }
            HStack {
                Text("Fine adjust").font(.system(size: 14))
                Button("−1 hour") { clock.shiftClockTime(byMinutes: -60) }.og(.secondary)
                Button("−1 min") { clock.shiftClockTime(byMinutes: -1) }.og(.secondary)
                Button("+1 min") { clock.shiftClockTime(byMinutes: 1) }.og(.secondary)
                Button("+1 hour") { clock.shiftClockTime(byMinutes: 60) }.og(.secondary)
            }.disabled(!enabled)
            DisclosureGroup("Clock not showing the estimated time?") {
                Text("Type what the clock shows right now. The next Set uses it, and the app remembers the correction for later estimates.").note()
                HStack(spacing: 10) {
                    Text("Clock shows").font(.system(size: 14))
                    TextField("e.g. 6:27 pm", text: $shownText).textFieldStyle(.roundedBorder).frame(width: 120)
                    Toggle("Its date is wrong", isOn: $shownDateWrong).toggleStyle(.checkbox).font(.system(size: 13)).fixedSize()
                    if shownDateWrong { DatePicker("", selection: $shownDate, displayedComponents: .date).labelsHidden() }
                }
            }.foregroundStyle(Theme.tealInk)
            DisclosureGroup("Using the OpenGlance Bridge again?") {
                Text("The clock adds the stored offset to the time it reads from the bridge. Before switching back to the bridge, reset the offset to 0; the bridge then sets the exact time on its next sync.").note()
                Button("Reset offset to 0") { confirmReset = true }.og(.secondary).disabled(!enabled || (clock.settings?.timeOffsetMinutes ?? 0) == 0)
            }.foregroundStyle(Theme.tealInk)
            Text("Whole minutes only; the clock keeps its own seconds. Check the clock face after each change.").note()
        }
        .confirmationDialog(pending.map { "Move the clock \(ClockTimeCorrection.describe($0))?" } ?? "",
                            isPresented: Binding(get: { pending != nil }, set: { if !$0 { pending = nil } })) {
            Button("Move the clock") {
                if let pending { clock.shiftClockTime(byMinutes: pending, clockMinusMac: pendingKnown) }
                pending = nil; shownText = ""   // the clock now shows something else
            }
        } message: { Text("The hands move to the new time.") }
        .confirmationDialog("Reset the stored offset to 0?", isPresented: $confirmReset) {
            Button("Reset offset") {
                if let offset = clock.settings?.timeOffsetMinutes { clock.shiftClockTime(byMinutes: -offset, title: "Time offset reset to 0") }
            }
        } message: { Text("This moves the clock \(ClockTimeCorrection.describe(-(clock.settings?.timeOffsetMinutes ?? 0))) until the bridge sets the time again.") }
    }
}

// MARK: Screenshot mode (demo data only; never uses Bluetooth)

@MainActor
enum Screenshot {
    /// `--screenshot <file.png> [--tab name] [--dark] [--height px]` renders a demo window offscreen and exits.
    static func runIfRequested(_ model: AppModel) -> Bool {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--screenshot"), i + 1 < args.count else { return false }
        let tabName = args.firstIndex(of: "--tab").flatMap { $0 + 1 < args.count ? args[$0 + 1] : nil } ?? "home"
        let tab = tabName.lowercased() == "messages" ? .alerts : Tab.allCases.first { $0.rawValue.lowercased() == tabName.lowercased() } ?? .home
        let height = args.firstIndex(of: "--height").flatMap { $0 + 1 < args.count ? Double(args[$0 + 1]) : nil } ?? 820
        model.clock.setDemo(true)
        var f = HourlyForecast()
        let start = Date(timeIntervalSince1970: (Date().timeIntervalSince1970 / 3600).rounded(.down) * 3600)
        f.hours = (0..<24).map { start.addingTimeInterval(Double($0) * 3600) }
        f.temperature = (0..<24).map { 17 - 6 * sin(Double($0) / 24 * .pi) }
        f.chance = (0..<24).map { [8, 6, 3, 0, 0, 3, 9, 18, 55, 70, 59, 48][$0 % 12] }
        f.rain = (0..<24).map { [0, 0, 0, 0, 0, 0, 0, 0.2, 0.8, 1.2, 0.2, 0][$0 % 12] }
        f.current = 17
        model.weather.loadDemo(f)
        if model.weather.place == nil { model.weather.place = .init(name: "Canberra, Australian Capital Territory, Australia", latitude: -35.28, longitude: 149.13) }
        if args.contains("--seconds") { model.seconds.apply(on: true, color: 18, rings: 3) }
        let view = RootView(model: model, clock: model.clock, alarms: model.alarms, chime: model.chime, weather: model.weather, seconds: model.seconds, tab: tab)
            .frame(width: 1100, height: height)
        let host = NSHostingView(rootView: view)
        host.appearance = NSAppearance(named: args.contains("--dark") ? .darkAqua : .aqua)
        host.frame = NSRect(x: 0, y: 0, width: 1100, height: height)
        let window = NSWindow(contentRect: host.frame, styleMask: [.titled], backing: .buffered, defer: false)
        window.contentView = host
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.8))
        if let rep = host.bitmapImageRepForCachingDisplay(in: host.bounds) {
            host.cacheDisplay(in: host.bounds, to: rep)
            try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: args[i + 1]))
        }
        exit(0)
    }
}
