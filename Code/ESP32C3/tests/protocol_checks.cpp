// Host-side checks for glance_protocol.h (run by build.sh before compiling
// firmware). Mirrors the Swift ProtocolChecks from OpenGlance 0.21–0.27.
#include "../OpenGlanceBridge/glance_protocol.h"
#include "../OpenGlanceBridge/weather.h"
#include "../OpenGlanceBridge/comet.h"
#include <algorithm>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>

using namespace glance;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { if (failures < 20) std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

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
    // Real owner snapshot 0x6314: time adjustment + DND, NOT calibration.
    {
        uint8_t raw[] = {0x14, 0x63};
        HandState h = decodeHandState(raw, 2);
        CHECK(h.valid && h.timeAdjusting && h.dnd && !h.homing && !h.awaitingConfirmation);
        CHECK(!comet::calibrationPauses(h, false));
        CHECK(comet::calibrationPauses(h, true));
        raw[0] |= 128; h = decodeHandState(raw, 2);
        CHECK(comet::calibrationPauses(h, false));
        raw[0] = 0; raw[1] = 4; h = decodeHandState(raw, 2);
        CHECK(comet::calibrationPauses(h, false));
        CHECK(comet::stateFresh(true, 12000, 0));
        CHECK(!comet::stateFresh(true, 12001, 0));
        CHECK(!comet::stateFresh(false, 100, 90));
        CHECK(comet::stateFresh(true, 50, UINT32_MAX - 100));
        CHECK(!comet::stateFresh(true, 13000, UINT32_MAX - 100));
    }
    // DND always pauses the comet, including after legacy override removal.
    {
        for (bool dnd : {false, true}) {
            bool blocked = comet::dndPauses(dnd);
            CHECK(blocked == dnd);
            CHECK(comet::canRun(true, true, true, false, false, blocked, false, false, false) == !blocked);
            CHECK(!comet::canRun(true, true, true, true, false, blocked, false, false, false));
            CHECK(!comet::canRun(true, true, true, false, true, blocked, false, false, false));
            CHECK(!comet::canRun(true, true, true, false, false, blocked, true, false, false));
            CHECK(!comet::canRun(true, true, true, false, false, blocked, false, true, false));
            CHECK(!comet::canRun(true, true, true, false, false, blocked, false, false, true));
        }
        CHECK(comet::validOptions(1, 18) && comet::validOptions(0, 18));
        CHECK(!comet::validOptions(2, 18) && !comet::validOptions(1, 25));
        CHECK(comet::validLegacyDndOption(0) && comet::validLegacyDndOption(1));
        CHECK(!comet::validLegacyDndOption(-1) && !comet::validLegacyDndOption(-2) && !comet::validLegacyDndOption(2));
    }
    // Native seconds (1.20): a single ticking pixel. Each step is ONE Fill painting
    // the new pixel and the previous one black (Expired doesn't erase on hardware).
    // Exhaustive minute × palette × takeover/handoff, plus a framebuffer emulation.
    {
        CHECK(comet::pixelAt(0) == 0 && comet::pixelAt(2999) == 47);
        CHECK(comet::boundaryFrame(48) == 3000 && comet::boundaryFrame(56) == 3500);
        CHECK(comet::kWindowPixels == 8 && comet::kMaxCommand == 155);
        Bytes invalid;
        CHECK(!comet::scene(-1, 18, invalid) && !comet::scene(3000, 18, invalid));
        CHECK(!comet::scene(0, 0, invalid) && !comet::scene(0, 25, invalid));
        auto sub = [](const WireMessage& m, const WireMessage::Field& f, Bytes& outBytes) {
            if (f.wireType != 2) return false;
            size_t i = f.start;
            while (m.raw[i] & 0x80) ++i;
            ++i;
            while (m.raw[i] & 0x80) ++i;
            ++i;
            outBytes.assign(m.raw.begin() + i, m.raw.begin() + f.end);
            return true;
        };
        // Decodes a command into timed paint operations: (frame, pixel, colour).
        struct Paint { int at, pixel, color; };
        auto paints = [&](const Bytes& cmd, std::vector<Paint>& out) {
            out.clear();
            WireMessage m; if (!m.parse(payloadOf(cmd))) return false;
            for (const auto& f : m.fields) {
                Bytes ob, fb; WireMessage obj, fill;
                if (f.number != 1 || !sub(m, f, ob) || !obj.parse(ob)) return false;
                if (obj.integerOr(1, 0) != 2 || obj.integerOr(3, 0) != 1 || obj.integerOr(4, 0) != 0) return false; // Fill, 1 frame, OK
                for (const auto& g : obj.fields) if (g.number != 1 && g.number != 2 && g.number != 3 && g.number != 6) return false;
                if (!sub(obj, obj.fields.back(), fb) || !fill.parse(fb)) return false;
                int at = int(obj.integerOr(2, 0));
                for (const auto& seg : fill.fields) {
                    if (seg.number != 1 || seg.wireType != 0) return false;
                    uint32_t v = uint32_t(seg.integer);
                    int start = v & 63, extra = (v >> 8) & 63, height = (v >> 14) & 3, offset = (v >> 6) & 3;
                    if (start + extra >= 48) return false;                      // never relies on firmware wrap
                    if (extra == 47) { if (start != 0 || height != 3 || offset != 0 || ((v >> 16) & 63) != 0) return false; }  // full black clear
                    else if (extra != 0 || height != 0 || offset != 0) return false;
                    for (int q = start; q <= start + extra; ++q) out.push_back({at, q, int((v >> 16) & 63)});
                }
            }
            return true;
        };
        long scenes = 0; size_t largest = 0;
        for (int color = 1; color <= 24; ++color) {
            for (int frame = 0; frame < 3000; ++frame) {
              for (bool continuation : {false, true}) {
                Bytes cmd; int endPixel = -1;
                CHECK(comet::scene(frame, color, cmd, continuation, &endPixel));
                CHECK(cmd.size() <= comet::kMaxCommand);
                largest = std::max(largest, cmd.size()); ++scenes;
                CHECK((Bytes(cmd.begin(), cmd.begin() + 4) == Bytes{0, 0, 8, 1}));
                const int first = comet::pixelAt(frame), steps = endPixel - first;
                CHECK(steps == 8);   // even a takeover with its ring clear fits all 8 (≤151 bytes)
                std::vector<Paint> ops; CHECK(paints(cmd, ops));
                CHECK(ops.size() == size_t(steps * 2 + (continuation ? 0 : 48)));
                size_t k = continuation ? 0 : 48;
                for (int i = 0; i < steps; ++i, k += 2) {
                    const int p = (first + i) % 48, at = i == 0 ? 0 : comet::boundaryFrame(first + i) - frame;
                    CHECK(ops[k].at == at && ops[k].pixel == p && ops[k].color == color);           // new pixel
                    CHECK(ops[k + 1].at == at && ops[k + 1].pixel == (p + 47) % 48 && ops[k + 1].color == 0); // previous black
                }
              }
            }
        }
        CHECK(scenes == 24L * 3000 * 2 && largest <= 155);
        // Two minutes of handoffs with Bluetooth delays, emulating the ring. Exactly
        // one pixel lit at every frame once running, and it is the current second's.
        for (bool clearsOnReplace : {true, false}) {
          for (int delay : {0, 5, 20, 60}) {
            int ring[48]; for (int& c : ring) c = 0;
            long now = 0; bool cont = false; int sent = 0;
            std::vector<Paint> ops; long sceneStart = 0; size_t next = 0; long nextScene = 0;
            while (now < 6000) {
                if (now == nextScene) {
                    Bytes cmd; int endPixel = 0; int f = int(now % 3000);
                    CHECK(comet::scene(f, 18, cmd, cont, &endPixel));
                    if (clearsOnReplace) for (int& c : ring) c = 0;
                    CHECK(paints(cmd, ops)); sceneStart = now; next = 0; ++sent;
                    nextScene = (now - f) + comet::boundaryFrame(endPixel) + delay;
                    cont = delay < 62;   // the bridge sends before the first uncovered step ends
                }
                while (next < ops.size() && sceneStart + ops[next].at <= now) { ring[ops[next].pixel] = ops[next].color; ++next; }
                int lit = 0, where = -1; for (int q = 0; q < 48; ++q) if (ring[q]) { ++lit; where = q; }
                CHECK(lit == 1);
                // During a late handoff the previous second's pixel stays until replaced.
                int expected = comet::pixelAt(int(now % 3000));
                CHECK(where == expected || (delay > 0 && where == (expected + 47) % 48));
                ++now;
            }
            CHECK(sent <= 2 * 6 + 2);
          }
        }
    }
    // 1.21: seconds LED choice. Any of the 15 radial-LED masks fits one write, each
    // contiguous run is one segment, the table is exact, and mask 1 equals 1.20.
    {
        CHECK(!comet::validRings(0) && !comet::validRings(16) && comet::validRings(1) && comet::validRings(15));
        Bytes bad; CHECK(!comet::scene(0, 18, bad, true, nullptr, 0) && !comet::scene(0, 18, bad, true, nullptr, 16));
        // Independent copy of the 1.20 builder (outer ring only).
        auto old120 = [](int frame, int color, bool cont, int steps) {
            Bytes body;
            if (!cont) { Bytes c; putInteger(c, 1, (47u << 8) | (3u << 14)); Bytes o; putInteger(o, 1, 2); putInteger(o, 3, 1); putBytes(o, 6, c); putBytes(body, 1, o); }
            int first = comet::pixelAt(frame);
            for (int i = 0; i < steps; ++i) {
                int q = (first + i) % 48, prev = (q + 47) % 48, at = i == 0 ? 0 : comet::boundaryFrame(first + i) - frame;
                Bytes f; putInteger(f, 1, uint32_t(q) | (uint32_t(color) << 16)); putInteger(f, 1, uint32_t(prev));
                Bytes o; putInteger(o, 1, 2); if (at) putInteger(o, 2, at); putInteger(o, 3, 1); putBytes(o, 6, f); putBytes(body, 1, o);
            }
            Bytes out{0, 0, 8, 1}; out.insert(out.end(), body.begin(), body.end()); return out;
        };
        for (int frame = 0; frame < 3000; ++frame) for (bool cont : {false, true}) {
            Bytes a, b; int end = 0;
            CHECK(comet::scene(frame, 17, a, cont, &end, 1) && comet::scene(frame, 17, b, cont));
            CHECK(a == b && a == old120(frame, 17, cont, end - comet::pixelAt(frame)));
        }
        auto sub = [](const WireMessage& m, const WireMessage::Field& f, Bytes& outBytes) {
            if (f.wireType != 2) return false;
            size_t i = f.start; while (m.raw[i] & 0x80) ++i; ++i; while (m.raw[i] & 0x80) ++i; ++i;
            outBytes.assign(m.raw.begin() + i, m.raw.begin() + f.end); return true;
        };
        for (int rings = 1; rings <= 15; ++rings) {
            int minSteps = 99, segsPerPaint = 0;
            int off[2], hf[2]; int runs = comet::ringRuns(rings, off, hf);
            int rebuilt = 0; for (int r = 0; r < runs; ++r) for (int k = 0; k <= hf[r]; ++k) rebuilt |= 1 << (off[r] + k);
            CHECK(rebuilt == rings && runs >= 1 && runs <= 2 && (runs == 2) == (rings == 5 || rings == 9 || rings == 10 || rings == 11 || rings == 13));
            for (int color : {1, 17, 24}) for (int frame = 0; frame < 3000; ++frame) for (bool cont : {false, true}) {
                Bytes cmd; int end = 0;
                CHECK(comet::scene(frame, color, cmd, cont, &end, rings) && cmd.size() <= comet::kMaxCommand);
                const int first = comet::pixelAt(frame), steps = end - first;
                if (cont) minSteps = std::min(minSteps, steps);
                // Emulate four rings × 48: after each step exactly the chosen LEDs
                // at that angle are lit (previous angle fully erased).
                WireMessage m; CHECK(m.parse(payloadOf(cmd)));
                int ring[4][48] = {};
                size_t k = cont ? 0 : 1, painted = 0;
                for (int i = 0; i < steps; ++i, ++k) {
                    Bytes ob, fb; WireMessage obj, fill;
                    CHECK(k < m.fields.size() && sub(m, m.fields[k], ob) && obj.parse(ob) && sub(obj, obj.fields.back(), fb) && fill.parse(fb));
                    segsPerPaint = int(fill.fields.size());
                    for (const auto& f : fill.fields) {
                        uint32_t v = uint32_t(f.integer);
                        int px = v & 63, o = (v >> 6) & 3, h = ((v >> 14) & 3) + 1, len = ((v >> 8) & 63) + 1, c = (v >> 16) & 63;
                        CHECK(len == 1 && o + h <= 4 && px < 48);
                        for (int r = o; r < o + h; ++r) ring[r][px] = c;
                        ++painted;
                    }
                    const int angle = (first + i) % 48;
                    for (int r = 0; r < 4; ++r) for (int q = 0; q < 48; ++q) {
                        bool want = q == angle && (rings >> r & 1);
                        if (!want && q != (angle + 47) % 48 && !(i == 0)) continue;  // only angles this scene touched
                        CHECK((ring[r][q] != 0) == want);
                        if (want) CHECK(ring[r][q] == color);
                    }
                }
                CHECK(painted == size_t(steps * runs * 2));
            }
            CHECK(minSteps == comet::kSceneSteps[rings]);
            CHECK(comet::kSceneSteps[rings] == (runs == 1 ? 8 : 6) && segsPerPaint == runs * 2);
        }
        CHECK(comet::canRun(true, true, true, false, false, false, false, false, false));
        for (int reason = 0; reason < 9; ++reason) {
            bool args[9] = {true, true, true, false, false, false, false, false, false};
            args[reason] = !args[reason];
            CHECK(!comet::canRun(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]));
        }
        CHECK(comet::alertFace(8) && comet::alertFace(9) && comet::alertFace(10) && comet::alertFace(2) && comet::alertFace(3));
        CHECK(!comet::alertFace(0) && !comet::alertFace(0x88) && !comet::alertFace(-1));
    }
    // 1.22 night schedules: DND (field 1 + permanentDND 3) and Silent (field 14 +
    // permanentMute 4). Exhaustive modes × hours, preservation and refusal cases.
    {
        // A clock with DND scheduled 22–8, Silent unset, chime 6, time offset 18 = -90, unknown field 30.
        Bytes base = settings(128);
        { Bytes d; putInteger(d, 1, 1); putInteger(d, 2, 22); putInteger(d, 3, 8); putBytes(base, 1, d); }
        putInteger(base, 15, 6);
        putInteger(base, 18, uint64_t(int64_t(-90)));
        { Bytes u{7, 7}; putBytes(base, 30, u); }
        Settings s; CHECK(parseSettings(base, s));
        CHECK(s.dnd.known && s.dnd.mode == 1 && s.dnd.from == 22 && s.dnd.till == 8);
        CHECK(s.silent.known && s.silent.mode == 2);   // settings() sets field 4 (permanentMute) = 1
        CHECK(validSchedule(DND_SCHEDULE, 0, 22, 8) && validSchedule(SILENT_SCHEDULE, 2, 0, 0));
        CHECK(!validSchedule(DND_SCHEDULE, 1, 5, 5) && !validSchedule(DND_SCHEDULE, 3, 1, 2) && !validSchedule(DND_SCHEDULE, 1, 24, 2));
        CHECK(!validSchedule(DND_SCHEDULE, 1, -1, 2) && !validSchedule(2, 1, 1, 2) && !validSchedule(SILENT_SCHEDULE, 1, 1, 24));
        int checked = 0;
        for (int kind : {int(DND_SCHEDULE), int(SILENT_SCHEDULE)}) for (int mode = 0; mode <= 2; ++mode)
        for (int from = 0; from < 24; ++from) for (int till = 0; till < 24; ++till) {
            Bytes cmd;
            bool ok = patchSchedule(s, kind, mode, from, till, cmd);
            CHECK(ok == validSchedule(kind, mode, from, till));
            if (!ok) continue;
            ++checked;
            CHECK((Bytes(cmd.begin(), cmd.begin() + 4) == Bytes{5, 0, 0, 0}));
            Settings u; CHECK(parseSettings(payloadOf(cmd), u));
            CHECK(scheduleMatches(u, kind, mode, from, till));
            const Settings::Schedule& other = kind == DND_SCHEDULE ? u.silent : u.dnd;
            const Settings::Schedule& before = kind == DND_SCHEDULE ? s.silent : s.dnd;
            CHECK(other.mode == before.mode && other.from == before.from && other.till == before.till);   // the other schedule untouched
            WireMessage m; CHECK(m.parse(u.raw));
            CHECK(m.integerOr(15, 0) == 6 && m.integerOr(18, 0) == uint64_t(int64_t(-90)) && m.integerOr(10, 0) == 128 && m.integerOr(2, 9) == 0);
            int f1 = 0, f3 = 0, f4 = 0, f14 = 0, f30 = 0;
            for (const auto& f : m.fields) { f1 += f.number == 1; f3 += f.number == 3; f4 += f.number == 4; f14 += f.number == 14; f30 += f.number == 30; }
            CHECK(f1 == 1 && f30 == 1 && f4 == 1 && (kind == DND_SCHEDULE ? f3 == 1 : f14 == 1));
            for (const auto& f : m.fields) if (f.number == 30) CHECK((Bytes(m.raw.begin() + f.start, m.raw.begin() + f.end) == Bytes{0xf2, 0x01, 0x02, 7, 7}));   // unknown field 30 kept byte for byte
            // Silent "Always" is exactly the existing Mute sounds flag (field 4).
            if (kind == SILENT_SCHEDULE) CHECK(u.muted == (mode == 2));
        }
        CHECK(checked == 2 * 3 * 24 * 24 - 2 * 24);   // only Scheduled with from == till is refused
        // Every required sub-field is always sent, even for Off.
        { Bytes cmd; CHECK(patchSchedule(s, DND_SCHEDULE, 0, 22, 8, cmd)); WireMessage m; CHECK(m.parse(payloadOf(cmd)));
          for (const auto& f : m.fields) if (f.number == 1) { WireMessage in; CHECK(in.parse(Bytes(m.raw.begin() + f.start + 2, m.raw.begin() + f.end)));
              CHECK(in.integerOr(1, 9) == 0 && in.integerOr(2, 0) == 22 && in.integerOr(3, 0) == 8 && in.fields.size() == 3); } }
        // Absent schedule: defaults 22–8, Off; a malformed field refuses editing.
        { Settings plain; CHECK(parseSettings(settings(), plain)); CHECK(plain.dnd.known && plain.dnd.mode == 0 && plain.dnd.from == 22 && plain.dnd.till == 8); }
        { Bytes bad = settings(); putInteger(bad, 1, 5); Settings b; CHECK(parseSettings(bad, b) && !b.dnd.known);
          Bytes cmd; CHECK(!patchSchedule(b, DND_SCHEDULE, 1, 22, 8, cmd)); CHECK(patchSchedule(b, SILENT_SCHEDULE, 1, 22, 7, cmd)); }
        { Bytes bad = settings(); Bytes d; putInteger(d, 2, 30); putBytes(bad, 14, d); Settings b; CHECK(parseSettings(bad, b) && !b.silent.known); }
        Settings invalid; Bytes cmd; CHECK(!patchSchedule(invalid, DND_SCHEDULE, 1, 22, 8, cmd));
    }
    // Built-in hourly chime: Off=0, all 17 sounds, preserve time offset and unknown fields.
    {
        Bytes original = settings(0xab1edc00);
        putInteger(original, 15, 6);
        putInteger(original, 18, uint64_t(int64_t(-62)));
        Bytes unknown; putBytes(unknown, 30, {0, 255, 8});
        original = cat(original, unknown);
        Settings base; CHECK(parseSettings(original, base) && base.builtInChimeSound == 6);
        for (uint64_t sound = 0; sound <= 17; ++sound) {
            Bytes packet; CHECK(patchSettings(base, {{15, sound}}, {}, packet));
            Settings updated; CHECK(parseSettings(payloadOf(packet), updated));
            CHECK(updated.builtInChimeSound == sound && settingsMatch(updated, {{15, sound}}, {}));
            CHECK(!settingsMatch(updated, {{15, (sound + 1) % 18}}, {}));
            WireMessage m; CHECK(m.parse(updated.raw));
            CHECK(m.integerOr(18, 0) == uint64_t(int64_t(-62)));
            CHECK(updated.brightnessRaw == base.brightnessRaw && endsWith(updated.raw, unknown));
        }
        Bytes out; CHECK(!patchSettings(base, {{15, 18}}, {}, out));
        CHECK(!patchSettings(base, {{15, UINT64_MAX}}, {}, out));
        Settings absent; CHECK(parseSettings(settings(), absent) && absent.builtInChimeSound == 0);
        Bytes future = settings(); putInteger(future, 15, 99);
        Settings unknownSound; CHECK(parseSettings(future, unknownSound) && unknownSound.builtInChimeSound == 99);
        CHECK(patchSettings(unknownSound, {{2, 1}}, {}, out));
        WireMessage m; CHECK(m.parse(payloadOf(out)) && m.integerOr(15, 0) == 99);
        Bytes malformed = settings(); putBytes(malformed, 15, {6});
        CHECK(!parseSettings(malformed, absent));
        CHECK(validSettingValue(15, 17) && !validSettingValue(15, 18) && !validSettingValue(3, 1));
    }
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
    std::printf("PASS: 17 check groups (discovery, settings preservation, validation, brightness range, notices, timers, malformed packets, time zones/DST, hand state, weather forecasts, alarms, built-in hourly chime, night schedules, native seconds comet, seconds LED choice, fixed comet DND policy, movement/freshness regression).\n");
    return 0;
}
