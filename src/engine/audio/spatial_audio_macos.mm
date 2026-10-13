#include "engine/audio/spatial_audio.h"

#include "engine/core/log.h"

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace pt::audio {
namespace {

// Engine/SDL: FL FR FC LFE BL BR SL SR. MPEG_7_1_C: FL FR FC LFE SL SR BL BR.
constexpr std::array<uint8_t, 8> kSdlToApple = {0, 1, 2, 3, 6, 7, 4, 5};

void Check(OSStatus status, const char* operation) {
    if (status != noErr) throw std::runtime_error(std::string(operation) + " (" + std::to_string(status) + ")");
}

AudioStreamBasicDescription Format(uint32_t sample_rate, uint32_t channels) {
    AudioStreamBasicDescription format{};
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float);
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = 32;
    return format;
}

class MacSpatialProcessor final : public SpatialProcessor {
public:
    explicit MacSpatialProcessor(uint32_t sample_rate) {
        AudioComponentDescription description{kAudioUnitType_Mixer, kAudioUnitSubType_SpatialMixer,
                                              kAudioUnitManufacturer_Apple, 0, 0};
        const auto component = AudioComponentFindNext(nullptr, &description);
        if (!component) throw std::runtime_error("Apple spatial mixer is unavailable");
        Check(AudioComponentInstanceNew(component, &unit_), "create spatial mixer");
        try {
            Set(kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, uint32_t{1});
            Set(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, Format(sample_rate, 8));
            Set(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, Format(sample_rate, 2));
            Layout(kAudioUnitScope_Input, kAudioChannelLayoutTag_MPEG_7_1_C);
            Layout(kAudioUnitScope_Output, kAudioChannelLayoutTag_Stereo);
            Set(kAudioUnitProperty_SpatializationAlgorithm, kAudioUnitScope_Input,
                uint32_t{kSpatializationAlgorithm_UseOutputType});
            Set(kAudioUnitProperty_SpatialMixerSourceMode, kAudioUnitScope_Input,
                uint32_t{kSpatialMixerSourceMode_AmbienceBed});
            Set(kAudioUnitProperty_SpatialMixerOutputType, kAudioUnitScope_Global,
                uint32_t{kSpatialMixerOutputType_Headphones});
            // The game already applies distance curves and room effects.
            Set(kAudioUnitProperty_SpatialMixerRenderingFlags, kAudioUnitScope_Input,
                uint32_t{kSpatialMixerRenderingFlags_InterAuralDelay});
            OptionalOff(kAudioUnitProperty_SpatialMixerEnableHeadTracking, "head_tracking");
            OptionalOff(kAudioUnitProperty_SpatialMixerPersonalizedHRTFMode, "personalized_HRTF");
            Check(AudioUnitSetParameter(unit_, kSpatialMixerParam_ReverbBlend, kAudioUnitScope_Input, 0, 0, 0),
                  "disable spatial mixer reverb");
            Set(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, kSpatialMaxFrames);
            const AURenderCallbackStruct callback{Input, this};
            Set(kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, callback);
            Check(AudioUnitInitialize(unit_), "initialize spatial mixer");
        } catch (...) {
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
            throw;
        }
    }

    ~MacSpatialProcessor() override {
        if (unit_) {
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
        }
    }

    bool Process(const float* surround, uint32_t frames, float* stereo) override {
        if (!frames) return true;
        if (frames > kSpatialMaxFrames) {
            error_ = kAudioUnitErr_TooManyFramesToProcess;
            return false;
        }
        prepared_frames_ = frames;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (size_t channel = 0; channel < 8; ++channel) {
                input_[channel][frame] = surround[static_cast<size_t>(frame) * 8 + kSdlToApple[channel]];
            }
        }
        struct Buffers { UInt32 count; AudioBuffer buffers[2]; } buffers{
            2, {{1, frames * uint32_t{sizeof(float)}, left_.data()},
                {1, frames * uint32_t{sizeof(float)}, right_.data()}}};
        AudioTimeStamp time{};
        time.mFlags = kAudioTimeStampSampleTimeValid;
        time.mSampleTime = static_cast<double>(offset_);
        AudioUnitRenderActionFlags flags = 0;
        error_ = AudioUnitRender(unit_, &flags, &time, 0, frames, reinterpret_cast<AudioBufferList*>(&buffers));
        if (error_ != noErr) return false;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            if (!std::isfinite(left_[frame]) || !std::isfinite(right_[frame])) {
                error_ = kAudioUnitErr_InvalidParameter;
                return false;
            }
            stereo[frame * 2] = left_[frame];
            stereo[frame * 2 + 1] = right_[frame];
        }
        offset_ += frames;
        return true;
    }

    int32_t ErrorCode() const override { return error_; }

