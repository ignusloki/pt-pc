// Research only: models an interleaved engine -> AUSpatialMixer -> stereo callback.
// No audio device, game assets, microphone or player profile is opened.
#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kRate = 48000;
constexpr uint32_t kSlice = 512;
// SoundEngine's public eight-channel output is in SDL order, not Wwise order.
constexpr std::array<uint8_t, 8> kSdlToApple = {0, 1, 2, 3, 6, 7, 4, 5};

void Check(OSStatus status, const char* operation) {
    if (status != noErr) throw std::runtime_error(std::string(operation) + ": " + std::to_string(status));
}

AudioStreamBasicDescription Format(uint32_t channels) {
    AudioStreamBasicDescription spec{};
    spec.mSampleRate = kRate;
    spec.mFormatID = kAudioFormatLinearPCM;
    spec.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    spec.mBytesPerPacket = spec.mBytesPerFrame = sizeof(float);
    spec.mFramesPerPacket = 1;
    spec.mChannelsPerFrame = channels;
    spec.mBitsPerChannel = 32;
    return spec;
}

// Fixed storage allows repeated input pulls to reuse exactly the same source block.
class BedMixer {
public:
    BedMixer() {
        AudioComponentDescription description{kAudioUnitType_Mixer, kAudioUnitSubType_SpatialMixer,
                                              kAudioUnitManufacturer_Apple, 0, 0};
        auto component = AudioComponentFindNext(nullptr, &description);
        if (!component) throw std::runtime_error("AUSpatialMixer unavailable");
        Check(AudioComponentInstanceNew(component, &unit_), "create mixer");
        try {
            Set(kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, uint32_t{1});
            Set(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, Format(8));
            Set(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, Format(2));
            Layout(kAudioUnitScope_Input, kAudioChannelLayoutTag_MPEG_7_1_C);
            Layout(kAudioUnitScope_Output, kAudioChannelLayoutTag_Stereo);
            Set(kAudioUnitProperty_SpatializationAlgorithm, kAudioUnitScope_Input,
                uint32_t{kSpatializationAlgorithm_UseOutputType});
            Set(kAudioUnitProperty_SpatialMixerSourceMode, kAudioUnitScope_Input,
                uint32_t{kSpatialMixerSourceMode_AmbienceBed});
            Set(kAudioUnitProperty_SpatialMixerOutputType, kAudioUnitScope_Global,
                uint32_t{kSpatialMixerOutputType_Headphones});
            Set(kAudioUnitProperty_SpatialMixerRenderingFlags, kAudioUnitScope_Input,
                uint32_t{kSpatialMixerRenderingFlags_InterAuralDelay});
            OptionalOff(kAudioUnitProperty_SpatialMixerEnableHeadTracking);
            OptionalOff(kAudioUnitProperty_SpatialMixerPersonalizedHRTFMode);
            Check(AudioUnitSetParameter(unit_, kSpatialMixerParam_ReverbBlend, kAudioUnitScope_Input, 0, 0, 0),
                  "disable added reverb");
            Set(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, kSlice);
            const AURenderCallbackStruct callback{Input, this};
            Set(kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, callback);
            Check(AudioUnitInitialize(unit_), "initialize mixer");
        } catch (...) {
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
            throw;
        }
    }
    ~BedMixer() {
        if (unit_) {
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
        }
    }
    BedMixer(const BedMixer&) = delete;
    BedMixer& operator=(const BedMixer&) = delete;

    void Process(const float* source, uint32_t frames, float* stereo) {
        if (!frames || frames > kSlice) throw std::runtime_error("invalid native slice size");
        prepared_frames_ = frames;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (size_t channel = 0; channel < 8; ++channel) {
                input_[channel][frame] = source[static_cast<size_t>(frame) * 8 + kSdlToApple[channel]];
            }
        }
        struct Buffers { UInt32 count; AudioBuffer buffers[2]; } buffers{
            2, {{1, frames * uint32_t{sizeof(float)}, left_.data()},
                {1, frames * uint32_t{sizeof(float)}, right_.data()}}};
        AudioTimeStamp time{};
        time.mFlags = kAudioTimeStampSampleTimeValid;
        time.mSampleTime = static_cast<double>(offset_);
        AudioUnitRenderActionFlags flags = 0;
        Check(AudioUnitRender(unit_, &flags, &time, 0, frames, reinterpret_cast<AudioBufferList*>(&buffers)),
              "render variable-size slice");
        for (uint32_t frame = 0; frame < frames; ++frame) {
            stereo[static_cast<size_t>(frame) * 2] = left_[frame];
            stereo[static_cast<size_t>(frame) * 2 + 1] = right_[frame];
        }
        offset_ += frames;
    }
    uint64_t InputPulls() const { return pulls_; }
    uint64_t Frames() const { return offset_; }

