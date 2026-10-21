#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <vector>

#include "gth/indexing/EnhancedStorageScanner.h"

using gth::indexing::EnhancedStorageScanner;

namespace {

// A deterministic, offline stand-in for OnlineTitleLookupService - records
// every call it receives so tests can assert on whether (and how often) the
// scanner actually reached for it, without any real network involved.
class FakeTitleLookupService : public gth::core::ITitleLookupService {
public:
    std::optional<std::string> lookupTitle(const std::string& platform, const std::string& titleId) override {
        calls.push_back({platform, titleId});
        lastFailure_.clear();
        if (auto failure = failures.find(titleId); failure != failures.end()) {
            lastFailure_ = failure->second;
            return std::nullopt;
        }
        auto it = responses.find(titleId);
        return it == responses.end() ? std::nullopt : std::make_optional(it->second);
    }
    std::string lastFailureReason() const override { return lastFailure_; }

    std::unordered_map<std::string, std::string> responses;
    std::unordered_map<std::string, std::string> failures;  // titleId -> reason the lookup couldn't be answered
    std::string lastFailure_;
    std::vector<std::pair<std::string, std::string>> calls;
};

void writePkg(const std::filesystem::path& path, const std::string& padding = "x") {
    std::ofstream out(path, std::ios::binary);
    out << padding;
}

void appendU32be(std::string& buf, uint32_t v) {
    buf.push_back(static_cast<char>((v >> 24) & 0xFF));
    buf.push_back(static_cast<char>((v >> 16) & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>(v & 0xFF));
}
void appendU16be(std::string& buf, uint16_t v) {
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>(v & 0xFF));
}
void appendU64be(std::string& buf, uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) buf.push_back(static_cast<char>((v >> shift) & 0xFF));
}
void appendU16le(std::string& buf, uint16_t v) {
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void appendU32le(std::string& buf, uint32_t v) {
    for (int shift = 0; shift <= 24; shift += 8) buf.push_back(static_cast<char>((v >> shift) & 0xFF));
}
void appendU64le(std::string& buf, uint64_t v) {
    for (int shift = 0; shift <= 56; shift += 8) buf.push_back(static_cast<char>((v >> shift) & 0xFF));
}

// Builds a complete, spec-shaped PS5 FIH+CNT package (same layout validated
// in test_pkg_file_parser.cpp's ParsesReachablePs5FihCntAndParamJsonForTheTitle)
// as one contiguous buffer, so the caller can split it at an arbitrary byte
// offset to simulate a package split into transfer-sized parts.
std::string buildPs5PackageBytes(const std::string& titleId, const std::string& contentId,
                                  const std::string& titleName) {
    constexpr uint64_t scOffset = 0x60;
    constexpr uint32_t cntHeaderSize = 0x5A0;
    constexpr uint32_t entryTableRelative = cntHeaderSize;
    constexpr uint32_t entrySize = 0x20;
    constexpr uint32_t entryCount = 2;
    const uint64_t entryTableAbsolute = scOffset + entryTableRelative;
    const uint64_t namesAbsolute = entryTableAbsolute + entryCount * entrySize;

    const std::string names = std::string("unused", 7) + std::string("sce_sys/param.json", 19);
    const uint64_t paramJsonAbsolute = namesAbsolute + names.size();
    const std::string paramJson = "{\"titleId\":\"" + titleId + "\",\"contentId\":\"" + contentId +
                                   "\",\"localizedParameters\":{\"en-US\":{\"titleName\":\"" + titleName +
                                   "\"}}}";

    std::string fih;
    fih += std::string("\x7f\x46\x49\x48", 4);
    fih.push_back('\x00');
    fih.push_back('\x80');
    appendU16le(fih, 3);
    appendU32le(fih, 0);
    fih.append(4, '\0');
    appendU64le(fih, 0x10000);
    appendU64le(fih, 0);
    fih.append(0x58 - fih.size(), '\0');
    appendU64le(fih, scOffset);
    fih.append(0x60 - fih.size(), '\0');

    std::string cnt;
    appendU32be(cnt, 0x7F434E54);
    appendU32be(cnt, 0);
    cnt.append(8, '\0');
    appendU32be(cnt, entryCount);
    appendU16be(cnt, 0);
    cnt.append(2, '\0');
    appendU32be(cnt, entryTableRelative);
    cnt.append(4, '\0');
    appendU64be(cnt, 0);
    appendU64be(cnt, 0);
    cnt.append(16, '\0');
    std::string paddedContentId = contentId;
    paddedContentId.resize(48, '\0');
    cnt += paddedContentId;
    cnt.append(cntHeaderSize - cnt.size(), '\0');

    std::string entries;
    appendU32be(entries, 0x0200);
    appendU32be(entries, 0);
    appendU32be(entries, 0);
    appendU32be(entries, 0);
    appendU32be(entries, static_cast<uint32_t>(namesAbsolute - scOffset));
    appendU32be(entries, static_cast<uint32_t>(names.size()));
    entries.append(8, '\0');
    appendU32be(entries, 0x1000);
    appendU32be(entries, 7);
    appendU32be(entries, 0);
    appendU32be(entries, 0);
    appendU32be(entries, static_cast<uint32_t>(paramJsonAbsolute - scOffset));
    appendU32be(entries, static_cast<uint32_t>(paramJson.size()));
    entries.append(8, '\0');

    return fih + cnt + entries + names + paramJson;
}

}  // namespace

