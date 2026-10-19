#include "engine/platform/macos_data_path.h"
#include "engine/platform/user_data.h"

#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

bool Alias(const fs::path& target, const fs::path& file) {
    const auto url = [](const fs::path& p, bool directory) {
        const auto native = p.native();
        return CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(native.data()), native.size(), directory);
    };
    CFURLRef targetURL = url(target, true), fileURL = url(file, false);
    CFDataRef data = targetURL ? CFURLCreateBookmarkData(kCFAllocatorDefault, targetURL,
        kCFURLBookmarkCreationSuitableForBookmarkFile, nullptr, nullptr, nullptr) : nullptr;
    const bool written = data && fileURL && CFURLWriteBookmarkDataToFile(data, fileURL, 0, nullptr);
    if (data) CFRelease(data);
    if (fileURL) CFRelease(fileURL);
    if (targetURL) CFRelease(targetURL);
    return written;
}

std::string Read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const fs::path root = fs::absolute(argv[1]) / fs::path(u8"Mac data paths éğ-Ж");
    if (fs::exists(root)) return 2;
    fs::create_directories(root);
    int failures = 0, checks = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks;
        failures += !ok;
        std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    };
    const auto usable = [&](const fs::path& input, const fs::path& expected, const char* label) {
        std::error_code error;
        const auto resolved = pt::platform::ResolveMacDataPath(input, error);
        bool ok = !error && pt::platform::PrepareUserDataDirectory(resolved, {}, false).success;
        if (ok) ok = fs::equivalent(resolved, expected, error) && !error;
        check(ok, label);
        return resolved;
    };
    const fs::path external = root / "External USB volume";
    const fs::path profile = external / "pt-port" / "pt";
    fs::create_directories(profile);
    std::ofstream(profile / "pt.ini") << "existing settings";
    std::ofstream(profile / "PT_Save_Data0") << "existing save";
    usable(root / "fresh" / "pt", root / "fresh" / "pt", "ordinary missing data folder is created");
    usable(profile, profile, "ordinary existing data folder works");

    const fs::path symlink = root / "pt-symlink";
    fs::create_directory_symlink(profile, symlink);
    usable(symlink, profile, "directory symlink resolves to the writable target");
    check(fs::is_symlink(fs::symlink_status(symlink)), "directory symlink is preserved");
    fs::create_directory_symlink(fs::relative(profile, root), root / "relative-link");
    usable(root / "relative-link", profile, "relative symlink resolves");
    fs::create_directory_symlink(external / "pt-port", root / "parent-link");
    usable(root / "parent-link" / "pt", profile, "linked pt-port parent resolves");

    const fs::path alias = root / "pt-alias";
    check(Alias(profile, alias), "Finder directory alias fixture is created");
    const auto aliasBytes = Read(alias);
    usable(alias, profile, "Finder directory alias resolves to the writable target");
    check(!aliasBytes.empty() && Read(alias) == aliasBytes && fs::is_regular_file(fs::symlink_status(alias)),
        "Finder alias file is preserved byte for byte");
    check(Alias(external / "pt-port", root / "parent-alias"), "Finder parent alias fixture is created");
    usable(root / "parent-alias" / "pt", profile, "Finder pt-port parent alias resolves");
    usable(root / "parent-alias" / "new-profile", external / "pt-port" / "new-profile",
        "missing child directory is created inside the parent alias target");
    check(Alias(symlink, root / "mixed-alias"), "mixed link fixture is created");
    usable(root / "mixed-alias", profile, "Finder alias and symlink chain resolves");

    std::error_code error;
    const fs::path missing = root / "unmounted-target";
    fs::create_directory_symlink(missing, root / "broken-link");
    const auto broken = pt::platform::ResolveMacDataPath(root / "broken-link", error);
    check(error && broken.empty() && !fs::exists(missing) && fs::is_symlink(fs::symlink_status(root / "broken-link")),
        "missing symlink target fails without creating it or replacing the link");
    fs::create_directory_symlink("loop-b", root / "loop-a");
    fs::create_directory_symlink("loop-a", root / "loop-b");
    const auto loop = pt::platform::ResolveMacDataPath(root / "loop-a", error);
    check(error == std::errc::too_many_symbolic_link_levels && loop.empty(), "symlink loop fails with a bounded lookup");
    std::ofstream(root / "ordinary-file") << "leave this file alone";
    const auto file = pt::platform::ResolveMacDataPath(root / "ordinary-file", error);
    check(error && file.empty() && Read(root / "ordinary-file") == "leave this file alone", "ordinary file is not a data directory");

    const fs::path readOnly = root / "read-only";
    fs::create_directories(readOnly);
    fs::permissions(readOnly, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
    fs::create_directory_symlink(readOnly, root / "read-only-link");
    const auto readOnlyResolved = pt::platform::ResolveMacDataPath(root / "read-only-link", error);
    check(!error && !pt::platform::PrepareUserDataDirectory(readOnlyResolved, {}, false).success,
        "resolved read-only target still fails its write check");
    fs::permissions(readOnly, fs::perms::owner_all, fs::perm_options::replace);
    check(Read(profile / "pt.ini") == "existing settings" && Read(profile / "PT_Save_Data0") == "existing save",
        "existing settings and saves are unchanged");
    fs::remove_all(root);
    std::printf("macOS data paths: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
