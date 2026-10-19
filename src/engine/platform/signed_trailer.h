#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace pt::trailer {

inline constexpr char kMagic[8] = {'P', 'T', 'S', 'I', 'G', 'N', 'D', '1'};
inline constexpr uint32_t kVersion = 1;
inline constexpr uint64_t kFooterSize = 48;
inline constexpr uint64_t kSignatureSize = 64;
inline constexpr uint64_t kMaxManifest = 16ull * 1024 * 1024;

struct Footer {
    uint64_t manifest_offset = 0, manifest_length = 0, signature_offset = 0, signature_length = 0;
    uint64_t body() const { return manifest_offset; }
    uint64_t first() const { return manifest_offset; }
};

inline std::optional<Footer> Parse(const unsigned char* last, uint64_t file_size) {
    if (file_size < kFooterSize || std::memcmp(last, kMagic, 8) != 0) return std::nullopt;
    auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, last + at, 4); return v; };
    auto u64 = [&](size_t at) { uint64_t v; std::memcpy(&v, last + at, 8); return v; };
    if (u32(8) != kVersion || u32(12) != 0) return std::nullopt;
    Footer f{u64(16), u64(24), u64(32), u64(40)};
    if (f.signature_length != kSignatureSize || f.manifest_length == 0 || f.manifest_length > kMaxManifest) return std::nullopt;
    if (f.manifest_offset > file_size || f.manifest_length > file_size - f.manifest_offset) return std::nullopt;
    if (f.manifest_offset + f.manifest_length != f.signature_offset) return std::nullopt;
    if (f.signature_offset + f.signature_length + kFooterSize != file_size) return std::nullopt;
    return f;
}

inline std::optional<Footer> Read(const std::filesystem::path& file) {
    std::error_code error;
    const uint64_t size = std::filesystem::file_size(file, error);
    if (error || size < kFooterSize) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    unsigned char last[kFooterSize];
    in.seekg(std::streamoff(size - kFooterSize));
    if (!in.read(reinterpret_cast<char*>(last), std::streamsize(kFooterSize))) return std::nullopt;
    return Parse(last, size);
}

inline uint64_t BodySize(const std::filesystem::path& file) {
    if (const auto footer = Read(file)) return footer->body();
    std::error_code error;
    const uint64_t size = std::filesystem::file_size(file, error);
    return error ? 0 : size;
}

inline bool ReadSigned(const std::filesystem::path& file, std::string& manifest, std::string& signature) {
    const auto footer = Read(file);
    if (!footer) return false;
    std::ifstream in(file, std::ios::binary);
    manifest.assign(size_t(footer->manifest_length), '\0');
    signature.assign(size_t(footer->signature_length), '\0');
    in.seekg(std::streamoff(footer->manifest_offset));
    if (!in.read(manifest.data(), std::streamsize(manifest.size()))) return false;
    in.seekg(std::streamoff(footer->signature_offset));
    return bool(in.read(signature.data(), std::streamsize(signature.size())));
}

}
