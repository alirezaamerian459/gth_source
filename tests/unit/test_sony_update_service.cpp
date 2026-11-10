#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gth/network/SonyPieceDownloader.h"
#include "gth/network/SonyUpdateService.h"

using namespace gth::network;
using gth::core::LatestReleaseStatus;

namespace {

// Real answers from Sony's servers (trimmed of nothing that matters), so the
// parsers are checked against the actual shape rather than a guess at it.
const char* kPs4VerXml =
    "<?xml version='1.0' encoding='UTF-8'?>\n"
    "<titlepatch titleid=\"CUSA07410\"><tag name=\"30\" mandatory=\"true\"><package version=\"01.36\" "
    "size=\"8023703552\" digest=\"E0EEDB72A45AE6CEE34439FC5A30AA6F59803742BF50E1D9F4BBCC35947C47E5\" "
    "manifest_url=\"http://gs2.ww.prod.dl.playstation.net/gs2/ppkgo/prod/CUSA07410_00/30/f_1d7b/f/"
    "EP9000-CUSA07410_00-00000000GODOFWAR-A0136-V0100.json\" content_id=\"EP9000-CUSA07410_00-00000000GODOFWAR\" "
    "system_ver=\"273711104\" type=\"cumulative\" remaster=\"false\" patchgo=\"true\"><delta_info_set "
    "url=\"http://gs2.ww.prod.dl.playstation.net/x-DP.pkg\"/><paramsfo><title>God of War</title></paramsfo>"
    "</package></tag></titlepatch>";

const char* kPs5VersionXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><title_patch ac_set_rev=\"0\" nptitleid=\"PPSA02343_00\" "
    "schema_ver=\"1.0\">\n"
    "    <app_tag content_id=\"EP0006-PPSA02343_00-NUTSEU0000000000\" name=\"_Import20250514130501\" revision=\"9\">\n"
    "        <package content_ver=\"01.500.000\" delta_url=\"http://gst.prod.dl.playstation.net/x-DP.pkg\" "
    "digest=\"323100401b8def09\" mandatory=\"false\" "
    "manifest_url=\"https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA02343_00/app/info/9/f_d5af/"
    "EP0006-PPSA02343_00-NUTSEU0000000000.json\" metadata_ver=\"9\" pfs_revision=\"7\" system_ver=\"287309829\"/>\n"
    "    </app_tag>\n"
    "    <pssdc><nptitleid>PPSA08560_00</nptitleid></pssdc>\n"
    "</title_patch>\n";

const char* kPs5ManifestJson =
    "{\"originalFileSize\":45498368000,\"packageDigest\":\"0220\",\"numberOfSplitFiles\":12,\"pieces\":["
    "{\"url\":\"http://x/_0.pkg\",\"fileOffset\":0,\"fileSize\":4294967296,\"hashValue\":\"6bbf\"},"
    "{\"url\":\"http://x/_1.pkg\",\"fileOffset\":4294967296,\"fileSize\":4294967296,\"hashValue\":\"aaaa\"},"
    "{\"url\":\"http://x/_2.pkg\",\"fileOffset\":8589934592,\"fileSize\":2459566080,\"hashValue\":\"bbbb\"},"
    "{\"url\":\"http://x/_3.pkg\",\"fileOffset\":11049500672,\"fileSize\":89128960,\"hashValue\":\"cccc\"}],"
    "\"playgoChunkCrcHashValue\":\"3f5d\"}";

const char* kItTakesTwoLink =
    "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/b437c912-d234-49fa-8321-ec699a103e05-version.xml";
const char* kItTakesTwoManifestUrl =
    "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA02343_00/app/info/9/f_d5af/"
    "EP0006-PPSA02343_00-NUTSEU0000000000.json";

// Answers by URL and remembers what was asked.
struct FakeSony {
    std::map<std::string, SonyHttpResponse> answers;
    std::vector<std::string> asked;

