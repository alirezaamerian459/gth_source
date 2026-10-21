#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

#include "gth/application/indexing/DownloadHistory.h"

using namespace gth::application;

namespace {

DownloadHistoryEntry MakeEntry(std::time_t when, std::string titleId) {
    DownloadHistoryEntry entry;
    entry.when = when;
    entry.titleId = std::move(titleId);
    entry.title = "Once Human";
    entry.kind = SyncKind::Update;
    entry.outcome = DownloadHistoryOutcome::Success;
    entry.bytesDownloaded = 45'498'368'000;
    entry.durationSeconds = 612;
    entry.message = "Downloaded 01.500.000: 12 file(s), 42.4 GB";
    return entry;
}

}  // namespace

TEST(DownloadHistoryTest, RoundTripsEveryField) {
    const auto entry = MakeEntry(1758540000, "PPSA31219");
    const auto parsed = ParseDownloadHistoryLine(FormatDownloadHistoryLine(entry));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->when, entry.when);
    EXPECT_EQ(parsed->titleId, entry.titleId);
    EXPECT_EQ(parsed->title, entry.title);
    EXPECT_EQ(parsed->kind, entry.kind);
    EXPECT_EQ(parsed->outcome, entry.outcome);
    EXPECT_EQ(parsed->bytesDownloaded, entry.bytesDownloaded);
    EXPECT_EQ(parsed->durationSeconds, entry.durationSeconds);
    EXPECT_EQ(parsed->message, entry.message);
}

TEST(DownloadHistoryTest, EveryKindAndOutcomeRoundTrips) {
    for (const auto kind : {SyncKind::NothingToDo, SyncKind::CompleteMetadata, SyncKind::Update, SyncKind::Unavailable}) {
        for (const auto outcome :
             {DownloadHistoryOutcome::Success, DownloadHistoryOutcome::Failed, DownloadHistoryOutcome::Cancelled, DownloadHistoryOutcome::SkippedNeedsUpdate}) {
            auto entry = MakeEntry(1, "PPSA00000");
            entry.kind = kind;
            entry.outcome = outcome;
            const auto parsed = ParseDownloadHistoryLine(FormatDownloadHistoryLine(entry));
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->kind, kind);
            EXPECT_EQ(parsed->outcome, outcome);
        }
    }
}

TEST(DownloadHistoryTest, TabsAndNewlinesInTextFieldsCantCorruptTheLine) {
    auto entry = MakeEntry(1, "PPSA31219");
    entry.title = "Once\tHuman\nSpecial\rEdition";
    entry.message = "Downloaded\t01.500.000\nwith extras";
    const std::string line = FormatDownloadHistoryLine(entry);
    EXPECT_EQ(std::count(line.begin(), line.end(), '\t'), 7) << "exactly the 7 field separators - none smuggled in from the text";

    const auto parsed = ParseDownloadHistoryLine(line);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->title, "Once Human Special Edition");
    EXPECT_EQ(parsed->message, "Downloaded 01.500.000 with extras");
}

TEST(DownloadHistoryTest, MalformedLinesAreRejectedNotCrashed) {
    EXPECT_FALSE(ParseDownloadHistoryLine("").has_value());
    EXPECT_FALSE(ParseDownloadHistoryLine("not\tenough\tfields").has_value());
    EXPECT_FALSE(ParseDownloadHistoryLine("notanumber\tPPSA1\tTitle\tUpdate\tSuccess\t1\t1\tmsg").has_value())
        << "when isn't a number";
    EXPECT_FALSE(ParseDownloadHistoryLine("1\tPPSA1\tTitle\tNotAKind\tSuccess\t1\t1\tmsg").has_value());
    EXPECT_FALSE(ParseDownloadHistoryLine("1\tPPSA1\tTitle\tUpdate\tNotAnOutcome\t1\t1\tmsg").has_value());
}

TEST(DownloadHistoryTest, AppendAndReadRoundTripsAndOrdersNewestFirst) {
    const auto path = std::filesystem::temp_directory_path() / "gth_download_history_test" / "history.tsv";
    std::filesystem::remove_all(path.parent_path());

    AppendDownloadHistory(path, MakeEntry(100, "PPSA00001"));
    AppendDownloadHistory(path, MakeEntry(200, "PPSA00002"));
    AppendDownloadHistory(path, MakeEntry(300, "PPSA00003"));

    const auto entries = ReadDownloadHistory(path);
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].titleId, "PPSA00003") << "newest first";
    EXPECT_EQ(entries[1].titleId, "PPSA00002");
    EXPECT_EQ(entries[2].titleId, "PPSA00001");

    std::filesystem::remove_all(path.parent_path());
}

TEST(DownloadHistoryTest, ReadRespectsALimit) {
    const auto path = std::filesystem::temp_directory_path() / "gth_download_history_limit_test" / "history.tsv";
    std::filesystem::remove_all(path.parent_path());

    for (int i = 0; i < 5; ++i) AppendDownloadHistory(path, MakeEntry(i, "PPSA0000" + std::to_string(i)));

    const auto entries = ReadDownloadHistory(path, 2);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].titleId, "PPSA00004");
    EXPECT_EQ(entries[1].titleId, "PPSA00003");

    std::filesystem::remove_all(path.parent_path());
}

TEST(DownloadHistoryTest, MissingFileReadsAsEmptyNotAnError) {
    const auto entries = ReadDownloadHistory(std::filesystem::temp_directory_path() / "gth_download_history_nonexistent.tsv");
    EXPECT_TRUE(entries.empty());
}
