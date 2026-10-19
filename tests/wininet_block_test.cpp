#include <windows.h>

#include <wininet.h>

#include <chrono>
#include <cstdio>
#include <string>

namespace {

std::wstring Setting(const wchar_t* name) {
    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf), type = 0;
    DWORD dword = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", name, RRF_RT_REG_SZ, &type, buf,
                     &size) == ERROR_SUCCESS)
        return buf;
    size = sizeof(dword);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", name, RRF_RT_REG_DWORD, &type,
                     &dword, &size) == ERROR_SUCCESS)
        return std::to_wstring(dword);
    return L"(none)";
}

}

int main(int argc, char** argv) {
    const std::wstring before = Setting(L"ProxyEnable") + L"|" + Setting(L"ProxyServer") + L"|" + Setting(L"AutoConfigURL");
    INTERNET_PROXY_INFO info{};
    info.dwAccessType = INTERNET_OPEN_TYPE_PROXY;
    info.lpszProxy = argc > 1 ? argv[1] : "0.0.0.0:9";
    info.lpszProxyBypass = TEXT("<-loopback>");
    const BOOL set = InternetSetOption(nullptr, INTERNET_OPTION_PROXY, &info, sizeof(info));
    const auto t0 = std::chrono::steady_clock::now();
    HINTERNET handle = InternetOpen(TEXT("reshade"), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    HINTERNET request = handle ? InternetOpenUrl(handle, TEXT("https://api.github.com/repos/crosire/reshade/tags"), nullptr, 0,
                                                 INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_CACHE_WRITE, 0)
                               : nullptr;
    const DWORD error = GetLastError();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (request) InternetCloseHandle(request);
    if (handle) InternetCloseHandle(handle);
    const std::wstring after = Setting(L"ProxyEnable") + L"|" + Setting(L"ProxyServer") + L"|" + Setting(L"AutoConfigURL");
    std::printf("set %d, request %s (error %lu) in %.0f ms, user settings %s\n", set ? 1 : 0, request ? "REACHED THE NETWORK" : "blocked",
                static_cast<unsigned long>(error), ms, before == after ? "unchanged" : "CHANGED");
    return request == nullptr && before == after ? 0 : 1;
}
