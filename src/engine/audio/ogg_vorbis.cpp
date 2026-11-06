#include "engine/audio/ogg_vorbis.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

namespace pt::audio {
namespace {
size_t ReadMemory(void* out, size_t size, size_t count, void* source) {
    auto& bytes = *static_cast<std::span<const uint8_t>*>(source);
    const size_t items = size ? std::min(count, bytes.size() / size) : 0;
    if (items) std::memcpy(out, bytes.data(), items * size);
    bytes = bytes.subspan(items * size);
    return items;
}
struct File {
    OggVorbis_File value{};
    ~File() { ov_clear(&value); }
};
}

bool DecodeOggVorbis(std::span<const uint8_t> bytes, std::vector<int16_t>& samples,
                     uint32_t& channels, uint32_t& rate, float max_seconds) {
    samples.clear();
    channels = rate = 0;
    if (bytes.empty() || bytes.size() > 0x7FFFFFFFu) return false;
    File file;
    if (ov_open_callbacks(&bytes, &file.value, nullptr, 0, {ReadMemory, nullptr, nullptr, nullptr}) != 0) return false;
    const vorbis_info* info = ov_info(&file.value, -1);
    if (!info || info->channels <= 0 || info->channels > 16 || info->rate <= 0 ||
        static_cast<unsigned long>(info->rate) > std::numeric_limits<uint32_t>::max()) return false;
    const size_t count = static_cast<size_t>(info->channels);
    const uint32_t sample_rate = static_cast<uint32_t>(info->rate);
    size_t limit = samples.max_size() / count;
    if (max_seconds > 0.0f && std::isfinite(max_seconds)) {
        const double frames = std::ceil(max_seconds * static_cast<float>(info->rate));
        if (frames < static_cast<double>(limit)) limit = static_cast<size_t>(frames);
    }
    std::array<int16_t, 4096> buffer;
    while (samples.size() / count < limit) {
        const size_t frames = std::min(buffer.size() / count, limit - samples.size() / count);
        int stream = 0;
        const long got = ov_read(&file.value, reinterpret_cast<char*>(buffer.data()),
                                 static_cast<int>(frames * count * sizeof(int16_t)),
                                 std::endian::native == std::endian::big, 2, 1, &stream);
        if (got <= 0 || stream != 0) break;
        samples.insert(samples.end(), buffer.begin(), buffer.begin() + got / sizeof(int16_t));
    }
    if (samples.empty()) return false;
    channels = static_cast<uint32_t>(count);
    rate = sample_rate;
    return true;
}
}
