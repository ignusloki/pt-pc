#include "engine/platform/android.h"

#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <jni.h>
#include <aaudio/AAudio.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <mutex>
#include "engine/core/memory_status.h"
#include "engine/core/log.h"

namespace {
constexpr const char* kPhoneCapture = "pt.android.phone.capture";
std::mutex capture_mutex;
AAudioStream* phone_capture = nullptr;
std::atomic<bool> app_foreground{true};
std::atomic<uint64_t> surface_generation{0};
std::atomic<bool> touch_enabled{false};
std::atomic<float> axes[4]{};
std::atomic<uint32_t> buttons{0};
std::atomic<Uint64> button_until[SDL_GAMEPAD_BUTTON_COUNT]{};
std::atomic<int> permission{0};
SDL_Joystick* touch_joystick = nullptr;
SDL_JoystickID touch_id = 0;
std::atomic<int> touch_menu_mode{0};
std::mutex menu_mutex;
std::deque<pt::AndroidTouchMenuAction> menu_actions;

void SDLCALL UpdateTouch(void*) {
    if (!touch_joystick) return;
    for (int axis = 0; axis < 4; ++axis)
        SDL_SetJoystickVirtualAxis(touch_joystick, axis, static_cast<Sint16>(std::clamp(axes[axis].load(), -1.0f, 1.0f) * 32767));
    const Uint64 now = SDL_GetTicks();
    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
        SDL_SetJoystickVirtualButton(touch_joystick, button, (buttons.load() & (1u << button)) != 0 || now < button_until[button].load());
}

bool PhoneRumbleSupported() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return false;
    const jclass cls = env->GetObjectClass(activity);
    const jmethodID method = env->GetMethodID(cls, "phoneRumbleSupported", "()Z");
    bool supported = method && env->CallBooleanMethod(activity, method);
    if (env->ExceptionCheck()) { env->ExceptionClear(); supported = false; }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return supported;
}

bool SDLCALL RumbleTouch(void*, Uint16 low, Uint16 high) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return false;
    const jclass cls = env->GetObjectClass(activity);
    const jmethodID method = env->GetMethodID(cls, "rumblePhone", "(II)Z");
    bool played = method && env->CallBooleanMethod(activity, method, static_cast<jint>(low), static_cast<jint>(high));
    if (env->ExceptionCheck()) { env->ExceptionClear(); played = false; }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return played;
}

bool PermissionGranted() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return false;
    const jclass cls = env->GetObjectClass(activity);
    const jmethodID method = env->GetMethodID(cls, "microphonePermissionGranted", "()Z");
    bool granted = method && env->CallBooleanMethod(activity, method);
    if (env->ExceptionCheck()) { env->ExceptionClear(); granted = false; }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return granted;
}

void SDLCALL ClosePhoneCapture(void*, void* value) {
    auto* capture = static_cast<AAudioStream*>(value);
    std::lock_guard lock(capture_mutex);
    if (phone_capture == capture) phone_capture = nullptr;
    AAudioStream_requestStop(capture);
    AAudioStream_close(capture);
}

int PhoneMicrophoneDevice() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return -1;
    const jclass cls = env->GetObjectClass(activity);
    const jmethodID method = env->GetMethodID(cls, "phoneMicrophoneDeviceId", "()I");
    int id = method ? env->CallIntMethod(activity, method) : -1;
    if (env->ExceptionCheck()) { env->ExceptionClear(); id = -1; }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return id;
}
}

extern "C" JNIEXPORT void JNICALL Java_org_ignusloki_pt_prototype_GameActivity_configure(JNIEnv* env, jclass, jstring libraries, jboolean touch) {
    const char* path = env->GetStringUTFChars(libraries, nullptr);
    if (path) { setenv("PT_ANDROID_NATIVE_LIBRARIES", path, 1); env->ReleaseStringUTFChars(libraries, path); }
    touch_enabled = touch;
    setenv("PT_STATUS_LOG", "1", 1);
}

extern "C" JNIEXPORT void JNICALL Java_org_ignusloki_pt_prototype_GameActivity_touchStick(JNIEnv*, jclass, jint stick, jfloat x, jfloat y) {
    if (stick < 0 || stick > 1 || !std::isfinite(x) || !std::isfinite(y)) return;
    axes[stick * 2] = x;
    axes[stick * 2 + 1] = y;
}

extern "C" JNIEXPORT void JNICALL Java_org_ignusloki_pt_prototype_GameActivity_touchButton(JNIEnv*, jclass, jint button, jboolean pressed) {
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) return;
    if (pressed) {
        // A brief Android tap can begin/end between 30 Hz polls. Keep its down state long enough to observe one press.
        button_until[button] = SDL_GetTicks() + 80;
        buttons.fetch_or(1u << button);
    }
    else buttons.fetch_and(~(1u << button));
}

