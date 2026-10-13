# Owner-reported hardware status

Updated 13 October 2026. These are the owner's observations, not automated hardware tests.

| Feature | Result |
| --- | --- |
| Pairing | Working after a short press on the clock's single button |
| Battery percentage | Working |
| Mac-to-clock time sync | **Mac app 0.31: setting a typed or Mac time works (owner, 13 Oct 2026).** **Working via the ESP32-C3 bridge (owner: "everything works", 1.02). Mac app 0.29: fine-adjust ±1 min/±1 hour via Settings field 18 works (owner, 13 Oct 2026).** Not possible through the Mac's built-in Bluetooth; bluetoothd requires a private Apple entitlement for 16-bit 0x1805; the 0.27 128-bit registration succeeded but logs show the clock never read it across nine reconnects. Needs an iPhone, ESP32, USB dongle stack or Linux CTS host |
| Sound | Working; the earlier failure was caused by Silent being enabled |
| Timer | Working |
| Message animations | Working |
| Hello world text and scrolling | Working |
| Hand calibration | Repeated stop at about 12:14–12:15 before confirmation; reference alignment not yet corrected |
| Automatic brightness limits | Original APK confirms low/high limits; implemented in 0.26, awaiting owner hardware test. Additive bias and sensor-output read remain unverified |

## Automatic brightness limitation

The original APK supersedes the earlier reference's incomplete interpretation: byte 0 is manual brightness (0 selects automatic); byte 1 is the automatic maximum; byte 2 is the automatic minimum. APK modules 819/1779/1780/1786 pack automatic settings as `(low << 16) | (high << 8)`, default low 30/high 220, with two-ended range controls. Higher bits and unrelated protobuf fields remain preserved.

Reference: https://github.com/Snille/glance_clock_ha/blob/master/custom_components/glance_clock/light.py

Version 0.26 exposes minimum and maximum sliders and restores the original 30–220 defaults. These are sensor-driven range limits, not an additive brightness bias or a Mac time-of-day schedule. It reads fresh settings before writing and verifies all three brightness bytes afterward. No successful physical test of the new controls has been reported yet.

## Hand alignment

The owner supplied Glance Clock Android 1.5.1 APK. Its calibration view (React Native module 2082) instructs removal/refitting of misaligned hands at 12, short hour hand first, then long minute hand, with clearance from the fabric and between hands. No software +/− hand controls were found in that version. The owner subsequently accepted reseating guidance and requested it in OpenGlance.

The calibration panel now includes an expandable reseating guide. It requires stopped hands, the physical clock's Confirm display and green segment, warns against twisting or force, and keeps confirmation a separate explicit action. No reseating or successful calibration has yet been reported by the owner. No clock commands were sent during implementation/testing.

The revised app reads/listens to the original app's hand-state characteristic when present. Displayed homing/confirmation/failure flags are not measured hand angles. Time-setting and confirmation are blocked while the clock reports movement.

## Revised time/date delivery

Time buttons choose either current Mac time or an advancing manual date/time in the Mac's time zone, then explicitly reconnect without removing the registered CTS server. Read/notify properties and service advertising are enabled. Actual read events and the supplied value are recorded separately from service availability. No direct command number was guessed.

The owner subsequently tested 0.25: standard CTS service registration is rejected by macOS (“The specified UUID is not allowed for this operation”). Version 0.26 shows an actionable failure instead of telling the user to wait and disables unavailable time controls. Android/Linux CTS hosting is a possible alternative, not implemented in this Mac release. Time sync and offline DST remain unresolved. Merely changing to a 128-bit UUID spelling is not a verified interoperable workaround.

Version 0.27 now retries that specific registration error with the equivalent full Bluetooth-base service UUID, retaining compact characteristic UUIDs. A registration-only probe on this Mac confirmed rejection of 1805 followed by acceptance of 00001805-0000-1000-8000-00805F9B34FB. No clock connection or Glance command was initiated by the probe, and the running 0.26 app was left alone. This is verified Mac registration, **not yet verified clock discovery or time synchronisation**. Normal set-time records actual reads/supplied dates and reports no read after 45 seconds. Owner should try 0.27 and report physical digital time/date plus delivery status.

## 0.27 owner test — log diagnosis

The owner ran 0.27 Set time (about 12:58–13:12, 13 October 2026). macOS logs show the clock reconnecting and encrypting each time, but no read of the Mac's time service. bluetoothd states the app "is not entitled to publish built-in service 0x1805". The 128-bit compatibility route is therefore **disproved** on this clock, not merely unconfirmed. Alternatives are listed in `Code/Research/TimeSync/Findings.md`.

## ESP32-C3 bridge (replaces the Mac app from 13 October 2026)

Firmware 1.01 was flashed first; 1.02 followed. **Owner update (13 Oct 2026): "everything works" with 1.02**, including Wi-Fi setup and pairing the clock through the bridge. Earlier pending items, kept for reference: pairing/PIN, the clock reading the bridge's 16-bit Current Time Service, the displayed time/date, reconnects and the controls all await owner testing. Source and instructions: `Code/ESP32C3/README.md`.

## Bridge installed in the clock (13 October 2026)

Owner installed the ESP32-C3 inside the clock in place of the barrel jack; the clock now charges and runs directly from USB. Weather rings (firmware 1.07) await the owner's first hardware test: temperature ring colours, rain ring shown only when rain is coming, and both scenes together.

## Mac app 0.29 — time correction through Settings field 18 (13 October 2026)

Not yet tested on the clock. Found by static disassembly of firmware 1.6.7: changing Settings field 18 (int32 minutes) moves the clock's running time by the difference, and CTS time is set as CTS + field 18. 0.29 shows the clock's firmware version (Device Information 2A26) and enables the controls only for 1.6.7 or later. Owner should report: the firmware version shown, whether **+1 min** moves the digital time and hands, and whether **Set clock to Mac time** gives the right time/date. If the bridge is used afterwards, reset the offset to 0 first.

**Owner update (13 Oct 2026): the 0.29 fine-adjust buttons work on the clock.** Typed manual time could not be entered in 0.29; 0.30 replaces it with text fields and a Set button (not yet tested on the clock).

**Owner update (13 Oct 2026, 0.30):** the clock reports firmware **1.6.7**, which matches the analysed image. Typed Set was unclear (it needed the clock's current reading). 0.31 estimates the reading and sets a typed or Mac time with one Set button. Not yet tested on the clock.

**Owner update (13 Oct 2026): 0.31 sets the time manually fine.**

## Windows apps (13 October 2026)

`Windows/OpenGlance Clock 0.31.exe` and `Windows/OpenGlance Flasher 1.01.exe` were built on the Mac and have **not been run on Windows**. Pairing, PIN entry, every clock control, the field-18 time setting, flashing and boot-banner confirmation on Windows all await the owner's test.
