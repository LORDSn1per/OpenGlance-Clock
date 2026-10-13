# OpenGlance Bridge (ESP32-C3 Super Mini)

The bridge replaces the Mac app. It is the only device that talks to the Glance Clock over Bluetooth, and you control everything from its web page.

**Why it exists:** the clock gets its time by reading a standard Bluetooth Current Time Service (0x1805) from whatever device is connected to it. macOS refuses to let apps publish that service ("not entitled to publish built-in service 0x1805"); the ESP32 can.

## First-time setup
1. Power the bridge over USB-C. It opens an open Wi-Fi hotspot called **OpenGlance-Setup** (no password).
2. Join it from a phone or computer. The setup page opens, or browse to `http://192.168.4.1/`.
3. Pick your 2.4 GHz home Wi-Fi from the dropdown (scanned automatically), enter its password and choose your time zone. Save.
4. The bridge restarts and joins your Wi-Fi. Open **http://openglance.local/** (or the IP address your router shows). There is no login: by owner choice the bridge has no passwords, so anyone on your home network can use the page.

## Pair the clock
1. Quit the old OpenGlance Mac apps and the phone app. The clock accepts one connection at a time.
2. **Short-press** the clock's button to activate pairing mode. Don't hold it.
3. On the web page press **Find clocks**, then **Pair** beside your clock.
4. Type the PIN the clock shows into the PIN box. After pairing, the bridge keeps the clock connected and reconnects automatically.

## Time
- Internet time comes from NTP: pool.ntp.org, time.google.com and time.cloudflare.com.
- Time zones are POSIX rules, so daylight saving switches automatically, even if the bridge is offline at that moment.
- The clock reads the time each time the bridge connects. The page shows **Last time read** and the exact value supplied.
- **Sync time now** reconnects so the clock reads the time again. The bridge also does this daily at 03:05 and whenever the UTC offset changes (daylight saving starts or ends).
- Until internet time arrives, the bridge reports "time unknown" (year 0) rather than a wrong time.
- A manual date and time can be set from the page. It pauses internet time until restart or **Use internet time** on Home.
- All time controls are now on **Home**. **Use internet time** is always visible;
  to use manual time, fill Date/Time and press **Apply manual time**. The active
  source is labelled separately, so editing the fields alone never switches it.
  Time zone, daily correction and **Update clock now** are also on Home.

## Web page
Tabs:
- **Home:** visible Internet/Manual time controls, time zone, clock status, weather summary and recent activity.
- **Alerts:** messages, timer and hourly-chime controls. Old `#message` bookmarks redirect to `#alerts`.
- **Alarms:** saved alarms and their schedules.
- **Weather**
- **Display:** display toggles, brightness and the comet seconds hand.
- **Clock:** pairing and hand calibration.
- **System:** Connection controls/options, firmware, Wi-Fi and the full activity log.

### Page updates (Bridge 1.17)

The page reads cached bridge status, not fresh Bluetooth data. It now checks
every 8 seconds when the comet is enabled, 5 seconds otherwise, and 1.5 seconds
while searching/pairing. Automatic checks stop when the page is hidden and
resume when it returns. Commands get a prompt status check, and overlapping
status requests are prevented. The displayed time ticks locally once per second
between checks; this does not make a network request.

Background Bluetooth activity no longer greys out every connected-clock
control until the next 8-second check. Commands still run one at a time on the
bridge. Seconds options keep your chosen switch/colour values while they are
being saved; temporary faster checks stop after acknowledgement or timeout.

