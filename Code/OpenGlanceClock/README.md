# OpenGlance Clock

A calm, cloud-free native Mac companion for the original Glance Clock, with a softer slate/charcoal-and-teal theme. Requires macOS 14 or newer; the supplied app is built for Apple Silicon. See Hardware-status.md for the owner's latest test results.

## Project storage and releases

Active code: `Code/OpenGlanceClock`.
Programming references: the sibling `Code/References` folder.
Compiled releases: `MAC/` (not in the repository; see Releases).

Numbered compiled apps are preserved in MAC. Each completed build advances by exactly 0.01. The build script creates only a numbered `.app`, never ZIPs, and never overwrites or removes older versions. The app footer reads the actual packaged version. Builds use a temporary local copy/cache to avoid network-volume compilation problems. The running project record is the root `../../AGENTS.md`.

Version 0.2 fixes unnamed Bluetooth devices appearing as clocks. The connection panel has a fixed outer size, scrollable results, a persistent Close button (Escape), and a persistent Quit button. Command-Q quits even with the connection sheet open, and closing the last window exits the app.

## Connect your clock

1. Open the newest numbered **OpenGlance Clock** app. A previously saved clock reconnects automatically by default; first-time setup still requires choosing a clock.
2. Choose **Connect clock**, keep the clock nearby, and close the original phone app if it is connected. **Briefly press the clock's single button to activate pairing mode.** Use a short press, not a long hold or reset. This was confirmed by the owner on their clock when pairing with OpenGlance Clock.
3. Choose **Search for clock** and allow macOS Bluetooth access.
4. Select your clock and choose **Connect**. If pairing mode has expired, briefly press the clock's button again before connecting. If macOS asks, enter the PIN displayed on the clock.
5. Successful protected settings access confirms the connection. The header shows Connected, Disconnected, Connecting or Authenticating. The app remembers your selection. Turn off automatic reconnection in Time & date if preferred; choosing Disconnect suspends retries for the current session until a new manual connection attempt.

Automatic connection attempts target only the saved clock, never an arbitrary nearby device. Unexpected disconnection permits up to three delayed reconnection attempts (30, 60 and 90 seconds); clock commands are not replayed. Demo and screenshot modes never start automatic Bluetooth connections.

If nothing appears or connection times out, check power and proximity, briefly press the clock's button, and try again. Bluetooth permission is managed in System Settings → Privacy & Security → Bluetooth. Firmware and existing pairing bonds may affect the macOS PIN flow. This build never erases bonds, resets the clock, or flashes firmware. “Forget clock” removes only this app's saved selection.

## Included

- Simple clock dashboard, brightness/automatic brightness, digital display, 12/24-hour format, date layout, hour dots, night mode and mute.
- Printable ASCII messages, selected colours, animations and sounds. Emoji are deliberately rejected. Actual message length may be lower than 64 bytes if the negotiated Bluetooth write limit is small.
- Timers with a clearly labelled Mac-side countdown estimate.
- Battery reading when the clock exposes the standard battery service.
- Explicit hand-calibration controls, original-app reseating guidance, movement/failure state readings when supported, and separate confirmation. Never twist an attached hand. Only remove/refit after the clock stops and physically displays Confirm with the green segment.
- Menu-bar shortcut, connection activity log, and a demo mode that sends no Bluetooth commands.
- Mac Bluetooth Current Time Service with read/notify support, advertising, and buttons to supply current Mac time/date or a manually chosen time/date. “Available” does not mean synced: only an actual time-characteristic read is reported. Compatibility with this clock's firmware still requires hardware testing.

## Set the time and date (0.29+)

macOS does not let apps publish the Bluetooth Current Time Service the clock reads (bluetoothd requires a private Apple entitlement), and the clock cannot report its time over Bluetooth. Version 0.29 instead uses a time offset found in the clock's own firmware: **Settings field 18**, a whole number of minutes. When the stored offset changes, the clock moves its running time by the difference. Details and evidence: `Code/Research/TimeSync/Findings.md`.

