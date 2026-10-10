#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <atomic>

#include "engine/core/log.h"
#include "engine/platform/macos_permissions.h"

namespace pt {
namespace {
std::atomic<bool> request_pending{false};

bool HasMicrophoneUsageDescription() {
    id usage = NSBundle.mainBundle.infoDictionary[@"NSMicrophoneUsageDescription"];
    return [usage isKindOfClass:NSString.class] && [usage length] > 0;
}
}

MacMicrophoneAccess GetMacMicrophoneAccess() {
    @autoreleasepool {
        // Bare developer executables cannot request protected media without bundle metadata.
        if (!HasMicrophoneUsageDescription()) return MacMicrophoneAccess::Unavailable;
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
        case AVAuthorizationStatusAuthorized: return MacMicrophoneAccess::Authorized;
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted: return MacMicrophoneAccess::Denied;
        case AVAuthorizationStatusNotDetermined:
            return request_pending.load() ? MacMicrophoneAccess::Pending : MacMicrophoneAccess::NotDetermined;
        }
        return MacMicrophoneAccess::Denied;
    }
}

void RequestMacMicrophoneAccess() {
    @autoreleasepool {
        const auto access = GetMacMicrophoneAccess();
        if (access == MacMicrophoneAccess::Unavailable) {
            LogWarn("microphone permission: launch the packaged Mac app to request access");
        } else if (access == MacMicrophoneAccess::Authorized) {
            LogInfo("microphone permission: access already granted");
        } else if (access == MacMicrophoneAccess::Denied) {
            LogWarn("microphone permission: blocked; enable P.T. in System Settings > Privacy & Security > Microphone");
        } else if (access == MacMicrophoneAccess::NotDetermined && !request_pending.exchange(true)) {
            LogInfo("microphone permission: requesting access before gameplay");
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                request_pending.store(false);
                if (granted) LogInfo("microphone permission: access granted");
                else LogWarn("microphone permission: access denied; gameplay continues without microphone input");
            }];
        }
    }
}
}
