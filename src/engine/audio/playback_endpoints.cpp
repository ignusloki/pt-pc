#include "engine/audio/playback_endpoints.h"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>
#endif

namespace pt::audio {

namespace {

#if defined(_WIN32)

constexpr PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::string WideToUtf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int length = static_cast<int>(wcslen(text));
    const int count = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), count, nullptr, nullptr);
    return out;
}

#endif

}

std::vector<PlaybackEndpoint> NativePlaybackEndpoints() {
    std::vector<PlaybackEndpoint> out;
#if defined(_WIN32)
    using Microsoft::WRL::ComPtr;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return out;
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
            SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)) && SUCCEEDED(devices->GetCount(&count))) {
            std::string default_id;
            ComPtr<IMMDevice> default_device;
            LPWSTR wide_default = nullptr;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &default_device)) &&
                SUCCEEDED(default_device->GetId(&wide_default))) {
                default_id = WideToUtf8(wide_default);
                CoTaskMemFree(wide_default);
            }
            for (UINT index = 0; index < count; ++index) {
                ComPtr<IMMDevice> device;
                if (FAILED(devices->Item(index, &device))) continue;
                PlaybackEndpoint endpoint;
                LPWSTR wide_id = nullptr;
                if (SUCCEEDED(device->GetId(&wide_id))) {
                    endpoint.id = WideToUtf8(wide_id);
                    CoTaskMemFree(wide_id);
                }
                ComPtr<IPropertyStore> properties;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
                    PROPVARIANT name;
                    PropVariantInit(&name);
                    if (SUCCEEDED(properties->GetValue(kFriendlyName, &name)) && name.vt == VT_LPWSTR) {
                        endpoint.name = WideToUtf8(name.pwszVal);
                    }
                    PropVariantClear(&name);
                }
                endpoint.system_default = !default_id.empty() && endpoint.id == default_id;
                out.push_back(std::move(endpoint));
            }
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
#endif
    return out;
}

std::string SystemDefaultPlaybackName() {
    if (!SDL_WasInit(SDL_INIT_AUDIO)) return {};
    const char* name = SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    return name ? std::string(name) : std::string();
}

std::vector<PlaybackEndpoint> ListPlaybackEndpoints(std::vector<uint32_t>* sdl_ids) {
    std::vector<PlaybackEndpoint> out;
    if (sdl_ids) sdl_ids->clear();
    if (!SDL_WasInit(SDL_INIT_AUDIO)) return out;
    const std::vector<PlaybackEndpoint> native = NativePlaybackEndpoints();
    const std::string sdl_default = SystemDefaultPlaybackName();
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    for (int i = 0; ids && i < count; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        if (!name) continue;
        PlaybackEndpoint endpoint;
        endpoint.name = name;
        SDL_AudioSpec spec{};
        if (SDL_GetAudioDeviceFormat(ids[i], &spec, nullptr)) endpoint.channels = spec.channels;
        const PlaybackEndpoint* match = nullptr;
        int matches = 0;
        for (const PlaybackEndpoint& n : native) {
            if (n.name == endpoint.name) {
                match = &n;
                ++matches;
            }
        }
        if (matches == 1) {
            endpoint.id = match->id;
            endpoint.system_default = match->system_default;
        } else {
            endpoint.system_default = endpoint.name == sdl_default;
        }
        out.push_back(std::move(endpoint));
        if (sdl_ids) sdl_ids->push_back(ids[i]);
    }
    SDL_free(ids);
    return out;
}

}
