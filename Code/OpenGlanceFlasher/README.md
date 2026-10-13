# OpenGlance Flasher (Mac)

Installs an `OpenGlance Bridge X.YY OTA.bin` onto an ESP32-C3 over USB, including a completely bare board.

- `Sources/FlasherCore`: firmware validation (ESP image magic, ESP32-C3 chip id, app descriptor, OpenGlance version stamp), the flash plan (0x0 bootloader, 0x8000 partitions, 0xE000 boot selector, 0x10000 app), esptool progress parsing and plain-language errors.
- `Sources/OpenGlanceFlasher`: the SwiftUI app. It runs Espressif's signed `esptool` (bundled unmodified, GPLv2 licence in Resources) on a pseudo-terminal for live progress, then reads the bridge's USB serial output to confirm the installed version started.
- `Support/`: the fixed images. They are refreshed automatically by `../ESP32C3/build.sh` if a firmware build ever changes them.
- `Tests/FlasherChecks`: standalone checks, run by the build script, including validation of every OTA.bin in `../../Firmware`.

Build with `bash build-app.sh`. It produces `MAC/OpenGlance Flasher X.YY.app`, advancing by 0.01 and never overwriting.

Screenshot-only mode (never flashes): `OpenGlanceFlasher --screenshot out.png [--firmware file.bin] [--state flashing|done|failed] [--log]`.
