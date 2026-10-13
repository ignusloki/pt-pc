#pragma once

// One HTTPS GET for the update check: WinHTTP on Windows, the system's libcurl (loaded at run
// time, so the game starts without it) elsewhere. Blocking: call it off the game loop.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pt::http {

struct Response {
    int status = 0;
    std::string body;
    std::optional<int64_t> date;  // the Date header as a Unix second
    bool aged = false;            // an Age header above 0: a cached answer
};

struct Request {
    std::string url;  // https only
    int timeout_ms = 5000;
    bool follow_redirects = false;
    size_t max_body = 256 * 1024;
    bool no_cache = false;
};

// nullopt when there is no answer at all (offline, DNS, TLS, timeout, no libcurl)
std::optional<Response> Get(const Request& request);

// One part of a multipart/form-data POST: a field, or a file when filename is set
struct FormPart {
    std::string name;
    std::string filename;
    std::string content_type = "application/octet-stream";
    std::string data;
};
// HTTPS POST of a multipart form (the crash report upload); Windows only, nullopt elsewhere or without an answer
std::optional<Response> PostForm(const std::string& url, const std::vector<FormPart>& parts, int timeout_ms = 60000);
// HTTPS POST of one body with its content type (the log report's Sentry envelope); Windows only, nullopt elsewhere or without
// an answer
std::optional<Response> PostBody(const std::string& url, const std::string& content_type, const std::string& body, int timeout_ms = 60000);

// "Tue, 06 Oct 2026 09:25:00 GMT" as a Unix second
std::optional<int64_t> ParseHttpDate(const std::string& text);

}
