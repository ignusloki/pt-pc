#pragma once

namespace pt {

enum class MacMicrophoneAccess { Unavailable, NotDetermined, Pending, Authorized, Denied };

MacMicrophoneAccess GetMacMicrophoneAccess();
// Requests permission only; recording starts later when gameplay or the microphone test needs it.
void RequestMacMicrophoneAccess();

}
