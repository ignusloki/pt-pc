#include "engine/fs/file_prefetch.h"

#include <chrono>
#include <vector>

#include "engine/core/log.h"
#include "engine/platform/os.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pt {

FilePrefetch::~FilePrefetch() {
    Cancel();
    Join();
}

void FilePrefetch::Start(std::vector<std::filesystem::path> files) {
    if (worker_.joinable()) return;
    cancel_ = false;
    done_ = false;
    worker_ = std::thread(&FilePrefetch::Run, this, std::move(files));
}

void FilePrefetch::Cancel() { cancel_ = true; }

void FilePrefetch::Join() {
    if (worker_.joinable()) worker_.join();
}

void FilePrefetch::Run(std::vector<std::filesystem::path> files) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
    const auto start = std::chrono::steady_clock::now();
    std::vector<char> buffer(4u << 20);
    for (const std::filesystem::path& path : files) {
        if (cancel_) break;
        FILE* file = os::OpenFile(path, "rb");
        if (!file) continue;
        std::setvbuf(file, nullptr, _IONBF, 0);
        size_t read = 0;
        while (!cancel_ && (read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
            bytes_ += read;
        }
        std::fclose(file);
    }
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
#endif
    LogInfo("game files: {:.0f} MB read ahead into the file cache in {:.1f} s{}", static_cast<double>(bytes_) / (1024.0 * 1024.0),
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), cancel_ ? " (stopped)" : "");
    done_ = true;
}

}
