import SwiftUI
import AppKit
import UniformTypeIdentifiers
import FlasherCore

// The SDK's SwiftUI macro plugin is unavailable with the Command Line Tools.
typealias ViewState<Value> = SwiftUI.State<Value>

enum Theme {
    static let teal = Color(red: 0.09, green: 0.54, blue: 0.50)
    static let tealSoft = Color(red: 0.09, green: 0.54, blue: 0.50).opacity(0.12)
    static let error = Color(nsColor: .systemRed)
}

enum Phase: Equatable { case idle, flashing, verifying, done, failed }

@MainActor
final class FlasherModel: ObservableObject {
    @Published var ports: [String] = []
    @Published var port = ""
    @Published var firmwareURL: URL?
    @Published var firmware: BridgeFirmware?
    @Published var firmwareProblem: String?
    @Published var eraseAll = false
    @Published var phase = Phase.idle
    @Published var progress = 0.0
    @Published var status = "Connect the ESP32-C3 with a USB-C data cable."
    @Published var result: String?
    @Published var log = ""
    private var process: Process?
    private var portTimer: Timer?

    var busy: Bool { phase == .flashing || phase == .verifying }
    var canFlash: Bool { !busy && !port.isEmpty && firmware != nil && supportFilesPresent }
    var appVersion: String { Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev" }

    init() {
        refreshPorts()
        loadDefaultFirmware()
        portTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            Task { @MainActor in if self?.busy == false { self?.refreshPorts() } }
        }
    }

    // MARK: ports
    func refreshPorts() {
        let found = SerialPorts.list()
        if found != ports { ports = found }
        if !ports.contains(port) { port = ports.first ?? "" }
    }

    // MARK: firmware
    private var supportDirectory: URL? { Bundle.main.resourceURL?.appendingPathComponent("Support") }
    private var esptool: URL { Bundle.main.bundleURL.appendingPathComponent("Contents/Helpers/esptool") }
    var supportFilesPresent: Bool {
        guard let dir = supportDirectory else { return false }
        return ["bootloader.bin", "partitions.bin", "boot_app0.bin"].allSatisfy { FileManager.default.fileExists(atPath: dir.appendingPathComponent($0).path) }
            && FileManager.default.isExecutableFile(atPath: esptool.path)
    }

