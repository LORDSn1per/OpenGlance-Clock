// OpenGlance Bridge — ESP32-C3 Super Mini firmware.
//
// The ESP32 is the only device that talks to the Glance Clock. It:
//   * joins Wi-Fi (setup hotspot on first boot) and keeps internet time (NTP,
//     configurable POSIX time zone with automatic daylight saving);
//   * pairs/bonds with the clock over Bluetooth using the PIN the clock shows;
//   * serves a genuine 16-bit Current Time Service (0x1805), which the clock
//     reads from its connected central — the thing macOS refuses to publish;
//   * hosts the control web page (messages, timer, display, brightness,
//     calibration, firmware update) at http://openglance.local/.
//
// Safety rules carried over from the Mac app: one Bluetooth operation at a
// time with acknowledged writes, no automatic retry of commands, settings are
// read before every change and verified afterwards, unknown protobuf fields and
// brightness bits are preserved, and the clock is never reset, bond-cleared,
// flashed or calibrated without an explicit action on the web page.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <deque>
#include <NimBLEDevice.h>
#include <HTTPClient.h>

#include "version.h"
#include "glance_protocol.h"
#include "weather.h"
#include "web_ui.h"

using glance::Bytes;

// Kept in the image so OTA uploads can be recognised as bridge firmware.
extern "C" __attribute__((used)) const char kFirmwareMarker[] = "#OGBRIDGE-FW-ESP32C3:" OG_FW_VERSION ";";

static const char* kSetupSSID = "OpenGlance-Setup";
static const char* kHostname = "openglance";
static const char* kDefaultTZ = "AEST-10AEDT,M10.1.0,M4.1.0/3";
static const char* kDefaultTZName = "Sydney / Melbourne / Canberra / Hobart";
static const int kLedPin = 8;     // Super Mini blue LED, active low
static const int kBootPin = 9;    // BOOT button, active low

// Guards state shared by the web server, the clock task and BLE callbacks.
static SemaphoreHandle_t stateLock;
struct Lock {
    Lock() { xSemaphoreTake(stateLock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(stateLock); }
};

// ------------------------------------------------------------------ config
struct BridgeAlarm {
    glance::Alarm alarm;
    bool enabled = true;
};

struct Config {
    String ssid, wifiPass, tz, tzName;
    String clockAddr, clockName, clockIdAddr;
    uint8_t clockAddrType = 0, clockIdType = 0;
    bool autoConnect = true, dailySync = true, announce = true;
    // Weather widgets
    bool wxOn = false, wxTemp = true, wxF = false, wxText = false;
    uint8_t wxRainMode = 1;          // 0 off, 1 alternate with temperature, 2 instead of temperature
    int wxCycle = 20;                // seconds per ring when alternating
    int wxRainHours = 6, wxRainChance = 40;
    // Hourly chime
    bool chimeOn = false;
    int chimeSound = 6, quietFrom = 22, quietTo = 7;   // Bells; no chime 22:00–06:59
    // Alarms (the clock cannot report its alarms, so the bridge keeps the list)
    std::vector<BridgeAlarm> alarms;
    bool alarmsPending = false;      // list changed but not yet confirmed by the clock
    String wxLat, wxLon, wxPlace;
    float wxRainMm = 0.3f;
    int wxColdAt = 5, wxHotAt = 35;
    bool paired() const { return clockAddr.length() > 0 || clockName.length() > 0; }
} cfg;
Preferences prefs;

void loadConfig() {
    prefs.begin("og", true);
    cfg.ssid = prefs.getString("ssid", "");
    cfg.wifiPass = prefs.getString("wpass", "");
    cfg.tz = prefs.getString("tz", kDefaultTZ);
    cfg.tzName = prefs.getString("tzname", kDefaultTZName);
    cfg.clockAddr = prefs.getString("caddr", "");
    cfg.clockAddrType = prefs.getUChar("ctype", 0);
    cfg.clockIdAddr = prefs.getString("cid", "");
    cfg.clockIdType = prefs.getUChar("cidtype", 0);
    cfg.clockName = prefs.getString("cname", "");
    cfg.autoConnect = prefs.getBool("auto", true);
    cfg.dailySync = prefs.getBool("daily", true);
    cfg.announce = prefs.getBool("announce", true);
    cfg.wxOn = prefs.getBool("wxon", false);
    cfg.wxTemp = prefs.getBool("wxtemp", true);
    cfg.wxRainMode = prefs.getUChar("wxrainmode", prefs.getBool("wxrain", true) ? 1 : 0);
    cfg.wxText = prefs.getBool("wxtext", false);
    cfg.alarms.clear();
    {
        // One line per alarm: hour,minute,days,sound,enabled,text
        String all = prefs.getString("alarms", "");
        int start = 0;
        while (start < (int)all.length() && cfg.alarms.size() < size_t(glance::kMaxAlarms)) {
            int end = all.indexOf('\n', start);
            if (end < 0) end = all.length();
            String line = all.substring(start, end);
            start = end + 1;
            int f[5], pos = 0, ok = 0;
            for (int k = 0; k < 5; k++) {
                int comma = line.indexOf(',', pos);
                if (comma < 0) break;
                f[k] = line.substring(pos, comma).toInt();
                pos = comma + 1;
                ok++;
            }
            if (ok != 5) continue;
            BridgeAlarm a;
            a.alarm.hour = f[0]; a.alarm.minute = f[1]; a.alarm.days = f[2]; a.alarm.sound = f[3];
            a.enabled = f[4] != 0;
            a.alarm.text = line.substring(pos).c_str();
            if (glance::validAlarm(a.alarm)) cfg.alarms.push_back(a);
        }
    }
    cfg.alarmsPending = prefs.getBool("alarmspend", false);
    cfg.chimeOn = prefs.getBool("chime", false);
    cfg.chimeSound = prefs.getInt("chimesnd", 6);
    cfg.quietFrom = prefs.getInt("quietfrom", 22);
    cfg.quietTo = prefs.getInt("quietto", 7);
    cfg.wxCycle = prefs.getInt("wxcycle", 20);
    cfg.wxRainHours = prefs.getInt("wxrainh", 6);
    cfg.wxRainChance = prefs.getInt("wxrainpct", 40);
    cfg.wxF = prefs.getBool("wxf", false);
    cfg.wxLat = prefs.getString("wxlat", "");
    cfg.wxLon = prefs.getString("wxlon", "");
    cfg.wxPlace = prefs.getString("wxplace", "");
    cfg.wxRainMm = prefs.getFloat("wxmm", 0.3f);
    cfg.wxColdAt = prefs.getInt("wxcoldat", 5);
    cfg.wxHotAt = prefs.getInt("wxhotat", 35);
    prefs.end();
}
void saveConfig() {
    prefs.begin("og", false);
    prefs.putString("ssid", cfg.ssid);
    prefs.putString("wpass", cfg.wifiPass);
    prefs.remove("webpass");  // bridge passwords were removed in 1.02
    prefs.putString("tz", cfg.tz);
    prefs.putString("tzname", cfg.tzName);
    prefs.putString("caddr", cfg.clockAddr);
    prefs.putUChar("ctype", cfg.clockAddrType);
    prefs.putString("cid", cfg.clockIdAddr);
    prefs.putUChar("cidtype", cfg.clockIdType);
    prefs.putString("cname", cfg.clockName);
    prefs.putBool("auto", cfg.autoConnect);
    prefs.putBool("daily", cfg.dailySync);
    prefs.putBool("announce", cfg.announce);
    prefs.putBool("wxon", cfg.wxOn);
    prefs.putBool("wxtemp", cfg.wxTemp);
    prefs.putUChar("wxrainmode", cfg.wxRainMode);
    prefs.putBool("wxtext", cfg.wxText);
    {
        Lock l;  // the web page and the clock task both change the list
        String all;
        for (const auto& a : cfg.alarms) {
            all += String(a.alarm.hour) + "," + a.alarm.minute + "," + a.alarm.days + "," + a.alarm.sound + "," + (a.enabled ? 1 : 0) + "," + a.alarm.text.c_str() + "\n";
        }
        prefs.putString("alarms", all);
    }
    prefs.putBool("alarmspend", cfg.alarmsPending);
    prefs.putBool("chime", cfg.chimeOn);
    prefs.putInt("chimesnd", cfg.chimeSound);
    prefs.putInt("quietfrom", cfg.quietFrom);
    prefs.putInt("quietto", cfg.quietTo);
    prefs.putInt("wxcycle", cfg.wxCycle);
    prefs.putInt("wxrainh", cfg.wxRainHours);
    prefs.putInt("wxrainpct", cfg.wxRainChance);
    prefs.putBool("wxf", cfg.wxF);
    prefs.putString("wxlat", cfg.wxLat);
    prefs.putString("wxlon", cfg.wxLon);
    prefs.putString("wxplace", cfg.wxPlace);
    prefs.putFloat("wxmm", cfg.wxRainMm);
    prefs.putInt("wxcoldat", cfg.wxColdAt);
    prefs.putInt("wxhotat", cfg.wxHotAt);
    prefs.end();
}

// ------------------------------------------------------------ shared state

enum class ClockState { NotPaired, Waiting, Scanning, Connecting, Securing, AwaitingPin, AwaitingConfirm, Connected, Error };
static const char* stateName(ClockState s) {
    switch (s) {
        case ClockState::NotPaired: return "not_paired";
        case ClockState::Waiting: return "waiting";
        case ClockState::Scanning: return "scanning";
        case ClockState::Connecting: return "connecting";
        case ClockState::Securing: return "securing";
        case ClockState::AwaitingPin: return "awaiting_pin";
        case ClockState::AwaitingConfirm: return "awaiting_confirm";
        case ClockState::Connected: return "connected";
        default: return "error";
    }
}

struct ScanItem { String name, addr; uint8_t type; int rssi; };
struct LogItem { time_t when; uint32_t uptime; bool error; String text; };

struct State {
    ClockState clock = ClockState::NotPaired;
    String clockText = "No clock paired yet";
    String clockName;
    int battery = -1;
    bool busy = false;
    glance::Settings settings;
    glance::HandState hand;
    bool calibrationRequested = false;
    time_t timerEnd = 0;
    time_t lastTimeRead = 0;
    bool lastReadValid = false;
    String lastSupplied;
    bool timeSubscribed = false;
    uint32_t confirmNumber = 0;
    bool scanning = false;
    std::vector<ScanItem> scan;
    std::deque<LogItem> log;
    bool ntpSynced = false;
    time_t lastNtp = 0;
    bool manualTime = false;
    uint8_t pendingReasonBits = 0;
    // Weather
    weather::Forecast forecast;
    time_t wxFetched = 0, wxSent = 0;
    String wxError;
    int wxShowing = 0;            // 0 nothing, 1 temperature, 2 rain
    bool wxRainDue = false;
} st;

static volatile bool linkDown = false;
static volatile bool authDone = false;
static volatile bool authOk = false;
static volatile bool userSuspended = false;
static volatile uint32_t timeReadCount = 0;
static volatile uint32_t restartAt = 0;
static volatile bool syncAfterNtp = false;
// Announce on the first connection after boot, after pairing and after a manual
// Connect — not on background reconnects (sync, daily 03:05, DST, drop-outs).
static volatile bool announcePending = true;
static volatile bool wxFetchRequested = true;   // fetch soon after boot
static volatile bool wxSendRequested = false;
static volatile bool wxSendUser = false;        // user-triggered: also refresh the display
static bool wxSlotUnknown = true;               // after boot we do not know what slot 1 holds
static bool wxOldSlotCleanup = true;            // remove the second scene used by 1.06/1.07
static bool wxPhaseRain = false;                // alternating: rain is the current ring
static Bytes wxLastCommand;                     // skip identical re-sends

void logEvent(const String& text, bool error = false) {
    Serial.printf("[%s] %s\n", error ? "ERR" : "log", text.c_str());
    Lock l;
    st.log.push_front({time(nullptr), millis() / 1000, error, text});
    while (st.log.size() > 40) st.log.pop_back();
}
void setClock(ClockState s, const String& text) {
    {
        Lock l;
        st.clock = s;
        st.clockText = text;
    }
    Serial.printf("[clock] %s: %s\n", stateName(s), text.c_str());
}
bool timeIsValid() {
    Lock l;
    if (!(st.manualTime || st.ntpSynced)) return false;
    return time(nullptr) > 1735689600;  // after 2025-01-01
}
String formatLocal(time_t t, const char* fmt = "%Y-%m-%d %H:%M:%S") {
    if (t < 1735689600) return "";
    struct tm l;
    localtime_r(&t, &l);
    char buf[40];
    strftime(buf, sizeof buf, fmt, &l);
    return buf;
}

// ------------------------------------------------------------ BLE objects
static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* control = nullptr;
static NimBLERemoteCharacteristic* batteryChr = nullptr;
static NimBLECharacteristic* ctsCurrent = nullptr;
static NimBLECharacteristic* ctsLocal = nullptr;

bool linkUp() { return client && client->isConnected() && control; }

class ClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient*, int reason) override {
        linkDown = true;
        logEvent("Clock link closed (reason " + String(reason) + ")");
    }
    void onPassKeyEntry(NimBLEConnInfo&) override {
        setClock(ClockState::AwaitingPin, "Enter the PIN shown on the clock");
        logEvent("Clock requested its PIN");
    }
    void onConfirmPasskey(NimBLEConnInfo&, uint32_t pin) override {
        { Lock l; st.confirmNumber = pin; }
        setClock(ClockState::AwaitingConfirm, "Confirm the number matches the clock");
        logEvent("Clock requested number confirmation: " + String(pin));
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        authOk = info.isEncrypted();
        if (authOk) {
            cfg.clockIdAddr = info.getIdAddress().toString().c_str();
            cfg.clockIdType = info.getIdAddress().getType();
        }
        authDone = true;
    }
} clientCallbacks;

class TimeCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        bool valid = timeIsValid();
        if (c == ctsCurrent) {
            uint8_t reason;
            {
                Lock l;
                reason = (st.manualTime ? 1 : 2) | st.pendingReasonBits;
            }
            Bytes v = glance::currentTimeValue(tv.tv_sec, valid, uint8_t(tv.tv_usec * 256LL / 1000000), reason);
            c->setValue(v.data(), v.size());
            {
                Lock l;
                st.lastTimeRead = tv.tv_sec;
                st.lastReadValid = valid;
                st.lastSupplied = valid ? formatLocal(tv.tv_sec) + (st.manualTime ? " (manual)" : " (internet)") : "unknown — no internet time yet";
                st.pendingReasonBits = 0;
            }
            timeReadCount = timeReadCount + 1;
            logEvent(valid ? "Clock read the time: " + formatLocal(tv.tv_sec) : "Clock read the time before internet time was available", !valid);
        } else {
            Bytes v = glance::localTimeInformation(tv.tv_sec);
            c->setValue(v.data(), v.size());
        }
    }
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t value) override {
        { Lock l; st.timeSubscribed = value & 1; }
        logEvent(value & 1 ? "Clock subscribed to time updates" : "Clock unsubscribed from time updates");
    }
} timeCallbacks;

void onHandState(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    glance::HandState h = glance::decodeHandState(data, len);
    if (!h.valid) return;
    bool changed;
    {
        Lock l;
        changed = h.bits != st.hand.bits || !st.hand.valid;
        st.hand = h;
        if (!h.homing && !h.awaitingConfirmation) st.calibrationRequested = false;
    }
    if (changed) {
        String text = h.motorFailure ? "motor failure" : h.homingFailure ? "homing failure" : h.homing ? "hands moving to reference"
                    : h.awaitingConfirmation ? "waiting for alignment confirmation" : "no calibration in progress";
        logEvent("Hand state: " + text, h.motorFailure || h.homingFailure);
    }
}