extern "C" JNIEXPORT void JNICALL Java_org_ignusloki_pt_prototype_GameActivity_resetTouch(JNIEnv*, jclass) {
    for (auto& axis : axes) axis = 0;
    for (auto& deadline : button_until) deadline = 0;
    buttons = 0;
    std::lock_guard lock(menu_mutex);
    menu_actions.clear();
}

extern "C" JNIEXPORT void JNICALL Java_org_ignusloki_pt_prototype_GameActivity_menuAction(JNIEnv*, jclass, jint action) {
    if (!touch_enabled || touch_menu_mode == 0 || action < 0 || action > static_cast<int>(pt::AndroidTouchMenuAction::Close)) return;
    std::lock_guard lock(menu_mutex);
    // Retain individual taps until the menu can accept them, including during its opening animation.
    if (menu_actions.size() < 64) menu_actions.push_back(static_cast<pt::AndroidTouchMenuAction>(action));
}

extern "C" JNIEXPORT void JNICALL Java_org_ignusloki_pt_prototype_GameActivity_captureForeground(JNIEnv*, jclass, jboolean foreground) {
    std::lock_guard lock(capture_mutex);
    if (app_foreground.exchange(foreground) && !foreground) ++surface_generation;
    if (phone_capture) {
        if (foreground) AAudioStream_requestStart(phone_capture);
        else AAudioStream_requestStop(phone_capture);
    }
}

