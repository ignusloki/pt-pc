#include "engine/platform/controller_speaker.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_hidapi.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <format>
#include <vector>

#include "engine/core/log.h"
#include "engine/platform/device_start.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <SetupAPI.h>
#include <audioclient.h>
#include <devpkey.h>
#include <hidclass.h>
#include <propvarutil.h>
#include <wrl/client.h>
#endif

namespace pt {

namespace {

constexpr uint16_t kSonyVendor = 0x054C;
constexpr uint16_t kDualSense = 0x0CE6;
constexpr uint16_t kDualSenseEdge = 0x0DF2;
constexpr int kPcmRate = 48000;
#if defined(_WIN32)
constexpr GUID kHidDeviceInterfaceGuid{0x4D1E55B2L, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
constexpr DEVPROPKEY kDeviceContainerId{{0x8C7ED206L, 0x3F8A, 0x4827, {0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C}}, 2};
#endif

void SetReason(std::string* reason, std::string text) {
    if (reason) {
        *reason = std::move(text);
    }
}

#if defined(_WIN32)

using Microsoft::WRL::ComPtr;

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), count) != count) {
        return {};
    }
    return out;
}

std::string WideToUtf8(const wchar_t* text) {
    if (!text || !*text) {
        return {};
    }
    const int length = static_cast<int>(std::wcslen(text));
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, out.data(), count, nullptr, nullptr) != count) {
        return {};
    }
    return out;
}

bool HidContainerId(std::string_view path, GUID* out) {
    const std::wstring wanted = Utf8ToWide(path);
    if (wanted.empty()) {
        return false;
    }
    HDEVINFO devices = SetupDiGetClassDevsW(&kHidDeviceInterfaceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool found = false;
    for (DWORD index = 0; !found; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &kHidDeviceInterfaceGuid, index, &interface_data)) {
            break;
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<std::byte> detail_buffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detail_buffer.data());
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA device_info{};
        device_info.cbSize = sizeof(device_info);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, detail, required, nullptr, &device_info)) {
            continue;
        }
        if (CompareStringOrdinal(detail->DevicePath, -1, wanted.c_str(), -1, TRUE) != CSTR_EQUAL) {
            continue;
        }
        DEVPROPTYPE type = 0;
        GUID container{};
        if (SetupDiGetDevicePropertyW(devices, &device_info, &kDeviceContainerId, &type,
                                      reinterpret_cast<PBYTE>(&container), sizeof(container), nullptr, 0) &&
            type == DEVPROP_TYPE_GUID) {
            *out = container;
            found = true;
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return found;
}

struct EndpointCandidate {
    std::string name;
    int channels = 0;
};

int QueryEndpointChannels(IMMDevice* endpoint, SonyPadAudio model) {
    ComPtr<IAudioClient> client;
    if (FAILED(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf())))) {
        return 0;
    }
    WAVEFORMATEX* format = nullptr;
    if (FAILED(client->GetMixFormat(&format)) || !format) {
        return 0;
    }
    const int channels = format->nChannels;
    bool matches = ControllerEndpointChannelsFit(model, channels) && (model != SonyPadAudio::DualSense || format->nSamplesPerSec == kPcmRate);
    if (matches && model == SonyPadAudio::DualSense) {
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
            constexpr DWORD kQuadMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
            matches = extended->dwChannelMask == kQuadMask;
        } else {
            matches = false;
        }
    }
    CoTaskMemFree(format);
    return matches ? channels : 0;
}

bool ReadEndpointContainer(IMMDevice* endpoint, GUID* out) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(endpoint->OpenPropertyStore(STGM_READ, properties.GetAddressOf()))) {
        return false;
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT result = properties->GetValue(PKEY_Device_ContainerId, &value);
    const bool ok = SUCCEEDED(result) && value.vt == VT_CLSID && value.puuid;
    if (ok) {
        *out = *value.puuid;
    }
    PropVariantClear(&value);
    return ok;
}

