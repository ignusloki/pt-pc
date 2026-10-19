#include "engine/render/output_mode.h"

#include <cstdio>

int main() {
    using pt::RendererOutputMode;
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };
    check(pt::ChooseD3D12OutputMode(true, true, true) == RendererOutputMode::Hdr10,
          "DLSS frame generation selects HDR10 on an HDR display");
    check(pt::D3D12FrameGenSupportsOutput(RendererOutputMode::Hdr10, true),
          "DLSS frame generation accepts HDR10");
    check(pt::ChooseD3D12OutputMode(true, true, false) == RendererOutputMode::ScRgb,
          "other presentation routes retain scRGB");
    check(pt::ChooseD3D12OutputMode(false, true, true) == RendererOutputMode::Sdr,
          "HDR disabled keeps SDR with DLSS frame generation");
    check(pt::ChooseD3D12OutputMode(true, false, true) == RendererOutputMode::Sdr,
          "Windows HDR disabled keeps SDR with DLSS frame generation");
    check(!pt::D3D12FrameGenSupportsOutput(RendererOutputMode::ScRgb, true),
          "DLSS frame generation rejects unsupported scRGB");
    check(!pt::D3D12FrameGenSupportsOutput(RendererOutputMode::Hdr10, false) &&
              !pt::D3D12FrameGenSupportsOutput(RendererOutputMode::ScRgb, false),
          "other frame generation routes retain the HDR guard");
    check(pt::D3D12FrameGenSupportsOutput(RendererOutputMode::Sdr, true) &&
              pt::D3D12FrameGenSupportsOutput(RendererOutputMode::Sdr, false),
          "all frame generation routes retain SDR");
    check(pt::D3D12FrameGenFallbackNeedsRecreate(pt::ChooseD3D12OutputMode(true, true, true)),
          "failed HDR DLSS owner recreates plain output instead of retaining its HDR10 descriptor");
    check(!pt::D3D12FrameGenFallbackNeedsRecreate(RendererOutputMode::Sdr) &&
              !pt::D3D12FrameGenFallbackNeedsRecreate(RendererOutputMode::ScRgb),
          "fallback with an ordinary output keeps the existing plain swapchain");
    return failures ? 1 : 0;
}