void setupBluetooth() {
    NimBLEDevice::init("OpenGlance Bridge");
    NimBLEDevice::setPower(9);
    NimBLEDevice::setSecurityAuth(true, true, true);  // bond, MITM, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);  // the clock displays its PIN
    NimBLEDevice::setMTU(185);

    // The clock is the GATT client here: on connection it reads Current Time
    // from whichever central connected to it.
    NimBLEServer* server = NimBLEDevice::createServer();
    NimBLEService* cts = server->createService("1805");
    ctsCurrent = cts->createCharacteristic("2A2B", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    ctsLocal = cts->createCharacteristic("2A0F", NIMBLE_PROPERTY::READ);
    ctsCurrent->setCallbacks(&timeCallbacks);
    ctsLocal->setCallbacks(&timeCallbacks);
    Bytes unknown(10, 0);
    ctsCurrent->setValue(unknown.data(), unknown.size());
    uint8_t lti[2] = {0, 0};
    ctsLocal->setValue(lti, 2);
    server->start();

    client = NimBLEDevice::createClient();
    client->setClientCallbacks(&clientCallbacks, false);
    client->setConnectTimeout(12000);
    client->setSelfDelete(false, false);
}

// ---------------------------------------------------------- clock commands
enum CmdType : uint8_t {
    CMD_SCAN, CMD_PAIR, CMD_CONNECT, CMD_DISCONNECT, CMD_FORGET, CMD_SYNC, CMD_MESSAGE,
    CMD_TIMER, CMD_TIMER_STOP, CMD_SETTING, CMD_RANGE, CMD_CAL_START, CMD_CAL_CONFIRM, CMD_REFRESH,
    CMD_ALARMS_SYNC, CMD_ALARM_STOP
};
void sendWeather(bool userTriggered);
struct Command {
    CmdType type;
    int32_t a, b, c;
    char text[72];
};
static QueueHandle_t commandQueue;

bool enqueue(CmdType type, int32_t a = 0, int32_t b = 0, int32_t c = 0, const String& text = "") {
    Command cmd{type, a, b, c, {0}};
    strlcpy(cmd.text, text.c_str(), sizeof cmd.text);
    return xQueueSend(commandQueue, &cmd, 0) == pdTRUE;
}

bool writeControl(const Bytes& data, const String& what) {
    if (!linkUp()) { logEvent(what + ": the clock is not connected", true); return false; }
    size_t limit = client->getMTU() > 3 ? client->getMTU() - 3 : 20;
    if (data.size() > limit) { logEvent(what + ": too long for this Bluetooth link (" + String(data.size()) + " > " + String(limit) + " bytes)", true); return false; }
    if (!control->writeValue(data.data(), data.size(), true)) {
        // Not retried automatically: the clock may already have acted on it.
        logEvent(what + ": the clock did not acknowledge (error " + String(client->getLastError()) + ")", true);
        return false;
    }
    return true;
}

bool readSettings(glance::Settings& out) {
    if (!linkUp()) return false;
    NimBLEAttValue v = control->readValue();
    if (v.size() == 0) { logEvent("Could not read clock settings (error " + String(client->getLastError()) + ")", true); return false; }
    if (!glance::parseSettings(Bytes(v.data(), v.data() + v.size()), out)) { logEvent("The clock's reply was not recognised as settings; nothing was changed", true); return false; }
    { Lock l; st.settings = out; }
    return true;
}

void readBattery() {
    if (!batteryChr || !client->isConnected()) return;
    NimBLEAttValue v = batteryChr->readValue();
    if (v.size() >= 1 && v.data()[0] <= 100) { Lock l; st.battery = v.data()[0]; }
}

void resetLinkObjects() {
    control = nullptr;
    batteryChr = nullptr;
    Lock l;
    st.battery = -1;
    st.settings = glance::Settings();
    st.hand = glance::HandState();
    st.timeSubscribed = false;
    st.calibrationRequested = false;
}

static uint32_t timeReadDeadline = 0;
static uint32_t timeReadBaseline = 0;

// Connects, secures (PIN on first pairing), discovers and reads settings.
bool openLink(const NimBLEAddress& address, const String& name, bool pairing) {
    resetLinkObjects();
    timeReadBaseline = timeReadCount;  // the clock may read the time while we secure the link
    setClock(ClockState::Connecting, "Connecting to " + name + "…");
    if (!client->connect(address, true, false, true)) {
        setClock(pairing ? ClockState::Error : ClockState::Waiting,
                 pairing ? "Could not connect. Short-press the clock button, then press Pair again." : "Clock found but the connection failed; retrying later");
        logEvent("Connection to " + name + " failed (error " + String(client->getLastError()) + ")", true);
        return false;
    }
    setClock(ClockState::Securing, pairing ? "Pairing — watch the clock for a PIN" : "Securing the connection…");
    authDone = false;
    authOk = false;
    client->secureConnection(true);
    uint32_t limit = millis() + (pairing ? 120000 : 30000);
    while (!authDone && client->isConnected() && millis() < limit) delay(100);
    { Lock l; st.confirmNumber = 0; }
    if (!authDone || !authOk || !client->isConnected()) {
        client->disconnect();
        setClock(ClockState::Error, pairing ? "Pairing did not complete. Short-press the clock button, press Pair again and enter the PIN shown on the clock."
                                            : "The clock refused the saved pairing. Use Pair to pair again.");
        logEvent(pairing ? "Pairing failed or timed out" : "Secure reconnection failed", true);
        return false;
    }
    NimBLERemoteService* svc = client->getService(glance::kServiceUUID);
    NimBLERemoteCharacteristic* ctl = svc ? svc->getCharacteristic(glance::kControlUUID) : nullptr;
    if (!ctl || !ctl->canRead() || !ctl->canWrite()) {
        client->disconnect();
        setClock(ClockState::Error, "This device does not expose the Glance control characteristic");
        logEvent("Glance service/control characteristic missing", true);
        return false;
    }
    control = ctl;
    NimBLERemoteCharacteristic* stateChr = svc->getCharacteristic(glance::kStateUUID);
    if (stateChr) {
        if (stateChr->canNotify()) stateChr->subscribe(true, onHandState, true);
        if (stateChr->canRead()) {
            NimBLEAttValue v = stateChr->readValue();
            if (v.size()) onHandState(stateChr, const_cast<uint8_t*>(v.data()), v.size(), false);
        }
    }
    if (NimBLERemoteService* bat = client->getService("180F")) {
        NimBLERemoteCharacteristic* level = bat->getCharacteristic("2A19");
        if (level && level->canRead()) batteryChr = level;
    }
    glance::Settings s;
    if (!readSettings(s)) {
        client->disconnect();
        control = nullptr;
        setClock(ClockState::Error, "Connected, but the clock settings could not be read");
        return false;
    }
    readBattery();
    cfg.clockAddr = address.toString().c_str();
    cfg.clockAddrType = address.getType();
    cfg.clockName = name;
    saveConfig();
    { Lock l; st.clockName = name; }
    setClock(ClockState::Connected, "Connected");
    logEvent(pairing ? "Paired with " + name + " and read its settings" : "Connected to " + name);
    timeReadDeadline = millis() + 30000;
    wxLastCommand.clear();  // the clock may have lost scenes while disconnected
    if (cfg.alarmsPending) enqueue(CMD_ALARMS_SYNC);  // a change made while the clock was away
    wxSendRequested = true;
    if (announcePending) {
        announcePending = false;
        bool handsMoving;
        { Lock l; handsMoving = st.hand.homing || st.hand.awaitingConfirmation; }
        Bytes notice;
        if (cfg.announce && !handsMoving && glance::notice("OpenGlance " OG_FW_VERSION " connected", 10, 0, 23, notice)) {  // Fire, Lime, never a sound
            delay(500);
            if (writeControl(notice, "Connection message")) logEvent("Connection message shown on the clock");
        }
    }
    return true;
}

// Finds the saved clock by address or by its advertised name (the clock may
// advertise a rotating private address).
bool connectSaved() {
    setClock(ClockState::Scanning, "Looking for " + (cfg.clockName.length() ? cfg.clockName : String("your clock")) + "…");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    NimBLEScanResults results = scan->getResults(6000, false);
    const NimBLEAdvertisedDevice* match = nullptr;
    for (int i = 0; i < results.getCount() && !match; i++) {
        const NimBLEAdvertisedDevice* d = results.getDevice(i);
        String addr = d->getAddress().toString().c_str();
        bool nameMatch = cfg.clockName.length() && d->haveName() && String(d->getName().c_str()) == cfg.clockName;
        if (addr.equalsIgnoreCase(cfg.clockAddr) || addr.equalsIgnoreCase(cfg.clockIdAddr) || nameMatch) match = d;
    }
    if (!match) {
        scan->clearResults();
        setClock(ClockState::Waiting, "Clock not found. It may be out of range or connected to another device (quit the old Mac app).");
        return false;
    }
    NimBLEAddress address = match->getAddress();
    String name = match->haveName() ? String(match->getName().c_str()) : cfg.clockName;
    scan->clearResults();
    return openLink(address, name, false);
}

void doScan() {
    { Lock l; st.scanning = true; st.scan.clear(); }
    setClock(ClockState::Scanning, "Searching for Glance clocks…");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->getResults(8000, false);
    std::vector<ScanItem> items;
    NimBLEUUID svc(glance::kServiceUUID);
    for (int i = 0; i < results.getCount(); i++) {
        const NimBLEAdvertisedDevice* d = results.getDevice(i);
        std::string name = d->haveName() ? d->getName() : "";
        if (!glance::isGlance(name, d->isAdvertisingService(svc))) continue;
        items.push_back({name.empty() ? String("Glance clock") : String(name.c_str()), d->getAddress().toString().c_str(), d->getAddress().getType(), d->getRSSI()});
    }
    scan->clearResults();
    std::sort(items.begin(), items.end(), [](const ScanItem& x, const ScanItem& y) { return x.rssi > y.rssi; });
    { Lock l; st.scan = items; st.scanning = false; }
    logEvent("Search finished: " + String(items.size()) + " clock(s) found");
    if (!linkUp()) setClock(cfg.paired() ? ClockState::Waiting : ClockState::NotPaired, items.empty() ? "No clock found. Keep it nearby and quit the old Mac app." : "Choose your clock and press Pair");
}

bool timerRunning() { Lock l; return st.timerEnd > time(nullptr); }

// Keeps exactly one weather scene (slot 1) on the clock so the clock never
// cycles scenes by itself. Shows temperature, or rain when rain is coming:
// alternating on the owner's interval, or instead of temperature.
void sendWeather(bool userTriggered) {
    if (!linkUp() || !timeIsValid()) return;
    weather::Forecast f;
    { Lock l; if (st.hand.homing || st.hand.awaitingConfirmation) return; f = st.forecast; }
    time_t now = time(nullptr);
    long offset = glance::utcOffsetSeconds(now);
    weather::Scene temp, rain;
    weather::TemperatureOptions to;
    to.fahrenheit = cfg.wxF; to.coldAt = cfg.wxColdAt; to.hotAt = cfg.wxHotAt;
    bool tempOk = cfg.wxTemp && weather::temperatureScene(f, now, offset, to, temp);
    weather::RainOptions ro;
    ro.thresholdMm = cfg.wxRainMm; ro.minChance = cfg.wxRainChance; ro.lookaheadHours = cfg.wxRainHours;
    bool rainDue = cfg.wxRainMode != 0 && weather::rainScene(f, now, offset, ro, rain);
    if (!rainDue) wxPhaseRain = false;
    int show = 0;
    if (cfg.wxOn) {
        if (rainDue && (cfg.wxRainMode == 2 || !tempOk)) show = 2;
        else if (rainDue && cfg.wxRainMode == 1) show = wxPhaseRain ? 2 : 1;
        else if (tempOk) show = 1;
    }
    if (cfg.wxOn && wxOldSlotCleanup) {
        if (writeControl(weather::deleteSceneCommand(weather::kRainSlot), "Remove the old second weather scene")) wxOldSlotCleanup = false;
    }
    int before;
    bool dueBefore;
    { Lock l; before = st.wxShowing; dueBefore = st.wxRainDue; }
    Bytes cmd;
    if (show) cmd = weather::forecastCommand(weather::kWeatherSlot, show == 2 ? rain : temp, !cfg.wxText);
    else if (before != 0 || (cfg.wxOn && wxSlotUnknown)) cmd = weather::deleteSceneCommand(weather::kWeatherSlot);
    bool sent = false;
    if (!cmd.empty() && (cmd != wxLastCommand || userTriggered)) {
        if (writeControl(cmd, show == 2 ? "Rain forecast" : show == 1 ? "Temperature forecast" : "Remove weather")) {
            wxLastCommand = cmd;
            wxSlotUnknown = false;
            sent = true;
            if (userTriggered) {
                const Bytes refresh{35};  // UpdateAndRefresh, only for the owner's own changes
                writeControl(refresh, "Refresh display");
            }
        } else return;
    }
    {
        Lock l;
        st.wxShowing = show;
        st.wxRainDue = rainDue;
        if (sent) st.wxSent = now;
    }
    // Log changes of state, not every alternation.
    if (userTriggered || rainDue != dueBefore || (show == 0) != (before == 0)) {
        String what = show == 0 ? "off" : !rainDue ? "temperature" :
                      cfg.wxRainMode == 2 || !tempOk ? "rain (rain is coming)" : "temperature and rain alternating every " + String(cfg.wxCycle) + " s";
        logEvent("Weather on clock: " + what);
    }
}
bool handsBusy() { Lock l; return st.hand.homing || st.hand.awaitingConfirmation || st.calibrationRequested; }

// Reconnects so the clock re-reads Current Time on connection.
void doSync(const String& why) {
    if (!linkUp()) { logEvent("Time sync skipped: the clock is not connected", true); return; }
    if (handsBusy()) { logEvent("Finish hand calibration before syncing time", true); return; }
    if (timerRunning()) { logEvent("Stop the running timer before syncing time", true); return; }
    if (!timeIsValid()) { logEvent("No valid time yet (internet time not received)", true); return; }
    logEvent("Syncing time (" + why + ")");
    bool subscribed;
    { Lock l; subscribed = st.timeSubscribed; }
    if (subscribed && ctsCurrent) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        uint8_t reason;
        { Lock l; reason = (st.manualTime ? 1 : 2) | st.pendingReasonBits; }
        Bytes v = glance::currentTimeValue(tv.tv_sec, true, uint8_t(tv.tv_usec * 256LL / 1000000), reason);
        ctsCurrent->setValue(v.data(), v.size());
        ctsCurrent->notify();
    }
    client->disconnect();
    uint32_t until = millis() + 5000;
    while (client->isConnected() && millis() < until) delay(50);
    linkDown = false;
    resetLinkObjects();
    delay(1500);
    connectSaved();
}

