#include "engine/platform/ios_memory.h"
#include "engine/assets/ftex.h"
#include "engine/assets/image_decode.h"
#include "engine/render/bc_decode.h"
#include "engine/render/gpu/gpu_vk_native.h"
#include <stb_image_write.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

// Compare the capped PNG chain against the corresponding levels of the full RGBA8 chain.
// Odd and one-dimensional inputs exercise the in-place reduction used for large model textures.
static void CheckModelImage(uint32_t width, uint32_t height, uint32_t limit, bool srgb) {
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>((i * 37) ^ (i >> 5));
    std::vector<uint8_t> png;
    const auto write = [](void* context, void* data, int len) {
        auto& bytes = *static_cast<std::vector<uint8_t>*>(context);
        const auto* chunk = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), chunk, chunk + len);
    };
    assert(stbi_write_png_to_func(write, &png, width, height, 4, pixels.data(), width * 4));
    std::vector<std::vector<uint8_t>> full, capped;
    uint32_t w = 0, h = 0;
    std::string error;
    assert(pt::DecodeImageMips(png, srgb, pt::kMaxImageDimension, full, w, h, error));
    assert(w == width && h == height && full.front() == pixels);
    assert(pt::DecodeImageMips(png, srgb, limit, capped, w, h, error));
    size_t skipped = 0;
    while (std::max(width, height) > limit) {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
        ++skipped;
    }
    assert(w == width && h == height && capped.back().size() == 4);
    assert(std::equal(capped.begin(), capped.end(), full.begin() + skipped, full.end()));
}

int main(int argc, char** argv) {
    assert(pt::IosTextureLimitForMemory(0) == 512);
    assert(pt::IosTextureLimitForMemory(3662ull * 1048576) == 512);
    assert(pt::IosTextureLimitForMemory(4ull * 1024 * 1048576) == 512);
    assert(pt::IosTextureLimitForMemory(4ull * 1024 * 1048576 + 1) == 1024);
    assert(pt::IosTextureLimitForMemory(8ull * 1024 * 1048576) == 1024);
    if (argc != 3) return 2;
    const uint32_t limit = static_cast<uint32_t>(std::atoi(argv[2]));
    assert(pt::IosTextureLimit() == limit);
    for (bool srgb : {false, true}) {
        CheckModelImage(2048, 1024, limit, srgb);
        CheckModelImage(1, 1025, limit, srgb);
        CheckModelImage(5, 3, 1, srgb);
    }
    pt::QarArchive qar;
    assert(qar.Open(std::filesystem::path(argv[1]) / "texture.qar"));
    for (const char* path : {
             "/Assets/sh/environ/object/shsb/telephone/shsb_tlph001/sourceimages/shsb_tlph001_bsm",
             "/Assets/sh/environ/object/shsb/telephone/shsb_tlph001/sourceimages/shsb_tlph001_nrm"}) {
        pt::FtexTexture texture;
        assert(pt::LoadFtex(qar, path, texture));
        assert(texture.width >= 1024 && texture.width == texture.height);
        uint32_t first = 0;
        while (first < texture.mip_count && texture.mips[first].empty()) ++first;
        assert(first < texture.mip_count && texture.MipWidth(first) == limit && texture.MipHeight(first) == limit);
        std::vector<pt::TextureMip> levels;
        for (uint32_t i = first; i < texture.mip_count; ++i)
            levels.push_back({texture.MipWidth(i), texture.MipHeight(i), texture.mips[i]});
        std::vector<std::vector<uint8_t>> decoded;
        std::vector<pt::TextureMip> output;
        VkFormat format = pt::gpu::vk::Native(texture.Format());
        assert(pt::DecodeBcLevels(format, levels, decoded, output));
        size_t bytes = 0;
        for (const auto& level : decoded) bytes += level.size();
        assert(decoded[0].size() == size_t(limit) * limit * 4);
        assert(output.back().width == texture.MipWidth(texture.mip_count - 1));
        assert(output.back().height == texture.MipHeight(texture.mip_count - 1));
        std::printf("%s: %ux%u, %zu levels, %zu decoded bytes\n", path, limit, limit, output.size(), bytes);
    }
}
