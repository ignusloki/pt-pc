#include "engine/assets/gltf_model.h"
#include "game/ui/archive_view.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <initializer_list>

#include "engine/assets/fmdl.h"
#include "engine/assets/ftex.h"
#include "engine/core/log.h"
#include "engine/data/fox2.h"
#include "engine/fs/vfs.h"
#include "engine/render/texture_manager.h"
#include "engine/ui/text_layout.h"
#include "game/archive.h"
#include "game/stage_data.h"
#include "game/ui/museum_layout.h"
#include "game/ui/pc_settings_page.h"
#include "game/ui/prompt_hint.h"
#include "game/ui/ui_assets.h"
#include "game/ui/uif_view.h"

namespace pt::game {
namespace {

constexpr const char* kHallwayData = "/Assets/sh/level/promotion/pt_2014/hallway/pt14_hallway.fpkd";
constexpr std::string_view kNazoGroup = "pt14_hallway_nazo|shsb_labl001_mapc";

glm::vec2 Fit(glm::vec2 size, glm::vec2 lo, glm::vec2 hi, glm::vec2& origin) {
    const glm::vec2 room = hi - lo;
    const float scale = size.x > 0.0f && size.y > 0.0f ? std::min(room.x / size.x, room.y / size.y) : 1.0f;
    const glm::vec2 shown = size * scale;
    origin = lo + (room - shown) * 0.5f;
    return shown;
}

ui::TextStyle PanelStyle(const UiFont& font, float size) {
    ui::TextStyle style;
    style.font = &font.font;
    style.font_width = size;
    style.font_height = size;
    style.text_space = 0.0f;
    style.line_space = size * 0.25f;
    return style;
}

}

glm::vec2 ArchiveView::TextureSize(UiAssets& assets, const std::string& path) {
    if (auto it = sizes_.find(path); it != sizes_.end()) return it->second;
    glm::vec2 size(16.0f, 9.0f);
    if (auto header = ReadTextureFile(assets.Files().Textures(), FtexStem(path) + ".ftex"); header && header->size() >= 0x0E) {
        uint16_t w = 0;
        uint16_t h = 0;
        std::memcpy(&w, header->data() + 0x0A, 2);
        std::memcpy(&h, header->data() + 0x0C, 2);
        if (w > 0 && h > 0) size = glm::vec2(w, h);
    }
    sizes_[path] = size;
    return size;
}

bool ArchiveView::LoadPiece(UiAssets& assets, const std::string& model, const glm::mat4& to_plane, Piece& out) {
    FmdlModel fmdl;
    if (!LoadModelFile(assets.Files(), model, fmdl) || fmdl.mesh.vertices.empty()) {
        LogWarn("archive: photo piece {} unreadable", model);
        return false;
    }
    const std::string base = fmdl.materials.empty() ? std::string() : fmdl.materials.front().base_color;
    out.texture = base.empty() ? TextureManager::kWhite : assets.Texture(base + ".ftex");
    for (const Vertex& v : fmdl.mesh.vertices) {
        const glm::vec4 p = to_plane * glm::vec4(v.position, 1.0f);
        out.positions.emplace_back(p.x, p.y);
        out.uvs.push_back(v.uv0);
    }
    for (const SubMesh& sub : fmdl.mesh.submeshes) {
        for (uint32_t i = 0; i < sub.index_count; ++i) {
            out.indices.push_back(fmdl.mesh.indices[sub.first_index + i] + static_cast<uint32_t>(sub.vertex_offset));
        }
    }
    if (fmdl.mesh.submeshes.empty()) out.indices = fmdl.mesh.indices;
    return true;
}

const ArchiveView::Photo& ArchiveView::LoadPhoto(UiAssets& assets, const std::string& name) {
    Photo& photo = photos_[name];
    if (photo.loaded) return photo;
    photo.loaded = true;
    if (name != "complete") {
        Piece piece;
        if (LoadPiece(assets, name, glm::mat4(1.0f), piece)) photo.pieces.push_back(std::move(piece));
    } else if (auto package = assets.Files().LoadPackage(kHallwayData)) {
        std::vector<std::pair<std::string, glm::mat4>> placed;
        for (const auto& entry : package->Entries()) {
            if (!entry.path.ends_with(".fox2")) continue;
            auto file = std::make_shared<fox2::DataSetFile>();
            const auto data = package->Read(entry);
            if (!file->Load(entry.path, data)) continue;
            const auto stage = BuildStageData(file, kHallwayData);
            for (const StaticModelPlacement& m : stage->static_models) {
                const size_t at = m.name.find(kNazoGroup);
                if (at == std::string::npos) continue;
                if (m.name.ends_with("_frame") || m.name.ends_with("mapc001_0000") || m.name.ends_with("mapc002_0000")) {
                    placed.emplace_back(m.model_file, m.world);
                    LogInfo("archive: frame piece {} ({})", m.name, m.model_file);
                }
            }
        }
        if (!placed.empty()) {
            const glm::mat4 to_plane = glm::inverse(placed.front().second);
            for (const auto& [model, world] : placed) {
                Piece piece;
                if (LoadPiece(assets, model, to_plane * world, piece)) photo.pieces.push_back(std::move(piece));
            }
        }
        LogInfo("archive: the frame's photo, {} pieces", photo.pieces.size());
    }
    bool first = true;
    for (const Piece& piece : photo.pieces) {
        for (const glm::vec2& p : piece.positions) {
            photo.lo = first ? p : glm::min(photo.lo, p);
            photo.hi = first ? p : glm::max(photo.hi, p);
            first = false;
        }
    }
    return photo;
}

bool ArchiveView::DrawPicture(ui::UiBatch& batch, UiAssets& assets, const PcPanel& panel, glm::vec2 lo, glm::vec2 hi, float gain) {
    if (!panel.photo.empty()) {
        const Photo& photo = LoadPhoto(assets, panel.photo);
        if (photo.pieces.empty()) return false;
        glm::vec2 origin;
        const glm::vec2 extent = photo.hi - photo.lo;
        const glm::vec2 shown = Fit(extent, lo, hi, origin);
        const float scale = extent.x > 0.0f ? shown.x / extent.x : 1.0f;
        for (const Piece& piece : photo.pieces) {
            std::vector<ui::UiVertex> vertices;
            for (const uint32_t i : piece.indices) {
                if (i >= piece.positions.size()) continue;
                const glm::vec2 p = piece.positions[i];
                vertices.push_back({origin + glm::vec2(p.x - photo.lo.x, photo.hi.y - p.y) * scale, piece.uvs[i], glm::vec4(1.0f)});
            }
            batch.Draw(vertices, ui::UiDrawParams::Plain(piece.texture), ui::UiShade::Material, ui::UiBlend::Alpha);
        }
        return true;
    }
    uint32_t texture = 0;
    glm::vec2 size(16.0f, 9.0f);
    if (!panel.texture.empty()) {
        texture = assets.Texture(panel.texture);
        size = panel.aspect > 0.0f ? glm::vec2(panel.aspect, 1.0f) : TextureSize(assets, panel.texture);
    } else if (!panel.file.empty()) {
        texture = assets.LocalPreview(panel.file);
    } else if (!panel.thumbnail.empty()) {
        texture = assets.LocalPreview(panel.thumbnail);
    }
    if (!texture) return false;
    glm::vec2 origin;
    const glm::vec2 shown = Fit(size, lo, hi, origin);
    if (panel.string) {
        ui::UiDrawParams params = ui::UiDrawParams::Plain(texture);
        params.extra = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        batch.Quad(origin, origin + shown, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec4(10.0f, 10.0f, 10.0f, 1.0f), params, ui::UiShade::String,
                   ui::UiBlend::Alpha);
    } else {
        const float lift = panel.thumbnail.empty() ? 1.0f : gain;
        batch.Quad(origin, origin + shown, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec4(lift, lift, lift, 1.0f), ui::UiDrawParams::Plain(texture),
                   ui::UiShade::Material, ui::UiBlend::Alpha);
    }
    return true;
}

bool ArchiveView::FitPicture(UiAssets& assets, const PcPanel& panel, glm::vec2 lo, glm::vec2 hi, glm::vec2& out_lo, glm::vec2& out_hi) {
    glm::vec2 size(16.0f, 9.0f);
    if (!panel.photo.empty()) {
        const Photo& photo = LoadPhoto(assets, panel.photo);
        if (photo.pieces.empty()) return false;
        size = photo.hi - photo.lo;
    } else if (!panel.texture.empty()) {
        size = panel.aspect > 0.0f ? glm::vec2(panel.aspect, 1.0f) : TextureSize(assets, panel.texture);
    } else if (panel.file.empty() && panel.thumbnail.empty()) {
        return false;
    }
    glm::vec2 origin;
    const glm::vec2 shown = Fit(size, lo, hi, origin);
    out_lo = origin;
    out_hi = origin + shown;
    return true;
}

void ArchiveView::DrawLine(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, std::string_view text, float size, glm::vec2 lo,
                           glm::vec2 hi, ui::TextAlign align, float alpha, int language) {
    UiFont* font = assets.Font(UiFontType::PcSystem, language);
    if (!font || text.empty()) return;
    ui::TextLayout layout = ui::LayoutText(text, PanelStyle(*font, size), hi.x - lo.x);
    const int fit = std::max(1, static_cast<int>((hi.y - lo.y) / std::max(layout.line_pitch, 1.0f)));
    if (static_cast<int>(layout.lines.size()) > fit) layout.lines.resize(static_cast<size_t>(fit));
    ui::PlaceText(layout, lo, hi, align, align, ui::TextAlign::Center, true);
    DrawText(batch, canvas, *font, layout, glm::vec4(1.0f, 1.0f, 1.0f, alpha), ui::UiBlend::Alpha);
}

void ArchiveView::DrawFrame(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, const PcPanel& panel, std::string_view name, glm::vec2 lo,
                            glm::vec2 hi, float inset, bool selected, bool locked, int language) {
    const auto solid = [&](glm::vec2 a, glm::vec2 b, glm::vec4 color) {
        batch.Quad(a, b, glm::vec2(0.0f), glm::vec2(1.0f), color, ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid, ui::UiBlend::Alpha);
    };
    glm::vec2 a = canvas.ToTarget(lo + glm::vec2(inset));
    glm::vec2 b = canvas.ToTarget(hi - glm::vec2(inset));
    glm::vec2 pa = a;
    glm::vec2 pb = b;
    const bool picture = !locked && !panel.audio && FitPicture(assets, panel, a, b, pa, pb);
    if (picture) {
        a = pa - glm::vec2(inset * canvas.scale);
        b = pb + glm::vec2(inset * canvas.scale);
    }
    const float line = std::max(1.0f, std::round((selected ? 2.0f : 1.0f) * canvas.scale));
    const float edge = selected ? 0.95f : locked ? 0.16f : 0.38f;
    solid(a, b, glm::vec4(0.0f, 0.0f, 0.0f, picture ? 0.9f : 0.55f));
    solid({a.x - line, a.y - line}, {b.x + line, a.y}, glm::vec4(1.0f, 1.0f, 1.0f, edge));
    solid({a.x - line, b.y}, {b.x + line, b.y + line}, glm::vec4(1.0f, 1.0f, 1.0f, edge));
    solid({a.x - line, a.y}, {a.x, b.y}, glm::vec4(1.0f, 1.0f, 1.0f, edge));
    solid({b.x, a.y}, {b.x + line, b.y}, glm::vec4(1.0f, 1.0f, 1.0f, edge));
    if (panel.music && !locked) {
        const glm::vec2 center((lo.x + hi.x) * 0.5f, lo.y + (hi.y - lo.y) * 0.2f);
        DrawNote(batch, canvas, center, 10.0f, glm::vec4(1.0f, 1.0f, 1.0f, selected ? 0.95f : 0.6f));
        DrawLine(batch, canvas, assets, name, 13.0f, {lo.x + 5.0f, lo.y + (hi.y - lo.y) * 0.46f},
                 {hi.x - 5.0f, hi.y - 3.0f}, ui::TextAlign::Center, selected ? 0.95f : 0.6f, language);
        return;
    }
    if (panel.audio && !locked) {
        const glm::vec2 center((lo.x + hi.x) * 0.5f, lo.y + (hi.y - lo.y) * 0.38f);
        const glm::vec4 color(1.0f, 1.0f, 1.0f, selected ? 0.95f : 0.6f);
        const auto rectangle = [&](glm::vec2 lo, glm::vec2 hi) { solid(canvas.ToTarget(lo), canvas.ToTarget(hi), color); };
        rectangle(center + glm::vec2(-18.0f, -5.0f), center + glm::vec2(-10.0f, 5.0f));
        const glm::vec2 cone[] = {center + glm::vec2(-10.0f, -5.0f), center + glm::vec2(0.0f, -13.0f),
                                 center + glm::vec2(0.0f, 13.0f), center + glm::vec2(-10.0f, 5.0f)};
        const ui::UiVertex vertices[] = {{canvas.ToTarget(cone[0]), {}, color}, {canvas.ToTarget(cone[1]), {}, color},
            {canvas.ToTarget(cone[2]), {}, color}, {canvas.ToTarget(cone[0]), {}, color},
            {canvas.ToTarget(cone[2]), {}, color}, {canvas.ToTarget(cone[3]), {}, color}};
        batch.Draw(vertices, ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid, ui::UiBlend::Alpha);
        for (const float radius : {10.0f, 17.0f}) {
            for (int i = 0; i < 9; ++i) {
                const float angle = -0.8f + static_cast<float>(i) * 0.2f;
                const glm::vec2 p = center + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
                rectangle(p - glm::vec2(1.2f), p + glm::vec2(1.2f));
            }
        }
        DrawLine(batch, canvas, assets, name, 13.0f, {lo.x + 5.0f, lo.y + (hi.y - lo.y) * 0.66f},
                 {hi.x - 5.0f, hi.y - 4.0f}, ui::TextAlign::Center, selected ? 0.95f : 0.6f, language);
        return;
    }
    const bool drawn = picture && DrawPicture(batch, assets, panel, pa, pb, panel.lift);
    if (drawn) {
        if (!selected) solid(pa, pb, glm::vec4(0.0f, 0.0f, 0.0f, 0.22f));
    } else if (locked && name == kArchiveLockedLabel) {
        DrawLine(batch, canvas, assets, name, 15.0f, lo + glm::vec2(inset + 8.0f), hi - glm::vec2(inset + 8.0f), ui::TextAlign::Center,
                 selected ? 0.7f : 0.3f, language);
    } else if (!locked && !picture && !name.empty()) {
        DrawLine(batch, canvas, assets, name, 15.0f, lo + glm::vec2(inset + 8.0f), hi - glm::vec2(inset + 8.0f), ui::TextAlign::Center,
                 selected ? 0.8f : 0.45f, language);
    }
}

void ArchiveView::DrawMuseum(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, const PcSettingsPage& page, int language) {
    const int n = page.RowCount();
    const int cells = page.CellCount();
    const int cursor = page.Cursor();
    using L = MuseumLayout;
    if (page.Gallery() == PcGallery::Halls) {
        for (int i = 0; i < n; ++i) {
            const PcSettingRow* row = page.RowAtIndex(i);
            if (!row) continue;
            const bool selected = i == cursor;
            if (i < cells) {
                const L::Cell picture = L::HallPicture(i, cells);
                const PcPanel panel = page.PanelOf(i);
                DrawFrame(batch, canvas, assets, panel, PcText(row->label, language), picture.lo, picture.hi, 4.0f, selected, !row->enabled || panel.locked,
                          language);
                const L::Cell cell = L::HallCell(i, cells);
                const float alpha = row->enabled ? (selected ? 1.0f : 0.7f) : 0.35f;
                const float name_size = L::HallColumns(cells) > L::kHallColumns ? 15.0f : 17.0f;
                DrawLine(batch, canvas, assets, PcText(row->label, language), name_size, {cell.lo.x + 2.0f, picture.hi.y + 4.0f},
                         {cell.hi.x - 2.0f, picture.hi.y + 26.0f}, ui::TextAlign::Center, alpha, language);
                if (!row->values.empty()) {
                    DrawLine(batch, canvas, assets, row->values.front(), 13.0f, {cell.lo.x + 4.0f, picture.hi.y + 26.0f},
                             {cell.hi.x - 4.0f, picture.hi.y + 42.0f}, ui::TextAlign::Center, alpha * 0.55f, language);
                }
            } else {
                const L::Cell cell = L::HallText(i - cells);
                DrawLine(batch, canvas, assets, PcText(row->label, language), 17.0f, cell.lo, cell.hi, ui::TextAlign::Center,
                         row->enabled ? (selected ? 1.0f : 0.55f) : 0.3f, language);
            }
        }
        return;
    }
    const int slots = std::min(n, page.WallVisible());
    const int visible = std::min(n - page.First(), slots);
    for (int slot = 0; slot < visible; ++slot) {
        const int index = page.First() + slot;
        const PcSettingRow* row = page.RowAtIndex(index);
        if (!row) continue;
        L::Cell cell = L::WallCell(slot, slots);
        const bool selected = index == cursor;
        if (selected) {
            cell.lo -= glm::vec2(3.0f);
            cell.hi += glm::vec2(3.0f);
        }
        const PcPanel panel = page.PanelOf(index);
        DrawFrame(batch, canvas, assets, panel, row->label, cell.lo, cell.hi, 3.0f, selected, !row->enabled || panel.locked, language);
    }
    for (const bool right : {false, true}) {
        const L::Cell arrow = L::WallArrow(right, slots);
        const bool enabled = right ? cursor < n - 1 : cursor > 0;
        DrawLine(batch, canvas, assets, right ? ">" : "<", 30.0f, arrow.lo, arrow.hi, ui::TextAlign::Center,
                 enabled ? 0.95f : 0.22f, language);
    }
    const int shown = std::min(cursor, n - 1);
    const PcSettingRow* current = page.RowAtIndex(shown);
    if (!current) return;
    const PcPanel panel = page.PanelOf(shown);
    const bool locked = !current->enabled || panel.locked;
    if (panel.music && !locked) {
        DrawMusicPlayer(batch, canvas, assets, panel, language);
    } else if (!panel.lines.empty() && !locked) {
        const glm::vec2 a = canvas.ToTarget(L::kTranscript.lo);
        const glm::vec2 b = canvas.ToTarget(L::kTranscript.hi);
        PcPanel sheet;
        DrawFrame(batch, canvas, assets, sheet, {}, L::kTranscript.lo, L::kTranscript.hi, 0.0f, true, false, language);
        batch.Quad(a, b, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.5f), ui::UiDrawParams::Plain(TextureManager::kWhite),
                   ui::UiShade::Solid, ui::UiBlend::Alpha);
        DrawLines(batch, canvas, assets, panel, L::kTranscript.lo + glm::vec2(22.0f, 18.0f), L::kTranscript.hi - glm::vec2(22.0f, 14.0f), language, true);
    } else {
        DrawFrame(batch, canvas, assets, panel, current->label, L::kSpotlight.lo, L::kSpotlight.hi, 8.0f, true, locked, language);
    }
    DrawLine(batch, canvas, assets, current->label, 21.0f, {200.0f, L::kPlaqueTitleY}, {1080.0f, L::kPlaqueTitleY + 26.0f}, ui::TextAlign::Center,
             locked ? 0.5f : 0.95f, language);
    if (!locked && !panel.caption.empty()) {
        DrawLine(batch, canvas, assets, panel.caption, 14.0f, {200.0f, L::kPlaqueCaptionY}, {1080.0f, L::kPlaqueCaptionY + 20.0f}, ui::TextAlign::Center,
                 0.42f, language);
    }
}

