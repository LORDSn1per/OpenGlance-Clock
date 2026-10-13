// Weather widgets for the Glance ring: hourly temperature and rain forecasts.
// Pure C++ (no Arduino headers) so ../tests/protocol_checks.cpp can test it.
//
// Data: Open-Meteo (free, no key; CC BY 4.0), requested with timeformat=unixtime.
// Display: the clock's ForecastScene (command 7, as used by the hardware-tested
// glance_clock_ha integration): 24 Int16LE hourly values from a local-time
// timestamp, drawn on the ring between min and max with a min→max colour, plus a
// text template where 0x08 is replaced by the current value.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include "glance_protocol.h"

namespace weather {

using glance::Bytes;

static const int kMaxHours = 48;
// One weather scene slot only, so the clock never cycles scenes by itself; the
// bridge alternates temperature/rain on the owner's chosen interval instead.
static const uint8_t kWeatherSlot = 1;
static const uint8_t kRainSlot = 2;      // used by 1.06/1.07; now only cleaned up

struct Forecast {
    bool valid = false;
    int count = 0;
    time_t hour[kMaxHours] = {};
    float temp[kMaxHours] = {};
    int chance[kMaxHours] = {};   // precipitation probability, %
    float rain[kMaxHours] = {};   // precipitation, mm in that hour
    bool hasCurrent = false;
    float currentTemp = 0;
};

// Finds `"key":[` inside the object that starts at `objectStart` and parses up to
// kMaxHours numbers (null → NAN). Returns the count, or -1 if the key is missing.
inline int parseArray(const std::string& json, size_t objectStart, const char* key, double* out) {
    std::string needle = std::string("\"") + key + "\":[";
    size_t p = json.find(needle, objectStart);
    if (p == std::string::npos) return -1;
    p += needle.size();
    int n = 0;
    while (p < json.size() && json[p] != ']' && n < kMaxHours) {
        while (p < json.size() && (json[p] == ' ' || json[p] == ',')) p++;
        if (p >= json.size() || json[p] == ']') break;
        if (json.compare(p, 4, "null") == 0) { out[n++] = NAN; p += 4; continue; }
        char* end = nullptr;
        double v = strtod(json.c_str() + p, &end);
        if (end == json.c_str() + p) return -1;
        out[n++] = v;
        p = end - json.c_str();
    }
    return n;
}

inline bool parseOpenMeteo(const std::string& json, Forecast& f) {
    f = Forecast();
    size_t hourly = json.find("\"hourly\":{");
    if (hourly == std::string::npos) return false;
    double times[kMaxHours], temps[kMaxHours], chances[kMaxHours], rains[kMaxHours];
    int nt = parseArray(json, hourly, "time", times);
    int n1 = parseArray(json, hourly, "temperature_2m", temps);
    int n2 = parseArray(json, hourly, "precipitation_probability", chances);
    int n3 = parseArray(json, hourly, "precipitation", rains);
    if (nt <= 0 || n1 != nt || n2 != nt || n3 != nt) return false;
    for (int i = 0; i < nt; i++) {
        if (std::isnan(times[i])) return false;
        f.hour[i] = time_t(times[i]);
        f.temp[i] = std::isnan(temps[i]) ? (i ? f.temp[i - 1] : 0) : float(temps[i]);
        f.chance[i] = std::isnan(chances[i]) ? 0 : int(lround(chances[i]));
        f.rain[i] = std::isnan(rains[i]) ? 0 : float(rains[i]);
    }
    f.count = nt;
    size_t current = json.find("\"current\":{");
    if (current != std::string::npos) {
        size_t p = json.find("\"temperature_2m\":", current);
        size_t close = json.find('}', current);
        if (p != std::string::npos && p < close) {
            char* end = nullptr;
            const char* s = json.c_str() + p + strlen("\"temperature_2m\":");
            double v = strtod(s, &end);
            if (end != s) { f.hasCurrent = true; f.currentTemp = float(v); }
        }
    }
    f.valid = true;
    return true;
}

inline uint32_t mix(uint32_t a, uint32_t b, double t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    auto ch = [&](int shift) { return uint32_t(lround(((a >> shift) & 255) + (((int)((b >> shift) & 255) - (int)((a >> shift) & 255)) * t))) << shift; };
    return ch(16) | ch(8) | ch(0);
}

// Index of the hour containing `now`, or -1 when the forecast does not cover it.
inline int startIndex(const Forecast& f, time_t now) {
    for (int i = 0; i < f.count; i++)
        if (f.hour[i] <= now && now < f.hour[i] + 3600) return i;
    return -1;
}

struct Scene {
    int64_t timestamp = 0;   // local time expressed as a Unix timestamp
    uint32_t maxColor = 0, minColor = 0;
    int32_t max = 0, min = 0;
    int16_t values[24] = {};
    Bytes templ;
};

inline Bytes encodeScene(const Scene& s) {
    Bytes out;
    glance::putInteger(out, 1, uint64_t(s.timestamp));
    glance::putInteger(out, 4, s.maxColor);
    glance::putInteger(out, 5, s.minColor);
    glance::putInteger(out, 6, uint64_t(int64_t(s.max)));   // negative int32 → 10-byte varint
    glance::putInteger(out, 7, uint64_t(int64_t(s.min)));
    Bytes values;
    for (int16_t v : s.values) { values.push_back(uint8_t(v & 255)); values.push_back(uint8_t((uint16_t(v) >> 8) & 255)); }
    glance::putBytes(out, 8, values);
    glance::putBytes(out, 9, s.templ);
    return out;
}

// SaveForecastScene: 7, priority 16 (band medium), display (8 = ring only, which
// keeps the time/date on the matrix; 24 = ring and text), slot.
inline Bytes forecastCommand(uint8_t slot, const Scene& s, bool ringOnly = true) {
    Bytes out{7, 16, uint8_t(ringOnly ? 8 : 24), slot};
    Bytes body = encodeScene(s);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
inline Bytes deleteSceneCommand(uint8_t slot) { return Bytes{33, 0, 0, slot}; }

// Temperature colour scale: blue → green → amber → red across coldAt…hotAt.
inline uint32_t palette(double t) {
    static const uint32_t stops[] = {0x2060FF, 0x00C070, 0xFFC000, 0xFF3000};
    if (t <= 0) return stops[0];
    if (t >= 1) return stops[3];
    double x = t * 3;
    int i = int(x);
    return mix(stops[i], stops[i + 1], x - i);
}

struct TemperatureOptions {
    bool fahrenheit = false;
    int coldAt = 5, hotAt = 35;                 // colour scale, in the display unit
};

// Temperature ring: next 24 hours from the current hour; the current hour uses
// the live temperature when available (as glance_clock_ha does).
inline bool temperatureScene(const Forecast& f, time_t now, long utcOffset, const TemperatureOptions& o, Scene& s) {
    int start = startIndex(f, now);
    if (!f.valid || start < 0) return false;
    s = Scene();
    s.timestamp = int64_t(f.hour[start]) + utcOffset;
    int lo = 0, hi = 0;
    for (int i = 0; i < 24; i++) {
        int idx = start + i < f.count ? start + i : f.count - 1;
        float t = (i == 0 && f.hasCurrent) ? f.currentTemp : f.temp[idx];
        int v = int(lround(t));
        s.values[i] = int16_t(v);
        if (i == 0 || v < lo) lo = v;
        if (i == 0 || v > hi) hi = v;
    }
    if (hi == lo) hi = lo + 1;
    s.min = lo;
    s.max = hi;
    double span = o.hotAt > o.coldAt ? double(o.hotAt - o.coldAt) : 1.0;
    s.minColor = palette((lo - o.coldAt) / span);
    s.maxColor = palette((hi - o.coldAt) / span);
    s.templ = {0xC2, 0x8F, 0x08, 0xC2, 0xB0, uint8_t(o.fahrenheit ? 'F' : 'C')};  // thermometer, value, °C/°F
    return true;
}

struct RainOptions {
    float thresholdMm = 0.3f;                   // an hour counts only at or above this amount…
    int minChance = 40;                         // …and at least this chance of rain (%)
    int lookaheadHours = 6;                     // rain is "coming" only within this window
    uint32_t lightColor = 0x7FD4FF, heavyColor = 0x0030FF;
};

// Rain ring: chance of rain per hour, but only for hours with at least the
// threshold amount AND the minimum chance. Returns false (no rain coming) when
// no hour in the look-ahead window qualifies.
inline bool rainScene(const Forecast& f, time_t now, long utcOffset, const RainOptions& o, Scene& s) {
    int start = startIndex(f, now);
    if (!f.valid || start < 0) return false;
    s = Scene();
    s.timestamp = int64_t(f.hour[start]) + utcOffset;
    bool any = false;
    for (int i = 0; i < 24; i++) {
        int idx = start + i;
        int v = 0;
        if (idx < f.count && f.rain[idx] >= o.thresholdMm - 1e-6f && f.chance[idx] >= o.minChance) v = f.chance[idx] < 10 ? 10 : f.chance[idx];
        if (v > 100) v = 100;
        s.values[i] = int16_t(v);
        if (v > 0 && i < o.lookaheadHours) any = true;
    }
    s.min = 0;
    s.max = 100;
    s.minColor = o.lightColor;
    s.maxColor = o.heavyColor;
    s.templ = {0xC2, 0x95, 0x08, '%'};  // umbrella, value, %
    return any;
}

}  // namespace weather
