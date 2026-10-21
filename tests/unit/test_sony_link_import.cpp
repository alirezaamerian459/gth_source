#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "gth/application/indexing/LinkCollection.h"
#include "gth/network/SonyLinkImport.h"

using namespace gth::network;

namespace {

const std::string kTitle = "PPSA02343";
const std::string kVersionUrl = "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/b437c912-d234-49fa-8321-ec699a103e05-version.xml";
const std::string kInfoFolder = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA02343_00/app/info/9/f_d5af803572a6/";
const std::string kScUrl = kInfoFolder + "EP0006-PPSA02343_00-NUTSEU0000000000_sc.pkg";
const std::string kManifestUrl = kInfoFolder + "EP0006-PPSA02343_00-NUTSEU0000000000.json";
const std::string kCrcUrl = kInfoFolder + "EP0006-PPSA02343_00-NUTSEU0000000000.crc";
const std::string kPieceUrl =
    "http://gst.prod.dl.playstation.net/gst/prod/00/PPSA02343_00/app/pkg/7/f_bdc44b4d0b53/EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg";

std::string ParamJson(const std::string& titleId, const std::string& versionUri) {
    // Sony pads the value with spaces inside the quotes.
    return "{\r\n  \"titleId\": \"" + titleId + "\",\r\n  \"userDefinedParam1\": 0,\r\n  \"versionFileUri\": \"" + versionUri +
           "                 \"\r\n}\r\n";
}

// A metadata piece like the real "_sc.pkg": a \x7fCNT header, an entry table, the names entry and two JSON entries
// (origin-param.json first, so picking the wrong one would show).
std::string BuildMetadataPiece(const std::string& paramJson, const std::string& originJson = "{\"versionFileUri\": \"WRONG\"}") {
    std::string buf(0x10000, '\0');
    auto put32 = [&buf](size_t at, uint32_t v) {
        buf[at] = static_cast<char>(v >> 24);
        buf[at + 1] = static_cast<char>(v >> 16);
        buf[at + 2] = static_cast<char>(v >> 8);
        buf[at + 3] = static_cast<char>(v);
    };
    buf[0] = 0x7f;
    buf[1] = 'C';
    buf[2] = 'N';
    buf[3] = 'T';

    std::string names;
    names.push_back('\0');
    const uint32_t originName = static_cast<uint32_t>(names.size());
    names += "origin-param.json";
    names.push_back('\0');
    const uint32_t paramName = static_cast<uint32_t>(names.size());
    names += "param.json";
    names.push_back('\0');

    constexpr uint32_t table = 0x600, namesAt = 0x1000, originAt = 0x2000, paramAt = 0x3000;
    put32(0x10, 3);
    put32(0x18, table);

    auto entry = [&](int index, uint32_t id, uint32_t nameOffset, uint32_t offset, uint32_t size) {
        const size_t at = table + static_cast<size_t>(index) * 0x20;
        put32(at, id);
        put32(at + 4, nameOffset);
        put32(at + 16, offset);
        put32(at + 20, size);
    };
    entry(0, 0x200, 0, namesAt, static_cast<uint32_t>(names.size()));
    entry(1, 0x2100, originName, originAt, static_cast<uint32_t>(originJson.size()));
    entry(2, 0x2000, paramName, paramAt, static_cast<uint32_t>(paramJson.size()));

    buf.replace(namesAt, names.size(), names);
    buf.replace(originAt, originJson.size(), originJson);
    buf.replace(paramAt, paramJson.size(), paramJson);
    return buf;
}

SonyHttpResponse Answer(int status, std::string body = {}) {
    SonyHttpResponse response;
    response.status = status;
    response.body = std::move(body);
    return response;
}

SonyHttpResponse NoAnswer() {
    SonyHttpResponse response;
    response.failure = "the connection was dropped";
    return response;
}

struct FakeSony {
    std::map<std::string, SonyHttpResponse> answers;
    std::vector<std::string> asked;
    std::vector<size_t> limits;

