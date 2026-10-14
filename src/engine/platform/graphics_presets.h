#pragma once
#include "engine/platform/settings.h"
namespace pt {
enum class GraphicsPreset { Low, Original, High, Ultra, Custom };
#ifdef __ANDROID__
inline constexpr bool kEnhancedTexturesSupported = false;
#else
inline constexpr bool kEnhancedTexturesSupported = true;
#endif
void ApplyGraphicsPreset(AppSettings& settings, GraphicsPreset preset, bool ray_tracing_supported,
                         bool enhanced_textures_supported = kEnhancedTexturesSupported);
GraphicsPreset DetectGraphicsPreset(const AppSettings& settings, bool ray_tracing_supported,
                                    bool enhanced_textures_supported = kEnhancedTexturesSupported);
}