    func loadDefaultFirmware() {
        let app = Bundle.main.bundleURL.deletingLastPathComponent()
        let candidates: [URL?] = [app.appendingPathComponent("../Firmware").standardized, app.appendingPathComponent("Firmware"),
                                  FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first]
        let folders = candidates.compactMap { $0 }
        var best: (version: Double, url: URL)?
        for folder in folders {
            for name in (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? [] {
                guard name.hasPrefix("OpenGlance Bridge "), name.hasSuffix(" OTA.bin"),
                      let v = Double(name.dropFirst("OpenGlance Bridge ".count).dropLast(" OTA.bin".count)) else { continue }
                if v > (best?.version ?? -1) { best = (v, folder.appendingPathComponent(name)) }
            }
        }
        if let best { load(best.url) }
    }

    func chooseFirmware() {
        let panel = NSOpenPanel()
        panel.title = "Choose OpenGlance Bridge firmware"
        panel.allowedContentTypes = [UTType(filenameExtension: "bin") ?? .data]
        panel.allowsMultipleSelection = false
        if let firmwareURL { panel.directoryURL = firmwareURL.deletingLastPathComponent() }
        if panel.runModal() == .OK, let url = panel.url { load(url) }
    }

    func load(_ url: URL) {
        firmwareURL = url
        result = nil
        do {
            firmware = try BridgeFirmware(data: Data(contentsOf: url))
            firmwareProblem = nil
            if phase == .done || phase == .failed { phase = .idle }
        } catch let problem as BridgeFirmware.Problem {
            firmware = nil; firmwareProblem = problem.description
        } catch {
            firmware = nil; firmwareProblem = "The file could not be read: \(error.localizedDescription)"
        }
    }

    // MARK: flashing
    func flash() {
        guard canFlash, let firmware, let firmwareURL, let dir = supportDirectory else { return }
        func size(_ url: URL) -> Int { ((try? FileManager.default.attributesOfItem(atPath: url.path))?[.size] as? Int) ?? 0 }
        let boot = dir.appendingPathComponent("bootloader.bin"), parts = dir.appendingPathComponent("partitions.bin"), ota = dir.appendingPathComponent("boot_app0.bin")
        let plan = FlashPlan(bootloader: (boot.path, size(boot)), partitions: (parts.path, size(parts)), otadata: (ota.path, size(ota)), app: (firmwareURL.path, firmware.size))
        let args = plan.esptoolArguments(port: port, eraseAll: eraseAll)
        let exe = esptool, port = self.port, version = firmware.version
        phase = .flashing; progress = 0; result = nil
        status = eraseAll ? "Erasing and installing \(version)…" : "Installing OpenGlance Bridge \(version)…"
        log = "$ esptool " + args.joined(separator: " ") + "\n"

        Task.detached {
            var pending = ""
            nonisolated(unsafe) var fullLog = ""  // only touched on this task's thread
            let code = Self.runEsptool(exe, args, onStart: { p in Task { @MainActor in self.process = p } }) { chunk in
                fullLog += chunk
                pending += EsptoolOutput.stripANSI(chunk)
                var lines: [String] = []
                while let r = pending.rangeOfCharacter(from: CharacterSet(charactersIn: "\r\n")) {
                    lines.append(String(pending[..<r.lowerBound])); pending = String(pending[r.upperBound...])
                }
                let updates = lines.compactMap { EsptoolOutput.progress(in: $0) }
                let text = lines.filter { !$0.trimmingCharacters(in: .whitespaces).isEmpty && (EsptoolOutput.progress(in: $0) == nil || $0.contains("100.0%")) }
                Task { @MainActor in
                    if let last = updates.last { self.progress = plan.overallProgress(address: last.address, percent: last.percent) }
                    if !text.isEmpty { self.log += text.joined(separator: "\n") + "\n" }
                }
            }
            await MainActor.run {
                self.process = nil
                if code == 0 {
                    self.progress = 1
                    self.phase = .verifying
                    self.status = "Installed and checksum-verified. Waiting for the bridge to start…"
                } else {
                    self.phase = .failed
                    self.status = code == 15 || code == -15 ? "Cancelled. The board needs a complete install before it will run again." : EsptoolOutput.friendlyError(fullLog)
                }
            }
            guard code == 0 else { return }
            let booted = Self.waitForBanner(port: port, timeout: 15)
            await MainActor.run {
                self.phase = .done
                if let booted {
                    self.status = "OpenGlance Bridge \(booted) is installed and running."
                    self.log += "Bridge reported: OpenGlance Bridge \(booted) started\n"
                } else {
                    self.status = "Installed and checksum-verified. The start-up message wasn't seen; that's usually fine."
                }
                self.result = self.eraseAll
                    ? "Settings were erased. On your phone, join the Wi-Fi hotspot “OpenGlance-Setup” to set up Wi-Fi, then pair the clock."
                    : "Existing settings were kept. A brand-new board shows the “OpenGlance-Setup” Wi-Fi hotspot; a configured bridge reconnects to Wi-Fi and the clock at http://openglance.local/."
            }
        }
    }

    func cancel() { process?.terminate() }

    /// Screenshot-only states; no process is started.
    func demo(_ state: String) {
        switch state {
        case "flashing": phase = .flashing; progress = 0.46; status = "Installing OpenGlance Bridge \(firmware?.version ?? "")…"
        case "done": phase = .done; progress = 1; status = "OpenGlance Bridge \(firmware?.version ?? "") is installed and running."
            result = "Existing settings were kept. A brand-new board shows the “OpenGlance-Setup” Wi-Fi hotspot; a configured bridge reconnects to Wi-Fi and the clock at http://openglance.local/."
        case "failed": phase = .failed; progress = 0.03; status = EsptoolOutput.friendlyError("Failed to connect to ESP32-C3: No serial data received.")
        default: break
        }
        log = "Screenshot preview — nothing was flashed.\n"
    }

    /// Runs esptool on a pseudo-terminal so it reports live progress.
    nonisolated static func runEsptool(_ exe: URL, _ args: [String], onStart: @escaping (Process) -> Void, onOutput: @escaping (String) -> Void) -> Int32 {
        var master: Int32 = -1, slave: Int32 = -1
        var size = winsize(ws_row: 40, ws_col: 132, ws_xpixel: 0, ws_ypixel: 0)
        guard openpty(&master, &slave, nil, nil, &size) == 0 else { onOutput("Could not open a terminal for esptool.\n"); return -1 }
        let p = Process()
        p.executableURL = exe
        p.arguments = args
        var env = ProcessInfo.processInfo.environment
        env["NO_COLOR"] = "1"; env["COLUMNS"] = "132"; env["PYTHONUNBUFFERED"] = "1"
        p.environment = env
        let terminal = FileHandle(fileDescriptor: slave, closeOnDealloc: false)
        p.standardOutput = terminal; p.standardError = terminal; p.standardInput = FileHandle.nullDevice
        do { try p.run() } catch {
            close(master); close(slave); onOutput("Could not start esptool: \(error.localizedDescription)\n"); return -1
        }
        close(slave)
        onStart(p)
        var buffer = [UInt8](repeating: 0, count: 4096)
        while true {
            let n = read(master, &buffer, buffer.count)
            if n <= 0 { break }
            onOutput(String(decoding: buffer[0..<n], as: UTF8.self))
        }
        p.waitUntilExit()
        close(master)
        return p.terminationReason == .uncaughtSignal ? -p.terminationStatus : p.terminationStatus
    }

    /// Reads the bridge's USB serial output after reset and returns the version it reports.
    nonisolated static func waitForBanner(port: String, timeout: TimeInterval) -> String? {
        let deadline = Date().addingTimeInterval(timeout)
        var fd: Int32 = -1
        var text = ""
        defer { if fd >= 0 { close(fd) } }
        while Date() < deadline {
            if fd < 0 {
                fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK)
                if fd < 0 { usleep(200_000); continue }
                var t = termios()
                tcgetattr(fd, &t); cfmakeraw(&t); cfsetspeed(&t, speed_t(B115200)); tcsetattr(fd, TCSANOW, &t)
            }
            var buffer = [UInt8](repeating: 0, count: 1024)
            let n = read(fd, &buffer, buffer.count)
            if n > 0 {
                text += String(decoding: buffer[0..<n], as: UTF8.self)
                if let v = BootBanner.version(in: text) { return v }
            } else if n < 0 && errno != EAGAIN {
                close(fd); fd = -1; usleep(200_000)   // port re-enumerating after reset
            } else { usleep(50_000) }
        }
        return nil
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    static var flashing: () -> Bool = { false }

    /// `--screenshot <file.png> [--firmware <bin>] [--state flashing|done|failed]` renders the window
    /// offscreen (no screen-recording permission needed) and exits. Never flashes.
    func applicationDidFinishLaunching(_ notification: Notification) {
        let args = CommandLine.arguments
        guard let i = args.firstIndex(of: "--screenshot"), i + 1 < args.count else { return }
        MainActor.assumeIsolated {
            let model = FlasherModel()
            if let f = args.firstIndex(of: "--firmware"), f + 1 < args.count { model.load(URL(fileURLWithPath: args[f + 1])) }
            if let s = args.firstIndex(of: "--state"), s + 1 < args.count { model.demo(args[s + 1]) }
            let host = NSHostingView(rootView: FlasherView(model: model, expandLog: args.contains("--log")).frame(width: 560, height: 720))
            host.frame = NSRect(x: 0, y: 0, width: 560, height: 720)
            let window = NSWindow(contentRect: host.frame, styleMask: [.titled], backing: .buffered, defer: false)
            window.contentView = host
            host.layoutSubtreeIfNeeded()
            RunLoop.main.run(until: Date().addingTimeInterval(0.6))
            if let rep = host.bitmapImageRepForCachingDisplay(in: host.bounds) {
                host.cacheDisplay(in: host.bounds, to: rep)
                try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: args[i + 1]))
            }
            exit(0)
        }
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard Self.flashing() else { return .terminateNow }
        let alert = NSAlert()
        alert.messageText = "Firmware is being installed"
        alert.informativeText = "Quitting now leaves the board unable to start until it is flashed again."
        alert.addButton(withTitle: "Keep installing")
        alert.addButton(withTitle: "Quit anyway")
        return alert.runModal() == .alertFirstButtonReturn ? .terminateCancel : .terminateNow
    }
}

