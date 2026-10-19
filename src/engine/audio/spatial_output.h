#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace pt::audio {

class SpatialOutput {
public:
    using RenderFunction = std::function<void(float* interleaved, uint32_t frames, uint32_t channels)>;

    SpatialOutput();
    ~SpatialOutput();
    SpatialOutput(const SpatialOutput&) = delete;
    SpatialOutput& operator=(const SpatialOutput&) = delete;

    bool Open(uint32_t sample_rate, RenderFunction render);
    void Close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
