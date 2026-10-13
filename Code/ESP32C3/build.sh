#!/bin/bash
# Builds the OpenGlance Bridge firmware for the ESP32-C3 Super Mini.
#  * finds the highest numbered build in ../../Firmware and advances by 0.01
#  * refuses to overwrite an existing release
#  * runs the host protocol checks, compiles from a local temporary copy
#    (compiling straight from the network volume is unreliable)
#  * saves ONE file per release in ../../Firmware: "OpenGlance Bridge X.YY OTA.bin".
#    Upload it from the bridge web page, or install it on a bare ESP32-C3 with
#    the OpenGlance Flasher Mac app (which carries the fixed bootloader,
#    partition table and boot selector; this script keeps those in step).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
FW_DIR="$(cd "$HERE/../.." && pwd)/Firmware"
FQBN="esp32:esp32:esp32c3:PartitionScheme=min_spiffs,CDCOnBoot=cdc,FlashSize=4M"
mkdir -p "$FW_DIR"

latest="$(ls "$FW_DIR" 2>/dev/null | sed -nE 's/^OpenGlance Bridge ([0-9]+\.[0-9]{2}) OTA\.bin$/\1/p' | sort -t. -k1,1n -k2,2n | tail -1)"
if [ -z "$latest" ]; then version="1.00"; else version="$(awk -v v="$latest" 'BEGIN{printf "%.2f", v + 0.01}')"; fi
OTA="$FW_DIR/OpenGlance Bridge $version OTA.bin"
FLASHER_SUPPORT="$(cd "$HERE/.." && pwd)/OpenGlanceFlasher/Support"
if [ -e "$OTA" ]; then echo "Refusing to overwrite release $version" >&2; exit 1; fi
echo "Building OpenGlance Bridge $version"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/openglance-bridge.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

echo "== Protocol checks"
clang++ -std=c++17 -Wall -Wextra -Werror -o "$WORK/protocol_checks" "$HERE/tests/protocol_checks.cpp"
"$WORK/protocol_checks"

echo "== Compile"
mkdir -p "$WORK/OpenGlanceBridge"
cp "$HERE"/OpenGlanceBridge/*.ino "$HERE"/OpenGlanceBridge/*.cpp "$HERE"/OpenGlanceBridge/*.h "$WORK/OpenGlanceBridge/"
printf '// Written by build.sh, which advances the version by 0.01 for each completed build.\n#pragma once\n#define OG_FW_VERSION "%s"\n' "$version" > "$WORK/OpenGlanceBridge/version.h"
arduino-cli compile --fqbn "$FQBN" --build-path "$WORK/build" --warnings default "$WORK/OpenGlanceBridge"

APP="$WORK/build/OpenGlanceBridge.ino.bin"
[ -f "$APP" ] || { echo "Build output missing" >&2; exit 1; }
grep -aq "#OGBRIDGE-FW-ESP32C3:$version;" "$APP" || { echo "Firmware marker missing from image" >&2; exit 1; }

# The flasher must carry the same fixed images this build expects.
changed=0
for pair in "OpenGlanceBridge.ino.bootloader.bin:bootloader.bin" "OpenGlanceBridge.ino.partitions.bin:partitions.bin" "boot_app0.bin:boot_app0.bin"; do
    src="$WORK/build/${pair%%:*}"; dst="$FLASHER_SUPPORT/${pair##*:}"
    if ! cmp -s "$src" "$dst"; then cp "$src" "$dst"; changed=1; fi
done
[ "$changed" = 1 ] && echo "NOTE: flasher support images changed; rebuild the Mac flasher (OpenGlanceFlasher/build-app.sh)."

cp "$APP" "$OTA"
# Keep the maintained source's version.h in step with the latest release.
cp "$WORK/OpenGlanceBridge/version.h" "$HERE/OpenGlanceBridge/version.h"
echo "== Saved"
ls -l "$OTA"
