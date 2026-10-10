#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace pt {

// The final image is centered and fitted to the drawable, including integer-pixel rounding.
inline VkRect2D FitRenderViewport(VkExtent2D source, VkExtent2D drawable) {
    if (!source.width || !source.height || !drawable.width || !drawable.height) return {};
    const float scale = std::min(static_cast<float>(drawable.width) / source.width,
                                 static_cast<float>(drawable.height) / source.height);
    const VkExtent2D shown{std::max(1u, static_cast<uint32_t>(source.width * scale)),
                           std::max(1u, static_cast<uint32_t>(source.height * scale))};
    return {{static_cast<int32_t>((drawable.width - shown.width) / 2),
             static_cast<int32_t>((drawable.height - shown.height) / 2)}, shown};
}

// SDL mouse coordinates are in window units; menu hit tests use the rendered image's pixels.
inline std::optional<glm::vec2> WindowToRenderPointer(glm::vec2 pointer, glm::ivec2 window_size,
                                                      VkExtent2D drawable, VkExtent2D render) {
    if (window_size.x <= 0 || window_size.y <= 0) return std::nullopt;
    const VkRect2D viewport = FitRenderViewport(render, drawable);
    if (!viewport.extent.width || !viewport.extent.height) return std::nullopt;
    const glm::vec2 pixels = pointer * glm::vec2(drawable.width, drawable.height) / glm::vec2(window_size);
    // Leave pointers in the borders or outside the window outside the image, so they cannot hit a menu edge.
    return (pixels - glm::vec2(viewport.offset.x, viewport.offset.y)) * glm::vec2(render.width, render.height) /
           glm::vec2(viewport.extent.width, viewport.extent.height);
}

}
