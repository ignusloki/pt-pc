#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void check(OSStatus result, const char* operation) {
    if (result != noErr) {
        std::fprintf(stderr, "%s failed: %d\n", operation, static_cast<int>(result));
        std::exit(1);
    }
}

static void optional_off(AudioUnit unit, AudioUnitPropertyID property, UInt32 value, const char* name) {
    UInt32 bytes = 0;
    Boolean writable = false;
    const OSStatus supported = AudioUnitGetPropertyInfo(unit, property, kAudioUnitScope_Global, 0, &bytes, &writable);
    if (supported == kAudioUnitErr_InvalidProperty || !writable) {
        std::printf("optional %s not available (%d); retaining generic fixed-headphone rendering\n", name, static_cast<int>(supported));
        return;
    }
    check(supported, name);
    check(AudioUnitSetProperty(unit, property, kAudioUnitScope_Global, 0, &value, sizeof(value)), name);
}

struct Source { unsigned channel; };

static OSStatus input(void* context, AudioUnitRenderActionFlags*, const AudioTimeStamp* time,
                      UInt32, UInt32 frames, AudioBufferList* buffers) {
    const auto& source = *static_cast<Source*>(context);
    for (UInt32 channel = 0; channel < buffers->mNumberBuffers; ++channel) {
        auto* output = static_cast<float*>(buffers->mBuffers[channel].mData);
        if (!output) return kAudioUnitErr_InvalidParameter;
        for (UInt32 i = 0; i < frames; ++i) {
            const auto sample = static_cast<unsigned long long>(time->mSampleTime) + i;
            output[i] = channel == source.channel && sample == 4096 ? 0.25f : 0.0f;
        }
        buffers->mBuffers[channel].mDataByteSize = frames * sizeof(float);
    }
    return noErr;
}

static AudioStreamBasicDescription format(UInt32 channels) {
    AudioStreamBasicDescription result{};
    result.mSampleRate = 48000;
    result.mFormatID = kAudioFormatLinearPCM;
    result.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    result.mBytesPerPacket = result.mBytesPerFrame = sizeof(float);
    result.mFramesPerPacket = 1;
    result.mChannelsPerFrame = channels;
    result.mBitsPerChannel = 32;
    return result;
}

struct StereoBuffers {
    UInt32 count = 2;
    AudioBuffer buffers[2];
};

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const char* names[8] = {"front-left", "front-right", "center", "lfe", "side-left", "side-right", "back-left", "back-right"};
    for (unsigned channel = 0; channel < 8; ++channel) {
        AudioComponentDescription description{};
        description.componentType = kAudioUnitType_Mixer;
        description.componentSubType = kAudioUnitSubType_SpatialMixer;
        description.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (!component) { std::fprintf(stderr, "SpatialMixer component unavailable\n"); return 1; }
        AudioUnit mixer = nullptr;
        check(AudioComponentInstanceNew(component, &mixer), "instance");
        UInt32 inputs = 1;
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0, &inputs, sizeof(inputs)), "input count");
        auto source_format = format(8);
        auto destination_format = format(2);
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &source_format, sizeof(source_format)), "8-channel input format");
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &destination_format, sizeof(destination_format)), "stereo output format");
        AudioChannelLayout input_layout{};
        input_layout.mChannelLayoutTag = kAudioChannelLayoutTag_MPEG_7_1_C;
        AudioChannelLayout output_layout{};
        output_layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
        const UInt32 layout_bytes = offsetof(AudioChannelLayout, mChannelDescriptions);
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Input, 0, &input_layout, layout_bytes), "7.1-C input layout");
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Output, 0, &output_layout, layout_bytes), "stereo output layout");
        UInt32 algorithm = kSpatializationAlgorithm_UseOutputType;
        UInt32 mode = kSpatialMixerSourceMode_AmbienceBed;
        UInt32 output_type = kSpatialMixerOutputType_Headphones;
        UInt32 off = 0;
        UInt32 generic_hrtf = kSpatialMixerPersonalizedHRTFMode_Off;
        UInt32 flags = kSpatialMixerRenderingFlags_InterAuralDelay;
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_SpatializationAlgorithm, kAudioUnitScope_Input, 0, &algorithm, sizeof(algorithm)), "algorithm");
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_SpatialMixerSourceMode, kAudioUnitScope_Input, 0, &mode, sizeof(mode)), "ambience bed");
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_SpatialMixerOutputType, kAudioUnitScope_Global, 0, &output_type, sizeof(output_type)), "headphone output");
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_SpatialMixerRenderingFlags, kAudioUnitScope_Input, 0, &flags, sizeof(flags)), "disable extra attenuation");
        optional_off(mixer, kAudioUnitProperty_SpatialMixerEnableHeadTracking, off, "head tracking");
        optional_off(mixer, kAudioUnitProperty_SpatialMixerPersonalizedHRTFMode, generic_hrtf, "personalized HRTF");
        check(AudioUnitSetParameter(mixer, kSpatialMixerParam_ReverbBlend, kAudioUnitScope_Input, 0, 0.0f, 0), "reverb off");
        Source source{channel};
        AURenderCallbackStruct callback{input, &source};
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback)), "input callback");
        UInt32 maximum = 512;
        check(AudioUnitSetProperty(mixer, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maximum, sizeof(maximum)), "maximum frames");
        check(AudioUnitInitialize(mixer), "initialize");
        double energy[2]{};
        float peak[2]{};
        unsigned first[2] = {48000, 48000};
        std::vector<float> left(512), right(512);
        char filename[1024];
        std::snprintf(filename, sizeof(filename), "%s/%s.f32", argv[1], names[channel]);
        auto* file = std::fopen(filename, "wb");
        if (!file) return 1;
        for (unsigned offset = 0; offset < 48000; offset += 512) {
            const UInt32 frames = std::min(512u, 48000u - offset);
            StereoBuffers buffers{};
            buffers.buffers[0] = {1, frames * static_cast<UInt32>(sizeof(float)), left.data()};
            buffers.buffers[1] = {1, frames * static_cast<UInt32>(sizeof(float)), right.data()};
            AudioTimeStamp time{};
            time.mFlags = kAudioTimeStampSampleTimeValid;
            time.mSampleTime = offset;
            AudioUnitRenderActionFlags render_flags = 0;
            check(AudioUnitRender(mixer, &render_flags, &time, 0, frames, reinterpret_cast<AudioBufferList*>(&buffers)), "render");
            for (unsigned i = 0; i < frames; ++i) {
                const float samples[2] = {left[i], right[i]};
                std::fwrite(samples, sizeof(float), 2, file);
                for (unsigned ear = 0; ear < 2; ++ear) {
                    if (!std::isfinite(samples[ear])) return 1;
                    energy[ear] += samples[ear] * samples[ear];
                    peak[ear] = std::max(peak[ear], std::abs(samples[ear]));
                    if (std::abs(samples[ear]) > 1e-7f) first[ear] = std::min(first[ear], offset + i);
                }
            }
        }
        std::fclose(file);
        check(AudioUnitUninitialize(mixer), "uninitialize");
        check(AudioComponentInstanceDispose(mixer), "dispose");
        if (channel != 3 && (energy[0] == 0 || energy[1] == 0)) return 1;
        std::printf("%s energy %.9f %.9f peak %.9f %.9f first %u %u\n", names[channel], energy[0], energy[1], peak[0], peak[1], first[0], first[1]);
    }
    std::puts("PASS generic native 7.1-to-headphone spatial rendering without audio playback or game assets");
}
