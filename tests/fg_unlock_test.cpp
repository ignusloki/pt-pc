#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/render/upscale/fg_unlock.h"

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--install-only") {
        std::string detail;
        const bool ok = pt::fg_unlock::InstallLive(argv[2], argv[3], detail);
        std::printf("%s %s\n", ok ? "installed" : "refused", detail.c_str());
        return ok ? 0 : 1;
    }
    if (argc != 2) {
        std::printf("usage: fg_unlock_test <work dir>\n");
        return 2;
    }
    const std::filesystem::path work = argv[1];
    std::string key, detail;
    const bool ok = pt::fg_unlock::PrepareTool(
        work, [](int) {}, [] { return false; }, &key, &detail);
    if (!ok) {
        std::printf("fail %s %s\n", key.c_str(), detail.c_str());
        return 1;
    }
    std::string source = "?";
    {
        std::ifstream ts(work / "tool-source.txt");
        std::getline(ts, source);
    }
    std::printf("ok %s\n", source.c_str());
    return 0;
}
