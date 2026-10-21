#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gth/application/indexing/GameIndexerService.h"
#include "gth/application/indexing/PackageSyncService.h"
#include "gth/database/Database.h"
#include "gth/database/GameFileRepository.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/Migrator.h"
#include "gth/database/ScanJobRepository.h"
#include "gth/database/StorageRepository.h"
#include "gth/verification/FileVerifier.h"
#include "gth/verification/Sha256.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;
namespace fs = std::filesystem;

namespace {

class FakeScanner : public IStorageScanner {
public:
    std::vector<ScannedVariant> results;
    std::vector<ScannedVariant> scan(const std::string&) override { return results; }

    std::vector<std::string> pinnedVersions;
    bool pinReleaseVersion(const std::string&, const std::string&, const std::string& version, std::string&) override {
        pinnedVersions.push_back(version);
        return true;
    }
};

class FakeSony : public ILatestReleaseService {
public:
    LatestReleaseResult answer;                            // for any title without its own answer below
    std::map<std::string, LatestReleaseResult> byTitle;    // title ID -> answer
    LatestReleaseResult latestRelease(const std::string&, const std::string& titleId) override {
        const auto found = byTitle.find(titleId);
        return found != byTitle.end() ? found->second : answer;
    }
};

// "Downloads" from a map of address -> bytes, like a server that supports Range.
class FakeDownloader : public IPieceDownloader {
public:
    std::map<std::string, std::string> content;
    std::map<std::string, std::string> failures;  // address -> why
    std::map<std::string, int> failTimes;         // address -> how many more attempts fail ("connection reset") before it works
    std::vector<std::pair<std::string, long long>> calls;  // address, bytes already on disk when asked
    bool cancelAfterHalf = false;

    PieceDownloadResult download(const std::string& url, const fs::path& destination, long long expectedSize,
                                 const std::function<bool(long long)>& onProgress) override {
        PieceDownloadResult result;
        std::error_code ec;
        const long long have = fs::exists(destination, ec) ? static_cast<long long>(fs::file_size(destination, ec)) : 0;
        calls.emplace_back(url, have);

        if (const auto failure = failures.find(url); failure != failures.end()) {
            result.error = failure->second;
            return result;
        }
        if (const auto flaky = failTimes.find(url); flaky != failTimes.end() && flaky->second > 0) {
            --flaky->second;
            result.error = "connection reset";
            return result;
        }
        const auto found = content.find(url);
        if (found == content.end() || static_cast<long long>(found->second.size()) != expectedSize) {
            result.error = "no such file on the fake server";
            return result;
        }

        fs::create_directories(destination.parent_path());
        std::ofstream out(destination, std::ios::binary | (have > 0 ? std::ios::app : std::ios::trunc));
        const std::string& bytes = found->second;
        const size_t half = bytes.size() / 2;
        if (cancelAfterHalf && static_cast<size_t>(have) < half) {
            out.write(bytes.data() + have, static_cast<std::streamsize>(half - have));
            out.flush();
            onProgress(static_cast<long long>(half));
            result.cancelled = true;
            return result;
        }
        out.write(bytes.data() + have, static_cast<std::streamsize>(bytes.size() - have));
        out.flush();
        onProgress(static_cast<long long>(bytes.size()));
        result.success = true;
        return result;
    }
};

std::string sha256Of(const std::string& bytes) {
    const fs::path path = fs::temp_directory_path() / "gth_sync_hash_scratch.bin";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const auto hash = gth::verification::computeSha256Hex(path.string());
    fs::remove(path);
    return hash.value_or("");
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

const std::string kContentId = "EP0006-PPSA02343_00-NUTSEU0000000000";
const std::string kBaseUrl = "http://gst.prod.dl.playstation.net/gst/prod/00/PPSA02343_00/app/pkg/7/f_abc/";
const std::string kInfoUrl = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA02343_00/app/info/9/f_def/";

// The game piece bytes: part 0 starts with a FIH wrapper carrying `digest` at 0x30.
std::string gamePiece(int index, size_t size, const std::string& digest = std::string(32, 'D')) {
    std::string bytes(size, static_cast<char>('a' + index));
    if (index == 0) {
        bytes.replace(0, 4, std::string("\x7f" "FIH", 4));
        bytes.replace(0x30, 32, digest);
    }
    return bytes;
}

// The metadata piece: the same digest sits inside it, as in the real thing.
std::string metadataPiece(size_t size, const std::string& digest = std::string(32, 'D')) {
    std::string bytes(size, 'm');
    bytes.replace(0, 4, std::string("\x7f" "CNT", 4));
    bytes.replace(0x440, 32, digest);
    return bytes;
}

struct SyncFixture {
    Database db{":memory:"};
    StorageRepository storageRepository{db};
    GameRepository gameRepository{db};
    GameVariantRepository variantRepository{db};
    GameVersionRepository versionRepository{db};
    GameFileRepository fileRepository{db};
    ScanJobRepository scanJobRepository{db};
    FakeScanner scanner;
    GameIndexerService indexer{storageRepository, scanner,        gameRepository,  variantRepository,
                               versionRepository, fileRepository, scanJobRepository};
    FakeSony sony;
    FakeDownloader downloader;
    gth::verification::FileVerifier verifier;
    PackageSyncService sync{storageRepository, variantRepository, versionRepository, fileRepository,
                            sony,              downloader,        verifier,          scanner};

    fs::path root = fs::temp_directory_path() / "gth_package_sync_test";
    std::string storageId;
    std::string variantId;

    SyncFixture() {
        fs::remove_all(root);
        fs::create_directories(root);
        Migrator::applyMigrations(db);
        Storage storage;
        storage.name = "Archive";
        storage.rootPath = root.string();
        storage.type = StorageType::LocalHdd;
        storageRepository.add(storage);
        storageId = storage.storageId;
    }
    ~SyncFixture() { fs::remove_all(root); }

    // Puts the given pieces on disk as "<contentId>_<n>.pkg" (and the metadata piece, if given) and scans them in.
    void installArchive(const std::vector<std::string>& pieces, const std::string& metadata = {},
                        const std::string& installedVersion = {}) {
        ScannedVersion version;
        version.version = installedVersion;
        version.relativePath = kContentId;
        for (size_t i = 0; i < pieces.size(); ++i) {
            const std::string name = kContentId + "_" + std::to_string(i) + ".pkg";
            writeFile(root / name, pieces[i]);
            version.files.push_back(ScannedFile{name, static_cast<long long>(pieces[i].size()), "2026-01-01T00:00:00Z"});
        }
        if (!metadata.empty()) {
            const std::string name = kContentId + "_sc.pkg";
            writeFile(root / name, metadata);
            version.files.push_back(ScannedFile{name, static_cast<long long>(metadata.size()), "2026-01-01T00:00:00Z"});
        }
        ScannedVariant variant;
        variant.platform = "PS5";
        variant.title = "It Takes Two";
        variant.region = "Europe";
        variant.relativePath = "PPSA02343";
        variant.versions.push_back(std::move(version));
        scanner.results = {variant};
        indexer.scanStorage(storageId);
        variantId = variantRepository.getByStorageId(storageId).front().gameVariantId;
    }

    // Tells the fake Sony (and its fake server) what the latest package is.
    void sonyHas(const std::vector<std::string>& pieces, const std::string& metadata, const std::string& version = "01.500.000") {
        LatestRelease release;
        release.version = version;
        for (size_t i = 0; i < pieces.size(); ++i) addPiece(release, kContentId + "_" + std::to_string(i) + ".pkg", kBaseUrl, pieces[i]);
        if (!metadata.empty()) addPiece(release, kContentId + "_sc.pkg", kInfoUrl, metadata);
        for (const auto& piece : release.pieces) release.pieceSizes.push_back(piece.sizeBytes);
        sony.answer.status = LatestReleaseStatus::Found;
        sony.answer.release = release;
    }

    void addPiece(LatestRelease& release, const std::string& name, const std::string& base, const std::string& bytes) {
        ReleasePiece piece;
        piece.url = base + name;
        piece.sizeBytes = static_cast<long long>(bytes.size());
        piece.sha256 = sha256Of(bytes);
        release.pieces.push_back(piece);
        downloader.content[piece.url] = bytes;
    }

    fs::path file(const std::string& suffix) const { return root / (kContentId + suffix); }
    bool stagingLeftOver() const { return fs::exists(root / ".gth-sync-PPSA02343"); }
    bool backupsLeftOver() const {
        for (const auto& entry : fs::directory_iterator(root)) {
            if (entry.path().extension() == ".gth-old") return true;
        }
        return false;
    }
};

}  // namespace

TEST(PackageSyncServiceTest, AddsTheMissingMetadataPieceToAGameThatIsSonysLatest) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));

    const auto plan = f.sync.plan(f.variantId);
    EXPECT_EQ(plan.kind, SyncKind::CompleteMetadata) << plan.detail;
    EXPECT_EQ(plan.bytesToDownload, 2000);
    EXPECT_EQ(plan.latestVersion, "01.500.000");

    const auto result = f.sync.run(f.variantId, SyncMode::MetadataOnly);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.filesDownloaded, 1);
    EXPECT_EQ(result.bytesDownloaded, 2000);
    EXPECT_EQ(result.storageId, f.storageId);
    EXPECT_EQ(readFile(f.file("_sc.pkg")), metadataPiece(2000));
    EXPECT_EQ(readFile(f.file("_0.pkg")), pieces[0]) << "the game files themselves are never touched";
    EXPECT_FALSE(f.stagingLeftOver());
    ASSERT_EQ(f.scanner.pinnedVersions.size(), 1u);
    EXPECT_EQ(f.scanner.pinnedVersions[0], "") << "a stale hand-set version is handed back to automatic detection";
}