std::vector<EndpointCandidate> AudioEndpointsForContainer(const GUID& container, SonyPadAudio model) {
    std::vector<EndpointCandidate> out;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(enumerator.GetAddressOf())))) {
        return out;
    }
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.GetAddressOf()))) {
        return out;
    }
    UINT count = 0;
    if (FAILED(devices->GetCount(&count))) {
        return out;
    }
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> endpoint;
        if (FAILED(devices->Item(index, endpoint.GetAddressOf()))) {
            continue;
        }
        GUID endpoint_container{};
        if (!ReadEndpointContainer(endpoint.Get(), &endpoint_container) || !IsEqualGUID(endpoint_container, container)) {
            continue;
        }
        const int channels = QueryEndpointChannels(endpoint.Get(), model);
        if (channels == 0) {
            continue;
        }
        ComPtr<IPropertyStore> properties;
        if (FAILED(endpoint->OpenPropertyStore(STGM_READ, properties.GetAddressOf()))) {
            continue;
        }
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR && name.pwszVal) {
            std::string utf8 = WideToUtf8(name.pwszVal);
            if (!utf8.empty()) {
                out.push_back({std::move(utf8), channels});
            }
        }
        PropVariantClear(&name);
    }
    return out;
}

std::optional<uint32_t> ResolveContainerDevice(const GUID& container, SonyPadAudio model,
                                               std::span<const ControllerAudioEndpointName> sdl_devices) {
    const std::vector<EndpointCandidate> native_endpoints = AudioEndpointsForContainer(container, model);
    std::vector<ControllerSpeakerEndpointName> endpoint_names;
    endpoint_names.reserve(native_endpoints.size());
    for (const EndpointCandidate& endpoint : native_endpoints) {
        endpoint_names.push_back({endpoint.name, endpoint.channels});
    }
    return MatchUniqueControllerAudioEndpoint(endpoint_names, sdl_devices, model);
}

#endif

std::vector<ControllerAudioEndpointName> SdlPlaybackDevices(std::vector<std::string>& names) {
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    std::vector<ControllerAudioEndpointName> sdl_devices;
    names.clear();
    names.reserve(count > 0 ? static_cast<size_t>(count) : 0);
    sdl_devices.reserve(count > 0 ? static_cast<size_t>(count) : 0);
    for (int index = 0; ids && index < count; ++index) {
        const char* name = SDL_GetAudioDeviceName(ids[index]);
        SDL_AudioSpec spec{};
        if (!name || !SDL_GetAudioDeviceFormat(ids[index], &spec, nullptr)) {
            continue;
        }
        names.emplace_back(name);
        sdl_devices.push_back({ids[index], names.back(), spec.channels});
    }
    SDL_free(ids);
    return sdl_devices;
}

std::string DeviceList(std::span<const ControllerAudioEndpointName> devices) {
    std::vector<std::string> entries;
    for (const ControllerAudioEndpointName& device : devices) {
        entries.push_back(std::format("{} ({} ch)", device.name, device.channels));
    }
    std::sort(entries.begin(), entries.end());
    std::string out;
    for (const std::string& entry : entries) {
        out += (out.empty() ? "" : "; ") + entry;
    }
    return out.empty() ? std::string("none") : out;
}

}

ControllerSpeakerOutput::ControllerSpeakerOutput() = default;

ControllerSpeakerOutput::~ControllerSpeakerOutput() {
    Close();
}

struct ControllerSpeakerOutput::HidWriter {
    explicit HidWriter(SDL_hid_device* device) : device(device), thread([this] { Run(); }) {}
    ~HidWriter() {
        {
            std::lock_guard lock(mutex);
            stop = true;
        }
        wake.notify_one();
        thread.join();
    }
    bool Push(const std::array<uint8_t, kDualSenseBtHapticsReportSize>& report) {
        if (failed.load()) return false;
        {
            std::lock_guard lock(mutex);
            if (queue.size() >= 8) queue.pop_front();
            queue.push_back(report);
        }
        wake.notify_one();
        return true;
    }
    void Run() {
        std::unique_lock lock(mutex);
        while (true) {
            wake.wait(lock, [this] { return stop || !queue.empty(); });
            if (stop) return;
            const auto report = queue.front();
            queue.pop_front();
            lock.unlock();
            if (SDL_hid_write(device, report.data(), report.size()) < 0) {
                LogInfo("input: DualSense Bluetooth haptics report failed: {}", SDL_GetError());
                failed.store(true);
            }
            lock.lock();
        }
    }
    SDL_hid_device* device;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::array<uint8_t, kDualSenseBtHapticsReportSize>> queue;
    std::atomic<bool> failed{false};
    bool stop = false;
    std::thread thread;
};