void changeSettings(const std::map<uint32_t, uint64_t>& changes, const glance::BrightnessRange& range, const String& what) {
    glance::Settings base;
    if (!readSettings(base)) return;
    Bytes command;
    if (!glance::patchSettings(base, changes, range, command)) { logEvent(what + ": invalid value; nothing was sent", true); return; }
    if (!writeControl(command, what)) return;
    glance::Settings updated;
    if (!readSettings(updated)) { logEvent(what + ": sent, but verification read failed", true); return; }
    if (glance::settingsMatch(updated, changes, range)) logEvent(what + ": confirmed by the clock");
    else logEvent(what + ": accepted, but the clock did not return the requested values", true);
}

void handleCommand(const Command& cmd) {
    { Lock l; st.busy = true; }
    switch (cmd.type) {
        case CMD_SCAN: doScan(); break;
        case CMD_PAIR: {
            ScanItem item;
            {
                Lock l;
                if (cmd.a < 0 || cmd.a >= int(st.scan.size())) { item.addr = ""; } else item = st.scan[cmd.a];
            }
            if (item.addr.isEmpty()) { logEvent("Search again, then choose a clock", true); break; }
            if (client->isConnected()) { client->disconnect(); delay(1500); linkDown = false; }
            userSuspended = false;
            if (cfg.clockIdAddr.length() && !String(item.addr).equalsIgnoreCase(cfg.clockIdAddr)) {
                cfg.clockIdAddr = "";  // pairing a different clock
            }
            logEvent("Pairing with " + item.name + ". Watch the clock for its PIN.");
            announcePending = true;
            openLink(NimBLEAddress(std::string(item.addr.c_str()), item.type), item.name, true);
            break;
        }
        case CMD_CONNECT:
            userSuspended = false;
            announcePending = true;
            if (!linkUp() && cfg.paired()) connectSaved();
            break;
        case CMD_DISCONNECT:
            userSuspended = true;
            if (client->isConnected()) client->disconnect();
            setClock(ClockState::Waiting, "Disconnected (automatic connection paused until you press Connect)");
            break;
        case CMD_FORGET: {
            userSuspended = true;
            if (client->isConnected()) { client->disconnect(); delay(1000); }
            // Removes only this bridge's own stored keys; never sends the clock's bond-clear command.
            if (cfg.clockIdAddr.length()) NimBLEDevice::deleteBond(NimBLEAddress(std::string(cfg.clockIdAddr.c_str()), cfg.clockIdType));
            if (cfg.clockAddr.length()) NimBLEDevice::deleteBond(NimBLEAddress(std::string(cfg.clockAddr.c_str()), cfg.clockAddrType));
            cfg.clockAddr = cfg.clockName = cfg.clockIdAddr = "";
            saveConfig();
            { Lock l; st.clockName = ""; }
            resetLinkObjects();
            setClock(ClockState::NotPaired, "No clock paired yet");
            logEvent("Saved clock forgotten on the bridge");
            userSuspended = false;
            break;
        }
        case CMD_SYNC: doSync("requested"); break;
        case CMD_MESSAGE: {
            Bytes data;
            if (!glance::notice(cmd.text, cmd.a, cmd.b, cmd.c, data)) { logEvent("Message rejected: use 1–64 plain characters", true); break; }
            if (writeControl(data, "Message")) logEvent("Message sent: " + String(cmd.text));
            break;
        }
        case CMD_TIMER: {
            Bytes data;
            if (!glance::timer(cmd.a * 60, data)) { logEvent("Timer must be 1–720 minutes", true); break; }
            if (writeControl(data, "Timer")) {
                { Lock l; st.timerEnd = time(nullptr) + cmd.a * 60; }
                logEvent("Timer started: " + String(cmd.a) + " min (countdown here is an estimate)");
            }
            break;
        }
        case CMD_TIMER_STOP:
            if (writeControl(Bytes(glance::kStopTimer, glance::kStopTimer + 1), "Timer stop")) {
                { Lock l; st.timerEnd = 0; }
                logEvent("Timer stopped");
            }
            break;
        case CMD_SETTING: {
            std::map<uint32_t, uint64_t> changes{{uint32_t(cmd.a), uint64_t(cmd.b)}};
            changeSettings(changes, glance::BrightnessRange(), "Setting " + String(cmd.a));
            break;
        }
        case CMD_RANGE: {
            glance::BrightnessRange r;
            r.present = true; r.low = cmd.a; r.high = cmd.b;
            changeSettings({}, r, "Automatic brightness " + String(cmd.a) + "–" + String(cmd.b));
            break;
        }
        case CMD_CAL_START: {
            bool homing;
            { Lock l; homing = st.hand.homing; }
            if (homing) { logEvent("The hands are already moving", true); break; }
            if (writeControl(Bytes(glance::kStartCalibration, glance::kStartCalibration + 1), "Start calibration")) {
                { Lock l; st.calibrationRequested = true; }
                logEvent("Calibration started. Confirm only when both hands point exactly to 12.");
            }
            break;
        }
        case CMD_CAL_CONFIRM: {
            bool homing;
            { Lock l; homing = st.hand.homing; }
            if (homing) { logEvent("Wait for the hands to stop before confirming", true); break; }
            if (writeControl(Bytes(glance::kConfirmCalibration, glance::kConfirmCalibration + 1), "Confirm calibration")) {
                { Lock l; st.calibrationRequested = false; }
                logEvent("Hand position confirmed");
            }
            break;
        }
        case CMD_ALARMS_SYNC: {
            std::vector<glance::Alarm> list;
            {
                Lock l;
                for (const auto& a : cfg.alarms) if (a.enabled) list.push_back(a.alarm);
            }
            Bytes data;
            if (!glance::alarmsCommand(list, data)) { logEvent("Alarm list is not valid; nothing was sent", true); break; }
            // Replaces every alarm on the clock (including old ones set by the phone app).
            if (writeControl(data, list.empty() ? "Clear alarms" : "Alarms")) {
                { Lock l; cfg.alarmsPending = false; }
                saveConfig();
                logEvent(list.empty() ? "All alarms on the clock cleared" : "Alarms on the clock replaced: " + String(list.size()) + " active");
            }
            break;
        }
        case CMD_ALARM_STOP:
            if (writeControl(Bytes(glance::kAlarmStop, glance::kAlarmStop + 4), "Stop alarm")) logEvent("Alarm stopped");
            break;
        case CMD_REFRESH: {
            glance::Settings s;
            if (readSettings(s)) logEvent("Settings refreshed");
            readBattery();
            break;
        }
    }
    { Lock l; st.busy = false; }
}