    SonyHeadFetcher fetcher() {
        return [this](const std::string& url, size_t maxBytes) {
            asked.push_back(url);
            limits.push_back(maxBytes);
            const auto found = answers.find(url);
            return found == answers.end() ? Answer(404) : found->second;
        };
    }
};

class TempDir {
public:
    explicit TempDir(const std::string& name) : path_(std::filesystem::temp_directory_path() / ("gth_link_import_" + name)) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string ReadAll(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteAll(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

}  // namespace

// ---- Finding addresses in pasted text -------------------------------------------------------------------------------

TEST(SonyAddressExtractionTest, RecognisesEveryKindAndMapsNeighboursToTheMetadataPiece) {
    const std::string text = "version: " + kVersionUrl + "\n" + "sc: " + kScUrl + "\n" + "piece: " + kPieceUrl + "\n";
    const auto found = ExtractSonyAddresses(text);
    ASSERT_EQ(found.size(), 3u);
    EXPECT_EQ(found[0].kind, SonyAddressKind::VersionFile);
    EXPECT_EQ(found[0].titleId, kTitle);
    EXPECT_EQ(found[0].url, kVersionUrl);
    EXPECT_EQ(found[1].kind, SonyAddressKind::MetadataPiece);
    EXPECT_EQ(found[1].url, kScUrl);
    EXPECT_EQ(found[2].kind, SonyAddressKind::GamePiece);
    EXPECT_EQ(found[2].titleId, kTitle);

    // The manifest and the .crc in the same folder both lead to the same metadata piece - and are counted once.
    const auto viaNeighbours = ExtractSonyAddresses(kManifestUrl + "\n" + kCrcUrl + "\n" + kScUrl);
    ASSERT_EQ(viaNeighbours.size(), 1u);
    EXPECT_EQ(viaNeighbours[0].kind, SonyAddressKind::MetadataPiece);
    EXPECT_EQ(viaNeighbours[0].url, kScUrl);
}

TEST(SonyAddressExtractionTest, ReadsAddressesOutOfMarkupAndTabSeparatedLists) {
    const std::string text = "<a href=\"" + kScUrl + "\">SC</a> and (" + kVersionUrl + "), then PPSA02343\t" + kCrcUrl + ".";
    const auto found = ExtractSonyAddresses(text);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].url, kScUrl);
    EXPECT_EQ(found[1].kind, SonyAddressKind::VersionFile);
}

TEST(SonyAddressExtractionTest, UpgradesPlainHttpVersionFileLinksToHttps) {
    const auto found = ExtractSonyAddresses("http://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/x-version.xml");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].url.rfind("https://", 0), 0u);
}

TEST(SonyAddressExtractionTest, IgnoresEveryHostThatIsNotSonysPackageHost) {
    EXPECT_TRUE(ExtractSonyAddresses("https://evil.example.com/sgst/prod/00/np/PPSA02343_00/x-version.xml").empty());
    EXPECT_TRUE(ExtractSonyAddresses("https://sgst.prod.dl.playstation.net.evil.com/sgst/prod/00/np/PPSA02343_00/x-version.xml").empty());
    EXPECT_TRUE(ExtractSonyAddresses("https://evil.example.com/?u=https://sgst.prod.dl.playstation.net@evil.com/PPSA02343_00/app/info/9/x_sc.pkg").empty());
    EXPECT_TRUE(ExtractSonyAddresses("nothing to see here, http and https://").empty());
    // Sony host, but nothing that names a title or a kind of file we know.
    EXPECT_TRUE(ExtractSonyAddresses("https://sgst.prod.dl.playstation.net/sgst/prod/00/np/index.html").empty());
    EXPECT_TRUE(ExtractSonyAddresses("https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA02343_00/app/info/9/f_x/readme.txt").empty());
}

// ---- Reading param.json out of a metadata piece ---------------------------------------------------------------------

TEST(SonyMetadataPieceTest, FindsTheRealParamJsonNotTheOriginCopy) {
    const std::string json = ParamJson("PPSA02343", kVersionUrl);
    const auto found = ReadParamJsonFromMetadataPiece(BuildMetadataPiece(json));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, json);
}