void BuildDualSenseBtHapticsReport(std::span<const int8_t> stereo_s8, uint8_t counter, std::span<uint8_t> report) {
    if (report.size() < kDualSenseBtHapticsReportSize) return;
    std::fill(report.begin(), report.begin() + kDualSenseBtHapticsReportSize, uint8_t{0});
    report[0] = 0x32;
    report[1] = 0x00;
    report[2] = 0x80 | 0x11;
    report[3] = 7;
    const uint8_t control[7] = {0xFE, 0x00, 0x00, 0x00, 0x00, 0xFF, counter};
    std::copy(std::begin(control), std::end(control), report.begin() + 4);
    report[11] = 0x80 | 0x12;
    report[12] = 64;
    for (size_t i = 0; i < 64 && i < stereo_s8.size(); ++i) {
        report[13 + i] = static_cast<uint8_t>(stereo_s8[i]);
    }
    uint32_t crc = 0xFFFFFFFFu;
    auto feed = [&](uint8_t byte) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    };
    feed(0xA2);
    for (size_t i = 0; i < kDualSenseBtHapticsReportSize - 4; ++i) feed(report[i]);
    crc = ~crc;
    for (size_t i = 0; i < 4; ++i) report[kDualSenseBtHapticsReportSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
}

bool ControllerSpeakerOutput::WriteBluetoothHaptics(const float* actuator_stereo, uint32_t frames) {
    if (!hid_ || !actuator_stereo) return false;
    constexpr uint32_t kDecimation = static_cast<uint32_t>(kPcmRate / kDualSenseBtHapticsRate);
    for (uint32_t i = 0; i < frames; ++i) {
        bt_sum_[0] += actuator_stereo[i * 2];
        bt_sum_[1] += actuator_stereo[i * 2 + 1];
        if (++bt_summed_ < kDecimation) continue;
        for (int c = 0; c < 2; ++c) {
            const float v = std::clamp(bt_sum_[c] / static_cast<float>(kDecimation), -1.0f, 1.0f);
            bt_frames_[bt_frame_count_ * 2 + c] = static_cast<int8_t>(std::lround(v * 127.0f));
        }
        bt_sum_ = {};
        bt_summed_ = 0;
        if (++bt_frame_count_ < kDualSenseBtHapticsFrames) continue;
        std::array<uint8_t, kDualSenseBtHapticsReportSize> report{};
        BuildDualSenseBtHapticsReport(bt_frames_, bt_counter_++, report);
        bt_frame_count_ = 0;
        if (!hid_writer_) hid_writer_ = std::make_unique<HidWriter>(hid_);
        if (!hid_writer_->Push(report)) return false;
    }
    return true;
}

bool ControllerSpeakerOutput::IsOpen() const {
    if (hid_) return true;
    if (!stream_) return false;
    const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(stream_);
    return device != 0 && SDL_GetAudioDeviceName(device) != nullptr;
}

