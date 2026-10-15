#include "engine/assets/texture_cache.h"
#include "engine/core/log.h"
#include "engine/platform/os.h"

#include <bc7enc.h>
#include <rgbcx.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>

namespace pt {
EncodeProfile& EncodeProfileTotals() { static EncodeProfile totals; return totals; }
namespace {
using ProfileClock = std::chrono::steady_clock;
int64_t ProfileNs(ProfileClock::time_point from) { return std::chrono::duration_cast<std::chrono::nanoseconds>(ProfileClock::now() - from).count(); }
constexpr uint32_t kMagic = 0x31585450;
constexpr uint32_t kMaxSide = 4096;
constexpr uint32_t kRgbcxLevel = 12;
uint32_t Levels(uint32_t w, uint32_t h) {
    uint32_t n = 1;
    while (w > 1 || h > 1) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); ++n; }
    return n;
}
size_t Bytes(uint32_t w, uint32_t h, uint32_t format = 8) { return size_t((w + 3) / 4) * ((h + 3) / 4) * (format == 2 ? 8 : 16); }
bool CacheFormat(uint32_t format) { return format == 2 || format == 4 || format == 8; }
float LinearExact(uint8_t v) {
    const float f = v / 255.0f;
    return f <= 0.04045f ? f / 12.92f : std::pow((f + 0.055f) / 1.055f, 2.4f);
}
float Linear(uint8_t v) {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (size_t i = 0; i < t.size(); ++i) t[i] = LinearExact(static_cast<uint8_t>(i));
        return t;
    }();
    return table[v];
}
uint8_t Gamma(float f) {
    f = f <= 0.0031308f ? f * 12.92f : 1.055f * std::pow(f, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(std::lround(f * 255), 0l, 255l));
}
uint8_t Unorm(float f) { return static_cast<uint8_t>(std::clamp(std::lround(f * 255.0f), 0l, 255l)); }
std::vector<float> Channel2x(const std::vector<float>& src, uint32_t w, uint32_t h, float sharpen) {
    static constexpr float kEven[4] = {-0.0234375f, 0.2265625f, 0.8671875f, -0.0703125f};
    static constexpr float kOdd[4] = {-0.0703125f, 0.8671875f, 0.2265625f, -0.0234375f};
    const uint32_t w2 = w * 2, h2 = h * 2;
    auto wrap = [](int64_t i, uint32_t n) { return static_cast<uint32_t>(((i % n) + n) % n); };
    std::vector<float> rows(size_t(w2) * h), out(size_t(w2) * h2);
    for (uint32_t y = 0; y < h; ++y) {
        const float* s = src.data() + size_t(y) * w;
        float* d = rows.data() + size_t(y) * w2;
        for (uint32_t x = 0; x < w2; ++x) {
            const int64_t base = x / 2 - ((x & 1) ? 0 : 1);
            const float* k = (x & 1) ? kOdd : kEven;
            float sum = 0.0f;
            for (int t = 0; t < 4; ++t) sum += k[t] * s[wrap(base - 1 + t, w)];
            d[x] = sum;
        }
    }
    for (uint32_t y = 0; y < h2; ++y) {
        const int64_t base = y / 2 - ((y & 1) ? 0 : 1);
        const float* k = (y & 1) ? kOdd : kEven;
        float* d = out.data() + size_t(y) * w2;
        const float* r[4];
        for (int t = 0; t < 4; ++t) r[t] = rows.data() + size_t(wrap(base - 1 + t, h)) * w2;
        for (uint32_t x = 0; x < w2; ++x) d[x] = k[0] * r[0][x] + k[1] * r[1][x] + k[2] * r[2][x] + k[3] * r[3][x];
    }
    if (sharpen <= 0.0f) return out;
    static constexpr float kGauss[7] = {0.00443305f, 0.05400558f, 0.24203623f, 0.39905028f, 0.24203623f, 0.05400558f, 0.00443305f};
    std::vector<float> blur(out.size());
    for (uint32_t y = 0; y < h2; ++y) {
        const float* s = out.data() + size_t(y) * w2;
        float* b = blur.data() + size_t(y) * w2;
        for (uint32_t x = 0; x < w2; ++x) {
            float sum = 0.0f;
            for (int t = -3; t <= 3; ++t) sum += kGauss[t + 3] * s[wrap(int64_t(x) + t, w2)];
            b[x] = sum;
        }
    }
    for (uint32_t y = 0; y < h2; ++y) {
        const float* r[7];
        for (int t = -3; t <= 3; ++t) r[t + 3] = blur.data() + size_t(wrap(int64_t(y) + t, h2)) * w2;
        float* o_row = out.data() + size_t(y) * w2;
        for (uint32_t x = 0; x < w2; ++x) {
            float sum = 0.0f;
            for (int t = 0; t < 7; ++t) sum += kGauss[t] * r[t][x];
            float& o = o_row[x];
            o += sharpen * (o - sum);
        }
    }
    return out;
}
void EncodeBlocks(uint32_t format, uint32_t w, uint32_t h, const uint8_t* pixels, uint8_t* out, uint32_t first, uint32_t last,
                  const bc7enc_compress_block_params* bc7) {
    const uint32_t bw = (w + 3) / 4;
    const size_t block = format == 2 ? 8 : 16;
    for (uint32_t by = first; by < last; ++by) for (uint32_t bx = 0; bx < bw; ++bx) {
        uint8_t px[64];
        for (uint32_t y = 0; y < 4; ++y) for (uint32_t x = 0; x < 4; ++x) {
            const size_t src = (size_t(std::min(by * 4 + y, h - 1)) * w + std::min(bx * 4 + x, w - 1)) * 4;
            std::copy_n(pixels + src, 4, px + (y * 4 + x) * 4);
        }
        uint8_t* dst = out + (size_t(by) * bw + bx) * block;
        if (format == 8) bc7enc_compress_block(dst, px, bc7);
        else if (format == 4) rgbcx::encode_bc3_hq(kRgbcxLevel, dst, px);
        else rgbcx::encode_bc1(kRgbcxLevel, dst, px, false, false);
    }
}
void EncodeLevel(uint32_t format, uint32_t w, uint32_t h, const uint8_t* pixels, uint8_t* out, const bc7enc_compress_block_params* bc7,
                 unsigned threads) {
    const uint32_t rows = (h + 3) / 4;
    const unsigned n = size_t(w) * h < 128 * 128 ? 1u : std::min<unsigned>(std::clamp(threads, 1u, 32u), rows);
    if (n <= 1) { EncodeBlocks(format, w, h, pixels, out, 0, rows, bc7); return; }
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < n; ++t) workers.emplace_back(EncodeBlocks, format, w, h, pixels, out, rows * t / n, rows * (t + 1) / n, bc7);
    for (auto& worker : workers) worker.join();
}
void InitEncoders() {
    static std::once_flag init;
    std::call_once(init, [] { bc7enc_compress_block_init(); rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal); });
}
}

