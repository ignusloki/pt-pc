#include "setup_core.h"
#include <cstdio>

int main(int argc, char** argv) {
    using namespace pt::setup;
    if (argc != 2) { std::fprintf(stderr, "Usage: installer_update_test <new fixture directory>\n"); return 2; }
    const auto root = fs::absolute(Utf8Path(argv[1]));
    try {
        if (!fs::create_directory(root)) throw std::runtime_error("Choose a new fixture directory.");
        const auto failures = SelfTestUpdate(root / "files") + SelfTestBundleUpdate(root / "bundles");
        std::printf("installer updates: %s%s\n", failures.empty() ? "PASS" : "FAIL", failures.c_str());
        return failures.empty() ? 0 : 1;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
