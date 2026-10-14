#pragma once

#include <filesystem>
#include <string_view>

namespace pt {

std::filesystem::path ExecutableDir();
#ifdef __APPLE__
/* The running executable. In the macOS app bundle ExecutableDir() is Contents/Resources, where the data is, and the
   executable is in Contents/MacOS (docs/macos.md). */
std::filesystem::path ExecutablePath();
#endif
std::filesystem::path ResourceDir(std::string_view name, const std::filesystem::path& build_dir);
#ifdef __ANDROID__
/* Android (docs/mobile.md): ExecutableDir() is the app's external files folder (the game files, data/, and the shaders and
   fonts). The APK carries those two as assets with a manifest.txt; this copies any that are missing or changed. */
void PrepareBundledFiles();
#endif

}