// Regression test for a real bug: two different pkg-scanned releases sitting
// directly in the same flat folder (no per-game subfolder - the normal
// layout for a pile of loose .pkg files, not the Platform/Title/Region
// convention) both computed variant.relativePath as "." (just the shared
// parent directory), which collided under the DB's UNIQUE(StorageId,
// RelativePath) index on GameVariants and silently merged two unrelated
// games into one row - every subsequent scan then reconciled both games'
// versions against that single shared variant, flip-flopping which one was
// "current" and reporting a spurious update every single scan.
TEST(EnhancedStorageScannerTest, TwoDifferentReleasesInTheSameFlatFolderGetDistinctVariantPaths) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_collision_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    writePkg(root / "EP0006-PPSA20050_00-EASPORTSFC2025BG_0.pkg");

    EnhancedStorageScanner scanner;
    auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 2u);
    EXPECT_NE(results[0].relativePath, results[1].relativePath)
        << "two different games ended up with the same variant.relativePath - they will collide under "
           "IX_GameVariants_Storage_Path and silently merge into one DB row";

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, SameReleaseSplitAcrossPartsInAFlatFolderStaysOneVariant) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_splitparts_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_1.pkg");
    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_2.pkg");

    EnhancedStorageScanner scanner;
    auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].versions.size(), 1u);
    EXPECT_EQ(results[0].versions[0].files.size(), 3u);

    std::filesystem::remove_all(root);
}

// Integration test for the cross-part title enrichment: builds one complete
// PS5 package (real param.json title included) and splits it across two
// physical files at an arbitrary byte boundary, mimicking a real transfer
// split. Neither part alone contains the embedded CNT header (it straddles
// the split point by construction), so this only passes if the scanner
// actually reads across both files as one virtual stream.
TEST(EnhancedStorageScannerTest, RecoversRealTitleAcrossASplitPs5Package) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_split_title_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const std::string full = buildPs5PackageBytes("PPSA99999", "IV0001-PPSA99999_00-TESTJSON00000000",
                                                    "My Split PS5 Game");

    // Split well inside the CNT header/entry table/names/param.json region
    // (which starts at 0x60), so neither half contains it whole.
    const size_t splitPoint = 0x60 + 0x100;
    ASSERT_LT(splitPoint, full.size());
    const std::string part0 = full.substr(0, splitPoint);
    const std::string part1 = full.substr(splitPoint);

    {
        std::ofstream out(root / "EP0006-PPSA99999_00-TESTJSON00000000_0.pkg", std::ios::binary);
        out.write(part0.data(), static_cast<std::streamsize>(part0.size()));
    }
    {
        std::ofstream out(root / "EP0006-PPSA99999_00-TESTJSON00000000_1.pkg", std::ios::binary);
        out.write(part1.data(), static_cast<std::streamsize>(part1.size()));
    }

    EnhancedStorageScanner scanner;
    auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].title, "My Split PS5 Game")
        << "expected the real title recovered by reading across both split parts, got the title ID fallback";
    EXPECT_EQ(results[0].platform, "PS5");

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, FirstScanWritesASidecarJsonNextToTheGamesFiles) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sidecar_write_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");

    EnhancedStorageScanner scanner;
    auto results = scanner.scan(root.string());
    ASSERT_EQ(results.size(), 1u);

    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";
    ASSERT_TRUE(std::filesystem::exists(sidecarPath)) << "expected a sidecar JSON next to the game's files";

    std::string content;
    {
        std::ifstream in(sidecarPath);
        content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(content.find("\"titleId\": \"PPSA02343\""), std::string::npos);
    EXPECT_NE(content.find("\"title\": \"PPSA02343\""), std::string::npos)
        << "with no real title discoverable, the sidecar's title should be the placeholder title ID";

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, RescanUsesAHandEditedSidecarTitleOverTheTitleId) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sidecar_edit_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");

    EnhancedStorageScanner scanner;
    auto firstScan = scanner.scan(root.string());
    ASSERT_EQ(firstScan.size(), 1u);
    ASSERT_EQ(firstScan[0].title, "PPSA02343");  // placeholder, as written by the first scan

    // Simulate the user hand-editing the sidecar to the real name.
    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";
    ASSERT_TRUE(std::filesystem::exists(sidecarPath));
    std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
    out << "{\n  \"title\": \"My Real Game Name\",\n  \"titleId\": \"PPSA02343\"\n}\n";
    out.close();

    auto secondScan = scanner.scan(root.string());
    ASSERT_EQ(secondScan.size(), 1u);
    EXPECT_EQ(secondScan[0].title, "My Real Game Name")
        << "a hand-edited sidecar title should override the title-ID placeholder on rescan";

    std::filesystem::remove_all(root);
}