Up to 1.18 the comet replaced its scene about every 2.5 seconds. Each replacement
makes the stock firmware restart the scene (frame counter, ring framebuffer and
the display's time/date cycle; static analysis of firmware 1.6.7). That caused the
tail glitches and the time/date swapping about every 3 seconds. **1.19 replaces
the scene only about every 10 seconds** (see below).

## Seconds pixel (Bridge 1.21)

On **Display → Comet seconds hand**, enable the switch and choose a colour.
Under **LEDs that light**, click any of the four LEDs at each position, from the
outer ring (1) to the centre (4); at least one stays on. Any unbroken run keeps
a new clock scene about every 10 s. A gap (for example outer + centre) needs two
drawing segments per tick, so the scene changes about every 7.5 s; the page shows
the interval for your choice. Outer only behaves exactly like 1.20.
The 24 named colours are the clock's native palette, not arbitrary RGB values.
Turquoise is the default. The switch and colour are saved on the bridge and the
animation continues when the webpage is closed, while the bridge is connected.
Off restores your existing weather-ring configuration. Weather data continues
updating while the comet uses the ring; no weather preferences are erased.

Seconds **always pause during the clock's Do Not Disturb**. The ineffective
Respect Do Not Disturb switch was removed at the owner's request in 1.18;
1.17's override had been confirmed not to show the physical ring. Old saved
override choices are ignored. Older cached clients can submit a valid legacy
option, but it never bypasses DND; status always reports respectDnd=true for
compatibility. Retry does not override DND.

Timers, alarms, messages, calibration and Bluetooth-error guards remain active.
The card reports the clock's actual DND state separately from automatic night
mode. Clock-wide DND, night mode, Mute and time settings are not changed by this
update. Changing the clock to AM manually changes its actual time and may
affect its scheduled alarms; it is not a seconds-only DND override.

This is an **experimental hardware-test build**, initially Off. It sends ring-only
CustomScene scenes in the same scene slot as weather: one clockwise lap per minute
across the outer ring's 48 positions. **Since 1.20** the seconds are a **single
pixel** that ticks one position every 1.25 s. Each step is one Fill that paints
the new pixel and paints the previous pixel black (the clock does not erase a
Fill when its lifetime ends, which made 1.19's segment grow). About 17 bytes per
step, so 8 steps (10 seconds) fit in one command; the clock refuses long
(multi-packet) writes. Expected result: about 6 scene replacements per minute
instead of 24, and time/date swapping about every 10 seconds. When seconds take
over the ring, the bridge first deletes its own old weather scenes (slots 1 and 2)
so the clock does not keep cycling a stale rain or temperature ring. MovingBar
uses an ease-in/out curve and Wave a repeating pattern, so neither is a uniform
seconds hand.
Timing is aligned to bridge time, not physical clock-hand telemetry. The webpage
preview is illustrative, not proof of the animation on the clock.

Timers, actual calibration/homing or confirmation, DND, recognised active alert faces,
saved alarm minutes and native hourly-chime minutes pause the comet. Messages
and bridge chimes hold it for 90 seconds. It requires a readable active-face
characteristic and stops on a Bluetooth error; **Retry / apply** explicitly
starts a fresh attempt. It never sets global brightness, disables night mode,
changes clock time/settings, clears all scene slots or uses the Timer command.
Normal movement while setting the time is no longer incorrectly called hand
calibration or used to pause the ring-only comet. State is read independently
of webpage checks (every 2 seconds while state-blocked, 5 seconds otherwise);
unavailable/stale state pauses safely rather than trusting an old notification.

After installation, check the direction/position, that exactly one pixel is lit,
any blip at each roughly 10-second handoff, how often time/date swap, that the old
rain graph is gone, alerts, night mode and weather restoration.
Version 1.13 was owner-confirmed to draw the seconds comet, but its periodic
whole-ring clear erased the tail at handoffs. Version 1.14 clears only expired
points during an adjacent-batch handoff and reapplies a fade to the existing
tail without refilling it at full brightness. Taking over from weather or
resynchronising after a missed batch still starts cleanly. Static inspection
now confirms that the stock firmware clears its framebuffer on replacement,
so the attempted tail preservation is insufficient. A proven continuous native
seconds scene is impossible with stock firmware; 1.19 instead makes replacements rare.
Disable the switch if the appearance isn't right and report what it does.
Mac and Windows applications are unchanged, as requested.

The address remembers the tab, for example `http://openglance.local/#weather`.

## Night settings (Bridge 1.22)

**Display → Night settings** shows and edits two schedules stored on the clock:

- **Do Not Disturb** (Off / Scheduled from–to / Always). While it is active the
  clock hides ring overlays: the seconds pixel, the weather ring and
  notifications. The bridge cannot override this.
- **Silent** (Off / Scheduled / Always) only mutes sounds, so the seconds stay
  visible. Silent "Always" is the same setting as **Mute sounds**.

For quiet nights with the seconds still ticking, set Do Not Disturb to Off (or
shorten it) and schedule Silent for the same hours. Each change reads the
clock's settings first, replaces only that schedule (every other setting and
unknown field is kept byte for byte), then reads back and logs whether the
clock confirmed it. Off keeps the saved hours, so switching back to Scheduled
restores them.

## Weather ring
- **Location:** search for a town on the Weather tab. The search runs from your browser via Open-Meteo's geocoding.
- **Forecast:** the bridge fetches it from Open-Meteo every 30 minutes over plain HTTP (no key; CC BY 4.0, attributed on the page).
- **One scene only:** exactly **one** weather scene (slot 1) is kept on the clock, so the clock never cycles scenes by itself. The scene left in slot 2 by 1.06/1.07 is deleted once.
- **Temperature ring:** the next 24 hours, coloured blue → green → amber → red across "Blue at" to "Red at". It stays up all the time.
- **Ring only by default:** scenes are sent ring-only (`7,16,8,1`), so the display keeps showing time and date. "Also show the temperature on the display" switches to ring and text (`7,16,24,1`).
- **Rain:** an hour counts as rain coming if it has at least the set amount (default 0.3 mm) **and** at least the set chance (default 40%) within the look-ahead (default 6 h). When rain is coming the bridge either alternates the ring between temperature and rain on the chosen interval (10 s to 10 min, default 20 s), or shows rain instead of temperature. Otherwise only temperature shows.
- **Re-sends:** identical scenes are not re-sent. The bridge sends after each new forecast, hourly as the window advances, and on reconnect. UpdateAndRefresh (35) is sent only for changes made on the page.
- **Night mode:** the clock hides clock-face rings by itself in night mode.

## Alarms (Alarms tab)
- **Clock limit:** up to 4 alarms, each with a time, days (Mon–Sun, Weekdays, Weekend, Every day or Once), a sound (with **Test**) and a label of up to 16 characters.
- **Format:** taken from the original app (APK 1.5.1). The whole list is sent as `4,47,0,0` + `Alarms`, and an empty list as AlarmsClear (`21`). Days are a bitmask (Mon=1 … Sun=64), omitted for "Once". Disabled alarms are kept on the bridge but not sent.
- **The clock cannot report its alarms.** Saving here **replaces every alarm on the clock**, including old ones from the phone app. **Delete all alarms on the clock** clears everything.
- **When the clock is away:** changes made while it is disconnected are sent when it reconnects.
- **Stop the alarm now** sends AlarmStop (`20`).

## Hourly chime and sounds
- **Hourly chime (Messages tab):** on the hour, the bridge sends a short message showing the time ("14:00", or "2PM" when the clock is in 12-hour mode) with the chosen sound. There is no animation.
- **Quiet hours:** default 22:00–07:00. Setting both to the same hour chimes around the clock.
- **When it skips:** no chime during the bridge's configured quiet hours, while the hands are calibrating, while a timer is running, or for an hour that began before the bridge started. Automatic night mode, clock DND and clock Mute are separate settings; do not assume night mode or DND silences every native sound.
- **Sounds:** all 17 sounds from the Glance protobuf schema are offered (Waves, Rise, Charging, Steps, Radar, Bells, Bye, Hello, Flowers, Circles, Complete, Popcorn, Break, Opening, High, Shine, Extension). Only Radar has been confirmed by the owner so far; each sound picker has a **Test** button that plays it once.

### Battery / charging beeps

The owner reports a periodic 100% notification during DND. The bridge only
reads battery level; it does not send a battery message. Stock 1.6.7's battery
notification routine checks the separate Mute state before playing sound 3
(Charging), not the DND state. **Display → Mute sounds** is the existing way to
test silencing this notification; no battery-only sound setting is verified.
The original APK says timers and alarms remain audible when muted, but verify
that behaviour on your clock before depending on it. The cause of repeated
charge notifications (rather than one notification) is not yet established.

## Firmware updates
### Clock's built-in hourly chime (1.11)

On **Messages**, **Clock’s built-in hourly chime** controls the clock's saved `charmSound` (field 15), separately from the bridge chime. Choose **Off** to stop only this sound, or pick one of the 17 sounds. The current clock-read value is shown. Every edit reads fresh settings, changes only field 15, preserves all other fields including time offset 18, then reads back to verify. Unknown values are displayed rather than overwritten.

It can play without the bridge connected; bridge quiet hours do not control it. Keep it Off when using the bridge chime to avoid doubles. Alarms, timers, messages and Mute are unchanged. Native silencing and power-cycle persistence await hardware testing. The owner chose to leave the 1.11 file ready, not install it yet.

Each release is a single file in `Firmware/`: `OpenGlance Bridge X.YY OTA.bin`.
- **Over Wi-Fi:** on the web page, open **Bridge firmware**, choose the OTA.bin and press **Install update**. Settings and the clock pairing are kept. Files that are not OpenGlance bridge firmware for the ESP32-C3 are rejected.
- **Over USB (a bare board, or recovery):** open `MAC/OpenGlance Flasher X.YY.app`, choose the board's USB port (it usually appears as `usbmodem…`) and press **Install firmware**. It picks the newest OTA.bin from `Firmware/` automatically. The app carries the fixed bootloader, partition table and boot selector, and writes them together with the OTA.bin. That result is byte-identical to a full image. Settings are kept unless you tick **Erase the whole chip first**.
- **Wi-Fi recovery:** if the saved Wi-Fi stops working, the setup hotspot returns after 2 minutes. Holding BOOT for 8 seconds also clears the Wi-Fi settings; the pairing is kept.

## Building
`bash build.sh`

The script:
1. Runs the host protocol checks in `tests/`.
2. Compiles a local temporary copy with arduino-cli: `esp32:esp32@3.3.12` core, NimBLE-Arduino 2.5.1, the `min_spiffs` partition scheme (OTA-capable) and USB CDC on boot.
3. Saves the next version, advanced by 0.01, to `../../Firmware` as a single `OTA.bin`. Existing releases are never overwritten.
4. If the build's bootloader, partition table or boot selector ever changes, it refreshes `../OpenGlanceFlasher/Support/` and reminds you to rebuild the flasher.

## Installing inside the clock (owner's plan)
The plan is to put the Super Mini where the barrel jack was, so its USB-C supplies the clock and remains reachable for USB flashing.

Before wiring:
- Check for a series diode between USB VBUS and the Super Mini `5V` pin. If present, feed the clock from VBUS, because the clock needs up to 1.5 A.
- Confirm the polarity of the clock's input pad.
- Check that the clock's battery does not back-feed the 5 V line when unplugged.
- Use a ≥2 A charger.
- Keep the chip antenna away from PCB copper.

The bridge needs no connection to the clock's logic, only power.

## Safety rules kept from the Mac app
- One Bluetooth operation at a time.
- Acknowledged writes only, and timed-out commands are never retried.
- Settings are read before every change, unknown protobuf fields and higher brightness bits are preserved, and changes are verified by reading back.
- No clock reset, bond clearing, firmware flashing or calibration without an explicit button press. Calibration needs a separate confirmation.
- **Forget clock** removes only the bridge's own stored keys.
