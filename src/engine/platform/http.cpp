#include "engine/core/ascii.h"
#include "engine/platform/http.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#endif

namespace pt::http {

std::optional<int64_t> ParseHttpDate(const std::string& text) {
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char month[4] = {};
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    const auto comma = text.find(',');
    const std::string rest = comma == std::string::npos ? text : text.substr(comma + 1);
    if (std::sscanf(rest.c_str(), " %d %3s %d %d:%d:%d", &day, month, &year, &hour, &minute, &second) != 6) return std::nullopt;
    int mon = -1;
    for (int i = 0; i < 12; ++i) {
        if (std::strcmp(month, kMonths[i]) == 0) mon = i;
    }
    if (mon < 0 || year < 1970) return std::nullopt;
    const int y = year - (mon < 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int m = mon + 1;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = int64_t(era) * 146097 + doe - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

std::string UrlScheme(std::string_view url) {
    const auto colon = url.find("://");
    if (colon == std::string_view::npos) return {};
    std::string scheme(url.substr(0, colon));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) { return char(pt::AsciiLower(static_cast<char>(c))); });
    return scheme == "https" || scheme == "http" ? scheme : std::string();
}

std::string UrlHost(std::string_view url) {
    if (UrlScheme(url).empty()) return {};
    std::string_view rest = url.substr(url.find("://") + 3);
    rest = rest.substr(0, rest.find_first_of("/?#"));
    if (rest.find('@') != std::string_view::npos) return {};
    if (!rest.empty() && rest.front() == '[') return {};
    rest = rest.substr(0, rest.find(':'));
    std::string host(rest);
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return char(pt::AsciiLower(static_cast<char>(c))); });
    return host;
}

namespace {
std::string Resolve(const std::string& base, const std::string& location) {
    if (!UrlScheme(location).empty()) return location;
    if (location.starts_with("//")) return UrlScheme(base) + ":" + location;
    if (!location.starts_with("/")) return {};
    const auto start = base.find("://") + 3;
    const auto path = base.find('/', start);
    return base.substr(0, path) + location;
}
bool Redirect(int status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }
}

#ifdef _WIN32

namespace {
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};
std::wstring Widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
}

std::optional<Response> Get(const Request& request) {
    const std::wstring url = Widen(request.url);
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[256] = {}, path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    parts.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return std::nullopt;
    std::wstring resource(path, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Handle session{WinHttpOpen(L"pt-port", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return std::nullopt;
    WinHttpSetTimeouts(session.h, request.timeout_ms, request.timeout_ms, request.timeout_ms, request.timeout_ms);
    Handle connection{WinHttpConnect(session.h, host, parts.nPort, 0)};
    if (!connection.h) return std::nullopt;
    Handle req{WinHttpOpenRequest(connection.h, L"GET", resource.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!req.h) return std::nullopt;
    if (!request.follow_redirects) {
        DWORD off = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(req.h, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof(off));
    }
    const wchar_t* headers = request.no_cache ? L"Cache-Control: no-cache\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
    if (!WinHttpSendRequest(req.h, headers, request.no_cache ? DWORD(-1) : 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req.h, nullptr))
        return std::nullopt;
    Response response;
    DWORD status = 0, bytes = sizeof(status);
    if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &bytes, nullptr)) return std::nullopt;
    response.status = int(status);
    DWORD age = 0;
    bytes = sizeof(age);
    response.aged = WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CUSTOM | WINHTTP_QUERY_FLAG_NUMBER, L"Age", &age, &bytes, nullptr) && age > 0;
    SYSTEMTIME date{};
    bytes = sizeof(date);
    if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_DATE | WINHTTP_QUERY_FLAG_SYSTEMTIME, nullptr, &date, &bytes, nullptr)) {
        FILETIME file{};
        if (SystemTimeToFileTime(&date, &file)) {
            ULARGE_INTEGER ticks{};
            ticks.LowPart = file.dwLowDateTime;
            ticks.HighPart = file.dwHighDateTime;
            response.date = int64_t(ticks.QuadPart / 10000000) - 11644473600LL;
        }
    }
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(req.h, &available) || available == 0) break;
        if (response.body.size() + available > request.max_body) return std::nullopt;
        const size_t at = response.body.size();
        response.body.resize(at + available);
        DWORD read = 0;
        if (!WinHttpReadData(req.h, response.body.data() + at, available, &read)) return std::nullopt;
        response.body.resize(at + read);
    }
    return response;
}

