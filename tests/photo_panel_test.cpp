#ifdef NDEBUG
#undef NDEBUG
#endif
#include "game/ui/photo_panel.h"

#include <cassert>

#include "engine/platform/input.h"
#include "game/ui/pc_settings.h"

using namespace pt::game;

namespace {
const PhotoPanelRow* Selected(const PhotoPanelView& view) {
    for (const PhotoPanelRow& row : view.rows) if (row.selected) return &row;
    return nullptr;
}
}

int main() {
    PhotoPanel panel;
    PhotoSettings settings;
    panel.Open(settings);
    auto view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->value == "Capture");

    auto tap = [&](uint32_t key) {
        panel.Update(static_cast<GameAudio*>(nullptr), {key, false}, 1.0f / 60.0f);
        panel.Update(static_cast<GameAudio*>(nullptr), {{}, false}, 1.0f / 60.0f);
    };
    tap(pt::kRawUp);
    view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->label == PcText("pc_photo_reset_settings", 0));
    tap(pt::kRawDown);
    view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->value == "Capture");

    tap(pt::kRawDown);
    tap(pt::kRawRight);
    view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->value == "14 mm");
    tap(pt::kRawDown);
    tap(pt::kRawLeft);
    view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->value == "-5");

    for (int i = 0; i < 11; ++i) tap(pt::kRawDown);
    view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->value == "Off");
    for (int i = 0; i < 3; ++i) tap(pt::kRawRight);
    view = panel.View(0, "", 16.0f / 9.0f);
    assert(Selected(view) && Selected(view)->value == "4:3");
    assert(view.crop.width < 1.0f && view.crop.height == 1.0f);
    tap(pt::kRawDown);
    view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->value == PcText("pc_photo_resolution_native", 0));
    tap(pt::kRawRight);
    assert(Selected(panel.View(0, "")) && Selected(panel.View(0, ""))->value == PcText("pc_photo_resolution_4k", 0));
    tap(pt::kRawDown);
    tap(pt::kRawRight);
    view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->value != "Off");

    panel.Settings().speed = 2;
    panel.Settings().focus = 4;
    panel.Settings().aperture = 3;
    panel.Settings().exposure = 9;
    panel.Settings().depth_of_field = false;
    panel.Settings().lens = panel.Settings().grain = panel.Settings().grading = false;
    panel.Settings().body = panel.Settings().flashlight = false;
    settings.bloom = false;
    panel.Open(settings);
    assert(panel.Settings().focal_length_mm == 14.0f && panel.Settings().roll == -5);
    assert(panel.Settings().aspect == static_cast<int>(PhotoAspectPreset::FourByThree));
    assert(panel.Settings().resolution == PhotoResolution::FourK && panel.Settings().filter != PhotoColorFilter::Off);
    assert(panel.Settings().speed == 2 && panel.Settings().focus == 4 && panel.Settings().aperture == 3 && panel.Settings().exposure == 9);
    assert(!panel.Settings().depth_of_field && !panel.Settings().lens && !panel.Settings().grain && !panel.Settings().grading);
    assert(!panel.Settings().body && !panel.Settings().flashlight);
    tap(pt::kRawUp);
    for (int language = 0; language < 14; ++language) {
        const auto localized = panel.View(language, "");
        assert(Selected(localized) && Selected(localized)->label == PcText("pc_photo_reset_settings", language));
        assert(!Selected(localized)->adjustable && localized.note == PcText("pc_note_photo_reset_settings", language));
    }
    assert(panel.Update(static_cast<GameAudio*>(nullptr), {{}, true}, 1.0f / 60.0f) == PhotoPanel::Action::ResetSettings);
    assert(panel.Settings().focal_length_mm == 12.0f && panel.Settings().roll == 0);
    assert(!panel.Settings().bloom && panel.Settings().aspect == 0 && panel.Settings().resolution == PhotoResolution::Native);
    assert(panel.Settings().filter == PhotoColorFilter::Off && panel.Settings().body && panel.Settings().flashlight);
    assert(panel.Settings().speed == 1 && panel.Settings().focus == 0 && panel.Settings().aperture == 0);
    assert(panel.Settings().exposure == PhotoSettings::kExposureZero && panel.Settings().depth_of_field);
    assert(panel.Settings().lens && panel.Settings().grain && panel.Settings().grading);
    panel.Open(settings);
    assert(!panel.Settings().bloom && panel.Settings().filter == PhotoColorFilter::Off);

    panel = PhotoPanel{};
    settings.roll = 90;
    panel.Open(settings);
    tap(pt::kRawDown);
    tap(pt::kRawDown);
    view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->value == "+90");
    tap(pt::kRawRight);
    assert(Selected(panel.View(0, "")) && Selected(panel.View(0, ""))->value == "+90");
    settings.roll = -90;
    panel = PhotoPanel{};
    panel.Open(settings);
    tap(pt::kRawDown);
    tap(pt::kRawDown);
    view = panel.View(0, "");
    assert(Selected(view) && Selected(view)->value == "-90");
    tap(pt::kRawLeft);
    assert(Selected(panel.View(0, "")) && Selected(panel.View(0, ""))->value == "-90");
}
