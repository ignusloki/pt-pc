#pragma once

#include "engine/assets/ftex.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>

namespace pt {
struct EncodeProfile {
    std::atomic<int64_t> mips_ns{0};
    std::atomic<int64_t> blocks_ns{0};
};
EncodeProfile& EncodeProfileTotals();
enum class EnhancedKind : uint8_t { None, Colour, ColourAlpha, Normal, Material };
constexpr uint32_t EnhancedKindBit(EnhancedKind kind) { return 1u << static_cast<uint32_t>(kind); }
constexpr uint32_t kEnhancedColourKinds = EnhancedKindBit(EnhancedKind::Colour);
constexpr uint32_t kEnhancedAllKinds = EnhancedKindBit(EnhancedKind::Colour) | EnhancedKindBit(EnhancedKind::ColourAlpha) |
                                       EnhancedKindBit(EnhancedKind::Normal) | EnhancedKindBit(EnhancedKind::Material);
constexpr uint32_t kEnhancedMaxSource = 2048;
EnhancedKind EnhancedTextureKindOf(std::string_view path, const FtexTexture& source);
bool EnhancedTextureEligible(std::string_view path, const FtexTexture& source, uint32_t kinds = kEnhancedAllKinds);
bool EncodeEnhancedTexture(uint32_t width, uint32_t height, std::span<const uint8_t> rgba, bool srgb, FtexTexture& out, unsigned threads = 1);
bool EncodeEnhancedTexture(EnhancedKind kind, uint32_t width, uint32_t height, std::vector<uint8_t> rgba, const FtexTexture& source,
                           FtexTexture& out, unsigned threads = 1);
std::vector<uint8_t> ReduceEnhancedLevel(EnhancedKind kind, bool srgb, uint32_t width, uint32_t height, std::span<const uint8_t> rgba,
                                         unsigned threads = 1);
void ParallelRanges(size_t count, unsigned threads, const std::function<void(size_t, size_t)>& fn);
std::vector<uint8_t> UpscaleNormalMap2x(uint32_t width, uint32_t height, std::span<const uint8_t> rgba, float sharpen);
std::vector<uint8_t> UpscaleMaterialMap2x(uint32_t width, uint32_t height, std::span<const uint8_t> rgba, float sharpen);
void UpscaleAlpha2x(uint32_t width, uint32_t height, std::span<const uint8_t> source, std::span<uint8_t> out);
bool WriteTextureCache(const std::filesystem::path& path, uint64_t source, const FtexTexture& texture);
bool ReadTextureCache(const std::filesystem::path& path, uint64_t source, FtexTexture& texture);
bool TextureCacheCurrent(const std::filesystem::path& path, uint64_t source);
}