void clockTask(void*) {
    uint32_t nextAttempt = 0, lastMinuteCheck = 0, lastBattery = 0;
    int failures = 0;
    long lastOffset = LONG_MIN;
    int lastIsDst = -1, lastDailyYday = -1;
    if (cfg.paired()) setClock(ClockState::Waiting, "Waiting for Wi-Fi and internet time…");
    for (;;) {
        Command cmd;
        if (xQueueReceive(commandQueue, &cmd, pdMS_TO_TICKS(200)) == pdTRUE) handleCommand(cmd);

        // A late disconnect notice from a link we already replaced (sync/pair
        // reconnects) must not clear the new link's handles.
        if (linkDown && !client->isConnected()) {
            linkDown = false;
            resetLinkObjects();
            if (!userSuspended) {
                setClock(ClockState::Waiting, "Clock disconnected; reconnecting shortly");
                nextAttempt = millis() + 5000;
            }
        }
        // Keep the saved clock connected. Reconnection is not command replay.
        bool wantLink = cfg.paired() && cfg.autoConnect && !userSuspended && !linkUp();
        bool timeReadyOrLate = timeIsValid() || millis() > 90000;
        if (wantLink && timeReadyOrLate && millis() >= nextAttempt && uxQueueMessagesWaiting(commandQueue) == 0) {
            { Lock l; st.busy = true; }
            bool ok = connectSaved();
            { Lock l; st.busy = false; }
            failures = ok ? 0 : failures + 1;
            nextAttempt = millis() + (ok ? 0 : (failures < 3 ? 15000 : failures < 6 ? 30000 : 60000));
        }
        if (timeReadDeadline && millis() > timeReadDeadline) {
            timeReadDeadline = 0;
            if (timeReadCount == timeReadBaseline && linkUp())
                logEvent("The clock has not read the time on this connection yet", true);
        }
        if (syncAfterNtp && linkUp()) {
            syncAfterNtp = false;
            bool lastValid;
            { Lock l; lastValid = st.lastReadValid; }
            if (!lastValid) doSync("internet time arrived");
        }
        // Hourly chime: the time as a short message with the chosen sound.
        static int lastChimeHour = -1;
        static uint32_t lastChimeCheck = 0;
        if (millis() - lastChimeCheck > 1000 && timeIsValid()) {
            lastChimeCheck = millis();
            time_t now = time(nullptr);
            struct tm l;
            localtime_r(&now, &l);
            if (lastChimeHour < 0) lastChimeHour = l.tm_hour;  // never chime for an hour that started before boot
            if (l.tm_min == 0 && l.tm_hour != lastChimeHour) {
                lastChimeHour = l.tm_hour;
                bool quiet = cfg.quietFrom == cfg.quietTo ? false
                           : cfg.quietFrom < cfg.quietTo ? (l.tm_hour >= cfg.quietFrom && l.tm_hour < cfg.quietTo)
                                                         : (l.tm_hour >= cfg.quietFrom || l.tm_hour < cfg.quietTo);
                if (cfg.chimeOn && !quiet && linkUp() && timeIsValid() && !handsBusy() && !timerRunning()) {
                    bool twelve;
                    { Lock lk; twelve = st.settings.valid && st.settings.twelveHour; }
                    char text[8];
                    if (twelve) snprintf(text, sizeof text, "%d%s", l.tm_hour % 12 ? l.tm_hour % 12 : 12, l.tm_hour < 12 ? "AM" : "PM");
                    else snprintf(text, sizeof text, "%02d:00", l.tm_hour);
                    Bytes chime;
                    if (glance::notice(text, 0, cfg.chimeSound, 12, chime) && writeControl(chime, "Hourly chime")) logEvent(String("Hourly chime ") + text);
                }
            }
        }
        // Alternate the single weather ring between temperature and rain.
        static uint32_t wxNextSwap = 0;
        bool alternating;
        { Lock l; alternating = cfg.wxOn && cfg.wxRainMode == 1 && cfg.wxTemp && st.wxRainDue; }
        if (alternating && linkUp() && !wxSendRequested && int32_t(millis() - wxNextSwap) >= 0 && uxQueueMessagesWaiting(commandQueue) == 0) {
            wxPhaseRain = !wxPhaseRain;
            wxNextSwap = millis() + uint32_t(cfg.wxCycle) * 1000;
            sendWeather(false);
        }
        if (wxSendRequested && linkUp() && uxQueueMessagesWaiting(commandQueue) == 0) {
            bool user = wxSendUser;
            wxSendRequested = false;
            wxSendUser = false;
            { Lock l; st.busy = true; }
            sendWeather(user);
            { Lock l; st.busy = false; }
        }
        if (millis() - lastMinuteCheck > 20000) {
            lastMinuteCheck = millis();
            time_t now = time(nullptr);
            bool manual;
            { Lock l; manual = st.manualTime; }
            if (timeIsValid() && !manual) {
                struct tm l;
                localtime_r(&now, &l);
                long offset = glance::utcOffsetSeconds(now);
                if (lastOffset != LONG_MIN && offset != lastOffset && linkUp()) {
                    { Lock lk; st.pendingReasonBits |= (l.tm_isdst != lastIsDst) ? 8 : 4; }
                    doSync("daylight saving / time-zone change");
                }
                lastOffset = offset;
                lastIsDst = l.tm_isdst;
                static int lastWeatherHour = -1;
                if (lastWeatherHour < 0) lastWeatherHour = l.tm_hour;  // just connected/sent; wait for the next hour
                else if (cfg.wxOn && l.tm_min >= 1 && l.tm_hour != lastWeatherHour && linkUp()) {
                    lastWeatherHour = l.tm_hour;
                    wxSendRequested = true;  // advance the 24-hour window each hour
                }
                if (cfg.dailySync && l.tm_hour == 3 && l.tm_min >= 5 && l.tm_min < 10 && l.tm_yday != lastDailyYday && linkUp()) {
                    lastDailyYday = l.tm_yday;
                    doSync("daily correction");
                }
            }
        }
        if (linkUp() && millis() - lastBattery > 600000) {
            lastBattery = millis();
            readBattery();
        }
    }
}

// ----------------------------------------------------------------- weather
bool fetchWeather() {
    String url;
    {
        Lock l;  // the web page may be changing the location
        url = "http://api.open-meteo.com/v1/forecast?latitude=" + cfg.wxLat + "&longitude=" + cfg.wxLon +
              "&hourly=temperature_2m,precipitation_probability,precipitation&current=temperature_2m&forecast_hours=36&timeformat=unixtime";
        if (cfg.wxF) url += "&temperature_unit=fahrenheit";
    }
    HTTPClient http;
    http.setTimeout(12000);
    if (!http.begin(url)) { Lock l; st.wxError = "Could not start the weather request"; return false; }
    int code = http.GET();
    String body = code == 200 ? http.getString() : "";
    http.end();
    weather::Forecast f;
    if (code != 200 || !weather::parseOpenMeteo(std::string(body.c_str()), f)) {
        String err = code == 200 ? "Weather data not recognised" : "Weather service error (HTTP " + String(code) + ")";
        { Lock l; st.wxError = err; }
        logEvent(err, true);
        return false;
    }
    { Lock l; st.forecast = f; st.wxFetched = time(nullptr); st.wxError = ""; }
    return true;
}

void weatherTask(void*) {
    uint32_t lastFetch = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        bool configured;
        { Lock l; configured = cfg.wxOn && cfg.wxLat.length() && cfg.wxLon.length(); }
        if (!configured || WiFi.status() != WL_CONNECTED) continue;
        bool due = wxFetchRequested || lastFetch == 0 || millis() - lastFetch > 30UL * 60UL * 1000UL;
        if (!due) continue;
        bool user = wxFetchRequested && lastFetch != 0;
        wxFetchRequested = false;
        lastFetch = millis();
        if (fetchWeather()) {
            if (user) wxSendUser = true;
            wxSendRequested = true;
        }
    }
}

// -------------------------------------------------------------- Wi-Fi/time
WebServer server(80);
DNSServer dns;
bool apActive = false;
bool mdnsStarted = false;
uint32_t staStarted = 0, staConnectedAt = 0;

void timeSynced(struct timeval*) {
    bool first;
    {
        Lock l;
        first = !st.ntpSynced;
        st.ntpSynced = true;
        st.lastNtp = time(nullptr);
    }
    if (first) {
        syncAfterNtp = true;
        logEvent("Internet time received: " + formatLocal(time(nullptr)));
    }
}
void startInternetTime() {
    esp_sntp_set_time_sync_notification_cb(timeSynced);
    configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}
void applyTimeZone() {
    setenv("TZ", cfg.tz.c_str(), 1);
    tzset();
}
void startAP() {
    if (apActive) return;
    WiFi.mode(cfg.ssid.length() ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(kSetupSSID);  // open hotspot, by owner request
    dns.start(53, "*", WiFi.softAPIP());
    apActive = true;
    logEvent("Setup hotspot " + String(kSetupSSID) + " started at " + WiFi.softAPIP().toString());
}
void stopAP() {
    if (!apActive) return;
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apActive = false;
    logEvent("Setup hotspot stopped");
}
void startWiFi() {
    WiFi.persistent(false);
    WiFi.setHostname(kHostname);
    if (cfg.ssid.isEmpty()) { startAP(); return; }
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.wifiPass.c_str());
    staStarted = millis();
}
void serviceWiFi() {
    if (cfg.ssid.isEmpty()) return;
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !staConnectedAt) {
        staConnectedAt = millis();
        logEvent("Wi-Fi connected: " + WiFi.localIP().toString());
        if (!mdnsStarted && MDNS.begin(kHostname)) {
            MDNS.addService("http", "tcp", 80);
            mdnsStarted = true;
        }
    }
    if (!connected && staConnectedAt) {
        staConnectedAt = 0;
        staStarted = millis();
        logEvent("Wi-Fi connection lost; reconnecting", true);
        WiFi.reconnect();
    }
    // Fallback hotspot so a wrong password or a new router can be fixed.
    if (!connected && !apActive && millis() - staStarted > 120000) startAP();
    if (connected && apActive && millis() - staConnectedAt > 60000 && WiFi.softAPgetStationNum() == 0) stopAP();
}

// ----------------------------------------------------------------- web
String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (uint8_t(c) < 32) out += ' ';
        else out += c;
    }
    return out;
}
String q(const String& s) { return "\"" + jsonEscape(s) + "\""; }
const char* b(bool v) { return v ? "true" : "false"; }

// No passwords, by owner request: anyone on the home network (or the setup
// hotspot) can use the page. OTA images are still validated before install.
bool setupMode() { return cfg.ssid.isEmpty(); }
void sendJson(int code, const String& body) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json", body);
}
void ok(const String& message = "") { sendJson(200, message.length() ? "{\"ok\":true,\"message\":" + q(message) + "}" : "{\"ok\":true}"); }
void fail(const String& error, int code = 400) { sendJson(code, "{\"ok\":false,\"error\":" + q(error) + "}"); }
void queued(bool accepted, const String& message) { accepted ? ok(message) : fail("The bridge is busy; try again in a moment", 503); }