private:
    template <typename T> void Set(AudioUnitPropertyID property, AudioUnitScope scope, const T& value) {
        Check(AudioUnitSetProperty(unit_, property, scope, 0, &value, sizeof(value)), "configure mixer property");
    }
    void OptionalOff(AudioUnitPropertyID property) {
        UInt32 size = 0;
        Boolean writable = false;
        const auto status = AudioUnitGetPropertyInfo(unit_, property, kAudioUnitScope_Global, 0, &size, &writable);
        if (status == kAudioUnitErr_InvalidProperty) return;
        Check(status, "query optional property");
        if (writable) Set(property, kAudioUnitScope_Global, uint32_t{0});
    }
    void Layout(AudioUnitScope scope, AudioChannelLayoutTag tag) {
        AudioChannelLayout layout{};
        layout.mChannelLayoutTag = tag;
        Check(AudioUnitSetProperty(unit_, kAudioUnitProperty_AudioChannelLayout, scope, 0, &layout,
                                   offsetof(AudioChannelLayout, mChannelDescriptions)), "set channel layout");
    }
    static OSStatus Input(void* context, AudioUnitRenderActionFlags*, const AudioTimeStamp* time,
                          UInt32, UInt32 frames, AudioBufferList* buffers) {
        auto& mixer = *static_cast<BedMixer*>(context);
        // If the AU requests a different range, a production adapter needs a FIFO.
        if (!(time->mFlags & kAudioTimeStampSampleTimeValid) ||
            time->mSampleTime != static_cast<double>(mixer.offset_) ||
            frames != mixer.prepared_frames_ || buffers->mNumberBuffers != 8) return kAudioUnitErr_InvalidParameter;
        ++mixer.pulls_;
        for (size_t channel = 0; channel < 8; ++channel) {
            auto& buffer = buffers->mBuffers[channel];
            if (!buffer.mData || buffer.mNumberChannels != 1 || buffer.mDataByteSize < frames * sizeof(float)) {
                return kAudioUnitErr_InvalidParameter;
            }
            std::memcpy(buffer.mData, mixer.input_[channel].data(), frames * sizeof(float));
            buffer.mDataByteSize = frames * sizeof(float);
        }
        return noErr;
    }
    AudioUnit unit_ = nullptr;
    std::array<std::array<float, kSlice>, 8> input_{};
    std::array<float, kSlice> left_{}, right_{};
    uint32_t prepared_frames_ = 0;
    uint64_t offset_ = 0, pulls_ = 0;
};

struct Result {
    std::vector<float> stereo;
    uint64_t produced = 0, pulls = 0, slices = 0;
    double max_slice_ms = 0, elapsed_ms = 0;
    float peak = 0;
    uint64_t above_full_scale = 0;
};

