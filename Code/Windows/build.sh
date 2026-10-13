#!/bin/bash
# Builds the Windows releases into ../../Windows as single self-contained x64 .exe files:
#   "OpenGlance Clock X.YY.exe"    (same number as the newest MAC/OpenGlance Clock X.YY.app)
#   "OpenGlance Flasher X.YY.exe"  (same number as the newest MAC/OpenGlance Flasher X.YY.app)
# Version rule: Mac and Windows share one number. Bump the Mac app first (its build-app.sh),
# then run this script so Windows gets the same number. An existing .exe is never overwritten.
#  * compiles a local temporary copy (network volumes break incremental builds)
#  * runs ProtocolChecks and FlasherChecks (including every OTA.bin in ../../Firmware)
#  * sets the Windows version resource (the SDK only does that when building on Windows)
#  * embeds Espressif's official esptool.exe (GPLv2, unmodified, licence included), downloaded
#    once from the URL in the Arduino ESP32 package index and verified against its SHA-256
set -euo pipefail
project_dir="$(cd "$(dirname "$0")" && pwd)"
code_dir="$(dirname "$project_dir")"
root_dir="$(dirname "$code_dir")"
mac_dir="$root_dir/MAC"
output_dir="$root_dir/Windows"
firmware_dir="$root_dir/Firmware"
mkdir -p "$output_dir"

latest() {   # latest "OpenGlance Clock" → 0.31
    local best=0 name
    for app in "$mac_dir/$1 "*.app; do
        name="$(basename "$app")"
        if [[ "$name" =~ ^$1\ ([0-9]+)\.([0-9][0-9])\.app$ ]]; then
            local v=$((10#${BASH_REMATCH[1]} * 100 + 10#${BASH_REMATCH[2]}))
            (( v > best )) && best=$v
        fi
    done
    (( best > 0 )) || { echo "No $1 app found in $mac_dir" >&2; exit 1; }
    printf '%d.%02d' "$((best / 100))" "$((best % 100))"
}
clock_version="$(latest "OpenGlance Clock")"
flasher_version="$(latest "OpenGlance Flasher")"
clock_exe="$output_dir/OpenGlance Clock $clock_version.exe"
flasher_exe="$output_dir/OpenGlance Flasher $flasher_version.exe"
build_clock=1; build_flasher=1
[ -e "$clock_exe" ] && { echo "Already built, not overwriting: $(basename "$clock_exe")"; build_clock=0; }
[ -e "$flasher_exe" ] && { echo "Already built, not overwriting: $(basename "$flasher_exe")"; build_flasher=0; }
(( build_clock + build_flasher > 0 )) || { echo "Nothing to build. Bump the Mac app first."; exit 0; }

# Official Windows esptool, matching the Mac Flasher's esptool 5.3.1.
esptool_version="5.3.1"
esptool_url="https://github.com/espressif/esptool/releases/download/v$esptool_version/esptool-v$esptool_version-windows-amd64.zip"
esptool_sha="2b4a73c45db27426685896f64ce3e557f63a64f43cc100cb65c0cc3486af96d3"
cache="$HOME/Library/Caches/OpenGlance/esptool-$esptool_version-win"
if (( build_flasher )); then
    mkdir -p "$cache"
    [ -f "$cache/esptool.zip" ] || curl -sSL --fail -o "$cache/esptool.zip" "$esptool_url"
    echo "$esptool_sha  $cache/esptool.zip" | shasum -a 256 -c - >/dev/null || { echo "esptool download checksum mismatch" >&2; rm -f "$cache/esptool.zip"; exit 1; }
    rm -rf "$cache/x"; unzip -q -o "$cache/esptool.zip" -d "$cache/x"
    esptool_dir="$(dirname "$(find "$cache/x" -name esptool.exe | head -1)")"
    [ -f "$esptool_dir/esptool.exe" ] && [ -f "$esptool_dir/LICENSE" ] || { echo "esptool.exe or LICENSE missing from the archive" >&2; exit 1; }
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/openglance-windows.XXXXXX")"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/src/Windows"
cp -R "$project_dir/." "$work/src/Windows/"
rm -rf "$work/src/Windows"/*/bin "$work/src/Windows"/*/obj
cp -R "$code_dir/OpenGlanceFlasher" "$work/src/OpenGlanceFlasher"   # Support/ images shared with the Mac Flasher
cd "$work/src/Windows"

dotnet run --project ProtocolChecks -c Release
dotnet run --project FlasherChecks -c Release -- "$firmware_dir"

publish() {   # project framework version extra...
    local project="$1" framework="$2" version="$3"; shift 3
    dotnet publish "$project" -c Release -f "$framework" -r win-x64 --self-contained true \
        -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true \
        -p:DebugType=none -p:Version="$version" -p:InformationalVersion="$version" -o "$work/out/$project" "$@" >/dev/null
}
if (( build_clock )); then
    publish OpenGlanceClock net8.0-windows10.0.19041.0 "$clock_version"
    file "$work/out/OpenGlanceClock/OpenGlanceClock.exe" | grep -q "PE32+ executable (GUI) x86-64" || { echo "Clock exe is not a Windows x64 GUI program" >&2; exit 1; }
    python3 -I "$project_dir/set-version-info.py" "$work/out/OpenGlanceClock/OpenGlanceClock.exe" "OpenGlance Clock" "$clock_version" "$(basename "$clock_exe")"
    cat "$work/out/OpenGlanceClock/OpenGlanceClock.exe" > "$clock_exe"
    echo "Built $clock_exe ($(du -h "$clock_exe" | cut -f1))"
fi
if (( build_flasher )); then
    publish OpenGlanceFlasher net8.0 "$flasher_version" -p:EsptoolDir="$esptool_dir"
    file "$work/out/OpenGlanceFlasher/OpenGlanceFlasher.exe" | grep -q "PE32+ executable (GUI) x86-64" || { echo "Flasher exe is not a Windows x64 GUI program" >&2; exit 1; }
    python3 -I "$project_dir/set-version-info.py" "$work/out/OpenGlanceFlasher/OpenGlanceFlasher.exe" "OpenGlance Flasher" "$flasher_version" "$(basename "$flasher_exe")"
    cat "$work/out/OpenGlanceFlasher/OpenGlanceFlasher.exe" > "$flasher_exe"
    echo "Built $flasher_exe ($(du -h "$flasher_exe" | cut -f1))"
fi
