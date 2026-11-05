#include "engine/render/renderer.h"
#include <cstdio>

int main() {
    int failures = 0;
    for (auto pipeline : {pt::RenderingPipeline::Original, pt::RenderingPipeline::Enhanced}) {
        pt::Renderer renderer;
        pt::RendererSettings settings;
        settings.headless = true;
        settings.width = settings.height = 64;
        settings.rendering_pipeline = pipeline;
        if (!renderer.Init(nullptr, settings)) return 2;
        const bool enhanced = pipeline == pt::RenderingPipeline::Enhanced;
        const auto format = enhanced ? pt::gpu::Format::R16G16B16A16Sfloat : pt::gpu::Format::R8G8B8A8Unorm;
        const bool correct = renderer.EnhancedRendering() == enhanced && renderer.SceneColor().format == format &&
                             bool(renderer.HdrBoost().image) == !enhanced;
        std::printf("%s %s: scene format %d, legacy HDR gain allocated %s\n", correct ? "PASS" : "FAIL",
                    enhanced ? "Enhanced" : "Original", int(renderer.SceneColor().format), renderer.HdrBoost().image ? "yes" : "no");
        failures += !correct;
        if (!renderer.BeginFrame(false)) return 2;
        renderer.EndFrame(false);
        renderer.Gpu().WaitIdle();
        renderer.Shutdown();
    }
    return failures ? 1 : 0;
}