private:
    template <typename T> void Set(AudioUnitPropertyID property, AudioUnitScope scope, const T& value) {
        Check(AudioUnitSetProperty(unit_, property, scope, 0, &value, sizeof(value)), "configure spatial mixer");
    }

    void OptionalOff(AudioUnitPropertyID property, const char* name) {
        UInt32 size = 0;
        Boolean writable = false;
        const OSStatus status = AudioUnitGetPropertyInfo(unit_, property, kAudioUnitScope_Global, 0, &size, &writable);
        if (status == kAudioUnitErr_InvalidProperty) {
            if (AudioDiagnosticsEnabled()) LogInfo("audio diag: native {} property unsupported by this macOS", name);
            return;
        }
        Check(status, "query optional spatial mixer property");
        if (writable) Set(property, kAudioUnitScope_Global, uint32_t{0});
        if (AudioDiagnosticsEnabled()) {
            uint32_t value = 0;
            UInt32 read_size = sizeof(value);
            const OSStatus read = AudioUnitGetProperty(unit_, property, kAudioUnitScope_Global, 0, &value, &read_size);
            LogInfo("audio diag: native {} writable={} read_status={} value={}", name, bool(writable), read, value);
        }
    }

    void Layout(AudioUnitScope scope, AudioChannelLayoutTag tag) {
        AudioChannelLayout layout{};
        layout.mChannelLayoutTag = tag;
        Check(AudioUnitSetProperty(unit_, kAudioUnitProperty_AudioChannelLayout, scope, 0, &layout,
                                  offsetof(AudioChannelLayout, mChannelDescriptions)), "set spatial speaker layout");
    }

    static OSStatus Input(void* context, AudioUnitRenderActionFlags*, const AudioTimeStamp* time,
                          UInt32, UInt32 frames, AudioBufferList* buffers) {
        auto& source = *static_cast<MacSpatialProcessor*>(context);
        if (!(time->mFlags & kAudioTimeStampSampleTimeValid) || time->mSampleTime != static_cast<double>(source.offset_) ||
            frames != source.prepared_frames_ || buffers->mNumberBuffers != 8) return kAudioUnitErr_InvalidParameter;
        for (size_t channel = 0; channel < 8; ++channel) {
            auto& buffer = buffers->mBuffers[channel];
            if (!buffer.mData || buffer.mNumberChannels != 1 || buffer.mDataByteSize < frames * sizeof(float)) {
                return kAudioUnitErr_InvalidParameter;
            }
            std::memcpy(buffer.mData, source.input_[channel].data(), frames * sizeof(float));
            buffer.mDataByteSize = frames * sizeof(float);
        }
        return noErr;
    }

    AudioUnit unit_ = nullptr;
    std::array<std::array<float, kSpatialMaxFrames>, 8> input_{};
    std::array<float, kSpatialMaxFrames> left_{}, right_{};
    uint64_t offset_ = 0;
    uint32_t prepared_frames_ = 0;
    int32_t error_ = 0;
};

} // namespace

std::unique_ptr<SpatialProcessor> CreateSpatialProcessor(uint32_t sample_rate, std::string& reason) {
    try {
        return std::make_unique<MacSpatialProcessor>(sample_rate);
    } catch (const std::exception& error) {
        reason = error.what();
        return nullptr;
    }
}

} // namespace pt::audio
