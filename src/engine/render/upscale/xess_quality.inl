// Included in each backend namespace with its existing SDK types and guards.
xess_quality_settings_t XessQuality(UpscaleQuality quality, VkExtent2D render, VkExtent2D display) {
    switch (quality) {
    case UpscaleQuality::NativeAA: return XESS_QUALITY_SETTING_AA;
    case UpscaleQuality::Quality: return XESS_QUALITY_SETTING_ULTRA_QUALITY;
    case UpscaleQuality::Balanced: return XESS_QUALITY_SETTING_QUALITY;
    case UpscaleQuality::Performance: return XESS_QUALITY_SETTING_BALANCED;
    case UpscaleQuality::UltraPerformance: return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
    default: break;
    }
    struct Preset {
        float ratio;
        xess_quality_settings_t setting;
    };
    constexpr Preset presets[] = {{1.0f, XESS_QUALITY_SETTING_AA},          {1.3f, XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS},
                                  {1.5f, XESS_QUALITY_SETTING_ULTRA_QUALITY}, {1.7f, XESS_QUALITY_SETTING_QUALITY},
                                  {2.0f, XESS_QUALITY_SETTING_BALANCED},     {2.3f, XESS_QUALITY_SETTING_PERFORMANCE},
                                  {3.0f, XESS_QUALITY_SETTING_ULTRA_PERFORMANCE}};
    const float ratio = static_cast<float>(display.width) / static_cast<float>(std::max(render.width, 1u));
    const Preset* best = &presets[0];
    for (const Preset& p : presets) {
        if (std::abs(p.ratio - ratio) < std::abs(best->ratio - ratio)) {
            best = &p;
        }
    }
    return best->setting;
}