TEST(PackageSyncServiceTest, ADownloadedMetadataPieceThatDoesNotBelongToTheFilesIsRejected) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000, std::string(32, 'X')));  // carries some other build's digest

    const auto result = f.sync.run(f.variantId, SyncMode::MetadataOnly);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("doesn't match"), std::string::npos) << result.message;
    EXPECT_FALSE(fs::exists(f.file("_sc.pkg"))) << "nothing was added to the archive";
}

TEST(PackageSyncServiceTest, ADownloadThatFailsSonysChecksumIsDiscarded) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    f.downloader.content[kInfoUrl + kContentId + "_sc.pkg"] = std::string(2000, 'z');  // same size, wrong bytes

    const auto result = f.sync.run(f.variantId, SyncMode::MetadataOnly);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("failed verification"), std::string::npos) << result.message;
    EXPECT_FALSE(fs::exists(f.file("_sc.pkg")));
    EXPECT_FALSE(fs::exists(f.root / ".gth-sync-PPSA02343" / (kContentId + "_sc.pkg.part"))) << "the bad download is removed";
}

TEST(PackageSyncServiceTest, ADownloadFailureIsReportedAndLeavesTheArchiveAlone) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    f.downloader.failures[kInfoUrl + kContentId + "_sc.pkg"] = "couldn't connect";

    const auto result = f.sync.run(f.variantId, SyncMode::MetadataOnly);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("couldn't connect"), std::string::npos) << result.message;
    EXPECT_FALSE(fs::exists(f.file("_sc.pkg")));
}

TEST(PackageSyncServiceTest, ACompleteArchiveThatMatchesSonyHasNothingToDo) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces, metadataPiece(2000));
    f.sonyHas(pieces, metadataPiece(2000));

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.kind, SyncKind::NothingToDo);
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncServiceTest, OlderInstalledVersionIsUpdatedWhenPieceNamesAndSizesStillMatch) {
    SyncFixture f;
    const auto oldPieces = std::vector<std::string>{gamePiece(0, 600, std::string(32, 'O')), gamePiece(1, 400)};
    const auto latestPieces = std::vector<std::string>{gamePiece(0, 600, std::string(32, 'N')), gamePiece(1, 400)};
    f.installArchive(oldPieces, metadataPiece(2000, std::string(32, 'O')), "01.400.000");
    f.sonyHas(latestPieces, metadataPiece(2000, std::string(32, 'N')), "01.500.000");

    const auto plan = f.sync.plan(f.variantId);
    ASSERT_EQ(plan.kind, SyncKind::Update) << plan.detail;

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.kind, SyncKind::Update);
    EXPECT_EQ(readFile(f.file("_0.pkg")), latestPieces[0]);
    EXPECT_EQ(readFile(f.file("_1.pkg")), latestPieces[1]);
    EXPECT_EQ(readFile(f.file("_sc.pkg")), metadataPiece(2000, std::string(32, 'N')));
    EXPECT_EQ(result.filesDownloaded, 2) << "same-sized pieces are checked and only mismatches are fetched";
}

TEST(PackageSyncServiceTest, UnknownInstalledVersionDoesNotTrustSameSizeMetadataFromAnOlderBuild) {
    SyncFixture f;
    const auto oldPieces = std::vector<std::string>{gamePiece(0, 600, std::string(32, 'O')), gamePiece(1, 400)};
    const auto latestPieces = std::vector<std::string>{gamePiece(0, 600, std::string(32, 'N')), gamePiece(1, 400)};
    f.installArchive(oldPieces, metadataPiece(2000, std::string(32, 'O')));
    f.sonyHas(latestPieces, metadataPiece(2000, std::string(32, 'N')));

    const auto plan = f.sync.plan(f.variantId);
    ASSERT_EQ(plan.kind, SyncKind::Update) << plan.detail;

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(readFile(f.file("_0.pkg")), latestPieces[0]);
    EXPECT_EQ(readFile(f.file("_sc.pkg")), metadataPiece(2000, std::string(32, 'N')));
}

TEST(PackageSyncServiceTest, NewerInstalledVersionIsNotDowngradedByAStaleSonyLink) {
    SyncFixture f;
    const auto installedPieces = std::vector<std::string>{gamePiece(0, 600, std::string(32, 'N')), gamePiece(1, 400)};
    const auto stalePieces = std::vector<std::string>{gamePiece(0, 650, std::string(32, 'O')), gamePiece(1, 400)};
    f.installArchive(installedPieces, metadataPiece(2000, std::string(32, 'N')), "01.600.000");
    f.sonyHas(stalePieces, metadataPiece(2000, std::string(32, 'O')), "01.500.000");

    const auto plan = f.sync.plan(f.variantId);
    EXPECT_EQ(plan.kind, SyncKind::NothingToDo) << plan.detail;

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(f.downloader.calls.empty());
    EXPECT_EQ(readFile(f.file("_0.pkg")), installedPieces[0]);
}

