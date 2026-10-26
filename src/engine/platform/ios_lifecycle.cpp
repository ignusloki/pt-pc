#include "engine/platform/ios_lifecycle.h"
#include <utility>
namespace pt {
IosLifecycle::IosLifecycle(std::function<void()> suspend) : suspend_(std::move(suspend)) {
    watching_ = SDL_AddEventWatch(Watch, this);
}
IosLifecycle::~IosLifecycle() {
    if (watching_) SDL_RemoveEventWatch(Watch, this);
}
bool SDLCALL IosLifecycle::Watch(void* userdata, SDL_Event* event) {
    auto& self = *static_cast<IosLifecycle*>(userdata);
    switch (event->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        if (!self.background_.exchange(true) && self.suspend_) self.suspend_();
        break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
        self.background_.store(true);
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        self.resumed_.store(true);
        self.background_.store(false);
        break;
    default: break;
    }
    return true;
}
}
