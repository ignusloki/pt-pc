#include "engine/assets/texture_cache.h"
#include "engine/assets/enhanced_textures.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <fstream>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++failures; } };
    pt::FtexTexture source;
    source.width = 8; source.height = 4; source.depth = 1; source.faces = 1; source.pixel_format = 2; source.flags = 2;
    check(pt::EnhancedTextureEligible("/Assets/sh/texture/wall_bsm", source), "opaque albedo eligible");
    check(!pt::EnhancedTextureEligible("/Assets/sh/texture/wall_nrm", source), "normal name excluded");
    source.flags |= 8;
    check(!pt::EnhancedTextureEligible("/Assets/sh/texture/wall_bsm", source), "normal flag excluded");
    source.flags = 2; source.pixel_format = 4;
    check(!pt::EnhancedTextureEligible("/Assets/sh/texture/wall_bsm", source), "alpha texture excluded");
    source.pixel_format = 2;
    check(!pt::EnhancedTextureEligible("/Assets/sh/ui/texture/wall_bsm", source), "UI excluded");
    source.faces = 6;
    check(!pt::EnhancedTextureEligible("/Assets/sh/texture/wall_bsm", source), "cube excluded");
    source.faces = 1;
    source.width = source.height = 2048;
    check(pt::EnhancedTextureEligible("/Assets/sh/texture/wall_bsm", source), "2048 colour map eligible (the hallway's walls)");
    source.width = source.height = 4096;
    check(!pt::EnhancedTextureEligible("/Assets/sh/texture/wall_bsm", source), "4096 excluded");
    source.width = 8; source.height = 4;
    source.mip_count = 1;
    source.mips = {{1, 2, 3, 4}};
    const uint64_t source_key = pt::EnhancedTextureKey(source, 1);
    check(source_key != pt::EnhancedTextureKey(source, 2), "model changes invalidate cache");
    source.mips[0][0] = 9;
    check(source_key != pt::EnhancedTextureKey(source, 1), "source bytes invalidate cache");
    const auto root = std::filesystem::temp_directory_path() / ("pt-cache-test-" + std::to_string(SDL_GetTicksNS()));
    std::filesystem::create_directory(root);
    const auto file = root / "texture.pttex";
    std::vector<uint8_t> pixels(8 * 4 * 4, 255);
    pt::FtexTexture enhanced;
    check(pt::EncodeEnhancedTexture(8, 4, pixels, true, enhanced), "encode BC7 chain");
    check(enhanced.mips.size() == 4 && enhanced.mips.back().size() == 16, "chain through 1x1");
    check(pt::WriteTextureCache(file, 42, enhanced), "cache write");
    pt::FtexTexture read;
    check(pt::ReadTextureCache(file, 42, read), "cache roundtrip");
    check(read.mips == enhanced.mips && read.width == 8 && read.Srgb(), "roundtrip contents");
    check(read.Format() == pt::gpu::Format::Bc7SrgbBlock, "BC7 sRGB upload format");
    check(!pt::ReadTextureCache(file, 43, read), "different source rejected");
    check(pt::TextureCacheCurrent(file, 42) && !pt::TextureCacheCurrent(file, 43), "header check names the source");
    {
        std::fstream corrupt(file, std::ios::in | std::ios::out | std::ios::binary);
        corrupt.seekp(40); corrupt.put('\0');
    }
    check(!pt::ReadTextureCache(file, 42, read), "corrupt block rejected");
    check(pt::TextureCacheCurrent(file, 42), "header check does not read the blocks (the load does)");
    std::filesystem::resize_file(file, 31);
    check(!pt::ReadTextureCache(file, 42, read), "truncated cache rejected");
    check(!pt::TextureCacheCurrent(file, 42), "truncated cache not current");
    std::filesystem::remove(file);

    {
        pt::FtexTexture t;
        t.width = t.height = 64; t.faces = 1; t.depth = 1;
        t.pixel_format = 4; t.flags = 8;
        check(pt::EnhancedTextureKindOf("/Assets/sh/texture/wall_nrm", t) == pt::EnhancedKind::Normal, "DXT5nm normal map");
        check(!pt::EnhancedTextureEligible("/Assets/sh/texture/wall_nrm", t, pt::kEnhancedColourKinds), "normal map left out with colour maps only");
        check(!pt::EnhancedTextureEligible("/Assets/sh/decal/wall_nrm", t), "decal normal map excluded");
        t.pixel_format = 2; t.flags = 0;
        check(pt::EnhancedTextureKindOf("/Assets/sh/texture/wall_srm", t) == pt::EnhancedKind::Material, "specular and roughness map");
        check(pt::EnhancedTextureKindOf("/Assets/sh/texture/wall_trm", t) == pt::EnhancedKind::None, "translucency map excluded");
        t.pixel_format = 4; t.flags = 2;
        check(pt::EnhancedTextureKindOf("/Assets/sh/texture/hair_bsm_alp", t) == pt::EnhancedKind::ColourAlpha, "alpha colour map");
        check(pt::EnhancedTextureKindOf("/Assets/sh/texture/mask_alp", t) == pt::EnhancedKind::None, "plain alpha mask excluded");
        t.mip_count = 1; t.mips = {{1, 2, 3}};
        check(pt::EnhancedTextureKey(t, 1) == pt::EnhancedTextureKey(t, 1, pt::EnhancedKind::Colour), "colour key unchanged");
        check(pt::EnhancedTextureKey(t, 1, pt::EnhancedKind::Normal) != pt::EnhancedTextureKey(t, 1, pt::EnhancedKind::Material), "kinds keyed apart");
    }
    auto bc3_source = [](uint32_t w, uint32_t h, std::vector<uint8_t> rgba, uint32_t flags) {
        pt::FtexTexture t;
        t.width = w; t.height = h; t.faces = 1; t.depth = 1; t.pixel_format = 4; t.flags = flags;
        for (;;) {
            std::vector<uint8_t> blocks(size_t((w + 3) / 4) * ((h + 3) / 4) * 16);
            for (uint32_t by = 0; by < (h + 3) / 4; ++by) for (uint32_t bx = 0; bx < (w + 3) / 4; ++bx) {
                uint8_t px[64];
                for (uint32_t y = 0; y < 4; ++y) for (uint32_t x = 0; x < 4; ++x)
                    std::copy_n(&rgba[(size_t(std::min(by * 4 + y, h - 1)) * w + std::min(bx * 4 + x, w - 1)) * 4], 4, px + (y * 4 + x) * 4);
                uint8_t* out = &blocks[(size_t(by) * ((w + 3) / 4) + bx) * 16];
                pt::EncodeBc3AlphaBlock(px, out);
                pt::EncodeBc1Block(px, out + 8);
            }
            t.mips.push_back(std::move(blocks));
            if (w == 1 && h == 1) break;
            rgba = pt::ReduceEnhancedLevel(pt::EnhancedKind::Material, false, w, h, rgba);
            w = std::max(1u, w / 2); h = std::max(1u, h / 2);
        }
        t.mip_count = static_cast<uint32_t>(t.mips.size());
        return t;
    };
    {
        const uint32_t w = 32, h = 16;
        std::vector<uint8_t> flat(w * h * 4);
        for (size_t i = 0; i < flat.size(); i += 4) { flat[i] = 132; flat[i + 1] = 128; flat[i + 2] = 132; flat[i + 3] = 128; }
        auto up = pt::UpscaleNormalMap2x(w, h, flat, 0.8f);
        bool same = up.size() == size_t(w) * h * 16;
        for (size_t i = 0; same && i < up.size(); i += 4) same = up[i] == 132 && up[i + 2] == 132 && std::abs(up[i + 1] - 128) <= 1 && std::abs(up[i + 3] - 128) <= 1;
        check(same, "flat normal map stays flat");
        for (size_t i = 0; i < flat.size(); i += 4) { flat[i + 1] = 100; flat[i + 3] = 170; }
        up = pt::UpscaleNormalMap2x(w, h, flat, 0.8f);
        same = true;
        for (size_t i = 0; same && i < up.size(); i += 4) same = std::abs(up[i + 1] - 100) <= 1 && std::abs(up[i + 3] - 170) <= 1;
        check(same, "constant tilt kept (no flipped or swapped channels)");
        std::vector<uint8_t> wave(w * h * 4);
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &wave[(size_t(y) * w + x) * 4];
            p[0] = p[2] = 132;
            p[3] = static_cast<uint8_t>(128 + 60 * std::sin(x * 6.2831853f / w));
            p[1] = static_cast<uint8_t>(128 + 40 * std::cos(y * 6.2831853f / h));
        }
        up = pt::UpscaleNormalMap2x(w, h, wave, 0.8f);
        int edge = std::abs(up[3] - up[(2 * w - 1) * 4 + 3]), inside = 0;
        for (uint32_t x = 1; x < 2 * w; ++x) inside = std::max(inside, std::abs(up[x * 4 + 3] - up[(x - 1) * 4 + 3]));
        check(edge <= inside, "no seam at the wrapped edge");
        bool unit = true;
        for (size_t i = 0; i < up.size(); i += 4) {
            const float nx = up[i + 3] / 127.5f - 1.0f, ny = up[i + 1] / 127.5f - 1.0f;
            unit = unit && nx * nx + ny * ny <= 1.0f;
        }
        check(unit, "renormalised normals");
        check(up[(w / 2) * 4 + 3] > 128 && up[(3 * w / 2) * 4 + 3] < 128, "x keeps its sign");
        const pt::FtexTexture source = bc3_source(w, h, wave, 8);
        pt::FtexTexture out;
        check(pt::EncodeEnhancedTexture(pt::EnhancedKind::Normal, 2 * w, 2 * h, up, source, out, 3), "encode normal chain");
        check(out.pixel_format == 4 && out.Format() == pt::gpu::Format::Bc3UnormBlock && out.mips.size() == 7, "BC3 normal chain through 1x1");
        std::vector<uint8_t> decoded;
        int worst = 0;
        if (pt::DecodeFtexLevel(out, 0, decoded))
            for (size_t i = 0; i < decoded.size(); i += 4) worst = std::max({worst, std::abs(decoded[i + 1] - up[i + 1]), std::abs(decoded[i + 3] - up[i + 3])});
        check(!decoded.empty() && worst <= 8, "BC3 keeps x and y");
        check(pt::WriteTextureCache(file, 7, out), "BC3 cache write");
        pt::FtexTexture back;
        check(pt::ReadTextureCache(file, 7, back) && back.mips == out.mips && back.pixel_format == 4 && !back.Srgb(), "BC3 cache roundtrip");
        std::filesystem::remove(file);
    }
    {
        const uint32_t w = 16, h = 16;
        std::vector<uint8_t> srm(w * h * 4);
        uint64_t before = 0;
        for (size_t i = 0; i < srm.size(); i += 4) {
            srm[i] = 250; srm[i + 1] = static_cast<uint8_t>(120 + (i * 37 % 61)); srm[i + 2] = 0; srm[i + 3] = 255;
            before += srm[i + 1];
        }
        const auto up = pt::UpscaleMaterialMap2x(w, h, srm, 0.5f);
        uint64_t after = 0;
        for (size_t i = 0; i < up.size(); i += 4) after += up[i + 1];
        check(std::abs(double(after) / (up.size() / 4) - double(before) / (srm.size() / 4)) < 1.0, "roughness mean kept");
        pt::FtexTexture source;
        source.width = w; source.height = h; source.pixel_format = 2; source.flags = 0; source.mip_count = 1;
        pt::FtexTexture out;
        check(pt::EncodeEnhancedTexture(pt::EnhancedKind::Material, 2 * w, 2 * h, up, source, out), "encode material chain");
        check(out.Format() == pt::gpu::Format::Bc1RgbaUnormBlock && out.mips[0].size() == 8 * 8 * 8, "BC1 material chain");
        check(pt::WriteTextureCache(file, 9, out), "BC1 cache write");
        pt::FtexTexture back;
        check(pt::ReadTextureCache(file, 9, back) && back.mips == out.mips && back.pixel_format == 2, "BC1 cache roundtrip");
        check(!pt::ReadTextureCache(file, 8, back), "BC1 cache keyed");
        std::filesystem::remove(file);
    }
    {
        const uint32_t w = 32, h = 32;
        std::vector<uint8_t> hair(w * h * 4);
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &hair[(size_t(y) * w + x) * 4];
            p[0] = 90; p[1] = 60; p[2] = 40;
            p[3] = ((x + y / 2) % 6) < 2 ? 255 : ((x + y / 2) % 6) == 2 ? 140 : 0;
        }
        const pt::FtexTexture source = bc3_source(w, h, hair, 2);
        std::vector<uint8_t> stored;
        pt::DecodeFtexLevel(source, 0, stored);
        std::vector<uint8_t> up(size_t(w) * h * 16, 0);
        for (size_t i = 0; i < up.size(); i += 4) { up[i] = 90; up[i + 1] = 60; up[i + 2] = 40; }
        pt::UpscaleAlpha2x(w, h, stored, up);
        pt::FtexTexture out;
        check(pt::EncodeEnhancedTexture(pt::EnhancedKind::ColourAlpha, 2 * w, 2 * h, up, source, out, 2), "encode alpha colour chain");
        check(out.Format() == pt::gpu::Format::Bc3SrgbBlock, "BC3 sRGB alpha colour chain");
        auto coverage = [](const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, uint32_t samples) {
            size_t n = 0;
            for (uint32_t sy = 0; sy < samples; ++sy) for (uint32_t sx = 0; sx < samples; ++sx) {
                const float u = (sx + 0.5f) / samples * w - 0.5f, v = (sy + 0.5f) / samples * h - 0.5f;
                const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
                const float fx = u - x0, fy = v - y0;
                auto a = [&](int x, int y) { return rgba[(size_t((y + h) % h) * w + (x + w) % w) * 4 + 3] / 255.0f; };
                const float top = a(x0, y0) + fx * (a(x0 + 1, y0) - a(x0, y0)), bottom = a(x0, y0 + 1) + fx * (a(x0 + 1, y0 + 1) - a(x0, y0 + 1));
                n += top + fy * (bottom - top) >= 0.5f;
            }
            return double(n) / (double(samples) * samples);
        };
        bool kept = true;
        for (uint32_t level = 1; level < out.mip_count && level <= source.mip_count; ++level) {
            std::vector<uint8_t> a, b;
            kept = kept && pt::DecodeFtexLevel(out, level, a) && pt::DecodeFtexLevel(source, level - 1, b) && a.size() == b.size();
            for (size_t i = 3; kept && i < a.size(); i += 4) kept = std::abs(a[i] - b[i]) <= 4;
        }
        check(kept, "levels below the top keep the source's alpha");
        std::vector<uint8_t> top;
        pt::DecodeFtexLevel(out, 0, top);
        const double before = coverage(stored, w, h, w * 8), after = coverage(top, 2 * w, 2 * h, w * 8);
        std::printf("alpha tested coverage at 8 samples a source texel: source %.4f, enhanced %.4f\n", before, after);
        check(std::abs(after - before) <= 0.01, "top level keeps the coverage");
    }
    {
        const uint32_t w = 200, h = 136;
        std::vector<uint8_t> noise(size_t(w) * h * 4);
        uint32_t seed = 12345;
        for (size_t i = 0; i < noise.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            noise[i] = static_cast<uint8_t>((seed >> 24) * 0.6f + 40.0f + 30.0f * std::sin(float(i / 4 % w) * 0.21f));
        }
        for (size_t i = 3; i < noise.size(); i += 4) noise[i] = 255;
        pt::FtexTexture one, three, many;
        check(pt::EncodeEnhancedTexture(w, h, noise, true, one, 1) && pt::EncodeEnhancedTexture(w, h, noise, true, three, 3) &&
              pt::EncodeEnhancedTexture(w, h, noise, true, many, 16), "encode threaded BC7 chains");
        check(one.mips == three.mips && one.mips == many.mips, "BC7 chain does not depend on the thread count");
        auto reference_level = [](const std::vector<uint8_t>& src, uint32_t w, uint32_t h) {
            const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
            std::vector<uint8_t> next(size_t(nw) * nh * 4);
            auto lin = [](uint8_t v) { const float f = v / 255.0f; return f <= 0.04045f ? f / 12.92f : std::pow((f + 0.055f) / 1.055f, 2.4f); };
            for (uint32_t y = 0; y < nh; ++y) for (uint32_t x = 0; x < nw; ++x) for (uint32_t c = 0; c < 4; ++c) {
                float sum = 0;
                for (uint32_t dy = 0; dy < 2; ++dy) for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint8_t v = src[(size_t(std::min(y * 2 + dy, h - 1)) * w + std::min(x * 2 + dx, w - 1)) * 4 + c];
                    sum += c < 3 ? lin(v) : v / 255.0f;
                }
                if (c < 3) {
                    const float mean = sum * 0.25f;
                    const float f = mean <= 0.0031308f ? mean * 12.92f : 1.055f * std::pow(mean, 1.0f / 2.4f) - 0.055f;
                    next[(size_t(y) * nw + x) * 4 + c] = static_cast<uint8_t>(std::clamp(std::lround(f * 255), 0l, 255l));
                } else {
                    next[(size_t(y) * nw + x) * 4 + c] = static_cast<uint8_t>(std::lround(sum * 63.75f));
                }
            }
            return next;
        };
        std::vector<uint8_t> level = noise;
        uint32_t lw = w, lh = h;
        bool mips_equal = true;
        for (uint32_t i = 0; i < many.mip_count; ++i) {
            pt::FtexTexture single;
            mips_equal = mips_equal && pt::EncodeEnhancedTexture(lw, lh, level, true, single, 2) && single.mips[0] == many.mips[i];
            if (lw == 1 && lh == 1) break;
            level = reference_level(level, lw, lh);
            lw = std::max(1u, lw / 2); lh = std::max(1u, lh / 2);
        }
        check(mips_equal, "BC7 mips equal the plain reduction's levels");
        for (const auto kind : {pt::EnhancedKind::Normal, pt::EnhancedKind::Material, pt::EnhancedKind::ColourAlpha}) {
            const pt::FtexTexture source = bc3_source(w, h, noise, kind == pt::EnhancedKind::ColourAlpha ? 2 : kind == pt::EnhancedKind::Normal ? 8 : 0);
            pt::FtexTexture a, b;
            check(pt::EncodeEnhancedTexture(kind, w, h, noise, source, a, 1) && pt::EncodeEnhancedTexture(kind, w, h, noise, source, b, 9) &&
                  a.mips == b.mips, "BC1 and BC3 chains do not depend on the thread count");
            check(pt::ReduceEnhancedLevel(kind, kind == pt::EnhancedKind::ColourAlpha, w, h, noise, 1) ==
                  pt::ReduceEnhancedLevel(kind, kind == pt::EnhancedKind::ColourAlpha, w, h, noise, 7), "reduction does not depend on the thread count");
        }
        auto reference_2x = [](const std::vector<float>& src, uint32_t w, uint32_t h, float sharpen) {
            static constexpr float kEven[4] = {-0.0234375f, 0.2265625f, 0.8671875f, -0.0703125f};
            static constexpr float kOdd[4] = {-0.0703125f, 0.8671875f, 0.2265625f, -0.0234375f};
            const uint32_t w2 = w * 2, h2 = h * 2;
            auto wrap = [](int64_t i, uint32_t n) { return static_cast<uint32_t>(((i % n) + n) % n); };
            std::vector<float> rows(size_t(w2) * h), out(size_t(w2) * h2);
            for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w2; ++x) {
                const int64_t base = x / 2 - ((x & 1) ? 0 : 1);
                const float* k = (x & 1) ? kOdd : kEven;
                float sum = 0.0f;
                for (int t = 0; t < 4; ++t) sum += k[t] * src[size_t(y) * w + wrap(base - 1 + t, w)];
                rows[size_t(y) * w2 + x] = sum;
            }
            for (uint32_t y = 0; y < h2; ++y) {
                const int64_t base = y / 2 - ((y & 1) ? 0 : 1);
                const float* k = (y & 1) ? kOdd : kEven;
                for (uint32_t x = 0; x < w2; ++x) {
                    float r[4];
                    for (int t = 0; t < 4; ++t) r[t] = rows[size_t(wrap(base - 1 + t, h)) * w2 + x];
                    out[size_t(y) * w2 + x] = k[0] * r[0] + k[1] * r[1] + k[2] * r[2] + k[3] * r[3];
                }
            }
            if (sharpen <= 0.0f) return out;
            static constexpr float kGauss[7] = {0.00443305f, 0.05400558f, 0.24203623f, 0.39905028f, 0.24203623f, 0.05400558f, 0.00443305f};
            std::vector<float> blur(out.size());
            for (uint32_t y = 0; y < h2; ++y) for (uint32_t x = 0; x < w2; ++x) {
                float sum = 0.0f;
                for (int t = -3; t <= 3; ++t) sum += kGauss[t + 3] * out[size_t(y) * w2 + wrap(int64_t(x) + t, w2)];
                blur[size_t(y) * w2 + x] = sum;
            }
            std::vector<float> line(h2);
            for (uint32_t x = 0; x < w2; ++x) {
                for (uint32_t y = 0; y < h2; ++y) line[y] = blur[size_t(y) * w2 + x];
                for (uint32_t y = 0; y < h2; ++y) {
                    float sum = 0.0f;
                    for (int t = -3; t <= 3; ++t) sum += kGauss[t + 3] * line[wrap(int64_t(y) + t, h2)];
                    float& o = out[size_t(y) * w2 + x];
                    o += sharpen * (o - sum);
                }
            }
            return out;
        };
        const auto material = pt::UpscaleMaterialMap2x(w, h, noise, 0.5f);
        bool material_equal = material.size() == size_t(w) * h * 16;
        std::vector<float> channel(size_t(w) * h);
        for (uint32_t c = 0; c < 4 && material_equal; ++c) {
            for (size_t i = 0; i < channel.size(); ++i) channel[i] = noise[i * 4 + c] / 255.0f;
            const auto up = reference_2x(channel, w, h, c < 3 ? 0.5f : 0.0f);
            for (size_t i = 0; i < up.size() && material_equal; ++i)
                material_equal = material[i * 4 + c] == static_cast<uint8_t>(std::clamp(std::lround(up[i] * 255.0f), 0l, 255l));
        }
        check(material_equal, "sharpened 2x equals the first implementation");
        std::atomic<int> covered{0};
        pt::ParallelRanges(1000, 7, [&](size_t first, size_t last) { covered += static_cast<int>(last - first); });
        check(covered == 1000, "ParallelRanges covers every item once");
    }
    std::filesystem::remove(root);
    std::printf("texture cache: %d failures\n", failures);
    return failures ? 1 : 0;
}