// A deterministic interleaved source stands in for SoundEngine::Render(..., 8).
Result Render(const std::vector<uint32_t>& requests, uint32_t total, int impulse_channel, bool correlated_tones) {
    BedMixer mixer;
    std::array<float, kSlice * 8> source{};
    std::array<float, kSlice * 2> stereo{};
    Result result;
    result.stereo.resize(static_cast<size_t>(total) * 2);
    const auto begin = std::chrono::steady_clock::now();
    size_t request = 0;
    while (result.produced < total) {
        uint32_t remaining = static_cast<uint32_t>(std::min<uint64_t>(requests[request++ % requests.size()],
                                                                    total - result.produced));
        while (remaining) {
            const auto start = std::chrono::steady_clock::now();
            const uint32_t frames = std::min(remaining, kSlice);
            for (uint32_t frame = 0; frame < frames; ++frame) {
                const uint64_t position = result.produced + frame;
                for (size_t channel = 0; channel < 8; ++channel) {
                    float sample = 0;
                    if (impulse_channel >= 0) {
                        sample = channel == static_cast<size_t>(impulse_channel) && position == 4096 ? 0.25f : 0;
                    } else if (position >= 4096 && channel != 3) {
                        const double frequency = correlated_tones ? 440.0 : 173.0 + channel * 107.0;
                        const float gain = correlated_tones ? 0.9f : 0.1f;
                        sample = gain * static_cast<float>(std::sin(position * frequency * 6.283185307179586 / kRate));
                    }
                    source[static_cast<size_t>(frame) * 8 + channel] = sample;
                }
            }
            mixer.Process(source.data(), frames, stereo.data());
            const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            result.max_slice_ms = std::max(result.max_slice_ms, elapsed);
            std::copy_n(stereo.data(), frames * 2, result.stereo.data() + result.produced * 2);
            result.produced += frames; // Advance the source once, independent of the AU's input-pull count.
            ++result.slices;
            remaining -= frames;
        }
    }
    result.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    result.pulls = mixer.InputPulls();
    if (mixer.Frames() != result.produced) throw std::runtime_error("source and mixer clocks disagree");
    for (const float sample : result.stereo) {
        if (!std::isfinite(sample)) throw std::runtime_error("nonfinite output");
        result.peak = std::max(result.peak, std::abs(sample));
        result.above_full_scale += std::abs(sample) > 1;
    }
    return result;
}
}

int main() {
    try {
        const std::vector<uint32_t> regular{256};
        const std::vector<uint32_t> irregular{1, 7, 127, 255, 511, 512, 997, 4096};
        const auto reference = Render(regular, 96000, -1, false);
        const auto varied = Render(irregular, 96000, -1, false);
        float difference = 0;
        for (size_t i = 0; i < reference.stereo.size(); ++i) {
            difference = std::max(difference, std::abs(reference.stereo[i] - varied.stereo[i]));
        }
        if (difference > 0.00002f) throw std::runtime_error("native result changes with callback size: " + std::to_string(difference));
        const auto loud = Render(irregular, 96000, -1, true);
        std::printf("{\n  \"sample_rate\": %u, \"native_max_slice\": %u,\n", kRate, kSlice);
        std::printf("  \"request_sizes\": [1,7,127,255,511,512,997,4096],\n");
        std::printf("  \"regular_variable_max_difference\": %.9g,\n", difference);
        std::printf("  \"variable_produced_frames\": %llu, \"variable_native_pulls\": %llu, \"variable_native_slices\": %llu,\n",
                    static_cast<unsigned long long>(varied.produced), static_cast<unsigned long long>(varied.pulls),
                    static_cast<unsigned long long>(varied.slices));
        std::printf("  \"offline_render_ms\": %.6f, \"max_slice_with_synthetic_source_ms\": %.6f,\n",
                    varied.elapsed_ms, varied.max_slice_ms);
        std::printf("  \"correlated_seven_channel_peak\": %.9g, \"correlated_samples_above_full_scale\": %llu,\n",
                    loud.peak, static_cast<unsigned long long>(loud.above_full_scale));
        const char* names[] = {"front-left", "front-right", "center", "lfe", "back-left", "back-right", "side-left", "side-right"};
        std::printf("  \"sdl_channel_impulses\": [\n");
        for (int channel = 0; channel < 8; ++channel) {
            const auto impulse = Render(irregular, 8192, channel, false);
            double energy[2]{};
            for (size_t i = 0; i < impulse.stereo.size(); ++i) energy[i % 2] += impulse.stereo[i] * impulse.stereo[i];
            const bool left = channel == 0 || channel == 4 || channel == 6;
            const bool right = channel == 1 || channel == 5 || channel == 7;
            if ((left && energy[0] <= energy[1]) || (right && energy[1] <= energy[0]) || energy[0] + energy[1] == 0) {
                throw std::runtime_error(std::string("incorrect directional output: ") + names[channel]);
            }
            std::printf("    {\"sdl_index\": %d, \"speaker\": \"%s\", \"left_energy\": %.9g, \"right_energy\": %.9g}%s\n",
                        channel, names[channel], energy[0], energy[1], channel == 7 ? "" : ",");
        }
        std::printf("  ], \"passed\": true\n}\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "streaming probe failed: %s\n", error.what());
        return 1;
    }
}
