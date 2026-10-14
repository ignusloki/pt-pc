#pragma once

#ifdef __ANDROID__
struct SDL_AudioStream;
namespace pt {
enum class AndroidMicrophoneAccess { Pending, Granted, Denied };
void RequestAndroidMicrophoneAccess(bool allow_prompt = true);
AndroidMicrophoneAccess GetAndroidMicrophoneAccess();
void InitAndroidTouchController();
void ShutdownAndroidTouchController();
void LogAndroidMemoryStatus();
SDL_AudioStream* OpenAndroidPhoneMicrophone(int sample_rate);
void PumpAndroidPhoneMicrophone(SDL_AudioStream* stream);
}
#endif