TEST(SonyMetadataPieceTest, RejectsWhatIsNotAMetadataPiece) {
    EXPECT_FALSE(ReadParamJsonFromMetadataPiece("").has_value());
    EXPECT_FALSE(ReadParamJsonFromMetadataPiece(std::string(0x10000, 'x')).has_value());

    std::string wrongMagic = BuildMetadataPiece(ParamJson(kTitle, kVersionUrl));
    wrongMagic[1] = 'X';
    EXPECT_FALSE(ReadParamJsonFromMetadataPiece(wrongMagic).has_value());

    // Cut off before the entries: nothing can be found, and nothing is read out of bounds.
    const std::string whole = BuildMetadataPiece(ParamJson(kTitle, kVersionUrl));
    EXPECT_FALSE(ReadParamJsonFromMetadataPiece(whole.substr(0, 0x800)).has_value());
    EXPECT_FALSE(ReadParamJsonFromMetadataPiece(whole.substr(0, 0x3000)).has_value());  // param.json itself cut off
}

TEST(SonyMetadataPieceTest, ParsesAbsurdEntryCountsWithoutOverreading) {
    std::string piece = BuildMetadataPiece(ParamJson(kTitle, kVersionUrl));
    piece[0x10] = static_cast<char>(0x7f);  // 2 billion entries
    EXPECT_FALSE(ReadParamJsonFromMetadataPiece(piece).has_value());
}

TEST(SonyParamJsonTest, ReadsTheVersionFileUriTrimmedOfSonysPadding) {
    const auto uri = VersionFileUriFromParamJson(ParamJson(kTitle, kVersionUrl));
    ASSERT_TRUE(uri.has_value());
    EXPECT_EQ(*uri, kVersionUrl);

    EXPECT_EQ(VersionFileUriFromParamJson("{\"versionFileUri\":\"https:\\/\\/sgst.prod.dl.playstation.net\\/a-version.xml\"}").value_or(""),
              "https://sgst.prod.dl.playstation.net/a-version.xml");
    EXPECT_FALSE(VersionFileUriFromParamJson("{\"titleId\": \"PPSA02343\"}").has_value());
    EXPECT_FALSE(VersionFileUriFromParamJson("{\"versionFileUri\": \"   \"}").has_value());
    EXPECT_FALSE(VersionFileUriFromParamJson("{\"versionFileUri\": \"unterminated").has_value());
    EXPECT_EQ(TitleIdFromParamJson(ParamJson("PPSA20050", kVersionUrl)).value_or(""), "PPSA20050");
}

TEST(SonyParamJsonTest, OnlyGenuineVersionFileLinksForTheTitleAreAccepted) {
    EXPECT_TRUE(IsSonyVersionFileUrl(kVersionUrl, kTitle));
    EXPECT_TRUE(IsSonyVersionFileUrl(kVersionUrl, "ppsa02343"));
    EXPECT_FALSE(IsSonyVersionFileUrl(kVersionUrl, "PPSA99999"));                     // another title's link
    EXPECT_FALSE(IsSonyVersionFileUrl(kVersionUrl, ""));
    EXPECT_FALSE(IsSonyVersionFileUrl("http://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/x-version.xml", kTitle));  // not https
    EXPECT_FALSE(IsSonyVersionFileUrl("https://evil.example.com/sgst/prod/00/np/PPSA02343_00/x-version.xml", kTitle));
    EXPECT_FALSE(IsSonyVersionFileUrl("https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/x.exe", kTitle));
    EXPECT_FALSE(IsSonyVersionFileUrl("https://sgst.prod.dl.playstation.net/../PPSA02343_00/x-version.xml", kTitle));
    EXPECT_FALSE(IsSonyVersionFileUrl("https://sgst.prod.dl.playstation.net/PPSA02343_00/a b-version.xml", kTitle));
}

// ---- Turning addresses into saved links -----------------------------------------------------------------------------

TEST(SonyLinkImporterTest, LearnsTheLinkFromAMetadataPieceAndSavesIt) {
    TempDir dir("learn");
    const auto links = dir.path() / "links.tsv";
    FakeSony sony;
    sony.answers[kScUrl] = Answer(206, BuildMetadataPiece(ParamJson(kTitle, kVersionUrl)));

    SonyLinkImporter importer(links, {links}, sony.fetcher());
    std::vector<std::string> progress;
    const auto result = importer.import(kManifestUrl, {}, [&progress](const std::string& line) { progress.push_back(line); });

    EXPECT_EQ(result.learned, 1u);
    EXPECT_EQ(result.titleIdsLearned, std::vector<std::string>{kTitle});
    EXPECT_EQ(result.failed, 0u);
    ASSERT_EQ(sony.asked.size(), 1u);
    EXPECT_EQ(sony.asked[0], kScUrl);   // the manifest address was turned into its metadata piece
    EXPECT_EQ(sony.limits[0], 65536u);  // and only the head of it was asked for
    EXPECT_FALSE(progress.empty());

    const auto saved = ParseSonyVersionLinks(ReadAll(links));
    ASSERT_TRUE(saved.contains(kTitle));
    EXPECT_EQ(saved.at(kTitle), kVersionUrl);
}

