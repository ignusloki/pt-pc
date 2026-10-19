#pragma once

#ifdef __APPLE__
#include <filesystem>
#include <system_error>

namespace pt::platform {

std::filesystem::path ResolveMacDataPath(const std::filesystem::path& path, std::error_code& error);

}
#endif
