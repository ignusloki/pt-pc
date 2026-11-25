# P.T. PC Port 1.1.2

- Fixed an extra delay in the frame limiter when rendering already exceeded the frame budget. The fix applies to Windows, macOS, iOS and Android.
- Reduced redundant rendering work and improved weapon switching and reloading in Survive the Night.
- Reduced peak texture and model memory use when starting Survive the Night on iOS. A player finished the mode on an Apple A15 and confirmed smoother play on test build 110102.

This release adds a portable Windows x64 ZIP alongside the Apple silicon and Intel Mac packages, the iOS IPA and the Android arm64 APK. On Windows, extract the ZIP, launch `pt.exe` and select your existing game folder.

The A15 report covers the iOS test build. Gameplay testing of the final packages on Windows, Intel Mac and Android is pending.
