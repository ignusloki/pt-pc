#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <vector>

namespace pt {

class FilePrefetch {
public:
    ~FilePrefetch();
    void Start(std::vector<std::filesystem::path> files);
    void Cancel();
    void Join();
    bool Done() const { return done_; }
    uint64_t Bytes() const { return bytes_; }

private:
    void Run(std::vector<std::filesystem::path> files);
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> done_{false};
    std::atomic<uint64_t> bytes_{0};
};

}
