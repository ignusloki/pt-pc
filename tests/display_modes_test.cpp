#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/platform/display_modes.h"
#include "engine/render/render_viewport.h"
#include "game/ui/pc_settings_page.h"
#include "game/ui/uif_view.h"

#include <array>
#include <cassert>

namespace {

class Settings : public pt::game::PcSettingsSource {
public:
    std::vector<pt::game::PcSettingSection> Sections() override {
        return {{"Display", 0, {{1, "Mode"}, {2, "Resolution"}, {3, "VSync"}}},
                {"Graphics", 1, {{4, "Upscaler"}, {5, "Quality"}}}};
    }
    void Set(int, int) override {}
    void Activate(int) override {}
};

glm::vec2 MenuUnits(glm::vec2 pointer, const pt::game::UiCanvas& canvas) {
    const glm::vec2 virtual_px = (pointer - canvas.origin) / canvas.scale;
    return glm::vec2(virtual_px.x - canvas.kWidth * 0.5f, canvas.kHeight * 0.5f - virtual_px.y) / canvas.kUnit;
}

void CheckPointerHits(pt::game::PcSettingsPage& page, glm::ivec2 window_size, VkExtent2D drawable,
                      VkExtent2D render, VkRect2D expected_viewport) {
    const auto viewport = pt::FitRenderViewport(render, drawable);
    assert(viewport.offset.x == expected_viewport.offset.x && viewport.offset.y == expected_viewport.offset.y);
    assert(viewport.extent.width == expected_viewport.extent.width && viewport.extent.height == expected_viewport.extent.height);
    const auto canvas = pt::game::UiCanvas::Fit(render);
    const std::array<glm::vec2, 6> targets{{{-30.0f, 18.0f}, {-30.0f, 15.0f}, {-30.0f, 12.0f},
                                         {35.0f, 18.0f}, {35.0f, 15.0f}, {-57.5f, -31.0f}}};
    for (size_t i = 0; i < targets.size(); ++i) {
        // Start with the actual menu hit regions, project them into the displayed image, then click in SDL window units.
        const glm::vec2 rendered = canvas.FromUnits(targets[i]);
        const glm::vec2 shown = glm::vec2(expected_viewport.offset.x, expected_viewport.offset.y) +
            rendered * glm::vec2(expected_viewport.extent.width, expected_viewport.extent.height) / glm::vec2(render.width, render.height);
        const glm::vec2 mouse = shown * glm::vec2(window_size) / glm::vec2(drawable.width, drawable.height);
        const auto pointer = pt::WindowToRenderPointer(mouse, window_size, drawable, render);
        assert(pointer);
        const glm::vec2 units = MenuUnits(*pointer, canvas);
        assert(glm::length(units - targets[i]) < 0.0001f);
        if (i < 5) assert(page.RowAt(units) == static_cast<int>(i));
        else assert(page.BackAt(units));
    }
}

}

int main() {
    const std::vector<glm::ivec2> listed{{1920, 1080}, {1280, 720}, {1920, 1080}, {0, 0}, {1600, 900}};
    const auto sizes = pt::UniqueDisplaySizes(listed);
    assert((sizes == std::vector<glm::ivec2>{{1280, 720}, {1600, 900}, {1920, 1080}}));
    assert((pt::ClosestDisplaySize(sizes, {1920, 1080}) == glm::ivec2(1920, 1080)));
    assert((pt::ClosestDisplaySize(sizes, {1900, 1050}) == glm::ivec2(1920, 1080)));
    assert((pt::ClosestDisplaySize({}, {1900, 1050}) == glm::ivec2(1900, 1050)));

    Settings source;
    pt::game::PcSettingsPage page;
    page.Open(&source);
    // Window -> borderless -> exclusive -> window on a Retina display, including a changed render resolution.
    CheckPointerHits(page, {1280, 720}, {2560, 1440}, {2560, 1440}, {{0, 0}, {2560, 1440}});
    CheckPointerHits(page, {1512, 945}, {3024, 1890}, {1280, 720}, {{0, 94}, {3024, 1701}});
    CheckPointerHits(page, {1512, 945}, {3024, 1890}, {1920, 1080}, {{0, 94}, {3024, 1701}});
    CheckPointerHits(page, {1512, 945}, {3024, 1890}, {1512, 945}, {{0, 0}, {3024, 1890}});
    CheckPointerHits(page, {1280, 720}, {2560, 1440}, {1280, 720}, {{0, 0}, {2560, 1440}});
    CheckPointerHits(page, {1440, 900}, {2880, 1800}, {2880, 1800}, {{0, 0}, {2880, 1800}});
    // The first transition frame still has the previous render target; do not infer its size from the new setting.
    CheckPointerHits(page, {1512, 945}, {3024, 1890}, {2560, 1440}, {{0, 94}, {3024, 1701}});
    // Standard/fractional DPI, pillarboxing, and asymmetric one-pixel borders use the same mapping.
    CheckPointerHits(page, {1920, 1080}, {1920, 1080}, {1280, 720}, {{0, 0}, {1920, 1080}});
    CheckPointerHits(page, {1536, 864}, {1920, 1080}, {1280, 720}, {{0, 0}, {1920, 1080}});
    CheckPointerHits(page, {3440, 1440}, {3440, 1440}, {1280, 720}, {{440, 0}, {2560, 1440}});
    CheckPointerHits(page, {3019, 1887}, {3019, 1887}, {1920, 1080}, {{0, 94}, {3019, 1698}});

    const auto outside = pt::WindowToRenderPointer({-10.0f, -20.0f}, {1512, 945}, {3024, 1890}, {1280, 720});
    assert(outside && outside->x < 0.0f && outside->y < 0.0f);
    const auto border = pt::WindowToRenderPointer({756.0f, 0.0f}, {1512, 945}, {3024, 1890}, {1280, 720});
    assert(border && border->y < 0.0f);
    assert(page.RowAt(MenuUnits(*border, pt::game::UiCanvas::Fit({1280, 720}))) == -1);
    assert(!pt::WindowToRenderPointer({1, 1}, {0, 720}, {2560, 1440}, {1280, 720}));
    assert(!pt::WindowToRenderPointer({1, 1}, {1280, -1}, {2560, 1440}, {1280, 720}));
    assert(!pt::WindowToRenderPointer({1, 1}, {1280, 720}, {0, 0}, {1280, 720}));
    assert(!pt::WindowToRenderPointer({1, 1}, {1280, 720}, {2560, 1440}, {0, 0}));
}
