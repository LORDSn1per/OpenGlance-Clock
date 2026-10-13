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
- A manual date and time can be set from the page. It pauses internet time until restart or **Back to internet time**.

## Web page
Tabs:
- **Home:** clock status, time, weather summary and recent activity.
- **Messages:** messages and timer.
- **Weather**
- **Display:** display toggles and brightness.
- **Clock:** pairing, connection options and hand calibration.
- **System:** time zone and manual time, firmware, Wi-Fi and the full activity log.

The address remembers the tab, for example `http://openglance.local/#weather`.

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
- **When it skips:** no chime while the hands are calibrating, while a timer is running, or for an hour that began before the bridge started. The clock's own night mode also mutes sounds.
- **Sounds:** all 17 sounds from the Glance protobuf schema are offered (Waves, Rise, Charging, Steps, Radar, Bells, Bye, Hello, Flowers, Circles, Complete, Popcorn, Break, Opening, High, Shine, Extension). Only Radar has been confirmed by the owner so far; each sound picker has a **Test** button that plays it once.

## Firmware updates
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
