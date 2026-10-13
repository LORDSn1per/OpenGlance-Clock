// Host-side checks for glance_protocol.h (run by build.sh before compiling
// firmware). Mirrors the Swift ProtocolChecks from OpenGlance 0.21–0.27.
#include "../OpenGlanceBridge/glance_protocol.h"
#include "../OpenGlanceBridge/weather.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>

using namespace glance;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static Bytes settings(uint64_t brightness = 128) {
    Bytes b;
    putInteger(b, 2, 0);
    putInteger(b, 4, 1);
    putInteger(b, 10, brightness);
    return b;
}
static Bytes cat(Bytes a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
static Bytes payloadOf(const Bytes& command) { return Bytes(command.begin() + 4, command.end()); }
static bool endsWith(const Bytes& a, const Bytes& b) {
    return a.size() >= b.size() && std::equal(b.begin(), b.end(), a.end() - b.size());
}
static void setTZ(const char* tz) { setenv("TZ", tz, 1); tzset(); }

int main() {
    // 1. Discovery filtering
    CHECK(!isGlance("", false));
    CHECK(!isGlance("Headphones", false));
    CHECK(isGlance("GlanceClock_07", false));
    CHECK(isGlance("", true));

    // 2. Settings preservation: opaque brightness bits and unknown fields
    {
        Bytes unknown; putBytes(unknown, 30, Bytes{0, 255, 8});
        Settings s; CHECK(parseSettings(cat(settings(2016768), unknown), s));
        CHECK(s.brightness == 0);
        Bytes cmd; CHECK(patchSettings(s, {{10, 180}, {2, 1}}, BrightnessRange(), cmd));
        CHECK(cmd[0] == 5 && cmd[1] == 0 && cmd[2] == 0 && cmd[3] == 0);
        WireMessage m; CHECK(m.parse(payloadOf(cmd)));
        CHECK(m.integerOr(10, 0) == (2016768 | 180));
        CHECK(endsWith(payloadOf(cmd), unknown));
        Settings u; CHECK(parseSettings(payloadOf(cmd), u));
        CHECK(settingsMatch(u, {{10, 180}, {2, 1}}, BrightnessRange()));
    }
    // 3. Validation
    {
        Settings s;
        CHECK(!parseSettings(Bytes{8, 1}, s));
        CHECK(parseSettings(settings(), s));
        Bytes cmd;
        CHECK(!patchSettings(s, {{3, 1}}, BrightnessRange(), cmd));
        CHECK(!patchSettings(s, {{10, 256}}, BrightnessRange(), cmd));
        CHECK(parseSettings(cat(Bytes{68, 97, 116, 97, 0}, settings()), s) && s.brightness == 128);
    }
    // 4. Automatic brightness range (original APK encoding)
    {
        BrightnessRange r; r.present = true; r.low = 30; r.high = 220;
        CHECK(r.encoded() == 2022400);
        Bytes unknown; putBytes(unknown, 30, Bytes{1, 2, 255});
        Settings original; CHECK(parseSettings(cat(settings(0xab000000ULL | 128), unknown), original));
        Bytes cmd; CHECK(patchSettings(original, {}, r, cmd));
        Settings u; CHECK(parseSettings(payloadOf(cmd), u));
        CHECK(u.brightness == 0 && u.autoLow == 30 && u.autoHigh == 220);
        WireMessage m; m.parse(u.raw); CHECK(m.integerOr(10, 0) == (0xab000000ULL | 2022400));
        CHECK(endsWith(u.raw, unknown));
        CHECK(settingsMatch(u, {}, r));
        BrightnessRange r2 = r; r2.low = 31; CHECK(!settingsMatch(u, {}, r2));
        Bytes manualCmd; CHECK(patchSettings(u, {{10, 200}}, BrightnessRange(), manualCmd));
        Settings manual; CHECK(parseSettings(payloadOf(manualCmd), manual));
        CHECK(manual.brightness == 200 && manual.autoLow == 30 && manual.autoHigh == 220);
        CHECK(!validRange(-1, 220) && !validRange(30, 256) && !validRange(200, 100));
        CHECK(!patchSettings(original, {{10, 128}}, r, cmd));
        for (int v : {0, 255}) {
            BrightnessRange f; f.present = true; f.low = v; f.high = v;
            Bytes c; CHECK(patchSettings(original, {}, f, c));
            Settings x; CHECK(parseSettings(payloadOf(c), x) && settingsMatch(x, {}, f));
        }
    }
    // 5. Notice golden packet
    {
        Bytes out; CHECK(notice("Hi", 1, 0, 18, out));
        CHECK((out == Bytes{2, 16, 0, 0, 8, 1, 16, 0, 24, 18, 34, 6, 8, 0, 18, 2, 72, 105}));
        CHECK(!notice("Hello \xF0\x9F\x91\x8B", 1, 0, 18, out));
        CHECK(!notice("", 1, 0, 18, out));
        CHECK(!notice(std::string(65, 'x'), 1, 0, 18, out));
        CHECK(!notice("Hi", 3, 0, 18, out));
        CHECK(notice("14:00", 0, 17, 12, out) && !notice("14:00", 0, 18, 12, out));  // sounds 0–17
    }
    // 6. Timer duration lives in the interval
    {
        Bytes out; CHECK(timer(300, out, "Done"));
        CHECK((Bytes(out.begin(), out.begin() + 12) == Bytes{3, 0, 0, 0, 8, 0, 18, 5, 16, 172, 2, 24}));
        CHECK(!timer(59, out) && !timer(43201, out));
        CHECK(kStartCalibration[0] == 43 && kConfirmCalibration[0] == 44 && kStopTimer[0] == 10);
    }
    // 7. Malformed packets
    {
        const Bytes bad[] = {{8, 128}, {18, 5, 1}, {0}, {11}, {8, 255, 255, 255, 255, 255, 255, 255, 255, 255, 2}};
        for (const auto& b : bad) { WireMessage m; CHECK(!m.parse(b)); }
    }
    // 8. Current time and Sydney daylight saving
    {
        time_t spring = 1791676800;  // 2026-10-11T00:00:00Z
        setTZ("UTC0");
        CHECK((currentTimeValue(spring, true, 0, 0) == Bytes{234, 7, 10, 11, 0, 0, 0, 7, 0, 0}));
        setTZ("AEST-10AEDT,M10.1.0,M4.1.0/3");
        CHECK((localTimeInformation(spring) == Bytes{40, 4}));
        CHECK(utcOffsetSeconds(spring) == 39600);
        setTZ("EST5");
        CHECK((localTimeInformation(spring) == Bytes{236, 0}));
        time_t winter = 1782864000;  // 2026-07-01T00:00:00Z
        setTZ("AEST-10AEDT,M10.1.0,M4.1.0/3");
        CHECK((localTimeInformation(winter) == Bytes{40, 0}));
        setTZ("UTC0");
        CHECK(currentTimeValue(winter, true, 0, 1).back() == 1);
        CHECK((currentTimeValue(winter, false, 0, 1) == Bytes(10, 0)));
        setTZ("ACST-9:30ACDT,M10.1.0,M4.1.0/3");  // Adelaide half-hour zone
        CHECK((localTimeInformation(winter) == Bytes{38, 0}));
    }
    // 9. Hand state decoding (original APK flags)
    {
        uint8_t moving[] = {128}, waiting[] = {0, 4}, failed[] = {64, 8}, three[] = {0, 0, 0};
        HandState h = decodeHandState(moving, 1); CHECK(h.valid && h.homing && !h.awaitingConfirmation);
        h = decodeHandState(waiting, 2); CHECK(h.awaitingConfirmation && !h.homing);
        h = decodeHandState(failed, 2); CHECK(h.homingFailure && h.motorFailure);
        CHECK(!decodeHandState(three, 0).valid && !decodeHandState(three, 3).valid);
    }
    // 11. Alarms (APK 1.5.1 encoder order)
    {
        Alarm a; a.hour = 7; a.minute = 30; a.days = 31; a.sound = 6; a.text = "Wake";
        Bytes out;
        CHECK(alarmsCommand({a}, out));
        CHECK((out == Bytes{4, 47, 0, 0, 0x0A, 0x12, 0x18, 0x1F, 0x22, 0x04, 0x08, 0x07, 0x10, 0x1E, 0x28, 0x06, 0x32, 0x06, 0x12, 0x04, 'W', 'a', 'k', 'e'}));
        CHECK(alarmsCommand({}, out) && (out == Bytes{21, 0, 0, 0}));
        Alarm once = a; once.days = 0;
        CHECK(alarmsCommand({once}, out) && out[6] == 0x22);  // days omitted for a one-time alarm
        Alarm big = a; big.text = std::string(16, 'x'); big.days = 127; big.hour = 23; big.minute = 59; big.sound = 17;
        CHECK(alarmsCommand({big, big, big, big}, out) && out.size() <= 155);   // fits the clock's write limit
        CHECK(!alarmsCommand({big, big, big, big, big}, out));
        Alarm bad = a; bad.hour = 24; CHECK(!alarmsCommand({bad}, out));
        bad = a; bad.text = std::string(17, 'x'); CHECK(!alarmsCommand({bad}, out));
        bad = a; bad.sound = 18; CHECK(!alarmsCommand({bad}, out));
        CHECK(kAlarmStop[0] == 20);
    }
    // 10. Weather: Open-Meteo parsing and forecast scenes
    {
        std::string path = std::string(__FILE__);
        path = path.substr(0, path.find_last_of('/')) + "/open_meteo_sydney.json";
        std::ifstream in(path);
        std::stringstream ss; ss << in.rdbuf();
        weather::Forecast f;
        CHECK(weather::parseOpenMeteo(ss.str(), f));
        CHECK(f.count == 24 && f.hour[0] == 1791867600 && f.hasCurrent && std::fabs(f.currentTemp - 18.6f) < 0.01f);
        CHECK(std::fabs(f.temp[0] - 18.8f) < 0.01f && f.chance[0] == 98 && std::fabs(f.rain[1] - 0.6f) < 0.01f);
        CHECK(!weather::parseOpenMeteo("{\"hourly_units\":{\"time\":\"unixtime\"}}", f));
        weather::parseOpenMeteo(ss.str(), f);
        time_t now = 1791868500;  // inside the first hour
        weather::Scene t;
        CHECK(weather::temperatureScene(f, now, 39600, weather::TemperatureOptions(), t));
        CHECK(t.timestamp == 1791867600LL + 39600 && t.values[0] == 19 && t.values[1] == 18 && t.min == 14 && t.max == 19);
        CHECK((t.templ == Bytes{0xC2, 0x8F, 0x08, 0xC2, 0xB0, 'C'}));
        CHECK(!weather::temperatureScene(f, 1791867600 + 30 * 3600, 0, weather::TemperatureOptions(), t));  // stale data
        Bytes cmd = weather::forecastCommand(weather::kWeatherSlot, t, false);
        CHECK(cmd[0] == 7 && cmd[1] == 16 && cmd[2] == 24 && cmd[3] == 1 && cmd.size() <= 150);
        CHECK(weather::forecastCommand(weather::kWeatherSlot, t)[2] == 8);  // default: ring only, time/date stay on the display
        WireMessage m; CHECK(m.parse(Bytes(cmd.begin() + 4, cmd.end())));
        CHECK(m.integerOr(1, 0) == uint64_t(1791867600LL + 39600) && m.integerOr(6, 0) == 19 && m.integerOr(7, 0) == 14);
        // Rain: chance only where >= threshold mm; hour 0 has 0.1 mm (below 0.3), hour 1 has 0.6 mm at 98 %.
        weather::Scene r;
        CHECK(weather::rainScene(f, now, 39600, weather::RainOptions(), r));
        CHECK(r.values[0] == 0 && r.values[1] == 98 && r.min == 0 && r.max == 100);
        CHECK((r.templ == Bytes{0xC2, 0x95, 0x08, '%'}));
        weather::RainOptions heavy; heavy.thresholdMm = 50;
        CHECK(!weather::rainScene(f, now, 39600, heavy, r));  // nothing that heavy: widget hidden
        weather::RainOptions likely; likely.minChance = 99;   // hour 1 is 98 %, hour 2 is 99 % with 0.8 mm
        CHECK(weather::rainScene(f, now, 39600, likely, r) && r.values[1] == 0 && r.values[2] == 99);
        weather::RainOptions soon; soon.lookaheadHours = 1;   // only the current hour (0.1 mm) counts
        CHECK(!weather::rainScene(f, now, 39600, soon, r));
        CHECK((weather::deleteSceneCommand(2) == Bytes{33, 0, 0, 2}));
        // Negative temperatures: Int16LE values and 10-byte varint min.
        weather::Scene cold = t; cold.values[0] = -5; cold.min = -5;
        Bytes c = weather::encodeScene(cold);
        WireMessage cm; CHECK(cm.parse(c));
        CHECK(int32_t(int64_t(cm.integerOr(7, 0))) == -5);
        CHECK(weather::mix(0x000000, 0xFFFFFF, 0.5) == 0x808080 && weather::mix(0x0090FF, 0xFF3000, -1) == 0x0090FF);
        CHECK(weather::palette(-1) == 0x2060FF && weather::palette(1.0 / 3) == 0x00C070 && weather::palette(2) == 0xFF3000);
        CHECK(t.minColor == weather::palette((14 - 5) / 30.0) && t.maxColor == weather::palette((19 - 5) / 30.0));
    }
    if (failures) { std::printf("FAILED: %d check(s)\n", failures); return 1; }
    std::printf("PASS: 11 check groups (discovery, settings preservation, validation, brightness range, notices, timers, malformed packets, time zones/DST, hand state, weather forecasts, alarms).\n");
    return 0;
}
