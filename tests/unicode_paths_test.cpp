#include "engine/core/ascii.h"
#include "engine/core/log.h"
#include "engine/platform/os.h"
#include "engine/platform/settings.h"
#include "engine/platform/user_data.h"
#include "game/save_data.h"
#include "setup_core.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <clocale>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

namespace fs = std::filesystem;
namespace {

int failed = 0;
void Check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    std::fflush(stdout);
    failed += !ok;
}
fs::path U8(const char8_t* text) { return fs::path(std::u8string(text)); }
std::string Utf8(const char8_t* text) { return std::string(reinterpret_cast<const char*>(text)); }

std::vector<std::string> Arguments(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 0; i < count; ++i) out.push_back(pt::os::PathToUtf8(fs::path(wide[i])));
    LocalFree(wide);
#else
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

std::string ReadText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

int Child(const std::vector<std::string>& args) {
    const fs::path out = pt::os::PathFromUtf8(args[2]);
    std::string text;
    if (args[1] == "--echo") {
        text = std::to_string(args.size() - 3) + "\n";
        for (size_t i = 3; i < args.size(); ++i) text += "[" + args[i] + "]\n";
        text += "cwd [" + pt::os::PathToUtf8(fs::current_path()) + "]\n";
    } else {
        text = pt::os::PathToUtf8(pt::os::ExecutableFile());
    }
    const fs::path temporary = fs::path(out).concat(".part");
    {
        std::ofstream file(temporary, std::ios::binary);
        file << text;
    }
    std::error_code ec;
    fs::rename(temporary, out, ec);
    return ec ? 3 : 0;
}

}