namespace pt {
bool AndroidAppForeground() { return app_foreground.load(); }
uint64_t AndroidSurfaceGeneration() { return surface_generation.load(); }

AndroidTouchMenuAction TakeAndroidTouchMenuAction() {
    std::lock_guard lock(menu_mutex);
    if (menu_actions.empty()) return AndroidTouchMenuAction::None;
    const auto action = menu_actions.front();
    menu_actions.pop_front();
    return action;
}

void SyncAndroidTouchMenu(int mode) {
    if (!touch_enabled || touch_menu_mode.exchange(mode) == mode) return;
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    const jclass cls = env->GetObjectClass(activity);
    const jmethodID method = env->GetMethodID(cls, "setTouchMenuMode", "(I)V");
    if (method) env->CallVoidMethod(activity, method, mode);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    LogInfo("android: touch overlay {}", mode == 0 ? "gameplay" : mode == 1 ? "original settings" : "PC settings");
}

void InitAndroidTouchController() {
    if (!touch_enabled || touch_id) return;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "P.T. Android touch controller";
    desc.Update = UpdateTouch;
    const bool phone_rumble = PhoneRumbleSupported();
    if (phone_rumble) desc.Rumble = RumbleTouch;
    touch_id = SDL_AttachVirtualJoystick(&desc);
    if (touch_id) touch_joystick = SDL_OpenJoystick(touch_id);
    if (touch_joystick) {
        SDL_SetJoystickVirtualAxis(touch_joystick, 4, SDL_JOYSTICK_AXIS_MIN);
        SDL_SetJoystickVirtualAxis(touch_joystick, 5, SDL_JOYSTICK_AXIS_MIN);
        LogInfo("android: optional touch controller attached through SDL's existing gamepad mapping");
        LogInfo("android: phone vibration {} via touch controller", phone_rumble ? "available" : "unavailable");
    } else LogError("android: touch controller unavailable: {}", SDL_GetError());
}

void ShutdownAndroidTouchController() {
    if (touch_joystick) SDL_CloseJoystick(touch_joystick);
    touch_joystick = nullptr;
    if (touch_id) SDL_DetachVirtualJoystick(touch_id);
    touch_id = 0;
}

AndroidMicrophoneAccess GetAndroidMicrophoneAccess() {
    if (PermissionGranted()) return AndroidMicrophoneAccess::Granted;
    return permission == 1 ? AndroidMicrophoneAccess::Pending : AndroidMicrophoneAccess::Denied;
}

void RequestAndroidMicrophoneAccess(bool allow_prompt) {
    if (PermissionGranted()) {
        permission = 2;
        LogInfo("microphone: Android access granted before gameplay");
        return;
    }
    // A denied request in the launcher stays denied at startup. An explicit menu toggle may request again.
    if (!allow_prompt) { permission = 3; return; }
    if (permission.exchange(1) == 1) return;
    if (!SDL_RequestAndroidPermission("android.permission.RECORD_AUDIO", [](void*, const char*, bool granted) {
            permission = granted ? 2 : 3;
            LogInfo("microphone: Android permission {}", granted ? "granted" : "denied");
        }, nullptr)) {
        permission = 3;
        LogWarn("microphone: Android permission request failed: {}", SDL_GetError());
    }
}

void LogAndroidMemoryStatus() {
    unsigned long long rss_kib = 0;
    if (FILE* file = std::fopen("/proc/self/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), file)) if (std::sscanf(line, "VmRSS: %llu kB", &rss_kib) == 1) break;
        std::fclose(file);
    }
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    jlong pss_kib = -1;
    jint thermal = -1;
    if (env && activity) {
        const jclass cls = env->GetObjectClass(activity);
        const jmethodID pss_method = env->GetMethodID(cls, "memoryPssKiB", "()J");
        if (pss_method) pss_kib = env->CallLongMethod(activity, pss_method);
        const jmethodID thermal_method = env->GetMethodID(cls, "thermalStatus", "()I");
        if (thermal_method) thermal = env->CallIntMethod(activity, thermal_method);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
    }
    LogInfo("android memory: process PSS {:.0f} MiB, RSS {:.0f} MiB, system available {:.0f} MiB, thermal status {} (shared GPU memory overlaps process memory)",
        static_cast<double>(pss_kib) / 1024, static_cast<double>(rss_kib) / 1024, QuerySystemMemory().free_physical_mb, thermal);
}

SDL_AudioStream* OpenAndroidPhoneMicrophone(int sample_rate) {
    // SDL 3.4 disables multiple Android audio devices internally. Use AAudio's explicit device routing for input,
    // and keep SDL's stream converter/monitor and the game's existing speech recognizer.
    const int device = PhoneMicrophoneDevice();
    if (!PermissionGranted() || device < 0) {
        LogWarn("microphone: Android phone microphone or permission unavailable");
        return nullptr;
    }
    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) return nullptr;
    AAudioStreamBuilder_setDeviceId(builder, device);
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setSampleRate(builder, sample_rate);
    AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
    AAudioStream* capture = nullptr;
    result = AAudioStreamBuilder_openStream(builder, &capture);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        LogWarn("microphone: AAudio phone input failed: {}", AAudio_convertResultToText(result));
        return nullptr;
    }
    if (AAudioStream_getDeviceId(capture) != device || AAudioStream_getChannelCount(capture) != 1 ||
        AAudioStream_getFormat(capture) != AAUDIO_FORMAT_PCM_I16) {
        LogWarn("microphone: AAudio did not provide the requested built-in mono input");
        AAudioStream_close(capture);
        return nullptr;
    }
    const int actual_rate = AAudioStream_getSampleRate(capture);
    SDL_AudioSpec source{SDL_AUDIO_S16, 1, actual_rate}, target{SDL_AUDIO_S16, 1, sample_rate};
    SDL_AudioStream* stream = SDL_CreateAudioStream(&source, &target);
    if (!stream) { AAudioStream_close(capture); return nullptr; }
    if (!SDL_SetPointerPropertyWithCleanup(SDL_GetAudioStreamProperties(stream), kPhoneCapture, capture, ClosePhoneCapture, nullptr)) {
        // SDL runs the cleanup callback on failure too; the AAudio stream is already closed.
        SDL_DestroyAudioStream(stream);
        return nullptr;
    }
    result = AAudioStream_requestStart(capture);
    if (result != AAUDIO_OK) {
        LogWarn("microphone: phone capture could not start: {}", AAudio_convertResultToText(result));
        SDL_DestroyAudioStream(stream);
        return nullptr;
    }
    {
        std::lock_guard lock(capture_mutex);
        phone_capture = capture;
        if (!app_foreground) AAudioStream_requestStop(capture);
    }
    LogInfo("microphone: built-in phone device {}, capture {} Hz -> {} Hz mono, AAudio", device, actual_rate, sample_rate);
    return stream;
}

void PumpAndroidPhoneMicrophone(SDL_AudioStream* stream) {
    auto* capture = static_cast<AAudioStream*>(SDL_GetPointerProperty(SDL_GetAudioStreamProperties(stream), kPhoneCapture, nullptr));
    if (!capture) return;
    std::lock_guard lock(capture_mutex);
    if (!app_foreground) { SDL_ClearAudioStream(stream); return; }
    int16_t samples[512];
    for (int block = 0; block < 8; ++block) {
        const aaudio_result_t frames = AAudioStream_read(capture, samples, 512, 0);
        if (frames <= 0) break;
        SDL_PutAudioStreamData(stream, samples, static_cast<int>(frames) * sizeof(int16_t));
    }
}
}
#endif