TEST(SonyLinkImporterTest, AVersionFileLinkIsSavedWithoutAskingSony) {
    TempDir dir("direct");
    const auto links = dir.path() / "links.tsv";
    FakeSony sony;

    SonyLinkImporter importer(links, {links}, sony.fetcher());
    const auto result = importer.import(kVersionUrl);

    EXPECT_EQ(result.learned, 1u);
    EXPECT_TRUE(sony.asked.empty());
    EXPECT_EQ(ParseSonyVersionLinks(ReadAll(links)).at(kTitle), kVersionUrl);
}

TEST(SonyLinkImporterTest, KnownTitlesAreNotLookedUpAgainAndOthersAreKept) {
    TempDir dir("known");
    const auto links = dir.path() / "links.tsv";
    const auto other = dir.path() / "beside-exe.tsv";
    const std::string otherLink = "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA20050_00/11111111-1111-1111-1111-111111111111-version.xml";
    WriteAll(other, "PPSA20050\t" + otherLink + "\n");
    WriteAll(links, "PPSA00001\thttps://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA00001_00/22222222-2222-2222-2222-222222222222-version.xml\n");

    FakeSony sony;
    sony.answers[kScUrl] = Answer(200, BuildMetadataPiece(ParamJson(kTitle, kVersionUrl)));
    const std::string knownSc = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA20050_00/app/info/3/f_aa/EP0006-PPSA20050_00-EASPORTSFC2025BG_sc.pkg";

    SonyLinkImporter importer(links, {other, links}, sony.fetcher());
    const auto result = importer.import(knownSc + "\n" + kScUrl);

    EXPECT_EQ(result.alreadyKnown, 1u);  // PPSA20050 - already in another links file
    EXPECT_EQ(result.learned, 1u);
    ASSERT_EQ(sony.asked.size(), 1u);
    EXPECT_EQ(sony.asked[0], kScUrl);

    const auto saved = ParseSonyVersionLinks(ReadAll(links));
    EXPECT_TRUE(saved.contains("PPSA00001"));  // what was already there survives
    EXPECT_TRUE(saved.contains(kTitle));
}