void handleStatus() {
    time_t now = time(nullptr);
    bool valid = timeIsValid();
    String j;
    j.reserve(6000);
    Lock l;
    j += "{\"fw\":" + q(OG_FW_VERSION) + ",\"uptime\":" + String(millis() / 1000) + ",\"heap\":" + String(ESP.getFreeHeap());
    j += ",\"setup\":" + String(b(setupMode()));
    j += ",\"wifi\":{\"connected\":" + String(b(WiFi.status() == WL_CONNECTED)) + ",\"ssid\":" + q(cfg.ssid) + ",\"ip\":" + q(WiFi.localIP().toString());
    j += ",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",\"ap\":" + String(b(apActive)) + ",\"apIp\":" + q(WiFi.softAPIP().toString()) + "}";
    j += ",\"time\":{\"valid\":" + String(b(valid)) + ",\"source\":" + q(st.manualTime ? "manual" : st.ntpSynced ? "internet" : "none");
    j += ",\"local\":" + q(valid ? formatLocal(now) : "") + ",\"epoch\":" + String((long)now) + ",\"tz\":" + q(cfg.tz) + ",\"tzName\":" + q(cfg.tzName);
    j += ",\"lastNtp\":" + q(formatLocal(st.lastNtp)) + ",\"daily\":" + b(cfg.dailySync) + "}";
    j += ",\"clock\":{\"state\":" + q(stateName(st.clock)) + ",\"text\":" + q(st.clockText) + ",\"paired\":" + b(cfg.paired());
    j += ",\"name\":" + q(st.clockName.length() ? st.clockName : cfg.clockName) + ",\"address\":" + q(cfg.clockAddr);
    j += ",\"connected\":" + String(b(st.clock == ClockState::Connected)) + ",\"battery\":" + String(st.battery) + ",\"busy\":" + b(st.busy);
    j += ",\"auto\":" + String(b(cfg.autoConnect)) + ",\"suspended\":" + b(userSuspended) + ",\"announce\":" + b(cfg.announce);
    j += ",\"chime\":{\"on\":" + String(b(cfg.chimeOn)) + ",\"sound\":" + String(cfg.chimeSound) + ",\"quietFrom\":" + String(cfg.quietFrom) + ",\"quietTo\":" + String(cfg.quietTo) + "}";
    j += ",\"alarmsPending\":" + String(b(cfg.alarmsPending)) + ",\"alarms\":[";
    for (size_t i = 0; i < cfg.alarms.size(); i++) {
        const auto& a = cfg.alarms[i];
        if (i) j += ",";
        j += "{\"hour\":" + String(a.alarm.hour) + ",\"minute\":" + String(a.alarm.minute) + ",\"days\":" + String(a.alarm.days) +
             ",\"sound\":" + String(a.alarm.sound) + ",\"enabled\":" + b(a.enabled) + ",\"text\":" + q(a.alarm.text.c_str()) + "}";
    }
    j += "]";
    j += ",\"lastRead\":" + q(formatLocal(st.lastTimeRead)) + ",\"lastReadValid\":" + b(st.lastReadValid) + ",\"supplied\":" + q(st.lastSupplied);
    j += ",\"subscribed\":" + String(b(st.timeSubscribed)) + ",\"confirm\":" + String(st.confirmNumber);
    long remaining = st.timerEnd > now ? long(st.timerEnd - now) : 0;
    j += ",\"timerRemaining\":" + String(remaining) + ",\"calibrationRequested\":" + b(st.calibrationRequested);
    if (st.settings.valid) {
        const auto& s = st.settings;
        j += ",\"settings\":{\"brightness\":" + String(s.brightness) + ",\"autoLow\":" + String(s.autoLow) + ",\"autoHigh\":" + String(s.autoHigh);
        j += ",\"nightMode\":" + String(b(s.nightMode)) + ",\"digitalTime\":" + b(s.digitalTime) + ",\"twelveHour\":" + b(s.twelveHour);
        j += ",\"hourPoints\":" + String(b(s.hourPoints)) + ",\"muted\":" + b(s.muted) + ",\"dateFormat\":" + String(s.dateFormat) + "}";
    } else j += ",\"settings\":null";
    j += ",\"hand\":{\"valid\":" + String(b(st.hand.valid)) + ",\"homing\":" + b(st.hand.homing) + ",\"awaiting\":" + b(st.hand.awaitingConfirmation);
    j += ",\"homingFailure\":" + String(b(st.hand.homingFailure)) + ",\"motorFailure\":" + b(st.hand.motorFailure) + ",\"bits\":" + String(st.hand.bits) + "}}";
    {
        j += ",\"weather\":{\"on\":" + String(b(cfg.wxOn)) + ",\"place\":" + q(cfg.wxPlace) + ",\"lat\":" + q(cfg.wxLat) + ",\"lon\":" + q(cfg.wxLon);
        j += ",\"temp\":" + String(b(cfg.wxTemp)) + ",\"rainMode\":" + String(cfg.wxRainMode) + ",\"text\":" + b(cfg.wxText) + ",\"cycle\":" + String(cfg.wxCycle) +
             ",\"rainHours\":" + String(cfg.wxRainHours) + ",\"rainChance\":" + String(cfg.wxRainChance) + ",\"rainMm\":" + String(cfg.wxRainMm, 1) + ",\"unit\":" + q(cfg.wxF ? "F" : "C");
        j += ",\"coldAt\":" + String(cfg.wxColdAt) + ",\"hotAt\":" + String(cfg.wxHotAt);
        j += ",\"fetched\":" + q(formatLocal(st.wxFetched)) + ",\"sent\":" + q(formatLocal(st.wxSent)) + ",\"error\":" + q(st.wxError);
        j += ",\"showing\":" + String(st.wxShowing) + ",\"rainDue\":" + b(st.wxRainDue);
        j += ",\"current\":" + (st.forecast.hasCurrent ? String(st.forecast.currentTemp, 1) : String("null")) + ",\"hours\":[";
        int start = weather::startIndex(st.forecast, now);
        for (int i = 0; start >= 0 && i < 12 && start + i < st.forecast.count; i++) {
            int k = start + i;
            if (i) j += ",";
            j += "{\"h\":" + q(formatLocal(st.forecast.hour[k], "%H")) + ",\"t\":" + String(st.forecast.temp[k], 1) +
                 ",\"c\":" + String(st.forecast.chance[k]) + ",\"r\":" + String(st.forecast.rain[k], 1) + "}";
        }
        j += "]}";
    }
    j += ",\"scan\":{\"running\":" + String(b(st.scanning)) + ",\"items\":[";
    for (size_t i = 0; i < st.scan.size(); i++) {
        if (i) j += ",";
        j += "{\"i\":" + String(i) + ",\"name\":" + q(st.scan[i].name) + ",\"rssi\":" + String(st.scan[i].rssi) + ",\"addr\":" + q(st.scan[i].addr) + "}";
    }
    j += "]},\"log\":[";
    for (size_t i = 0; i < st.log.size(); i++) {
        const auto& e = st.log[i];
        if (i) j += ",";
        String when = formatLocal(e.when, "%H:%M:%S");
        if (when.isEmpty()) when = "+" + String(e.uptime) + "s";
        j += "{\"t\":" + q(when) + ",\"e\":" + b(e.error) + ",\"m\":" + q(e.text) + "}";
    }
    j += "]}";
    sendJson(200, j);
}

int argInt(const char* name, int fallback = -99999) {
    if (!server.hasArg(name)) return fallback;
    String v = server.arg(name);
    v.trim();
    if (v.isEmpty()) return fallback;
    for (size_t i = 0; i < v.length(); i++) if (!isDigit(v[i]) && !(i == 0 && v[i] == '-')) return fallback;
    return v.toInt();
}
bool clockReady() {
    Lock l;
    return st.clock == ClockState::Connected;
}
bool validPosixTZ(const String& tz) {
    if (tz.length() < 3 || tz.length() > 60) return false;
    for (size_t i = 0; i < tz.length(); i++) {
        char c = tz[i];
        if (!(isAlphaNumeric(c) || c == ',' || c == '.' || c == '/' || c == ':' || c == '-' || c == '+' || c == '<' || c == '>')) return false;
    }
    return true;
}

// OTA upload state
static bool otaHeaderChecked = false, otaMarkerFound = false;
static String otaError, otaNewVersion;
static size_t otaMarkerState = 0, otaVersionCapture = 0;
static size_t otaBytes = 0;

