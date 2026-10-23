#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pt::audio {

class SpatialOutput {
public:
    using RenderFunction = std::function<void(float* interleaved, uint32_t frames, uint32_t channels)>;

    SpatialOutput();
    ~SpatialOutput();
    SpatialOutput(const SpatialOutput&) = delete;
    SpatialOutput& operator=(const SpatialOutput&) = delete;

    bool Open(uint32_t sample_rate, RenderFunction render, const std::string& endpoint_id = {});
    void Close();
    const std::string& DeviceName() const;
    const std::string& DeviceId() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
