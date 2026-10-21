#include <gtest/gtest.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "gth/application/indexing/SonyAutomation.h"

using namespace gth::application;
using namespace std::chrono;

TEST(SonyAutomationSettingsTest, DefaultsEnableAutomaticUpdatesAndScanning) {
    const SonyAutomationSettings defaults;
    EXPECT_TRUE(defaults.checkForUpdates);
    EXPECT_TRUE(defaults.addMissingMetadata) << "the ~85 MB metadata piece is small and additive";
    EXPECT_TRUE(defaults.downloadUpdates) << "the requested default keeps supported games current automatically";
    EXPECT_EQ(defaults.intervalHours, 12);
    EXPECT_TRUE(defaults.scanForNewGames) << "looking for new games only reads the storages";
}

TEST(SonyAutomationSettingsTest, TheNewGamesScanIsASettingLikeTheOthers) {
    EXPECT_FALSE(ParseSonyAutomationSettings("scanForNewGames = off\n").scanForNewGames);
    EXPECT_TRUE(ParseSonyAutomationSettings("scanForNewGames = yes\n").scanForNewGames);
    EXPECT_TRUE(ParseSonyAutomationSettings("scanForNewGames=maybe\n").scanForNewGames) << "a bad value keeps the default";

    SonyAutomationSettings changed;
    changed.scanForNewGames = false;
    EXPECT_EQ(ParseSonyAutomationSettings(SerializeSonyAutomationSettings(changed)), changed);
    changed.checkForUpdates = changed.addMissingMetadata = changed.downloadUpdates = false;
    EXPECT_FALSE(changed.anythingEnabled()) << "the scan alone is not a reason to start a run";
}

TEST(SonyAutomationSettingsTest, ParsesTheFileIncludingCommentsAndVariousSpellings) {
    const auto settings = ParseSonyAutomationSettings(
        "# comment\n"
        "  CheckForUpdates = OFF \r\n"
        "addMissingMetadata=no\n"
        "downloadUpdates=Yes\n"
        "intervalHours = 6\n"
        "unknownKey=whatever\n"
        "not a setting\n");

    EXPECT_FALSE(settings.checkForUpdates);
    EXPECT_FALSE(settings.addMissingMetadata);
    EXPECT_TRUE(settings.downloadUpdates);
    EXPECT_EQ(settings.intervalHours, 6);
}

TEST(SonyAutomationSettingsTest, BadValuesKeepTheDefaultAndTheIntervalIsClamped) {
    EXPECT_TRUE(ParseSonyAutomationSettings("checkForUpdates=maybe\n").checkForUpdates);
    EXPECT_EQ(ParseSonyAutomationSettings("intervalHours=abc\n").intervalHours, 12);
    EXPECT_EQ(ParseSonyAutomationSettings("intervalHours=0\n").intervalHours, 1);
    EXPECT_EQ(ParseSonyAutomationSettings("intervalHours=100000\n").intervalHours, 168);
}

TEST(SonyAutomationSettingsTest, RoundTripsThroughTheFile) {
    const auto path = std::filesystem::temp_directory_path() / "gth_sony_automation_test" / "sony-automation.txt";
    std::filesystem::remove_all(path.parent_path());
    SonyAutomationSettingsStore store(path);

    EXPECT_EQ(store.load(), SonyAutomationSettings{}) << "no file yet: defaults";

    SonyAutomationSettings changed;
    changed.checkForUpdates = false;
    changed.downloadUpdates = true;
    changed.intervalHours = 3;
    ASSERT_TRUE(store.save(changed));
    EXPECT_EQ(store.load(), changed);

    std::filesystem::remove_all(path.parent_path());
}

