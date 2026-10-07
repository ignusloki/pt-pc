#import <Cocoa/Cocoa.h>
#include <CommonCrypto/CommonDigest.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "engine/platform/os.h"
#include "pt_version.h"
#include "setup_core.h"

using namespace pt::setup;
namespace {
std::atomic<bool> cancelled{false};
NSString* Text(const std::string& text) { return [NSString stringWithUTF8String:text.c_str()] ?: @""; }
fs::path Path(NSURL* url) { return url ? fs::path(url.fileSystemRepresentation) : fs::path(); }
fs::path Resources() { return fs::path(NSBundle.mainBundle.resourcePath.fileSystemRepresentation); }

std::string HashFile(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + file.filename().string());
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    char buffer[65536];
    while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
        CheckCancel();
        CC_SHA256_Update(&context, buffer, static_cast<CC_LONG>(input.gcount()));
    }
    if (!input.eof()) throw std::runtime_error("Could not verify file integrity.");
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    const char* hex = "0123456789abcdef";
    std::string result;
    for (auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
void VerifyPayload() {
    std::ifstream file(Resources() / "payload.sha256");
    std::string expected;
    file >> expected;
    if (expected.size() != 64 || HashFile(Resources() / "payload.bin") != expected)
        throw std::runtime_error("This installer payload is damaged. Please obtain a fresh copy.");
}
std::vector<unsigned char> ReadPayload() {
    std::ifstream file(Resources() / "payload.bin", std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size < 12 || size > 1024ll * 1024 * 1024) throw std::runtime_error("Installer payload missing or invalid.");
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("Cannot read installer payload.");
    return bytes;
}
void CheckParents(const fs::path& destination) {
    for (auto p = fs::absolute(destination); !p.empty();) {
        std::error_code error;
        if (fs::is_symlink(p, error)) throw std::runtime_error("Choose an installation location without symbolic links.");
        const auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
}
void MarkExecutables(const fs::path& root) {
    const auto bits = fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
    for (const char* name : {kGameExe, "extractor/PT.PkgExtract"}) {
        if (!fs::is_regular_file(root / name)) throw std::runtime_error("Incomplete macOS runtime: " + std::string(name));
        fs::permissions(root / name, bits, fs::perm_options::add);
    }
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream file(entry.path(), std::ios::binary);
        char magic[4]{};
        file.read(magic, sizeof(magic));
        // The self-contained .NET runtime also includes Mach-O tools such as
        // createdump, which have no extension and must keep their execute bits.
        for (const char* header : {"\xcf\xfa\xed\xfe", "\xfe\xed\xfa\xcf", "\xca\xfe\xba\xbe",
                                  "\xbe\xba\xfe\xca", "\xca\xfe\xba\xbf", "\xbf\xba\xfe\xca"}) {
            if (std::memcmp(magic, header, sizeof(magic)) == 0) {
                fs::permissions(entry.path(), bits, fs::perm_options::add);
                break;
            }
        }
    }
}
void VerifyRuntime(const fs::path& root) {
    const auto log = root / "install-verification.log";
    auto verify = [&](const fs::path& program, const std::vector<std::string>& args, int expected) {
        const auto result = pt::os::RunProcess(program, args, root, log, cancelled, std::chrono::minutes(2));
        CheckCancel();
        if (!result.started || result.timed_out || result.exit_code != expected) {
            std::ifstream input(log);
            const std::string details((std::istreambuf_iterator<char>(input)), {});
            throw std::runtime_error("Could not verify the unpacked Mac runtime. " + details.substr(0, 1200));
        }
    };
    verify("/usr/bin/codesign", {"--verify", "--deep", "--strict", (root / "P.T..app").string()}, 0);
    verify("/usr/bin/codesign", {"--verify", "--strict", (root / "extractor/PT.PkgExtract").string()}, 0);
    verify(root / kGameExe, {"--help"}, 0);
    verify(root / "extractor/PT.PkgExtract", {}, 1);
    std::ifstream input(log);
    const std::string details((std::istreambuf_iterator<char>(input)), {});
    if (details.find("Usage: PT.PkgExtract") == std::string::npos)
        throw std::runtime_error("The unpacked PKG helper did not start correctly. " + details.substr(0, 1200));
}
void Extract(const fs::path& staging, const fs::path& package) {
    const auto result = pt::os::RunProcess(staging / "extractor/PT.PkgExtract",
        {fs::absolute(package).string(), (staging / "CUSA01127").string()}, staging,
        staging / "install-extraction.log", cancelled, std::chrono::hours(2));
    CheckCancel();
    if (!result.started) throw std::runtime_error("Could not start the Apple Silicon PKG extraction helper.");
    if (result.exit_code != 0) {
        std::ifstream log(staging / "install-extraction.log");
        const std::string details((std::istreambuf_iterator<char>(log)), {});
        throw std::runtime_error("PKG extraction failed. " + details.substr(0, 1200));
    }
}
InstallOutcome Install(const fs::path& input, const fs::path& destination) {
    InstallSteps steps;
    steps.version = PT_VERSION;
    steps.unique_id = NSUUID.UUID.UUIDString.UTF8String;
    steps.check_parents = CheckParents;
    steps.verify_integrity = VerifyPayload;
    steps.whole_trees = {"P.T..app"};
    auto payload = std::make_shared<std::vector<unsigned char>>();
    steps.payload_bytes = [payload] {
        if (payload->empty()) *payload = ReadPayload();
        return PayloadBytes(payload->data(), payload->size());
    };
    steps.unpack = [payload](const fs::path& staging) {
        auto files = UnpackPayload(payload->data(), payload->size(), staging, HashFile);
        MarkExecutables(staging);
        VerifyRuntime(staging);
        files.push_back({"install-verification.log", HashFile(staging / "install-verification.log")});
        return files;
    };
    steps.extract = Extract;
    return RunInstall(input, destination, false, steps);
}
bool Alert(NSString* title, NSString* message, NSString* action, bool can_cancel = true) {
    NSAlert* alert = [NSAlert new];
    alert.messageText = title;
    alert.informativeText = message;
    [alert addButtonWithTitle:action];
    if (can_cancel) [alert addButtonWithTitle:@"Cancel"];
    return [alert runModal] == NSAlertFirstButtonReturn;
}
int CommandLine(int argc, char** argv) {
    hooks.cancelled = [] { return cancelled.load(); };
    hooks.report = [](const std::string& text) { std::fprintf(stderr, "%s\n", text.c_str()); };
    if (argc == 5 && std::string(argv[1]) == "--install") {
        try {
            const auto outcome = Install(Utf8Path(argv[2]), Utf8Path(argv[3]));
            std::ofstream(argv[4]) << "PASS " << (outcome.updated ? "updated" : "installed") << '\n';
            return 0;
        } catch (const std::exception& e) {
            std::ofstream(argv[4]) << "FAIL " << e.what() << '\n';
            std::fprintf(stderr, "%s\n", e.what());
            return 1;
        }
    }
    if (argc == 3 && std::string(argv[1]) == "--self-test") {
        const fs::path root = fs::path(NSTemporaryDirectory().fileSystemRepresentation) /
            ("pt-macos-test-" + std::string(NSUUID.UUID.UUIDString.UTF8String));
        try {
            VerifyPayload();
            const auto payload = ReadPayload();
            const auto runtime = root / "unpacked";
            UnpackPayload(payload.data(), payload.size(), runtime, HashFile);
            MarkExecutables(runtime);
            VerifyRuntime(runtime);
            const auto failures = SelfTestUpdate(root / "files") + SelfTestBundleUpdate(root / "bundles");
            fs::remove_all(root);
            std::ofstream(argv[2]) << (failures.empty() ? "PASS" : "FAIL " + failures) << '\n';
            return failures.empty() ? 0 : 1;
        } catch (const std::exception& e) {
            fs::remove_all(root);
            std::ofstream(argv[2]) << "FAIL " << e.what() << '\n';
            return 1;
        }
    }
    std::fprintf(stderr, "Usage: pt_setup_macos [--install <PKG or folder> <destination> <result file> | --self-test <result file>]\n");
    return 2;
}
}

