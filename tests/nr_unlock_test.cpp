#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/render/upscale/neural_unlock.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: nr_unlock_test <work dir>\n");
        return 2;
    }
    const std::filesystem::path work = argv[1];
    std::string key, detail;
    const bool ok = pt::nr_unlock::PrepareTool(
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
    const std::string sha = pt::nr_unlock::FileSha256(pt::nr_unlock::ToolPath(work));
    std::printf("ok %s %s\n", source.c_str(), sha.c_str());
    return 0;
}
