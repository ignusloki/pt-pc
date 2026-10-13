#pragma once

#include <filesystem>
#include <string>

namespace pt {

// Crash reports sent to the developer (Sentry's minidump endpoint), only with the player's consent.
//
// The log of a session that crashed is rewritten by the next start, so PreserveCrashLog runs before the log file is opened: a
// crash dump (pt-crash-*.dmp, crash_report.h) newer than pt.log's last write and without a log of its own gets pt.log copied
// next to it (pt-crash-*.log). OfferCrashUpload then asks once per dump whether to send it (Send, Don't send, Always send);
// the answer "always" is kept in crash-reports.txt in the same folder. A dump that was sent or declined gets a .sent or .skip
// file and is not asked about again. The upload runs on its own thread.
void PreserveCrashLog(const std::filesystem::path& log_path);
void OfferCrashUpload(const std::filesystem::path& dir, const std::string& version, bool interactive);

// The PC settings page's "Send pt.log to the developer": the running session's pt.log and the newest crash dump of `dir`, if
// there is one, sent as one Sentry event (an envelope with the log and the dump as attachments) on its own thread. The player's
// press is the consent. LogReportState tells the row what to show.
enum class LogReportState { Idle, Sending, Sent, Failed };
void SendLogReport(const std::filesystem::path& log, const std::filesystem::path& dir, const std::string& version);
LogReportState GetLogReportState();

}