@interface PTCancelTarget : NSObject
- (void)cancel:(id)sender;
@end
@implementation PTCancelTarget
- (void)cancel:(id)sender { cancelled = true; [sender setEnabled:NO]; }
@end

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc > 1) return CommandLine(argc, argv);
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        [NSApp activateIgnoringOtherApps:YES];
        if (!Alert(@"Install P.T. for Mac", @"Select your local P.T. PS4 fake PKG or decrypted dump folder, then an installation location. The installer creates a P.T. PC Port folder containing the app and your game archives.", @"Continue")) return 0;
        NSOpenPanel* source = [NSOpenPanel openPanel];
        source.title = @"Choose your P.T. PKG or dumped game folder";
        source.canChooseFiles = YES;
        source.canChooseDirectories = YES;
        source.allowsMultipleSelection = NO;
        if ([source runModal] != NSModalResponseOK) return 0;
        const auto input = Path(source.URL);
        NSOpenPanel* location = [NSOpenPanel openPanel];
        location.title = @"Choose where to create the P.T. PC Port folder";
        location.message = @"Choose Applications in your home folder, or another writable folder.";
        location.canChooseFiles = NO;
        location.canChooseDirectories = YES;
        location.canCreateDirectories = YES;
        location.directoryURL = [NSURL fileURLWithPath:[NSHomeDirectory() stringByAppendingPathComponent:@"Applications"]];
        if ([location runModal] != NSModalResponseOK) return 0;
        const auto destination = Path(location.URL) / "P.T. PC Port";
        try {
            const auto old = InspectInstall(destination);
            if (old.found && !Alert(@"Update P.T.", Text(UpdateQuestion(old, PT_VERSION)), @"Continue")) return 0;
        } catch (const std::exception& e) { Alert(@"Cannot install here", Text(e.what()), @"OK", false); return 1; }

        NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 540, 155)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        window.title = @"Installing P.T. for Mac";
        NSTextField* label = [NSTextField labelWithString:@"Preparing installation..."];
        label.frame = NSMakeRect(24, 105, 490, 25);
        NSProgressIndicator* bar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(24, 67, 490, 20)];
        bar.indeterminate = NO;
        bar.minValue = 0;
        bar.maxValue = 100;
        PTCancelTarget* target = [PTCancelTarget new];
        NSButton* button = [NSButton buttonWithTitle:@"Cancel" target:target action:@selector(cancel:)];
        button.frame = NSMakeRect(420, 20, 94, 30);
        [window.contentView addSubview:label];
        [window.contentView addSubview:bar];
        [window.contentView addSubview:button];
        [window center];
        [window makeKeyAndOrderFront:nil];
        hooks.cancelled = [] { return cancelled.load(); };
        hooks.report = [label](const std::string& text) {
            NSString* message = Text(text);
            dispatch_async(dispatch_get_main_queue(), ^{ label.stringValue = message; });
        };
        hooks.progress = [bar](uint64_t done, uint64_t total) {
            const double percent = total ? double(done) * 100 / double(total) : 0;
            dispatch_async(dispatch_get_main_queue(), ^{ bar.doubleValue = percent; });
        };
        std::atomic<bool> finished{false};
        std::string error;
        InstallOutcome outcome;
        std::thread worker([&] {
            @autoreleasepool {
                try { outcome = Install(input, destination); }
                catch (const std::exception& e) { error = e.what(); }
                catch (...) { error = "Installation failed unexpectedly."; }
                finished = true;
            }
        });
        while (!finished) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]
                inMode:NSDefaultRunLoopMode dequeue:YES];
            if (event) [NSApp sendEvent:event];
            [NSApp updateWindows];
        }
        worker.join();
        [window orderOut:nil];
        if (!error.empty()) { Alert(@"Installation stopped", Text(error), @"OK", false); return 1; }
        NSString* message = Text("Open P.T..app in " + destination.string() + ".");
        if (!outcome.notes.empty()) message = [message stringByAppendingString:@" See install-notes.txt for notes about this copy's game data."];
        if (Alert(@"P.T. is installed", message, @"Show in Finder"))
            [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:Text((destination / "P.T..app").string())]]];
        return 0;
    }
}