TEST(SonyLinkImporterTest, ExplainsEachTitleThatDidNotWork) {
    TempDir dir("failures");
    const auto links = dir.path() / "links.tsv";
    FakeSony sony;

    const std::string expired = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA00001_00/app/info/1/f_a/EP0001-PPSA00001_00-AAAAAAAAAAAAAAAA_sc.pkg";
    const std::string notPiece = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA00002_00/app/info/1/f_b/EP0001-PPSA00002_00-AAAAAAAAAAAAAAAA_sc.pkg";
    const std::string otherGame = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA00003_00/app/info/1/f_c/EP0001-PPSA00003_00-AAAAAAAAAAAAAAAA_sc.pkg";
    const std::string foreignLink = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA00004_00/app/info/1/f_d/EP0001-PPSA00004_00-AAAAAAAAAAAAAAAA_sc.pkg";
    const std::string onlyPieces = "http://gst.prod.dl.playstation.net/gst/prod/00/PPSA00005_00/app/pkg/1/f_e/EP0001-PPSA00005_00-AAAAAAAAAAAAAAAA_0.pkg";
    sony.answers[expired] = Answer(403);
    sony.answers[notPiece] = Answer(200, std::string(70000, 'z'));
    sony.answers[otherGame] = Answer(206, BuildMetadataPiece(ParamJson("PPSA99999", kVersionUrl)));
    sony.answers[foreignLink] = Answer(206, BuildMetadataPiece(ParamJson("PPSA00004", "https://evil.example.com/PPSA00004_00/x-version.xml")));

    SonyLinkImporter importer(links, {links}, sony.fetcher());
    const auto result = importer.import(expired + "\n" + notPiece + "\n" + otherGame + "\n" + foreignLink + "\n" + onlyPieces);

    EXPECT_EQ(result.learned, 0u);
    EXPECT_EQ(result.failed, 4u);
    EXPECT_EQ(result.gamePiecesOnly, 1u);
    ASSERT_EQ(result.notes.size(), 5u);
    EXPECT_NE(result.notes[0].find("HTTP 403"), std::string::npos);
    EXPECT_NE(result.notes[1].find("isn't a metadata piece"), std::string::npos);
    EXPECT_NE(result.notes[2].find("PPSA99999"), std::string::npos);
    EXPECT_NE(result.notes[3].find("isn't a Sony version file link"), std::string::npos);
    EXPECT_NE(result.notes[4].find("SC link"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(links));  // nothing worked, so nothing is written
}

TEST(SonyLinkImporterTest, TriesAnotherPatchsMetadataPieceWhenTheFirstIsGone) {
    TempDir dir("fallback");
    const auto links = dir.path() / "links.tsv";
    const std::string olderSc = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA02343_00/app/info/3/f_old/EP0006-PPSA02343_00-NUTSEU0000000000_sc.pkg";
    FakeSony sony;
    sony.answers[olderSc] = Answer(404);
    sony.answers[kScUrl] = Answer(206, BuildMetadataPiece(ParamJson(kTitle, kVersionUrl)));

    SonyLinkImporter importer(links, {links}, sony.fetcher());
    const auto result = importer.import(olderSc + "\n" + kScUrl);

    EXPECT_EQ(result.learned, 1u);
    EXPECT_EQ(sony.asked.size(), 2u);
}

TEST(SonyLinkImporterTest, StopsWhenSonyStopsAnswering) {
    TempDir dir("offline");
    const auto links = dir.path() / "links.tsv";
    FakeSony sony;
    std::string text;
    for (int i = 1; i <= 8; ++i) {
        const std::string id = "PPSA0000" + std::to_string(i);
        const std::string url = "https://sgst.prod.dl.playstation.net/sgst/prod/00/" + id + "_00/app/info/1/f_a/EP0001-" + id + "_00-AAAAAAAAAAAAAAAA_sc.pkg";
        sony.answers[url] = NoAnswer();
        text += url + "\n";
    }

    SonyLinkImporter importer(links, {links}, sony.fetcher());
    const auto result = importer.import(text);

    EXPECT_EQ(result.learned, 0u);
    EXPECT_EQ(sony.asked.size(), 3u);  // gave up after three silent answers, not eight
    EXPECT_NE(result.notes.back().find("Sony isn't answering"), std::string::npos);
}

TEST(SonyLinkImporterTest, CancelStopsBetweenTitlesButKeepsWhatWasLearned) {
    TempDir dir("cancel");
    const auto links = dir.path() / "links.tsv";
    const std::string secondSc = "https://sgst.prod.dl.playstation.net/sgst/prod/00/PPSA20050_00/app/info/3/f_aa/EP0006-PPSA20050_00-EASPORTSFC2025BG_sc.pkg";
    const std::string secondLink = "https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA20050_00/11111111-1111-1111-1111-111111111111-version.xml";
    FakeSony sony;
    sony.answers[kScUrl] = Answer(206, BuildMetadataPiece(ParamJson(kTitle, kVersionUrl)));
    sony.answers[secondSc] = Answer(206, BuildMetadataPiece(ParamJson("PPSA20050", secondLink)));

    int asks = 0;
    SonyLinkImporter importer(links, {links}, sony.fetcher());
    const auto result = importer.import(kScUrl + "\n" + secondSc, [&asks] { return asks++ >= 1; });

    EXPECT_TRUE(result.cancelled);
    EXPECT_EQ(result.learned, 1u);
    const auto saved = ParseSonyVersionLinks(ReadAll(links));
    EXPECT_TRUE(saved.contains(kTitle));
    EXPECT_FALSE(saved.contains("PPSA20050"));
}

// ---- Collecting links game by game ----------------------------------------------------------------------------------

TEST(LinkCollectionTest, PicksTheFirstGameWithoutALinkThatWasNotSkipped) {
    const std::vector<std::pair<std::string, std::string>> games = {
        {"PPSA00001", "First"}, {"PPSA00002", "Second"}, {"PPSA00003", "Third"}, {"PPSA00004", "Fourth"}};

    auto next = gth::application::NextGameNeedingLink(games, {}, {});
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->first, "PPSA00001");
    EXPECT_EQ(next->second, "First");

    next = gth::application::NextGameNeedingLink(games, {"PPSA00001"}, {});  // has a link now
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->first, "PPSA00002");

    next = gth::application::NextGameNeedingLink(games, {"PPSA00001"}, {"PPSA00002"});  // skipped
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->first, "PPSA00003");

    // A link for a game later in the list doesn't disturb the order: the first still-needed game stays first.
    next = gth::application::NextGameNeedingLink(games, {"PPSA00003"}, {});
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->first, "PPSA00001");
}