bool ControllerSpeakerOutput::OpenForGamepad(SDL_Gamepad* gamepad, std::string* reason) {
    Close();
    if (!gamepad) {
        SetReason(reason, "no gamepad selected");
        return false;
    }
    const uint16_t vendor = SDL_GetGamepadVendor(gamepad);
    const uint16_t product = SDL_GetGamepadProduct(gamepad);
    if (SDL_GetGamepadConnectionState(gamepad) == SDL_JOYSTICK_CONNECTION_WIRELESS && vendor == kSonyVendor &&
        (product == kDualSense || product == kDualSenseEdge)) {
        const char* hid_path = SDL_GetGamepadPath(gamepad);
        const bool hid_ready = hid_path && SDL_hid_init() == 0;
        hid_ = hid_ready ? SDL_hid_open_path(hid_path) : nullptr;
        if (!hid_) {
            if (hid_ready) SDL_hid_exit();
            SetReason(reason, std::format("could not open the DualSense Bluetooth HID path for haptics: {}", SDL_GetError()));
            return false;
        }
        route_ = ControllerPcmRoute::DualSenseBluetoothHaptics;
        device_name_ = "DualSense Bluetooth haptics";
        LogInfo("input: DualSense Bluetooth haptics opened (HID output report 0x32; the speaker needs a wired connection)");
        return true;
    }
    const SonyPadAudio model = SonyPadAudioModel(vendor, product);
    if (model == SonyPadAudio::None) {
        SetReason(reason, "controller model has no verified speaker audio layout");
        return false;
    }
    const char* override_env = SDL_getenv("PT_CONTROLLER_SPEAKER_DEVICE");
    const std::string override_name = override_env ? override_env : "";
    const SDL_JoystickConnectionState connection = SDL_GetGamepadConnectionState(gamepad);
    if (connection == SDL_JOYSTICK_CONNECTION_WIRELESS || connection == SDL_JOYSTICK_CONNECTION_INVALID) {
        SetReason(reason, "controller speaker routing requires a wired gamepad (a DualSense over Bluetooth gets haptics only)");
        return false;
    }
    const char* path = SDL_GetGamepadPath(gamepad);

    if (devices::AudioStarting()) {
        SetReason(reason, "the audio devices are still starting");
        return false;
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SetReason(reason, std::format("SDL audio initialization failed: {}", SDL_GetError()));
            return false;
        }
        owns_audio_subsystem_ = true;
    }
    std::vector<std::string> device_names;
    const std::vector<ControllerAudioEndpointName> devices = SdlPlaybackDevices(device_names);

    std::optional<uint32_t> id;
    const char* how = "";
    if (!override_name.empty()) {
        id = MatchControllerEndpointOverride(devices, override_name, model);
        how = "PT_CONTROLLER_SPEAKER_DEVICE override";
        if (!id) {
            Close();
            SetReason(reason, std::format("PT_CONTROLLER_SPEAKER_DEVICE '{}' matched no single playback device with a fitting channel count "
                                          "(devices: {})", override_name, DeviceList(devices)));
            return false;
        }
    }
#if defined(_WIN32)
    GUID container{};
    if (!id && connection == SDL_JOYSTICK_CONNECTION_WIRED && path && HidContainerId(path, &container)) {
        const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(com_result) || com_result == RPC_E_CHANGED_MODE) {
            id = ResolveContainerDevice(container, model, devices);
            how = "the pad's PnP container";
        }
        if (SUCCEEDED(com_result)) {
            CoUninitialize();
        }
    }
