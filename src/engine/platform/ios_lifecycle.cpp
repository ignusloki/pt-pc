#include "engine/platform/ios_lifecycle.h"
#include "engine/platform/ios_diagnostics.h"
#include <utility>
namespace pt {
#if PT_IOS
namespace { IosLifecycle* active_lifecycle = nullptr; }

void PumpIosLoadingEvents() {
    if (!SDL_IsMainThread() || !active_lifecycle) return;
    static Uint64 last = 0;
    const Uint64 now = SDL_GetTicksNS();
    if (now - last < 50000000) return;
    last = now;
    SDL_PumpEvents();
    // The event watch drains the GPU before backgrounding; resume before uploading more.
    while (active_lifecycle && active_lifecycle->Background()) {
        SDL_Delay(16);
        SDL_PumpEvents();
    }
}
#endif
IosLifecycle::IosLifecycle(std::function<void()> suspend) : suspend_(std::move(suspend)) {
    watching_ = SDL_AddEventWatch(Watch, this);
#if PT_IOS
    if (watching_) active_lifecycle = this;
#endif
}
IosLifecycle::~IosLifecycle() {
#if PT_IOS
    if (active_lifecycle == this) active_lifecycle = nullptr;
#endif
    if (watching_) SDL_RemoveEventWatch(Watch, this);
}
bool SDLCALL IosLifecycle::Watch(void* userdata, SDL_Event* event) {
    auto& self = *static_cast<IosLifecycle*>(userdata);
    switch (event->type) {
    case SDL_EVENT_LOW_MEMORY:
        ios_diag::Event("iOS low-memory notification");
        break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        ios_diag::Event("entering background");
        if (!self.background_.exchange(true) && self.suspend_) self.suspend_();
        break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
        ios_diag::Event("will enter foreground");
        self.background_.store(true);
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        ios_diag::Event("did enter foreground");
        self.resumed_.store(true);
        self.background_.store(false);
        break;
    default: break;
    }
    return true;
}
}
