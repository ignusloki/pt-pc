#include "engine/render/model_cache.h"
#include "game/game.h"
#include "game/ui/options_menu.h"
#include "game/ui/pc_settings_page.h"

#include <cstdio>
#include <memory>

namespace {
using namespace pt::game;
int failed = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failed += !ok;
}

PcSettingRow Row(int id, const char* label, bool action = false) {
    PcSettingRow row;
    row.id = id;
    row.label = label;
    row.values = {"value"};
    row.action = action;
    row.confirm_default_yes = true;
    return row;
}

struct Source : PcSettingsSource {
    std::vector<PcSettingRow> rows{Row(10, "first"), Row(20, "focused", true)};
    std::optional<PcSettingRow> footer;
    std::optional<PcSettingRow> FooterAction() override { return footer; }
    bool tabbed = false;
    int activated = -1;
    std::vector<PcSettingSection> Sections() override { return {{"section", 0, rows}}; }
    void Set(int, int) override {}
    void Activate(int id) override { activated = id; }
    int TabCount() const override { return tabbed ? 1 : 0; }
    std::string_view TabLabel(int) const override { return "tab"; }
    bool ShowSettingPage(std::string_view label) override {
        if (label != "new_first") return false;
        rows = {Row(99, "new_first"), Row(20, "focused", true)};
        return true;
    }
    void Insert() { rows.insert(rows.begin(), Row(1, "updater", true)); }
};

void Tick(PcSettingsPage& page) { for (int i = 0; i < 15; ++i) page.Step(true); }
bool Is(const PcSettingsPage& page, int id) { return page.CurrentRow() && page.CurrentRow()->id == id; }

struct Context {
    pt::Vfs vfs;
    pt::SceneRenderer scene;
    pt::TextureManager textures;
    pt::ModelCache models{vfs, scene, textures};
    Game game{vfs, models};
};
}

int main() {
    using namespace pt::game;
    auto context = std::make_unique<Context>();
    Game& game = context->game;
    {
        Source source;
        PcSettingsPage page;
        page.Open(&source);
        Check(page.Focus("focused"), "focus existing setting");
        source.Insert();
        Tick(page);
        Check(Is(page, 20), "async insertion before focused row preserves its ID");
        source.rows.erase(source.rows.begin());
        Tick(page);
        Check(Is(page, 20), "removal before focused row preserves its ID");
        source.rows = {Row(20, "renamed", true), Row(10, "first")};
        Tick(page);
        Check(Is(page, 20), "reordered and relabelled setting preserves its ID");
    }
    {
        Source source;
        source.tabbed = true;
        PcSettingsPage page;
        page.Open(&source);
        MenuInput up;
        up.held_dirs = pt::kRawUp;
        page.Update(game, up, 1.0f / 30.0f);
        Check(page.StripFocused(), "up from first row focuses tab strip");
        source.Insert();
        Tick(page);
        Check(page.StripFocused(), "async insertion preserves tab strip focus");
    }
    {
        Source source;
        PcSettingsPage page;
        page.Open(&source);
        page.Focus("focused");
        MenuInput down;
        down.held_dirs = pt::kRawDown;
        page.Update(game, down, 1.0f / 30.0f);
        Check(page.Cursor() == page.RowCount(), "down from last row focuses Back");
        source.Insert();
        Tick(page);
        Check(!page.CurrentRow() && page.Cursor() == page.RowCount(), "async insertion preserves Back focus");
    }
    {
        Source source;
        PcSettingsPage page;
        page.Open(&source);
        page.Focus("focused");
        MenuInput accept;
        accept.accept = true;
        page.Update(game, accept, 1.0f / 30.0f);
        Check(page.Confirming(), "action opens confirmation");
        source.Insert();
        Tick(page);
        Check(page.Confirming() && Is(page, 20) && page.DialogFocus() == 0, "refresh keeps confirmation attached to same action");
        source.rows = {Row(1, "updater", true), Row(10, "first"), Row(30, "replacement", true)};
        Tick(page);
        Check(!page.Confirming(), "removing confirmed action cancels rather than retargets dialog");
        Check(source.activated == -1, "refresh never activates an action");
    }
    {
        Source source;
        PcSettingsPage page;
        page.Open(&source);
        page.Focus("focused");
        Check(page.ShowRowPage("new_first") && Is(page, 99), "page change starts on its first row despite shared IDs");
        page.Focus("focused");
        page.Open(&source);
        Check(Is(page, 99), "reopening starts on first row despite previous focus");
    }
    {
        Source source;
        source.tabbed = true;
        source.footer = Row(80, "update", true);
        PcSettingsPage page;
        page.Open(&source);
        MenuInput up;
        up.held_dirs = pt::kRawUp;
        page.Update(game, up, 1.0f / 30.0f);
        page.Update(game, {}, 1.0f / 30.0f);
        page.Update(game, up, 1.0f / 30.0f);
        Check(!page.CurrentRow() && page.Cursor() == page.RowCount(), "wrap from strip reaches Back; the Update button is not a row");
        page.Update(game, {}, 1.0f / 30.0f);
        page.Update(game, up, 1.0f / 30.0f);
        Check(Is(page, 20), "Up from Back reaches the last row, not the Update button");
        MenuInput update;
        update.update = true;
        page.Update(game, update, 1.0f / 30.0f);
        Check(page.Confirming() && Is(page, 80), "U or Triangle opens the Update confirmation from any row");
        source.Insert();
        Tick(page);
        Check(page.Confirming() && Is(page, 80), "Update confirmation survives a refresh that inserts a row");
        source.footer.reset();
        Tick(page);
        Check(!page.Confirming() && !page.FindRow("update"), "withdrawn update cancels its confirmation");
        Check(source.activated == -1, "withdrawn footer never activates another action");
    }
    std::printf("%d failures\n", failed);
    return failed ? 1 : 0;
}
