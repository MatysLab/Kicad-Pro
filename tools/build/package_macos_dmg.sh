#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/release}"
SOURCE_APP="${SOURCE_APP:-$BUILD_DIR/kicad/KiCad.app}"
PACKAGE_DIR="${PACKAGE_DIR:-$BUILD_DIR/packages}"
PRODUCT_NAME="KiCad Pro"
PRODUCT_VERSION="${KICAD_PRO_VERSION:-10.0.5-rc1-kicad-pro.4}"
BUNDLE_VERSION="${KICAD_PRO_BUNDLE_VERSION:-10.0.5.4}"
VOLUME_NAME="${KICAD_PRO_VOLUME_NAME:-KiCad Pro Installer}"

case "$(uname -m)" in
    arm64) ARCH_NAME="arm64" ;;
    x86_64) ARCH_NAME="x86_64" ;;
    *) ARCH_NAME="$(uname -m)" ;;
esac

OUTPUT_DMG="$PACKAGE_DIR/KiCad-Pro-$PRODUCT_VERSION-macOS-$ARCH_NAME.dmg"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/kicad-pro-dmg.XXXXXX")"
STAGE_DIR="$WORK_DIR/root"
RW_DMG="$WORK_DIR/KiCad-Pro-rw.dmg"
MOUNT_DIR="/Volumes/$VOLUME_NAME"
BACKGROUND_DIR="$STAGE_DIR/.background"
BACKGROUND_IMAGE="$BACKGROUND_DIR/installer-background.png"

cleanup()
{
    if mount | grep -Fq "on $MOUNT_DIR ("; then
        hdiutil detach "$MOUNT_DIR" -quiet || hdiutil detach "$MOUNT_DIR" -force -quiet || true
    fi

    rmdir "$MOUNT_DIR" 2>/dev/null || true
    rm -rf "$WORK_DIR"
}

trap cleanup EXIT

for tool in ditto hdiutil magick osascript; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Required tool not found: $tool" >&2
        exit 1
    fi
done

if [[ ! -d "$SOURCE_APP" ]]; then
    echo "Built application not found: $SOURCE_APP" >&2
    exit 1
fi

mkdir -p "$STAGE_DIR" "$BACKGROUND_DIR" "$PACKAGE_DIR"
ditto "$SOURCE_APP" "$STAGE_DIR/$PRODUCT_NAME.app"
ln -s /Applications "$STAGE_DIR/Applications"

# Always package the repository's current KiCad Pro icon, even when the app bundle
# was produced before the icon resource changed.
ditto "$ROOT_DIR/kicad/kicad.icns" \
      "$STAGE_DIR/$PRODUCT_NAME.app/Contents/Resources/kicad.icns"

PLIST="$STAGE_DIR/$PRODUCT_NAME.app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleName $PRODUCT_NAME" "$PLIST"

if ! /usr/libexec/PlistBuddy -c "Set :CFBundleDisplayName $PRODUCT_NAME" "$PLIST" 2>/dev/null; then
    /usr/libexec/PlistBuddy -c "Add :CFBundleDisplayName string $PRODUCT_NAME" "$PLIST"
fi

/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $PRODUCT_VERSION" "$PLIST"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $BUNDLE_VERSION" "$PLIST"

FONT_REGULAR="/System/Library/Fonts/SFNS.ttf"
FONT_BOLD="/System/Library/Fonts/SFNS.ttf"

# Finder displays the application and Applications icons over this image at the
# centers of the two cards. The arrow and copy make the installation action clear.
magick -size 720x440 \
    gradient:'#16181c-#292d33' \
    -fill '#ffb60010' -stroke '#ffb60022' -strokewidth 1 \
    -draw 'circle 690,35 785,35' \
    -fill none -stroke '#ffffff0c' -strokewidth 1 \
    -draw "path 'M 0,375 C 155,315 220,455 390,375 S 640,335 720,390'" \
    -draw "path 'M 0,398 C 150,345 245,472 420,395 S 645,360 720,410'" \
    -fill '#ffb600' -stroke none -draw 'circle 38,38 44,38' \
    -font "$FONT_BOLD" -pointsize 21 -fill '#f6f7f8' \
    -draw "text 56,46 '$PRODUCT_NAME'" \
    -font "$FONT_BOLD" -pointsize 34 -gravity north -fill '#ffffff' \
    -annotate +0+72 'Install KiCad Pro' \
    -font "$FONT_REGULAR" -pointsize 17 -fill '#bfc5cc' \
    -annotate +0+118 'Drag KiCad Pro into Applications' \
    -gravity northwest \
    -fill '#ffffff0a' -stroke '#ffffff18' -strokewidth 1 \
    -draw 'roundrectangle 90,168 270,342 18,18' \
    -draw 'roundrectangle 450,168 630,342 18,18' \
    -fill none -stroke '#ffb600' -strokewidth 7 \
    -draw 'line 310,248 405,248' \
    -fill '#ffb600' -stroke none \
    -draw 'polygon 405,230 432,248 405,266' \
    -font "$FONT_REGULAR" -pointsize 14 -gravity south -fill '#929aa3' \
    -annotate +0+22 'Open KiCad Pro from Applications after copying.' \
    "$BACKGROUND_IMAGE"

STAGE_SIZE_KB="$(du -sk "$STAGE_DIR" | awk '{print $1}')"
DMG_SIZE_KB="$((STAGE_SIZE_KB + 65536))"

hdiutil create -srcfolder "$STAGE_DIR" -volname "$VOLUME_NAME" -fs HFS+ \
    -format UDRW -size "${DMG_SIZE_KB}k" "$RW_DMG" -quiet
hdiutil attach "$RW_DMG" -readwrite -noverify -noautoopen -mountpoint "$MOUNT_DIR" -quiet

/usr/bin/SetFile -a V "$MOUNT_DIR/.background"

osascript <<APPLESCRIPT
tell application "Finder"
    tell disk "$VOLUME_NAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set pathbar visible of container window to false
        set sidebar width of container window to 0
        set bounds of container window to {120, 120, 840, 560}

        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 104
        set text size of viewOptions to 13
        set background picture of viewOptions to file ".background:installer-background.png"

        set position of item "$PRODUCT_NAME.app" of container window to {180, 250}
        set position of item "Applications" of container window to {540, 250}
        update without registering applications
        delay 2
        close
        open
        delay 2
    end tell
end tell
APPLESCRIPT

sync
hdiutil detach "$MOUNT_DIR" -quiet

rm -f "$OUTPUT_DMG" "$OUTPUT_DMG.sha256"
hdiutil convert "$RW_DMG" -format ULFO -o "$OUTPUT_DMG" -quiet
( cd "$PACKAGE_DIR" && shasum -a 256 "$(basename "$OUTPUT_DMG")" ) > "$OUTPUT_DMG.sha256"

echo "$OUTPUT_DMG"
