#!/bin/bash
# Packages build/android (cmake --build build/android --target pt) as build/android/PT.apk, without Gradle:
# SDL's Java activity code and PTActivity -> classes.dex, the shaders and fonts as assets, libmain.so (docs/mobile.md).
set -e
cd "$(dirname "$0")/.."
SDK=${ANDROID_HOME:-$HOME/Library/Android/sdk}
NDK=$SDK/ndk/$(ls $SDK/ndk | sort -V | tail -1)
BT=$SDK/build-tools/$(ls $SDK/build-tools | sort -V | tail -1)
PLATFORM=$SDK/platforms/$(ls $SDK/platforms | sort -V | tail -1)
B=build/android
SDL=$B/_deps/sdl3-src
[ -d "$SDL" ] || SDL=$(grep -o 'FETCHCONTENT_SOURCE_DIR_SDL3:[A-Z]*=.*' $B/CMakeCache.txt | cut -d= -f2)
rm -rf $B/apk $B/java $B/assets && mkdir -p $B/apk/lib/arm64-v8a $B/java $B/assets/shaders $B/assets/fonts $B/assets/voice
# assets: shaders, fonts, the voice models (cmake/Mobile.cmake) and the manifest the game copies them out by (resource_path.cpp PrepareBundledFiles)
cp ${PT_APK_SHADERS:-$B/shaders}/*.spv $B/assets/shaders/  # PT_APK_SHADERS: another build's (a test)
cp assets/fonts/* $B/assets/fonts/
cp $B/voice/ggml-base.en-q5_1.bin $B/voice/ggml-silero-v6.2.0.bin $B/assets/voice/
(cd $B/assets && find shaders fonts voice -type f | sort | while read -r f; do echo "$(stat -f %z "$f") $f"; done > manifest.txt)
# Java -> dex
javac -nowarn --release 11 -classpath $PLATFORM/android.jar -d $B/java $(find "$SDL/android-project/app/src/main/java" android/java -name '*.java') 2>&1 | grep -v "^Note:" || true
$BT/d8 --release --min-api 30 --lib $PLATFORM/android.jar --output $B/apk $(find $B/java -name '*.class')
# native library
$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip --strip-debug -o $B/apk/lib/arm64-v8a/libmain.so $B/libmain.so
# package
$BT/aapt2 link -o $B/unaligned.apk --manifest android/AndroidManifest.xml -I $PLATFORM/android.jar -A $B/assets
(cd $B/apk && zip -q -0 -r ../unaligned.apk lib && zip -q ../unaligned.apk classes.dex)
$BT/zipalign -f -P 16 4 $B/unaligned.apk $B/aligned.apk
KS=$HOME/.android/debug.keystore
[ -f $KS ] || keytool -genkeypair -keystore $KS -storepass android -keypass android -alias androiddebugkey \
  -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null
$BT/apksigner sign --ks $KS --ks-pass pass:android --key-pass pass:android --out $B/PT.apk $B/aligned.apk
echo "built $B/PT.apk"