// Regression test for a design flaw caught before shipping: an early version
// of this feature decided "is this a manual edit?" purely by comparing the
// stored title to the title ID, which meant *any* non-placeholder title -
// including the app's own previous auto-guess - would get permanently
// frozen the moment a better guess became available later. The
// "titleSource" field exists specifically so an "auto" sidecar can still be
// upgraded, while a "manual" one never is.
TEST(EnhancedStorageScannerTest, AutoSidecarGetsUpgradedButManualOneDoesNot) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sidecar_provenance_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";

    // The tracker site knows the real name - a better title than the stale guess below.
    FakeTitleLookupService lookup;
    lookup.responses["PPSA02343"] = "It Takes Two";
    EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);

    // Simulate a stale "auto" guess from an earlier scan/app version.
    {
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << "{\n  \"title\": \"Some Stale Auto Guess\",\n  \"titleSource\": \"auto\"\n}\n";
    }
    auto afterAuto = scanner.scan(root.string());
    ASSERT_EQ(afterAuto.size(), 1u);
    EXPECT_EQ(afterAuto[0].title, "It Takes Two")
        << "an 'auto' sidecar should be upgraded by a better title, not treated as a manual edit";

    // Now simulate a real manual edit, explicitly marked as such.
    {
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << "{\n  \"title\": \"My Hand-Picked Name\",\n  \"titleSource\": \"manual\"\n}\n";
    }
    auto afterManual = scanner.scan(root.string());
    ASSERT_EQ(afterManual.size(), 1u);
    EXPECT_EQ(afterManual[0].title, "My Hand-Picked Name")
        << "a 'manual' sidecar must never be overwritten by an auto-guess, however different";

    std::filesystem::remove_all(root);
}

// Neither the title ID nor (for this fixture) the content ID's distributor
// code resolves to a known region (see PkgFileParser::detectRegion), so it
// comes back "Unknown" - the sidecar's region field is the intended way to
// set the real one by hand, with the same auto/manual provenance rule as
// title.
TEST(EnhancedStorageScannerTest, RescanUsesAHandEditedSidecarRegionOverUnknown) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sidecar_region_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // "IV0001" has no verified distributor-code region mapping (unlike
    // "EP0006"/"UP0006"/etc.), keeping this fixture's fresh-scan region
    // "Unknown" - this test is about the sidecar override mechanism, not
    // content-ID region detection (that's covered in test_pkg_file_parser).
    writePkg(root / "IV0001-PPSA02343_00-NUTSEU0000000000_0.pkg");

    EnhancedStorageScanner scanner;
    auto firstScan = scanner.scan(root.string());
    ASSERT_EQ(firstScan.size(), 1u);
    ASSERT_EQ(firstScan[0].region, "Unknown");

    const auto sidecarPath = root / "IV0001-PPSA02343_00-NUTSEU0000000000.gth.json";
    ASSERT_TRUE(std::filesystem::exists(sidecarPath));
    {
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << "{\n  \"title\": \"PPSA02343\",\n  \"titleSource\": \"auto\",\n  \"region\": \"Japan\",\n  "
               "\"regionSource\": \"manual\"\n}\n";
    }

    auto secondScan = scanner.scan(root.string());
    ASSERT_EQ(secondScan.size(), 1u);
    EXPECT_EQ(secondScan[0].region, "Japan")
        << "a hand-edited, manually-marked region should override the Unknown default on rescan";
    EXPECT_EQ(secondScan[0].title, "PPSA02343") << "editing region shouldn't disturb the independently-tracked title";

    std::filesystem::remove_all(root);
}

// Regression test for a self-heal gap: sidecars written before the region
// fix have region="North America / Europe" with no regionSource field at
// all (that field didn't exist yet). Without special-casing that exact
// legacy value, the generic "non-placeholder legacy value looks manual"
// rule would wrongly freeze every already-scanned PS5 title on the old,
// now-known-wrong guess forever, since no real user would have hand-typed
// that specific string. Once unfrozen, this fixture's "EP0006" distributor
// code resolves to a real region ("Europe") rather than "Unknown" - the
// self-heal doesn't just unstick it, it also picks up the more accurate
// value immediately.
TEST(EnhancedStorageScannerTest, SelfHealsAPreFixLegacySidecarsWrongPs5RegionGuess) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sidecar_region_migration_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";

    // A pre-fix sidecar: has the old wrong guess, and predates regionSource.
    {
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << "{\n  \"title\": \"PPSA02343\",\n  \"titleSource\": \"auto\",\n  "
               "\"region\": \"North America / Europe\"\n}\n";
    }

    EnhancedStorageScanner scanner;
    auto rescan = scanner.scan(root.string());
    ASSERT_EQ(rescan.size(), 1u);
    EXPECT_EQ(rescan[0].region, "Europe")
        << "a pre-fix sidecar's old 'North America / Europe' guess should self-heal to this content ID's real "
           "region, not stay frozen";

    std::filesystem::remove_all(root);
}

// Regression test for a real bug: relativePath used to fold in the display
// title to disambiguate releases sharing a flat folder (see
// buildVariantsFromHits), which meant relativePath changed the moment a
// later scan found a better title - GameIndexerService matches an existing
// variant by (StorageId, RelativePath), so that churn made every title
// improvement look like a brand new variant, silently orphaning the old DB
// row as "Missing" instead of updating it in place. relativePath must stay
// identical across scans regardless of how much better the title gets.
TEST(EnhancedStorageScannerTest, RelativePathStaysStableWhenTheTitleImproves) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_stable_path_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");

    EnhancedStorageScanner scannerWithoutLookup;
    auto firstScan = scannerWithoutLookup.scan(root.string());
    ASSERT_EQ(firstScan.size(), 1u);
    ASSERT_EQ(firstScan[0].title, "PPSA02343");  // placeholder, nothing better available yet

    FakeTitleLookupService lookup;
    lookup.responses["PPSA02343"] = "It Takes Two";
    EnhancedStorageScanner scannerWithLookup(gth::indexing::FilesystemStorageScanner(), &lookup);
    auto secondScan = scannerWithLookup.scan(root.string());
    ASSERT_EQ(secondScan.size(), 1u);
    ASSERT_EQ(secondScan[0].title, "It Takes Two");

    EXPECT_EQ(firstScan[0].relativePath, secondScan[0].relativePath)
        << "relativePath must not change just because a rescan found a better title";

    std::filesystem::remove_all(root);
}

