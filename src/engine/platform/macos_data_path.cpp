#include "engine/platform/macos_data_path.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>

#include <memory>
#include <type_traits>
#include <vector>

namespace pt::platform {
namespace {
namespace fs = std::filesystem;

struct ReleaseCF {
    void operator()(CFTypeRef value) const { CFRelease(value); }
};
template<class T> using CFHandle = std::unique_ptr<std::remove_pointer_t<T>, ReleaseCF>;

void SetCFError(CFErrorRef failure, std::error_code& error) {
    error = failure && CFEqual(CFErrorGetDomain(failure), kCFErrorDomainPOSIX)
        ? std::error_code(static_cast<int>(CFErrorGetCode(failure)), std::generic_category())
        : std::make_error_code(std::errc::io_error);
}

bool AliasTarget(const fs::path& path, fs::path& target, std::error_code& error) {
    const auto native = path.native();
    CFHandle<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(native.data()), native.size(), false));
    if (!url) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    CFTypeRef value = nullptr;
    CFErrorRef failure = nullptr;
    const bool inspected = CFURLCopyResourcePropertyForKey(url.get(), kCFURLIsAliasFileKey, &value, &failure);
    CFHandle<CFErrorRef> inspectError(failure);
    const bool alias = value && CFEqual(value, kCFBooleanTrue);
    if (value) CFRelease(value);
    if (!inspected) {
        SetCFError(failure, error);
        return false;
    }
    if (!alias) return false;

    failure = nullptr;
    CFHandle<CFDataRef> bookmark(CFURLCreateBookmarkDataFromFile(kCFAllocatorDefault, url.get(), &failure));
    CFHandle<CFErrorRef> bookmarkError(failure);
    if (!bookmark) {
        SetCFError(failure, error);
        return false;
    }
    failure = nullptr;
    Boolean stale = false;
    CFHandle<CFURLRef> resolved(CFURLCreateByResolvingBookmarkData(kCFAllocatorDefault, bookmark.get(),
        kCFBookmarkResolutionWithoutUIMask | kCFBookmarkResolutionWithoutMountingMask,
        nullptr, nullptr, &stale, &failure));
    CFHandle<CFErrorRef> resolveError(failure);
    if (!resolved) {
        SetCFError(failure, error);
        return false;
    }
    CFHandle<CFStringRef> text(CFURLCopyFileSystemPath(resolved.get(), kCFURLPOSIXPathStyle));
    if (!text) {
        error = std::make_error_code(std::errc::invalid_argument);
        return false;
    }
    std::vector<char> bytes(CFStringGetMaximumSizeForEncoding(CFStringGetLength(text.get()), kCFStringEncodingUTF8) + 1);
    if (!CFStringGetCString(text.get(), bytes.data(), bytes.size(), kCFStringEncodingUTF8)) {
        error = std::make_error_code(std::errc::illegal_byte_sequence);
        return false;
    }
    target = fs::path(bytes.data());
    return true;
}

fs::path Resolve(const fs::path& path, unsigned& linksLeft, std::error_code& error) {
    const auto absolute = fs::absolute(path, error);
    if (error) return {};
    fs::path current = absolute.root_path();
    for (const auto& component : absolute.relative_path()) {
        if (component == ".") continue;
        if (component == "..") {
            current = current.parent_path();
            continue;
        }
        current /= component;
        const auto status = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            continue;
        }
        if (error) return {};
        fs::path target;
        bool linked = fs::is_symlink(status);
        if (linked) {
            target = fs::read_symlink(current, error);
            if (error) return {};
            if (target.is_relative()) target = current.parent_path() / target;
        } else if (fs::is_regular_file(status)) {
            linked = AliasTarget(current, target, error);
            if (error) return {};
        }
        if (linked) {
            if (!linksLeft) {
                error = std::make_error_code(std::errc::too_many_symbolic_link_levels);
                return {};
            }
            --linksLeft;
            current = Resolve(target, linksLeft, error);
            if (error) return {};
            if (!fs::is_directory(current, error)) {
                if (!error) error = std::make_error_code(std::errc::not_a_directory);
                return {};
            }
        } else if (fs::exists(status) && !fs::is_directory(status)) {
            error = std::make_error_code(std::errc::not_a_directory);
            return {};
        }
    }
    return current;
}
}

fs::path ResolveMacDataPath(const fs::path& path, std::error_code& error) {
    error.clear();
    unsigned linksLeft = 32;
    return Resolve(path, linksLeft, error);
}
}
#endif
