// Host reproduction of an external scene recursively launching the game.
#include "engine/platform/ios_diagnostics.h"
#include "engine/platform/ios_scene_state.h"
#include <chrono>
#include <functional>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const bool guarded = std::string_view(argv[2]) == "guarded";
    PTIosSceneState state{};
    int entries = 0;
    std::function<void()> launch = [&] {
        if (guarded && !PTIosBeginMain(&state)) return;
        pt::ios_diag::Session session(argv[1], "scene-reentry-reproduction");
        if (++entries == 1) launch();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        pt::ios_diag::FramePresented();
    };
    launch();
    return entries == 1 ? 0 : 3;
}
