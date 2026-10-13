#!/bin/bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")" && pwd)"
code_dir="$(dirname "$project_dir")"
output_dir="$(dirname "$code_dir")/MAC"
mkdir -p "$output_dir"
latest_version=0
for previous_app in "$output_dir"/OpenGlance\ Clock\ *.app; do
    previous_name="$(basename "$previous_app")"
    if [[ "$previous_name" =~ ^OpenGlance\ Clock\ ([0-9]+)\.([0-9][0-9])\.app$ ]]; then
        previous_version=$((10#${BASH_REMATCH[1]} * 100 + 10#${BASH_REMATCH[2]}))
        if (( previous_version > latest_version )); then latest_version=$previous_version; fi
    fi
done
next_version=$((latest_version + 1))
release_version="$(printf '%d.%02d' "$((next_version / 100))" "$((next_version % 100))")"
release_name="OpenGlance Clock $release_version"
release_app="$output_dir/$release_name.app"
if [ -e "$release_app" ]; then
    printf 'Refusing to overwrite an existing release: %s\n' "$release_name" >&2
    exit 1
fi
# Compile on the local disk; the project can live on a network-mounted volume.
# Each build gets its own cache, avoiding stale absolute paths after relocation.
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/openglance-build.XXXXXX")"
app_dir="$build_dir/$release_name.app"
cd "$project_dir"
mkdir -p "$build_dir/source"
cp Package.swift "$build_dir/source/Package.swift"
cp -R Sources Tests "$build_dir/source/"
cd "$build_dir/source"
swift build --build-system native --scratch-path "$build_dir/swift" -c release
binary_dir="$(swift build --build-system native --scratch-path "$build_dir/swift" -c release --show-bin-path)"
"$binary_dir/ProtocolChecks"
cd "$project_dir"
mkdir -p "$app_dir/Contents/MacOS" "$app_dir/Contents/Resources"
cp "$binary_dir/OpenGlanceClock" "$app_dir/Contents/MacOS/OpenGlanceClock"
cp Info.plist "$app_dir/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $release_version" "$app_dir/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $next_version" "$app_dir/Contents/Info.plist"
cp Sources/OpenGlanceClock/Resources/Logo.png "$app_dir/Contents/Resources/Logo.png"
for resource_bundle in "$binary_dir"/*.bundle; do
    if [ -d "$resource_bundle" ]; then cp -R "$resource_bundle" "$app_dir/Contents/Resources/"; fi
done
icon_dir="$build_dir/OpenGlanceClock.iconset"
mkdir -p "$icon_dir"
for icon_size in 16 32 128 256 512; do
    sips -z "$icon_size" "$icon_size" Sources/OpenGlanceClock/Resources/Logo.png --out "$icon_dir/icon_${icon_size}x${icon_size}.png" >/dev/null
    double_size=$((icon_size * 2))
    sips -z "$double_size" "$double_size" Sources/OpenGlanceClock/Resources/Logo.png --out "$icon_dir/icon_${icon_size}x${icon_size}@2x.png" >/dev/null
done
iconutil -c icns "$icon_dir" -o "$app_dir/Contents/Resources/OpenGlanceClock.icns"
codesign --force --deep --sign - "$app_dir"
codesign --verify --deep --strict "$app_dir"
mv "$app_dir" "$release_app"
printf 'Built %s\nTemporary build cache: %s\n' "$release_app" "$build_dir"
