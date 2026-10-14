# Android distribution notices

The port's own code is MIT licensed; see the included `LICENSE`.
P.T. and its original game content belong to Konami and are not included.

The Android launcher uses **LibOrbisPkg**, licensed under **LGPL version 3**.
The APK carries the LGPL and GPL notices. The release provides its patched
source as `LibOrbisPkg-Android-source.zip`, including the pinned source revision
and links to the application source and build/relink instructions. The code is
not trimmed or AOT compiled, and the public application source can be rebuilt
with a modified library and a tester's own signing key. Reverse engineering for
debugging changes to that library is permitted under its license terms.

Other dependency notices are copied from their pinned sources at build time:
SDL3/hidapi, zlib, volk, Vulkan Memory Allocator, GLM, Dear ImGui, stb, Lua 5.1,
HarfBuzz, libogg, libvorbis and bc7enc. Noto's OFL notices accompany the fonts.
The voice directory includes the Whisper, whisper.cpp and Silero VAD notices.

This Android APK does not package MoltenVK, MetalFX, the desktop upscaler SDKs
or the enhanced-texture helper.
