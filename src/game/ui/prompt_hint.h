#pragma once

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "engine/platform/input.h"
#include "engine/ui/text_layout.h"
#include "engine/ui/ui_batch.h"
#include "game/ui/ui_icons.h"

namespace pt::game {

struct UiCanvas;
struct UiFont;
class UiAssets;

struct PromptLine {
    std::string text;
    std::vector<Prompt> pictures;
};

PromptLine ExpandPromptHint(std::string_view hint, bool keyboard);
inline PromptLine ExpandPromptHint(std::string_view hint, const PromptStyle& style) {
    return ExpandPromptHint(hint, style.device == PromptDevice::Keyboard);
}
bool HasPromptTokens(std::string_view text);

std::vector<float> PromptAdvances(UiAssets& assets, const PromptLine& line, const PromptStyle& style, float font_height);

void DrawPromptPictures(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, const UiFont& font, const ui::TextLayout& layout,
                        const PromptLine& line, const PromptStyle& prompts, float alpha);

void DrawPromptText(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, UiFont& font, std::string_view hint, const PromptStyle& prompts,
                    float size, glm::vec2 lo, glm::vec2 hi, ui::TextAlign align, ui::TextAlign vertical, glm::vec4 color, bool fit, float line_space = 0.0f);

}
