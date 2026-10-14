#include "engine/core/resource_path.h"

#include <SDL3/SDL.h>

#ifdef __ANDROID__
#include <string>
#include <vector>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>

#include <vector>
#endif

namespace pt {

std::filesystem::path ExecutableDir() {
#ifdef __ANDROID__
    // the app's external files folder: the game files, data/, and the shaders and fonts copied from the APK
    if (const char* external = SDL_GetAndroidExternalStoragePath()) return std::filesystem::path(external);
#endif
    const char* base = SDL_GetBasePath();
    return base ? std::filesystem::path(reinterpret_cast<const char8_t*>(base)) : std::filesystem::current_path();
}

#ifdef __APPLE__
std::filesystem::path ExecutablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()), ec);
    return ec ? std::filesystem::path(buffer.data()) : path;
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

#ifdef __ANDROID__
void PrepareBundledFiles() {
    // manifest.txt (written by android/build_apk.sh): "<size> <path>" per bundled file; SDL opens relative paths from the APK
    size_t size = 0;
    void* manifest = SDL_LoadFile("manifest.txt", &size);
    if (!manifest) return;
    const std::string text(static_cast<const char*>(manifest), size);
    SDL_free(manifest);
    const std::filesystem::path root = ExecutableDir();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        const uintmax_t want = std::stoull(line.substr(0, space));
        const std::string name = line.substr(space + 1);
        const std::filesystem::path target = root / name;
        std::error_code ec;
        if (std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) == want) continue;
        size_t length = 0;
        void* data = SDL_LoadFile(name.c_str(), &length);
        if (!data) continue;
        std::filesystem::create_directories(target.parent_path(), ec);
        SDL_SaveFile(target.string().c_str(), data, length);
        SDL_free(data);
    }
}
#endif

}