FetchResult Fetch(const FetchRequest& request) {
    FetchResult result;
    std::string url = request.url;
    for (int hop = 0; hop < 6; ++hop) {
        if (!request.allow || !request.allow(url)) {
            result.error = "refused address " + UrlScheme(url) + "://" + UrlHost(url);
            return result;
        }
        const bool secure = UrlScheme(url) == "https";
        const std::wstring wide = Widen(url);
        URL_COMPONENTS parts{sizeof(parts)};
        wchar_t host[256] = {}, path[4096] = {};
        parts.lpszHostName = host;
        parts.dwHostNameLength = 256;
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = 4096;
        parts.dwExtraInfoLength = 1;
        if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP)) {
            result.error = "invalid address";
            return result;
        }
        std::wstring resource(path, parts.dwUrlPathLength);
        if (parts.lpszExtraInfo && parts.dwExtraInfoLength) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        Handle session{WinHttpOpen(L"pt-port", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
        if (!session.h) {
            result.error = "no network";
            return result;
        }
        WinHttpSetTimeouts(session.h, request.timeout_ms, request.timeout_ms, request.timeout_ms, request.timeout_ms);
        Handle connection{WinHttpConnect(session.h, host, parts.nPort, 0)};
        if (!connection.h) {
            result.error = "could not connect";
            return result;
        }
        Handle req{WinHttpOpenRequest(connection.h, L"GET", resource.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      secure ? WINHTTP_FLAG_SECURE : 0)};
        if (!req.h) {
            result.error = "could not connect";
            return result;
        }
        DWORD off = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(req.h, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof(off));
        const std::wstring range = request.range.empty() ? std::wstring() : L"Range: " + Widen(request.range) + L"\r\n";
        if (!WinHttpSendRequest(req.h, range.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : range.c_str(), range.empty() ? 0 : DWORD(-1), nullptr, 0, 0, 0) ||
            !WinHttpReceiveResponse(req.h, nullptr)) {
            result.error = "no answer from " + UrlHost(url) + " (error " + std::to_string(GetLastError()) + ")";
            return result;
        }
        DWORD status = 0, bytes = sizeof(status);
        if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &bytes, nullptr)) {
            result.error = "invalid answer";
            return result;
        }
        result.status = int(status);
        if (Redirect(result.status)) {
            wchar_t location[4096] = {};
            DWORD size = sizeof(location);
            if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_LOCATION, nullptr, location, &size, nullptr)) {
                result.error = "redirect without a target";
                return result;
            }
            const std::wstring target(location, size / sizeof(wchar_t));
            const int n = WideCharToMultiByte(CP_UTF8, 0, target.data(), int(target.size()), nullptr, 0, nullptr, nullptr);
            std::string narrow(size_t(n), '\0');
            WideCharToMultiByte(CP_UTF8, 0, target.data(), int(target.size()), narrow.data(), n, nullptr, nullptr);
            url = Resolve(url, narrow);
            if (url.empty()) {
                result.error = "invalid redirect";
                return result;
            }
            continue;
        }
        if (!request.range.empty() && result.status == 200) {
            result.range_ignored = true;
            result.error = UrlHost(url) + " does not serve parts of a file";
            return result;
        }
        if (result.status != (request.range.empty() ? 200 : 206)) {
            result.error = "HTTP " + std::to_string(result.status) + " from " + UrlHost(url);
            return result;
        }
        result.final_url = url;
        uint64_t total = 0;
        wchar_t length[32] = {};
        DWORD length_size = sizeof(length);
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, length, &length_size, nullptr)) total = _wcstoui64(length, nullptr, 10);
        if (total > request.max_bytes) {
            result.error = "the file is larger than expected";
            return result;
        }
        std::vector<char> buffer(256 * 1024);
        if (request.progress) request.progress(0, total);
        for (;;) {
            if (request.cancelled && request.cancelled()) {
                result.error = "cancelled";
                return result;
            }
            DWORD read = 0;
            if (!WinHttpReadData(req.h, buffer.data(), DWORD(buffer.size()), &read)) {
                result.error = "the download stopped (error " + std::to_string(GetLastError()) + ")";
                return result;
            }
            if (read == 0) break;
            if (result.bytes + read > request.max_bytes) {
                result.error = "the file is larger than expected";
                return result;
            }
            if (request.write && !request.write(buffer.data(), read)) {
                result.error = "could not store the download";
                return result;
            }
            result.bytes += read;
            if (request.progress) request.progress(result.bytes, total);
        }
        if (total && result.bytes != total) {
            result.error = "the download is incomplete";
            return result;
        }
        result.ok = true;
        return result;
    }
    result.error = "too many redirects";
    return result;
}

