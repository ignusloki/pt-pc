#pragma once

#if defined(__APPLE__) && !PT_IOS
#include <filesystem>
#include <system_error>

namespace pt::platform {

std::filesystem::path ResolveMacDataPath(const std::filesystem::path& path, std::error_code& error);

}
#endif
