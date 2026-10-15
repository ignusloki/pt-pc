#include "engine/assets/enhanced_textures.h"
#include "engine/core/log.h"
#include "engine/core/pathcode.h"
#include "engine/platform/os.h"
#define STBI_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace pt {
namespace {
constexpr std::string_view kModel = "realesr-general-x4v3-x2";
constexpr int kCacheVersion = 3;
constexpr uint32_t kFilterVersion[5] = {0, 0, 2, 1, 1};
constexpr float kNormalSharpen = 0.8f;
constexpr float kMaterialSharpen = 0.5f;
constexpr float kColourAlphaSharpen = 0.5f;
unsigned EncodeThreads() { return std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u); }
uint64_t Hash(std::span<const uint8_t> bytes, uint64_t hash = 14695981039346656037ull) {
    for (uint8_t byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
std::vector<uint8_t> Read(const std::filesystem::path& path, size_t max = 64 * 1024 * 1024) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size < 0 || static_cast<uint64_t>(size) > max) throw std::runtime_error("Cannot read texture source or model.");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!file) throw std::runtime_error("Incomplete texture source or model.");
    return bytes;
}
std::vector<std::string> TexturePaths(const std::filesystem::path& game) {
    const auto bytes = Read(game / "pathid_list_ps4.bin");
    auto u32 = [&](size_t at) { uint32_t v; if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("Invalid texture name table.");
        std::memcpy(&v, bytes.data() + at, 4); return v; };
    auto align = [](size_t n) { return (n + 15) & ~size_t(15); };
    const size_t count = u32(4), dirs = u32(8), names = u32(12);
    if (count > 1000000 || dirs > 1000000 || names > 1000000) throw std::runtime_error("Invalid texture name table.");
    const size_t records = align(32 + 8 * count), dir_table = align(records + 4 * count);
    const size_t name_table = align(dir_table + 4 * dirs), strings = align(name_table + 4 * names);
    auto string = [&](size_t offset) {
        const size_t start = strings + offset;
        if (start >= bytes.size()) throw std::runtime_error("Invalid texture path.");
        const auto end = std::find(bytes.begin() + start, bytes.end(), 0);
        if (end == bytes.end()) throw std::runtime_error("Unterminated texture path.");
        return std::string(bytes.begin() + start, end);
    };
    std::vector<std::string> paths;
    for (size_t i = 0; i < count; ++i) {
        const size_t code_at = 32 + i * 8;
        if (code_at > bytes.size() || bytes.size() - code_at < 8) throw std::runtime_error("Invalid texture code table.");
        uint64_t code;
        std::memcpy(&code, bytes.data() + code_at, 8);
        if ((code >> 51) != ExtensionType("ftex")) continue;
        const uint32_t record = u32(records + i * 4), dir = record >> 20, name = record & 0xffff;
        if (dir >= dirs || name >= names) throw std::runtime_error("Invalid texture path index.");
        std::string path = string(u32(dir_table + dir * 4)) + "/" + string(u32(name_table + name * 4));
        if (path.starts_with("/app0/as/")) path = "/Assets/" + path.substr(9);
        paths.push_back(FtexStem(path));
    }
    std::sort(paths.begin(), paths.end()); paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}