#endif
    if (!id) {
        id = MatchControllerEndpointByName(devices, model);
        how = "the pad's endpoint name";
    }
    if (!id) {
        Close();
        SetReason(reason, std::format("no single playback endpoint of this {} found (by container or by name; devices: {})",
                                      model == SonyPadAudio::DualSense ? "DualSense" : "DualShock 4", DeviceList(devices)));
        return false;
    }
    int channels = model == SonyPadAudio::DualSense ? 4 : 1;
    for (const ControllerAudioEndpointName& device : devices) {
        if (device.id == *id) channels = device.channels;
    }

    SDL_AudioSpec spec{SDL_AUDIO_F32, channels, kPcmRate};
    stream_ = SDL_OpenAudioDeviceStream(*id, &spec, nullptr, nullptr);
    if (!stream_) {
        Close();
        SetReason(reason, std::format("SDL could not open the matched controller audio endpoint: {}", SDL_GetError()));
        return false;
    }
    SDL_AudioSpec source{};
    SDL_AudioSpec output{};
    if (!SDL_GetAudioStreamFormat(stream_, &source, &output) || source.channels != channels || output.channels != channels ||
        (model == SonyPadAudio::DualSense && output.freq != kPcmRate)) {
        Close();
        SetReason(reason, "opened controller endpoint changed format; expected the verified PCM channel layout");
        return false;
    }
    if (model == SonyPadAudio::DualSense) {
        int map_count = 0;
        int* map = SDL_GetAudioStreamOutputChannelMap(stream_, &map_count);
        const bool identity = !map || (map_count == 4 && map[0] == 0 && map[1] == 1 && map[2] == 2 && map[3] == 3);
        SDL_free(map);
        if (!identity) {
            Close();
            SetReason(reason, "opened DualSense endpoint has a nonstandard output channel map");
            return false;
        }
    }
    const char* name = SDL_GetAudioDeviceName(*id);
    device_name_ = name ? name : "controller audio endpoint";
    if (model == SonyPadAudio::DualSense && path) {
        if (SDL_hid_init() == 0) {
            if (SDL_hid_device* hid = SDL_hid_open_path(path)) {
                uint8_t report[48] = {};
                report[0] = 0x02;
                report[1] = 0x20 | 0x80;
                report[6] = 0x64;
                report[8] = 3 << 4;
                const bool sent = SDL_hid_write(hid, report, sizeof(report)) >= 0;
                LogInfo("input: DualSense speaker path {}", sent ? "set to the built-in speaker" : std::string("not set: ") + SDL_GetError());
                SDL_hid_close(hid);
            }
            SDL_hid_exit();
        }
    }
    route_ = model == SonyPadAudio::DualSense ? ControllerPcmRoute::DualSenseQuad : ControllerPcmRoute::DualShock4Mono;
    ds4_channels_ = model == SonyPadAudio::DualShock4 ? channels : 1;
    SDL_ResumeAudioStreamDevice(stream_);
    SetReason(reason, {});
    LogInfo("input: controller PCM endpoint '{}' opened for '{}' ({} channels at {} Hz, found by {})", device_name_, SDL_GetGamepadName(gamepad),
            channels, output.freq, how);
    return true;
}

void ControllerSpeakerOutput::Close() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    hid_writer_.reset();
    if (hid_) {
        SDL_hid_close(hid_);
        SDL_hid_exit();
        hid_ = nullptr;
    }
    bt_sum_ = {};
    bt_summed_ = 0;
    bt_frame_count_ = 0;
    route_ = ControllerPcmRoute::None;
    ds4_channels_ = 1;
    device_name_.clear();
    haptic_filter_.Reset();
    if (owns_audio_subsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        owns_audio_subsystem_ = false;
    }
}

void ControllerSpeakerOutput::ClearPending() {
    if (stream_) SDL_ClearAudioStream(stream_);
    haptic_filter_.Reset();
}

bool ControllerSpeakerOutput::WriteMono(const float* speaker_mono, uint32_t frames) {
    if (!stream_ || route_ != ControllerPcmRoute::DualShock4Mono || !speaker_mono || frames == 0 || frames > INT_MAX / sizeof(float)) {
        return false;
    }
    if (frames > audio::ControllerPcmBlock::kMaxFrames) {
        return false;
    }
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(ds4_channels_);
    const std::span<float> out(ds4_scratch_.data(), samples);
    if (!SpreadControllerSpeakerMono(std::span<const float>(speaker_mono, frames), out, frames, ds4_channels_)) {
        return false;
    }
    if (SDL_GetAudioStreamQueued(stream_) > 48000 * ds4_channels_ * static_cast<int>(sizeof(float)) / 10) SDL_ClearAudioStream(stream_);
    return SDL_PutAudioStreamData(stream_, out.data(), static_cast<int>(samples * sizeof(float)));
}