// When local parsing can't produce a real title (no embedded param.json/SFO
// reachable), the scanner should fall back to an online lookup by title ID
// and use whatever real name comes back, instead of leaving the bare title
// ID as the displayed title.
TEST(EnhancedStorageScannerTest, OnlineLookupFillsInTheTitleWhenLocalParsingCannotFindOne) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_online_lookup_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");

    FakeTitleLookupService lookup;
    lookup.responses["PPSA02343"] = "It Takes Two";

    EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
    auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].title, "It Takes Two")
        << "expected the online-looked-up title to fill in where local parsing only had the title ID";
    ASSERT_EQ(lookup.calls.size(), 1u);
    EXPECT_EQ(lookup.calls[0].first, "PS5");
    EXPECT_EQ(lookup.calls[0].second, "PPSA02343");

    std::filesystem::remove_all(root);
}

// A lookup that can't even get an answer (no internet, blocked site) used to
// look identical to "no names changed" with nothing to show why; the scan now
// reports resolved / not-listed / failed counts and the last failure reason so
// the UI can say what actually happened.
TEST(EnhancedStorageScannerTest, ReportsOnlineLookupOutcomesAndTheFailureReason) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_lookup_diagnostics_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");  // will resolve
    writePkg(root / "EP0006-PPSA20050_00-EASPORTSFC2025BG_0.pkg");  // site has no such title
    writePkg(root / "EP0006-PPSA30000_00-ABCDEFGHIJKLMNOP_0.pkg");  // lookup can't be answered at all

    FakeTitleLookupService lookup;
    lookup.responses["PPSA02343"] = "It Takes Two";
    lookup.failures["PPSA30000"] = "couldn't resolve the site's address (error 12007)";

    EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
    scanner.scan(root.string());

    const auto diagnostics = scanner.lastScanDiagnostics();
    EXPECT_EQ(diagnostics.titleLookupsAttempted, 3);
    EXPECT_EQ(diagnostics.titleLookupsResolved, 1);
    EXPECT_EQ(diagnostics.titleLookupsNotFound, 1);
    EXPECT_EQ(diagnostics.titleLookupsFailed, 1);
    EXPECT_EQ(diagnostics.lastTitleLookupFailure, "couldn't resolve the site's address (error 12007)");

    // A later scan starts from a clean slate rather than accumulating (3 + 2 would mean it added up). The game the
    // first scan already named isn't asked about again, so this time only the two that are still unnamed are.
    scanner.scan(root.string());
    EXPECT_EQ(scanner.lastScanDiagnostics().titleLookupsAttempted, 2);
    EXPECT_EQ(scanner.lastScanDiagnostics().titleLookupsResolved, 0);
    EXPECT_EQ(scanner.lastScanDiagnostics().releasesNamedFromFiles, 1);

    std::filesystem::remove_all(root);
}

static std::string readWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The version has the same auto/manual handling as title and region. Older
// sidecars stored a made-up "1.00" with no versionSource; that value must be
// treated as the placeholder it was, not mistaken for a hand-typed version
// and locked in forever.
TEST(EnhancedStorageScannerTest, ASidecarsOldMadeUpVersionIsReplacedNotLockedIn) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_version_legacy_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000-A0110-V0100_0.pkg");
    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";
    {
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << "{\n  \"title\": \"PPSA02343\",\n  \"titleSource\": \"auto\",\n  \"version\": \"1.00\"\n}\n";
    }

    EnhancedStorageScanner scanner;
    auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].versions.size(), 1u);
    EXPECT_EQ(results[0].versions[0].version, "01.10") << "the detected version should replace the old placeholder";
    EXPECT_NE(readWholeFile(sidecarPath).find("\"versionSource\": \"auto\""), std::string::npos);

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, AVersionSetByHandInTheSidecarIsNeverOverwritten) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_version_manual_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000-A0110-V0100_0.pkg");  // says 01.10
    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";

    EnhancedStorageScanner scanner;
    {
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << "{\n  \"version\": \"01.500.000\",\n  \"versionSource\": \"manual\"\n}\n";
    }
    auto explicitlyManual = scanner.scan(root.string());
    ASSERT_EQ(explicitlyManual.size(), 1u);
    EXPECT_EQ(explicitlyManual[0].versions[0].version, "01.500.000");

    // Typing a version into a sidecar without marking its source counts too.
    {
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << "{\n  \"version\": \"01.400.000\"\n}\n";
    }
    auto justTyped = scanner.scan(root.string());
    ASSERT_EQ(justTyped.size(), 1u);
    EXPECT_EQ(justTyped[0].versions[0].version, "01.400.000");

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, AVersionNobodyStatedStaysUnknownInsteadOfBecomingOnePointZeroZero) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_version_unknown_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");

    EnhancedStorageScanner scanner;
    auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].versions[0].version.empty()) << "got \"" << results[0].versions[0].version << "\"";
    EXPECT_NE(readWholeFile(root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json").find("\"version\": \"\""),
              std::string::npos);

    std::filesystem::remove_all(root);
}

