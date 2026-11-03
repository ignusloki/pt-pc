// Host smoke test of the same recorder used in the iOS diagnostic build.
#include "engine/platform/ios_diagnostics.h"
#include <CoreFoundation/CoreFoundation.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::filesystem::path root(argv[1]);
    const bool abrupt = std::string_view(argv[2]) == "abrupt";
    {
        pt::ios_diag::Session session(root, "host-smoke-diagnostic-3");
        pt::ios_diag::RecordImages();
        pt::ios_diag::Stage("synthetic blocked main thread");
        {
            pt::ios_diag::Scope model("model", "synthetic-model");
            {
                pt::ios_diag::Scope texture("texture", "synthetic-texture");
                texture.Step("synthetic decoder work");
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                std::ifstream in(root / "ios-diagnostics/last-state.txt");
                const std::string state{std::istreambuf_iterator<char>(in), {}};
                if (state.find("main-returned=no") == std::string::npos || state.find("synthetic-model") == std::string::npos ||
                    state.find("synthetic-texture") == std::string::npos || state.find("synthetic decoder work") == std::string::npos)
                    return 3;
                if (abrupt) std::_Exit(0); // Keep the interrupted session's files for the next launch test.
            }
            pt::ios_diag::Event("texture scope returned");
            std::ifstream in(root / "ios-diagnostics/last-state.txt");
            const std::string state{std::istreambuf_iterator<char>(in), {}};
            if (state.find("synthetic-model") == std::string::npos || state.find("synthetic-texture") != std::string::npos) return 4;
        }
        for (int i = 0; i < 4; ++i) CFRunLoopRunInMode(kCFRunLoopDefaultMode, .02, false);
        pt::ios_diag::FramePresented();
    }
    std::ifstream in(root / "ios-diagnostics/last-state.txt");
    const std::string state{std::istreambuf_iterator<char>(in), {}};
    return state.find("main-returned=yes") != std::string::npos && state.find("frames=1") != std::string::npos &&
           state.find("synthetic-model") == std::string::npos ? 0 : 5;
}
