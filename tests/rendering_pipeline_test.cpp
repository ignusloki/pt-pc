#include "engine/platform/settings.h"
#include "engine/platform/graphics_presets.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path dir(argv[1]);
    std::filesystem::create_directories(dir);
    const auto path = dir / "pipeline.ini";
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };
    auto load = [&](const char* text) {
        std::ofstream(path) << text;
        pt::AppSettings s;
        check("load profile", pt::LoadAppSettings(path, s));
        return s;
    };
    auto s = load("[graphics]\nfilm_grain=0.25\nclarity=0.4\n");
    check("old settings start in Original", s.active_pipeline == pt::RenderingPipeline::Original && s.display.rendering_pipeline == "original");
    check("old effect choices survive", s.graphics.film_grain == .25f && s.graphics.clarity == .4f);
    s.display.rendering_pipeline = "enhanced";
    s.graphics.clarity = .2f;
    s.enhanced_graphics.film_grain = .75f;
    s.enhanced_graphics.camera_motion_blur = false;
    check("save pending selection", pt::SaveAppSettings(path, s));
    pt::AppSettings enhanced;
    check("reload selected profile", pt::LoadAppSettings(path, enhanced));
    if (pt::EnhancedRenderingAvailable()) {
        check("Enhanced takes effect on restart", enhanced.active_pipeline == pt::RenderingPipeline::Enhanced);
        check("pending save preserves both profiles", enhanced.original_graphics.clarity == .2f && enhanced.graphics.film_grain == .75f);
        check("object-only choice survives", !enhanced.graphics.camera_motion_blur);
        enhanced.graphics.clarity = .6f;
        enhanced.display.rendering_pipeline = "original";
        check("save return to Original", pt::SaveAppSettings(path, enhanced));
        pt::AppSettings original;
        check("reload Original", pt::LoadAppSettings(path, original));
        check("return restores Original choices", original.active_pipeline == pt::RenderingPipeline::Original && original.graphics.clarity == .2f && original.graphics.film_grain == .25f);
        check("Enhanced changes stay separate", original.enhanced_graphics.clarity == .6f && original.enhanced_graphics.film_grain == .75f);
        original.display.rendering_pipeline = "enhanced";
        check("save second selection", pt::SaveAppSettings(path, original));
        check("reload second selection", pt::LoadAppSettings(path, enhanced));
        check("second restart restores Enhanced choices", enhanced.graphics.clarity == .6f);
        pt::ApplyGraphicsPreset(enhanced, pt::GraphicsPreset::High, false);
        check("Enhanced High recognized", pt::DetectGraphicsPreset(enhanced, false) == pt::GraphicsPreset::High);
        check("Enhanced preset keeps Original stored", enhanced.original_graphics.clarity == .2f);
    } else {
        check("unsupported platforms retain Original", enhanced.active_pipeline == pt::RenderingPipeline::Original && enhanced.graphics.clarity == .2f);
    }
    s = load("[display]\nrendering_pipeline=unknown\n");
    check("unknown mode falls back to Original", s.active_pipeline == pt::RenderingPipeline::Original && s.display.rendering_pipeline == "original");
    s = load("[display]\nrendering_pipeline=enhanced\nhdr_sdr_eotf=99\nhdr_ui_brightness=-1\n[hdr_color]\nexposure=nan\ncontrast=300\n[enhanced_graphics]\nfilm_grain=nan\nclarity=nan\nbloom_strength=inf\nlut_sampling=99\n");
    check("invalid grading is normalized", s.display.hdr_color.exposure == 0 && s.display.hdr_color.contrast == 2);
    check("invalid calibration is bounded", s.display.hdr_sdr_eotf == 2 && s.display.hdr_ui_brightness == 80);
    check("invalid enhanced controls are normalized", s.enhanced_graphics.film_grain == 1 && s.enhanced_graphics.clarity == 0 && s.enhanced_graphics.bloom_strength == 1 && s.enhanced_graphics.lut_sampling == 1);
    return failures ? 1 : 0;
}