TEST(PackageSyncServiceTest, AnOutOfDateGameIsLeftAloneWhenOnlyMetadataMayBeFetched) {
    SyncFixture f;
    f.installArchive({gamePiece(0, 600), gamePiece(1, 400)});
    f.sonyHas({gamePiece(0, 650, std::string(32, 'N')), gamePiece(1, 400)}, metadataPiece(2000, std::string(32, 'N')));

    const auto plan = f.sync.plan(f.variantId);
    EXPECT_EQ(plan.kind, SyncKind::Update);

    const auto result = f.sync.run(f.variantId, SyncMode::MetadataOnly);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.skippedNeedsUpdate);
    EXPECT_TRUE(f.downloader.calls.empty()) << "a multi-gigabyte update never starts by itself";
}

TEST(PackageSyncServiceTest, UpdatingReplacesOnlyThePiecesThatChangedAndChecksTheRest) {
    SyncFixture f;
    const std::vector<std::string> oldPieces = {gamePiece(0, 600, std::string(32, 'O')), gamePiece(1, 400), gamePiece(2, 300)};
    f.installArchive(oldPieces, metadataPiece(2000, std::string(32, 'O')));

    // The new package: piece 0 changed, piece 1 identical, piece 2 same size but different bytes, no piece 3.
    const std::vector<std::string> newPieces = {gamePiece(0, 600, std::string(32, 'N')), gamePiece(1, 400),
                                                std::string(300, 'Q')};
    f.sonyHas(newPieces, metadataPiece(2100, std::string(32, 'N')), "01.600.000");

    const auto plan = f.sync.plan(f.variantId);
    ASSERT_EQ(plan.kind, SyncKind::Update) << plan.detail;

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(readFile(f.file("_0.pkg")), newPieces[0]);
    EXPECT_EQ(readFile(f.file("_1.pkg")), newPieces[1]);
    EXPECT_EQ(readFile(f.file("_2.pkg")), newPieces[2]) << "same size, different content: caught by its checksum";
    EXPECT_EQ(readFile(f.file("_sc.pkg")), metadataPiece(2100, std::string(32, 'N')));
    EXPECT_EQ(result.filesDownloaded, 3) << "pieces 0 and 2, and the metadata piece; piece 1 was already right";
    for (const auto& [url, alreadyThere] : f.downloader.calls) {
        EXPECT_EQ(url.find("_1.pkg"), std::string::npos) << "piece 1 must not be downloaded again";
    }
    EXPECT_FALSE(f.stagingLeftOver());
    EXPECT_FALSE(f.backupsLeftOver());
}

TEST(PackageSyncServiceTest, UpdatingRemovesPiecesTheNewPackageNoLongerHas) {
    SyncFixture f;
    f.installArchive({gamePiece(0, 600, std::string(32, 'O')), gamePiece(1, 400), gamePiece(2, 300)}, metadataPiece(2000, std::string(32, 'O')));
    f.sonyHas({gamePiece(0, 700, std::string(32, 'N')), gamePiece(1, 400)}, metadataPiece(2000, std::string(32, 'N')));

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_FALSE(fs::exists(f.file("_2.pkg")));
    EXPECT_FALSE(f.backupsLeftOver());
}

TEST(PackageSyncServiceTest, AFailedUpdateChangesNothing) {
    SyncFixture f;
    const std::vector<std::string> oldPieces = {gamePiece(0, 600, std::string(32, 'O')), gamePiece(1, 400)};
    f.installArchive(oldPieces, metadataPiece(2000, std::string(32, 'O')));
    f.sonyHas({gamePiece(0, 650, std::string(32, 'N')), gamePiece(1, 450)}, metadataPiece(2000, std::string(32, 'N')));
    f.downloader.failures[kBaseUrl + kContentId + "_1.pkg"] = "connection reset";  // the second download dies

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(readFile(f.file("_0.pkg")), oldPieces[0]) << "the old game is still intact";
    EXPECT_EQ(readFile(f.file("_1.pkg")), oldPieces[1]);
    EXPECT_EQ(readFile(f.file("_sc.pkg")), metadataPiece(2000, std::string(32, 'O')));
}

TEST(PackageSyncServiceTest, CancellingKeepsThePartialDownloadAndTheNextRunResumesIt) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));

    f.downloader.cancelAfterHalf = true;
    const auto first = f.sync.run(f.variantId, SyncMode::MetadataOnly);
    EXPECT_TRUE(first.cancelled);
    EXPECT_FALSE(fs::exists(f.file("_sc.pkg")));
    const fs::path partial = f.root / ".gth-sync-PPSA02343" / (kContentId + "_sc.pkg.part");
    ASSERT_TRUE(fs::exists(partial));
    EXPECT_EQ(fs::file_size(partial), 1000u);

    f.downloader.cancelAfterHalf = false;
    f.downloader.calls.clear();
    const auto second = f.sync.run(f.variantId, SyncMode::MetadataOnly);

    ASSERT_TRUE(second.success) << second.message;
    ASSERT_EQ(f.downloader.calls.size(), 1u);
    EXPECT_EQ(f.downloader.calls[0].second, 1000) << "continued from what was already on disk";
    EXPECT_EQ(readFile(f.file("_sc.pkg")), metadataPiece(2000));
}

TEST(PackageSyncServiceTest, RefusesToDownloadFromAnythingThatIsNotSonysServers) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    f.sony.answer.release.pieces.back().url = "http://evil.example.com/" + kContentId + "_sc.pkg";

    const auto plan = f.sync.plan(f.variantId);

    EXPECT_EQ(plan.kind, SyncKind::Unavailable);
    EXPECT_NE(plan.detail.find("isn't safe"), std::string::npos) << plan.detail;
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncServiceTest, RefusesToUpdateWhenSonyDoesNotProvideAValidChecksum) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    f.sony.answer.release.pieces[0].sha256.clear();

    const auto plan = f.sync.plan(f.variantId);
    EXPECT_EQ(plan.kind, SyncKind::Unavailable);
    EXPECT_NE(plan.detail.find("valid SHA-256 checksum"), std::string::npos) << plan.detail;

    const auto result = f.sync.run(f.variantId, SyncMode::Full);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncServiceTest, RefusesFileNamesThatCouldEscapeTheGameFolder) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    f.sony.answer.release.pieces.back().url = kInfoUrl + "..%5C..%5Cevil_sc.pkg";
    EXPECT_EQ(f.sync.plan(f.variantId).kind, SyncKind::Unavailable);

    f.sony.answer.release.pieces.back().url = kInfoUrl + "x_sc.exe";
    EXPECT_EQ(f.sync.plan(f.variantId).kind, SyncKind::Unavailable);
}

TEST(PackageSyncServiceTest, WithoutASonyLinkThereIsNothingToSync) {
    SyncFixture f;
    f.installArchive({gamePiece(0, 600), gamePiece(1, 400)});
    f.sony.answer.status = LatestReleaseStatus::NoSonyLink;
    f.sony.answer.detail = "no Sony version link saved for PPSA02343";

    const auto plan = f.sync.plan(f.variantId);

    EXPECT_EQ(plan.kind, SyncKind::Unavailable);
    EXPECT_NE(plan.detail.find("PPSA02343"), std::string::npos);
    const auto result = f.sync.run(f.variantId, SyncMode::Full);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncServiceTest, ALocalPieceWithTheWrongNameIsNotOverwrittenBlindly) {
    SyncFixture f;
    f.installArchive({gamePiece(0, 600), gamePiece(1, 400)});
    fs::rename(f.file("_1.pkg"), f.root / "renamed-by-the-user.pkg");
    // The scanned file list still says the old name; make the database match what is really there.
    f.scanner.results[0].versions[0].files[1].relativePath = "renamed-by-the-user.pkg";
    f.indexer.scanStorage(f.storageId);
    f.sonyHas({gamePiece(0, 600), gamePiece(1, 400)}, metadataPiece(2000));

    EXPECT_EQ(f.sync.plan(f.variantId).kind, SyncKind::Unavailable);
}

