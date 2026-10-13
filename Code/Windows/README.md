# OpenGlance for Windows

Windows versions of the two Mac apps, built from this folder into `../../Windows/`:

- **OpenGlance Clock X.YY.exe**: controls the Glance clock directly over Bluetooth (same features as the Mac app with the same number).
- **OpenGlance Flasher X.YY.exe**: installs `OpenGlance Bridge … OTA.bin` firmware onto an ESP32-C3 over USB.

Each is one self-contained 64-bit `.exe`. Nothing needs installing: no .NET runtime, no Python, no drivers on Windows 10/11. They are not code-signed, so the first launch may show Windows SmartScreen: choose **More info → Run anyway**.

## Using OpenGlance Clock

1. Requires Windows 10 version 2004 or later and Bluetooth LE.
2. Quit the bridge or phone app if either is connected to the clock (the clock accepts one connection).
3. Short-press the clock's button to activate pairing mode (do not hold it).
4. Open the **Clock** tab, press **Find clocks**, then **Connect** beside your clock.
5. When asked, type the PIN shown on the clock. Windows remembers the pairing; later launches reconnect automatically (switch this off on the Clock tab while using the bridge).

Setting the time works the same way as on the Mac (Settings field 18, firmware 1.6.7+): choose **This PC's time** or **Manual time** and press **Set**. The app estimates what the clock shows; correct it under *Clock not showing the estimated time?* if needed. Settings are kept in `%APPDATA%\OpenGlance Clock\settings.json`.

To remove the pairing, use Windows Settings → Bluetooth & devices.

## Using OpenGlance Flasher

Plug the ESP32-C3 in with a USB-C data cable; it appears as a new COM port and is selected automatically. The newest `OpenGlance Bridge … OTA.bin` next to the app (in `../Firmware`) is chosen automatically, or choose/drop another file. Press **Install firmware**.

The Flasher contains Espressif's official, unmodified `esptool.exe` 5.3.1 (GPLv2; licence unpacked alongside it in `%LOCALAPPDATA%\OpenGlance Flasher\`; source at https://github.com/espressif/esptool).

## Building (on the Mac)

```
bash build.sh
```

- Needs the .NET 8 SDK (`brew install dotnet@8`) and internet access on first use (NuGet packages; esptool download, checksum-verified and cached in `~/Library/Caches/OpenGlance`).
- Version numbers come from the newest `MAC/OpenGlance Clock X.YY.app` and `MAC/OpenGlance Flasher X.YY.app`: **bump the Mac app first, then run this script**, so Mac and Windows always share a number. Existing `.exe` files are never overwritten.
- Runs `ProtocolChecks` (12 groups) and `FlasherChecks` (5 groups, including every real OTA.bin) before publishing.
- `set-version-info.py` writes the Windows version details (Explorer → Properties), which the .NET SDK only does when building on Windows.

## Layout

| Folder | Contents |
| --- | --- |
| `GlanceProtocol/` | C# port of `../OpenGlanceClock/Sources/GlanceProtocol` (keep them in step) |
| `ProtocolChecks/` | Standalone checks, same cases as the Mac `ProtocolChecks` |
| `OpenGlanceClock/` | Avalonia app; `Radio.cs` holds the Windows Bluetooth (WinRT) code |
| `FlasherCore/`, `FlasherChecks/` | Port of the Mac `FlasherCore` with Windows COM-port handling |
| `OpenGlanceFlasher/` | Avalonia Flasher app; fixed images come from `../OpenGlanceFlasher/Support/` |

## Checking screens on the Mac (no Bluetooth, nothing flashed)

The apps also build for plain `net8.0`, which runs on macOS in demo mode only:

```
dotnet build OpenGlanceClock -c Release -f net8.0
dotnet OpenGlanceClock/bin/Release/net8.0/OpenGlanceClock.dll --screenshot out.png --tab clock [--dark|--light] [--height 1000] [--fill]
dotnet OpenGlanceFlasher/bin/Release/net8.0/OpenGlanceFlasher.dll --screenshot out.png [--firmware file.bin] [--state flashing|done|failed] [--log]
```

Screenshot mode never touches Bluetooth or USB and never reads or writes the owner's settings.
