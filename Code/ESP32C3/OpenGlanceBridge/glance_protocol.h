// Glance Clock protocol helpers. Pure C++ (no Arduino headers) so the same
// code is exercised by the host-side checks in ../tests/protocol_checks.cpp.
// Ported from the verified Swift GlanceProtocol used by OpenGlance 0.21–0.27.
#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace glance {

using Bytes = std::vector<uint8_t>;

static const char* const kServiceUUID = "5075f606-1e0e-11e7-93ae-92361f002671";
static const char* const kControlUUID = "5075fb2e-1e0e-11e7-93ae-92361f002671";
static const char* const kStateUUID   = "5075fc78-1e0e-11e7-93ae-92361f002671";

// ---------------------------------------------------------------- wire format
inline void putVarint(Bytes& out, uint64_t value) {
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (value) byte |= 0x80;
        out.push_back(byte);
    } while (value);
}
inline void putInteger(Bytes& out, uint32_t field, uint64_t value) {
    putVarint(out, uint64_t(field) << 3);
    putVarint(out, value);
}
inline void putBytes(Bytes& out, uint32_t field, const Bytes& value) {
    putVarint(out, (uint64_t(field) << 3) | 2);
    putVarint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

// Retains every original field, including unknown ones. We never rebuild a
// clock's settings from guessed defaults.
struct WireMessage {
    struct Field {
        uint32_t number;
        uint8_t wireType;
        size_t start, end;
        bool hasInteger;
        uint64_t integer;
    };
    Bytes raw;
    std::vector<Field> fields;

    bool parse(const Bytes& data) {
        raw = data;
        fields.clear();
        if (data.size() > 65536) return false;
        size_t cursor = 0;
        auto varint = [&](uint64_t& out) -> bool {
            out = 0;
            for (int i = 0; i < 10; i++) {
                if (cursor >= data.size()) return false;
                uint8_t byte = data[cursor++];
                if (i == 9 && byte > 1) return false;
                out |= uint64_t(byte & 0x7f) << (7 * i);
                if (!(byte & 0x80)) return true;
            }
            return false;
        };
        auto skip = [&](uint64_t count) -> bool {
            if (count > data.size() - cursor) return false;
            cursor += count;
            return true;
        };
        while (cursor < data.size()) {
            size_t start = cursor;
            uint64_t key;
            if (!varint(key)) return false;
            uint64_t number = key >> 3;
            uint8_t type = key & 7;
            if (number == 0 || number > 536870911ULL) return false;
            Field f{uint32_t(number), type, start, 0, false, 0};
            switch (type) {
                case 0: if (!varint(f.integer)) return false; f.hasInteger = true; break;
                case 1: if (!skip(8)) return false; break;
                case 2: { uint64_t len; if (!varint(len) || !skip(len)) return false; break; }
                case 5: if (!skip(4)) return false; break;
                default: return false;  // unsupported wire type: refuse to edit
            }
            f.end = cursor;
            fields.push_back(f);
        }
        return true;
    }
    bool integer(uint32_t number, uint64_t& out) const {
        bool found = false;
        for (const auto& f : fields)
            if (f.number == number && f.hasInteger) { out = f.integer; found = true; }
        return found;
    }
    uint64_t integerOr(uint32_t number, uint64_t fallback) const {
        uint64_t v;
        return integer(number, v) ? v : fallback;
    }
    Bytes replacingIntegers(const std::map<uint32_t, uint64_t>& changes) const {
        Bytes out;
        std::map<uint32_t, bool> written;
        for (const auto& f : fields) {
            auto it = changes.find(f.number);
            if (it != changes.end()) {
                if (!written[f.number]) { putInteger(out, f.number, it->second); written[f.number] = true; }
            } else {
                out.insert(out.end(), raw.begin() + f.start, raw.begin() + f.end);
            }
        }
        for (const auto& c : changes)
            if (!written[c.first]) putInteger(out, c.first, c.second);
        return out;
    }
};

// Identify clocks only from the real advertised name or the Glance service
// UUID. Never assign an unnamed device a clock identity.
inline bool isGlance(const std::string& name, bool advertisesGlanceService) {
    std::string lower;
    for (char ch : name) lower += char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    return lower.find("glance") != std::string::npos || advertisesGlanceService;
}

// ------------------------------------------------------------------ settings
struct Settings {
    bool valid = false;
    Bytes raw;              // payload without any response prefix
    uint64_t brightnessRaw = 0;
    int brightness = 0;     // byte 0: 0 = automatic, 1–255 = fixed
    int autoLow = 0;        // byte 2
    int autoHigh = 0;       // byte 1
    bool nightMode = false, digitalTime = false, twelveHour = false;
    bool hourPoints = false, muted = false;
    int dateFormat = 0;
    uint64_t builtInChimeSound = 0; // field 15 charmSound; 0 = Off, 1–17 = Sound enum
    // 1.22 night schedules. Firmware 1.6.7 (0x1F982 DND / 0x1FA02 Silent):
    // permanent flag -> always; else `recurring` -> active while from <= hour < till
    // (wrapping past midnight); recurring false -> off (compared against an absolute
    // timestamp, so plain hours never match).
    struct Schedule {
        bool known = false;   // false when the clock's field is malformed: refuse editing
        int mode = 0;         // 0 Off, 1 Scheduled, 2 Always
        int from = 22, till = 8;  // original app defaults when the clock has none
    };
    Schedule dnd;     // field 1 {recurring, fromHour, tillHour} + field 3 permanentDND
    Schedule silent;  // field 14 {recurring, fromHour, tillHour} + field 4 permanentMute
};
enum ScheduleKind { DND_SCHEDULE = 1, SILENT_SCHEDULE = 14 };
inline uint32_t permanentFieldFor(int kind) { return kind == DND_SCHEDULE ? 3 : 4; }
inline bool validSchedule(int kind, int mode, int from, int till) {
    return (kind == DND_SCHEDULE || kind == SILENT_SCHEDULE) && mode >= 0 && mode <= 2 &&
           from >= 0 && from <= 23 && till >= 0 && till <= 23 && (mode != 1 || from != till);
}
// Reads one schedule from a parsed Settings message.
inline Settings::Schedule readSchedule(const WireMessage& m, int kind) {
    Settings::Schedule out;
    bool always = m.integerOr(permanentFieldFor(kind), 0) != 0;
    bool recurring = false, malformed = false;
    for (const auto& f : m.fields) {
        if (f.number != uint32_t(kind)) continue;
        WireMessage inner;
        size_t i = f.start;
        while (i < f.end && (m.raw[i] & 0x80)) ++i;   // key
        ++i;
        while (i < f.end && (m.raw[i] & 0x80)) ++i;   // length
        ++i;
        if (f.wireType != 2 || i > f.end || !inner.parse(Bytes(m.raw.begin() + i, m.raw.begin() + f.end))) { malformed = true; continue; }
        recurring = inner.integerOr(1, 0) != 0;
        uint64_t from = inner.integerOr(2, uint64_t(out.from)), till = inner.integerOr(3, uint64_t(out.till));
        if (from > 23 || till > 23) { malformed = true; continue; }
        out.from = int(from); out.till = int(till);
    }
    out.known = !malformed;
    out.mode = always ? 2 : recurring ? 1 : 0;
    return out;
}

inline bool isEditableField(uint32_t f) {
    return f == 2 || f == 4 || f == 5 || f == 9 || f == 10 || f == 11 || f == 12 || f == 15;
}
inline bool validSettingValue(uint32_t field, uint64_t value) {
    if (!isEditableField(field)) return false;
    if (field == 10) return value <= 255;
    if (field == 5) return value <= 4;
    if (field == 15) return value <= 17;
    return value <= 1;
}

inline bool parseSettings(const Bytes& data, Settings& s) {
    s = Settings();
    Bytes payload = data;
    static const uint8_t dataPrefix[5] = {68, 97, 116, 97, 0};  // "Data\0"
    if (payload.size() >= 5 && memcmp(payload.data(), dataPrefix, 5) == 0) payload.erase(payload.begin(), payload.begin() + 5);
    else if (!payload.empty() && payload[0] == 5) payload.erase(payload.begin());
    WireMessage m;
    if (!m.parse(payload)) return false;
    int recognised = 0;
    for (const auto& f : m.fields) {
        uint32_t n = f.number;
        if ((n == 2 || n == 4 || n == 5 || n == 9 || n == 10 || n == 11 || n == 12 || n == 13) && f.wireType == 0) recognised++;
        if (n == 2 || n == 3 || n == 4 || n == 9 || n == 11 || n == 12) {
            if (f.wireType != 0 || !f.hasInteger || f.integer > 1) return false;
        }
    }
    if (recognised < 3) return false;
    if (m.integerOr(5, 0) > 4) return false;
    for (const auto& f : m.fields) if (f.number == 15 && f.wireType != 0) return false;
    s.raw = payload;
    s.brightnessRaw = m.integerOr(10, 0);
    s.brightness = int(s.brightnessRaw & 255);
    s.autoHigh = int((s.brightnessRaw >> 8) & 255);
    s.autoLow = int((s.brightnessRaw >> 16) & 255);
    s.nightMode = m.integerOr(2, 0) != 0;
    s.muted = m.integerOr(4, 0) != 0;
    s.dateFormat = int(m.integerOr(5, 0));
    s.hourPoints = m.integerOr(9, 0) != 0;
    s.digitalTime = m.integerOr(11, 0) != 0;
    s.twelveHour = m.integerOr(12, 0) != 0;
    s.builtInChimeSound = m.integerOr(15, 0);
    s.dnd = readSchedule(m, DND_SCHEDULE);
    s.silent = readSchedule(m, SILENT_SCHEDULE);
    s.valid = true;
    return true;
}

struct BrightnessRange {
    bool present = false;
    int low = 0, high = 0;
    uint64_t encoded() const { return (uint64_t(low) << 16) | (uint64_t(high) << 8); }
};

inline bool validRange(int low, int high) {
    return low >= 0 && low <= 255 && high >= 0 && high <= 255 && low <= high;
}

// Builds the full Settings write command (header 5,0,0,0 + payload). Only the
// low three brightness bytes are replaced; higher bits and unrelated fields stay.
inline bool patchSettings(const Settings& base, const std::map<uint32_t, uint64_t>& changes,
                          const BrightnessRange& range, Bytes& command) {
    if (!base.valid) return false;
    if (changes.empty() && !range.present) return false;
    if (range.present && (changes.count(10) || !validRange(range.low, range.high))) return false;
    for (const auto& c : changes) {
        if (!validSettingValue(c.first, c.second)) return false;
    }
    WireMessage m;
    if (!m.parse(base.raw)) return false;
    std::map<uint32_t, uint64_t> merged = changes;
    uint64_t current = m.integerOr(10, 0);
    if (changes.count(10)) merged[10] = (current & ~uint64_t(255)) | changes.at(10);
    if (range.present) merged[10] = (current & ~uint64_t(0xffffff)) | range.encoded();
    command = {5, 0, 0, 0};
    Bytes body = m.replacingIntegers(merged);
    command.insert(command.end(), body.begin(), body.end());
    return true;
}

// Builds a Settings write changing ONE night schedule: its sub-message (all three
// required fields, as the firmware's nanopb table demands) and its permanent flag.
// Every other field, known or unknown, is resent byte-for-byte.
inline bool patchSchedule(const Settings& base, int kind, int mode, int from, int till, Bytes& command) {
    if (!base.valid || !validSchedule(kind, mode, from, till)) return false;
    if (!(kind == DND_SCHEDULE ? base.dnd.known : base.silent.known)) return false;
    WireMessage m;
    if (!m.parse(base.raw)) return false;
    Bytes inner;
    putInteger(inner, 1, mode == 1 ? 1 : 0);
    putInteger(inner, 2, uint64_t(from));
    putInteger(inner, 3, uint64_t(till));
    const uint32_t flag = permanentFieldFor(kind);
    Bytes body;
    bool wroteSchedule = false, wroteFlag = false;
    for (const auto& f : m.fields) {
        if (f.number == uint32_t(kind)) {
            if (!wroteSchedule) { putBytes(body, uint32_t(kind), inner); wroteSchedule = true; }
        } else if (f.number == flag) {
            if (!wroteFlag) { putInteger(body, flag, mode == 2 ? 1 : 0); wroteFlag = true; }
        } else {
            body.insert(body.end(), m.raw.begin() + f.start, m.raw.begin() + f.end);
        }
    }
    if (!wroteSchedule) putBytes(body, uint32_t(kind), inner);
    if (!wroteFlag) putInteger(body, flag, mode == 2 ? 1 : 0);
    command = {5, 0, 0, 0};
    command.insert(command.end(), body.begin(), body.end());
    return true;
}
inline bool scheduleMatches(const Settings& s, int kind, int mode, int from, int till) {
    if (!s.valid) return false;
    const Settings::Schedule& x = kind == DND_SCHEDULE ? s.dnd : s.silent;
    return x.known && x.mode == mode && x.from == from && x.till == till;
}

inline bool settingsMatch(const Settings& s, const std::map<uint32_t, uint64_t>& changes, const BrightnessRange& range) {
    if (!s.valid) return false;
    WireMessage m;
    if (!m.parse(s.raw)) return false;
    if (range.present && (m.integerOr(10, 0) & 0xffffff) != range.encoded()) return false;
    for (const auto& c : changes) {
        uint64_t actual = m.integerOr(c.first, 0);
        if ((c.first == 10 ? (actual & 255) : actual) != c.second) return false;
    }
    return true;
}

// ------------------------------------------------------------------ commands
inline bool validText(const std::string& text) {
    if (text.empty() || text.size() > 64) return false;
    for (unsigned char ch : text) if (ch < 32 || ch > 126) return false;
    return true;
}
inline Bytes textData(const std::string& text) {
    Bytes out;
    putInteger(out, 1, 0);
    putBytes(out, 2, Bytes(text.begin(), text.end()));
    return out;
}
inline bool validAnimation(int a) { return a == 0 || a == 1 || a == 2 || a == 10 || a == 11 || a == 12 || a == 15 || a == 17; }
// Sound enum from the Glance protobuf schema (0 none, 1 Waves … 17 Extension).
inline bool validSound(int s) { return s >= 0 && s <= 17; }
inline bool validColor(int c) { return c == 5 || c == 12 || c == 15 || c == 18 || c == 23; }

inline bool notice(const std::string& text, int animation, int sound, int color, Bytes& out) {
    if (!validText(text) || !validAnimation(animation) || !validSound(sound) || !validColor(color)) return false;
    out = {2, 16, 0, 0};
    putInteger(out, 1, animation);
    putInteger(out, 2, sound);
    putInteger(out, 3, color);
    putBytes(out, 4, textData(text));
    return true;
}
inline bool timer(int seconds, Bytes& out, const std::string& finalText = "Time's up!") {
    if (seconds < 60 || seconds > 43200) return false;
    // countdown (field 1) is the pre-timer count, NOT the timer duration.
    Bytes interval;
    putInteger(interval, 2, seconds);
    putInteger(interval, 3, 0);
    out = {3, 0, 0, 0};
    putInteger(out, 1, 0);
    putBytes(out, 2, interval);
    putBytes(out, 3, textData(finalText));
    return true;
}
static const uint8_t kStopTimer[] = {10};
static const uint8_t kStartCalibration[] = {43};
static const uint8_t kConfirmCalibration[] = {44};

// -------------------------------------------------------------------- alarms
// From the original app (APK 1.5.1 modules 819/1991/1992): the whole list is
// sent at once as 4,47,0,0 + Alarms{repeated AlarmData alarm = 1}, at most 4;
// an empty list is sent as AlarmsClear (21). Days: Mon=1 … Sun=64, omitted for
// a one-time alarm. AlarmData: days=3, time=4 {hours=1, minutes=2}, sound=5,
// text=6 (TextData{text=2}). `enabled` (2) is left at its default, as the app did.
static const int kMaxAlarms = 4;
static const size_t kMaxAlarmText = 16;   // keeps 4 alarms inside one Bluetooth write
struct Alarm {
    int hour = 7, minute = 0;
    int days = 0;          // bitmask Mon=1 … Sun=64; 0 = once
    int sound = 6;
    std::string text;
};
inline bool validAlarm(const Alarm& a) {
    if (a.hour < 0 || a.hour > 23 || a.minute < 0 || a.minute > 59 || a.days < 0 || a.days > 127) return false;
    if (a.sound < 0 || a.sound > 17 || a.text.size() > kMaxAlarmText) return false;
    for (unsigned char ch : a.text) if (ch < 32 || ch > 126) return false;
    return true;
}
inline bool alarmsCommand(const std::vector<Alarm>& alarms, Bytes& out) {
    if (alarms.size() > size_t(kMaxAlarms)) return false;
    if (alarms.empty()) { out = {21, 0, 0, 0}; return true; }
    Bytes list;
    for (const auto& a : alarms) {
        if (!validAlarm(a)) return false;
        Bytes data, time;
        if (a.days) putInteger(data, 3, a.days);
        putInteger(time, 1, a.hour);
        putInteger(time, 2, a.minute);
        putBytes(data, 4, time);
        putInteger(data, 5, a.sound);
        Bytes text;
        putBytes(text, 2, Bytes(a.text.begin(), a.text.end()));
        putBytes(data, 6, text);
        putBytes(list, 1, data);
    }
    out = {4, 47, 0, 0};
    out.insert(out.end(), list.begin(), list.end());
    return true;
}
static const uint8_t kAlarmStop[] = {20, 0, 0, 0};

// --------------------------------------------------------------- hand state
struct HandState {
    bool valid = false;
    uint16_t bits = 0;
    bool homing = false, awaitingConfirmation = false, homingFailure = false, motorFailure = false;
    bool timeAdjusting = false, dnd = false;
};
inline HandState decodeHandState(const uint8_t* data, size_t len) {
    HandState h;
    if (len != 1 && len != 2) return h;
    h.bits = data[0] | (len == 2 ? uint16_t(data[1]) << 8 : 0);
    h.homing = h.bits & 128;
    h.awaitingConfirmation = h.bits & 1024;
    h.homingFailure = h.bits & 64;
    h.motorFailure = h.bits & 2048;
    h.timeAdjusting = h.bits & 256;
    h.dnd = h.bits & 16;
    h.valid = true;
    return h;
}

// ------------------------------------------------------- current time (CTS)
// Offset of local time from UTC, in seconds, for the process TZ.
inline long utcOffsetSeconds(time_t now) {
    struct tm l, g;
    localtime_r(&now, &l);
    gmtime_r(&now, &g);
    long diff = (l.tm_hour - g.tm_hour) * 3600L + (l.tm_min - g.tm_min) * 60L + (l.tm_sec - g.tm_sec);
    int dayDelta = (l.tm_year != g.tm_year) ? (l.tm_year > g.tm_year ? 1 : -1) : (l.tm_yday - g.tm_yday);
    return diff + dayDelta * 86400L;
}

// 10-byte Current Time (0x2A2B). An invalid (unsynchronised) time is sent as
// year 0, which the Bluetooth specification defines as "not known".
inline Bytes currentTimeValue(time_t now, bool valid, uint8_t fraction256, uint8_t reason) {
    if (!valid) return Bytes(10, 0);
    struct tm l;
    localtime_r(&now, &l);
    int year = l.tm_year + 1900;
    uint8_t weekday = l.tm_wday == 0 ? 7 : l.tm_wday;  // Monday=1 … Sunday=7
    return Bytes{uint8_t(year & 255), uint8_t(year >> 8), uint8_t(l.tm_mon + 1), uint8_t(l.tm_mday),
                 uint8_t(l.tm_hour), uint8_t(l.tm_min), uint8_t(l.tm_sec), weekday, fraction256, reason};
}

// 2-byte Local Time Information (0x2A0F): standard offset in quarter hours and
// DST offset code (0 standard, 2 +0.5 h, 4 +1 h, 8 +2 h).
inline Bytes localTimeInformation(time_t now) {
    struct tm l;
    localtime_r(&now, &l);
    long offset = utcOffsetSeconds(now);
    long daylight = l.tm_isdst > 0 ? 3600 : 0;
    long quarters = (offset - daylight) / 900;
    if (quarters < -48) quarters = -48;
    if (quarters > 56) quarters = 56;
    return Bytes{uint8_t(int8_t(quarters)), uint8_t(daylight ? 4 : 0)};
}

}  // namespace glance
