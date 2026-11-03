#include "engine/render/bc_decode.h"
#include <cassert>
#include <array>

int main() {
    std::vector<std::vector<uint8_t>> storage;
    std::vector<pt::TextureMip> output;
    std::array<uint8_t, 16> block{};
    // BC2 endpoints black < white, selector 3 must interpolate, not become transparent black.
    for (int i = 0; i < 8; ++i) block[i] = 0xff;
    block[10] = block[11] = 0xff;
    for (int i = 12; i < 16; ++i) block[i] = 0xff;
    pt::TextureMip mip{3, 2, block};
    VkFormat format = VK_FORMAT_BC2_SRGB_BLOCK;
    assert(pt::DecodeBcLevels(format, {&mip, 1}, storage, output));
    assert(format == VK_FORMAT_R8G8B8A8_SRGB && storage[0].size() == 24);
    for (int i = 0; i < 6; ++i) {
        assert(storage[0][i * 4] == 170 && storage[0][i * 4 + 3] == 255);
    }
    // Truncated compressed input must fail before reading beyond the block.
    mip.data = std::span<const uint8_t>(block).first(7);
    format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    assert(!pt::DecodeBcLevels(format, {&mip, 1}, storage, output));
    // BC5 keeps two independently decoded normal channels, including a 1x1 tail mip.
    block = {}; block[0] = 77; block[8] = 133;
    mip = {1, 1, block}; format = VK_FORMAT_BC5_UNORM_BLOCK;
    assert(pt::DecodeBcLevels(format, {&mip, 1}, storage, output));
    assert(format == VK_FORMAT_R8G8_UNORM && storage[0].size() == 2);
    assert(storage[0][0] == 77 && storage[0][1] == 133);
    // RGB BC1 must keep opaque alpha even in the punch-through selector mode.
    block = {}; block[2] = block[3] = 0xff;
    for (int i = 4; i < 8; ++i) block[i] = 0xff;
    mip = {1, 1, std::span<const uint8_t>(block).first(8)};
    format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    assert(pt::DecodeBcLevels(format, {&mip, 1}, storage, output));
    assert(storage[0][3] == 255);
    // SceneRenderer's film-grain resource is BC1 RGBA, with a complete mip chain.
    // Its two-color mode must preserve punch-through alpha, unlike BC1 RGB above.
    std::array<uint8_t, 8> rgba_block{0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    std::array<pt::TextureMip, 3> chain{{{4, 4, rgba_block}, {2, 2, rgba_block}, {1, 1, rgba_block}}};
    format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    assert(pt::DecodeBcLevels(format, chain, storage, output));
    assert(format == VK_FORMAT_R8G8B8A8_UNORM && output.size() == 3);
    assert(storage[0].size() == 64 && storage[1].size() == 16 && storage[2].size() == 4);
    for (const auto& level : storage)
        for (size_t at = 3; at < level.size(); at += 4) assert(level[at] == 0);
    format = VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    assert(pt::DecodeBcLevels(format, chain, storage, output));
    assert(format == VK_FORMAT_R8G8B8A8_SRGB);
    block = {}; mip = {1, 1, block}; format = VK_FORMAT_BC7_UNORM_BLOCK;
    assert(!pt::DecodeBcLevels(format, {&mip, 1}, storage, output));
}