// A scan that attempted no lookups used to say nothing at all about online
// titles, which looked exactly like the feature not being there. The
// diagnostics now account for every release: looked up, named from the game
// files already, or not something the tracker sites cover.
TEST(EnhancedStorageScannerTest, ExplainsWhyReleasesWereNotLookedUp) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_lookup_reasons_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // Named straight from the package's own param.json - no lookup needed.
    const std::string full = buildPs5PackageBytes("PPSA99999", "IV0001-PPSA99999_00-TESTJSON00000000",
                                                    "Locally Known Title");
    {
        std::ofstream out(root / "IV0001-PPSA99999_00-TESTJSON00000000_0.pkg", std::ios::binary);
        out.write(full.data(), static_cast<std::streamsize>(full.size()));
    }
    // A PS3 release - neither tracker site covers it.
    writePkg(root / "UP9000-NPUA80001_00-PSTHREEGAMELABEL_0.pkg");

    FakeTitleLookupService lookup;
    EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
    scanner.scan(root.string());

    const auto diagnostics = scanner.lastScanDiagnostics();
    EXPECT_EQ(diagnostics.titleLookupsAttempted, 0);
    EXPECT_EQ(diagnostics.releasesNamedFromFiles, 1);
    EXPECT_EQ(diagnostics.releasesNotEligible, 1);
    EXPECT_TRUE(lookup.calls.empty());

    std::filesystem::remove_all(root);
}

// The online lookup is a last resort - it must not be reached for a release
// whose real title was already recovered locally (the common case), since
// that would mean a network call on every single scan for no benefit.
TEST(EnhancedStorageScannerTest, OnlineLookupIsSkippedWhenLocalParsingAlreadyFoundTheRealTitle) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_online_lookup_skip_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const std::string full = buildPs5PackageBytes("PPSA99999", "IV0001-PPSA99999_00-TESTJSON00000000",
                                                    "Locally Known Title");
    std::ofstream out(root / "IV0001-PPSA99999_00-TESTJSON00000000_0.pkg", std::ios::binary);
    out.write(full.data(), static_cast<std::streamsize>(full.size()));
    out.close();

    FakeTitleLookupService lookup;
    lookup.responses["PPSA99999"] = "Should Never Be Used";

    EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
    auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].title, "Locally Known Title");
    EXPECT_TRUE(lookup.calls.empty()) << "online lookup should never be reached once local parsing already has the "
                                          "real title";

    std::filesystem::remove_all(root);
}

// A sidecar the user has already hand-edited (titleSource: manual) must
// keep winning even when the online lookup succeeds and returns a
// different name - the same provenance rule that already protects a manual
// edit from the local auto-guess must also protect it from this new source.
TEST(EnhancedStorageScannerTest, ManualSidecarTitleWinsOverASuccessfulOnlineLookup) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_online_lookup_manual_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";
    {
        std::ofstream sidecarOut(sidecarPath, std::ios::binary | std::ios::trunc);
        sidecarOut << "{\n  \"title\": \"My Hand-Picked Name\",\n  \"titleSource\": \"manual\"\n}\n";
    }

    FakeTitleLookupService lookup;
    lookup.responses["PPSA02343"] = "It Takes Two";

    EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
    auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].title, "My Hand-Picked Name")
        << "a manually-edited sidecar title must not be overwritten by a successful online lookup";

    std::filesystem::remove_all(root);
}

// PS5 dumps carry no readable version, so the user types one in (Games tab).
// It has to be saved where a rescan will find and respect it.
TEST(EnhancedStorageScannerTest, PinnedVersionSurvivesRescansAndKeepsTheRestOfTheSidecar) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_pin_version_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";

    EnhancedStorageScanner scanner;
    auto first = scanner.scan(root.string());
    ASSERT_EQ(first.size(), 1u);
    ASSERT_TRUE(first[0].versions[0].version.empty());

    // A field the user added by hand must come through the edit untouched.
    {
        std::string json = readWholeFile(sidecarPath);
        json.insert(json.rfind('}'), ",\n  \"myNote\": \"keep me\"\n");
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << json;
    }

    std::string error;
    ASSERT_TRUE(scanner.pinReleaseVersion(root.string(), first[0].versions[0].relativePath, "01.300.000", error))
        << error;

    const std::string json = readWholeFile(sidecarPath);
    EXPECT_NE(json.find("\"version\": \"01.300.000\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"versionSource\": \"manual\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"myNote\": \"keep me\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"titleId\": \"PPSA02343\""), std::string::npos) << json;

    auto rescanned = scanner.scan(root.string());
    ASSERT_EQ(rescanned.size(), 1u);
    EXPECT_EQ(rescanned[0].versions[0].version, "01.300.000") << "a rescan must not wipe a version the user set";

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, PinningAnEmptyVersionHandsItBackToAutomaticDetection) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_unpin_version_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000-A0110-V0100_0.pkg");  // file name says 01.10

    EnhancedStorageScanner scanner;
    auto first = scanner.scan(root.string());
    ASSERT_EQ(first.size(), 1u);
    const std::string versionPath = first[0].versions[0].relativePath;

    std::string error;
    ASSERT_TRUE(scanner.pinReleaseVersion(root.string(), versionPath, "01.999", error)) << error;
    EXPECT_EQ(scanner.scan(root.string())[0].versions[0].version, "01.999");

    ASSERT_TRUE(scanner.pinReleaseVersion(root.string(), versionPath, "", error)) << error;
    EXPECT_EQ(scanner.scan(root.string())[0].versions[0].version, "01.10")
        << "clearing the manual version should let the detected one back in";

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, PinningWorksBeforeAnySidecarExistsAndInsideSubfolders) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_pin_subfolder_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "PS5" / "It Takes Two");

    writePkg(root / "PS5" / "It Takes Two" / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    const std::string versionPath =
        (std::filesystem::path("PS5") / "It Takes Two" / "EP0006-PPSA02343_00-NUTSEU0000000000").string();

    EnhancedStorageScanner scanner;
    std::string error;
    ASSERT_TRUE(scanner.pinReleaseVersion(root.string(), versionPath, "01.500.000", error)) << error;
    EXPECT_TRUE(std::filesystem::exists(root / "PS5" / "It Takes Two" / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json"));

    auto results = scanner.scan(root.string());
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].versions[0].version, "01.500.000");
    EXPECT_EQ(results[0].versions[0].relativePath, versionPath);

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, PinningExplainsWhyWhenThereIsNowhereToSaveIt) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_pin_fail_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    EnhancedStorageScanner scanner;
    std::string error;

    // No Content ID: the release is keyed by its .pkg file name.
    EXPECT_FALSE(scanner.pinReleaseVersion(root.string(), "loose_game.pkg", "01.00", error));
    EXPECT_NE(error.find("Content ID"), std::string::npos) << error;

    // The game's folder is gone (drive unplugged, share offline).
    error.clear();
    EXPECT_FALSE(scanner.pinReleaseVersion(
        root.string(), (std::filesystem::path("Missing") / "EP0006-PPSA02343_00-NUTSEU0000000000").string(), "01.00",
        error));
    EXPECT_NE(error.find("isn't reachable"), std::string::npos) << error;

    std::filesystem::remove_all(root);
}

