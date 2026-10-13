// Ring-only seconds scene. Based on Hypfer's CustomScene schema and Snille's
// hardware-verified Fill -> Pulse sequencing. No clock settings/RTC writes.
#pragma once
#include "glance_protocol.h"

namespace comet {
using glance::Bytes;
constexpr int kPixels = 48, kFPS = 50, kMinuteFrames = 3000;
constexpr uint8_t kSlot = 1; // shares the bridge's weather slot, never two rotating scenes
// 1.19: stock firmware 1.6.7 re-initialises a scene whenever it is replaced
// (frame counter, ring framebuffer and the matrix time/date cycle), and it
// refuses long writes (user-memory reply NULL), so one command is one ATT write.
// Replacements can't be avoided, only made rare: ~8 steps per scene (~10 s).
// 1.20: a single ticking pixel. Owner test of 1.19 showed Result=Expired does NOT
// erase a Fill (the segments accumulated into a growing bar), so each step is ONE
// Fill that paints the new pixel AND paints the previous pixel black. Fills stay
// drawn after their lifeTime (Result OK), so no hold is needed for late handoffs.
constexpr int kWindowPixels = 8;
constexpr size_t kMaxCommand = 155;       // proven acknowledged single-write size
inline bool validColor(int color) { return color >= 1 && color <= 24; }
// 1.21: which of the 4 radial LEDs light, as a bitmask. Bit 0 = outer ring
// (segment offset 0) ... bit 3 = the LED nearest the centre (offset 3).
constexpr int kOuterRing = 1;
inline bool validRings(int rings) { return rings >= 1 && rings <= 15; }
// Minimum steps a normal handoff scene fits for each mask (all colours/frames;
// verified exhaustively by the host checks). Contiguous runs keep 8 steps
// (10 s); a gap needs two segments per paint, so 6 steps (7.5 s).
constexpr int kSceneSteps[16] = {0, 8, 8, 8, 8, 6, 8, 8, 8, 6, 6, 6, 8, 6, 8, 8};
inline bool validOptions(int on, int color) {
    return (on == 0 || on == 1) && validColor(color);
}
inline bool dndPauses(bool dndActive) { return dndActive; }
// Cached older pages may still submit this removed option. Validate but never
// apply it: DND is always respected, regardless of any old saved preference.
inline bool validLegacyDndOption(int value) { return value == 0 || value == 1; }
// A normal time adjustment is NOT homing. Ring scenes do not drive the motors.
// Keep real homing/confirmation guards; do not strand the comet on bit 256.
inline bool calibrationPauses(const glance::HandState& h, bool requested) {
    return h.homing || h.awaitingConfirmation || requested;
}
constexpr uint32_t kStateFreshMs = 12000;
inline bool stateFresh(bool valid, uint32_t now, uint32_t updated) {
    return valid && uint32_t(now - updated) <= kStateFreshMs;
}
inline int pixelAt(int frame) { return frame * kPixels / kMinuteFrames; }
inline int boundaryFrame(int pixel) { return (pixel * kMinuteFrames + kPixels - 1) / kPixels; }
// Segment value (Hypfer's 22-bit area): start pixel, then (length - 1) at bit 8,
// colour at bit 16. Offset/height fields zero = one LED on the outer ring.
// Colour 0 is Black.
inline uint32_t segment(int start, int extra, int color) {
    return uint32_t(start) | (uint32_t(extra) << 8) | (uint32_t(color) << 16);
}
// One radial run at one angle: offset = first ring, height field = rings - 1.
inline uint32_t radial(int pixel, int offset, int heightField, int color) {
    return segment(pixel, 0, color) | (uint32_t(offset) << 6) | (uint32_t(heightField) << 14);
}
// Splits a ring mask into contiguous runs (at most 2 for 4 bits).
inline int ringRuns(int rings, int offset[2], int heightField[2]) {
    int n = 0;
    for (int b = 0; b < 4 && n < 2;) {
        if (!(rings >> b & 1)) { ++b; continue; }
        int lo = b;
        while (b < 4 && (rings >> b & 1)) ++b;
        offset[n] = lo; heightField[n] = b - 1 - lo; ++n;
    }
    return n;
}
inline Bytes object(int method, int at, int life, int result, int field, const Bytes& data) {
    Bytes out;
    glance::putInteger(out, 1, method);
    if (at) glance::putInteger(out, 2, at);           // optional, default 0
    glance::putInteger(out, 3, life);
    if (result) glance::putInteger(out, 4, result);   // optional, default OK
    glance::putBytes(out, field, data);
    return out;
}
// Builds one command for `steps` steps starting at `frame`, or returns false.
inline bool build(int frame, int color, bool continuation, int steps, Bytes& out, int rings = kOuterRing) {
    const int first = pixelAt(frame);
    int offset[2], heightField[2];
    const int runs = ringRuns(rings, offset, heightField);
    Bytes body;
    if (!continuation) {
        // Taking over (from weather or after a pause): blacken all four rings once.
        Bytes clear; glance::putInteger(clear, 1, (47u << 8) | (3u << 14));
        glance::putBytes(body, 1, object(2, 0, 1, 0, 6, clear));
    }
    for (int i = 0; i < steps; ++i) {
        const int p = (first + i) % kPixels, previous = (p + kPixels - 1) % kPixels;
        const int at = i == 0 ? 0 : boundaryFrame(first + i) - frame;
        // One frame is enough: a Fill stays drawn until something paints over it.
        // The black previous pixel also covers a replacement that kept the old frame.
        // Mask 1 (outer only) is byte-identical to 1.20.
        Bytes fill;
        for (int r = 0; r < runs; ++r) glance::putInteger(fill, 1, radial(p, offset[r], heightField[r], color));
        for (int r = 0; r < runs; ++r) glance::putInteger(fill, 1, radial(previous, offset[r], heightField[r], 0));
        glance::putBytes(body, 1, object(2, at, 1, 0, 6, fill));
    }
    out = {0, 0, 8, kSlot}; // watchface overlay: do not replace matrix time/date
    out.insert(out.end(), body.begin(), body.end());
    return out.size() <= kMaxCommand;
}
// One scene covering the next kWindowPixels steps from `frame` (crossing the
// minute if needed), as many as fit in a single write. `endPixel` (exclusive,
// may exceed 47 when the scene runs into the next minute) is the first step NOT covered.
inline bool scene(int frame, int color, Bytes& out, bool continuation = false, int* endPixel = nullptr, int rings = kOuterRing) {
    out.clear();
    if (frame < 0 || frame >= kMinuteFrames || !validColor(color) || !validRings(rings)) return false;
    const int first = pixelAt(frame);
    for (int steps = kWindowPixels; steps >= 1; --steps) {
        if (build(frame, color, continuation, steps, out, rings)) {
            if (endPixel) *endPixel = first + steps;
            return true;
        }
    }
    out.clear();
    return false;
}

// Only recognised, active factory alert faces block us. Bit 7 = inactive.
inline bool alertFace(int sceneByte) {
    if (sceneByte < 0 || sceneByte > 255 || (sceneByte & 128)) return false;
    return sceneByte == 2 || sceneByte == 3 || (sceneByte >= 8 && sceneByte <= 10);
}
// Shared policy makes important pause/error cases testable without Bluetooth.
inline bool canRun(bool enabled, bool connected, bool validTime, bool handsBusy,
                   bool timer, bool dnd, bool alert, bool cooldown, bool failed) {
    return enabled && connected && validTime && !handsBusy && !timer && !dnd &&
           !alert && !cooldown && !failed;
}
} // namespace comet
