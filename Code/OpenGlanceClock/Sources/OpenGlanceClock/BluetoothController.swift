import Foundation
import Combine
@preconcurrency import CoreBluetooth
import GlanceProtocol

struct NearbyClock: Identifiable {
    let id: UUID
    let name: String
    let signal: Int
    var signalLabel: String { signal > -60 ? "Nearby" : signal > -80 ? "In range" : "Weak signal" }
}
struct ActivityItem: Identifiable {
    let id = UUID()
    let date = Date()
    let title: String
    let detail: String
    let isError: Bool
}

@MainActor
final class BluetoothController: NSObject, ObservableObject, @preconcurrency CBCentralManagerDelegate, @preconcurrency CBPeripheralDelegate {
    @Published var nearby: [NearbyClock] = []
    @Published var isScanning = false
    @Published var isConnecting = false
    @Published var isConnected = false
    @Published var isBusy = false
    @Published var status = "Ready when you are"
    @Published var settings: ClockSettings?
    @Published var battery: Int?
    /// Device Information firmware revision (2A26), e.g. "1.6.7".
    @Published var firmwareVersion: String?
    /// Offset at which the clock matches Mac time (see ClockTimeCorrection.estimatedClockTime).
    @Published var timeBaseline = UserDefaults.standard.integer(forKey: "timeOffsetBaseline") {
        didSet { UserDefaults.standard.set(timeBaseline, forKey: "timeOffsetBaseline") }
    }
    @Published var clockName = "Your Glance Clock"
    @Published var errorMessage: String?
    @Published var events: [ActivityItem] = []
    @Published var demoMode = false
    @Published var timerEnd: Date?
    @Published var needsCalibrationConfirmation = false
    @Published var isCalibrating = false
    @Published var handStatus = "Hand position not read"
    @Published var rememberedName: String?
    @Published var autoConnect = UserDefaults.standard.object(forKey: "autoConnect") as? Bool ?? true {
        didSet { UserDefaults.standard.set(autoConnect, forKey: "autoConnect"); if !autoConnect { retryTimer?.invalidate() } }
    }
    /// Called each time the clock connects and its settings have been read.
    var onClockReady: [() -> Void] = []

    private var central: CBCentralManager?
    private var peripherals: [UUID: CBPeripheral] = [:]
    private var active: CBPeripheral?
    private var control: CBCharacteristic?
    private var stateCharacteristic: CBCharacteristic?
    private var scanRequested = false
    private var scanTimer: Timer?
    private var connectTimer: Timer?
    private var connectWhenFound = false
    private var operationTimer: Timer?
    private var currentOperation: Operation?
    private var operations: [Operation] = []
    private var generation = 0
    private var startupAttempted = false
    private var autoSuspended = false
    private var automaticAttempt = false
    private var retryCount = 0
    private var retryTimer: Timer?
    private struct Operation {
        let characteristic: CBCharacteristic
        let payload: Data?
        let completion: (Result<Data, Error>) -> Void
    }
    private var savedID: UUID? {
        UserDefaults.standard.string(forKey: "clockIdentifier").flatMap(UUID.init(uuidString:))
    }
    var canControl: Bool { demoMode || (isConnected && settings != nil && !isBusy) }
    var hasRememberedClock: Bool { savedID != nil }

    override init() {
        super.init()
        rememberedName = UserDefaults.standard.string(forKey: "clockName")
        log("Welcome to OpenGlance", "Your saved clock reconnects automatically when enabled.")
    }