void ParallelRanges(size_t count, unsigned threads, const std::function<void(size_t, size_t)>& fn) {
    const size_t n = std::min<size_t>(std::clamp(threads, 1u, 64u), count);
    if (n <= 1) { if (count) fn(0, count); return; }
    std::vector<std::thread> workers;
    workers.reserve(n - 1);
    for (size_t t = 1; t < n; ++t) workers.emplace_back(fn, count * t / n, count * (t + 1) / n);
    fn(0, count / n);
    for (auto& worker : workers) worker.join();
}

EnhancedKind EnhancedTextureKindOf(std::string_view path, const FtexTexture& t) {
    if (t.Cube() || t.faces != 1 || t.depth != 1 || !t.width || !t.height || t.width > kEnhancedMaxSource || t.height > kEnhancedMaxSource)
        return EnhancedKind::None;
    if (path.find("/ui/") != path.npos || path.find("/effect/") != path.npos || path.find("/decal/") != path.npos) return EnhancedKind::None;
    if (t.NormalMap()) return t.pixel_format == 4 && !t.Srgb() && path.ends_with("_nrm") ? EnhancedKind::Normal : EnhancedKind::None;
    if (t.pixel_format == 2 && t.Srgb() && (path.ends_with("_bsm") || path.ends_with("_lym") || path.ends_with("_ils"))) return EnhancedKind::Colour;
    if (t.pixel_format == 4 && t.Srgb() && path.ends_with("_bsm_alp")) return EnhancedKind::ColourAlpha;
    if (t.pixel_format == 2 && !t.Srgb() && path.ends_with("_srm")) return EnhancedKind::Material;
    return EnhancedKind::None;
}