    SonyFetcher fetcher() {
        return [this](const std::string& url) {
            asked.push_back(url);
            const auto it = answers.find(url);
            if (it != answers.end()) return it->second;
            SonyHttpResponse missing;
            missing.status = 404;
            return missing;
        };
    }
    void answer(const std::string& url, int status, std::string body = {}) {
        SonyHttpResponse response;
        response.status = status;
        response.body = std::move(body);
        answers[url] = std::move(response);
    }
    void unreachable(const std::string& url, std::string why = "couldn't connect") {
        SonyHttpResponse response;
        response.failure = std::move(why);
        answers[url] = std::move(response);
    }
};

struct TempLinksFile {
    std::filesystem::path path = gth::test::testTempRoot() / "gth_sony_links_test.tsv";
    TempLinksFile() { std::filesystem::remove(path); }
    ~TempLinksFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    void write(const std::string& text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
};

}  // namespace

TEST(SonyPs4UpdateUrlTest, MatchesTheAddressSonysServerActuallyServes) {
    // Checked against the live server: this exact address answered with God of War's update file.
    EXPECT_EQ(SonyPs4UpdateUrl("CUSA07410"),
              "http://gs-sec.ww.np.dl.playstation.net/plo/np/CUSA07410/"
              "27c80b57b8c94c34b951a43f24395c8be72d6b67dade7d4d975eb598cae997a2/CUSA07410-ver.xml");
    EXPECT_EQ(SonyPs4UpdateUrl("cusa07410"), SonyPs4UpdateUrl("CUSA07410")) << "title IDs are case-insensitive";
}

TEST(SonyParsersTest, ReadsThePs4UpdateFile) {
    const auto info = ParseSonyPs4UpdateXml(kPs4VerXml);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->version, "01.36");
    EXPECT_EQ(info->sizeBytes, 8023703552LL);
    EXPECT_NE(info->manifestUrl.find("GODOFWAR-A0136-V0100.json"), std::string::npos);
}

TEST(SonyParsersTest, PicksTheNewestWhenSeveralPackagesAreListed) {
    const auto info = ParseSonyPs4UpdateXml(
        "<titlepatch><tag><package version=\"01.05\" size=\"1\"/></tag>"
        "<tag><package version=\"01.36\" size=\"3\"/></tag><tag><package version=\"01.09\" size=\"2\"/></tag></titlepatch>");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->version, "01.36");
    EXPECT_EQ(info->sizeBytes, 3);
}

TEST(SonyParsersTest, ReadsThePs5VersionFileWithoutMixingUpTheVersionLikeAttributes) {
    const auto info = ParseSonyPs5VersionXml(kPs5VersionXml);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->version, "01.500.000") << "content_ver, not metadata_ver or system_ver";
    EXPECT_EQ(info->manifestUrl, kItTakesTwoManifestUrl);
}

TEST(SonyParsersTest, RejectsAnythingThatIsNotAVersionFile) {
    EXPECT_FALSE(ParseSonyPs4UpdateXml("").has_value());
    EXPECT_FALSE(ParseSonyPs4UpdateXml("<html><body>403 ERROR</body></html>").has_value());
    EXPECT_FALSE(ParseSonyPs5VersionXml("<title_patch><packages content_ver=\"1.0\"/></title_patch>").has_value())
        << "<packages> is not <package>";
}

TEST(SonyParsersTest, ReadsEveryPieceOfThePackageManifest) {
    const auto manifest = ParseSonyPackageManifest(kPs5ManifestJson);
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->originalFileSize, 45498368000LL);
    ASSERT_EQ(manifest->pieceSizes.size(), 4u);
    EXPECT_EQ(manifest->pieceSizes[0], 4294967296LL);
    EXPECT_EQ(manifest->pieceSizes[2], 2459566080LL);
    EXPECT_EQ(manifest->pieceSizes[3], 89128960LL);
    EXPECT_FALSE(ParseSonyPackageManifest("not json").has_value());
}