bool ControllerSpeakerOutput::WriteDualSense(const float* speaker_stereo, const float* actuator_stereo, uint32_t frames) {
    if (!stream_ || route_ != ControllerPcmRoute::DualSenseQuad || !speaker_stereo || frames == 0 ||
        frames > audio::ControllerPcmBlock::kMaxFrames) {
        return false;
    }
    std::array<float, audio::ControllerPcmBlock::kMaxFrames * 4> quad{};
    const std::span<const float> speaker(speaker_stereo, static_cast<size_t>(frames) * 2);
    const std::span<const float> actuator = actuator_stereo
                                                ? std::span<const float>(actuator_stereo, static_cast<size_t>(frames) * 2)
                                                : std::span<const float>();
    const std::span<float> output(quad.data(), static_cast<size_t>(frames) * 4);
    if (!InterleaveDualSensePcm(speaker, actuator, output, frames)) {
        return false;
    }
    if (SDL_GetAudioStreamQueued(stream_) > 48000 * 4 * static_cast<int>(sizeof(float)) / 10) SDL_ClearAudioStream(stream_);
    return SDL_PutAudioStreamData(stream_, quad.data(), static_cast<int>(output.size() * sizeof(float)));
}

bool ControllerSpeakerOutput::WriteCapturedBlock(const audio::ControllerPcmBlock& block, bool speaker_enabled,
                                                  bool dualsense_haptics_enabled, float speaker_gain, float haptics_gain) {
    if (block.frames == 0 || block.frames > audio::ControllerPcmBlock::kMaxFrames) {
        return false;
    }
    const size_t stereo_samples = static_cast<size_t>(block.frames) * 2;
    const std::span<const float> authored(block.stereo.data(), stereo_samples);
    const std::span<float> speaker_buffer(speaker_scratch_.data(), stereo_samples);
    const std::span<float> haptics_source(haptics_source_scratch_.data(), stereo_samples);
    if (!ScaleControllerPcm(authored, speaker_buffer, haptics_source, block.frames, speaker_enabled, dualsense_haptics_enabled,
                            speaker_gain, haptics_gain)) {
        return false;
    }
    if (speaker_enabled) {
        constexpr float kSpeakerDrive = 8.0f;
        constexpr float kSpeakerCeiling = 0.9f;
        for (size_t i = 0; i + 1 < stereo_samples; i += 2) {
            const float mono = 0.5f * (speaker_buffer[i] + speaker_buffer[i + 1]);
            speaker_buffer[i] = speaker_buffer[i + 1] = kSpeakerCeiling * std::tanh(mono * kSpeakerDrive / kSpeakerCeiling);
        }
    }
    const float haptics_level = dualsense_haptics_enabled && std::isfinite(haptics_gain) ? std::clamp(haptics_gain, 0.0f, 1.0f) : 0.0f;
    for (size_t i = 0; i < stereo_samples; ++i) {
        haptics_source[i] = block.haptic[i] * haptics_level;
    }
    const std::span<const float> speaker(speaker_scratch_.data(), stereo_samples);
    if (route_ == ControllerPcmRoute::DualShock4Mono) {
        const std::span<float> mono(mono_scratch_.data(), block.frames);
        return DownmixControllerSpeakerMono(speaker, mono, block.frames) && WriteMono(mono.data(), block.frames);
    }
    if (route_ == ControllerPcmRoute::DualSenseQuad) {
        const float* actuator = nullptr;
        if (dualsense_haptics_enabled) {
            const std::span<float> haptics(actuator_scratch_.data(), stereo_samples);
            const std::span<const float> source(haptics_source_scratch_.data(), stereo_samples);
            if (!haptic_filter_.Process(source, haptics, block.frames)) {
                return false;
            }
            actuator = actuator_scratch_.data();
        } else {
            haptic_filter_.Reset();
        }
        return WriteDualSense(speaker_scratch_.data(), actuator, block.frames);
    }
    if (route_ == ControllerPcmRoute::DualSenseBluetoothHaptics) {
        if (!dualsense_haptics_enabled) {
            haptic_filter_.Reset();
            return true;
        }
        const std::span<float> haptics(actuator_scratch_.data(), stereo_samples);
        const std::span<const float> source(haptics_source_scratch_.data(), stereo_samples);
        return haptic_filter_.Process(source, haptics, block.frames) && WriteBluetoothHaptics(actuator_scratch_.data(), block.frames);
    }
    return false;
}

}
