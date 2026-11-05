#pragma once

#include <string_view>

namespace pt {

enum class RenderingPipeline { Original, Enhanced };

constexpr bool EnhancedRenderingAvailable() {
#if defined(__APPLE__) && !PT_IOS
    return true;
#else
    return false;
#endif
}

constexpr RenderingPipeline ResolveRenderingPipeline(std::string_view value) {
    return EnhancedRenderingAvailable() && value == "enhanced" ? RenderingPipeline::Enhanced : RenderingPipeline::Original;
}

constexpr std::string_view RenderingPipelineName(RenderingPipeline pipeline) {
    return pipeline == RenderingPipeline::Enhanced ? "enhanced" : "original";
}

} // namespace pt