// ---- The metadata piece ("<content id>_sc.pkg") of a Sony PS5 package -------

namespace {

// The 0x60-byte start of a PS5 FIH wrapper whose embedded metadata block
// begins at `scOffset` - i.e. right after the last game piece.
std::string buildFihStart(uint64_t scOffset) {
    std::string fih;
    fih += std::string("\x7f\x46\x49\x48", 4);
    fih.push_back('\x00');
    fih.push_back('\x80');
    appendU16le(fih, 3);
    appendU32le(fih, 0);
    fih.append(4, '\0');
    appendU64le(fih, 0x10000);
    appendU64le(fih, 0);
    fih.append(0x58 - fih.size(), '\0');
    appendU64le(fih, scOffset);
    fih.append(0x60 - fih.size(), '\0');
    return fih;
}

// A metadata piece: the \x7fCNT header, its entry table, and the names and
// param.json data. With `originCopyFirst` the table lists an "origin-param.json"
// (an older version) before the package's own "param.json", as real ones can.
std::string buildMetadataPieceBytes(const std::string& titleId, const std::string& contentId,
                                    const std::string& titleName, const std::string& contentVersion,
                                    bool originCopyFirst = false) {
    constexpr uint32_t cntHeaderSize = 0x5A0;
    constexpr uint32_t entrySize = 0x20;
    const uint32_t entryCount = originCopyFirst ? 3 : 2;

    const std::string paramName = "param.json";
    const std::string originName = "origin-param.json";
    const std::string names = std::string("unused", 7) + paramName + std::string(1, '\0') + originName + std::string(1, '\0');
    const uint32_t paramNameOffset = 7;
    const uint32_t originNameOffset = static_cast<uint32_t>(7 + paramName.size() + 1);

    const auto makeParamJson = [&](const std::string& version) {
        return "{\"titleId\":\"" + titleId + "\",\"contentId\":\"" + contentId + "\",\"contentVersion\":\"" + version +
               "\",\"localizedParameters\":{\"en-US\":{\"titleName\":\"" + titleName + "\"}}}";
    };
    const std::string paramJson = makeParamJson(contentVersion);
    const std::string originJson = makeParamJson("01.000.000");

    const uint32_t entryTableEnd = cntHeaderSize + entryCount * entrySize;
    const uint32_t namesOffset = entryTableEnd;
    const uint32_t paramOffset = namesOffset + static_cast<uint32_t>(names.size());
    const uint32_t originOffset = paramOffset + static_cast<uint32_t>(paramJson.size());

    std::string cnt;
    appendU32be(cnt, 0x7F434E54);
    appendU32be(cnt, 0);
    cnt.append(8, '\0');
    appendU32be(cnt, entryCount);
    appendU16be(cnt, 0);
    cnt.append(2, '\0');
    appendU32be(cnt, cntHeaderSize);
    cnt.append(4, '\0');
    appendU64be(cnt, 0);
    appendU64be(cnt, 0);
    cnt.append(16, '\0');
    std::string paddedContentId = contentId;
    paddedContentId.resize(48, '\0');
    cnt += paddedContentId;
    cnt.append(cntHeaderSize - cnt.size(), '\0');

    const auto entry = [&](uint32_t id, uint32_t nameOffset, uint32_t dataOffset, uint32_t size) {
        std::string e;
        appendU32be(e, id);
        appendU32be(e, nameOffset);
        appendU32be(e, 0);
        appendU32be(e, 0);
        appendU32be(e, dataOffset);
        appendU32be(e, size);
        e.append(8, '\0');
        return e;
    };

    std::string entries = entry(0x0200, 0, namesOffset, static_cast<uint32_t>(names.size()));
    if (originCopyFirst) entries += entry(0x2100, originNameOffset, originOffset, static_cast<uint32_t>(originJson.size()));
    entries += entry(0x2000, paramNameOffset, paramOffset, static_cast<uint32_t>(paramJson.size()));

    return cnt + entries + names + paramJson + (originCopyFirst ? originJson : std::string());
}

// A complete release on disk: two game pieces (the first starts with the FIH
// wrapper) and the metadata piece that follows them.
void writeCompletedPs5Release(const std::filesystem::path& folder, const std::string& contentId,
                              const std::string& titleId, const std::string& titleName,
                              const std::string& contentVersion, bool originCopyFirst = false) {
    constexpr size_t pieceSize = 0x100;
    std::string part0 = buildFihStart(2 * pieceSize);
    part0.resize(pieceSize, '\0');
    const std::string part1(pieceSize, '\0');

    const auto write = [&](const std::string& name, const std::string& bytes) {
        std::ofstream out(folder / name, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    write(contentId + "_0.pkg", part0);
    write(contentId + "_1.pkg", part1);
    write(contentId + "_sc.pkg", buildMetadataPieceBytes(titleId, contentId, titleName, contentVersion, originCopyFirst));
}

}  // namespace

TEST(EnhancedStorageScannerTest, ACompletedPs5ReleaseIsOneGameWithItsRealTitleAndVersion) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sc_piece_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    writeCompletedPs5Release(root, "EP0006-PPSA99999_00-TESTJSON00000000", "PPSA99999", "My Completed Game", "01.500.000");

    EnhancedStorageScanner scanner;
    const auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u) << "the metadata piece must not show up as a game of its own";
    EXPECT_EQ(results[0].platform, "PS5");
    EXPECT_EQ(results[0].title, "My Completed Game");
    ASSERT_EQ(results[0].versions.size(), 1u);
    EXPECT_EQ(results[0].versions[0].version, "01.500.000") << "the version comes from the package's own param.json";
    ASSERT_EQ(results[0].versions[0].files.size(), 3u);
    bool hasMetadataPiece = false;
    for (const auto& file : results[0].versions[0].files) {
        if (file.relativePath.find("_sc.pkg") != std::string::npos) hasMetadataPiece = true;
    }
    EXPECT_TRUE(hasMetadataPiece) << "the piece is part of the release's files, so its size counts";

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, TheVersionFromTheMetadataPieceIsWrittenToTheSidecar) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sc_sidecar_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string contentId = "EP0006-PPSA99999_00-TESTJSON00000000";
    writeCompletedPs5Release(root, contentId, "PPSA99999", "My Completed Game", "01.500.000");

    EnhancedStorageScanner scanner;
    scanner.scan(root.string());

    const std::string sidecar = readWholeFile(root / (contentId + ".gth.json"));
    EXPECT_NE(sidecar.find("\"version\": \"01.500.000\""), std::string::npos) << sidecar;
    EXPECT_NE(sidecar.find("\"versionSource\": \"auto\""), std::string::npos) << sidecar;

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, AMetadataPieceOnItsOwnIsNotAGame) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sc_orphan_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string contentId = "EP0006-PPSA99999_00-TESTJSON00000000";
    const std::string bytes = buildMetadataPieceBytes("PPSA99999", contentId, "Orphan", "01.000.000");
    {
        std::ofstream out(root / (contentId + "_sc.pkg"), std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    EnhancedStorageScanner scanner;
    EXPECT_TRUE(scanner.scan(root.string()).empty());

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, ThePackagesOwnParamJsonWinsOverTheOriginCopy) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sc_origin_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    writeCompletedPs5Release(root, "EP0006-PPSA99999_00-TESTJSON00000000", "PPSA99999", "Origin Test", "01.500.000",
                             /*originCopyFirst=*/true);

    EnhancedStorageScanner scanner;
    const auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].versions[0].version, "01.500.000")
        << "read the version out of origin-param.json (the version the update starts from) instead";

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, AMetadataPieceOnlyCompletesTheReleaseItsContentIdNames) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_sc_other_release_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    writeCompletedPs5Release(root, "EP0006-PPSA99999_00-TESTJSON00000000", "PPSA99999", "Game One", "01.500.000");
    // A second release with no metadata piece of its own must not borrow the first one's.
    writePkg(root / "EP0006-PPSA88888_00-OTHERGAME0000000_0.pkg");

    EnhancedStorageScanner scanner;
    const auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 2u);
    for (const auto& variant : results) {
        if (variant.title == "Game One") {
            EXPECT_EQ(variant.versions[0].version, "01.500.000");
        } else {
            EXPECT_TRUE(variant.versions[0].version.empty()) << "got " << variant.versions[0].version;
            EXPECT_EQ(variant.versions[0].files.size(), 1u);
        }
    }

    std::filesystem::remove_all(root);
}

