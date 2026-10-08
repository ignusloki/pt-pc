#include <cstdio>
#include <memory>

#include <SDL3/SDL.h>

#include "engine/render/renderer.h"

namespace {
int failures = 0;
int retired = 0;
int submitted = 0;

void Expect(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

struct Timelines {
    VkDevice device;
    VkSemaphore inputs = VK_NULL_HANDLE;
    VkSemaphore output = VK_NULL_HANDLE;
    ~Timelines() {
        if (inputs) vkDestroySemaphore(device, inputs, nullptr);
        if (output) vkDestroySemaphore(device, output, nullptr);
        ++retired;
    }
};

std::shared_ptr<Timelines> CreateTimelines(VkDevice device) {
    auto timelines = std::make_shared<Timelines>();
    timelines->device = device;
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
    if (vkCreateSemaphore(device, &info, nullptr, &timelines->inputs) != VK_SUCCESS ||
        vkCreateSemaphore(device, &info, nullptr, &timelines->output) != VK_SUCCESS) return {};
    return timelines;
}

bool Handoff(pt::Renderer& renderer, const std::shared_ptr<Timelines>& timelines, uint64_t value) {
    return renderer.QueueHandoff(renderer.Cmd(), timelines->inputs, value, timelines->output, value, [timelines, value] {
        ++submitted;
        VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.semaphoreCount = 1;
        wait.pSemaphores = &timelines->inputs;
        wait.pValues = &value;
        Expect(vkWaitSemaphores(timelines->device, &wait, 5'000'000'000ull) == VK_SUCCESS,
               "external work starts only after the Vulkan inputs finish");
        VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        signal.semaphore = timelines->output;
        signal.value = value;
        Expect(vkSignalSemaphore(timelines->device, &signal) == VK_SUCCESS, "external work releases the continuation");
    }) != VK_NULL_HANDLE;
}
}

int main() {
    if (!SDL_Init(0)) return 1;
    pt::Renderer renderer;
    pt::RendererSettings settings;
    settings.headless = true;
    settings.width = 64;
    settings.height = 64;
    if (!renderer.Init(nullptr, settings)) return 1;
    auto timelines = CreateTimelines(renderer.Context().device);
    if (!timelines) return 1;
    std::weak_ptr<Timelines> lifetime = timelines;
    if (!renderer.BeginFrame(false)) return 1;
    Expect(Handoff(renderer, timelines, 1), "first handoff records a continuation");
    Expect(Handoff(renderer, timelines, 2), "a second handoff can follow the first in the same frame");
    Expect(submitted == 0, "recording does not submit external work");
    timelines.reset();
    renderer.EndFrame(false);
    Expect(submitted == 2, "both external submissions execute");
    Expect(!lifetime.expired(), "synchronization resources remain alive until the frame fence retires them");
    if (!renderer.BeginFrame(false)) return 1;
    renderer.EndFrame(false);
    if (!renderer.BeginFrame(false)) return 1;
    Expect(lifetime.expired() && retired == 1, "reusing the frame waits for completion before releasing synchronization resources");
    renderer.EndFrame(false);

    // Abandon a recorded frame: no command on the external queue should be waiting for unsubmitted Vulkan inputs.
    timelines = CreateTimelines(renderer.Context().device);
    if (!timelines || !renderer.BeginFrame(false)) return 1;
    Expect(Handoff(renderer, timelines, 1), "abandoned frame records a handoff");
    lifetime = timelines;
    timelines.reset();
    renderer.Shutdown();
    Expect(submitted == 2, "abandoning the frame does not commit external work");
    Expect(lifetime.expired() && retired == 2, "shutdown releases the abandoned handoff before destroying the device");
    SDL_Quit();
    std::printf("queue handoff: %d failures\n", failures);
    return failures ? 1 : 0;
}