bool EnhancedTextureEligible(std::string_view path, const FtexTexture& t, uint32_t kinds) {
    const EnhancedKind kind = EnhancedTextureKindOf(path, t);
    return kind != EnhancedKind::None && (kinds & EnhancedKindBit(kind)) != 0;
}

bool EncodeEnhancedTexture(uint32_t w, uint32_t h, std::span<const uint8_t> rgba, bool srgb, FtexTexture& out, unsigned threads) {
    if (!w || !h || w > kMaxSide || h > kMaxSide || rgba.size() != size_t(w) * h * 4) return false;
    InitEncoders();
    out = {};
    out.width = w; out.height = h; out.faces = 1; out.depth = 1; out.flags = srgb ? 2 : 0;
    out.pixel_format = 8; out.mip_count = Levels(w, h);
    std::vector<uint8_t> pixels(rgba.begin(), rgba.end());
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    params.m_max_partitions = 8;
    for (uint32_t level = 0; level < out.mip_count; ++level) {
        auto& mip = out.mips.emplace_back(Bytes(w, h));
        const auto block_start = ProfileClock::now();
        EncodeLevel(8, w, h, pixels.data(), mip.data(), &params, threads);
        EncodeProfileTotals().blocks_ns += ProfileNs(block_start);
        if (w == 1 && h == 1) break;
        const auto mip_start = ProfileClock::now();
        const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
        std::vector<uint8_t> next(size_t(nw) * nh * 4);
        const unsigned mip_threads = size_t(nw) * nh < 128 * 128 ? 1u : threads;
        const uint32_t cw = w, ch = h;
        const uint8_t* source = pixels.data();
        uint8_t* target = next.data();
        ParallelRanges(nh, mip_threads, [&](size_t first, size_t last) {
            for (size_t y = first; y < last; ++y) for (uint32_t x = 0; x < nw; ++x) for (uint32_t c = 0; c < 4; ++c) {
                float sum = 0;
                for (uint32_t dy = 0; dy < 2; ++dy) for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint8_t v = source[(size_t(std::min<uint32_t>(static_cast<uint32_t>(y) * 2 + dy, ch - 1)) * cw + std::min(x * 2 + dx, cw - 1)) * 4 + c];
                    sum += srgb && c < 3 ? Linear(v) : v / 255.0f;
                }
                target[(y * nw + x) * 4 + c] = srgb && c < 3 ? Gamma(sum * 0.25f) : static_cast<uint8_t>(std::lround(sum * 63.75f));
            }
        });
        pixels = std::move(next); w = nw; h = nh;
        EncodeProfileTotals().mips_ns += ProfileNs(mip_start);
    }
    return true;
}

