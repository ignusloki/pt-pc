#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include "engine/core/log.h"
#include "engine/platform/macos_permissions.h"

namespace pt {
void RequestMacMicrophoneAccessAtStartup() {
    @autoreleasepool {
        // A bare developer executable has no usage description. macOS terminates
        // a process that requests protected media without this bundle metadata.
        id usage = NSBundle.mainBundle.infoDictionary[@"NSMicrophoneUsageDescription"];
        if (![usage isKindOfClass:NSString.class] || [usage length] == 0) {
            LogWarn("microphone permission: launch the packaged Mac app to request access at startup");
            return;
        }
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
        case AVAuthorizationStatusNotDetermined:
            LogInfo("microphone permission: requesting access at startup");
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                if (granted) LogInfo("microphone permission: access granted");
                else LogWarn("microphone permission: access denied; enable P.T. in System Settings > Privacy & Security > Microphone for the voice puzzle");
            }];
            break;
        case AVAuthorizationStatusAuthorized:
            LogInfo("microphone permission: access already granted");
            break;
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
            LogWarn("microphone permission: unavailable; enable P.T. in System Settings > Privacy & Security > Microphone for the voice puzzle");
            break;
        }
    }
}
}