void WritePngRgb(const std::filesystem::path& path, uint32_t w, uint32_t h, std::span<const uint8_t> rgba) {
    const size_t stride = size_t(w) * 3 + 1;
    std::vector<uint8_t> raw(stride * h);
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* row = raw.data() + y * stride;
        row[0] = 0;
        const uint8_t* src = rgba.data() + size_t(y) * w * 4;
        for (uint32_t x = 0; x < w; ++x) std::memcpy(row + 1 + size_t(x) * 3, src + size_t(x) * 4, 3);
    }
    uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> packed(packed_size);
    if (compress2(packed.data(), &packed_size, raw.data(), static_cast<uLong>(raw.size()), 1) != Z_OK) throw std::runtime_error("Cannot pack temporary texture.");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    auto be32 = [](uint32_t v) { const uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; return std::array<uint8_t, 4>{b[0], b[1], b[2], b[3]}; };
    auto chunk = [&](const char* type, const uint8_t* data, uint32_t size) {
        const auto length = be32(size);
        file.write(reinterpret_cast<const char*>(length.data()), 4);
        file.write(type, 4);
        if (size) file.write(reinterpret_cast<const char*>(data), size);
        uLong crc = crc32(0, reinterpret_cast<const Bytef*>(type), 4);
        if (size) crc = crc32(crc, data, size);
        const auto sum = be32(static_cast<uint32_t>(crc));
        file.write(reinterpret_cast<const char*>(sum.data()), 4);
    };
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    file.write(reinterpret_cast<const char*>(signature), 8);
    uint8_t header[13];
    std::memcpy(header, be32(w).data(), 4);
    std::memcpy(header + 4, be32(h).data(), 4);
    header[8] = 8; header[9] = 2; header[10] = 0; header[11] = 0; header[12] = 0;
    chunk("IHDR", header, 13);
    chunk("IDAT", packed.data(), static_cast<uint32_t>(packed_size));
    chunk("IEND", nullptr, 0);
    file.flush();
    if (!file) throw std::runtime_error("Cannot write temporary texture (check disk space).");
}
#ifdef _WIN32
constexpr const char* kUpscalerExe = "realesrgan-ncnn-vulkan.exe";
#else
constexpr const char* kUpscalerExe = "realesrgan-ncnn-vulkan";
#endif
std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
std::filesystem::path UpscalerFolder(const std::filesystem::path& runtime) {
    constexpr size_t kUpscalerBuffer = 256, kProgramPath = 250;
    const size_t name = std::string(kModel).size() + 8;
    auto fits = [&](const std::filesystem::path& dir) {
        return (dir / "models").native().size() + name < kUpscalerBuffer && (dir / kUpscalerExe).native().size() < kProgramPath;
    };
    if (fits(runtime)) return runtime;
    std::error_code ec;
    const std::filesystem::path copy = std::filesystem::temp_directory_path(ec) / "pt-texture-tools";
    if (ec || !fits(copy)) return runtime;
    bool copied = false;
    for (std::filesystem::recursive_directory_iterator it(runtime, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path to = copy / it->path().lexically_relative(runtime);
        std::error_code size_error;
        if (std::filesystem::file_size(it->path(), size_error) == std::filesystem::file_size(to, size_error) && !size_error) continue;
        std::filesystem::create_directories(to.parent_path(), size_error);
        std::filesystem::copy_file(it->path(), to, std::filesystem::copy_options::overwrite_existing, size_error);
        copied = true;
    }
    if (copied) LogInfo("enhanced textures: the game folder is too long for the upscaler; it runs from a copy in {}", Utf8(copy));
    return copy;
}
using Clock = std::chrono::steady_clock;
int64_t NsSince(Clock::time_point from) { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - from).count(); }

std::vector<uint8_t> BoxReduceColour(const std::vector<uint8_t>& src, int w, int h, bool srgb, unsigned threads) {
    static const std::array<float, 256> kPower = [] {
        std::array<float, 256> t{};
        for (size_t i = 0; i < t.size(); ++i) t[i] = std::pow(static_cast<uint8_t>(i) / 255.0f, 2.2f);
        return t;
    }();
    const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
    std::vector<uint8_t> half(size_t(nw) * nh * 4);
    ParallelRanges(static_cast<size_t>(nh), size_t(nw) * nh < 128 * 128 ? 1u : threads, [&](size_t first, size_t last) {
        for (int y = static_cast<int>(first); y < static_cast<int>(last); ++y) for (int x = 0; x < nw; ++x) for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                const uint8_t v = src[(size_t(std::min(y * 2 + dy, h - 1)) * w + std::min(x * 2 + dx, w - 1)) * 4 + c];
                sum += srgb && c < 3 ? kPower[v] : v / 255.0f;
            }
            const float mean = sum * 0.25f;
            half[(size_t(y) * nw + x) * 4 + c] =
                static_cast<uint8_t>(std::lround(std::clamp(srgb && c < 3 ? std::pow(mean, 1.0f / 2.2f) : mean, 0.0f, 1.0f) * 255.0f));
        }
    });
    return half;
}

class Generation {
public:
    struct Item { std::string path; EnhancedKind kind = EnhancedKind::None; uint32_t width = 0, height = 0; };
    struct Timing {
        std::atomic<int64_t> load{0}, input_png{0}, output_png{0}, reduce{0}, encode{0}, write{0}, filter{0};
        std::atomic<int64_t> upscaler_wall{0};
        std::atomic<uint32_t> batches{0};
    };
    Generation(const QarArchive& qar, std::filesystem::path cache, std::filesystem::path runtime, std::filesystem::path temporary, uint64_t model,
               uint32_t max_output, const std::atomic<bool>& cancel, const std::atomic<bool>& paused,
               std::function<bool(const std::string&)> is_priority, std::function<void(uint32_t)> progress)
        : qar_(qar), cache_(std::move(cache)), runtime_(std::move(runtime)), temporary_(std::move(temporary)), model_(model),
          max_output_(max_output), cancel_(cancel), paused_(paused), is_priority_(std::move(is_priority)), progress_(std::move(progress)) {}

    void Add(Item item) { items_.push_back(std::move(item)); }
    uint32_t Generated() const { return generated_; }
    uint32_t Done() const { return done_; }
    double FirstSeconds() const { return first_seconds_; }
    const Timing& Times() const { return times_; }