std::vector<uint8_t> ReduceEnhancedLevel(EnhancedKind kind, bool srgb, uint32_t w, uint32_t h, std::span<const uint8_t> rgba, unsigned threads) {
    const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
    std::vector<uint8_t> next(size_t(nw) * nh * 4);
    auto at = [&](uint32_t x, uint32_t y, uint32_t dx, uint32_t dy) {
        return rgba.data() + (size_t(std::min(y * 2 + dy, h - 1)) * w + std::min(x * 2 + dx, w - 1)) * 4;
    };
    ParallelRanges(nh, size_t(nw) * nh < 128 * 128 ? 1u : threads, [&](size_t first, size_t last) {
    for (uint32_t y = static_cast<uint32_t>(first); y < last; ++y) for (uint32_t x = 0; x < nw; ++x) {
        uint8_t* d = next.data() + (size_t(y) * nw + x) * 4;
        if (kind == EnhancedKind::Normal) {
            float n[3] = {};
            for (uint32_t dy = 0; dy < 2; ++dy) for (uint32_t dx = 0; dx < 2; ++dx) {
                const uint8_t* p = at(x, y, dx, dy);
                const float nx = p[3] / 255.0f * 2.0f - 1.0f, ny = p[1] / 255.0f * 2.0f - 1.0f;
                n[0] += nx; n[1] += ny; n[2] += std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
            }
            const float len = std::max(1e-6f, std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]));
            const uint8_t* p = at(x, y, 0, 0);
            d[0] = p[0]; d[2] = p[2];
            d[3] = Unorm(n[0] / len * 0.5f + 0.5f);
            d[1] = Unorm(n[1] / len * 0.5f + 0.5f);
            continue;
        }
        for (uint32_t c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (uint32_t dy = 0; dy < 2; ++dy) for (uint32_t dx = 0; dx < 2; ++dx) {
                const uint8_t v = at(x, y, dx, dy)[c];
                sum += srgb && c < 3 ? Linear(v) : v / 255.0f;
            }
            d[c] = srgb && c < 3 ? Gamma(sum * 0.25f) : Unorm(sum * 0.25f);
        }
    }
    });
    return next;
}

std::vector<uint8_t> UpscaleNormalMap2x(uint32_t w, uint32_t h, std::span<const uint8_t> rgba, float sharpen) {
    std::vector<float> x(size_t(w) * h), y(x.size()), z(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        x[i] = rgba[i * 4 + 3] / 255.0f * 2.0f - 1.0f;
        y[i] = rgba[i * 4 + 1] / 255.0f * 2.0f - 1.0f;
        z[i] = std::sqrt(std::max(0.0f, 1.0f - x[i] * x[i] - y[i] * y[i]));
    }
    x = Channel2x(x, w, h, sharpen);
    y = Channel2x(y, w, h, sharpen);
    z = Channel2x(z, w, h, sharpen);
    std::vector<uint8_t> out(x.size() * 4);
    const uint8_t r = rgba.empty() ? 132 : rgba[0], b = rgba.empty() ? 132 : rgba[2];
    for (size_t i = 0; i < x.size(); ++i) {
        const float nz = std::max(z[i], 0.05f);
        const float len = std::sqrt(x[i] * x[i] + y[i] * y[i] + nz * nz);
        out[i * 4] = r;
        out[i * 4 + 1] = Unorm(y[i] / len * 0.5f + 0.5f);
        out[i * 4 + 2] = b;
        out[i * 4 + 3] = Unorm(x[i] / len * 0.5f + 0.5f);
    }
    return out;
}

std::vector<uint8_t> UpscaleMaterialMap2x(uint32_t w, uint32_t h, std::span<const uint8_t> rgba, float sharpen) {
    std::vector<uint8_t> out(size_t(w) * h * 16);
    std::vector<float> c(size_t(w) * h);
    for (uint32_t channel = 0; channel < 4; ++channel) {
        for (size_t i = 0; i < c.size(); ++i) c[i] = rgba[i * 4 + channel] / 255.0f;
        const std::vector<float> up = Channel2x(c, w, h, channel < 3 ? sharpen : 0.0f);
        for (size_t i = 0; i < up.size(); ++i) out[i * 4 + channel] = Unorm(up[i]);
    }
    return out;
}

