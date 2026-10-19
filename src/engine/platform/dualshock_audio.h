#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace pt {

constexpr size_t kDs4AudioReportSize = 462;
constexpr size_t kDs4SbcFrameSize = 112;
constexpr size_t kDs4AudioFramesPerReport = 512;

std::array<uint8_t, 78> BuildDs4SpeakerControl(bool enabled);
void SealDs4BluetoothReport(std::span<uint8_t> report);

class DualShockAudioEncoder {
public:
    DualShockAudioEncoder();
    ~DualShockAudioEncoder();
    bool Ready() const;
    void Clear();
    bool Push(std::span<const float> stereo, const std::function<bool(std::span<const uint8_t>)>& submit);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
