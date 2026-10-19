#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

int wmain(int argc, wchar_t** argv) {
    bool install = false, remove = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--route") install = true;
        if (std::wstring(argv[i]) == L"--remove") remove = true;
    }
    if (!install && !remove) {
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::ofstream out(fs::path(self).parent_path() / L"started.txt", std::ios::app);
        for (int i = 1; i < argc; ++i) out << fs::path(argv[i]).string() << "\n";
        out << "--\n";
        return 0;
    }
    const fs::path dir = fs::path(argv[1]).parent_path();
    const char* names[] = {"dxgi.dll", "renodx-dlss5.addon64", "nvngx_dlssnr.dll"};
    if (const char* delay = std::getenv("PT_FAKE_TOOL_DELAY_MS")) Sleep(DWORD(std::atoi(delay)));
    std::error_code ec;
    if (remove) {
        for (const char* n : names) fs::remove(dir / n, ec);
        fs::remove(dir / "dlss5-autopilot.json", ec);
        std::printf("removed 3 files\n");
        return 0;
    }
    if (const char* code = std::getenv("PT_FAKE_TOOL_EXIT"); code && std::atoi(code) != 0) {
        std::printf("fake installer failed\n");
        return std::atoi(code);
    }
    for (int i = 0; i < 3; ++i) {
        std::ofstream(dir / names[i], std::ios::binary) << "fake autopilot file " << names[i];
        std::printf("[%d/3] %s\n", i + 1, names[i]);
    }
    std::ofstream(dir / "dlss5-autopilot.json", std::ios::binary)
        << "{\"path\": \"native\", \"components\": {\"reshade\": \"6.8.0\", \"renodx\": \"4.55\", \"dlssnr\": \"310.8\"}, "
           "\"files\": [\"dxgi.dll\", \"renodx-dlss5.addon64\", \"nvngx_dlssnr.dll\"]}";
    return 0;
}
