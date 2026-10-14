#pragma once

#ifdef __ANDROID__
struct SDL_AudioStream;
namespace pt {
enum class AndroidMicrophoneAccess { Pending, Granted, Denied };
void RequestAndroidMicrophoneAccess(bool allow_prompt = true);
AndroidMicrophoneAccess GetAndroidMicrophoneAccess();
void InitAndroidTouchController();
void ShutdownAndroidTouchController();
enum class AndroidTouchMenuAction { None = -1, Accept, Back, Up, Down, Left, Right, Column, InvertY, InvertX, Close };
AndroidTouchMenuAction TakeAndroidTouchMenuAction();
void SyncAndroidTouchMenu(int mode);
void LogAndroidMemoryStatus();
SDL_AudioStream* OpenAndroidPhoneMicrophone(int sample_rate);
void PumpAndroidPhoneMicrophone(SDL_AudioStream* stream);
}
#endif
