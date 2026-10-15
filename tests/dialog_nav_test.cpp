#include "game/ui/dialog_view.h"
#include "game/ui/options_menu.h"

#include <cstdio>

namespace {

int failed = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failed += !ok;
}

pt::game::MenuInput Dir(uint32_t dir) {
    pt::game::MenuInput in;
    in.held_dirs = dir;
    return in;
}

pt::game::MenuInput Over(int index, int count, int language, bool click) {
    const pt::game::dialog::Cell plate = pt::game::dialog::Plate(index, count, language);
    const glm::vec2 centre = (plate.lo + plate.hi) * 0.5f;
    pt::game::MenuInput in;
    in.pointer_valid = true;
    in.pointer = glm::vec2(centre.x - pt::game::UiCanvas::kWidth * 0.5f, pt::game::UiCanvas::kHeight * 0.5f - centre.y) / pt::game::UiCanvas::kUnit;
    in.click = click;
    return in;
}

}

int main() {
    using namespace pt::game;
    using dialog::Nav;
    using dialog::Result;

    {
        const dialog::Cell a = dialog::Plate(0, 2, 0), b = dialog::Plate(1, 2, 0);
        Check(a.hi.x < b.lo.x && a.lo.y == b.lo.y, "two plates sit side by side, the first on the left");
        const dialog::Cell ra = dialog::Plate(0, 2, dialog::kArabic), rb = dialog::Plate(1, 2, dialog::kArabic);
        Check(ra.lo.x > rb.hi.x, "in Arabic the first choice is on the right");
        const dialog::Cell panel = dialog::Panel();
        Check(a.lo.x > panel.lo.x && b.hi.x < panel.hi.x && a.hi.y < panel.hi.y, "the plates are inside the panel");
        const dialog::Cell one = dialog::Plate(0, 1, 0);
        Check(one.lo.x < 640.0f && one.hi.x > 640.0f, "a single plate is centred");
    }
    {
        Nav nav;
        nav.Open(1, 0);
        Check(nav.Focus() == 1, "the focus starts on the safe choice");
        Check(nav.Update(Dir(pt::kRawLeft), 2, 0, true) == Result::None && nav.Focus() == 0 && nav.Moved(), "left moves to Yes");
        nav.Update(Dir(0), 2, 0, true);
        nav.Update(Dir(pt::kRawLeft), 2, 0, true);
        Check(nav.Focus() == 0 && !nav.Moved(), "left at the first plate stays");
        nav.Update(Dir(0), 2, 0, true);
        nav.Update(Dir(pt::kRawRight), 2, 0, true);
        Check(nav.Focus() == 1, "right moves to No");
        nav.Update(Dir(pt::kRawRight), 2, 0, true);
        Check(nav.Focus() == 1, "a direction still held does not step again");
        nav.Update(Dir(0), 2, 0, true);
        nav.Update(Dir(pt::kRawUp), 2, 0, true);
        Check(nav.Focus() == 0, "up goes to the previous choice");
        nav.Update(Dir(0), 2, 0, true);
        nav.Update(Dir(pt::kRawDown), 2, 0, true);
        Check(nav.Focus() == 1, "down goes to the next choice");
    }
    {
        Nav nav;
        nav.Open(0, pt::kRawRight);
        nav.Update(Dir(pt::kRawRight), 2, 0, true);
        Check(nav.Focus() == 0, "a direction held from before the dialog opened is ignored");
    }
    {
        Nav nav;
        nav.Open(0, 0);
        nav.Update(Dir(pt::kRawLeft), 2, dialog::kArabic, true);
        Check(nav.Focus() == 1, "in Arabic left moves to the second choice");
        nav.Update(Dir(0), 2, dialog::kArabic, true);
        nav.Update(Dir(pt::kRawRight), 2, dialog::kArabic, true);
        Check(nav.Focus() == 0, "in Arabic right moves back to the first choice");
    }
    {
        Nav nav;
        nav.Open(1, 0);
        pt::game::MenuInput accept;
        accept.accept = true;
        Check(nav.Update(accept, 2, 0, true) == Result::Chosen && nav.Chosen() == 1, "accept chooses the focused plate");
        pt::game::MenuInput back;
        back.back = true;
        Check(nav.Update(back, 2, 0, true) == Result::Cancelled, "back cancels");
        pt::game::MenuInput close;
        close.close = true;
        Check(nav.Update(close, 2, 0, true) == Result::Cancelled, "the pause button cancels");
        pt::game::MenuInput right;
        right.right_click = true;
        Check(nav.Update(right, 2, 0, true) == Result::Cancelled, "the right mouse button cancels");
        Check(nav.Update(back, 1, 0, false) == Result::None, "back does nothing where the dialog has no way out");
    }
    {
        Nav nav;
        nav.Open(1, 0);
        Check(nav.Update(Over(0, 2, 0, false), 2, 0, true) == Result::None && nav.Focus() == 1 && !nav.Moved(),
              "a pointer resting over a plate when the dialog opens does not take the focus");
        pt::game::MenuInput aside = Over(0, 2, 0, false);
        aside.pointer.y += 1.0f;
        Check(nav.Update(aside, 2, 0, true) == Result::None && nav.Focus() == 0 && nav.Moved(), "moving the pointer over the first plate focuses it");
        Check(nav.Update(Over(1, 2, 0, true), 2, 0, true) == Result::Chosen && nav.Chosen() == 1 && nav.Focus() == 1, "a click chooses the plate under the pointer");
        pt::game::MenuInput beside;
        beside.pointer_valid = true;
        beside.pointer = glm::vec2(0.0f, 30.0f);
        beside.click = true;
        Check(nav.Update(beside, 2, 0, true) == Result::None, "a click beside the plates chooses nothing");
        Check(nav.Update(Over(0, 2, dialog::kArabic, true), 2, dialog::kArabic, true) == Result::Chosen && nav.Chosen() == 0,
              "in Arabic the plate on the right is the first choice");
        Nav single;
        single.Open(0, 0);
        Check(single.Update(Over(0, 1, 0, true), 1, 0, true) == Result::Chosen, "a click on the single OK chooses it");
    }
    return failed ? 1 : 0;
}