void otaScan(const uint8_t* data, size_t len) {
    static const size_t markerLen = strlen("#OGBRIDGE-FW-ESP32C3:");
    static const char* marker = "#OGBRIDGE-FW-ESP32C3:";
    for (size_t i = 0; i < len; i++) {
        char c = char(data[i]);
        if (otaVersionCapture) {
            // The marker text also occurs inside this checker; only accept the
            // occurrence followed by "<digits.dots>;".
            if (c == ';' && otaNewVersion.length()) { otaMarkerFound = true; otaVersionCapture = 0; continue; }
            if ((isDigit(c) || c == '.') && otaNewVersion.length() < 8) { otaNewVersion += c; continue; }
            otaVersionCapture = 0;
            otaNewVersion = "";
            otaMarkerState = (c == marker[0]) ? 1 : 0;
            continue;
        }
        if (otaMarkerFound) continue;
        if (c == marker[otaMarkerState]) {
            if (++otaMarkerState == markerLen) { otaMarkerState = 0; otaVersionCapture = 1; otaNewVersion = ""; }
        } else otaMarkerState = (c == marker[0]) ? 1 : 0;  // '#' occurs once in the marker
    }
}
void handleUploadData() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        otaError = otaNewVersion = "";
        otaHeaderChecked = otaMarkerFound = false;
        otaMarkerState = otaVersionCapture = otaBytes = 0;
        logEvent("Firmware upload started: " + up.filename);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaError = String("Cannot start update: ") + Update.errorString();
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (otaError.length()) return;
        if (!otaHeaderChecked) {
            otaHeaderChecked = true;
            const uint8_t* h = up.buf;
            bool image = up.currentSize >= 36 && h[0] == 0xE9;
            bool c3 = image && h[12] == 0x05 && h[13] == 0x00;  // esp_image_header_t chip_id: ESP32-C3
            bool app = image && h[32] == 0x32 && h[33] == 0x54 && h[34] == 0xCD && h[35] == 0xAB;  // esp_app_desc_t magic
            if (!image || !c3 || !app) {
                otaError = !image ? "Not an ESP32 firmware image" : !c3 ? "This image is not for the ESP32-C3"
                                  : "This looks like a full USB image; upload the OTA .bin instead";
                Update.abort();
                return;
            }
        }
        otaScan(up.buf, up.currentSize);
        otaBytes += up.currentSize;
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            otaError = String("Write failed: ") + Update.errorString();
            Update.abort();
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (otaError.length()) return;
        if (!otaMarkerFound) { otaError = "This file is not OpenGlance bridge firmware"; Update.abort(); return; }
        if (!Update.end(true)) otaError = String("Verification failed: ") + Update.errorString();
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        otaError = "Upload interrupted";
    }
}
void handleUploadDone() {
    if (otaError.length()) {
        logEvent("Firmware update rejected: " + otaError, true);
        fail(otaError);
        return;
    }
    logEvent("Firmware " + otaNewVersion + " installed (" + String(otaBytes) + " bytes); restarting");
    sendJson(200, "{\"ok\":true,\"version\":" + q(otaNewVersion) + "}");
    restartAt = millis() + 1500;
}

void setupWeb() {
    server.on("/", HTTP_GET, [] {
        server.sendHeader("Cache-Control", "no-store");
        server.send_P(200, "text/html", setupMode() ? SETUP_HTML : INDEX_HTML);
    });
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/networks", HTTP_GET, [] {
        int n = WiFi.scanNetworks(false, false);
        struct Net { String ssid; int rssi; bool secure; };
        std::vector<Net> nets;
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) continue;
            bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            auto it = std::find_if(nets.begin(), nets.end(), [&](const Net& x) { return x.ssid == ssid; });
            if (it == nets.end()) nets.push_back({ssid, WiFi.RSSI(i), secure});
            else if (WiFi.RSSI(i) > it->rssi) it->rssi = WiFi.RSSI(i);  // mesh/extenders: keep the strongest
        }
        WiFi.scanDelete();
        std::sort(nets.begin(), nets.end(), [](const Net& x, const Net& y) { return x.rssi > y.rssi; });
        String j = "{\"ok\":true,\"networks\":[";
        for (size_t i = 0; i < nets.size() && i < 30; i++) {
            if (i) j += ",";
            j += "{\"ssid\":" + q(nets[i].ssid) + ",\"rssi\":" + String(nets[i].rssi) + ",\"secure\":" + b(nets[i].secure) + "}";
        }
        sendJson(200, j + "]}");
    });
    server.on("/api/setup", HTTP_POST, [] {
        String ssid = server.arg("ssid"), pass = server.arg("pass");
        String tz = server.arg("tz"), tzName = server.arg("tzName");
        ssid.trim();
        if (ssid.isEmpty() || ssid.length() > 32) return fail("Enter your Wi-Fi network name");
        if (pass.length() > 63) return fail("Wi-Fi password is too long");
        if (tz.length() && !validPosixTZ(tz)) return fail("Time zone not recognised");
        cfg.ssid = ssid;
        cfg.wifiPass = pass;
        if (tz.length()) { cfg.tz = tz; cfg.tzName = tzName.length() ? tzName : tz; }
        saveConfig();
        ok("Saved. The bridge is restarting and joining " + ssid + ". Then open http://openglance.local/ on your home Wi-Fi.");
        restartAt = millis() + 2000;
    });
    server.on("/api/wifi", HTTP_POST, [] {
        String ssid = server.arg("ssid");
        ssid.trim();
        if (ssid.isEmpty() || ssid.length() > 32 || server.arg("pass").length() > 63) return fail("Check the network name and password");
        cfg.ssid = ssid;
        cfg.wifiPass = server.arg("pass");
        saveConfig();
        ok("Saved. Restarting to join " + ssid + ". If it fails, the OpenGlance-Setup hotspot returns after 2 minutes.");
        restartAt = millis() + 2000;
    });
    server.on("/api/reboot", HTTP_POST, [] {
        ok("Restarting…");
        restartAt = millis() + 1000;
    });
    server.on("/api/scan", HTTP_POST, [] { queued(enqueue(CMD_SCAN), "Searching for clocks…"); });
    server.on("/api/pair", HTTP_POST, [] {
        int i = argInt("i");
        if (i < 0) return fail("Choose a clock");
        queued(enqueue(CMD_PAIR, i), "Pairing… watch the clock for its PIN");
    });
    server.on("/api/pin", HTTP_POST, [] {
        String pin = server.arg("pin");
        pin.trim();
        if (pin.isEmpty() || pin.length() > 6) return fail("Enter the PIN shown on the clock (up to 6 digits)");
        for (char c : pin) if (!isDigit(c)) return fail("The PIN contains only digits");
        { Lock l; if (st.clock != ClockState::AwaitingPin) return fail("The clock is not asking for a PIN right now"); }
        if (!NimBLEDevice::injectPassKey(client->getConnInfo(), pin.toInt())) return fail("The PIN could not be delivered; press Pair again");
        setClock(ClockState::Securing, "Checking the PIN…");
        ok("PIN sent");
    });
    server.on("/api/confirm", HTTP_POST, [] {
        { Lock l; if (st.clock != ClockState::AwaitingConfirm) return fail("Nothing to confirm right now"); }
        bool accept = server.arg("accept") == "1";
        NimBLEDevice::injectConfirmPasskey(client->getConnInfo(), accept);
        setClock(ClockState::Securing, accept ? "Number confirmed…" : "Pairing rejected");
        ok();
    });
    server.on("/api/connect", HTTP_POST, [] { queued(enqueue(CMD_CONNECT), "Connecting…"); });
    server.on("/api/disconnect", HTTP_POST, [] { queued(enqueue(CMD_DISCONNECT), "Disconnecting…"); });
    server.on("/api/forget", HTTP_POST, [] { queued(enqueue(CMD_FORGET), "Forgetting the saved clock…"); });
    server.on("/api/sync", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        if (!timeIsValid()) return fail("The bridge has no valid time yet");
        queued(enqueue(CMD_SYNC), "Reconnecting so the clock reads the time…");
    });
    server.on("/api/message", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        String text = server.arg("text");
        if (!glance::validText(text.c_str())) return fail("Use 1–64 plain characters (English letters, numbers, punctuation)");
        queued(enqueue(CMD_MESSAGE, argInt("animation", 1), argInt("sound", 0), argInt("color", 18), text), "Sending…");
    });
    server.on("/api/timer", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        int minutes = argInt("minutes");
        if (minutes < 1 || minutes > 720) return fail("Choose 1–720 minutes");
        queued(enqueue(CMD_TIMER, minutes), "Starting timer…");
    });
    server.on("/api/timer/stop", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        queued(enqueue(CMD_TIMER_STOP), "Stopping timer…");
    });
    server.on("/api/setting", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        int field = argInt("field"), value = argInt("value");
        if (!glance::isEditableField(field) || value < 0 || value > 255 || (field == 5 && value > 4) || (field != 10 && field != 5 && value > 1))
            return fail("Invalid setting");
        queued(enqueue(CMD_SETTING, field, value), "Updating…");
    });
    server.on("/api/brightness/range", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        int low = argInt("low"), high = argInt("high");
        if (!glance::validRange(low, high)) return fail("Minimum and maximum must be 0–255 with minimum ≤ maximum");
        queued(enqueue(CMD_RANGE, low, high), "Updating brightness limits…");
    });
    server.on("/api/calibration/start", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        queued(enqueue(CMD_CAL_START), "Starting calibration — the hands will move");
    });
    server.on("/api/calibration/confirm", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        queued(enqueue(CMD_CAL_CONFIRM), "Confirming hand position…");
    });
    server.on("/api/refresh", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        queued(enqueue(CMD_REFRESH), "Reading the clock…");
    });
    server.on("/api/options", HTTP_POST, [] {
        if (server.hasArg("auto")) cfg.autoConnect = server.arg("auto") == "1";
        if (server.hasArg("daily")) cfg.dailySync = server.arg("daily") == "1";
        if (server.hasArg("announce")) cfg.announce = server.arg("announce") == "1";
        saveConfig();
        ok("Saved");
    });
    server.on("/api/timezone", HTTP_POST, [] {
        String tz = server.arg("tz");
        tz.trim();
        if (!validPosixTZ(tz)) return fail("Time zone not recognised");
        cfg.tz = tz;
        cfg.tzName = server.arg("tzName").length() ? server.arg("tzName") : tz;
        saveConfig();
        applyTimeZone();
        { Lock l; st.pendingReasonBits |= 4; }
        logEvent("Time zone set to " + cfg.tzName);
        if (clockReady() && timeIsValid()) enqueue(CMD_SYNC);
        ok("Time zone saved");
    });
    server.on("/api/time/manual", HTTP_POST, [] {
        int y, mo, d, h, mi;
        if (sscanf(server.arg("date").c_str(), "%d-%d-%d", &y, &mo, &d) != 3 || sscanf(server.arg("time").c_str(), "%d:%d", &h, &mi) != 2 ||
            y < 2025 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59)
            return fail("Enter a valid date and time");
        struct tm t = {};
        t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
        time_t when = mktime(&t);
        esp_sntp_stop();
        struct timeval tv = {when, 0};
        settimeofday(&tv, nullptr);
        { Lock l; st.manualTime = true; st.pendingReasonBits |= 1; }
        logEvent("Manual time set: " + formatLocal(when) + " (internet time paused until restart or Back to internet time)");
        if (clockReady()) enqueue(CMD_SYNC);
        ok("Manual time set");
    });
    server.on("/api/time/internet", HTTP_POST, [] {
        { Lock l; st.manualTime = false; st.ntpSynced = false; }
        startInternetTime();
        logEvent("Returning to internet time");
        ok("Waiting for internet time; the clock is updated when it arrives");
    });
    server.on("/api/weather", HTTP_POST, [] {
        bool refetch = false;
        if (server.hasArg("lat") || server.hasArg("lon")) {
            double lat = server.arg("lat").toDouble(), lon = server.arg("lon").toDouble();
            if (server.arg("lat").isEmpty() || server.arg("lon").isEmpty() || lat < -90 || lat > 90 || lon < -180 || lon > 180) return fail("Choose a location");
            Lock l;
            cfg.wxLat = String(lat, 4);
            cfg.wxLon = String(lon, 4);
            cfg.wxPlace = server.arg("place").substring(0, 60);
            refetch = true;
        }
        if (server.hasArg("on")) { cfg.wxOn = server.arg("on") == "1"; refetch = refetch || cfg.wxOn; }
        if (server.hasArg("temp")) cfg.wxTemp = server.arg("temp") == "1";
        if (server.hasArg("rainMode")) { int m = argInt("rainMode"); if (m < 0 || m > 2) return fail("Invalid rain option"); cfg.wxRainMode = m; }
        if (server.hasArg("text")) cfg.wxText = server.arg("text") == "1";
        if (server.hasArg("cycle")) { int c = argInt("cycle"); if (c < 5 || c > 3600) return fail("Choose 5 seconds to 1 hour"); cfg.wxCycle = c; }
        if (server.hasArg("rainHours")) { int h = argInt("rainHours"); if (h < 1 || h > 12) return fail("Look ahead 1–12 hours"); cfg.wxRainHours = h; }
        if (server.hasArg("rainChance")) { int c = argInt("rainChance"); if (c < 0 || c > 100) return fail("Chance must be 0–100 %"); cfg.wxRainChance = c; }
        if (server.hasArg("unit")) { bool f = server.arg("unit") == "F"; refetch = refetch || f != cfg.wxF; cfg.wxF = f; }
        if (server.hasArg("rainMm")) {
            float mm = server.arg("rainMm").toFloat();
            if (mm < 0.1f || mm > 10.0f) return fail("Rain threshold must be 0.1–10 mm");
            cfg.wxRainMm = mm;
        }
        if (server.hasArg("coldAt") && server.hasArg("hotAt")) {
            int c = argInt("coldAt"), h = argInt("hotAt");
            if (c < -60 || h > 130 || c >= h) return fail("“Cold at” must be lower than “hot at”");
            cfg.wxColdAt = c; cfg.wxHotAt = h;
        }
        if (cfg.wxOn && (cfg.wxLat.isEmpty() || cfg.wxLon.isEmpty())) { cfg.wxOn = false; saveConfig(); return fail("Choose a location first"); }
        saveConfig();
        if (refetch) wxFetchRequested = true;
        else { wxSendUser = true; wxSendRequested = true; }
        ok(refetch ? "Saved. Fetching the forecast…" : "Saved");
    });
    auto alarmsChanged = [](const String& message) {
        { Lock l; cfg.alarmsPending = true; }
        saveConfig();
        if (clockReady()) { enqueue(CMD_ALARMS_SYNC); ok(message); }
        else ok(message + " It will be sent when the clock connects.");
    };
    server.on("/api/alarms/save", HTTP_POST, [alarmsChanged] {
        int index = argInt("index", -1);
        BridgeAlarm a;
        a.alarm.hour = argInt("hour");
        a.alarm.minute = argInt("minute");
        a.alarm.days = argInt("days", 0);
        a.alarm.sound = argInt("sound");
        a.alarm.text = server.arg("text").c_str();
        a.enabled = server.arg("enabled") != "0";
        if (!glance::validAlarm(a.alarm) || a.alarm.sound == 0) return fail("Check the time, days, sound and label (up to 16 plain characters)");
        {
            Lock l;
            if (index >= 0 && index < int(cfg.alarms.size())) cfg.alarms[index] = a;
            else if (cfg.alarms.size() >= size_t(glance::kMaxAlarms)) return fail("The clock supports up to 4 alarms");
            else cfg.alarms.push_back(a);
        }
        alarmsChanged("Alarm saved.");
    });
    server.on("/api/alarms/enable", HTTP_POST, [alarmsChanged] {
        int index = argInt("index", -1);
        {
            Lock l;
            if (index < 0 || index >= int(cfg.alarms.size())) return fail("No such alarm");
            cfg.alarms[index].enabled = server.arg("enabled") == "1";
        }
        alarmsChanged("Saved.");
    });
    server.on("/api/alarms/delete", HTTP_POST, [alarmsChanged] {
        int index = argInt("index", -1);
        {
            Lock l;
            if (index < 0 || index >= int(cfg.alarms.size())) return fail("No such alarm");
            cfg.alarms.erase(cfg.alarms.begin() + index);
        }
        alarmsChanged("Alarm deleted.");
    });
    server.on("/api/alarms/clear", HTTP_POST, [alarmsChanged] {
        { Lock l; cfg.alarms.clear(); }
        alarmsChanged("All alarms deleted.");
    });
    server.on("/api/alarms/stop", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        queued(enqueue(CMD_ALARM_STOP), "Stopping the alarm…");
    });
    server.on("/api/chime", HTTP_POST, [] {
        if (server.hasArg("on")) cfg.chimeOn = server.arg("on") == "1";
        if (server.hasArg("sound")) { int v = argInt("sound"); if (!glance::validSound(v) || v == 0) return fail("Choose a sound"); cfg.chimeSound = v; }
        if (server.hasArg("quietFrom") && server.hasArg("quietTo")) {
            int a = argInt("quietFrom"), z = argInt("quietTo");
            if (a < 0 || a > 23 || z < 0 || z > 23) return fail("Quiet hours must be 0–23");
            cfg.quietFrom = a; cfg.quietTo = z;
        }
        saveConfig();
        ok("Saved");
    });
    server.on("/api/sound/test", HTTP_POST, [] {
        if (!clockReady()) return fail("Connect the clock first");
        int v = argInt("sound");
        if (!glance::validSound(v) || v == 0) return fail("Choose a sound");
        bool muted;
        { Lock l; muted = st.settings.valid && st.settings.muted; }
        queued(enqueue(CMD_MESSAGE, 0, v, 12, "Sound test"), muted ? "Sent, but Mute is on (Display tab), so you may hear nothing" : "Playing on the clock…");
    });
    server.on("/api/weather/refresh", HTTP_POST, [] {
        if (!cfg.wxOn) return fail("Turn on the weather widgets first");
        wxFetchRequested = true;
        ok("Fetching the forecast…");
    });
    server.on("/update", HTTP_POST, handleUploadDone, handleUploadData);
    server.onNotFound([] {
        if (apActive) {
            String host = server.hostHeader();
            if (host != WiFi.softAPIP().toString() && host != String(kHostname) + ".local") {
                server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
                server.send(302, "text/plain", "");
                return;
            }
        }
        server.send(404, "text/plain", "Not found");
    });
    const char* headers[] = {"Host"};
    server.collectHeaders(headers, 1);
    server.begin();
}

