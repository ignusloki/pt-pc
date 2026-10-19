#include "engine/audio/spatial_output.h"

#include "engine/core/log.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <spatialaudioclient.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace pt::audio {

using Microsoft::WRL::ComPtr;

namespace {

constexpr std::array<AudioObjectType, 8> kBed = {
    AudioObjectType_FrontLeft, AudioObjectType_FrontRight, AudioObjectType_FrontCenter, AudioObjectType_LowFrequency,
    AudioObjectType_BackLeft,  AudioObjectType_BackRight,  AudioObjectType_SideLeft,    AudioObjectType_SideRight,
};

}

struct SpatialOutput::Impl {
    ComPtr<ISpatialAudioClient> client;
    ComPtr<ISpatialAudioObjectRenderStream> stream;
    std::array<ComPtr<ISpatialAudioObject>, 8> objects;
    HANDLE event = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};
    bool com_initialized = false;
    RenderFunction render;
    std::vector<float> mix;

    void Run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (running) {
            if (WaitForSingleObject(event, 100) != WAIT_OBJECT_0) continue;
            UINT32 available = 0, frames = 0;
            if (FAILED(stream->BeginUpdatingAudioObjects(&available, &frames))) continue;
            if (frames > 0) {
                mix.assign(static_cast<size_t>(frames) * kBed.size(), 0.0f);
                render(mix.data(), frames, static_cast<uint32_t>(kBed.size()));
                for (size_t c = 0; c < kBed.size(); ++c) {
                    if (!objects[c]) continue;
                    BYTE* buffer = nullptr;
                    UINT32 bytes = 0;
                    if (FAILED(objects[c]->GetBuffer(&buffer, &bytes)) || !buffer) continue;
                    float* out = reinterpret_cast<float*>(buffer);
                    const UINT32 count = std::min<UINT32>(frames, bytes / sizeof(float));
                    for (UINT32 i = 0; i < count; ++i) out[i] = mix[static_cast<size_t>(i) * kBed.size() + c];
                }
            }
            stream->EndUpdatingAudioObjects();
        }
        CoUninitialize();
    }
};

SpatialOutput::SpatialOutput() : impl_(std::make_unique<Impl>()) {}

SpatialOutput::~SpatialOutput() {
    Close();
}

bool SpatialOutput::Open(uint32_t sample_rate, RenderFunction render) {
    Close();
    Impl& s = *impl_;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        LogWarn("audio: spatial sound: COM initialization failed ({:#x})", static_cast<uint32_t>(initialized));
        return false;
    }
    s.com_initialized = SUCCEEDED(initialized);
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
        LogWarn("audio: spatial sound: no default playback device");
        device.Reset();
        enumerator.Reset();
        Close();
        return false;
    }
    if (FAILED(device->Activate(__uuidof(ISpatialAudioClient), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(s.client.GetAddressOf())))) {
        LogInfo("audio: spatial sound is not available for this device (turn on Windows Sonic or Dolby Atmos in Windows' sound settings)");
        device.Reset();
        enumerator.Reset();
        Close();
        return false;
    }
    device.Reset();
    enumerator.Reset();
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 1;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = 32;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = sample_rate * 4;
    if (FAILED(s.client->IsAudioObjectFormatSupported(&format))) {
        LogInfo("audio: spatial sound: {} Hz float objects not supported", sample_rate);
        Close();
        return false;
    }
    AudioObjectType mask = AudioObjectType_None;
    for (const AudioObjectType t : kBed) mask = static_cast<AudioObjectType>(mask | t);
    AudioObjectType offered = AudioObjectType_None;
    if (FAILED(s.client->GetNativeStaticObjectTypeMask(&offered)) || (offered & mask) != mask) {
        LogInfo("audio: spatial sound: the full 7.1 bed is unavailable, using ordinary playback");
        Close();
        return false;
    }
    s.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    SpatialAudioObjectRenderStreamActivationParams params{};
    params.ObjectFormat = &format;
    params.StaticObjectTypeMask = mask;
    params.MinDynamicObjectCount = 0;
    params.MaxDynamicObjectCount = 0;
    params.Category = AudioCategory_GameEffects;
    params.EventHandle = s.event;
    PROPVARIANT activation;
    PropVariantInit(&activation);
    activation.vt = VT_BLOB;
    activation.blob.cbSize = sizeof(params);
    activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    const HRESULT hr = s.client->ActivateSpatialAudioStream(&activation, IID_PPV_ARGS(&s.stream));
    if (FAILED(hr)) {
        LogInfo("audio: spatial sound stream not activated ({:#x}): no spatial format is on for this device", static_cast<uint32_t>(hr));
        Close();
        return false;
    }
    int active = 0;
    for (size_t c = 0; c < kBed.size(); ++c) {
        if (!(mask & kBed[c])) continue;
        if (SUCCEEDED(s.stream->ActivateSpatialAudioObject(kBed[c], &s.objects[c]))) ++active;
    }
    if (active != static_cast<int>(kBed.size()) || FAILED(s.stream->Start())) {
        LogWarn("audio: spatial sound: the full 7.1 bed could not start, using ordinary playback");
        Close();
        return false;
    }
    s.render = std::move(render);
    s.running = true;
    s.thread = std::thread([&s] { s.Run(); });
    LogInfo("audio: playback through Windows spatial sound at {} Hz, a bed of {} static objects", sample_rate, active);
    return true;
}

void SpatialOutput::Close() {
    Impl& s = *impl_;
    if (s.running) {
        s.running = false;
        if (s.thread.joinable()) s.thread.join();
    }
    if (s.stream) s.stream->Stop();
    for (auto& o : s.objects) o.Reset();
    s.stream.Reset();
    s.client.Reset();
    if (s.event) CloseHandle(s.event);
    s.event = nullptr;
    if (s.com_initialized) {
        CoUninitialize();
        s.com_initialized = false;
    }
}

}

#else

namespace pt::audio {

struct SpatialOutput::Impl {};
SpatialOutput::SpatialOutput() : impl_(std::make_unique<Impl>()) {}
SpatialOutput::~SpatialOutput() = default;
bool SpatialOutput::Open(uint32_t, RenderFunction) {
    return false;
}
void SpatialOutput::Close() {}

}

#endif