TEST(SonyAutomationSettingsTest, LegacySettingsEnableDownloadsAndNewSettingsKeepAnOptOut) {
    const auto path = std::filesystem::temp_directory_path() / "gth_sony_automation_migration_test" / "sony-automation.txt";
    std::filesystem::remove_all(path.parent_path());
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "checkForUpdates=on\n"
               "addMissingMetadata=on\n"
               "downloadUpdates=off\n"
               "scanForNewGames=on\n"
               "intervalHours=12\n";
    }

    SonyAutomationSettingsStore store(path);
    auto settings = store.load();
    EXPECT_TRUE(settings.downloadUpdates) << "legacy files inherited the previous off-by-default setting";
    {
        std::ifstream in(path, std::ios::binary);
        const std::string migrated((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_NE(migrated.find("settingsVersion=2"), std::string::npos);
    }

    settings.downloadUpdates = false;
    ASSERT_TRUE(store.save(settings));
    EXPECT_FALSE(store.load().downloadUpdates) << "an opt-out in the new versioned settings file persists";

    std::filesystem::remove_all(path.parent_path());
}

TEST(SonyAutomationDueTest, WaitsOutTheStartupDelayThenRunsOnceAndThenOnTheInterval) {
    SonyAutomationSettings settings;
    settings.intervalHours = 12;
    const auto started = steady_clock::time_point{} + hours(100);

    EXPECT_FALSE(IsSonyAutomationDue(settings, started, std::nullopt, started + seconds(30)));
    EXPECT_TRUE(IsSonyAutomationDue(settings, started, std::nullopt, started + seconds(121)));

    const auto ran = started + minutes(3);
    EXPECT_FALSE(IsSonyAutomationDue(settings, started, ran, ran + hours(11)));
    EXPECT_TRUE(IsSonyAutomationDue(settings, started, ran, ran + hours(12)));
}

TEST(SonyAutomationDueTest, NeverRunsWhenEverythingIsSwitchedOff) {
    SonyAutomationSettings off;
    off.checkForUpdates = off.addMissingMetadata = off.downloadUpdates = false;
    const auto started = steady_clock::time_point{} + hours(100);
    EXPECT_FALSE(IsSonyAutomationDue(off, started, std::nullopt, started + hours(48)));
    EXPECT_FALSE(off.anythingEnabled());
}

// ---- Times used by the report tests ------------------------------------------------------

namespace {

std::time_t localTime(int year, int month, int day, int hour, int minute = 0) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_isdst = -1;
    return std::mktime(&t);
}

}  // namespace

// ---- The report a run leaves for the morning -------------------------------------------

namespace {

SonyRunReportInput sampleRun() {
    SonyRunReportInput run;
    run.when = localTime(2026, 9, 20, 2, 0);
    run.automatic = true;
    run.duration = std::chrono::seconds(4320);  // 1h 12m
    run.syncRan = true;
    run.batch.completed = 1;
    run.batch.updated = 1;
    run.batch.alreadyFine = 3;
    run.batch.needUpdate = 1;
    run.batch.unavailable = 2;
    run.batch.failed = 1;
    run.batch.bytesDownloaded = 12LL * 1024 * 1024 * 1024;
    run.batch.games = {
        {"PPSA02343", SyncKind::CompleteMetadata, true, false, "Added the metadata piece (85.0 MB).", 89128960},
        {"PPSA01288", SyncKind::Update, true, false, "Brought the package up to Sony's latest (01.300.000).", 12LL * 1024 * 1024 * 1024},
        {"PPSA03000", SyncKind::Update, false, true, "Out of date (01.100.000 is Sony's latest).", 0},
        {"PPSA04000", SyncKind::Update, false, false, "Couldn't download X: couldn't connect", 0},
    };
    run.storagesRescanned = 1;
    run.checked = true;
    run.check.entries.resize(7);
    run.check.upToDate = 5;
    run.check.noSonyData = 2;
    return run;
}

}  // namespace

TEST(SonyRunReportTest, SaysWhatHappenedToEachGameWithNamesAndTheOverallCounts) {
    const std::string report = FormatSonyRunReport(sampleRun(), {{"PPSA02343", "It Takes Two"}, {"PPSA01288", "Sackboy"}});

    EXPECT_NE(report.find("=== 2026-09-20 02:00 - automatic run (took 1h 12m) ==="), std::string::npos) << report;
    EXPECT_NE(report.find("1 metadata added, 1 updated, 3 already current, 1 out of date but not downloaded, "
                          "2 without a usable Sony link, 1 failed. Downloaded 12.00 GB."),
              std::string::npos) << report;
    EXPECT_NE(report.find("  + It Takes Two: Added the metadata piece"), std::string::npos) << report;
    EXPECT_NE(report.find("  + Sackboy: Brought the package up"), std::string::npos) << report;
    EXPECT_NE(report.find("  ~ PPSA03000: out of date, not downloaded"), std::string::npos) << "unknown titles show their ID";
    EXPECT_NE(report.find("  ! PPSA04000: FAILED - Couldn't download X"), std::string::npos) << report;
    EXPECT_NE(report.find("Rescanned 1 storage(s)"), std::string::npos);
    EXPECT_NE(report.find("Sony check of 7 game(s): 5 up to date"), std::string::npos) << report;
}

TEST(SonyRunReportTest, AManualSingleGameSyncReadsAsOneLine) {
    SonyRunReportInput run;
    run.when = localTime(2026, 9, 20, 9, 15);
    run.duration = std::chrono::seconds(45);
    run.syncRan = true;
    run.oneGame = true;
    run.single.titleId = "PPSA02343";
    run.single.message = "Added the metadata piece (85.0 MB); the title and version can now be read from the files.";
    run.single.filesDownloaded = 1;
    run.single.bytesDownloaded = 89128960;

    const std::string report = FormatSonyRunReport(run, {{"PPSA02343", "It Takes Two"}});

    EXPECT_NE(report.find("manual run (took 45s)"), std::string::npos) << report;
    EXPECT_NE(report.find("It Takes Two: Added the metadata piece"), std::string::npos) << report;
    EXPECT_NE(report.find("[1 file(s), 85.0 MB]"), std::string::npos) << report;
}

