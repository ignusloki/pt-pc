#!/bin/sh
# Signs build/ios/pt.app with a development profile, installs it on a connected iPhone or iPad, copies the game files into
# the app's Documents folder (once) and launches it (docs/mobile.md).
#   PT_PROFILE   the .mobileprovision for the bundle id (Xcode makes one: build any app with that id once, signed in)
#   PT_IDENTITY  the signing identity's SHA-1 or name (security find-identity -v -p codesigning)
#   PT_GAME      the CUSA01127 folder (chunk1.psarc, texture.qar, pathid_list_ps4.bin)
#   PT_DEVICE    the device (xcrun devicectl list devices), default: the first one connected
#   PT_LAUNCH_ENV  environment for the game as JSON, e.g. '{"PT_DEBUG_SHOT":"shot.png"}' 
set -eu
cd "$(dirname "$0")/.."
: "${PT_PROFILE:?the .mobileprovision}" "${PT_IDENTITY:?the signing identity}"
B=build/ios-signed
rm -rf "$B" && mkdir -p "$B"
cp -R build/ios/pt.app "$B/"
A="$B/pt.app"
security cms -D -i "$PT_PROFILE" > "$B/profile.plist"
ID=$(/usr/libexec/PlistBuddy -c "Print :Entitlements:application-identifier" "$B/profile.plist")
BUNDLE_ID=${ID#*.}
/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier $BUNDLE_ID" "$A/Info.plist"
/usr/libexec/PlistBuddy -x -c "Print :Entitlements" "$B/profile.plist" > "$B/entitlements.plist"
cp "$PT_PROFILE" "$A/embedded.mobileprovision"
codesign -f -s "$PT_IDENTITY" "$A/Frameworks/MoltenVK.framework"
codesign -f -s "$PT_IDENTITY" --entitlements "$B/entitlements.plist" "$A"

DEVICE=${PT_DEVICE:-$(xcrun devicectl list devices 2>/dev/null | grep -E "available|connected" | grep -v simulated | grep -oE "[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}|[0-9A-F]{8}-[0-9A-F]{16}" | head -1)}
xcrun devicectl device install app --device "$DEVICE" "$A"
if [ -n "${PT_GAME:-}" ]; then
  for f in chunk1.psarc texture.qar pathid_list_ps4.bin; do
    xcrun devicectl device copy to --device "$DEVICE" --domain-type appDataContainer --domain-identifier "$BUNDLE_ID" \
      --source "$PT_GAME/$f" --destination "Documents/CUSA01127/$f"
  done
fi
if [ -n "${PT_LAUNCH_ENV:-}" ]; then
  xcrun devicectl device process launch --device "$DEVICE" --terminate-existing --environment-variables "$PT_LAUNCH_ENV" "$BUNDLE_ID"
else
  xcrun devicectl device process launch --device "$DEVICE" --terminate-existing "$BUNDLE_ID"
fi
