#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "engine/platform/http.h"
#include "engine/platform/os.h"
#include "engine/platform/update_check.h"

namespace pt::unlock_io {

inline std::string EnvOr(const char* name, std::string_view fallback) {
    const std::string value = os::GetEnv(name);
    return value.empty() ? std::string(fallback) : value;
}

inline std::filesystem::path FileUrlPath(const std::string& url) {
    return url.compare(0, 5, "file:") == 0 ? os::PathFromUtf8(url.substr(5)) : std::filesystem::path();
}

inline std::optional<std::string> FetchAllowedText(const std::string& url, bool metadata) {
    if (!(metadata ? update::AllowedMetadataUrl(url) : update::AllowedAssetUrl(url))) return std::nullopt;
    http::Request request;
    request.url = url;
    request.timeout_ms = 8000;
    request.follow_redirects = true;
    request.max_body = 256 * 1024;
    const auto response = http::Get(request);
    if (!response || response->status != 200) return std::nullopt;
    return response->body;
}

}