TEST(PackageSyncServiceTest, RunAllHandlesEachGameAndCountsWhatHappened) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));

    const auto batch = f.sync.runAll(SyncMode::MetadataOnly);

    EXPECT_EQ(batch.completed, 1);
    EXPECT_EQ(batch.updated, 0);
    EXPECT_EQ(batch.failed, 0);
    EXPECT_EQ(batch.bytesDownloaded, 2000);
    ASSERT_EQ(batch.storageIds.size(), 1u);
    EXPECT_EQ(*batch.storageIds.begin(), f.storageId);

    // Once the library has been rescanned and shows the metadata piece, there is nothing left to do.
    f.scanner.results[0].versions[0].files.push_back(
        ScannedFile{kContentId + "_sc.pkg", 2000, "2026-01-01T00:00:00Z"});
    f.indexer.scanStorage(f.storageId);
    const auto again = f.sync.runAll(SyncMode::MetadataOnly);
    EXPECT_EQ(again.completed, 0);
    EXPECT_EQ(again.alreadyFine, 1);
    EXPECT_TRUE(again.storageIds.empty());
}

TEST(PackageSyncServiceTest, FilesWithSonysSizesButAnotherBuildsContentGetAFullUpdate) {
    SyncFixture f;
    // Every piece is the size Sony's package has, but piece 0 is from another build (different image digest).
    f.installArchive({gamePiece(0, 600, std::string(32, 'O')), gamePiece(1, 400)});
    const std::vector<std::string> sonyPieces = {gamePiece(0, 600, std::string(32, 'N')), gamePiece(1, 400)};
    f.sonyHas(sonyPieces, metadataPiece(2000, std::string(32, 'N')));

    EXPECT_EQ(f.sync.plan(f.variantId).kind, SyncKind::CompleteMetadata) << "by size alone it looks like Sony's package";

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.kind, SyncKind::Update);
    EXPECT_EQ(readFile(f.file("_0.pkg")), sonyPieces[0]) << "the piece with the wrong content was replaced";
    EXPECT_EQ(readFile(f.file("_1.pkg")), sonyPieces[1]);
    EXPECT_EQ(readFile(f.file("_sc.pkg")), metadataPiece(2000, std::string(32, 'N')));
    EXPECT_FALSE(f.stagingLeftOver());
    EXPECT_FALSE(f.backupsLeftOver());
}

TEST(PackageSyncServiceTest, WhenOnlyMetadataMayBeFetchedSuchFilesAreReportedAsNeedingAnUpdate) {
    SyncFixture f;
    const std::vector<std::string> oldPieces = {gamePiece(0, 600, std::string(32, 'O')), gamePiece(1, 400)};
    f.installArchive(oldPieces);
    f.sonyHas({gamePiece(0, 600, std::string(32, 'N')), gamePiece(1, 400)}, metadataPiece(2000, std::string(32, 'N')));

    const auto result = f.sync.run(f.variantId, SyncMode::MetadataOnly);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.skippedNeedsUpdate);
    EXPECT_EQ(result.kind, SyncKind::Update);
    EXPECT_EQ(readFile(f.file("_0.pkg")), oldPieces[0]) << "the archive is untouched";
    EXPECT_FALSE(fs::exists(f.file("_sc.pkg")));

    const auto batch = f.sync.runAll(SyncMode::MetadataOnly);
    EXPECT_EQ(batch.needUpdate, 1);
    EXPECT_EQ(batch.failed, 0);
}

TEST(PackageSyncServiceTest, RunAllCountsUpdatesItWasNotAllowedToMake) {
    SyncFixture f;
    f.installArchive({gamePiece(0, 600, std::string(32, 'O')), gamePiece(1, 400)});
    f.sonyHas({gamePiece(0, 700, std::string(32, 'N')), gamePiece(1, 400)}, metadataPiece(2000, std::string(32, 'N')));

    const auto batch = f.sync.runAll(SyncMode::MetadataOnly);

    EXPECT_EQ(batch.needUpdate, 1);
    EXPECT_EQ(batch.completed, 0);
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncServiceTest, RunAllStopsWhenCancelled) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    SyncCallbacks callbacks;
    callbacks.shouldCancel = [] { return true; };

    const auto batch = f.sync.runAll(SyncMode::MetadataOnly, callbacks);

    EXPECT_TRUE(batch.cancelled);
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(MetadataPieceBelongsToPackageTest, NeedsTheImageDigestFromThePartHeaderInsideTheMetadata) {
    const fs::path dir = fs::temp_directory_path() / "gth_metadata_belongs_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    writeFile(dir / "part0.pkg", gamePiece(0, 600));
    writeFile(dir / "matching_sc.pkg", metadataPiece(2000));
    writeFile(dir / "other_sc.pkg", metadataPiece(2000, std::string(32, 'X')));
    writeFile(dir / "notfih.pkg", std::string(600, 'a'));

    EXPECT_TRUE(MetadataPieceBelongsToPackage(dir / "part0.pkg", dir / "matching_sc.pkg"));
    EXPECT_FALSE(MetadataPieceBelongsToPackage(dir / "part0.pkg", dir / "other_sc.pkg"));
    EXPECT_FALSE(MetadataPieceBelongsToPackage(dir / "notfih.pkg", dir / "matching_sc.pkg")) << "not a PS5 package header";
    EXPECT_FALSE(MetadataPieceBelongsToPackage(dir / "part0.pkg", dir / "missing.pkg"));

    fs::remove_all(dir);
}

TEST(PackageSyncServiceTest, RefusesAPackageThatBelongsToADifferentEditionOfTheGame) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    // Sony's package for this title ID is named for another Content ID than the files on disk.
    for (auto& piece : f.sony.answer.release.pieces) {
        const auto at = piece.url.find(kContentId);
        ASSERT_NE(at, std::string::npos);
        piece.url.replace(at, kContentId.size(), "UP0006-PPSA02343_00-NUTSUS0000000000");
    }

    const auto plan = f.sync.plan(f.variantId);

    EXPECT_EQ(plan.kind, SyncKind::Unavailable);
    EXPECT_NE(plan.detail.find("different edition"), std::string::npos) << plan.detail;
    const auto result = f.sync.run(f.variantId, SyncMode::Full);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(f.downloader.calls.empty()) << "nothing is downloaded into a game's folder for the wrong edition";
}

TEST(PackageSyncServiceTest, ABatchListsWhatWasDoneAndWhatFailedPerGame) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));

    const auto batch = f.sync.runAll(SyncMode::MetadataOnly);

    ASSERT_EQ(batch.games.size(), 1u);
    EXPECT_EQ(batch.games[0].titleId, "PPSA02343");
    EXPECT_EQ(batch.games[0].kind, SyncKind::CompleteMetadata);
    EXPECT_TRUE(batch.games[0].success);
    EXPECT_EQ(batch.games[0].bytesDownloaded, 2000);

    f.downloader.failures[kInfoUrl + kContentId + "_sc.pkg"] = "couldn't connect";
    fs::remove(f.file("_sc.pkg"));
    const auto failed = f.sync.runAll(SyncMode::MetadataOnly);
    ASSERT_EQ(failed.games.size(), 1u);
    EXPECT_FALSE(failed.games[0].success);
    EXPECT_NE(failed.games[0].message.find("couldn't connect"), std::string::npos);
}

