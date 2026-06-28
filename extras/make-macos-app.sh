#!/usr/bin/env bash
# Assemble a self-contained diffy.app from an already-built diffy-gui.
#
# The normal build produces a plain executable (out/<plat>-gui-*/gui/diffy-gui)
# whose dylibs are found via build-tree rpaths. This script copies that binary
# plus its dylibs into a relocatable .app, rewrites the load paths to
# @executable_path/../Frameworks, and renders an .icns from the bundled SVG so
# the app launches from Finder.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${1:-out/macos-release-gui}"
# Fall back to the Makefile's gui-release dir name if the arg wasn't given.
[ -x "$BUILD_DIR/gui/diffy-gui" ] || BUILD_DIR="out/macos-gui-release"

EXE="$BUILD_DIR/gui/diffy-gui"
if [ ! -x "$EXE" ]; then
  echo "error: $EXE not found — build diffy-gui first (e.g. make gui-release)." >&2
  exit 1
fi

APP="$BUILD_DIR/diffy.app"
echo "==> Assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

BIN="$APP/Contents/MacOS/diffy"
cp "$EXE" "$BIN"
chmod +w "$BIN"

# --- bundle dylibs -----------------------------------------------------------
# Slint is linked by absolute path; crc32c/fmt come in via @rpath.
SLINT_SRC="$(otool -L "$EXE" | awk '/libslint_cpp\.dylib/{print $1; exit}')"
if [ -n "$SLINT_SRC" ] && [ -f "$SLINT_SRC" ]; then
  cp "$SLINT_SRC" "$APP/Contents/Frameworks/"
  install_name_tool -change "$SLINT_SRC" \
    "@executable_path/../Frameworks/$(basename "$SLINT_SRC")" "$BIN"
fi
find "$BUILD_DIR/subprojects" -name 'libcrc32c*.dylib' -exec cp {} "$APP/Contents/Frameworks/" \; 2>/dev/null || true
find "$BUILD_DIR/subprojects" -name 'libfmt*.dylib' -exec cp {} "$APP/Contents/Frameworks/" \; 2>/dev/null || true

# Make @rpath/* (crc32c, fmt) resolve from the bundle.
install_name_tool -add_rpath "@executable_path/../Frameworks" "$BIN" 2>/dev/null || true

# Strip build-tree rpaths (anything pointing into the build dir) so the bundle is
# relocatable and actually depends on the bundled Frameworks.
otool -l "$BIN" | awk '/ cmd LC_RPATH/{getline; getline; print $2}' | while read -r rp; do
  case "$rp" in
    */out/*) install_name_tool -delete_rpath "$rp" "$BIN" 2>/dev/null || true ;;
  esac
done

# --- icon --------------------------------------------------------------------
if command -v rsvg-convert >/dev/null && command -v iconutil >/dev/null; then
  ICONSET="$(mktemp -d)/diffy.iconset"
  mkdir -p "$ICONSET"
  for s in 16 32 128 256 512; do
    rsvg-convert -w "$s" -h "$s" gui/packaging/diffy-icon.svg -o "$ICONSET/icon_${s}x${s}.png"
    rsvg-convert -w "$((s * 2))" -h "$((s * 2))" gui/packaging/diffy-icon.svg \
      -o "$ICONSET/icon_${s}x${s}@2x.png"
  done
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/diffy.icns"
  rm -rf "$(dirname "$ICONSET")"
else
  echo "    (skipping icon: rsvg-convert / iconutil not found)"
fi

# --- Info.plist --------------------------------------------------------------
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>            <string>diffy</string>
  <key>CFBundleDisplayName</key>     <string>diffy</string>
  <key>CFBundleIdentifier</key>      <string>se.algelind.diffy</string>
  <key>CFBundleVersion</key>         <string>0.1</string>
  <key>CFBundleShortVersionString</key> <string>0.1</string>
  <key>CFBundlePackageType</key>     <string>APPL</string>
  <key>CFBundleExecutable</key>      <string>diffy</string>
  <key>CFBundleIconFile</key>        <string>diffy</string>
  <key>NSHighResolutionCapable</key> <true/>
  <key>LSMinimumSystemVersion</key>  <string>11.0</string>
</dict>
</plist>
PLIST

echo "==> Done: $APP"
echo "    Verify deps:  otool -L \"$BIN\" | grep -i slint"