void UpscaleAlpha2x(uint32_t w, uint32_t h, std::span<const uint8_t> source, std::span<uint8_t> out) {
    auto tap = [](uint32_t i, uint32_t n, uint32_t& a, uint32_t& b) {
        const uint32_t k = i / 2;
        if (i & 1) { a = k; b = (k + 1) % n; } else { a = (k + n - 1) % n; b = k; }
        return (i & 1) ? 0.25f : 0.75f;
    };
    const uint32_t w2 = w * 2, h2 = h * 2;
    for (uint32_t y = 0; y < h2; ++y) {
        uint32_t y0, y1;
        const float fy = tap(y, h, y0, y1);
        for (uint32_t x = 0; x < w2; ++x) {
            uint32_t x0, x1;
            const float fx = tap(x, w, x0, x1);
            auto a = [&](uint32_t sx, uint32_t sy) { return source[(size_t(sy) * w + sx) * 4 + 3] / 255.0f; };
            const float top = a(x0, y0) + fx * (a(x1, y0) - a(x0, y0));
            const float bottom = a(x0, y1) + fx * (a(x1, y1) - a(x0, y1));
            out[(size_t(y) * w2 + x) * 4 + 3] = Unorm(top + fy * (bottom - top));
        }
    }
}

bool EncodeEnhancedTexture(EnhancedKind kind, uint32_t w, uint32_t h, std::vector<uint8_t> pixels, const FtexTexture& source, FtexTexture& out,
                           unsigned threads) {
    if (kind == EnhancedKind::None || !w || !h || w > kMaxSide || h > kMaxSide || pixels.size() != size_t(w) * h * 4) return false;
    if (kind == EnhancedKind::Colour) return EncodeEnhancedTexture(w, h, pixels, source.Srgb(), out, threads);
    InitEncoders();
    const uint32_t format = kind == EnhancedKind::Material ? 2 : 4;
    const bool srgb = source.Srgb();
    out = {};
    out.width = w; out.height = h; out.faces = 1; out.depth = 1; out.flags = srgb ? 2 : 0;
    out.pixel_format = static_cast<uint16_t>(format); out.mip_count = Levels(w, h);
    for (uint32_t level = 0; level < out.mip_count; ++level) {
        if (kind == EnhancedKind::ColourAlpha) {
            for (uint32_t s = 0; s < source.mip_count; ++s) {
                std::vector<uint8_t> stored;
                if (source.MipWidth(s) != w || source.MipHeight(s) != h) continue;
                if (DecodeFtexLevel(source, s, stored) && stored.size() == pixels.size())
                    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = stored[i];
                break;
            }
        }
        auto& mip = out.mips.emplace_back(Bytes(w, h, format));
        const auto block_start = ProfileClock::now();
        EncodeLevel(format, w, h, pixels.data(), mip.data(), nullptr, threads);
        EncodeProfileTotals().blocks_ns += ProfileNs(block_start);
        if (w == 1 && h == 1) break;
        const auto mip_start = ProfileClock::now();
        pixels = ReduceEnhancedLevel(kind, srgb, w, h, pixels, threads);
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
        EncodeProfileTotals().mips_ns += ProfileNs(mip_start);
    }
    return true;
}