struct Card<Content: View>: View {
    let number: Int
    let title: String
    @ViewBuilder let content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 8) {
                Text("\(number)").font(.system(size: 12, weight: .bold)).foregroundStyle(.white)
                    .frame(width: 20, height: 20).background(Circle().fill(Theme.teal))
                Text(title).font(.headline)
            }
            content
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 14).fill(Color(nsColor: .controlBackgroundColor)))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(Color.primary.opacity(0.08)))
    }
}

struct FlasherView: View {
    @ObservedObject var model: FlasherModel
    @ViewState private var showLog: Bool

    init(model: FlasherModel, expandLog: Bool = false) {
        self.model = model
        _showLog = ViewState(initialValue: expandLog)
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                header
                Card(number: 1, title: "Board") { boardSection }
                Card(number: 2, title: "Firmware") { firmwareSection }
                Card(number: 3, title: "Install") { installSection }
                DisclosureGroup("Details", isExpanded: $showLog) {
                    ScrollView {
                        Text(model.log.isEmpty ? "No output yet." : model.log)
                            .font(.system(size: 11, design: .monospaced)).textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading).padding(8)
                    }
                    .frame(height: 160)
                    .background(RoundedRectangle(cornerRadius: 8).fill(Color(nsColor: .textBackgroundColor)))
                }
                .font(.subheadline)
            }
            .padding(20)
        }
        .frame(minWidth: 520, idealWidth: 560, minHeight: 600)
        .background(Color(nsColor: .windowBackgroundColor))
        .tint(Theme.teal)
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            guard !model.busy, let provider = providers.first else { return false }
            _ = provider.loadObject(ofClass: URL.self) { url, _ in
                if let url { Task { @MainActor in model.load(url) } }
            }
            return true
        }
    }

    private var header: some View {
        HStack(spacing: 12) {
            if let logo = Bundle.main.url(forResource: "Logo", withExtension: "png").flatMap(NSImage.init(contentsOf:)) {
                Image(nsImage: logo).resizable().interpolation(.high).frame(width: 46, height: 46)
                    .clipShape(RoundedRectangle(cornerRadius: 11, style: .continuous))
            }
            VStack(alignment: .leading, spacing: 2) {
                Text("OpenGlance Flasher").font(.title2.weight(.semibold))
                Text("Install bridge firmware on an ESP32-C3 over USB").font(.subheadline).foregroundStyle(.secondary)
            }
            Spacer()
            Text("v\(model.appVersion)").font(.caption).foregroundStyle(.secondary)
        }
    }

    private var boardSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            if model.ports.isEmpty {
                Label("No USB board found. Plug in the ESP32-C3 with a data cable.", systemImage: "cable.connector.slash")
                    .foregroundStyle(.secondary)
            } else {
                Picker("USB port", selection: $model.port) {
                    ForEach(model.ports, id: \.self) { Text(($0 as NSString).lastPathComponent).tag($0) }
                }
                .disabled(model.busy)
            }
            HStack {
                Button("Refresh") { model.refreshPorts() }.disabled(model.busy)
                Text("The Super Mini usually appears as usbmodem….").font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    private var firmwareSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            if let url = model.firmwareURL {
                Text(url.lastPathComponent).font(.body.weight(.medium)).lineLimit(1).truncationMode(.middle)
            } else {
                Text("No firmware chosen. Choose an “OpenGlance Bridge … OTA.bin” file, or drop it here.").foregroundStyle(.secondary)
            }
            if let fw = model.firmware {
                Label("OpenGlance Bridge \(fw.version) · \(ByteCountFormatter.string(fromByteCount: Int64(fw.size), countStyle: .file)) · ESP32-C3",
                      systemImage: "checkmark.seal.fill").foregroundStyle(Theme.teal).font(.callout)
            }
            if let problem = model.firmwareProblem {
                Label(problem, systemImage: "exclamationmark.triangle.fill").foregroundStyle(Theme.error).font(.callout)
            }
            Button("Choose firmware…") { model.chooseFirmware() }.disabled(model.busy)
        }
    }

    private var installSection: some View {
        VStack(alignment: .leading, spacing: 10) {
            Toggle(isOn: $model.eraseAll) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Erase the whole chip first")
                    Text("Removes the bridge's Wi-Fi settings and clock pairing. Not needed for a new board.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            .disabled(model.busy)
            if !model.supportFilesPresent {
                Label("This app is missing its bundled flashing files. Rebuild it with build-app.sh.", systemImage: "xmark.octagon.fill")
                    .foregroundStyle(Theme.error).font(.callout)
            }
            HStack {
                Button {
                    model.flash()
                } label: {
                    Text(model.busy ? "Installing…" : "Install firmware").frame(minWidth: 140)
                }
                .buttonStyle(.borderedProminent).controlSize(.large).disabled(!model.canFlash)
                if model.phase == .flashing { Button("Cancel") { model.cancel() } }
            }
            if model.phase != .idle {
                ProgressView(value: model.progress).progressViewStyle(.linear)
            }
            Text(model.status)
                .font(.callout)
                .foregroundStyle(model.phase == .failed ? Theme.error : .primary)
                .fixedSize(horizontal: false, vertical: true)
            if let result = model.result {
                Text(result).font(.callout).padding(10).frame(maxWidth: .infinity, alignment: .leading)
                    .background(RoundedRectangle(cornerRadius: 10).fill(Theme.tealSoft))
            }
        }
    }
}

@main
struct OpenGlanceFlasherApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model = FlasherModel()

    var body: some Scene {
        WindowGroup("OpenGlance Flasher") {
            FlasherView(model: model)
                .onAppear { let m = model; AppDelegate.flashing = { MainActor.assumeIsolated { m.busy } } }
        }
        .windowResizability(.contentSize)
        .defaultSize(width: 560, height: 680)
    }
}