    func connectSavedOnLaunch() {
        guard !startupAttempted else { return }; startupAttempted = true
        guard autoConnect, hasRememberedClock, !demoMode else { return }
        scan(reconnect: true, automatic: true)
    }
    private func scheduleReconnect() {
        guard autoConnect, !autoSuspended, !demoMode, hasRememberedClock, retryCount < 3 else { return }
        retryCount += 1
        let delay = Double(retryCount * 30)
        retryTimer?.invalidate()
        retryTimer = Timer.scheduledTimer(withTimeInterval: delay, repeats: false) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.autoConnect, !self.autoSuspended, !self.demoMode else { return }
                self.scan(reconnect: true, automatic: true)
            }
        }
    }
    func log(_ title: String, _ detail: String = "", error: Bool = false) {
        events.insert(ActivityItem(title: title, detail: detail, isError: error), at: 0)
        if events.count > 100 { events.removeLast(events.count - 100) }
    }
    private func fail(_ message: String) {
        if !automaticAttempt { errorMessage = message }
        log("Something needs attention", message, error: true)
    }
    func setDemo(_ enabled: Bool) {
        disconnect()
        demoMode = enabled
        nearby = []
        if enabled {
            clockName = "Studio clock"
            settings = try? ClockSettings(data: Data([16, 1, 32, 0, 40, 1, 72, 1, 80, 128, 1, 88, 1, 96, 0]))
            battery = 84
            firmwareVersion = "1.6.7"
            status = "Demo mode · no Bluetooth commands"
            log("Demo mode", "Try the controls. No clock is connected and no Bluetooth commands are sent.")
        } else { settings = nil; battery = nil; firmwareVersion = nil; clockName = "Your Glance Clock"; status = "Ready when you are" }
    }
    func scan(reconnect: Bool = false, automatic: Bool = false) {
        if demoMode { setDemo(false) }
        guard !isConnected, !isConnecting else { return }
        automaticAttempt = automatic
        if !automaticAttempt { autoSuspended = false; retryCount = 0 }
        retryTimer?.invalidate()
        errorMessage = nil
        connectWhenFound = reconnect
        scanRequested = true
        if central == nil {
            status = "Checking Bluetooth access…"
            central = CBCentralManager(delegate: self, queue: .main, options: [CBCentralManagerOptionShowPowerAlertKey: false])
        } else if central?.state == .poweredOn { beginScan() }
        else { handleRadioState() }
    }
    private func beginScan() {
        guard let central, central.state == .poweredOn else { return }
        scanRequested = false
        nearby = []
        if connectWhenFound, let id = savedID,
           let peripheral = central.retrievePeripherals(withIdentifiers: [id]).first {
            peripherals[id] = peripheral
            connectWhenFound = false
            connect(id)
            return
        }
        isScanning = true; status = "Looking for your clock…"
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        log("Searching nearby", "Only Glance clocks appear in the list.")
        scanTimer?.invalidate()
        scanTimer = Timer.scheduledTimer(withTimeInterval: 15, repeats: false) { [weak self] _ in
            Task { @MainActor in self?.stopScan() }
        }
    }
    func stopScan() {
        scanRequested = false; central?.stopScan(); scanTimer?.invalidate(); isScanning = false
        if !isConnecting && !isConnected {
            status = nearby.isEmpty ? "No clock found yet" : "Choose your clock"
        }
    }
    func connect(_ id: UUID) {
        guard !demoMode, let _ = peripherals[id], central?.state == .poweredOn, !isConnecting, !isConnected else { return }
        if !automaticAttempt { autoSuspended = false; retryCount = 0 }
        retryTimer?.invalidate()
        stopScan()
        isConnecting = true; status = "Preparing connection…"; errorMessage = nil
        performConnect(id)
    }
    private func performConnect(_ id: UUID) {
        guard let peripheral = peripherals[id], isConnecting else { return }
        active = peripheral; control = nil; settings = nil; battery = nil; firmwareVersion = nil
        clockName = peripheral.name ?? "Glance Clock"
        status = "Connecting · watch for a macOS PIN prompt"
        peripheral.delegate = self
        central?.connect(peripheral, options: nil)
        log("Connecting to \(clockName)", "If macOS asks, enter the PIN shown on the clock. There is no Glance account to sign in to.")
        connectTimer?.invalidate()
        connectTimer = Timer.scheduledTimer(withTimeInterval: 45, repeats: false) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.isConnecting else { return }
                self.disconnect(userInitiated: false); self.fail("The clock did not connect. Briefly press the clock's button to activate pairing mode, then choose Connect again. Keep it nearby and close the original phone app if connected. Do not hold the button or reset the clock.")
            }
        }
    }
    func disconnect(userInitiated: Bool = true) {
        if userInitiated { autoSuspended = true; automaticAttempt = false; retryTimer?.invalidate() }
        generation += 1
        connectWhenFound = false
        stopScan(); connectTimer?.invalidate(); operationTimer?.invalidate()
        isConnecting = false; isConnected = false; settings = nil; control = nil; battery = nil; firmwareVersion = nil
        needsCalibrationConfirmation = false; isCalibrating = false; stateCharacteristic = nil; handStatus = "Hand position not read"
        let pending = operations + (currentOperation.map { [$0] } ?? [])
        operations = []; currentOperation = nil; isBusy = false
        if let active { central?.cancelPeripheralConnection(active) }
        active = nil
        pending.forEach { $0.completion(.failure(NSError(domain: "OpenGlance", code: 1, userInfo: [NSLocalizedDescriptionKey: "The clock disconnected."]))) }
        if !demoMode { status = "Disconnected" }
        if !userInitiated { scheduleReconnect() }
    }
    func forgetClock() {
        disconnect()
        UserDefaults.standard.removeObject(forKey: "clockIdentifier")
        UserDefaults.standard.removeObject(forKey: "clockName")
        timeBaseline = 0
        rememberedName = nil
        log("Saved clock removed", "This removes the app's remembered selection. macOS Bluetooth pairing is managed by macOS.")
    }
    private func handleRadioState() {
        switch central?.state {
        case .poweredOn: if scanRequested { beginScan() }
        case .unauthorized:
            stopScan(); status = "Bluetooth permission needed"
            fail("Allow OpenGlance Clock in System Settings → Privacy & Security → Bluetooth, then search again.")
        case .poweredOff: disconnect(userInitiated: false); status = "Bluetooth is off"; fail("Turn Bluetooth on in System Settings, then search again.")
        case .unsupported: stopScan(); status = "Bluetooth unavailable"; fail("This Mac does not offer Bluetooth Low Energy.")
        case .resetting: disconnect(userInitiated: false); status = "Bluetooth is restarting…"
        default: status = "Waiting for Bluetooth…"
        }
    }
    func centralManagerDidUpdateState(_ central: CBCentralManager) { handleRadioState() }
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard isScanning else { return }
        let advertisedName = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name
        let services = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        guard ClockDiscovery.isGlance(name: advertisedName, advertisedServices: services.map(\.uuidString)) else { return }
        let name = advertisedName ?? "Glance Clock"
        peripherals[peripheral.identifier] = peripheral
        let item = NearbyClock(id: peripheral.identifier, name: name, signal: RSSI.intValue)
        nearby.removeAll { $0.id == item.id }; nearby.append(item); nearby.sort { $0.signal > $1.signal }
        if connectWhenFound, peripheral.identifier == savedID { connectWhenFound = false; connect(peripheral.identifier) }
    }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard peripheral.identifier == active?.identifier else { central.cancelPeripheralConnection(peripheral); return }
        status = "Reading clock · enter its PIN if prompted"
        peripheral.discoverServices([CBUUID(string: GlanceCommands.serviceUUID), CBUUID(string: "180F"), CBUUID(string: "180A")])
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        guard active?.identifier == peripheral.identifier else { return }
        disconnect(userInitiated: false); fail(error?.localizedDescription ?? "Connection failed. Try again with the clock nearby and awake.")
    }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        guard active?.identifier == peripheral.identifier else { return }
        disconnect(userInitiated: false); log("Clock disconnected", error?.localizedDescription ?? "Automatic reconnection will try your saved clock when enabled.")
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard peripheral.identifier == active?.identifier else { return }
        if let error { disconnect(userInitiated: false); fail(error.localizedDescription); return }
        guard let services = peripheral.services, services.contains(where: { $0.uuid == CBUUID(string: GlanceCommands.serviceUUID) }) else {
            disconnect(userInitiated: false); fail("This device does not expose the Glance Clock service."); return
        }
        for service in services {
            let uuids = service.uuid == CBUUID(string: "180F") ? [CBUUID(string: "2A19")]
                : service.uuid == CBUUID(string: "180A") ? [CBUUID(string: "2A26")]
                : [CBUUID(string: GlanceCommands.controlUUID), CBUUID(string: "5075fc78-1e0e-11e7-93ae-92361f002671")]
            peripheral.discoverCharacteristics(uuids, for: service)
        }
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard peripheral.identifier == active?.identifier else { return }
        if let error {
            if service.uuid == CBUUID(string: GlanceCommands.serviceUUID) { disconnect(userInitiated: false); fail(error.localizedDescription) }
            return
        }
        for characteristic in service.characteristics ?? [] {
            if characteristic.uuid == CBUUID(string: GlanceCommands.controlUUID) {
                guard characteristic.properties.contains(.read), characteristic.properties.contains(.write) else {
                    disconnect(userInitiated: false); fail("This clock's control characteristic does not support the expected read/write access."); return
                }
                control = characteristic; isConnected = true; isConnecting = false; connectTimer?.invalidate()
                refreshSettings()
            } else if characteristic.uuid == CBUUID(string: "2A19"), characteristic.properties.contains(.read) {
                enqueue(characteristic, payload: nil) { [weak self] result in
                    if case .success(let value) = result, let level = value.first, level <= 100 { self?.battery = Int(level) }
                }
            } else if characteristic.uuid == CBUUID(string: "2A26"), characteristic.properties.contains(.read) {
                enqueue(characteristic, payload: nil) { [weak self] result in
                    guard let self, case .success(let value) = result,
                          let text = String(data: value, encoding: .utf8)?.trimmingCharacters(in: .controlCharacters.union(.whitespaces)), !text.isEmpty else { return }
                    self.firmwareVersion = text
                    self.log("Clock firmware \(text)", ClockTimeCorrection.supportsOffset(firmware: text) == true
                        ? "Time correction from this Mac is available (firmware 1.6.7 or later)."
                        : "Time correction from this Mac needs firmware 1.6.7 or later.")
                }
            } else if characteristic.uuid == CBUUID(string: "5075fc78-1e0e-11e7-93ae-92361f002671") {
                stateCharacteristic = characteristic
                if characteristic.properties.contains(.notify) { peripheral.setNotifyValue(true, for: characteristic) }
                if characteristic.properties.contains(.read) {
                    enqueue(characteristic, payload: nil) { [weak self] result in
                        if case .success(let value) = result { self?.updateHandState(value) }
                    }
                }
            }
        }
        if service.uuid == CBUUID(string: GlanceCommands.serviceUUID), control == nil {
            disconnect(userInitiated: false); fail("The Glance control characteristic was not found.")
        }
    }
    func refreshSettings() {
        guard !demoMode, isConnected, let control, !isBusy else { return }
        isBusy = true
        readSettings(control) { [weak self] result in
            guard let self else { return }; self.isBusy = false
            switch result {
            case .success(let settings):
                self.automaticAttempt = false; self.retryCount = 0; self.retryTimer?.invalidate()
                self.settings = settings; self.status = "Connected · local Bluetooth"
                if let active = self.active {
                    UserDefaults.standard.set(active.identifier.uuidString, forKey: "clockIdentifier")
                    UserDefaults.standard.set(self.clockName, forKey: "clockName")
                    self.rememberedName = self.clockName
                }
                self.log("Clock settings read", "Authenticated access is working. Settings are preserved before each change.")
                self.onClockReady.forEach { $0() }
            case .failure(let error): self.fail("Could not read clock settings: \(error.localizedDescription)")
            }
        }
    }
    private func readSettings(_ characteristic: CBCharacteristic, completion: @escaping (Result<ClockSettings, Error>) -> Void) {
        enqueue(characteristic, payload: nil) { result in
            completion(result.flatMap { value in Result { try ClockSettings(data: value) } })
        }
    }
    func setAutomaticBrightness(low: Int, high: Int) {
        do { changeSettings([:], automaticRange: try AutomaticBrightnessRange(low: low, high: high)) }
        catch { fail("Brightness limits must be 0–255, with minimum no higher than maximum.") }
    }
    func changeSettings(_ changes: [Int: UInt64], automaticRange: AutomaticBrightnessRange? = nil) {
        guard canControl else { return }
        if demoMode {
            do { if let settings { self.settings = try ClockSettings(data: Data(settings.patched(changes, automaticRange: automaticRange).dropFirst(4))) }; log("Demo · settings updated") }
            catch { fail(error.localizedDescription) }
            return
        }
        guard let control else { return }; isBusy = true
        readSettings(control) { [weak self] result in
            guard let self else { return }
            do {
                let base = try result.get()
                let command = try base.patched(changes, automaticRange: automaticRange)
                self.enqueue(control, payload: command) { [weak self] write in
                    guard let self else { return }
                    switch write {
                    case .failure(let error): self.isBusy = false; self.fail(error.localizedDescription)
                    case .success:
                        self.readSettings(control) { [weak self] read in
                            guard let self else { return }; self.isBusy = false
                            switch read {
                            case .success(let updated):
                                self.settings = updated
                                if updated.matches(changes, automaticRange: automaticRange) { self.log("Settings confirmed", "The clock returned the new values, including the full automatic brightness range when changed.") }
                                else { self.fail("The write was accepted, but the clock has not returned the requested values. Try Read settings again.") }
                            case .failure(let error): self.settings = nil; self.fail("Settings were sent, but verification failed: \(error.localizedDescription)")
                            }
                        }
                    }
                }
            } catch { self.isBusy = false; self.fail(error.localizedDescription) }
        }
    }
    /// nil while unknown (not connected, or the clock did not report its firmware).
    var timeCorrectionSupported: Bool? { ClockTimeCorrection.supportsOffset(firmware: firmwareVersion) }

    /// Moves the clock's running time by `minutes` via Settings field 18 (see ClockTimeCorrection).
    /// Reads fresh settings first, changes only field 18, then reads back. The read-back proves the
    /// clock stored the value, not that its display moved: the owner checks the clock face.
    /// `clockMinusMac`: the clock's reading relative to Mac time once this change lands, when known
    /// (the owner typed the clock's reading); it re-learns the estimate baseline.
    func shiftClockTime(byMinutes minutes: Int, title: String? = nil, clockMinusMac: Int? = nil) {
        guard canControl, minutes != 0 else { return }
        guard timeCorrectionSupported == true else {
            fail("Time correction needs clock firmware 1.6.7 or later\(firmwareVersion.map { "; this clock reports \($0)" } ?? ", and this clock has not reported its firmware version").")
            return
        }
        guard !calibrationBusy else { fail("Finish hand calibration before changing the time; the hands must not be moving."); return }
        let summary = title ?? "Clock time moved \(ClockTimeCorrection.describe(minutes))"
        if demoMode {
            do {
                if let settings {
                    self.settings = try ClockSettings(data: Data(settings.patched(timeOffsetMinutes: settings.timeOffsetMinutes + minutes).dropFirst(4)))
                    if let clockMinusMac { timeBaseline = ClockTimeCorrection.baseline(offset: settings.timeOffsetMinutes + minutes, clockMinusMac: clockMinusMac) }
                }
                log("Demo · \(summary.lowercased())", "No Bluetooth command was sent.")
            } catch { fail(error.localizedDescription) }
            return
        }
        guard let control else { return }; isBusy = true
        readSettings(control) { [weak self] result in
            guard let self else { return }
            do {
                let base = try result.get()
                let target = base.timeOffsetMinutes + minutes
                let command = try base.patched(timeOffsetMinutes: target)
                self.enqueue(control, payload: command) { [weak self] write in
                    guard let self else { return }
                    switch write {
                    case .failure(let error): self.isBusy = false; self.fail(error.localizedDescription)
                    case .success:
                        self.readSettings(control) { [weak self] read in
                            guard let self else { return }; self.isBusy = false
                            switch read {
                            case .success(let updated):
                                self.settings = updated
                                if updated.timeOffsetMinutes == target {
                                    if let clockMinusMac { self.timeBaseline = ClockTimeCorrection.baseline(offset: target, clockMinusMac: clockMinusMac) }
                                    self.log(summary, "Stored time offset \(base.timeOffsetMinutes) → \(target) minutes. Check the time shown on the clock; the hands move to match.")
                                } else { self.fail("The clock did not return the new time offset. Read the clock face before trying again; no command is retried automatically.") }
                            case .failure(let error): self.settings = nil; self.fail("The time change was sent, but verification failed: \(error.localizedDescription)")
                            }
                        }
                    }
                }
            } catch { self.isBusy = false; self.fail(error.localizedDescription) }
        }
    }
    func sendMessage(_ text: String, animation: Int, sound: Int, color: Int) {
        do { send(try GlanceCommands.notice(text, animation: animation, sound: sound, color: color), title: "Message sent", detail: "\(text) · Bluetooth acknowledged; check the clock display.") }
        catch { fail(error.localizedDescription) }
    }
    func startTimer(minutes: Int) {
        do {
            let packet = try GlanceCommands.timer(seconds: minutes * 60)
            send(packet, title: "Timer sent", detail: "\(minutes) minutes · the countdown here is a Mac estimate.") { [weak self] in self?.timerEnd = Date().addingTimeInterval(Double(minutes * 60)) }
        } catch { fail(error.localizedDescription) }
    }
    func stopTimer() { send(GlanceCommands.stopTimer, title: "Timer stop sent") { [weak self] in self?.timerEnd = nil } }
    func startCalibration() {
        guard !isCalibrating else { fail("Calibration is already moving the hands. Wait for the clock to stop."); return }
        send(GlanceCommands.startCalibration, title: "Calibration started", detail: "Wait for the hands to finish moving. Confirm only when both hands point to 12.") { [weak self] in self?.needsCalibrationConfirmation = true }
    }
    func confirmCalibration() {
        guard !isCalibrating else { fail("Wait until the hands stop before confirming calibration."); return }
        send(GlanceCommands.confirmCalibration, title: "Hand position confirmed") { [weak self] in self?.needsCalibrationConfirmation = false }
    }
    private func updateHandState(_ data: Data) {
        guard let state = try? ClockHandState(data: data) else { handStatus = "Clock hand-state format not recognised"; return }
        isCalibrating = state.homing
        needsCalibrationConfirmation = state.awaitingConfirmation
        let next = state.motorFailure ? "Clock reports a motor failure" : state.homingFailure ? "Clock reports a homing failure" : state.homing ? "Clock is finding the hand reference…" : state.awaitingConfirmation ? "Clock is waiting for hand alignment confirmation" : "Clock reports no calibration in progress"
        if handStatus != next { log("Hand calibration state", next, error: state.motorFailure || state.homingFailure) }
        handStatus = next
    }
    /// Background send (chime, weather, alarms): never shows an alert and never retries.
    func sendQuietly(_ data: Data, title: String, logSuccess: Bool = true, completion: ((Bool) -> Void)? = nil) {
        if demoMode { if logSuccess { log("Demo · \(title.lowercased())", "No Bluetooth command was sent.") }; completion?(true); return }
        guard isConnected, settings != nil, !isBusy, let control else { completion?(false); return }
        isBusy = true
        enqueue(control, payload: data) { [weak self] result in
            guard let self else { return }; self.isBusy = false
            switch result {
            case .success: if logSuccess { self.log(title) }; completion?(true)
            case .failure(let error): self.log(title + " failed", error.localizedDescription, error: true); completion?(false)
            }
        }
    }
    func testSound(_ sound: Int) {
        do { send(try GlanceCommands.notice("Sound test", animation: 0, sound: sound, color: 12), title: "Sound test sent", detail: settings?.muted == true ? "Mute is on, so the clock may stay silent." : Glance.sounds[sound]) }
        catch { fail(error.localizedDescription) }
    }
    func stopAlarm() { send(GlanceCommands.alarmStop, title: "Alarm stop sent") }
    var calibrationBusy: Bool { isCalibrating || needsCalibrationConfirmation }

    private func send(_ data: Data, title: String, detail: String = "", success: (() -> Void)? = nil) {
        guard canControl else { return }
        if demoMode { log("Demo · \(title.lowercased())", "No Bluetooth command was sent."); success?(); return }
        guard let control else { return }; isBusy = true
        enqueue(control, payload: data) { [weak self] result in
            guard let self else { return }; self.isBusy = false
            switch result {
            case .success: self.log(title, detail); success?()
            case .failure(let error): self.fail(error.localizedDescription)
            }
        }
    }
    private func enqueue(_ characteristic: CBCharacteristic, payload: Data?, completion: @escaping (Result<Data, Error>) -> Void) {
        guard let active, active.state == .connected else {
            completion(.failure(NSError(domain: "OpenGlance", code: 2, userInfo: [NSLocalizedDescriptionKey: "Connect your clock first."]))); return
        }
        if let payload, payload.count > active.maximumWriteValueLength(for: .withResponse) {
            completion(.failure(NSError(domain: "OpenGlance", code: 3, userInfo: [NSLocalizedDescriptionKey: "This message is too long for the current Bluetooth connection. Try a shorter message."]))); return
        }
        operations.append(Operation(characteristic: characteristic, payload: payload, completion: completion)); runNext()
    }
    private func runNext() {
        guard currentOperation == nil, !operations.isEmpty, let active, active.state == .connected else { return }
        let operation = operations.removeFirst(); currentOperation = operation
        operationTimer?.invalidate()
        operationTimer = Timer.scheduledTimer(withTimeInterval: 90, repeats: false) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.currentOperation != nil else { return }
                self.disconnect(userInitiated: false); self.fail("Bluetooth did not respond. Complete any macOS PIN prompt, then reconnect. No command is retried automatically.")
            }
        }
        if let data = operation.payload { active.writeValue(data, for: operation.characteristic, type: .withResponse) }
        else { active.readValue(for: operation.characteristic) }
    }
    private func finish(_ result: Result<Data, Error>) {
        guard let operation = currentOperation else { return }
        operationTimer?.invalidate(); currentOperation = nil
        operation.completion(result); runNext()
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if peripheral.identifier == active?.identifier, characteristic.uuid == stateCharacteristic?.uuid,
           error == nil, let value = characteristic.value { updateHandState(value) }
        guard peripheral.identifier == active?.identifier, currentOperation?.characteristic.uuid == characteristic.uuid, currentOperation?.payload == nil else { return }
        if let error { finish(.failure(error)) }
        else if let value = characteristic.value { finish(.success(value)) }
        else { finish(.failure(ProtocolError.malformed)) }
    }
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral.identifier == active?.identifier, currentOperation?.characteristic.uuid == characteristic.uuid, currentOperation?.payload != nil else { return }
        if let error { finish(.failure(error)) } else { finish(.success(Data())) }
    }
}