// ------------------------------------------------------------ setup/loop
void setup() {
    pinMode(kLedPin, OUTPUT);
    digitalWrite(kLedPin, HIGH);
    pinMode(kBootPin, INPUT_PULLUP);
    Serial.begin(115200);
    stateLock = xSemaphoreCreateMutex();
    commandQueue = xQueueCreate(8, sizeof(Command));
    loadConfig();
    applyTimeZone();
    Serial.printf("\nOpenGlance Bridge %s (%s)\n", OG_FW_VERSION, kFirmwareMarker);
    logEvent(String("OpenGlance Bridge ") + OG_FW_VERSION + " started");
    if (cfg.paired()) { Lock l; st.clockName = cfg.clockName; }
    startWiFi();
    startInternetTime();
    setupBluetooth();
    setupWeb();
    xTaskCreate(clockTask, "clock", 10240, nullptr, 2, nullptr);
    xTaskCreate(weatherTask, "weather", 8192, nullptr, 1, nullptr);
}

void loop() {
    server.handleClient();
    if (apActive) dns.processNextRequest();
    serviceWiFi();

    // Hold BOOT for 8 s to return to Wi-Fi setup (keeps the clock pairing).
    static uint32_t pressedAt = 0;
    if (digitalRead(kBootPin) == LOW) {
        if (!pressedAt) pressedAt = millis();
        if (millis() - pressedAt > 8000) {
            cfg.ssid = cfg.wifiPass = "";
            saveConfig();
            logEvent("Wi-Fi settings cleared from the BOOT button");
            delay(200);
            ESP.restart();
        }
    } else pressedAt = 0;

    // LED (active low): fast blink = setup hotspot, short blip = clock connected.
    uint32_t phase = millis() % (apActive ? 400 : 3000);
    bool on = apActive ? phase < 200 : (clockReady() ? phase < 60 : phase < 1000);
    digitalWrite(kLedPin, on ? LOW : HIGH);

    if (restartAt && millis() > restartAt) {
        if (client && client->isConnected()) client->disconnect();
        delay(300);
        ESP.restart();
    }
    delay(2);
}
