#pragma once

#include <span>
#include <string_view>

namespace pt::game {

struct MuseumLine {
    float start = 0.0f;
    std::string_view subtitle;
    int subtitle_line = -1;
    std::string_view key;
    std::string_view original;
};

std::span<const MuseumLine> MuseumTranscript(std::string_view entry_id);

}