// ---- The download line -------------------------------------------------------------------

namespace {

const std::string kSecondContentId = "EP0006-PPSA20050_00-EASPORTSFC2025BG";
const std::string kSecondBaseUrl = "http://gst.prod.dl.playstation.net/gst/prod/00/PPSA20050_00/app/pkg/3/f_xyz/";
const std::string kSecondInfoUrl = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA20050_00/app/info/2/f_uvw/";

// Two games in the archive, PPSA02343 and PPSA20050, each with the given pieces (and metadata piece, if any).
void installTwoGames(SyncFixture& f, const std::vector<std::string>& firstPieces, const std::string& firstMetadata,
                     const std::vector<std::string>& secondPieces, const std::string& secondMetadata) {
    const auto makeVariant = [&](const std::string& contentId, const std::string& titleId, const std::string& title,
                                 const std::vector<std::string>& pieces, const std::string& metadata) {
        ScannedVersion version;
        version.relativePath = contentId;
        for (size_t i = 0; i < pieces.size(); ++i) {
            const std::string name = contentId + "_" + std::to_string(i) + ".pkg";
            writeFile(f.root / name, pieces[i]);
            version.files.push_back(ScannedFile{name, static_cast<long long>(pieces[i].size()), "2026-01-01T00:00:00Z"});
        }
        if (!metadata.empty()) {
            const std::string name = contentId + "_sc.pkg";
            writeFile(f.root / name, metadata);
            version.files.push_back(ScannedFile{name, static_cast<long long>(metadata.size()), "2026-01-01T00:00:00Z"});
        }
        ScannedVariant variant;
        variant.platform = "PS5";
        variant.title = title;
        variant.region = "Europe";
        variant.relativePath = titleId;
        variant.versions.push_back(std::move(version));
        return variant;
    };
    f.scanner.results = {makeVariant(kContentId, "PPSA02343", "It Takes Two", firstPieces, firstMetadata),
                         makeVariant(kSecondContentId, "PPSA20050", "EA SPORTS FC 25", secondPieces, secondMetadata)};
    f.indexer.scanStorage(f.storageId);
}

// What Sony says the latest package of one title is.
void sonyHasFor(SyncFixture& f, const std::string& titleId, const std::string& contentId, const std::string& baseUrl,
                const std::string& infoUrl, const std::vector<std::string>& pieces, const std::string& metadata) {
    LatestRelease release;
    release.version = "01.500.000";
    for (size_t i = 0; i < pieces.size(); ++i) f.addPiece(release, contentId + "_" + std::to_string(i) + ".pkg", baseUrl, pieces[i]);
    f.addPiece(release, contentId + "_sc.pkg", infoUrl, metadata);
    for (const auto& piece : release.pieces) release.pieceSizes.push_back(piece.sizeBytes);
    LatestReleaseResult result;
    result.status = LatestReleaseStatus::Found;
    result.release = release;
    f.sony.byTitle[titleId] = result;
}

}  // namespace

TEST(PackageSyncQueueTest, TheLineIsKnownBeforeTheFirstDownloadAndLeavesOutGamesThatNeedNothing) {
    SyncFixture f;
    const std::vector<std::string> current = {gamePiece(0, 600), gamePiece(1, 400)};
    const std::vector<std::string> other = {gamePiece(0, 300), gamePiece(1, 200)};
    // Game one is complete and current; game two is missing its metadata piece.
    installTwoGames(f, current, metadataPiece(2000), other, "");
    sonyHasFor(f, "PPSA02343", kContentId, kBaseUrl, kInfoUrl, current, metadataPiece(2000));
    sonyHasFor(f, "PPSA20050", kSecondContentId, kSecondBaseUrl, kSecondInfoUrl, other, metadataPiece(3000));

    std::vector<SyncQueueItem> queue;
    size_t downloadsWhenQueueWasAnnounced = 99;
    SyncCallbacks callbacks;
    callbacks.onQueue = [&](const std::vector<SyncQueueItem>& items) {
        queue = items;
        downloadsWhenQueueWasAnnounced = f.downloader.calls.size();
    };

    const auto batch = f.sync.runAll(SyncMode::Full, callbacks);

    ASSERT_EQ(queue.size(), 1u) << "only the game that needs something is in the line";
    EXPECT_EQ(queue[0].titleId, "PPSA20050");
    EXPECT_EQ(queue[0].kind, SyncKind::CompleteMetadata);
    EXPECT_EQ(queue[0].bytesToDownload, 3000);
    EXPECT_EQ(downloadsWhenQueueWasAnnounced, 0u) << "announced before anything was downloaded";
    EXPECT_EQ(batch.alreadyFine, 1);
    EXPECT_EQ(batch.completed, 1);
}

TEST(PackageSyncQueueTest, EveryGameReportsItsPlaceInTheLineAndItsOutcomeAsSoonAsItIsDone) {
    SyncFixture f;
    const std::vector<std::string> first = {gamePiece(0, 600), gamePiece(1, 400)};
    const std::vector<std::string> second = {gamePiece(0, 300), gamePiece(1, 200)};
    installTwoGames(f, first, "", second, "");
    sonyHasFor(f, "PPSA02343", kContentId, kBaseUrl, kInfoUrl, first, metadataPiece(2000));
    sonyHasFor(f, "PPSA20050", kSecondContentId, kSecondBaseUrl, kSecondInfoUrl, second, metadataPiece(3000));

    std::set<std::string> titlesSeenInProgress;
    std::set<int> positionsSeen;
    int lineLength = 0;
    std::vector<std::string> doneInOrder;
    SyncCallbacks callbacks;
    callbacks.onProgress = [&](const SyncProgress& progress) {
        titlesSeenInProgress.insert(progress.titleId);
        positionsSeen.insert(progress.gameIndex);
        lineLength = progress.gameCount;
    };
    callbacks.onGameDone = [&](const SyncGameOutcome& outcome) {
        EXPECT_TRUE(outcome.success);
        doneInOrder.push_back(outcome.titleId);
    };

    const auto batch = f.sync.runAll(SyncMode::Full, callbacks);

    EXPECT_EQ(batch.completed, 2);
    EXPECT_EQ(lineLength, 2);
    EXPECT_EQ(positionsSeen, (std::set<int>{1, 2}));
    EXPECT_EQ(titlesSeenInProgress, (std::set<std::string>{"PPSA02343", "PPSA20050"}));
    ASSERT_EQ(doneInOrder.size(), 2u);
    EXPECT_EQ(batch.games.size(), 2u);
    EXPECT_TRUE(fs::exists(f.file("_sc.pkg")));
    EXPECT_TRUE(fs::exists(f.root / (kSecondContentId + "_sc.pkg")));
}

TEST(PackageSyncQueueTest, CancellingPartWayKeepsTheGamesAlreadyFinishedAndLeavesTheRestUntouched) {
    SyncFixture f;
    const std::vector<std::string> first = {gamePiece(0, 600), gamePiece(1, 400)};
    const std::vector<std::string> second = {gamePiece(0, 300), gamePiece(1, 200)};
    installTwoGames(f, first, "", second, "");
    sonyHasFor(f, "PPSA02343", kContentId, kBaseUrl, kInfoUrl, first, metadataPiece(2000));
    sonyHasFor(f, "PPSA20050", kSecondContentId, kSecondBaseUrl, kSecondInfoUrl, second, metadataPiece(3000));

    int finished = 0;
    SyncCallbacks callbacks;
    callbacks.onGameDone = [&](const SyncGameOutcome&) { ++finished; };
    callbacks.shouldCancel = [&] { return finished >= 1; };  // stop as soon as the first game is done

    const auto batch = f.sync.runAll(SyncMode::Full, callbacks);

    EXPECT_TRUE(batch.cancelled);
    EXPECT_EQ(batch.completed, 1);
    const bool firstDone = fs::exists(f.file("_sc.pkg"));
    const bool secondDone = fs::exists(f.root / (kSecondContentId + "_sc.pkg"));
    EXPECT_NE(firstDone, secondDone) << "exactly one game was finished before the line was stopped";
}

