#include "engine/core/resource_path.h"

#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <vector>
#endif

namespace pt {

std::filesystem::path ExecutableDir() {
    const char* base = SDL_GetBasePath();
    return base ? std::filesystem::path(reinterpret_cast<const char8_t*>(base)) : std::filesystem::current_path();
}

std::filesystem::path ExecutablePath() {
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) == 0) return std::filesystem::path(path.data());
    return {};
#elif defined(_WIN32)
    return ExecutableDir() / "pt.exe";
#else
    return ExecutableDir() / "pt";
#endif
}

#ifdef __APPLE__
std::filesystem::path MacVulkanLibrary() {
    std::error_code error;
    const auto bundled = ExecutableDir() / ".." / "Frameworks" / "libMoltenVK.dylib";
    if (std::filesystem::is_regular_file(bundled, error)) return bundled;
    const auto local = ExecutableDir() / "libMoltenVK.dylib";
    if (std::filesystem::is_regular_file(local, error)) return local;
    return PT_MOLTENVK_LIBRARY;
}
#endif

std::filesystem::path ResourceDir(std::string_view name, const std::filesystem::path& build_dir) {
    std::error_code ec;
    const std::filesystem::path local = ExecutableDir() / name;
    if (std::filesystem::is_directory(local, ec)) {
        return local;
    }
    return build_dir;
}

}
