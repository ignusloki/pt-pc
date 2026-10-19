#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pt::http {

struct Response {
    int status = 0;
    std::string body;
    std::optional<int64_t> date;
    bool aged = false;
};

struct Request {
    std::string url;
    int timeout_ms = 5000;
    bool follow_redirects = false;
    size_t max_body = 256 * 1024;
    bool no_cache = false;
};

std::optional<Response> Get(const Request& request);

struct FormPart {
    std::string name;
    std::string filename;
    std::string content_type = "application/octet-stream";
    std::string data;
};
std::optional<Response> PostForm(const std::string& url, const std::vector<FormPart>& parts, int timeout_ms = 60000);
std::optional<Response> PostBody(const std::string& url, const std::string& content_type, const std::string& body, int timeout_ms = 60000);

struct FetchRequest {
    std::string url;
    int timeout_ms = 30000;
    uint64_t max_bytes = 64ull * 1024 * 1024;
    std::function<bool(const std::string& url)> allow;
    std::function<bool(const char* data, size_t size)> write;
    std::function<void(uint64_t done, uint64_t total)> progress;
    std::function<bool()> cancelled;
    std::string range;
};
struct FetchResult {
    bool ok = false;
    bool range_ignored = false;
    int status = 0;
    uint64_t bytes = 0;
    std::string final_url;
    std::string error;
};
FetchResult Fetch(const FetchRequest& request);
std::string UrlScheme(std::string_view url);
std::string UrlHost(std::string_view url);

std::optional<int64_t> ParseHttpDate(const std::string& text);

}
