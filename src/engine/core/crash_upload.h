#pragma once

#include <filesystem>
#include <string>

namespace pt {

void PreserveCrashLog(const std::filesystem::path& log_path);
void OfferCrashUpload(const std::filesystem::path& dir, const std::string& version, bool interactive);

enum class LogReportState { Idle, Sending, Sent, Failed };
void SendLogReport(const std::filesystem::path& log, const std::filesystem::path& dir, const std::string& version);
LogReportState GetLogReportState();

}