TEST(SonyRunReportTest, ARunWithNothingSwitchedOnSaysSo) {
    SonyRunReportInput run;
    run.when = localTime(2026, 9, 20, 2, 0);
    EXPECT_NE(FormatSonyRunReport(run, {}).find("Nothing was switched on to do."), std::string::npos);
}

TEST(SonyRunReportTest, ALogKeepsEveryRunAndShowsTheNewestAsTheSummary) {
    const auto dir = std::filesystem::temp_directory_path() / "gth_sony_report_test";
    std::filesystem::remove_all(dir);
    const auto file = dir / "sony-automation-log.txt";

    EXPECT_EQ(LastSonyRunSummary(file), "") << "no log yet";

    SonyRunReportInput first = sampleRun();
    first.when = localTime(2026, 9, 19, 2, 0);
    AppendSonyRunReport(file, FormatSonyRunReport(first, {}));
    SonyRunReportInput second = sampleRun();
    second.when = localTime(2026, 9, 20, 2, 0);
    second.batch.games.clear();
    AppendSonyRunReport(file, FormatSonyRunReport(second, {}));

    const std::string summary = LastSonyRunSummary(file);
    EXPECT_NE(summary.find("2026-09-20 02:00"), std::string::npos) << summary;
    EXPECT_EQ(summary.find("2026-09-19"), std::string::npos) << "only the newest run";
    EXPECT_NE(summary.find("Sony sync:"), std::string::npos);
    EXPECT_EQ(summary.find("  + "), std::string::npos) << "per-game detail stays in the file, not the summary";

    std::string all;
    {
        std::ifstream in(file, std::ios::binary);
        all.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(all.find("2026-09-19 02:00"), std::string::npos) << "the older run is still in the log";

    std::filesystem::remove_all(dir);
}

TEST(SonyRunReportTest, TheLogDoesNotGrowWithoutBoundAndKeepsWholeReports) {
    const auto dir = std::filesystem::temp_directory_path() / "gth_sony_report_trim_test";
    std::filesystem::remove_all(dir);
    const auto file = dir / "sony-automation-log.txt";

    SonyRunReportInput run = sampleRun();
    run.batch.games.assign(200, {"PPSA0000", SyncKind::CompleteMetadata, true, false, std::string(120, 'x'), 1});
    const std::string one = FormatSonyRunReport(run, {});
    for (int i = 0; i < 60; ++i) AppendSonyRunReport(file, one);

    EXPECT_LT(std::filesystem::file_size(file), 600u * 1024u);
    std::string first(4, ' ');
    {
        std::ifstream in(file, std::ios::binary);
        in.read(first.data(), 4);
    }
    EXPECT_EQ(first, "=== ") << "trimming cuts between reports, never in the middle of one";

    std::filesystem::remove_all(dir);
}

TEST(SonyRunReportTest, SaysWhatTheScanForNewGamesAndTheLocalLinksFound) {
    SonyRunReportInput run = sampleRun();
    run.scannedForNewGames = true;
    run.newGamesFound = 4;
    run.linksLearnedFromFiles = 2;
    run.batch.recoveredOnRetry = 1;

    const std::string report = FormatSonyRunReport(run, {});

    EXPECT_NE(report.find("Looked through the storages for new games: 4 new game(s) found."), std::string::npos) << report;
    EXPECT_NE(report.find("Read 2 Sony link(s) out of metadata pieces already in the archive."), std::string::npos) << report;
    EXPECT_NE(report.find("1 game(s) failed at first and worked on a retry."), std::string::npos) << report;
}

TEST(SonyRunReportTest, TheSummaryShownWhenTheTabOpensStillReachesTheCheckLine) {
    const auto dir = std::filesystem::temp_directory_path() / "gth_sony_report_summary_test";
    std::filesystem::remove_all(dir);
    const auto file = dir / "sony-automation-log.txt";

    SonyRunReportInput run = sampleRun();
    run.scannedForNewGames = true;
    run.newGamesFound = 1;
    run.linksLearnedFromFiles = 1;
    AppendSonyRunReport(file, FormatSonyRunReport(run, {}));

    const std::string summary = LastSonyRunSummary(file);
    EXPECT_NE(summary.find("Looked through the storages"), std::string::npos) << summary;
    EXPECT_NE(summary.find("Sony check of 7 game(s)"), std::string::npos) << summary;

    std::filesystem::remove_all(dir);
}