1. Connect the clock. The **Clock** page shows the clock's firmware version. The feature needs firmware **1.6.7 or later**; older firmware ignores the offset, so the controls stay off.
2. Finish any hand calibration first.
3. In **Clock time**, choose **Mac time** or **Manual time** (type a time such as 7:35 pm, and optionally a date), then press **Set** and confirm. The app cannot read the clock, so it estimates what the clock shows (Mac time plus the offset changes made since the bridge last synced). If the clock shows something else, open **Clock not showing the estimated time?** and type what it shows; the app remembers the correction. The −1 hour / −1 min / +1 min / +1 hour buttons nudge the clock directly (owner-confirmed working).
4. Check the clock face. The app reads the settings back and confirms the stored offset, but the clock keeps its last-written settings, so this does not prove the display moved.

Limits: whole minutes only (the clock keeps its own seconds); the clock must already have had a valid time once (it remembers this in flash); no automatic DST. Fine adjust is owner-confirmed; Set with the estimate (0.31) is owner-confirmed working. Calibrating the hands to 12 does not reset the clock's time; after calibration the hands return to the clock's own time.

The clock also adds the offset to the time it reads from the OpenGlance Bridge. Before using the bridge again, open **Using the OpenGlance Bridge again?** and choose **Reset offset to 0**.

## Verification and limits

Ten protocol-check groups cover binary commands, malformed packets, unknown settings and brightness flags, input limits, automatic brightness range packing/verification, time-zone/DST encoding, advancing manual time and original-app hand-state decoding. The GUI can be rendered without touching Bluetooth. **The owner reports pairing, battery percentage, sounds, timers, message animations and scrolling text working. The 0.27 time service registers successfully on this Mac, but clock-side time delivery and the new brightness limits await hardware testing. Hands repeatedly stopped around 12:14–12:15 during calibration; corrected alignment has not yet been reported.** See Hardware-status.md.

## Automatic brightness limits (0.26)

Enable **Adjust automatically** in Brightness to expose Minimum and Maximum sliders. These use the clock's 0–255 brightness scale, with minimum never above maximum. The light sensor continues to select brightness between the limits without requiring the Mac to remain connected. Raise Minimum for a brighter low-light display; lower Maximum to cap brightness. **Restore limits · 30–220** sends the original Android app's defaults. Changes are sent on slider release, with a fresh settings read and full-range read-back verification.

This reproduces the original app's sensor-driven range controls, not a time schedule or additive +/− bias. Unrelated fields and brightness bits above the known three-byte value are preserved. Power-cycle persistence has not been tested.

The clock-face illustration is a Mac-time preview, not a live reading of the physical hands. Calendar, weather, automatic schedules, Linux GUI and firmware updates are not included in this first version. No Glance account or manufacturer cloud is used. The app uses no network APIs.

This is locally ad-hoc signed, not Apple-notarised for public distribution. The build script requires Apple Command Line Tools; a Developer ID release can be added later.

## Build

```sh
swift run --build-system native --scratch-path .build-checks ProtocolChecks
bash build-app.sh
```

For Bluetooth use, run the packaged `.app`, not the bare Swift executable: the app bundle contains the required privacy descriptions and identity. The normal app can reconnect to the saved clock. Screenshot-only mode avoids Bluetooth:

```sh
"../../MAC/OpenGlance Clock 0.22.app/Contents/MacOS/OpenGlanceClock" --screenshot /absolute/path/preview.png
```

## Protocol references

Independently written Swift implementation informed by these public primary sources:

- [Hypfer/glance-clock protocol schema](https://github.com/Hypfer/glance-clock)
- [PorlyBe/glance_clock_ha](https://github.com/PorlyBe/glance_clock_ha)
- [Snille/glance_clock_ha](https://github.com/Snille/glance_clock_ha), including observed opaque upper bits in the brightness field
- [PorlyBe/bluetooth-cts](https://github.com/PorlyBe/bluetooth-cts)
- [Apple Core Bluetooth](https://developer.apple.com/documentation/corebluetooth)
- [stass/blew peripheral registration findings](https://github.com/stass/blew#periph-adv--advertise-and-host-a-gatt-server)

See the separate revival findings for original power-supply evidence. Do not connect an unverified supply merely because its plug fits.

The new logo was generated with the built-in image-generation tool; its brief is saved in Brand.md.