// ---- A name a scan found is never lost to a scan that couldn't find it ----------------------------------------------
//
// Regression: once the online lookup had named a library, ANY later scan that couldn't reach the site (offline, the
// site throttling a big library, a share that hiccuped) fell back to the bare title ID and wrote it over the saved
// name - in the sidecar and in the library - so every game went back to "PPSA02343". Update All Games rescans, so it
// showed up as soon as scanning became routine.

TEST(EnhancedStorageScannerTest, AResolvedNameSurvivesLaterScansThatCouldNotResolveIt) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_name_survives_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";

    {
        FakeTitleLookupService lookup;
        lookup.responses["PPSA02343"] = "It Takes Two";
        EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
        const auto first = scanner.scan(root.string());
        ASSERT_EQ(first.size(), 1u);
        EXPECT_EQ(first[0].title, "It Takes Two");
    }
    EXPECT_NE(readWholeFile(sidecarPath).find("\"titleSource\": \"online\""), std::string::npos)
        << "the sidecar should say the name came from a lookup";

    {
        // The next scan: the site can't be reached.
        FakeTitleLookupService lookup;
        lookup.failures["PPSA02343"] = "couldn't resolve the site's address (error 12007)";
        EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
        const auto second = scanner.scan(root.string());
        ASSERT_EQ(second.size(), 1u);
        EXPECT_EQ(second[0].title, "It Takes Two") << "a lookup that failed must not turn the name back into the ID";
    }
    {
        // And one with no lookup service at all (an offline context).
        EnhancedStorageScanner scanner;
        const auto third = scanner.scan(root.string());
        ASSERT_EQ(third.size(), 1u);
        EXPECT_EQ(third[0].title, "It Takes Two");
    }
    EXPECT_NE(readWholeFile(sidecarPath).find("\"title\": \"It Takes Two\""), std::string::npos)
        << "the saved name must still be in the sidecar";

    std::filesystem::remove_all(root);
}

