#include <chrono>
#include <cstdio>
#include <thread>

#include <SDL3/SDL.h>

#include "engine/voice/microphone.h"

namespace {
int failures = 0;
void Expect(bool value, const char* what) {
    if (!value) { ++failures; std::printf("FAIL %s\n", what); }
}
pt::Microphone::AsyncState FinishOpen(pt::Microphone& microphone) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    auto state = microphone.OpenAsync(16000);
    while (state == pt::Microphone::AsyncState::Pending && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        state = microphone.OpenAsync(16000);
    }
    return state;
}
}

int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_AUDIO)) return 1;
    Expect(std::string(SDL_GetCurrentAudioDriver()) == "dummy", "fixture uses no physical microphone");
    for (int i = 0; i < 20; ++i) {
        pt::Microphone microphone;
        Expect(FinishOpen(microphone) == pt::Microphone::AsyncState::Opened, "completed asynchronous open transfers the stream");
        Expect(microphone.IsOpen(), "completed stream is usable");
        microphone.Close();
        Expect(!microphone.IsOpen(), "close drops the completed stream");
        microphone.OpenAsync(16000);
        microphone.Close();
        Expect(!microphone.IsOpen(), "cancellation never transfers the cancelled stream");
        Expect(FinishOpen(microphone) == pt::Microphone::AsyncState::Opened, "a cancelled open can be retried");
    }
    for (int i = 0; i < 20; ++i) {
        pt::Microphone microphone;
        microphone.OpenAsync(16000);
        // Destruction must join the worker before SDL's global audio state is shut down.
    }
    SDL_Quit();
    std::printf("asynchronous microphone: %d failures\n", failures);
    return failures ? 1 : 0;
}