TEST(SonyParsersTest, ReadsTheVersionLinksInTheFormatsTheScenePublishes) {
    const auto links = ParseSonyVersionLinks(
        "contentId\ttitleName\tversionFileUri\n"
        "IP9100-PPSA01280_00-SFSRELE000000100\tShare Factory Studio\t"
        "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA01280_00/d8ec167a-59da-4e54-8e2c-1161c706516a-version.xml\r\n"
        "# a comment line\n"
        "\n"
        "ppsa02343, https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/uuid-version.xml\n"
        "PPSA20050\t\"https://example.test/abc-version.xml\"\n"
        "no url on this line PPSA99999\n");

    ASSERT_EQ(links.size(), 3u);
    EXPECT_EQ(links.at("PPSA01280"),
              "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA01280_00/d8ec167a-59da-4e54-8e2c-1161c706516a-version.xml");
    EXPECT_EQ(links.at("PPSA02343"),
              "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/uuid-version.xml");
    EXPECT_EQ(links.at("PPSA20050"), "https://example.test/abc-version.xml")
        << "with no title ID in the address, the one before it on the line is used";
}

TEST(SonyCertificatePinTest, OnlyTrustsAnUnknownRootThatIsTheSonyOne) {
    const std::string sony = "51d5a7833f67f1d1c6212f75997e4b83dada0f7910ba4b5a3fa4e9b7b9bc827c";
    const unsigned long kUntrustedRoot = 0x20;

    EXPECT_TRUE(IsPinnedSonyChain(kUntrustedRoot, sony));
    EXPECT_TRUE(IsPinnedSonyChain(0, "anything")) << "a chain Windows fully trusts needs no pin";

    EXPECT_FALSE(IsPinnedSonyChain(kUntrustedRoot, "0000000000000000000000000000000000000000000000000000000000000000"))
        << "someone else's self-made root";
    EXPECT_FALSE(IsPinnedSonyChain(kUntrustedRoot, ""));
    EXPECT_FALSE(IsPinnedSonyChain(kUntrustedRoot | 0x1, sony)) << "expired certificate on top of the unknown root";
    EXPECT_FALSE(IsPinnedSonyChain(0x1, sony)) << "any other problem";
    EXPECT_FALSE(IsPinnedSonyChain(kUntrustedRoot | 0x4, sony)) << "revoked/other status";
}

TEST(SonyUpdateServiceTest, Ps4ReportsTheLatestUpdate) {
    FakeSony sony;
    sony.answer(SonyPs4UpdateUrl("CUSA07410"), 200, kPs4VerXml);
    SonyUpdateService service({}, sony.fetcher());

    const auto result = service.latestRelease("PS4", "CUSA07410");

    ASSERT_EQ(result.status, LatestReleaseStatus::Found) << result.detail;
    EXPECT_EQ(result.release.version, "01.36");
    EXPECT_EQ(result.release.packageSizeBytes, 8023703552LL);
    EXPECT_TRUE(result.release.pieceSizes.empty());
}

TEST(SonyUpdateServiceTest, Ps4WithNoUpdateFileMeansThereIsNoUpdate) {
    FakeSony sony;  // every address answers 404
    SonyUpdateService service({}, sony.fetcher());
    EXPECT_EQ(service.latestRelease("PS4", "CUSA00001").status, LatestReleaseStatus::NoUpdatePublished);
}

TEST(SonyUpdateServiceTest, Ps4FailuresAreExplained) {
    FakeSony sony;
    sony.answer(SonyPs4UpdateUrl("CUSA00002"), 500);
    sony.answer(SonyPs4UpdateUrl("CUSA00003"), 200, "<html>captive portal</html>");
    sony.unreachable(SonyPs4UpdateUrl("CUSA00004"), "couldn't connect to Sony's server");
    SonyUpdateService service({}, sony.fetcher());

    const auto serverError = service.latestRelease("PS4", "CUSA00002");
    EXPECT_EQ(serverError.status, LatestReleaseStatus::Failed);
    EXPECT_NE(serverError.detail.find("500"), std::string::npos) << serverError.detail;

    const auto garbage = service.latestRelease("PS4", "CUSA00003");
    EXPECT_EQ(garbage.status, LatestReleaseStatus::Failed);
    EXPECT_FALSE(garbage.detail.empty());

    const auto offline = service.latestRelease("PS4", "CUSA00004");
    EXPECT_EQ(offline.status, LatestReleaseStatus::Failed);
    EXPECT_NE(offline.detail.find("couldn't connect"), std::string::npos) << offline.detail;
}