    void Run() {
        started_ = Clock::now();
        colour_.reset(new ColourWork[items_.size()]);
        for (size_t i = 0; i < items_.size(); ++i) (items_[i].kind == EnhancedKind::Colour ? colour_pending_ : filtered_).push_back(i);
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        hardware_ = hw;
        const unsigned count = std::clamp(hw > 3 ? hw - 2 : 1u, 1u, 12u);
        for (unsigned t = 0; t < count; ++t) pool_.emplace_back([this] { Worker(); });
        try {
            Feed();
            while (!Stopping() && done_ < items_.size()) { Tick(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
        } catch (const std::exception& e) {
            Fail(e.what());
        }
        {
            std::lock_guard lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& worker : pool_) worker.join();
        pool_.clear();
        Tick();
        if (failed_) throw std::runtime_error(error_);
    }

private:
    struct ColourWork {
        std::filesystem::path in, out;
        std::atomic<int> state{0};
        uint64_t input_rgb = 0, key = 0, input_bytes = 0;
        bool srgb = true;
    };
    enum class Type { None, Prep, Post, Filter };
    struct Task { Type type = Type::None; size_t item = 0; uint64_t cost = 0; unsigned threads = 1; };
    static constexpr uint64_t kMemoryBudget = 3ull << 30;

    bool Stopping() const { return cancel_ || failed_; }
    void Fail(const std::string& what) {
        std::lock_guard lock(mu_);
        if (!failed_) { error_ = what; failed_ = true; }
        cv_.notify_all();
    }
    void Tick() {
        const uint32_t done = done_;
        if (done != reported_) { reported_ = done; progress_(done); }
    }
    void Finish(bool made) {
        if (made) ++generated_;
        if (!first_seconds_set_.exchange(true)) first_seconds_ = std::chrono::duration<double>(Clock::now() - started_).count();
        ++done_;
        cv_.notify_all();
    }
    uint64_t Cost(Type type, size_t i) const {
        const uint64_t pixels = uint64_t(items_[i].width) * items_[i].height;
        return type == Type::Prep ? pixels * 8 : type == Type::Post ? pixels * 80 : pixels * 128;
    }
    bool Priority(size_t i) const { return is_priority_ && is_priority_(items_[i].path); }

    Task Next() {
        if (paused_ || Stopping()) return {};
        auto fits = [&](uint64_t cost) { return running_ == 0 || memory_ + cost <= kMemoryBudget; };
        if (!post_.empty()) {
            const size_t i = post_.front();
            const uint64_t cost = Cost(Type::Post, i);
            if (!fits(cost)) return {};
            post_.pop_front();
            return {Type::Post, i, cost};
        }
        if (!prep_.empty()) {
            const size_t i = prep_.front();
            const uint64_t cost = Cost(Type::Prep, i);
            if (fits(cost)) { prep_.pop_front(); return {Type::Prep, i, cost}; }
        }
        if (!filtered_.empty()) {
            size_t pick = 0;
            for (size_t k = 0; k < filtered_.size(); ++k) if (Priority(filtered_[k])) { pick = k; break; }
            const size_t i = filtered_[pick];
            const uint64_t cost = Cost(Type::Filter, i);
            if (!fits(cost)) return {};
            filtered_.erase(filtered_.begin() + static_cast<std::ptrdiff_t>(pick));
            return {Type::Filter, i, cost};
        }
        return {};
    }
    void Worker() {
        os::LowerCurrentThreadPriority();
        std::unique_lock lock(mu_);
        while (!stop_ && !Stopping()) {
            Task task = Next();
            if (task.type == Type::None) { cv_.wait_for(lock, std::chrono::milliseconds(50)); continue; }
            ++running_;
            memory_ += task.cost;
            task.threads = task.type == Type::Prep ? 1u : std::max(1u, hardware_ / running_);
            lock.unlock();
            try {
                if (task.type == Type::Prep) Prepare(task.item);
                else if (task.type == Type::Post) Finalise(task.item, task.threads);
                else MakeFiltered(task.item, task.threads);
            } catch (const std::exception& e) { Fail(e.what()); }
            lock.lock();
            --running_;
            memory_ -= task.cost;
            cv_.notify_all();
        }
    }

    void CheckSpace() const {
        if (std::filesystem::space(cache_).available < 256 * 1024 * 1024ull) throw std::runtime_error("Not enough free space for enhanced textures.");
    }

    void MakeFiltered(size_t index, unsigned threads) {
        const Item& item = items_[index];
        auto step = Clock::now();
        auto lap = [&](std::atomic<int64_t>& into) { const auto now = Clock::now(); into += std::chrono::duration_cast<std::chrono::nanoseconds>(now - step).count(); step = now; };
        FtexTexture source, cached;
        if (!LoadFtex(qar_, item.path, source)) throw std::runtime_error("A texture could not be decoded.");
        const EnhancedKind kind = EnhancedTextureKindOf(item.path, source);
        const uint64_t key = EnhancedTextureKey(source, model_, kind);
        const auto file = EnhancedCacheFile(cache_, item.path);
        if (TextureCacheCurrent(file, key)) { Finish(false); return; }
        std::vector<uint8_t> pixels;
        if (!DecodeFtexLevel(source, 0, pixels)) throw std::runtime_error("A top-level texture could not be decoded.");
        CheckSpace();
        lap(times_.load);
        uint32_t w = source.width * 2, h = source.height * 2;
        std::vector<uint8_t> result;
        if (kind == EnhancedKind::Normal) {
            result = UpscaleNormalMap2x(source.width, source.height, pixels, kNormalSharpen);
        } else if (kind == EnhancedKind::Material) {
            result = UpscaleMaterialMap2x(source.width, source.height, pixels, kMaterialSharpen);
        } else {
            result = UpscaleMaterialMap2x(source.width, source.height, pixels, kColourAlphaSharpen);
            UpscaleAlpha2x(source.width, source.height, pixels, result);
        }
        lap(times_.filter);
        while (max_output_ && (w > max_output_ || h > max_output_)) {
            result = ReduceEnhancedLevel(kind, source.Srgb(), w, h, result, threads);
            w = std::max(1u, w / 2); h = std::max(1u, h / 2);
        }
        lap(times_.reduce);
        if (!EncodeEnhancedTexture(kind, w, h, std::move(result), source, cached, threads))
            throw std::runtime_error("An enhanced texture could not be encoded.");
        lap(times_.encode);
        if (cancel_) return;
        if (!WriteTextureCache(file, key, cached)) throw std::runtime_error("Cannot save enhanced textures (check disk space).");
        lap(times_.write);
        Finish(true);
    }

    void Prepare(size_t index) {
        const Item& item = items_[index];
        ColourWork& work = colour_[index];
        auto step = Clock::now();
        FtexTexture source;
        if (!LoadFtex(qar_, item.path, source)) throw std::runtime_error("A texture could not be decoded.");
        const EnhancedKind kind = EnhancedTextureKindOf(item.path, source);
        work.key = EnhancedTextureKey(source, model_, kind);
        if (TextureCacheCurrent(EnhancedCacheFile(cache_, item.path), work.key)) { work.state = 2; Finish(false); return; }
        std::vector<uint8_t> pixels;
        if (!DecodeFtexLevel(source, 0, pixels)) throw std::runtime_error("A top-level texture could not be decoded.");
        bool opaque = true;
        for (size_t i = 3; i < pixels.size(); i += 4) if (pixels[i] != 255) { opaque = false; break; }
        if (!opaque) { work.state = 2; Finish(false); return; }
        CheckSpace();
        uint64_t input_rgb = 0;
        for (size_t i = 0; i < pixels.size(); ++i) if ((i & 3) != 3) input_rgb += pixels[i];
        work.input_rgb = input_rgb;
        work.input_bytes = pixels.size();
        work.srgb = source.Srgb();
        times_.load += NsSince(step);
        step = Clock::now();
        WritePngRgb(work.in, source.width, source.height, pixels);
        times_.input_png += NsSince(step);
        work.state = 1;
        cv_.notify_all();
    }

    void Finalise(size_t index, unsigned threads) {
        const Item& item = items_[index];
        ColourWork& work = colour_[index];
        auto step = Clock::now();
        auto lap = [&](std::atomic<int64_t>& into) { const auto now = Clock::now(); into += std::chrono::duration_cast<std::chrono::nanoseconds>(now - step).count(); step = now; };
        const auto bytes = Read(work.out, 128 * 1024 * 1024);
        int w = 0, h = 0, channels = 0;
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels) ||
            w != static_cast<int>(item.width * 2) || h != static_cast<int>(item.height * 2))
            throw std::runtime_error("Texture upscaler returned an invalid size.");
        stbi_uc* image = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
        if (!image) throw std::runtime_error("Texture upscaler output could not be read.");
        const bool dimensions = w == static_cast<int>(item.width * 2) && h == static_cast<int>(item.height * 2);
        uint64_t output_rgb = 0;
        if (dimensions) for (size_t i = 0; i < size_t(w) * h * 4; ++i) if ((i & 3) != 3) output_rgb += image[i];
        const bool black_output = dimensions && work.input_rgb > work.input_bytes * 4 && output_rgb == 0;
        std::vector<uint8_t> result(image, image + size_t(w) * h * 4);
        stbi_image_free(image);
        lap(times_.output_png);
        while (dimensions && max_output_ && (static_cast<uint32_t>(w) > max_output_ || static_cast<uint32_t>(h) > max_output_)) {
            result = BoxReduceColour(result, w, h, work.srgb, threads);
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
        lap(times_.reduce);
        FtexTexture cached;
        const bool encoded = dimensions && !black_output && EncodeEnhancedTexture(w, h, result, work.srgb, cached, threads);
        if (!encoded) throw std::runtime_error("Texture upscaler returned invalid pixels or size.");
        lap(times_.encode);
        if (cancel_) return;
        if (!WriteTextureCache(EnhancedCacheFile(cache_, item.path), work.key, cached)) throw std::runtime_error("Cannot save enhanced textures (check disk space).");
        std::error_code ec;
        std::filesystem::remove(work.in, ec);
        std::filesystem::remove(work.out, ec);
        lap(times_.write);
        Finish(true);
    }

    std::vector<size_t> TakeBatch(size_t count) {
        std::vector<size_t> batch;
        std::lock_guard lock(mu_);
        while (batch.size() < count && !colour_pending_.empty()) {
            size_t pick = 0;
            for (size_t k = 0; k < colour_pending_.size(); ++k) if (Priority(colour_pending_[k])) { pick = k; break; }
            batch.push_back(colour_pending_[pick]);
            colour_pending_.erase(colour_pending_.begin() + static_cast<std::ptrdiff_t>(pick));
        }
        if (batch.empty()) return batch;
        const auto dir = temporary_ / ("b" + std::to_string(next_batch_number_++));
        std::error_code ec;
        std::filesystem::create_directories(dir / "in", ec);
        std::filesystem::create_directories(dir / "out", ec);
        for (size_t k = 0; k < batch.size(); ++k) {
            ColourWork& work = colour_[batch[k]];
            work.state = 0;
            work.in = dir / "in" / (std::to_string(k) + ".png");
            work.out = dir / "out" / (std::to_string(k) + ".png");
            prep_.push_back(batch[k]);
        }
        cv_.notify_all();
        return batch;
    }

    void Feed() {
        static constexpr size_t kBatches[] = {2, 6};
        constexpr size_t kBatchSize = 16;
        const std::filesystem::path folder = UpscalerFolder(runtime_);
        size_t number = 0;
        auto size_of = [&](size_t n) { return n < std::size(kBatches) ? kBatches[n] : kBatchSize; };
        std::vector<size_t> next = TakeBatch(size_of(number));
        while (!Stopping()) {
            std::vector<size_t> current = std::move(next);
            if (current.empty()) {
                next = TakeBatch(size_of(number));
                if (next.empty()) break;
                continue;
            }
            for (;;) {
                {
                    std::unique_lock lock(mu_);
                    bool ready = true;
                    for (const size_t i : current) if (colour_[i].state == 0) { ready = false; break; }
                    if (ready || Stopping()) break;
                    cv_.wait_for(lock, std::chrono::milliseconds(50));
                }
                Tick();
            }
            if (Stopping()) break;
            std::vector<size_t> run;
            for (const size_t i : current) if (colour_[i].state == 1) run.push_back(i);
            ++number;
            next = TakeBatch(size_of(number));
            if (run.empty()) continue;
            while (paused_ && !Stopping()) { Tick(); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
            if (Stopping()) break;
            RunUpscaler(folder, run);
        }
    }

    void RunUpscaler(const std::filesystem::path& folder, const std::vector<size_t>& run) {
        const auto batch_dir = colour_[run.front()].in.parent_path().parent_path();
        const auto log = cache_ / "upscaler.log";
        std::atomic<bool> stop{false};
        os::ProcessResult result;
        { std::ofstream clear(log, std::ios::binary | std::ios::trunc); }
        const auto started = Clock::now();
        std::atomic<bool> ended{false};
        std::thread process([&] {
            result = os::RunProcess(folder / kUpscalerExe,
                {"-i", Utf8(os::ExtendedLengthPath(batch_dir / "in")), "-o", Utf8(os::ExtendedLengthPath(batch_dir / "out")), "-m",
                 Utf8(folder / "models"), "-n", "realesr-general-x4v3-x2", "-s", "2", "-t", "128", "-j", "2:2:2", "-f", "png", "-v"},
                folder, log, stop, std::chrono::minutes(15));
            ended = true;
        });
        size_t offset = 0;
        std::string pending;
        bool paused_stop = false;
        auto poll = [&] {
            std::ifstream file(log, std::ios::binary);
            if (!file) return;
            file.seekg(static_cast<std::streamoff>(offset));
            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            offset += text.size();
            pending += text;
            size_t line_end;
            while ((line_end = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, line_end);
                pending.erase(0, line_end + 1);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                const std::string tail = " done";
                const size_t arrow = line.find(" -> ");
                if (arrow == std::string::npos || line.size() < tail.size() || line.compare(line.size() - tail.size(), tail.size(), tail) != 0) continue;
                const std::string out = line.substr(arrow + 4, line.size() - tail.size() - arrow - 4);
                const size_t slash = out.find_last_of("/\\"), dot = out.find_last_of('.');
                const size_t first = slash == std::string::npos ? 0 : slash + 1;
                if (dot == std::string::npos || dot <= first) continue;
                const std::string number = out.substr(first, dot - first);
                if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos) continue;
                const size_t k = std::stoul(number);
                for (const size_t i : run) {
                    if (colour_[i].out.filename() != std::to_string(k) + ".png") continue;
                    int expected = 1;
                    if (colour_[i].state.compare_exchange_strong(expected, 3)) {
                        std::lock_guard lock(mu_);
                        post_.push_back(i);
                        cv_.notify_all();
                    }
                    break;
                }
            }
        };
        while (!ended) {
            if (!stop && (cancel_ || failed_ || paused_)) { paused_stop = paused_ && !cancel_ && !failed_; stop = true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            poll();
            Tick();
        }
        process.join();
        poll();
        times_.upscaler_wall += NsSince(started);
        ++times_.batches;
        if (!stop && (!result.started || result.exit_code != 0 || result.timed_out)) {
            if (!result.started) throw std::runtime_error("Cannot start the bundled texture upscaler.");
            if (result.timed_out) throw std::runtime_error("Texture upscaler timed out; original textures remain active.");
            throw std::runtime_error("Texture upscaler failed; see enhanced-textures/upscaler.log.");
        }
        for (const size_t i : run) {
            if (colour_[i].state != 1) continue;
            if (!paused_stop) {
                if (Stopping()) return;
                throw std::runtime_error("Texture upscaler failed; see enhanced-textures/upscaler.log.");
            }
            colour_[i].state = 0;
            std::lock_guard lock(mu_);
            colour_pending_.push_back(i);
        }
    }

    const QarArchive& qar_;
    std::filesystem::path cache_, runtime_, temporary_;
    uint64_t model_ = 0;
    uint32_t max_output_ = 0;
    const std::atomic<bool>& cancel_;
    const std::atomic<bool>& paused_;
    std::function<bool(const std::string&)> is_priority_;
    std::function<void(uint32_t)> progress_;
    std::vector<Item> items_;
    std::unique_ptr<ColourWork[]> colour_;
    std::vector<std::thread> pool_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<size_t> colour_pending_, filtered_;
    std::deque<size_t> prep_, post_;
    unsigned running_ = 0, hardware_ = 1;
    uint64_t memory_ = 0;
    bool stop_ = false;
    size_t next_batch_number_ = 0;
    std::atomic<bool> failed_{false};
    std::string error_;
    std::atomic<uint32_t> done_{0}, generated_{0};
    uint32_t reported_ = 0;
    Clock::time_point started_;
    std::atomic<bool> first_seconds_set_{false};
    std::atomic<double> first_seconds_{-1.0};
    Timing times_;
};
}

std::vector<std::string> EnhancedTextureCandidates(const std::filesystem::path& game) {
    std::vector<std::string> named;
    for (const auto& path : TexturePaths(game)) {
        if (path.ends_with("_bsm") || path.ends_with("_lym") || path.ends_with("_ils") || path.ends_with("_bsm_alp") ||
            path.ends_with("_nrm") || path.ends_with("_srm")) named.push_back(path);
    }
    return named;
}
uint64_t EnhancedModelKey(const std::filesystem::path& runtime) {
    try {
        uint64_t key = Hash(Read(runtime / "models" / (std::string(kModel) + ".param")));
        key = Hash(Read(runtime / "models" / (std::string(kModel) + ".bin")), key);
        return Hash(Read(runtime / kUpscalerExe), key);
    } catch (...) { return 0; }
}
uint64_t EnhancedTextureKey(const FtexTexture& t, uint64_t model, EnhancedKind kind) {
    const uint32_t version = kind == EnhancedKind::Colour ? 2u : 0x1000u | (static_cast<uint32_t>(kind) << 4) | kFilterVersion[static_cast<uint32_t>(kind)];
    const uint32_t header[] = {version, t.width, t.height, t.pixel_format, t.flags, t.faces, t.mip_count};
    uint64_t key = Hash({reinterpret_cast<const uint8_t*>(header), sizeof(header)}, model);
    for (const auto& mip : t.mips) key = Hash(mip, key);
    return key;
}
std::filesystem::path EnhancedCacheFile(const std::filesystem::path& root, std::string_view stem) {
    return root / std::format("{:016x}.pttex", PathCode64(std::string(stem) + ".ftex"));
}

bool EnhancedManifestCurrent(const std::filesystem::path& game, const std::filesystem::path& cache, const std::filesystem::path& runtime, uint32_t kinds,
                             uint32_t max_output) {
    std::ifstream in(cache / "manifest.txt");
    if (!in) return false;
    std::map<std::string, std::string> have;
    for (std::string line; std::getline(in, line);) {
        if (const size_t at = line.find('='); at != std::string::npos) have[line.substr(0, at)] = line.substr(at + 1);
    }
    const uint64_t model = EnhancedModelKey(runtime);
    std::error_code ec;
    const std::filesystem::path archive = game / "texture.qar";
    const uintmax_t bytes = std::filesystem::file_size(archive, ec);
    if (ec || !model) return false;
    const auto mtime = static_cast<long long>(std::filesystem::last_write_time(archive, ec).time_since_epoch().count());
    if (ec) return false;
    auto is = [&](const char* key, const std::string& value) {
        const auto it = have.find(key);
        return it != have.end() && it->second == value;
    };
    return is("cache_version", std::to_string(kCacheVersion)) && is("kinds", std::to_string(kinds)) &&
           is("filter_versions", std::format("{},{},{}", kFilterVersion[2], kFilterVersion[3], kFilterVersion[4])) && is("model", std::string(kModel)) &&
           is("model_fingerprint", std::to_string(model)) && is("archive_bytes", std::to_string(bytes)) && is("archive_mtime", std::to_string(mtime)) &&
           is("max_output", std::to_string(max_output));
}

EnhancedTextureJob::~EnhancedTextureJob() { Cancel(); if (worker_.joinable()) worker_.join(); }
void EnhancedTextureJob::Cancel() { cancel_ = true; }
EnhancedTextureJob::Status EnhancedTextureJob::GetStatus() const { std::lock_guard lock(mutex_); return status_; }
void EnhancedTextureJob::Publish(State state, uint32_t done, uint32_t total, std::string note) {
    std::lock_guard lock(mutex_); status_ = {state, done, total, std::move(note)};
}
bool EnhancedTextureJob::Start(const std::filesystem::path& game, const std::filesystem::path& cache,
                               const std::filesystem::path& runtime, uint32_t limit, uint32_t max_output, uint32_t kinds) {
    if (GetStatus().state == State::Running) return false;
    if (worker_.joinable()) worker_.join();
    cancel_ = false; Publish(State::Running, 0, 0, "Checking texture cache...");
    worker_ = std::thread(&EnhancedTextureJob::Run, this, game, cache, runtime, limit, max_output, kinds);
    return true;
}
void EnhancedTextureJob::Prioritise(std::vector<std::string> stems) {
    std::unordered_set<std::string> set(std::make_move_iterator(stems.begin()), std::make_move_iterator(stems.end()));
    std::lock_guard lock(priority_mutex_);
    priority_ = std::move(set);
}
bool EnhancedTextureJob::IsPriority(const std::string& stem) const {
    std::lock_guard lock(priority_mutex_);
    return !priority_.empty() && priority_.contains(stem);
}
void EnhancedTextureJob::Run(std::filesystem::path game, std::filesystem::path cache, std::filesystem::path runtime, uint32_t limit,
                             uint32_t max_output, uint32_t kinds) {
    const auto job_started = Clock::now();
    uint32_t generated = 0;
    std::optional<os::FileLock> lock;
    std::filesystem::path temporary;
    uint32_t done = 0, total = 0;
    try {
        const uint64_t model = EnhancedModelKey(runtime);
        if (!model) throw std::runtime_error("Enhanced texture tools or model are missing.");
        std::filesystem::create_directories(cache);
        lock.emplace(cache / "generation.lock");
        if (!lock->Held()) throw std::runtime_error("Texture generation is already running in another instance.");
        QarArchive qar;
        if (!qar.Open(game / "texture.qar")) throw std::runtime_error("Cannot open the game's texture archive.");
        std::vector<std::string> named = EnhancedTextureCandidates(game);
        struct Item { EnhancedKind kind = EnhancedKind::None; bool current = false; uint32_t width = 0, height = 0; };
        std::vector<Item> items(named.size());
        const auto inventory_started = Clock::now();
        {
            std::atomic<size_t> next{0};
            auto inventory = [&] {
                for (size_t i = next++; i < named.size() && !cancel_; i = next++) {
                    FtexTexture source;
                    const std::string& path = named[i];
                    if (!LoadFtex(qar, path, source) || !EnhancedTextureEligible(path, source, kinds)) continue;
                    const EnhancedKind kind = EnhancedTextureKindOf(path, source);
                    if (kind != EnhancedKind::Colour && max_output && std::max(source.width, source.height) * 2 > max_output) continue;
                    items[i] = {kind, TextureCacheCurrent(EnhancedCacheFile(cache, path), EnhancedTextureKey(source, model, kind)), source.width, source.height};
                }
            };
            std::vector<std::thread> workers;
            for (unsigned t = 1; t < EncodeThreads(); ++t) workers.emplace_back(inventory);
            inventory();
            for (auto& worker : workers) worker.join();
        }
        const double inventory_seconds = std::chrono::duration<double>(Clock::now() - inventory_started).count();
        std::vector<size_t> to_make, filtered;
        uint32_t counts[5] = {};
        for (size_t i = 0; i < named.size(); ++i) {
            if (items[i].kind == EnhancedKind::None) continue;
            ++counts[static_cast<uint32_t>(items[i].kind)];
            ++total;
            if (items[i].current) ++done;
            else (items[i].kind == EnhancedKind::Colour ? to_make : filtered).push_back(i);
        }
        to_make.insert(to_make.end(), filtered.begin(), filtered.end());
        if (limit && done + to_make.size() > limit) {
            to_make.resize(limit > done ? limit - done : 0);
            total = std::min(total, limit);
            done = std::min(done, total);
        }
        LogInfo("enhanced textures: {} colour, {} colour with alpha, {} normal and {} material maps, {} to make", counts[1], counts[2], counts[3],
                counts[4], to_make.size());
        if (!total && !cancel_) throw std::runtime_error("No supported colour textures were found.");
        temporary = cache / ("job-" + std::to_string(os::ProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(temporary);
        const uint32_t done_before = done;
        double first_seconds = -1.0;
        if (!cancel_ && !to_make.empty()) {
            const uint32_t pending = static_cast<uint32_t>(to_make.size());
            Generation generation(qar, cache, runtime, temporary, model, max_output, cancel_, paused_,
                                  [this](const std::string& stem) { return IsPriority(stem); },
                                  [&](uint32_t made) {
                                      Publish(State::Running, done_before + made, total,
                                              std::format("Generating textures: {} / {}. Switch Off to cancel.", done_before + made, total));
                                  });
            for (const size_t i : to_make) generation.Add({named[i], items[i].kind, items[i].width, items[i].height});
            Publish(State::Running, done, total, std::format("Generating textures: {} / {}. Switch Off to cancel.", done, total));
            struct Report {
                static void Log(const Generation& g, double wall, uint32_t pending, double inventory) {
                    const auto& t = g.Times();
                    auto s = [](const std::atomic<int64_t>& ns) { return ns.load() * 1e-9; };
                    const auto& encoder = EncodeProfileTotals();
                    LogInfo("enhanced textures: {} of {} maps in {:.1f} s (inventory {:.1f} s, first map after {:.1f} s); upscaler {} batches, {:.1f} s on the GPU; "
                            "thread seconds: source load {:.1f}, input png {:.1f}, output png {:.1f}, cpu filters {:.1f}, reduce {:.1f}, encode {:.1f} (mips {:.1f}, blocks {:.1f}), write {:.1f}",
                            g.Done(), pending, wall, inventory, g.FirstSeconds(), t.batches.load(), s(t.upscaler_wall), s(t.load), s(t.input_png), s(t.output_png),
                            s(t.filter), s(t.reduce), s(t.encode), encoder.mips_ns.load() * 1e-9, encoder.blocks_ns.load() * 1e-9, s(t.write));
                }
            };
            try {
                generation.Run();
            } catch (...) {
                generated = generation.Generated();
                done = done_before + generation.Done();
                Report::Log(generation, std::chrono::duration<double>(Clock::now() - job_started).count(), pending, inventory_seconds);
                throw;
            }
            generated = generation.Generated();
            done = done_before + generation.Done();
            first_seconds = generation.FirstSeconds();
            Report::Log(generation, std::chrono::duration<double>(Clock::now() - job_started).count(), pending, inventory_seconds);
        }
        (void)first_seconds;
        if (!cancel_) {
            std::ofstream manifest(cache / "manifest.txt", std::ios::trunc);
            manifest << "cache_version=" << kCacheVersion << "\nkinds=" << kinds << "\nfilter_versions=" << kFilterVersion[2] << "," << kFilterVersion[3] << "," << kFilterVersion[4] << "\nmodel=" << kModel << "\nmodel_fingerprint=" << model
                << "\narchive_bytes=" << std::filesystem::file_size(game / "texture.qar")
                << "\narchive_mtime=" << static_cast<long long>(std::filesystem::last_write_time(game / "texture.qar").time_since_epoch().count())
                << "\ntextures=" << total << "\nmax_output=" << max_output << "\n";
            if (!manifest) throw std::runtime_error("Cannot save enhanced texture manifest.");
        }
        Publish(cancel_ ? State::Cancelled : State::Ready, done, total, cancel_ ? "Texture generation cancelled." : "Enhanced textures ready.");
        LogInfo("enhanced textures: {} / {}, {} ({} generated in {:.1f} s, output {})", done, total, cancel_ ? "cancelled" : "ready", generated,
                std::chrono::duration<double>(Clock::now() - job_started).count(),
                max_output ? std::format("capped at {}", max_output) : std::string("2x"));
    } catch (const std::exception& e) { Publish(State::Failed, done, total, e.what()); LogWarn("enhanced textures: {}", e.what()); }
    lock.reset();
    if (!temporary.empty()) { std::error_code ec; std::filesystem::remove_all(temporary, ec); }
}
}
