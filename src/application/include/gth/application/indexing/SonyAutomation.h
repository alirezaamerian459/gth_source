#pragma once

#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include "gth/application/indexing/IPackageSync.h"
#include "gth/application/indexing/IUpdateChecker.h"

namespace gth::application {

// What the Sony automation does when it runs by itself. Kept in a small
// "key=value" text file beside the database - readable and editable by hand
// or by another tool, and needing no schema change on machines that already
// have a database.
struct SonyAutomationSettings {
    bool checkForUpdates = true;       // ask Sony about every game and show the verdicts
    bool addMissingMetadata = true;    // fetch the small (~85 MB) metadata piece PS5 downloads lack
    bool downloadUpdates = true;       // also download out-of-date packages (gigabytes)
    bool scanForNewGames = true;       // look through storages for games added since the last scan before each run
    int intervalHours = 12;            // how often, while the app is open

    bool anythingEnabled() const { return checkForUpdates || addMissingMetadata || downloadUpdates; }
    bool operator==(const SonyAutomationSettings&) const = default;
};

SonyAutomationSettings ParseSonyAutomationSettings(const std::string& text);
std::string SerializeSonyAutomationSettings(const SonyAutomationSettings& settings);

class SonyAutomationSettingsStore {
public:
    explicit SonyAutomationSettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

    // The saved settings, or the defaults when there is no file (or it can't be read).
    SonyAutomationSettings load() const;
    // False when the file couldn't be written.
    bool save(const SonyAutomationSettings& settings) const;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// Whether an automatic run is due: something is enabled and the interval has
// passed since the last one. The first run of a session waits `startupDelay`
// after the app started, so it never competes with the app coming up.
bool IsSonyAutomationDue(const SonyAutomationSettings& settings, std::chrono::steady_clock::time_point appStarted,
                         std::optional<std::chrono::steady_clock::time_point> lastRun,
                         std::chrono::steady_clock::time_point now,
                         std::chrono::seconds startupDelay = std::chrono::seconds(120));

// ---- The report a run leaves behind, for reading in the morning --------------

struct SonyRunReportInput {
    SonyRunReportInput() = default;
    SonyRunReportInput(const SonyRunReportInput&) = default;
    SonyRunReportInput& operator=(const SonyRunReportInput&) = default;
    SonyRunReportInput(SonyRunReportInput&&) noexcept = default;
    SonyRunReportInput& operator=(SonyRunReportInput&&) noexcept = default;
    std::time_t when = 0;                   // when the run started
    bool automatic = false;
    std::chrono::seconds duration{0};
    bool syncRan = false;
    bool oneGame = false;                   // a single game was synced (not a batch)
    SyncResult single;
    SyncBatchResult batch;
    int storagesRescanned = 0;
    bool scannedForNewGames = false;        // the storages were searched for new games before the sync
    int newGamesFound = 0;                  // ...and this many games turned up that the library hadn't seen
    int linksLearnedFromFiles = 0;          // Sony links read out of metadata pieces already in the archive
    bool checked = false;
    UpdateCheckSummary check;
};

// The text of one run's report. `titlesByTitleId` puts game names in place of
// title IDs where it knows them.
std::string FormatSonyRunReport(const SonyRunReportInput& run, const std::map<std::string, std::string>& titlesByTitleId);

// Appends `text` to the report log (creating it), keeping the file from growing without bound.
void AppendSonyRunReport(const std::filesystem::path& file, const std::string& text);

// The first few lines of the newest report in the log, or "" when there is none.
std::string LastSonyRunSummary(const std::filesystem::path& file);

}  // namespace gth::application