TEST(SonyUpdateServiceTest, Ps5WithoutASavedLinkSaysThereIsNowhereToAsk) {
    FakeSony sony;
    TempLinksFile links;
    SonyUpdateService service({links.path}, sony.fetcher());

    const auto result = service.latestRelease("PS5", "PPSA02343");

    EXPECT_EQ(result.status, LatestReleaseStatus::NoSonyLink);
    EXPECT_NE(result.detail.find("PPSA02343"), std::string::npos);
    EXPECT_TRUE(sony.asked.empty()) << "no link means no request";
}

TEST(SonyUpdateServiceTest, Ps5ReadsTheVersionAndThePackageSizesFromSony) {
    FakeSony sony;
    sony.answer(kItTakesTwoLink, 200, kPs5VersionXml);
    sony.answer(kItTakesTwoManifestUrl, 200, kPs5ManifestJson);
    TempLinksFile links;
    links.write(std::string("PPSA02343\t") + kItTakesTwoLink + "\n");
    SonyUpdateService service({links.path}, sony.fetcher());

    const auto result = service.latestRelease("PS5", "ppsa02343");

    ASSERT_EQ(result.status, LatestReleaseStatus::Found) << result.detail;
    EXPECT_EQ(result.release.version, "01.500.000");
    EXPECT_EQ(result.release.packageSizeBytes, 45498368000LL);
    ASSERT_EQ(result.release.pieceSizes.size(), 4u);
    EXPECT_EQ(result.release.pieceSizes.back(), 89128960LL);
}

TEST(SonyUpdateServiceTest, Ps5StillReportsTheVersionWhenTheManifestCannotBeFetched) {
    FakeSony sony;
    sony.answer(kItTakesTwoLink, 200, kPs5VersionXml);
    sony.unreachable(kItTakesTwoManifestUrl);
    TempLinksFile links;
    links.write(std::string("PPSA02343\t") + kItTakesTwoLink + "\n");
    SonyUpdateService service({links.path}, sony.fetcher());

    const auto result = service.latestRelease("PS5", "PPSA02343");

    ASSERT_EQ(result.status, LatestReleaseStatus::Found) << result.detail;
    EXPECT_EQ(result.release.version, "01.500.000");
    EXPECT_TRUE(result.release.pieceSizes.empty());
}

TEST(SonyUpdateServiceTest, Ps5SavedLinkThatSonyNoLongerServesIsCalledOutOfDate) {
    FakeSony sony;  // the saved link answers 404
    TempLinksFile links;
    links.write(std::string("PPSA02343\t") + kItTakesTwoLink + "\n");
    SonyUpdateService service({links.path}, sony.fetcher());

    const auto result = service.latestRelease("PS5", "PPSA02343");

    EXPECT_EQ(result.status, LatestReleaseStatus::Failed);
    EXPECT_NE(result.detail.find("out of date"), std::string::npos) << result.detail;
}

TEST(SonyUpdateServiceTest, LinksAppendedWhileTheAppIsRunningArePickedUp) {
    FakeSony sony;
    sony.answer(kItTakesTwoLink, 200, kPs5VersionXml);
    TempLinksFile links;
    SonyUpdateService service({links.path}, sony.fetcher());

    EXPECT_EQ(service.latestRelease("PS5", "PPSA02343").status, LatestReleaseStatus::NoSonyLink);
    EXPECT_EQ(service.knownPs5LinkCount(), 0u);

    links.write(std::string("PPSA02343\t") + kItTakesTwoLink + "\n");  // e.g. a download automation writes it

    EXPECT_EQ(service.latestRelease("PS5", "PPSA02343").status, LatestReleaseStatus::Found);
    EXPECT_EQ(service.knownPs5LinkCount(), 1u);
}

