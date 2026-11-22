#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <mach/mach_time.h>
#include <SDL3/SDL.h>
#include <fstream>
#include <unistd.h>

#include "engine/core/log.h"
#include "engine/render/frame_signature.h"
#include "engine/render/macos_hdr.h"

// All stream state and file writes run on one serial queue. The main thread only
// supplies the game's own window number and initiates the lifetime of the observer.
@interface PTWindowTrace : NSObject <SCStreamOutput, SCStreamDelegate> {
    dispatch_queue_t _queue;
    SCStream* _stream;
    std::filesystem::path _directory;
    std::ofstream _csv;
    uint64_t _count;
    bool _stopped;
}
- (instancetype)initWithWindow:(CGWindowID)window directory:(const std::filesystem::path&)directory;
- (void)stop;
@end

@implementation PTWindowTrace
- (instancetype)initWithWindow:(CGWindowID)window directory:(const std::filesystem::path&)directory {
    self = [super init];
    if (!self) return nil;
    _directory = directory;
    _queue = dispatch_queue_create("pt.diagnostics.game-window", DISPATCH_QUEUE_SERIAL);
    if (@available(macOS 14.4, *)) {
        [SCShareableContent getCurrentProcessShareableContentWithCompletionHandler:^(SCShareableContent* content, NSError* error) {
            dispatch_async(self->_queue, ^{
                if (self->_stopped) return;
                for (SCWindow* w in content.windows) {
                    if (w.windowID != window || w.owningApplication.processID != getpid()) continue;
                    SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:w];
                    SCStreamConfiguration* config = [SCStreamConfiguration new];
                    config.width = 128;
                    config.height = std::clamp<NSInteger>(std::lround(128 * w.frame.size.height / std::max(1.0, w.frame.size.width)), 1, 160);
                    config.minimumFrameInterval = CMTimeMake(1, 120);
                    config.pixelFormat = kCVPixelFormatType_32BGRA;
                    config.colorSpaceName = kCGColorSpaceSRGB;
                    config.showsCursor = NO;
                    config.capturesAudio = NO;
                    config.includeChildWindows = NO;
                    config.ignoreShadowsSingleWindow = YES;
                    self->_stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:self];
                    NSError* addError = nil;
                    if (![self->_stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:self->_queue error:&addError]) {
                        pt::LogWarn("frame observer: game window output unavailable ({})", int(addError.code));
                        self->_stream = nil;
                        return;
                    }
                    [self->_stream startCaptureWithCompletionHandler:^(NSError* startError) {
                        if (startError) pt::LogWarn("frame observer: game window capture unavailable ({})", int(startError.code));
                        else pt::LogInfo("frame observer: started on the game's own window {}, SDR sample codes, no audio", window);
                    }];
                    return;
                }
                pt::LogWarn("frame observer: game window {} unavailable to this process ({})", window, int(error.code));
            });
        }];
    } else {
        pt::LogInfo("frame observer: own-window sampling requires macOS 14.4");
    }
    return self;
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    pt::LogInfo("frame observer: stream stopped ({})", int(error.code));
}

- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type {
    if (_stopped || type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample)) return;
    const auto attachments = (__bridge NSArray*)CMSampleBufferGetSampleAttachmentsArray(sample, false);
    NSDictionary* info = attachments.firstObject;
    NSNumber* statusNumber = info[SCStreamFrameInfoStatus];
    if (!statusNumber) return;
    const NSInteger status = statusNumber.integerValue;
    static constexpr const char* names[]{"complete", "idle", "blank", "suspended", "started", "stopped"};
    if (_count % 8192 == 0) {
        const auto segment = _count / 8192;
        _csv.close();
        _csv.open(_directory / std::format("window-{}.csv", segment ? 1 + (segment - 1) % 2 : 0));
        if (!_csv) { pt::LogWarn("frame observer: cannot open window log"); _stopped = true; return; }
        _csv << "sample,log_seconds,display_mach_ticks,status,width,height,unit," << pt::FrameSignature::Header << '\n';
    }
    pt::FrameSignature signature;
    CVPixelBufferRef buffer = CMSampleBufferGetImageBuffer(sample);
    size_t width = 0, height = 0;
    if ((status == SCFrameStatusComplete || status == SCFrameStatusStarted) && buffer &&
        CVPixelBufferGetPixelFormatType(buffer) == kCVPixelFormatType_32BGRA &&
        CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess) {
        width = CVPixelBufferGetWidth(buffer);
        height = CVPixelBufferGetHeight(buffer);
        const auto* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
        for (size_t y = 0; base && y < height; ++y) {
            const auto* p = base + y * CVPixelBufferGetBytesPerRow(buffer);
            for (size_t x = 0; x < width; ++x, p += 4) {
                const unsigned cell = unsigned(y * signature.Rows / height * signature.Columns + x * signature.Columns / width);
                signature.Add(cell, (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0], p[2], p[1], p[0], 1.0f);
            }
        }
        CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    }
    const uint64_t timestamp = [info[SCStreamFrameInfoDisplayTime] unsignedLongLongValue];
    _csv << std::format("{},{:.6f},{},{},{},{},sdr_code_0_255,", ++_count, pt::LogClock(), timestamp,
                        status >= 0 && status < 6 ? names[status] : "unknown", width, height);
    signature.Write(_csv);
    if ((_count % 16 == 0) || status == SCFrameStatusBlank) _csv.flush();
}

- (void)stop {
    dispatch_sync(_queue, ^{
        self->_stopped = true;
        if (self->_stream) {
            [self->_stream removeStreamOutput:self type:SCStreamOutputTypeScreen error:nil];
            [self->_stream stopCaptureWithCompletionHandler:nil];
            self->_stream = nil;
        }
        self->_csv.close();
    });
}
@end

namespace pt::vk {
class MacWindowTrace {
public:
    explicit MacWindowTrace(PTWindowTrace* trace) : trace_(trace) {}
    ~MacWindowTrace() { [trace_ stop]; }
private:
    PTWindowTrace* trace_;
};

std::shared_ptr<MacWindowTrace> StartMacWindowTrace(SDL_Window* window, const std::filesystem::path& directory) {
    NSWindow* native = (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!native || native.windowNumber <= 0) return {};
    mach_timebase_info_data_t timebase{};
    mach_timebase_info(&timebase);
    LogInfo("frame observer: requested window {}, mach ticks {} at {:.6f} log seconds, timebase {}/{} ns", native.windowNumber,
            mach_absolute_time(), LogClock(), timebase.numer, timebase.denom);
    return std::make_shared<MacWindowTrace>([[PTWindowTrace alloc] initWithWindow:CGWindowID(native.windowNumber) directory:directory]);
}
}