bool WriteTextureCache(const std::filesystem::path& path, uint64_t source, const FtexTexture& t) {
    if (!t.width || !t.height || t.width > kMaxSide || t.height > kMaxSide || !CacheFormat(t.pixel_format) ||
        t.faces != 1 || t.depth != 1 || t.mips.size() != Levels(t.width, t.height)) return false;
    const auto temp = std::filesystem::path(path.wstring() + L".partial");
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    const uint32_t version = t.pixel_format == 8 ? 1 : 2;
    const uint32_t flags = (t.flags & 2) | (version == 2 ? uint32_t(t.pixel_format) << 8 : 0);
    const uint32_t header[] = {kMagic, version, t.width, t.height, flags, static_cast<uint32_t>(t.mips.size())};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&source), sizeof(source));
    uint32_t w = t.width, h = t.height;
    for (const auto& mip : t.mips) {
        if (mip.size() != Bytes(w, h, t.pixel_format)) { file.close(); std::filesystem::remove(temp); return false; }
        const uint32_t crc = crc32(0, mip.data(), static_cast<uInt>(mip.size()));
        file.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
        file.write(reinterpret_cast<const char*>(mip.data()), mip.size());
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    file.flush();
    const bool ok = file.good();
    if (!ok) LogWarn("texture cache: {} could not be written (disk full or file locked)", os::PathToUtf8(path.filename()));
    file.close();
    std::error_code ec;
    if (!ok) { std::filesystem::remove(temp, ec); return false; }
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(attempt < 8 ? 50 * attempt : 250));
        std::filesystem::remove(path, ec);
        ec.clear(); std::filesystem::rename(temp, path, ec);
        if (!ec) return true;
    }
    LogWarn("texture cache: {} could not be moved into place: {}", os::PathToUtf8(path.filename()), ec.message());
    std::filesystem::remove(temp, ec);
    return false;
}

bool ReadTextureCache(const std::filesystem::path& path, uint64_t source, FtexTexture& t) {
    std::ifstream file(path, std::ios::binary);
    uint32_t header[6]{}; uint64_t stored_source = 0;
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    file.read(reinterpret_cast<char*>(&stored_source), sizeof(stored_source));
    const uint32_t w0 = header[2], h0 = header[3];
    const uint32_t format = header[1] == 1 ? 8 : (header[4] >> 8) & 0xff;
    const uint32_t flags = header[1] == 1 ? header[4] : header[4] & 0xff;
    if (!file || header[0] != kMagic || (header[1] != 1 && header[1] != 2) || stored_source != source || !w0 || !h0 ||
        w0 > kMaxSide || h0 > kMaxSide || (flags & ~2u) || (header[1] == 2 && (header[4] >> 16)) || !CacheFormat(format) ||
        header[5] != Levels(w0, h0)) return false;
    FtexTexture result;
    result.width = w0; result.height = h0; result.flags = flags; result.pixel_format = static_cast<uint16_t>(format); result.mip_count = header[5];
    uint32_t w = w0, h = h0;
    for (uint32_t i = 0; i < result.mip_count; ++i) {
        uint32_t crc = 0;
        file.read(reinterpret_cast<char*>(&crc), sizeof(crc));
        auto& mip = result.mips.emplace_back(Bytes(w, h, format));
        file.read(reinterpret_cast<char*>(mip.data()), mip.size());
        if (!file || crc != crc32(0, mip.data(), static_cast<uInt>(mip.size()))) return false;
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    if (file.peek() != std::char_traits<char>::eof()) return false;
    t = std::move(result);
    return true;
}

bool TextureCacheCurrent(const std::filesystem::path& path, uint64_t source) {
    std::ifstream file(path, std::ios::binary);
    uint32_t header[6]{}; uint64_t stored_source = 0;
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    file.read(reinterpret_cast<char*>(&stored_source), sizeof(stored_source));
    const uint32_t w0 = header[2], h0 = header[3];
    const uint32_t format = header[1] == 1 ? 8 : (header[4] >> 8) & 0xff;
    if (!file || header[0] != kMagic || (header[1] != 1 && header[1] != 2) || stored_source != source || !w0 || !h0 ||
        w0 > kMaxSide || h0 > kMaxSide || !CacheFormat(format) || header[5] != Levels(w0, h0)) return false;
    uint64_t size = sizeof(header) + sizeof(stored_source);
    for (uint32_t i = 0, w = w0, h = h0; i < header[5]; ++i, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) size += 4 + Bytes(w, h, format);
    std::error_code ec;
    return std::filesystem::file_size(path, ec) == size && !ec;
}
}