namespace {

void Solid(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 lo, glm::vec2 hi, glm::vec4 color) {
    batch.Quad(canvas.ToTarget(lo), canvas.ToTarget(hi), glm::vec2(0.0f), glm::vec2(1.0f), color, ui::UiDrawParams::Plain(TextureManager::kWhite),
               ui::UiShade::Solid, ui::UiBlend::Alpha);
}

void Triangles(ui::UiBatch& batch, const UiCanvas& canvas, std::initializer_list<glm::vec2> points, glm::vec4 color) {
    std::vector<ui::UiVertex> vertices;
    for (const glm::vec2& p : points) vertices.push_back({canvas.ToTarget(p), {}, color});
    batch.Draw(vertices, ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid, ui::UiBlend::Alpha);
}

void Arc(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 center, float radius, float a0, float a1, bool head, glm::vec4 color) {
    constexpr int kSteps = 14;
    for (int i = 0; i <= kSteps; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / kSteps;
        const glm::vec2 p = center + glm::vec2(std::cos(a), std::sin(a)) * radius;
        Solid(batch, canvas, p - glm::vec2(1.3f), p + glm::vec2(1.3f), color);
    }
    if (head) {
        const glm::vec2 out(std::cos(a1), std::sin(a1));
        const glm::vec2 tip = center + out * radius;
        const glm::vec2 along = glm::vec2(-out.y, out.x) * (a1 > a0 ? 1.0f : -1.0f);
        Triangles(batch, canvas, {tip + along * 5.0f, tip - out * 5.0f, tip + out * 5.0f}, color);
    }
}

std::string Clock(float seconds) {
    const int s = std::max(0, static_cast<int>(std::floor(seconds)));
    return std::format("{}:{:02}", s / 60, s % 60);
}

}