TEST(PackageSyncQueueTest, AGameThatFailsDoesNotStopTheLine) {
    SyncFixture f;
    const std::vector<std::string> first = {gamePiece(0, 600), gamePiece(1, 400)};
    const std::vector<std::string> second = {gamePiece(0, 300), gamePiece(1, 200)};
    installTwoGames(f, first, "", second, "");
    sonyHasFor(f, "PPSA02343", kContentId, kBaseUrl, kInfoUrl, first, metadataPiece(2000));
    sonyHasFor(f, "PPSA20050", kSecondContentId, kSecondBaseUrl, kSecondInfoUrl, second, metadataPiece(3000));
    f.downloader.failures[kInfoUrl + kContentId + "_sc.pkg"] = "connection reset";

    const auto batch = f.sync.runAll(SyncMode::Full);

    EXPECT_EQ(batch.failed, 1);
    EXPECT_EQ(batch.completed, 1) << "the other game still got its turn";
    EXPECT_NE(batch.firstFailure.find("PPSA02343"), std::string::npos) << batch.firstFailure;
}

// ---- PS4 updates ----------------------------------------------------------------

namespace {

const std::string kPs4ContentId = "EP9000-CUSA07410_00-00000000GODOFWAR";
const std::string kPs4UpdateBase = "http://gs2.ww.prod.dl.playstation.net/gs2/ppkgo/prod/CUSA07410_00/30/f_1d7b/f/";

struct Ps4Fixture : SyncFixture {
    fs::path gameFolder = root / "God of War";

    // A PS4 game in "God of War/", at `version`.
    void installGame(const std::string& version) {
        fs::create_directories(gameFolder);
        const std::string name = "God of War/" + kPs4ContentId + ".pkg";
        const std::string bytes(500, 'g');
        writeFile(root / name, bytes);

        ScannedVersion scanned;
        scanned.version = version;
        scanned.relativePath = "God of War/" + kPs4ContentId;
        scanned.files.push_back(ScannedFile{name, static_cast<long long>(bytes.size()), "2026-01-01T00:00:00Z"});
        ScannedVariant variant;
        variant.platform = "PS4";
        variant.title = "God of War";
        variant.region = "Europe";
        variant.relativePath = "CUSA07410";
        variant.versions.push_back(std::move(scanned));
        scanner.results = {variant};
        indexer.scanStorage(storageId);
        variantId = variantRepository.getByStorageId(storageId).front().gameVariantId;
    }

    // Sony's latest update: one .pkg served as the consecutive pieces `parts`, named "<content id>-A0136-V0100_<n>.pkg",
    // with each piece's offset and the whole update's size, as Sony's manifest gives them.
    void sonyHasUpdate(const std::vector<std::string>& parts, const std::string& version = "01.36",
                       const std::string& contentId = kPs4ContentId) {
        LatestRelease release;
        release.version = version;
        long long offset = 0;
        for (size_t i = 0; i < parts.size(); ++i) {
            addPiece(release, contentId + "-A0136-V0100_" + std::to_string(i) + ".pkg", kPs4UpdateBase, parts[i]);
            release.pieces.back().offsetBytes = offset;
            offset += static_cast<long long>(parts[i].size());
        }
        release.packageSizeBytes = offset;
        sony.answer.status = LatestReleaseStatus::Found;
        sony.answer.release = release;
    }

    // A piece as Sony names it - where an older version of the app left it, loose in the Updates folder.
    fs::path updateFile(size_t index) const {
        return gameFolder / "Updates" / (kPs4ContentId + "-A0136-V0100_" + std::to_string(index) + ".pkg");
    }
    // The update itself: the one .pkg the pieces make up.
    fs::path joinedUpdate() const { return gameFolder / "Updates" / (kPs4ContentId + "-A0136-V0100.pkg"); }
};

}  // namespace

TEST(PackageSyncPs4Test, DownloadsTheUpdateIntoAnUpdatesFolderBesideTheGameAndChecksItFirst) {
    Ps4Fixture f;
    f.installGame("01.00");
    const std::vector<std::string> parts = {std::string(700, 'a'), std::string(300, 'b')};
    f.sonyHasUpdate(parts);

    const auto plan = f.sync.plan(f.variantId);
    EXPECT_EQ(plan.kind, SyncKind::Update) << plan.detail;
    EXPECT_EQ(plan.bytesToDownload, 1000);
    EXPECT_EQ(plan.latestVersion, "01.36");

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.kind, SyncKind::Update);
    EXPECT_EQ(result.filesDownloaded, 2);
    EXPECT_EQ(result.bytesDownloaded, 1000);
    EXPECT_EQ(result.storageId, f.storageId);
    EXPECT_EQ(readFile(f.joinedUpdate()), parts[0] + parts[1]) << "the pieces are joined into the one .pkg they are parts of";
    EXPECT_FALSE(fs::exists(f.updateFile(0))) << "no loose pieces are left for the scanner to mistake for a game";
    EXPECT_FALSE(fs::exists(f.updateFile(1)));
    EXPECT_EQ(readFile(f.gameFolder / (kPs4ContentId + ".pkg")), std::string(500, 'g')) << "the base game is never touched";
    EXPECT_FALSE(fs::exists(f.gameFolder / "Updates" / ".gth-sync-CUSA07410")) << "the staging folder is cleaned up";
}

TEST(PackageSyncPs4Test, RemovesAnOldOutdatedUpdateFileOnceTheNewOneIsVerifiedAndInPlace) {
    Ps4Fixture f;
    f.installGame("01.00");
    // An earlier run's update, still sitting in the Updates folder from before this game was bumped again -
    // a different version tag than what Sony has now, so a different file name.
    const fs::path oldUpdate = f.gameFolder / "Updates" / (kPs4ContentId + "-A0099-V0100.pkg");
    fs::create_directories(oldUpdate.parent_path());
    writeFile(oldUpdate, std::string(900, 'o'));

    const std::vector<std::string> parts = {std::string(700, 'a'), std::string(300, 'b')};
    f.sonyHasUpdate(parts);

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_FALSE(fs::exists(oldUpdate)) << "the superseded version's update file is removed once the new one is verified";
    EXPECT_EQ(readFile(f.joinedUpdate()), parts[0] + parts[1]) << "the new update is still put in place correctly";
    EXPECT_NE(result.message.find("Removed 1 outdated update file"), std::string::npos) << result.message;
}

TEST(PackageSyncPs4Test, NeverRemovesAnotherGamesFileEvenIfItSitsInTheSameUpdatesFolder) {
    Ps4Fixture f;
    f.installGame("01.00");
    // A different game's own update file - shares the Updates folder (this app groups them all under the
    // storage root's own "Updates" area in some layouts) but has nothing to do with this content ID.
    const fs::path unrelated = f.gameFolder / "Updates" / "EP0006-CUSA99999_00-SOMEOTHERGAME-A0050-V0100.pkg";
    fs::create_directories(unrelated.parent_path());
    writeFile(unrelated, std::string(400, 'x'));

    f.sonyHasUpdate({std::string(700, 'a'), std::string(300, 'b')});
    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(unrelated)) << "only files whose name starts with this exact game's content ID are ever removed";
}

