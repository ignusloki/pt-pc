#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace pt::game {

struct MuseumLayout {
    struct Cell {
        glm::vec2 lo{0.0f};
        glm::vec2 hi{0.0f};
        bool Contains(glm::vec2 p) const { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; }
    };

    static constexpr int kHallColumns = 4;
    static constexpr float kHallWidth = 280.0f;
    static constexpr float kHallGap = 26.0f;
    static constexpr float kHallPicture = 140.0f;
    static constexpr int kHallColumnsWide = 5;
    static constexpr float kHallWidthWide = 216.0f;
    static constexpr float kHallGapWide = 20.0f;
    static constexpr float kHallPictureWide = 120.0f;
    static constexpr float kHallPlaque = 44.0f;
    static constexpr float kHallTop = 158.0f;
    static constexpr float kHallRowGap = 18.0f;
    static constexpr float kHallTextY = 564.0f;
    static constexpr float kHallTextHeight = 22.0f;

    static constexpr float kThumbWidth = 128.0f;
    static constexpr float kThumbHeight = 72.0f;
    static constexpr float kThumbGap = 12.0f;
    static constexpr float kStripTop = 503.0f;
    static constexpr float kHelpTop = 594.0f;
    static constexpr float kHelpBottom = 642.0f;
    static constexpr Cell kTranscript{{210.0f, 156.0f}, {1070.0f, 436.0f}};
    static constexpr float kCanvasWidth = 1280.0f;
    static constexpr float kCanvasHeight = 720.0f;
    static constexpr Cell kSpotlight{{391.0f, 156.0f}, {889.0f, 436.0f}};
    static constexpr float kPlaqueTitleY = 446.0f;
    static constexpr float kPlaqueCaptionY = 472.0f;

    static int HallColumns(int count) { return count > kHallColumns * 2 ? kHallColumnsWide : kHallColumns; }
    static float HallWidth(int count) { return count > kHallColumns * 2 ? kHallWidthWide : kHallWidth; }
    static float HallGap(int count) { return count > kHallColumns * 2 ? kHallGapWide : kHallGap; }
    static float HallPictureHeight(int count) { return count > kHallColumns * 2 ? kHallPictureWide : kHallPicture; }
    static Cell HallCell(int i, int count) {
        const int columns = HallColumns(count);
        const float width = HallWidth(count);
        const float gap = HallGap(count);
        const float picture = HallPictureHeight(count);
        const int column = i % columns;
        const int row = i / columns;
        const int in_row = std::clamp(count - row * columns, 1, columns);
        const float x = (kCanvasWidth - (in_row * width + (in_row - 1) * gap)) * 0.5f + column * (width + gap);
        const float y = kHallTop + row * (picture + kHallPlaque + kHallRowGap);
        return {{x, y}, {x + width, y + picture + kHallPlaque}};
    }
    static Cell HallPicture(int i, int count) {
        Cell cell = HallCell(i, count);
        cell.hi.y = cell.lo.y + HallPictureHeight(count);
        return cell;
    }
    static Cell HallText(int index) {
        const float y = kHallTextY + index * kHallTextHeight;
        return {{kCanvasWidth * 0.5f - 300.0f, y - kHallTextHeight * 0.5f}, {kCanvasWidth * 0.5f + 300.0f, y + kHallTextHeight * 0.5f}};
    }

    static int WallVisible(float width) {
        const float room = std::max(width, kCanvasWidth) - 180.0f;
        return std::clamp(static_cast<int>(std::floor((room + kThumbGap) / (kThumbWidth + kThumbGap))), 4, 16);
    }
    static Cell WallArrow(bool right, int visible) {
        const Cell end = WallCell(right ? visible - 1 : 0, visible);
        const float x = right ? end.hi.x + 16.0f : end.lo.x - 60.0f;
        return {{x, kStripTop}, {x + 44.0f, kStripTop + kThumbHeight}};
    }

    static constexpr Cell kMusicSheet{{300.0f, 156.0f}, {980.0f, 436.0f}};
    static constexpr glm::vec2 kMusicNote{640.0f, 222.0f};
    static constexpr float kMusicTimeY = 300.0f;
    static constexpr Cell kMusicBar{{360.0f, 326.0f}, {920.0f, 346.0f}};
    enum MusicButtonId { kMusicPrevious, kMusicBack, kMusicPlay, kMusicForward, kMusicNext, kMusicRestart, kMusicLoop, kMusicButtons };
    static Cell MusicButton(int i) {
        constexpr float kWidth = 52.0f;
        constexpr float kGap = 16.0f;
        const float x = kCanvasWidth * 0.5f - (kMusicButtons * kWidth + (kMusicButtons - 1) * kGap) * 0.5f + i * (kWidth + kGap);
        return {{x, 364.0f}, {x + kWidth, 404.0f}};
    }

    static Cell WallCell(int slot, int visible) {
        const float total = visible * kThumbWidth + (visible - 1) * kThumbGap;
        const float x = kCanvasWidth * 0.5f - total * 0.5f + slot * (kThumbWidth + kThumbGap);
        return {{x, kStripTop}, {x + kThumbWidth, kStripTop + kThumbHeight}};
    }
};

}
