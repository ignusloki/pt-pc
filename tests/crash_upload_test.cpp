#include "../src/engine/core/crash_upload.cpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(root);
    const auto old = root / "pt-crash-old-20261012-010000.dmp";
    const auto current = root / "pt-crash-current-20261013-010000.dmp";
    const auto log = root / "pt.log";
    std::ofstream(old) << "old dump";
    std::ofstream(current) << "current dump";
    std::ofstream(log) << "crash reporting: build current\nerror crash: dump " << current.string() << "\n";
    pt::PreserveCrashLog(log);
    auto old_log = old; old_log.replace_extension(".log");
    auto current_log = current; current_log.replace_extension(".log");
    bool ok = !std::filesystem::exists(old_log) && std::filesystem::exists(current_log);
    std::printf("Only the dump named by the session receives its log: %s\n", ok ? "PASS" : "FAIL");
    const auto long_log = root / "long.log";
    const auto binary = root / "large.dmp";
    std::ofstream(long_log) << "0123456789";
    std::ofstream(binary) << "0123456789";
    const bool bounded = pt::ReadFile(long_log, 4) == "6789" && pt::ReadFile(binary, 4).empty() &&
                         pt::ReadFile(binary, 10) == "0123456789";
    std::printf("Logs keep their tail; minidumps remain whole or are omitted: %s\n", bounded ? "PASS" : "FAIL");
    ok = ok && bounded;
    std::ofstream(current_log) << "pt-port version 1.0.2 (windows), Hallway Engine\ncrash reporting: build current, dumps to data\nerror crash: dump " << current.filename().string();
    const bool identity = pt::CrashVersionFromLog(current_log, "current", current.filename().string()) == "1.0.2" &&
                          pt::CrashVersionFromLog(current_log, "old", old.filename().string()) == "unknown" &&
                          pt::CrashVersionFromLog(current_log, "wrong-build", current.filename().string()) == "unknown";
    std::printf("Crash version comes from a matching session, not the uploader: %s\n", identity ? "PASS" : "FAIL");
    ok = ok && identity;
    return ok ? 0 : 1;
}
