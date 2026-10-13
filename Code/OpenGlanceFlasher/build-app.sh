#!/bin/bash
# Builds "OpenGlance Flasher X.YY.app" into ../../MAC.
#  * version = highest existing OpenGlance Flasher app + 0.01 (first is 1.00)
#  * refuses to overwrite an existing release
#  * compiles a local temporary copy (network volumes break incremental builds)
#  * runs FlasherChecks (including validation of every OTA.bin in ../../Firmware)
#  * bundles Espressif's signed esptool (GPLv2, licence included) unmodified in
#    Contents/Helpers, plus the fixed bootloader/partition/otadata images
set -euo pipefail
project_dir="$(cd "$(dirname "$0")" && pwd)"
root_dir="$(cd "$project_dir/../.." && pwd)"
output_dir="$root_dir/MAC"
firmware_dir="$root_dir/Firmware"
mkdir -p "$output_dir"

latest=0
for app in "$output_dir"/OpenGlance\ Flasher\ *.app; do
    name="$(basename "$app")"
    if [[ "$name" =~ ^OpenGlance\ Flasher\ ([0-9]+)\.([0-9][0-9])\.app$ ]]; then
        v=$((10#${BASH_REMATCH[1]} * 100 + 10#${BASH_REMATCH[2]}))
        (( v > latest )) && latest=$v
    fi
done
if (( latest == 0 )); then next=100; else next=$((latest + 1)); fi
version="$(printf '%d.%02d' "$((next / 100))" "$((next % 100))")"
release="$output_dir/OpenGlance Flasher $version.app"
[ -e "$release" ] && { echo "Refusing to overwrite $release" >&2; exit 1; }

esptool_dir="$(ls -d "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/ 2>/dev/null | sort -V | tail -1)"
[ -x "$esptool_dir/esptool" ] || { echo "esptool not found (install the esp32 Arduino core)" >&2; exit 1; }
codesign --verify "$esptool_dir/esptool" || { echo "esptool signature invalid" >&2; exit 1; }
for f in bootloader.bin partitions.bin boot_app0.bin Logo.png; do
    [ -f "$project_dir/Support/$f" ] || { echo "Missing Support/$f" >&2; exit 1; }
done

work="$(mktemp -d "${TMPDIR:-/tmp}/openglance-flasher.XXXXXX")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/source"
cp "$project_dir/Package.swift" "$work/source/"
cp -R "$project_dir/Sources" "$project_dir/Tests" "$work/source/"
cd "$work/source"
swift build --build-system native --scratch-path "$work/swift" -c release
bin="$(swift build --build-system native --scratch-path "$work/swift" -c release --show-bin-path)"
"$bin/FlasherChecks" "$firmware_dir"

app="$work/OpenGlance Flasher $version.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Helpers" "$app/Contents/Resources/Support"
cp "$bin/OpenGlanceFlasher" "$app/Contents/MacOS/OpenGlanceFlasher"
cp "$project_dir/Info.plist" "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $next" "$app/Contents/Info.plist"
cp "$project_dir/Support/bootloader.bin" "$project_dir/Support/partitions.bin" "$project_dir/Support/boot_app0.bin" "$app/Contents/Resources/Support/"
cp "$project_dir/Support/Logo.png" "$app/Contents/Resources/Logo.png"
cp "$esptool_dir/esptool" "$app/Contents/Helpers/esptool"
cp "$esptool_dir/LICENSE" "$app/Contents/Resources/esptool-LICENSE.txt"

iconset="$work/OpenGlanceFlasher.iconset"
mkdir -p "$iconset"
for s in 16 32 128 256 512; do
    sips -z "$s" "$s" "$project_dir/Support/Logo.png" --out "$iconset/icon_${s}x${s}.png" >/dev/null
    sips -z "$((s * 2))" "$((s * 2))" "$project_dir/Support/Logo.png" --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/OpenGlanceFlasher.icns"

# Sign the app ad hoc without --deep so esptool keeps Espressif's own signature.
codesign --force --sign - "$app"
codesign --verify --deep --strict "$app"
codesign --verify "$app/Contents/Helpers/esptool"
"$app/Contents/Helpers/esptool" version >/dev/null
mv "$app" "$release"
echo "Built $release"
