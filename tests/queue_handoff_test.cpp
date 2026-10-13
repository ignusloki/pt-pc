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
    if (!renderer.SplitForExternal(timelines->inputs, value, timelines->output, value)) return false;
    ++submitted;
    VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &timelines->inputs;
    wait.pValues = &value;
    Expect(vkWaitSemaphores(timelines->device, &wait, 5'000'000'000ull) == VK_SUCCESS,
           "external work starts only after the submitted Vulkan inputs finish");
    VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    signal.semaphore = timelines->output;
    signal.value = value;
    Expect(vkSignalSemaphore(timelines->device, &signal) == VK_SUCCESS, "external work releases the continuation");
    return true;
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
    if (!renderer.BeginFrame(false)) return 1;
    Expect(Handoff(renderer, timelines, 1), "first handoff records a continuation");
    Expect(submitted == 1, "the external boundary submits its Vulkan inputs immediately");
    Expect(!Handoff(renderer, timelines, 2), "a second split cannot overwrite a pending resume buffer");
    renderer.EndFrame(false);
    if (!renderer.BeginFrame(false)) return 1;
    Expect(Handoff(renderer, timelines, 2), "the next frame can use another handoff");
    renderer.EndFrame(false);
    if (!renderer.BeginFrame(false)) return 1;
    uint64_t complete = 0;
    Expect(vkGetSemaphoreCounterValue(timelines->device, timelines->output, &complete) == VK_SUCCESS && complete >= 1,
           "reusing a frame slot waits for its external continuation");
    renderer.EndFrame(false);
    Expect(vkDeviceWaitIdle(renderer.Context().device) == VK_SUCCESS, "backend resources can retire after the consuming queue finishes");
    timelines.reset();
    Expect(retired == 1, "completed backend synchronization resources retire safely");

    // Before the external boundary nothing has been submitted, so abandoning this frame cannot leave external work waiting.
    if (!renderer.BeginFrame(false)) return 1;
    renderer.Shutdown();
    Expect(submitted == 2, "abandoning a frame before its external boundary does not submit work");
    SDL_Quit();
    std::printf("queue handoff: %d failures\n", failures);
    return failures ? 1 : 0;
}
