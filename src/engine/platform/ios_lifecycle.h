#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <functional>

namespace pt {
#if PT_IOS
// Called between loading operations on the main thread, outside GPU submissions.
void PumpIosLoadingEvents();
#else
inline void PumpIosLoadingEvents() {}
#endif
// SDL requires iOS lifecycle handling in an event watch, before UIKit's notification returns.
class IosLifecycle {
public:
    explicit IosLifecycle(std::function<void()> suspend);
    ~IosLifecycle();
    IosLifecycle(const IosLifecycle&) = delete;
    IosLifecycle& operator=(const IosLifecycle&) = delete;
    bool Watching() const { return watching_; }
    bool Background() const { return background_.load(); }
    bool TakeResume() { return resumed_.exchange(false); }
private:
    static bool SDLCALL Watch(void* userdata, SDL_Event* event);
    std::function<void()> suspend_;
    std::atomic<bool> background_{false};
    std::atomic<bool> resumed_{false};
    bool watching_ = false;
};
}
