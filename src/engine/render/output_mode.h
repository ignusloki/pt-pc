#pragma once

namespace pt {

enum class RendererOutputMode { Sdr, ScRgb, Hdr10 };

inline RendererOutputMode ChooseD3D12OutputMode(bool want_hdr, bool display_hdr, bool dlss_framegen) {
    return want_hdr && display_hdr ? (dlss_framegen ? RendererOutputMode::Hdr10 : RendererOutputMode::ScRgb) : RendererOutputMode::Sdr;
}

inline bool D3D12FrameGenSupportsOutput(RendererOutputMode output, bool dlss_framegen) {
    return output == RendererOutputMode::Sdr || (dlss_framegen && output == RendererOutputMode::Hdr10);
}

inline bool D3D12FrameGenFallbackNeedsRecreate(RendererOutputMode output) {
    return output == RendererOutputMode::Hdr10;
}

}