TEST(PackageSyncPs4Test, AGameAtSonysLatestVersionNeedsNothing) {
    Ps4Fixture f;
    f.installGame("01.36");
    f.sonyHasUpdate({std::string(700, 'a')});

    EXPECT_EQ(f.sync.plan(f.variantId).kind, SyncKind::NothingToDo);
    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.kind, SyncKind::NothingToDo);
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncPs4Test, AGameSonyPublishesNoUpdateForIsAlreadyCurrent) {
    Ps4Fixture f;
    f.installGame("01.00");
    f.sony.answer.status = LatestReleaseStatus::NoUpdatePublished;

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.kind, SyncKind::NothingToDo);
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncPs4Test, MetadataOnlyModeNeverDownloadsAnUpdate) {
    Ps4Fixture f;
    f.installGame("01.00");
    f.sonyHasUpdate({std::string(700, 'a')});

    const auto result = f.sync.run(f.variantId, SyncMode::MetadataOnly);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.skippedNeedsUpdate);
    EXPECT_TRUE(f.downloader.calls.empty());
    EXPECT_FALSE(fs::exists(f.gameFolder / "Updates"));
}

TEST(PackageSyncPs4Test, AnUpdateThatFailsSonysChecksumIsDiscarded) {
    Ps4Fixture f;
    f.installGame("01.00");
    f.sonyHasUpdate({std::string(700, 'a')});
    f.downloader.content[kPs4UpdateBase + kPs4ContentId + "-A0136-V0100_0.pkg"] = std::string(700, 'z');  // right size, wrong bytes

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("failed verification"), std::string::npos) << result.message;
    EXPECT_FALSE(fs::exists(f.updateFile(0)));
    EXPECT_FALSE(fs::exists(f.joinedUpdate())) << "nothing unverified reaches the archive";
}

TEST(PackageSyncPs4Test, AnUpdateWithNoChecksumIsNotDownloaded) {
    Ps4Fixture f;
    f.installGame("01.00");
    f.sonyHasUpdate({std::string(700, 'a')});
    f.sony.answer.release.pieces[0].sha256.clear();

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_EQ(result.kind, SyncKind::Unavailable);
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncPs4Test, AnUpdateForADifferentEditionIsNotDownloadedIntoThisGame) {
    Ps4Fixture f;
    f.installGame("01.00");
    f.sonyHasUpdate({std::string(700, 'a')}, "01.36", "UP9000-CUSA07410_00-00000000GODOFWAR");

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_EQ(result.kind, SyncKind::Unavailable);
    EXPECT_NE(result.message.find("different edition"), std::string::npos) << result.message;
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncPs4Test, APieceAnOlderVersionSavedLooseIsReusedAndBecomesTheUpdate) {
    Ps4Fixture f;
    f.installGame("01.00");
    const std::vector<std::string> parts = {std::string(700, 'a')};
    f.sonyHasUpdate(parts);
    fs::create_directories(f.gameFolder / "Updates");
    writeFile(f.updateFile(0), parts[0]);  // an earlier run had already put it there, still named as Sony's piece

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.filesDownloaded, 0);
    EXPECT_TRUE(f.downloader.calls.empty());
    EXPECT_EQ(readFile(f.joinedUpdate()), parts[0]);
    EXPECT_FALSE(fs::exists(f.updateFile(0)));
}

TEST(PackageSyncPs4Test, LoosePiecesFromAnOlderRunAreJoinedWithTheOnesStillMissing) {
    Ps4Fixture f;
    f.installGame("01.00");
    const std::vector<std::string> parts = {std::string(700, 'a'), std::string(300, 'b'), std::string(120, 'c')};
    f.sonyHasUpdate(parts);
    fs::create_directories(f.gameFolder / "Updates");
    writeFile(f.updateFile(0), parts[0]);  // piece 0 is already there and right; 1 and 2 aren't

    EXPECT_EQ(f.sync.plan(f.variantId).bytesToDownload, 420) << "only what is missing counts";
    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.filesDownloaded, 2);
    EXPECT_EQ(readFile(f.joinedUpdate()), parts[0] + parts[1] + parts[2]);
    EXPECT_FALSE(fs::exists(f.updateFile(0))) << "the loose piece is part of the joined file now";
    EXPECT_FALSE(fs::exists(f.gameFolder / "Updates" / ".gth-sync-CUSA07410"));
}

TEST(PackageSyncPs4Test, AnUpdateThatIsAlreadyJoinedInTheUpdatesFolderIsNotFetchedAgain) {
    Ps4Fixture f;
    f.installGame("01.00");
    const std::vector<std::string> parts = {std::string(700, 'a'), std::string(300, 'b')};
    f.sonyHasUpdate(parts);
    fs::create_directories(f.gameFolder / "Updates");
    writeFile(f.joinedUpdate(), parts[0] + parts[1]);  // put there, but the library hasn't been rescanned since

    EXPECT_EQ(f.sync.plan(f.variantId).bytesToDownload, 0);
    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.filesDownloaded, 0);
    EXPECT_TRUE(f.downloader.calls.empty());
    EXPECT_EQ(result.storageId, f.storageId) << "so that the rescan happens";
}

TEST(PackageSyncPs4Test, AJoinedUpdateWithTheRightSizeButWrongBytesIsDownloadedAgain) {
    Ps4Fixture f;
    f.installGame("01.00");
    const std::vector<std::string> parts = {std::string(700, 'a'), std::string(300, 'b')};
    f.sonyHasUpdate(parts);
    fs::create_directories(f.gameFolder / "Updates");
    writeFile(f.joinedUpdate(), std::string(1000, 'x'));  // same size, but neither Sony piece matches

    EXPECT_EQ(f.sync.plan(f.variantId).bytesToDownload, 1000);

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.filesDownloaded, 2);
    EXPECT_EQ(f.downloader.calls.size(), 2u);
    EXPECT_EQ(readFile(f.joinedUpdate()), parts[0] + parts[1]);
}

TEST(PackageSyncPs4Test, PiecesThatAreNotConsecutivePartsOfOneFileAreRefused) {
    Ps4Fixture f;
    f.installGame("01.00");
    f.sonyHasUpdate({std::string(700, 'a'), std::string(300, 'b')});
    f.sony.answer.release.pieces[1].offsetBytes = 999;  // a gap: these can't be joined into one file

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_EQ(result.kind, SyncKind::Unavailable);
    EXPECT_NE(result.message.find("consecutive"), std::string::npos) << result.message;
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncPs4Test, PiecesThatDoNotAddUpToTheUpdatesSizeAreRefused) {
    Ps4Fixture f;
    f.installGame("01.00");
    f.sonyHasUpdate({std::string(700, 'a'), std::string(300, 'b')});
    f.sony.answer.release.packageSizeBytes = 5000;  // the update is bigger than the pieces listed: the list is incomplete

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_EQ(result.kind, SyncKind::Unavailable);
    EXPECT_NE(result.message.find("add up"), std::string::npos) << result.message;
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncPs4Test, PiecesThatAreNotNumberedAsPartsOfOneFileAreRefused) {
    Ps4Fixture f;
    f.installGame("01.00");
    f.sonyHasUpdate({std::string(700, 'a'), std::string(300, 'b')});
    f.sony.answer.release.pieces[1].url = kPs4UpdateBase + kPs4ContentId + "-A0999-V0100_1.pkg";  // another file's part

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    EXPECT_EQ(result.kind, SyncKind::Unavailable);
    EXPECT_TRUE(f.downloader.calls.empty());
}

