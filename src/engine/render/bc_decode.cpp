// BC fallback adapted from Alex Blount (ilsonb), PR #5.
#include "engine/render/bc_decode.h"
#if PT_IOS || PT_BC_DECODE_TEST
#include <bc7decomp.h>
#include <rgbcx.h>
#include <algorithm>
#include <cstring>
namespace pt {
namespace {
void DecodeBcColor4(const uint8_t* block, uint8_t* pixels) {
    const uint16_t c0 = uint16_t(block[0]) | uint16_t(block[1]) << 8;
    const uint16_t c1 = uint16_t(block[2]) | uint16_t(block[3]) << 8;
    uint8_t colors[4][3];
    for (int i = 0; i < 2; ++i) {
        const uint16_t c = i ? c1 : c0;
        const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        colors[i][0] = (r << 3) | (r >> 2);
        colors[i][1] = (g << 2) | (g >> 4);
        colors[i][2] = (b << 3) | (b >> 2);
    }
    for (int channel = 0; channel < 3; ++channel) {
        colors[2][channel] = (2 * colors[0][channel] + colors[1][channel]) / 3;
        colors[3][channel] = (colors[0][channel] + 2 * colors[1][channel]) / 3;
    }
    for (int i = 0; i < 16; ++i) {
        const unsigned selector = (block[4 + i / 4] >> (2 * (i % 4))) & 3;
        std::memcpy(pixels + i * 4, colors[selector], 3);
        pixels[i * 4 + 3] = 255;
    }
}
}

// BC compressed levels decoded to a plain format, for GPUs without BC textures (Context::bc_textures): RGBA8 for BC1, BC2,
// BC3 and BC7, R8 for BC4 and R8G8 for BC5 (normal maps keep their two channels). False for formats with no decoder here.
bool DecodeBcLevels(VkFormat& format, std::span<const TextureMip> mips, std::vector<std::vector<uint8_t>>& storage,
                    std::vector<TextureMip>& out) {
    enum class Kind { Bc1, Bc2, Bc3, Bc4, Bc5, Bc7 } kind;
    VkFormat plain = VK_FORMAT_UNDEFINED;
    switch (format) {
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK: case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: kind = Kind::Bc1; plain = VK_FORMAT_R8G8B8A8_UNORM; break;
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK: case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: kind = Kind::Bc1; plain = VK_FORMAT_R8G8B8A8_SRGB; break;
    case VK_FORMAT_BC2_UNORM_BLOCK: kind = Kind::Bc2; plain = VK_FORMAT_R8G8B8A8_UNORM; break;
    case VK_FORMAT_BC2_SRGB_BLOCK: kind = Kind::Bc2; plain = VK_FORMAT_R8G8B8A8_SRGB; break;
    case VK_FORMAT_BC3_UNORM_BLOCK: kind = Kind::Bc3; plain = VK_FORMAT_R8G8B8A8_UNORM; break;
    case VK_FORMAT_BC3_SRGB_BLOCK: kind = Kind::Bc3; plain = VK_FORMAT_R8G8B8A8_SRGB; break;
    case VK_FORMAT_BC4_UNORM_BLOCK: kind = Kind::Bc4; plain = VK_FORMAT_R8_UNORM; break;
    case VK_FORMAT_BC5_UNORM_BLOCK: kind = Kind::Bc5; plain = VK_FORMAT_R8G8_UNORM; break;
    case VK_FORMAT_BC7_UNORM_BLOCK: kind = Kind::Bc7; plain = VK_FORMAT_R8G8B8A8_UNORM; break;
    case VK_FORMAT_BC7_SRGB_BLOCK: kind = Kind::Bc7; plain = VK_FORMAT_R8G8B8A8_SRGB; break;
    default: return false;
    }
    const uint32_t block_bytes = kind == Kind::Bc1 || kind == Kind::Bc4 ? 8 : 16;
    const uint32_t texel_bytes = kind == Kind::Bc4 ? 1 : kind == Kind::Bc5 ? 2 : 4;
    storage.resize(mips.size());
    out.resize(mips.size());
    for (size_t m = 0; m < mips.size(); ++m) {
        const TextureMip& mip = mips[m];
        const uint32_t w = std::max(mip.width, 1u), h = std::max(mip.height, 1u), bw = (w + 3) / 4, bh = (h + 3) / 4;
        if (mip.data.size() < size_t(bw) * bh * block_bytes) return false;
        std::vector<uint8_t>& texels = storage[m];
        texels.assign(size_t(w) * h * texel_bytes, 0);
        for (uint32_t by = 0; by < bh; ++by) {
            for (uint32_t bx = 0; bx < bw; ++bx) {
                const uint8_t* block = mip.data.data() + (size_t(by) * bw + bx) * block_bytes;
                uint8_t px[16 * 4]{};
                switch (kind) {
                case Kind::Bc1:
                    rgbcx::unpack_bc1(block, px);
                    if (format == VK_FORMAT_BC1_RGB_UNORM_BLOCK || format == VK_FORMAT_BC1_RGB_SRGB_BLOCK)
                        for (int i = 0; i < 16; ++i) px[i * 4 + 3] = 255;
                    break;
                case Kind::Bc2:
                    DecodeBcColor4(block + 8, px);
                    for (int i = 0; i < 16; ++i) {
                        const uint8_t a = (block[i / 2] >> ((i & 1) * 4)) & 15;
                        px[i * 4 + 3] = static_cast<uint8_t>(a * 17);
                    }
                    break;
                case Kind::Bc3: DecodeBcColor4(block + 8, px); rgbcx::unpack_bc4(block, px + 3, 4); break;
                case Kind::Bc4: rgbcx::unpack_bc4(block, px, 1); break;
                case Kind::Bc5: rgbcx::unpack_bc5(block, px, 0, 1, 2); break;
                case Kind::Bc7: if (!bc7decomp::unpack_bc7(block, reinterpret_cast<bc7decomp::color_rgba*>(px))) return false; break;
                }
                for (uint32_t y = 0; y < 4 && by * 4 + y < h; ++y) {
                    const uint32_t cols = std::min(4u, w - bx * 4);
                    std::memcpy(texels.data() + (size_t(by * 4 + y) * w + bx * 4) * texel_bytes, px + y * 4 * texel_bytes,
                                cols * texel_bytes);
                }
            }
        }
        out[m] = {w, h, texels};
    }
    format = plain;
    return true;
}

}
#endif
