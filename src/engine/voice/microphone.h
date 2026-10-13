#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace pt {

class Microphone {
public:
    ~Microphone();
    Microphone() = default;
    Microphone(const Microphone&) = delete;
    Microphone& operator=(const Microphone&) = delete;
    bool Open(int sample_rate, const std::string& device_name = {});
    // Opens the device on a worker thread: some systems take 20 s and more to open a recording endpoint, and the game froze
    // for that long on entering f160 and on every close of the pause menu there. Called each frame until it is not Pending.
    enum class AsyncState { Pending, Opened, Failed };
    AsyncState OpenAsync(int sample_rate, const std::string& device_name = {});
    void Close();
    bool IsOpen() const { return stream_ != nullptr; }
    size_t Read(std::vector<int16_t>& out);
    bool SetMonitor(bool enabled);

private:
    struct PendingOpen;
    static SDL_AudioStream* OpenStream(int sample_rate, const std::string& device_name);
    void ReapOpenWorkers(bool wait);

    SDL_AudioStream* stream_ = nullptr;
    SDL_AudioStream* monitor_ = nullptr;
    std::shared_ptr<PendingOpen> pending_;
    std::vector<std::shared_ptr<PendingOpen>> cancelled_opens_;
    int sample_rate_ = 16000;
};

}
