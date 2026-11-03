#include "engine/platform/ios_lifecycle.h"
#include <cassert>
#include <thread>
#include <chrono>
int main() {
    assert(SDL_Init(SDL_INIT_EVENTS));
    unsigned drained = 0;
    {
        pt::IosLifecycle lifecycle([&] { ++drained; });
        assert(lifecycle.Watching());
        const auto send = [](Uint32 type) {
            SDL_Event event{}; event.type = type;
            assert(SDL_PushEvent(&event));
        };
        send(SDL_EVENT_WILL_ENTER_BACKGROUND);
        // The watch must drain GPU work synchronously, without waiting for SDL_PollEvent.
        assert(lifecycle.Background() && drained == 1);
        send(SDL_EVENT_DID_ENTER_BACKGROUND);
        assert(lifecycle.Background() && drained == 1);
        send(SDL_EVENT_WILL_ENTER_FOREGROUND);
        assert(lifecycle.Background() && !lifecycle.TakeResume());
        send(SDL_EVENT_DID_ENTER_FOREGROUND);
        assert(!lifecycle.Background() && lifecycle.TakeResume() && !lifecycle.TakeResume());
        send(SDL_EVENT_WILL_ENTER_BACKGROUND);
        assert(drained == 2);
#if PT_IOS
        SDL_Delay(60); // Pass the loading-event throttle.
        std::thread foreground([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            send(SDL_EVENT_WILL_ENTER_FOREGROUND);
            send(SDL_EVENT_DID_ENTER_FOREGROUND);
        });
        pt::PumpIosLoadingEvents();
        foreground.join();
        assert(!lifecycle.Background() && lifecycle.TakeResume());
#endif
    }
    SDL_Event event{}; event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
    assert(SDL_PushEvent(&event)); // watcher was removed before its state was destroyed
    SDL_Quit();
}
