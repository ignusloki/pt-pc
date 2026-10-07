#pragma once

#include <filesystem>
#include <string_view>

namespace pt {

std::filesystem::path ExecutableDir();
// The actual executable; SDL_GetBasePath returns Resources/ inside a Mac app.
std::filesystem::path ExecutablePath();
#ifdef __APPLE__
std::filesystem::path MacVulkanLibrary();
#endif
std::filesystem::path ResourceDir(std::string_view name, const std::filesystem::path& build_dir);

}