int main(int argc, char** argv) {
    const std::vector<std::string> args = Arguments(argc, argv);
    if (args.size() >= 3 && (args[1] == "--echo" || args[1] == "--exe")) return Child(args);
    if (args.size() != 2) return 2;
#ifdef _WIN32
    std::printf("INFO ANSI code page %u, OEM code page %u\n", GetACP(), GetOEMCP());
#endif
    const fs::path base = pt::os::PathFromUtf8(args[1]);

    const std::vector<const char8_t*> names = {
        u8"Kullan\u0131c\u0131 \u0130\u015f\u00e7i \u015e\u015f\u011e\u011f\u00c7\u00e7\u00d6\u00f6\u00dc\u00fc",
        u8"\u041f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u0435\u043b\u044c",
        u8"\u03a7\u03c1\u03ae\u03c3\u03c4\u03b7\u03c2",
        u8"\u0645\u0633\u062a\u062e\u062f\u0645",
        u8"\u30e6\u30fc\u30b6\u30fc \u7528\u6237",
        u8"\U0001f3ae \U0001f600",
        u8"Caf\u00e9",
        u8"Cafe\u0301",
        u8"O'Brien & Sons 100% %USERPROFILE% !x! ^ (1)",
    };
    for (const char8_t* name : names) {
        const std::string text = Utf8(name);
        Check(pt::os::PathToUtf8(pt::os::PathFromUtf8(text)) == text, "UTF-8 round trip: " + text);
    }
    Check(pt::os::PathFromUtf8(Utf8(names[6])) != pt::os::PathFromUtf8(Utf8(names[7])), "NFC and NFD stay different names (no normalization)");

    fs::path deep = base;
    for (const char8_t* name : names) deep /= U8(name);
    for (int i = 0; deep.native().size() < 300; ++i)
        deep /= U8(u8"deep \u015e\u011f\u0130\u0131 \u0416\u0436 \u65e5\u672c\u8a9e \U0001f3ae ") += std::to_string(i);
    std::error_code ec;
    fs::create_directories(deep, ec);
    Check(!ec && fs::is_directory(deep), "created a Unicode folder " + std::to_string(deep.native().size()) + " characters long");
    Check(fs::exists(deep.parent_path() / deep.filename()), "the deep folder is found again");
    fs::create_directory(base / U8(names[6]) / "nfc-mark", ec);
    Check(!fs::exists(base / U8(names[7]) / "nfc-mark"), "an NFC folder is not found under its NFD spelling (both are kept as given)");

    const fs::path file = deep / U8(u8"pt \u0130.txt");
    if (FILE* f = pt::os::OpenFile(file, "wb")) {
        std::fputs("written", f);
        std::fclose(f);
    }
    Check(ReadText(file) == "written", "os::OpenFile and std::ifstream in the deep folder");

#ifdef _WIN32
    SetEnvironmentVariableW(L"PT_UNICODE_PATH_TEST", deep.c_str());
#else
    setenv("PT_UNICODE_PATH_TEST", deep.c_str(), 1);
#endif
    Check(pt::os::PathFromUtf8(pt::os::GetEnv("PT_UNICODE_PATH_TEST")) == deep, "Unicode environment variable preserved");

    const fs::path log = deep / "pt.log";
    pt::LogSetFile(log);
    pt::LogInfo("unicode log sentinel {}", pt::os::PathToUtf8(deep));
    pt::LogSetFile({});
    Check(ReadText(log).find("unicode log sentinel " + pt::os::PathToUtf8(deep)) != std::string::npos, "log written in the deep folder, path logged as UTF-8");

    pt::AppSettings written, loaded;
    written.voice.key = "J";
    written.audio.volume = 0.37f;
    Check(pt::SaveAppSettings(deep / "pt.ini", written) && pt::LoadAppSettings(deep / "pt.ini", loaded) && loaded.voice.key == "J" &&
              loaded.audio.volume == written.audio.volume,
          "pt.ini written and read back in the deep folder");
    pt::game::SaveStore store;
    store.SetDirectory(deep, "PT_Save_Data");
    pt::game::SaveFile save;
    save.progress.floor = "f050";
    Check(store.Save(save), "save written in the deep folder");
    const auto read = store.Load();
    Check(read && read->progress.floor == "f050", "save read back in the deep folder");

    const fs::path png = deep / U8(u8"photo \u0416 \u65e5 \U0001f600.png");
    const unsigned char pixels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const bool png_written = stbi_write_png(pt::os::PathToUtf8(png).c_str(), 2, 2, 4, pixels, 8) != 0;
    int w = 0, h = 0, channels = 0;
    Check(png_written && fs::is_regular_file(png) && stbi_info(pt::os::PathToUtf8(png).c_str(), &w, &h, &channels) && w == 2 && h == 2,
          "stb writes and reads a PNG by its UTF-8 name");

    const char* turkish = nullptr;
    for (const char* name : {"tr-TR", "Turkish_Turkey.1254", "tr_TR.UTF-8", "tr_TR.utf8", "tr_TR"}) {
        if (std::setlocale(LC_ALL, name)) {
            turkish = name;
            break;
        }
    }
    std::printf("INFO locale %s: tolower('I')=0x%02x toupper('i')=0x%02x towlower(L'I')=U+%04X towupper(L'i')=U+%04X\n", turkish ? turkish : "(none)",
                static_cast<unsigned char>(std::tolower('I')), static_cast<unsigned char>(std::toupper('i')), static_cast<unsigned>(std::towlower(L'I')),
                static_cast<unsigned>(std::towupper(L'i')));
    Check(pt::AsciiLower('I') == 'i' && pt::AsciiUpper('i') == 'I', "AsciiLower('I') is 'i' and AsciiUpper('i') is 'I' under the Turkish locale");
    Check(pt::EqualsIgnoreAsciiCase("PT.EXE", "pt.exe") && pt::EqualsIgnoreAsciiCase("TEXTURE.QAR", "texture.qar") &&
              pt::AsciiLowerCopy("PT_INI.LISA") == "pt_ini.lisa",
          "file names compare without case under the Turkish locale");
    Check(!pt::EqualsIgnoreAsciiCase("pt.exe", Utf8(u8"p\u0131.exe")) && !pt::EqualsIgnoreAsciiCase("PT.EXE", Utf8(u8"P\u0130.EXE")) &&
              pt::AsciiLowerCopy(Utf8(u8"\u0130I\u0131i")) == Utf8(u8"\u0130i\u0131i"),
          "dotted and dotless i stay letters of their own (UTF-8 bytes untouched)");
    Check(pt::setup::IsUserDataPath("DATA/pt.ini") && pt::setup::IsUserDataPath("Data/saves") && !pt::setup::IsUserDataPath("datai/x"),
          "setup: DATA, Data and data are the player's folder under the Turkish locale");
    Check(pt::setup::IsPlayerPath("PT.INI") && pt::setup::IsPlayerPath("MODS/x/init.lua") && pt::setup::IsPlayerPath("CUSA01127/TEXTURE.QAR") &&
              !pt::setup::IsPlayerPath("SHADERS/A.SPV"),
          "setup: the player's files are recognised in upper case under the Turkish locale");
    Check(pt::setup::Lower("PT.EXE") == "pt.exe" && pt::setup::Lower("PATHID_LIST_PS4.BIN") == "pathid_list_ps4.bin", "setup: Lower is ASCII");
#ifdef _WIN32
    Check(pt::os::SameFileName(L"PT.EXE", L"pt.exe") && pt::os::SameFileName(L"I.TXT", L"i.txt"), "Windows: PT.EXE and pt.exe are the same file name");
    Check(!pt::os::SameFileName(L"\u0130.txt", L"i.txt") && !pt::os::SameFileName(L"\u0131.txt", L"I.txt"),
          "Windows: dotted capital I and dotless i are other names (the file system's table, not the locale's)");
#else
    Check(!pt::os::SameFileName("PT.EXE", "pt.exe") && pt::os::SameFileName("pt", "pt"), "POSIX: file names are compared as bytes");
#endif
    Check(!pt::os::SameFileName(U8(names[6]), U8(names[7])), "NFC and NFD names are not merged");
    std::setlocale(LC_ALL, "C");

    const fs::path self = pt::os::ExecutableFile();
    Check(!self.empty() && fs::is_regular_file(self), "ExecutableFile names this program");
#ifdef _WIN32
    const fs::path child = deep / U8(u8"child \u0130\u015f \u65e5 \U0001f3ae.exe");
#else
    const fs::path child = deep / U8(u8"child \u0130\u015f \u65e5 \U0001f3ae");
#endif
    fs::copy_file(self, child, fs::copy_options::overwrite_existing, ec);
    Check(!ec, "copied this program into the deep folder");
    std::vector<std::string> sent = {"plain", "with space", "", "quote\"inside", "%PATH% & | ^ < > ! $HOME `x`",
                                     Utf8(u8"Kullan\u0131c\u0131 \u0130\u0131 \u041f\u043e\u043b\u044c\u0437 \u30e6\u30fc\u30b6\u30fc \U0001f3ae Cafe\u0301"),
                                     pt::os::PathToUtf8(deep)};
#ifdef _WIN32
    sent.push_back(pt::os::PathToUtf8(deep) + "\\");
    sent.push_back("a\\\\\"b\\");
#endif
    auto expect = [&](const fs::path& cwd) {
        std::string text = std::to_string(sent.size()) + "\n";
        for (const auto& arg : sent) text += "[" + arg + "]\n";
        return text + "cwd [" + pt::os::PathToUtf8(cwd) + "]\n";
    };
    const fs::path echo = deep / U8(u8"echo \u0416.txt");
    std::vector<std::string> run_args{"--echo", pt::os::PathToUtf8(echo)};
    run_args.insert(run_args.end(), sent.begin(), sent.end());
    std::atomic<bool> cancel{false};
#ifdef _WIN32
    const fs::path deep_cwd = deep.native().size() >= MAX_PATH - 2 ? fs::current_path() : deep;
#else
    const fs::path deep_cwd = deep;
#endif
    const auto result = pt::os::RunProcess(child, run_args, deep, deep / U8(u8"child \u0130.log"), cancel, std::chrono::seconds(60));
    const std::string got = ReadText(echo);
    Check(result.started && result.exit_code == 0 && got == expect(deep_cwd), "RunProcess: a program and its arguments in the deep folder");
    if (got != expect(deep_cwd))
        std::printf("INFO RunProcess started %d exit %d, sent:\n%sgot:\n%s\n", int(result.started), result.exit_code, expect(deep_cwd).c_str(), got.c_str());
    const fs::path short_cwd = base / U8(u8"cwd \u0130\u015f \u65e5");
    fs::create_directories(short_cwd, ec);
    fs::remove(echo, ec);
    const auto in_short = pt::os::RunProcess(child, run_args, short_cwd, short_cwd / "child.log", cancel, std::chrono::seconds(60));
    Check(in_short.exit_code == 0 && ReadText(echo) == expect(short_cwd), "RunProcess: a Unicode working folder is passed on");

    const fs::path detached = deep / U8(u8"detached \u65e5.txt");
    std::vector<std::string> start_args{"--echo", pt::os::PathToUtf8(detached)};
    start_args.insert(start_args.end(), sent.begin(), sent.end());
    const bool started = pt::os::StartDetached(child, start_args, deep);
    for (int i = 0; i < 200 && !fs::exists(detached); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Check(started && ReadText(detached) == expect(deep_cwd), "StartDetached: a program and its arguments in the deep folder");

    const fs::path named = deep / "exe.txt";
    const auto exe_result = pt::os::RunProcess(child, {"--exe", pt::os::PathToUtf8(named)}, deep, deep / "exe.log", cancel, std::chrono::seconds(60));
    Check(exe_result.exit_code == 0 && fs::equivalent(pt::os::PathFromUtf8(ReadText(named)), child, ec),
          "ExecutableFile gives the whole path of a program deeper than MAX_PATH");

    Check(pt::platform::PrepareUserDataDirectory(deep / "data", {}, false).success, "data folder prepared in the deep folder");
    const fs::path target = base / U8(u8"link target \u0130");
    const fs::path link = base / U8(u8"link \u0416");
    fs::create_directories(target, ec);
#ifdef _WIN32
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    pt::os::RunProcess(fs::path(system) / L"cmd.exe", {"/c", "mklink", "/J", pt::os::PathToUtf8(link), pt::os::PathToUtf8(target)}, base, base / "mklink.log",
                       cancel, std::chrono::seconds(30));
#else
    fs::create_directory_symlink(target, link, ec);
#endif
    Check(fs::exists(link) && pt::os::IsLink(link), "a junction (Windows) or symbolic link is a link");
    Check(!pt::os::IsLink(target) && !pt::os::IsLink(deep) && !pt::os::IsLink(file), "plain folders and files are not links");
    Check(!pt::platform::PrepareUserDataDirectory(link, {}, false).success, "the data folder is refused through a link");
#ifdef _WIN32
    Check(pt::os::IsLinkReparseTag(0xA000000CL) && pt::os::IsLinkReparseTag(0xA0000003L), "symbolic link and mount point tags are links");
    Check(!pt::os::IsLinkReparseTag(0x9000001AL) && !pt::os::IsLinkReparseTag(0x9000301AL) && !pt::os::IsLinkReparseTag(0x9000F01AL) &&
              !pt::os::IsLinkReparseTag(0x80000013L) && !pt::os::IsLinkReparseTag(0x8000001BL),
          "OneDrive (cloud files), deduplication and app alias tags are not links");
#endif
    fs::remove(link, ec);
    std::printf("%s\n", failed ? "FAILED" : "ALL PASS");
    return failed ? 1 : 0;
}
