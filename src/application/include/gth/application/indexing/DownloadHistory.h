#pragma once

#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "gth/application/indexing/IPackageSync.h"

namespace gth::application {

enum class DownloadHistoryOutcome { Success, Failed, Cancelled, SkippedNeedsUpdate };

// One completed download line item - one game, one Sync run (a batch's per-game entries each
// get their own record too), kept so the Downloads tab can show real per-game numbers instead of
// only the narrative text sony-automation-log.txt already keeps.
struct DownloadHistoryEntry {
    std::time_t when = 0;        // when this game's download finished
    std::string titleId;
    std::string title;           // human name at the time - kept here so the tab needs no live library lookup
    SyncKind kind = SyncKind::Unavailable;
    DownloadHistoryOutcome outcome = DownloadHistoryOutcome::Failed;
    long long bytesDownloaded = 0;
    long long durationSeconds = 0;  // 0 when not known (e.g. one game's slice of a batch's total time)
    std::string message;
};

// One line, tab-separated, no trailing newline. Tabs/newlines inside title or message are
// replaced with spaces first so they can never split a field or corrupt the next line.
std::string FormatDownloadHistoryLine(const DownloadHistoryEntry& entry);

// The reverse of FormatDownloadHistoryLine; nullopt if the line isn't a well-formed record (a
// stray blank line, a future version's extra column, ...) rather than throwing.
std::optional<DownloadHistoryEntry> ParseDownloadHistoryLine(const std::string& line);

// Appends one record (creating the file/folder if needed) and keeps the file from growing
// without bound by dropping the oldest lines once it passes a size limit.
void AppendDownloadHistory(const std::filesystem::path& file, const DownloadHistoryEntry& entry);

// Every record in the file, newest first. `limit` caps how many are returned (0 = no cap).
std::vector<DownloadHistoryEntry> ReadDownloadHistory(const std::filesystem::path& file, size_t limit = 0);

}  // namespace gth::application