TEST(EnhancedStorageScannerTest, AGameAlreadyNamedIsNotLookedUpAgain) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_no_repeat_lookup_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");

    {
        FakeTitleLookupService lookup;
        lookup.responses["PPSA02343"] = "It Takes Two";
        EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
        scanner.scan(root.string());
        ASSERT_EQ(lookup.calls.size(), 1u);
    }

    FakeTitleLookupService lookup;
    lookup.responses["PPSA02343"] = "A Different Name";
    EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
    const auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].title, "It Takes Two");
    EXPECT_TRUE(lookup.calls.empty()) << "asking the site again for a game that already has its name is what let a "
                                          "slow site rename a whole library";
    const auto diagnostics = scanner.lastScanDiagnostics();
    EXPECT_EQ(diagnostics.titleLookupsAttempted, 0);
    EXPECT_EQ(diagnostics.releasesNamedFromFiles, 1);

    std::filesystem::remove_all(root);
}

// A sidecar saved by an older version says only "auto", even when its name was a real one from a lookup. It can't be
// told from a guess, so it is looked up again - but it is still never traded for the bare ID.
TEST(EnhancedStorageScannerTest, ALegacyAutoNameIsKeptAgainstTheIdAndUpgradedByALookup) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_legacy_auto_name_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    writePkg(root / "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg");
    const auto sidecarPath = root / "EP0006-PPSA02343_00-NUTSEU0000000000.gth.json";
    {
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << "{\n  \"title\": \"It Takes Two\",\n  \"titleSource\": \"auto\"\n}\n";
    }

    {
        FakeTitleLookupService lookup;
        lookup.failures["PPSA02343"] = "timed out";
        EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
        const auto results = scanner.scan(root.string());
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0].title, "It Takes Two");
    }
    {
        FakeTitleLookupService lookup;
        lookup.responses["PPSA02343"] = "It Takes Two (Renamed On The Site)";
        EnhancedStorageScanner scanner(gth::indexing::FilesystemStorageScanner(), &lookup);
        const auto results = scanner.scan(root.string());
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0].title, "It Takes Two (Renamed On The Site)");
    }
    EXPECT_NE(readWholeFile(sidecarPath).find("\"titleSource\": \"online\""), std::string::npos);

    std::filesystem::remove_all(root);
}

// The file-name guess (a humanized label) is weaker than a name a lookup found: it must not replace it either.
TEST(EnhancedStorageScannerTest, AFileNameGuessNeverReplacesAVerifiedName) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_guess_vs_verified_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    writePkg(root / "EP0006-PPSA20050_00-EASPORTSFC2025BG_0.pkg");
    {
        std::ofstream out(root / "EP0006-PPSA20050_00-EASPORTSFC2025BG.gth.json", std::ios::binary | std::ios::trunc);
        out << "{\n  \"title\": \"EA SPORTS FC 25\",\n  \"titleSource\": \"online\"\n}\n";
    }

    EnhancedStorageScanner scanner;  // no lookup: the only fresh title is the guess made from the file name
    const auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].title, "EA SPORTS FC 25");

    std::filesystem::remove_all(root);
}

// ...while the package's own name, when it becomes readable, does win over an online one.
TEST(EnhancedStorageScannerTest, ThePackagesOwnNameUpgradesAnOnlineName) {
    const auto root = std::filesystem::temp_directory_path() / "gth_enhanced_scanner_package_beats_online_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string full = buildPs5PackageBytes("PPSA99999", "IV0001-PPSA99999_00-TESTJSON00000000", "The Package Own Name");
    {
        std::ofstream out(root / "IV0001-PPSA99999_00-TESTJSON00000000_0.pkg", std::ios::binary);
        out.write(full.data(), static_cast<std::streamsize>(full.size()));
    }
    const auto sidecarPath = root / "IV0001-PPSA99999_00-TESTJSON00000000.gth.json";
    {
        std::ofstream out(sidecarPath, std::ios::binary | std::ios::trunc);
        out << "{\n  \"title\": \"An Online Name\",\n  \"titleSource\": \"online\"\n}\n";
    }

    EnhancedStorageScanner scanner;
    const auto results = scanner.scan(root.string());

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].title, "The Package Own Name");
    EXPECT_NE(readWholeFile(sidecarPath).find("\"titleSource\": \"package\""), std::string::npos);

    std::filesystem::remove_all(root);
}
