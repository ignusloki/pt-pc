// Included in each backend namespace with its existing SDK types and guards.
NVSDK_NGX_PerfQuality_Value QualityValue(UpscaleQuality quality) {
    switch (quality) {
    case UpscaleQuality::NativeAA: return NVSDK_NGX_PerfQuality_Value_DLAA;
    case UpscaleQuality::Quality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case UpscaleQuality::Balanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case UpscaleQuality::Performance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case UpscaleQuality::UltraPerformance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    default: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    }
}
