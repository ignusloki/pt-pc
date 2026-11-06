#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace pt::audio {
// Decode the first Ogg/Vorbis stream. A positive duration returns only its prefix.
bool DecodeOggVorbis(std::span<const uint8_t> bytes, std::vector<int16_t>& samples,
                     uint32_t& channels, uint32_t& rate, float max_seconds = 0.0f);
}