TEST(PackageSyncPs4Test, PiecesLargerThanTheCopyBufferAreJoinedByteForByte) {
    Ps4Fixture f;
    f.installGame("01.00");
    // Sizes that are not multiples of the 4 MiB copy buffer, with content that changes along the way, so a
    // piece boundary or a buffer boundary put in the wrong place would show up as a difference.
    const auto pattern = [](size_t size, unsigned seed) {
        std::string bytes(size, 'x');
        for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<char>((i * 31 + seed + (i >> 12)) & 0xFF);
        return bytes;
    };
    const std::vector<std::string> parts = {pattern(5 * 1024 * 1024 + 17, 1), pattern(4 * 1024 * 1024, 2), pattern(3, 3)};
    f.sonyHasUpdate(parts);

    const auto result = f.sync.run(f.variantId, SyncMode::Full);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.filesDownloaded, 3);
    EXPECT_TRUE(readFile(f.joinedUpdate()) == parts[0] + parts[1] + parts[2]) << "the joined file differs from the pieces";
}

TEST(PackageSyncPs4Test, CancellingWhileJoiningKeepsTheDownloadedPiecesForTheNextRun) {
    Ps4Fixture f;
    f.installGame("01.00");
    const std::vector<std::string> parts = {std::string(700, 'a'), std::string(300, 'b')};
    f.sonyHasUpdate(parts);

    SyncCallbacks callbacks;
    callbacks.shouldCancel = [&f] { return f.downloader.calls.size() >= 2; };  // the moment both are downloaded
    const auto cancelled = f.sync.run(f.variantId, SyncMode::Full, callbacks);

    EXPECT_TRUE(cancelled.cancelled);
    EXPECT_FALSE(fs::exists(f.joinedUpdate())) << "a half-joined file is never left behind";
    EXPECT_TRUE(fs::exists(f.gameFolder / "Updates" / ".gth-sync-CUSA07410")) << "the verified pieces are kept";

    const auto resumed = f.sync.run(f.variantId, SyncMode::Full);
    ASSERT_TRUE(resumed.success) << resumed.message;
    EXPECT_EQ(readFile(f.joinedUpdate()), parts[0] + parts[1]);
}

TEST(PackageSyncPs4Test, TheDownloadLineIncludesPs4GamesAndAsksForTheirStorageToBeRescanned) {
    Ps4Fixture f;
    f.installGame("01.00");
    f.sonyHasUpdate({std::string(700, 'a')});

    const auto batch = f.sync.runAll(SyncMode::Full);

    EXPECT_EQ(batch.updated, 1);
    EXPECT_EQ(batch.failed, 0);
    EXPECT_EQ(batch.bytesDownloaded, 700);
    EXPECT_EQ(batch.storageIds.count(f.storageId), 1u);
    EXPECT_TRUE(fs::exists(f.joinedUpdate()));
}


// ---- Going back to games that failed for a reason that can pass -----------------------------------------------------

TEST(PackageSyncRetryTest, AGameThatFailsOnADroppedConnectionWorksOnTheRetry) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    f.downloader.failTimes[kInfoUrl + kContentId + "_sc.pkg"] = 1;  // the first attempt is reset, the second works

    SyncCallbacks callbacks;
    callbacks.retryRounds = 1;
    callbacks.retryPause = std::chrono::seconds(0);
    std::vector<SyncGameOutcome> told;
    callbacks.onGameDone = [&told](const SyncGameOutcome& outcome) { told.push_back(outcome); };
    const auto batch = f.sync.runAll(SyncMode::Full, callbacks);

    EXPECT_EQ(batch.failed, 0);
    EXPECT_EQ(batch.recoveredOnRetry, 1);
    EXPECT_EQ(batch.completed, 1);
    EXPECT_TRUE(batch.firstFailure.empty());
    ASSERT_EQ(batch.games.size(), 1u) << "the failure is replaced by the result, not listed beside it";
    EXPECT_TRUE(batch.games[0].success);
    EXPECT_EQ(batch.storageIds.count(f.storageId), 1u);
    ASSERT_EQ(told.size(), 2u) << "the row shows the failure and then the result";
    EXPECT_FALSE(told[0].success);
    EXPECT_TRUE(told[1].success);
    EXPECT_TRUE(fs::exists(f.file("_sc.pkg")));
}

TEST(PackageSyncRetryTest, AGameThatKeepsFailingIsTriedTheAgreedNumberOfTimesAndThenReportedOnce) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    const std::string url = kInfoUrl + kContentId + "_sc.pkg";
    f.downloader.failTimes[url] = 100;

    SyncCallbacks callbacks;
    callbacks.retryRounds = 2;
    callbacks.retryPause = std::chrono::seconds(0);
    const auto batch = f.sync.runAll(SyncMode::Full, callbacks);

    EXPECT_EQ(batch.failed, 1);
    EXPECT_EQ(batch.recoveredOnRetry, 0);
    EXPECT_EQ(f.downloader.calls.size(), 3u) << "the first go and two more rounds";
    ASSERT_EQ(batch.games.size(), 1u);
    EXPECT_FALSE(batch.games[0].success);
    EXPECT_NE(batch.firstFailure.find("PPSA02343"), std::string::npos) << batch.firstFailure;
    EXPECT_NE(batch.firstFailure.find("connection reset"), std::string::npos) << batch.firstFailure;
}

TEST(PackageSyncRetryTest, WithoutRetryRoundsAFailedGameStaysFailed) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    f.downloader.failTimes[kInfoUrl + kContentId + "_sc.pkg"] = 1;

    const auto batch = f.sync.runAll(SyncMode::Full);

    EXPECT_EQ(batch.failed, 1);
    EXPECT_EQ(f.downloader.calls.size(), 1u);
}

TEST(PackageSyncRetryTest, CancellingDuringTheWaitBeforeARetryStopsTheLine) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces);
    f.sonyHas(pieces, metadataPiece(2000));
    f.downloader.failTimes[kInfoUrl + kContentId + "_sc.pkg"] = 1;

    SyncCallbacks callbacks;
    callbacks.retryRounds = 1;
    callbacks.retryPause = std::chrono::seconds(30);
    callbacks.shouldCancel = [&f] { return !f.downloader.calls.empty(); };  // Cancel right after the first failure
    const auto batch = f.sync.runAll(SyncMode::Full, callbacks);

    EXPECT_TRUE(batch.cancelled);
    EXPECT_EQ(f.downloader.calls.size(), 1u) << "no retry was made, and the 30 s pause was not waited out";
}

TEST(PackageSyncMetadataPieceFilesTest, ListsThePiecesThatAreOnDiskByTitle) {
    SyncFixture f;
    const std::vector<std::string> pieces = {gamePiece(0, 600), gamePiece(1, 400)};
    f.installArchive(pieces, metadataPiece(2000));

    const auto files = f.sync.metadataPieceFiles();

    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].first, "PPSA02343");
    EXPECT_EQ(fs::path(files[0].second), f.file("_sc.pkg"));
}

TEST(PackageSyncMetadataPieceFilesTest, AGameWithoutTheMetadataPieceIsNotListed) {
    SyncFixture f;
    f.installArchive({gamePiece(0, 900)});

    EXPECT_TRUE(f.sync.metadataPieceFiles().empty());
}