void ArchiveView::DrawNote(ui::UiBatch& batch, const UiCanvas& canvas, glm::vec2 center, float size, glm::vec4 color) {
    const glm::vec2 head = center + glm::vec2(-size * 0.3f, size * 0.9f);
    constexpr int kSides = 18;
    constexpr float kTilt = -0.35f;
    const auto at = [&](int i) {
        const float a = 6.2831853f * static_cast<float>(i) / kSides;
        const glm::vec2 e(std::cos(a) * size * 0.62f, std::sin(a) * size * 0.42f);
        return head + glm::vec2(e.x * std::cos(kTilt) - e.y * std::sin(kTilt), e.x * std::sin(kTilt) + e.y * std::cos(kTilt));
    };
    std::vector<ui::UiVertex> vertices;
    for (int i = 0; i < kSides; ++i) {
        for (const glm::vec2& p : {head, at(i), at(i + 1)}) vertices.push_back({canvas.ToTarget(p), {}, color});
    }
    batch.Draw(vertices, ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid, ui::UiBlend::Alpha);
    const float x = head.x + size * 0.52f;
    const float top = head.y - size * 2.4f;
    Solid(batch, canvas, {x - size * 0.08f, top}, {x + size * 0.08f, head.y - size * 0.1f}, color);
    Triangles(batch, canvas, {{x, top}, {x + size * 0.75f, top + size * 0.95f}, {x, top + size * 0.55f}}, color);
}