TEST(SonyUpdateServiceTest, LaterLinkFilesOverrideEarlierOnes) {
    FakeSony sony;
    sony.answer("https://example.test/new-version.xml", 200, kPs5VersionXml);
    const auto dir = gth::test::testTempRoot();
    const auto first = dir / "gth_sony_links_first.tsv";
    const auto second = dir / "gth_sony_links_second.tsv";
    { std::ofstream(first) << "PPSA02343\thttps://example.test/old-version.xml\n"; }
    { std::ofstream(second) << "PPSA02343\thttps://example.test/new-version.xml\n"; }

    SonyUpdateService service({first, second, dir / "gth_sony_links_missing.tsv"}, sony.fetcher());
    EXPECT_EQ(service.latestRelease("PS5", "PPSA02343").status, LatestReleaseStatus::Found);

    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

TEST(SonyUpdateServiceTest, OtherPlatformsAreNotSonysToAnswer) {
    FakeSony sony;
    SonyUpdateService service({}, sony.fetcher());
    const auto result = service.latestRelease("PS3", "NPUB30001");
    EXPECT_EQ(result.status, LatestReleaseStatus::Failed);
    EXPECT_FALSE(result.detail.empty());
    EXPECT_TRUE(sony.asked.empty());
}

TEST(SonyUpdateServiceTest, StopsAskingForAWhileWhenSonyCannotBeReached) {
    FakeSony sony;
    for (const char* id : {"CUSA00010", "CUSA00011", "CUSA00012", "CUSA00013"}) sony.unreachable(SonyPs4UpdateUrl(id));
    SonyUpdateService service({}, sony.fetcher());

    for (const char* id : {"CUSA00010", "CUSA00011", "CUSA00012"}) {
        EXPECT_EQ(service.latestRelease("PS4", id).status, LatestReleaseStatus::Failed);
    }
    ASSERT_EQ(sony.asked.size(), 3u);

    const auto skipped = service.latestRelease("PS4", "CUSA00013");
    EXPECT_EQ(skipped.status, LatestReleaseStatus::Failed);
    EXPECT_NE(skipped.detail.find("skipped after repeated failures"), std::string::npos) << skipped.detail;
    EXPECT_EQ(sony.asked.size(), 3u) << "no fourth request while backing off";
}

TEST(SonyParsersTest, ReadsEachPiecesAddressAndChecksumFromTheManifest) {
    const auto manifest = ParseSonyPackageManifest(kPs5ManifestJson);
    ASSERT_TRUE(manifest.has_value());
    ASSERT_EQ(manifest->pieces.size(), 4u);
    EXPECT_EQ(manifest->pieces[0].url, "http://x/_0.pkg");
    EXPECT_EQ(manifest->pieces[0].sizeBytes, 4294967296LL);
    EXPECT_EQ(manifest->pieces[0].sha256, "6bbf");
    EXPECT_EQ(manifest->pieces[3].url, "http://x/_3.pkg");
    EXPECT_EQ(manifest->pieces[3].sizeBytes, 89128960LL);
    EXPECT_EQ(manifest->pieces[0].offsetBytes, 0LL);
    EXPECT_EQ(manifest->pieces[1].offsetBytes, 4294967296LL) << "where each piece starts inside the whole package";
    EXPECT_EQ(manifest->pieces[3].offsetBytes, 11049500672LL);
    EXPECT_EQ(manifest->pieces[3].sha256, "cccc");
}

TEST(SonyParsersTest, ManifestChecksumsAreLowerCasedAndEscapedSlashesAreUnescaped) {
    const auto manifest = ParseSonyPackageManifest(
        "{\"originalFileSize\":10,\"pieces\":[{\"url\":\"http:\\/\\/gst.prod.dl.playstation.net\\/a\\/b_0.pkg\","
        "\"fileOffset\":0,\"fileSize\":10,\"hashValue\":\"ABCDEF12\"}]}");
    ASSERT_TRUE(manifest.has_value());
    ASSERT_EQ(manifest->pieces.size(), 1u);
    EXPECT_EQ(manifest->pieces[0].url, "http://gst.prod.dl.playstation.net/a/b_0.pkg");
    EXPECT_EQ(manifest->pieces[0].sha256, "abcdef12");
}

TEST(SonyUpdateServiceTest, Ps5CarriesTheAddressAndChecksumOfEveryPiece) {
    FakeSony sony;
    sony.answer(kItTakesTwoLink, 200, kPs5VersionXml);
    sony.answer(kItTakesTwoManifestUrl, 200, kPs5ManifestJson);
    TempLinksFile links;
    links.write(std::string("PPSA02343\t") + kItTakesTwoLink + "\n");
    SonyUpdateService service({links.path}, sony.fetcher());

    const auto result = service.latestRelease("PS5", "PPSA02343");

    ASSERT_EQ(result.status, LatestReleaseStatus::Found) << result.detail;
    ASSERT_EQ(result.release.pieces.size(), 4u);
    EXPECT_EQ(result.release.pieces[2].url, "http://x/_2.pkg");
    EXPECT_EQ(result.release.pieces[2].sha256, "bbbb");
}

TEST(SonyPieceDownloaderTest, OnlyAcceptsSonysOwnHosts) {
    EXPECT_TRUE(IsSonyDownloadUrl("http://gst.prod.dl.playstation.net/gst/prod/00/PPSA02343_00/app/pkg/7/f/a_0.pkg"));
    EXPECT_TRUE(IsSonyDownloadUrl("https://sgst.prod.dl.playstation.net/sgst/prod/00/x_sc.pkg"));
    EXPECT_FALSE(IsSonyDownloadUrl("http://example.com/a_0.pkg"));
    EXPECT_FALSE(IsSonyDownloadUrl("http://playstation.net.evil.example/a_0.pkg"));
    EXPECT_FALSE(IsSonyDownloadUrl("http://evilplaystation.net/a_0.pkg")) << "must be a subdomain, not just a matching ending";
    EXPECT_FALSE(IsSonyDownloadUrl("not a url"));
}

TEST(SonyPieceDownloaderTest, RefusesForeignAddressesWithoutTouchingTheNetwork) {
    SonyPieceDownloader downloader;
    const auto destination = gth::test::testTempRoot() / "gth_downloader_refuse_test" / "x.pkg.part";

    const auto result = downloader.download("http://example.com/x.pkg", destination, 10, nullptr);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_NE(result.error.find("isn't on Sony's servers"), std::string::npos) << result.error;
    EXPECT_FALSE(std::filesystem::exists(destination));

    const auto noSize = downloader.download("http://gst.prod.dl.playstation.net/x.pkg", destination, 0, nullptr);  // NOSONAR: zero-size guard test returns before network access.
    EXPECT_FALSE(noSize.success);
}

namespace {

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct TempFolder {
    std::filesystem::path path = gth::test::testTempRoot() / "gth_sony_links_folder_test";
    TempFolder() {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

}  // namespace

TEST(SonyLinksFileTest, MergingKeepsTheLinksAlreadyThereAndTheIncomingOneWins) {
    TempFolder folder;
    const auto file = folder.path / "sony-version-links.tsv";
    {
        std::ofstream out(file);
        out << "PPSA02343\thttps://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/old-version.xml\n"
               "PPSA20050\thttps://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA20050_00/mine-version.xml\n";
    }

    const auto result = MergeSonyVersionLinks(
        file,
        "contentId\ttitleName\tversionFileUri\n"
        "EP0006-PPSA02343_00-NUTSEU0000000000\tIt Takes Two\t"
        "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/new-version.xml\n"
        "IP9100-PPSA01280_00-SFSRELE000000100\tShare Factory Studio\t"
        "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA01280_00/sfs-version.xml\n");

    EXPECT_TRUE(result.written);
    EXPECT_EQ(result.incoming, 2u);
    EXPECT_EQ(result.total, 3u);
    const auto links = ParseSonyVersionLinks(slurp(file));
    ASSERT_EQ(links.size(), 3u);
    EXPECT_NE(links.at("PPSA02343").find("new-version.xml"), std::string::npos) << "the incoming link replaces the old one";
    EXPECT_NE(links.at("PPSA20050").find("mine-version.xml"), std::string::npos) << "a link that was already there stays";
    EXPECT_EQ(links.count("PPSA01280"), 1u);
}

TEST(SonyLinksFileTest, MergingCreatesTheFileAndLeavesItAloneWhenThereIsNothingToAdd) {
    TempFolder folder;
    const auto file = folder.path / "sub" / "sony-version-links.tsv";

    const auto nothing = MergeSonyVersionLinks(file, "no links in this text at all\n");
    EXPECT_EQ(nothing.incoming, 0u);
    EXPECT_FALSE(nothing.written);
    EXPECT_FALSE(std::filesystem::exists(file)) << "an empty download must not create an empty links file";

    const auto created = MergeSonyVersionLinks(file, "PPSA02343\thttps://example.test/a-version.xml\n");
    EXPECT_TRUE(created.written);
    EXPECT_EQ(created.total, 1u);
    EXPECT_TRUE(std::filesystem::exists(file));

    const std::string before = slurp(file);
    EXPECT_FALSE(MergeSonyVersionLinks(file, "garbage").written);
    EXPECT_EQ(slurp(file), before);
}

TEST(SonyLinksFileTest, ReportsWhichTitlesHaveALinkAcrossSeveralFiles) {
    TempFolder folder;
    const auto first = folder.path / "a.tsv";
    const auto second = folder.path / "b.tsv";
    { std::ofstream(first) << "PPSA00001\thttps://example.test/1-version.xml\n"; }
    { std::ofstream(second) << "ppsa00002 https://example.test/2-version.xml\nPPSA00003\tnot a link yet\n"; }

    const auto ids = ReadSonyLinkTitleIds({first, second, folder.path / "missing.tsv"});

    EXPECT_EQ(ids, (std::set<std::string>{"PPSA00001", "PPSA00002"}));
}

TEST(SonyLinksFileTest, TheNeededListListsEachGameAndCanBeFilledInAndReadBack) {
    const std::string list = BuildSonyLinksNeededList({{"PPSA20050", "EA SPORTS FC 25"}, {"PPSA02343", "It Takes Two"}});

    EXPECT_NE(list.find("PPSA20050\tEA SPORTS FC 25\t\n"), std::string::npos) << list;
    EXPECT_NE(list.find("PPSA02343\tIt Takes Two\t\n"), std::string::npos) << list;
    EXPECT_TRUE(ParseSonyVersionLinks(list).empty()) << "an unfilled list gives no links (and no spurious ones from its help text)";

    // The user pastes a link at the end of one line.
    std::string filled = list;
    const std::string line = "PPSA20050\tEA SPORTS FC 25\t";
    filled.replace(filled.find(line), line.size(),
                   line + "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA20050_00/abc-version.xml");
    const auto links = ParseSonyVersionLinks(filled);
    ASSERT_EQ(links.size(), 1u);
    EXPECT_NE(links.at("PPSA20050").find("abc-version.xml"), std::string::npos);
}

TEST(SonyUpdateServiceTest, Ps4UpdateListsItsFilesFromTheManifest) {
    FakeSony sony;
    sony.answer(SonyPs4UpdateUrl("CUSA07410"), 200, kPs4VerXml);
    sony.answer("http://gs2.ww.prod.dl.playstation.net/gs2/ppkgo/prod/CUSA07410_00/30/f_1d7b/f/"  // NOSONAR: fake response key; fetcher is injected and no request is sent.
                "EP9000-CUSA07410_00-00000000GODOFWAR-A0136-V0100.json",
                200,
                "{\"originalFileSize\":8023703552,\"numberOfSplitFiles\":2,\"pieces\":["
                "{\"url\":\"http://gs2.ww.prod.dl.playstation.net/x/EP9000-CUSA07410_00-00000000GODOFWAR-A0136-V0100_0.pkg\","
                "\"fileOffset\":0,\"fileSize\":4294967296,\"hashValue\":\"AABB\"},"
                "{\"url\":\"http://gs2.ww.prod.dl.playstation.net/x/EP9000-CUSA07410_00-00000000GODOFWAR-A0136-V0100_1.pkg\","
                "\"fileOffset\":4294967296,\"fileSize\":3728736256,\"hashValue\":\"CCDD\"}]}");
    SonyUpdateService service({}, sony.fetcher());

    const auto result = service.latestRelease("PS4", "CUSA07410");

    ASSERT_EQ(result.status, LatestReleaseStatus::Found) << result.detail;
    ASSERT_EQ(result.release.pieces.size(), 2u);
    EXPECT_EQ(result.release.pieces[0].sizeBytes, 4294967296LL);
    EXPECT_EQ(result.release.pieces[1].sha256, "ccdd");
    EXPECT_TRUE(result.release.pieceSizes.empty()) << "piece sizes recognise a PS5 dump, and an update is not one";
}

TEST(SonyUpdateServiceTest, Ps4UpdateAddressedDirectlyIsOneFileWithTheWholePackagesDigest) {
    const std::string digest(64, 'E');
    FakeSony sony;
    sony.answer(SonyPs4UpdateUrl("CUSA00001"), 200,
                "<titlepatch><tag><package version=\"01.05\" size=\"1234\" digest=\"" + digest +
                    "\" url=\"http://gs2.ww.prod.dl.playstation.net/a/EP0-CUSA00001_00-X-A0105-V0100.pkg\"/></tag></titlepatch>");
    SonyUpdateService service({}, sony.fetcher());

    const auto result = service.latestRelease("PS4", "CUSA00001");

    ASSERT_EQ(result.status, LatestReleaseStatus::Found) << result.detail;
    ASSERT_EQ(result.release.pieces.size(), 1u);
    EXPECT_EQ(result.release.pieces[0].sizeBytes, 1234);
    EXPECT_EQ(result.release.pieces[0].sha256, std::string(64, 'e'));
}


// ---- Asking again when the answer isn't final -----------------------------------------------------------------------

namespace {

struct ScriptedFetcher {
    std::vector<int> statuses;   // one answer per call, in order (the last repeats)
    int calls = 0;
    SonyFetcher fetcher() {
        return [this](const std::string&) {
            SonyHttpResponse response;
            response.status = statuses[std::min<size_t>(static_cast<size_t>(calls), statuses.size() - 1)];
            if (response.status == 0) response.failure = "the connection was dropped";
            response.body = response.status == 200 ? "ok" : "";
            ++calls;
            return response;
        };
    }
};

}  // namespace

TEST(SonyRetryTest, OnlyNoAnswerBusyAndServerErrorsAreWorthAskingAgainAbout) {
    const auto with = [](int status) { SonyHttpResponse r; r.status = status; return ShouldRetrySonyResponse(r); };
    EXPECT_TRUE(with(0));
    EXPECT_TRUE(with(429));
    EXPECT_TRUE(with(500));
    EXPECT_TRUE(with(503));
    EXPECT_FALSE(with(200));
    EXPECT_FALSE(with(206));
    EXPECT_FALSE(with(403)) << "Sony's own answer - asking again changes nothing";
    EXPECT_FALSE(with(404));
}

TEST(SonyRetryTest, ADroppedConnectionIsRetriedWithAGrowingPauseUntilItWorks) {
    ScriptedFetcher server{{0, 503, 200}};
    std::vector<std::chrono::milliseconds> pauses;
    const auto fetch = WithRetries(server.fetcher(), SonyRetryPolicy{3, std::chrono::milliseconds(700)},
                                   [&pauses](std::chrono::milliseconds pause) { pauses.push_back(pause); });

    const auto response = fetch("https://sgst.prod.dl.playstation.net/x");

    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(server.calls, 3);
    EXPECT_EQ(pauses, (std::vector<std::chrono::milliseconds>{std::chrono::milliseconds(700), std::chrono::milliseconds(1400)}));
}

TEST(SonyRetryTest, GivesUpAfterTheLastAttemptAndReturnsWhatSonySaid) {
    ScriptedFetcher server{{0}};
    int pauses = 0;
    const auto fetch = WithRetries(server.fetcher(), SonyRetryPolicy{3, std::chrono::milliseconds(1)},
                                   [&pauses](std::chrono::milliseconds) { ++pauses; });

    const auto response = fetch("https://sgst.prod.dl.playstation.net/x");

    EXPECT_EQ(response.status, 0);
    EXPECT_EQ(response.failure, "the connection was dropped");
    EXPECT_EQ(server.calls, 3);
    EXPECT_EQ(pauses, 2) << "no pause after the last attempt";
}

TEST(SonyRetryTest, AnAnswerThatIsSonysFinalWordIsNotAskedAgain) {
    ScriptedFetcher server{{404}};
    const auto fetch = WithRetries(server.fetcher(), {}, [](std::chrono::milliseconds) {});

    EXPECT_EQ(fetch("https://sgst.prod.dl.playstation.net/x").status, 404);
    EXPECT_EQ(server.calls, 1);
}
