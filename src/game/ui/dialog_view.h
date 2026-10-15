#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "engine/platform/input.h"
#include "engine/ui/ui_batch.h"

namespace pt::ui {
class UifModel;
}

namespace pt::game {

struct UiCanvas;
struct MenuInput;
class UiAssets;

namespace dialog {

constexpr int kArabic = 9;
inline bool Rtl(int language) { return language == kArabic; }

struct Cell {
    glm::vec2 lo{0.0f};
    glm::vec2 hi{0.0f};
    bool Contains(glm::vec2 p) const { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; }
};

Cell Panel();
Cell Plate(int index, int count, int language);
glm::vec2 UnitsToCanvas(glm::vec2 units);

enum class Result { None, Chosen, Cancelled };

class Nav {
public:
    void Open(int focus, uint32_t held);
    Result Update(const MenuInput& input, int count, int language, bool can_cancel);
    int Focus() const { return focus_; }
    void SetFocus(int focus) { focus_ = focus; }
    int Chosen() const { return chosen_; }
    bool Moved() const { return moved_; }

private:
    int focus_ = 0;
    int chosen_ = -1;
    bool moved_ = false;
    uint32_t held_ = 0;
    glm::vec2 last_pointer_{-1.0f, -1.0f};
    bool fresh_ = true;
};

struct View {
    std::string text;
    std::vector<std::string> choices;
    int focus = 0;
    int language = 0;
    std::string back_label;
    float flash = 1.0f;
    float dim = 0.75f;
    float alpha = 1.0f;
};

void Draw(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, const ui::UifModel* model, const PromptStyle& prompts, const View& view);

}

}
