#include "engine/platform/ios_memory.h"
#include "engine/assets/ftex.h"
#include "engine/render/bc_decode.h"
#include "engine/render/gpu/gpu_vk_native.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    assert(pt::IosTextureLimitForMemory(0) == 512);
    assert(pt::IosTextureLimitForMemory(3662ull * 1048576) == 512);
    assert(pt::IosTextureLimitForMemory(4ull * 1024 * 1048576) == 512);
    assert(pt::IosTextureLimitForMemory(4ull * 1024 * 1048576 + 1) == 1024);
    assert(pt::IosTextureLimitForMemory(8ull * 1024 * 1048576) == 1024);
    if (argc != 3) return 2;
    const uint32_t limit = static_cast<uint32_t>(std::atoi(argv[2]));
    assert(pt::IosTextureLimit() == limit);
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