void ArchiveView::DrawMusicPlayer(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, const PcPanel& panel, int language) {
    using L = MuseumLayout;
    PcPanel sheet;
    DrawFrame(batch, canvas, assets, sheet, {}, L::kMusicSheet.lo, L::kMusicSheet.hi, 0.0f, true, false, language);
    Solid(batch, canvas, L::kMusicSheet.lo, L::kMusicSheet.hi, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
    const glm::vec4 ink(1.0f, 1.0f, 1.0f, 0.92f);
    const glm::vec4 dim(1.0f, 1.0f, 1.0f, 0.35f);
    DrawNote(batch, canvas, L::kMusicNote, 22.0f, panel.music_playing ? ink : glm::vec4(1.0f, 1.0f, 1.0f, 0.6f));
    const std::string time = panel.music_length > 0.0f ? std::format("{} / {}", Clock(panel.music_position), Clock(panel.music_length))
                                                       : Clock(panel.music_position);
    DrawLine(batch, canvas, assets, time, 17.0f, {L::kMusicBar.lo.x, L::kMusicTimeY - 12.0f}, {L::kMusicBar.hi.x, L::kMusicTimeY + 12.0f},
             ui::TextAlign::Center, 0.85f, language);
    const L::Cell bar = L::kMusicBar;
    const float mid = (bar.lo.y + bar.hi.y) * 0.5f;
    const float fraction = panel.music_length > 0.0f ? std::clamp(panel.music_position / panel.music_length, 0.0f, 1.0f) : 0.0f;
    const float x = bar.lo.x + (bar.hi.x - bar.lo.x) * fraction;
    Solid(batch, canvas, {bar.lo.x, mid - 2.0f}, {bar.hi.x, mid + 2.0f}, glm::vec4(1.0f, 1.0f, 1.0f, 0.22f));
    Solid(batch, canvas, {bar.lo.x, mid - 2.0f}, {x, mid + 2.0f}, ink);
    Solid(batch, canvas, {x - 4.0f, mid - 7.0f}, {x + 4.0f, mid + 7.0f}, ink);
    for (int b = 0; b < L::kMusicButtons; ++b) {
        const L::Cell cell = L::MusicButton(b);
        const glm::vec2 c = (cell.lo + cell.hi) * 0.5f;
        const glm::vec4 color = b == L::kMusicLoop && !panel.music_loop ? dim : ink;
        const bool strong = b == L::kMusicPlay || (b == L::kMusicLoop && panel.music_loop);
        const glm::vec4 edge(1.0f, 1.0f, 1.0f, strong ? 0.7f : 0.25f);
        Solid(batch, canvas, cell.lo, {cell.hi.x, cell.lo.y + 1.0f}, edge);
        Solid(batch, canvas, {cell.lo.x, cell.hi.y - 1.0f}, cell.hi, edge);
        Solid(batch, canvas, cell.lo, {cell.lo.x + 1.0f, cell.hi.y}, edge);
        Solid(batch, canvas, {cell.hi.x - 1.0f, cell.lo.y}, cell.hi, edge);
        const auto right = [&](glm::vec2 at, float h) {
            Triangles(batch, canvas, {at + glm::vec2(-h * 0.8f, -h), at + glm::vec2(h * 0.8f, 0.0f), at + glm::vec2(-h * 0.8f, h)}, color);
        };
        const auto left = [&](glm::vec2 at, float h) {
            Triangles(batch, canvas, {at + glm::vec2(h * 0.8f, -h), at + glm::vec2(-h * 0.8f, 0.0f), at + glm::vec2(h * 0.8f, h)}, color);
        };
        switch (b) {
        case L::kMusicPrevious:
            Solid(batch, canvas, c + glm::vec2(-9.0f, -8.0f), c + glm::vec2(-6.0f, 8.0f), color);
            left(c + glm::vec2(2.0f, 0.0f), 8.0f);
            break;
        case L::kMusicNext:
            right(c + glm::vec2(-2.0f, 0.0f), 8.0f);
            Solid(batch, canvas, c + glm::vec2(6.0f, -8.0f), c + glm::vec2(9.0f, 8.0f), color);
            break;
        case L::kMusicBack:
            left(c + glm::vec2(-5.0f, 0.0f), 7.0f);
            left(c + glm::vec2(6.0f, 0.0f), 7.0f);
            break;
        case L::kMusicForward:
            right(c + glm::vec2(-6.0f, 0.0f), 7.0f);
            right(c + glm::vec2(5.0f, 0.0f), 7.0f);
            break;
        case L::kMusicPlay:
            if (panel.music_playing) {
                Solid(batch, canvas, c + glm::vec2(-7.0f, -9.0f), c + glm::vec2(-2.0f, 9.0f), color);
                Solid(batch, canvas, c + glm::vec2(2.0f, -9.0f), c + glm::vec2(7.0f, 9.0f), color);
            } else {
                right(c + glm::vec2(1.0f, 0.0f), 10.0f);
            }
            break;
        case L::kMusicRestart:
            Arc(batch, canvas, c, 9.0f, 0.6f, 5.6f, true, color);
            break;
        case L::kMusicLoop:
            Arc(batch, canvas, c, 9.0f, 3.5f, 6.0f, true, color);
            Arc(batch, canvas, c, 9.0f, 0.35f, 2.85f, true, color);
            break;
        default:
            break;
        }
    }
}

void ArchiveView::DrawLines(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, const PcPanel& panel, glm::vec2 lo, glm::vec2 hi,
                            int language, bool complete) {
    UiFont* font = assets.Font(UiFontType::PcSystem, language);
    if (!font || panel.lines.empty()) return;
    if (complete) {
        std::string text;
        for (const std::string& line : panel.lines) {
            if (!text.empty()) text += "\n";
            text += line;
        }
        const ui::TextLayout layout = ui::LayoutTextInBox(text, PanelStyle(*font, 19.0f), lo, hi, true);
        DrawText(batch, canvas, *font, layout, glm::vec4(1.0f), ui::UiBlend::Alpha);
        return;
    }
    const ui::TextStyle style = PanelStyle(*font, 19.0f);
    const float width = hi.x - lo.x;
    std::vector<ui::TextLayout> layouts;
    std::vector<float> heights;
    for (const std::string& line : panel.lines) {
        layouts.push_back(ui::LayoutText(line, style, width));
        heights.push_back(std::max(1, static_cast<int>(layouts.back().lines.size())) * layouts.back().line_pitch + 8.0f);
    }
    size_t first = 0;
    const int current = std::clamp(panel.current_line, 0, static_cast<int>(panel.lines.size()) - 1);
    float above = 0.0f;
    for (int i = current; i >= 0; --i) {
        above += heights[i];
        if (above > (hi.y - lo.y) * 0.6f) {
            first = static_cast<size_t>(i + 1);
            break;
        }
    }
    first = std::min(first, static_cast<size_t>(current));
    float y = lo.y;
    for (size_t i = first; i < layouts.size() && y + heights[i] - 8.0f <= hi.y; ++i) {
        ui::PlaceText(layouts[i], {lo.x, y}, {hi.x, y + heights[i]}, ui::TextAlign::Start, ui::TextAlign::Start, ui::TextAlign::Start, true);
        const bool spoken = panel.current_line < 0 || static_cast<int>(i) == panel.current_line;
        DrawText(batch, canvas, *font, layouts[i], glm::vec4(1.0f, 1.0f, 1.0f, spoken ? 1.0f : 0.45f), ui::UiBlend::Alpha);
        y += heights[i];
    }
}

void ArchiveView::DrawPanel(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, const PcPanel& panel, glm::vec2 lo, glm::vec2 hi,
                            int language) {
    if (panel.Empty()) return;
    const glm::vec2 a = canvas.ToTarget(lo);
    const glm::vec2 b = canvas.ToTarget(hi);
    if (!panel.texture.empty() || !panel.photo.empty()) {
        const glm::vec4 ground = panel.string ? glm::vec4(0.0f, 0.0f, 0.0f, 0.85f) : glm::vec4(0.16f, 0.16f, 0.16f, 0.9f);
        batch.Quad(a, b, glm::vec2(0.0f), glm::vec2(1.0f), ground, ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid,
                   ui::UiBlend::Alpha);
        const glm::vec2 inset = (b - a) * 0.03f;
        DrawPicture(batch, assets, panel, a + inset, b - inset);
    } else if (!panel.file.empty()) {
        DrawPicture(batch, assets, panel, a, b);
    }
    if (!panel.lines.empty()) {
        DrawLines(batch, canvas, assets, panel, lo, hi, language);
    }
}

void ArchiveView::DrawFullScreen(ui::UiBatch& batch, const UiCanvas& canvas, UiAssets& assets, const PcPanel& panel, std::string_view hint,
                                 const PromptStyle& prompts, int language) {
    const glm::vec2 full(static_cast<float>(batch.Extent().width), static_cast<float>(batch.Extent().height));
    const glm::vec4 ground = panel.string ? glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) : glm::vec4(0.08f, 0.08f, 0.08f, 1.0f);
    batch.Quad(glm::vec2(0.0f), full, glm::vec2(0.0f), glm::vec2(1.0f), ground,
               ui::UiDrawParams::Plain(TextureManager::kWhite), ui::UiShade::Solid, ui::UiBlend::Alpha);
    const glm::vec2 lo = canvas.ToTarget({40.0f, 60.0f});
    const glm::vec2 hi = canvas.ToTarget({UiCanvas::kWidth - 40.0f, UiCanvas::kHeight - 70.0f});
    DrawPicture(batch, assets, panel, panel.string ? canvas.ToTarget({0.0f, 0.0f}) : lo,
                panel.string ? canvas.ToTarget({UiCanvas::kWidth, UiCanvas::kHeight}) : hi);
    UiFont* font = assets.Font(UiFontType::PcSystem, language);
    if (!font) return;
    if (!panel.title.empty()) {
        ui::TextLayout layout = ui::LayoutText(panel.title, PanelStyle(*font, 24.0f), 0.0f);
        ui::PlaceText(layout, {40.0f, 18.0f}, {UiCanvas::kWidth - 40.0f, 50.0f}, ui::TextAlign::Start, ui::TextAlign::Start, ui::TextAlign::Center, true);
        DrawText(batch, canvas, *font, layout, glm::vec4(1.0f, 1.0f, 1.0f, 0.85f), ui::UiBlend::Alpha);
    }
    if (!hint.empty()) {
        DrawPromptText(batch, canvas, assets, *font, hint, prompts, 18.0f, {40.0f, UiCanvas::kHeight - 50.0f}, {UiCanvas::kWidth - 40.0f, UiCanvas::kHeight - 16.0f},
                       ui::TextAlign::Center, ui::TextAlign::Center, glm::vec4(1.0f, 1.0f, 1.0f, 0.6f), false, 18.0f * 0.25f);
    }
}

}