TEST(LinkCollectionTest, NothingIsLeftWhenEveryGameHasALinkOrWasSkipped) {
    const std::vector<std::pair<std::string, std::string>> games = {{"PPSA00001", "First"}, {"PPSA00002", "Second"}};
    EXPECT_FALSE(gth::application::NextGameNeedingLink(games, {"PPSA00001"}, {"PPSA00002"}).has_value());
    EXPECT_FALSE(gth::application::NextGameNeedingLink({}, {}, {}).has_value());
}

// ---- Links learned from metadata pieces already in the archive ------------------------------------------------------

TEST(LocalLinkLearningTest, ReadsTheLinkOutOfAMetadataPieceOnDisk) {
    TempDir dir("local_learn");
    const auto piece = dir.path() / "EP0006-PPSA02343_00-NUTSEU0000000000_sc.pkg";
    WriteAll(piece, BuildMetadataPiece(ParamJson(kTitle, kVersionUrl)));
    const auto links = dir.path() / "links.tsv";

    const auto result = LearnLinksFromLocalMetadata({{kTitle, piece.string()}}, links, {links});

    EXPECT_EQ(result.learned, 1u);
    EXPECT_EQ(result.titleIdsLearned, std::vector<std::string>{kTitle});
    EXPECT_FALSE(result.saveFailed);
    EXPECT_EQ(ParseSonyVersionLinks(ReadAll(links)).at(kTitle), kVersionUrl);
}

TEST(LocalLinkLearningTest, TitlesThatAlreadyHaveALinkAreNotReadAgain) {
    TempDir dir("local_known");
    const auto links = dir.path() / "links.tsv";
    WriteAll(links, kTitle + "\t" + kVersionUrl + "\n");

    // The piece doesn't even exist: nothing is opened for a title that is already covered.
    const auto result = LearnLinksFromLocalMetadata({{kTitle, (dir.path() / "gone_sc.pkg").string()}}, links, {links});

    EXPECT_EQ(result.alreadyKnown, 1u);
    EXPECT_EQ(result.looked, 0u);
    EXPECT_EQ(result.learned, 0u);
    EXPECT_EQ(result.unreadable, 0u);
}

TEST(LocalLinkLearningTest, OnlyAGenuineLinkForTheSameTitleIsAccepted) {
    TempDir dir("local_refuse");
    const auto links = dir.path() / "links.tsv";
    const auto otherTitle = dir.path() / "other_sc.pkg";
    const auto foreign = dir.path() / "foreign_sc.pkg";
    const auto notAPiece = dir.path() / "notapiece_sc.pkg";
    WriteAll(otherTitle, BuildMetadataPiece(ParamJson("PPSA99999", kVersionUrl)));
    WriteAll(foreign, BuildMetadataPiece(ParamJson("PPSA00004", "https://evil.example.com/PPSA00004_00/x-version.xml")));
    WriteAll(notAPiece, std::string(4096, 'x'));

    const auto result = LearnLinksFromLocalMetadata({{kTitle, otherTitle.string()},
                                                     {"PPSA00004", foreign.string()},
                                                     {"PPSA00005", notAPiece.string()},
                                                     {"PPSA00006", (dir.path() / "missing_sc.pkg").string()}},
                                                    links, {links});

    EXPECT_EQ(result.learned, 0u);
    EXPECT_EQ(result.unreadable, 4u);
    EXPECT_EQ(result.notes.size(), 4u);
    EXPECT_FALSE(std::filesystem::exists(links)) << "nothing learned, nothing written";
}