std::optional<Response> PostForm(const std::string& url_text, const std::vector<FormPart>& form, int timeout_ms) {
    const std::string boundary = "----pt-port-crash-7f3a9c2e5b1d";
    std::string body;
    for (const FormPart& p : form) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + p.name + "\"";
        if (!p.filename.empty()) body += "; filename=\"" + p.filename + "\"";
        body += "\r\n";
        if (!p.filename.empty()) body += "Content-Type: " + p.content_type + "\r\n";
        body += "\r\n";
        body += p.data;
        body += "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return PostBody(url_text, "multipart/form-data; boundary=" + boundary, body, timeout_ms);
}

std::optional<Response> PostBody(const std::string& url_text, const std::string& content_type, const std::string& body, int timeout_ms) {
    const std::wstring url = Widen(url_text);
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[256] = {}, path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    parts.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return std::nullopt;
    std::wstring resource(path, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Handle session{WinHttpOpen(L"pt-port", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return std::nullopt;
    WinHttpSetTimeouts(session.h, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    Handle connection{WinHttpConnect(session.h, host, parts.nPort, 0)};
    if (!connection.h) return std::nullopt;
    Handle req{WinHttpOpenRequest(connection.h, L"POST", resource.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!req.h) return std::nullopt;
    const std::wstring headers = Widen("Content-Type: " + content_type + "\r\n");
    if (!WinHttpSendRequest(req.h, headers.c_str(), DWORD(-1), const_cast<char*>(body.data()), DWORD(body.size()), DWORD(body.size()), 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        return std::nullopt;
    }
    Response response;
    DWORD status = 0, bytes = sizeof(status);
    if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &bytes, nullptr)) return std::nullopt;
    response.status = int(status);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(req.h, &available) || available == 0 || response.body.size() > 65536) break;
        const size_t at = response.body.size();
        response.body.resize(at + available);
        DWORD read = 0;
        if (!WinHttpReadData(req.h, response.body.data() + at, available, &read)) break;
        response.body.resize(at + read);
    }
    return response;
}

#else

std::optional<Response> PostForm(const std::string&, const std::vector<FormPart>&, int) {
    return std::nullopt;
}

std::optional<Response> PostBody(const std::string&, const std::string&, const std::string&, int) {
    return std::nullopt;
}

namespace {
using CURL = void;
struct curl_slist;
enum : int {
    kOptWriteData = 10001, kOptRange = 10007, kOptUrl = 10002, kOptUserAgent = 10018, kOptHttpHeader = 10023, kOptHeaderData = 10029,
    kOptWriteFunction = 20011, kOptHeaderFunction = 20079, kOptFollowLocation = 52, kOptMaxRedirs = 68, kOptNoSignal = 99,
    kOptTimeoutMs = 155, kOptConnectTimeoutMs = 156, kOptProtocolsStr = 10318, kOptLowSpeedLimit = 19, kOptLowSpeedTime = 20,
};
constexpr int kInfoResponseCode = 0x200002;
constexpr int kInfoRedirectUrl = 0x100000 + 31;
struct Curl {
    void* library = nullptr;
    CURL* (*easy_init)() = nullptr;
    int (*easy_setopt)(CURL*, int, ...) = nullptr;
    int (*easy_perform)(CURL*) = nullptr;
    int (*easy_getinfo)(CURL*, int, ...) = nullptr;
    void (*easy_cleanup)(CURL*) = nullptr;
    curl_slist* (*slist_append)(curl_slist*, const char*) = nullptr;
    void (*slist_free_all)(curl_slist*) = nullptr;
    Curl() {
#ifdef __APPLE__
        /* macOS ships libcurl with the system */
        for (const char* name : {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib"}) {
#else
        for (const char* name : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"}) {
#endif
            if ((library = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
        }
        if (!library) return;
        auto load = [&](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(library, name)); };
        load(easy_init, "curl_easy_init");
        load(easy_setopt, "curl_easy_setopt");
        load(easy_perform, "curl_easy_perform");
        load(easy_getinfo, "curl_easy_getinfo");
        load(easy_cleanup, "curl_easy_cleanup");
        load(slist_append, "curl_slist_append");
        load(slist_free_all, "curl_slist_free_all");
    }
    bool Ready() const { return easy_init && easy_setopt && easy_perform && easy_getinfo && easy_cleanup && slist_append && slist_free_all; }
};
struct Sink {
    Response* response;
    size_t max;
    bool overflow = false;
};
size_t OnBody(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<Sink*>(user);
    if (sink->response->body.size() + size * count > sink->max) {
        sink->overflow = true;
        return 0;
    }
    sink->response->body.append(data, size * count);
    return size * count;
}
size_t OnHeader(char* data, size_t size, size_t count, void* user) {
    auto* response = static_cast<Response*>(user);
    std::string line(data, size * count);
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(pt::AsciiLower(static_cast<char>(c))); });
    if (lower.starts_with("http/")) {
        response->date.reset();
        response->aged = false;
    } else if (lower.starts_with("date:")) {
        response->date = ParseHttpDate(line.substr(5));
    } else if (lower.starts_with("age:")) {
        response->aged = std::atol(line.c_str() + 4) > 0;
    }
    return size * count;
}
}

std::optional<Response> Get(const Request& request) {
    static Curl curl;
    if (!curl.Ready() || !request.url.starts_with("https://")) return std::nullopt;
    CURL* handle = curl.easy_init();
    if (!handle) return std::nullopt;
    Response response;
    Sink sink{&response, request.max_body};
    curl_slist* headers = request.no_cache ? curl.slist_append(nullptr, "Cache-Control: no-cache") : nullptr;
    curl.easy_setopt(handle, kOptUrl, request.url.c_str());
    curl.easy_setopt(handle, kOptProtocolsStr, "https");
    curl.easy_setopt(handle, kOptUserAgent, "pt-port");
    curl.easy_setopt(handle, kOptNoSignal, 1L);
    curl.easy_setopt(handle, kOptTimeoutMs, long(request.timeout_ms));
    curl.easy_setopt(handle, kOptConnectTimeoutMs, long(request.timeout_ms));
    curl.easy_setopt(handle, kOptFollowLocation, request.follow_redirects ? 1L : 0L);
    curl.easy_setopt(handle, kOptMaxRedirs, 5L);
    curl.easy_setopt(handle, kOptWriteFunction, &OnBody);
    curl.easy_setopt(handle, kOptWriteData, &sink);
    curl.easy_setopt(handle, kOptHeaderFunction, &OnHeader);
    curl.easy_setopt(handle, kOptHeaderData, &response);
    if (headers) curl.easy_setopt(handle, kOptHttpHeader, headers);
    const int result = curl.easy_perform(handle);
    long status = 0;
    curl.easy_getinfo(handle, kInfoResponseCode, &status);
    curl.easy_cleanup(handle);
    if (headers) curl.slist_free_all(headers);
    if (result != 0 || sink.overflow || status == 0) return std::nullopt;
    response.status = int(status);
    return response;
}

namespace {
struct FetchSink {
    const FetchRequest* request;
    FetchResult* result;
    int status = 0;
    uint64_t total = 0;
    bool failed = false;
    bool range_ignored = false;
    std::string error;
};
size_t OnFetchHeader(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<FetchSink*>(user);
    std::string line(data, size * count);
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(pt::AsciiLower(static_cast<char>(c))); });
    if (lower.starts_with("http/")) {
        const auto space = line.find(' ');
        sink->status = space == std::string::npos ? 0 : std::atoi(line.c_str() + space + 1);
        sink->total = 0;
    } else if (lower.starts_with("content-length:")) {
        sink->total = std::strtoull(line.c_str() + 15, nullptr, 10);
    }
    return size * count;
}
size_t OnFetchBody(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<FetchSink*>(user);
    const size_t n = size * count;
    const FetchRequest& request = *sink->request;
    if (!request.range.empty() && sink->status == 200) {
        sink->failed = true;
        sink->range_ignored = true;
        sink->error = "the server does not serve parts of a file";
        return 0;
    }
    if (sink->status != (request.range.empty() ? 200 : 206)) return n;
    const bool cancelled = request.cancelled && request.cancelled();
    if (cancelled || sink->result->bytes + n > request.max_bytes || sink->total > request.max_bytes) {
        sink->failed = true;
        sink->error = cancelled ? "cancelled" : "the file is larger than expected";
        return 0;
    }
    if (request.write && !request.write(data, n)) {
        sink->failed = true;
        sink->error = "could not store the download";
        return 0;
    }
    sink->result->bytes += n;
    if (request.progress) request.progress(sink->result->bytes, sink->total);
    return n;
}
}

FetchResult Fetch(const FetchRequest& request) {
    static Curl curl;
    FetchResult result;
    if (!curl.Ready()) {
        result.error = "libcurl is not installed";
        return result;
    }
    std::string url = request.url;
    for (int hop = 0; hop < 6; ++hop) {
        if (!request.allow || !request.allow(url)) {
            result.error = "refused address " + UrlScheme(url) + "://" + UrlHost(url);
            return result;
        }
        CURL* handle = curl.easy_init();
        if (!handle) {
            result.error = "libcurl failed";
            return result;
        }
        FetchSink sink{&request, &result};
        curl.easy_setopt(handle, kOptUrl, url.c_str());
        curl.easy_setopt(handle, kOptProtocolsStr, UrlScheme(url) == "http" ? "http" : "https");
        curl.easy_setopt(handle, kOptUserAgent, "pt-port");
        curl.easy_setopt(handle, kOptNoSignal, 1L);
        curl.easy_setopt(handle, kOptConnectTimeoutMs, long(request.timeout_ms));
        curl.easy_setopt(handle, kOptLowSpeedLimit, 1L);
        curl.easy_setopt(handle, kOptLowSpeedTime, long(std::max(1, request.timeout_ms / 1000)));
        curl.easy_setopt(handle, kOptFollowLocation, 0L);
        curl.easy_setopt(handle, kOptWriteFunction, &OnFetchBody);
        curl.easy_setopt(handle, kOptWriteData, &sink);
        curl.easy_setopt(handle, kOptHeaderFunction, &OnFetchHeader);
        curl.easy_setopt(handle, kOptHeaderData, &sink);
        const std::string range = request.range.starts_with("bytes=") ? request.range.substr(6) : request.range;
        if (!range.empty()) curl.easy_setopt(handle, kOptRange, range.c_str());
        if (request.progress) request.progress(0, 0);
        const int code = curl.easy_perform(handle);
        long status = 0;
        curl.easy_getinfo(handle, kInfoResponseCode, &status);
        std::string location;
        if (Redirect(int(status))) {
            char* target = nullptr;
            curl.easy_getinfo(handle, kInfoRedirectUrl, &target);
            if (target) location = target;
        }
        curl.easy_cleanup(handle);
        result.status = int(status);
        if (sink.failed) {
            result.error = sink.error;
            result.range_ignored = sink.range_ignored;
            return result;
        }
        if (code != 0) {
            result.error = "no answer from " + UrlHost(url) + " (curl " + std::to_string(code) + ")";
            return result;
        }
        if (Redirect(result.status)) {
            url = Resolve(url, location);
            if (url.empty()) {
                result.error = "invalid redirect";
                return result;
            }
            continue;
        }
        if (result.status != (request.range.empty() ? 200 : 206)) {
            result.range_ignored = !request.range.empty() && result.status == 200;
            result.error = "HTTP " + std::to_string(result.status) + " from " + UrlHost(url);
            return result;
        }
        if (sink.total && result.bytes != sink.total) {
            result.error = "the download is incomplete";
            return result;
        }
        result.final_url = url;
        result.ok = true;
        return result;
    }
    result.error = "too many redirects";
    return result;
}

#endif

}
