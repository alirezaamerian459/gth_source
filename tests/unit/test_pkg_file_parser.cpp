#include "TestTempPaths.h"
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "gth/indexing/PkgFileParser.h"

using gth::indexing::PkgFileParser;
using gth::indexing::PkgMetadata;

namespace {

class ByteBuilder {
public:
    void u32be(uint32_t v) {
        buf_.push_back(static_cast<char>((v >> 24) & 0xFF));
        buf_.push_back(static_cast<char>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<char>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<char>(v & 0xFF));
    }
    void u64be(uint64_t v) {
        for (int shift = 56; shift >= 0; shift -= 8) buf_.push_back(static_cast<char>((v >> shift) & 0xFF));
    }
    void u16be(uint16_t v) {
        buf_.push_back(static_cast<char>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<char>(v & 0xFF));
    }
    void u32le(uint32_t v) {
        buf_.push_back(static_cast<char>(v & 0xFF));
        buf_.push_back(static_cast<char>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<char>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<char>((v >> 24) & 0xFF));
    }
    void u64le(uint64_t v) {
        for (int shift = 0; shift <= 56; shift += 8) buf_.push_back(static_cast<char>((v >> shift) & 0xFF));
    }
    void u16le(uint16_t v) {
        buf_.push_back(static_cast<char>(v & 0xFF));
        buf_.push_back(static_cast<char>((v >> 8) & 0xFF));
    }
    void raw(const std::string& s) { buf_ += s; }
    void cstr(const std::string& s) {
        buf_ += s;
        buf_.push_back('\0');
    }
    void zeros(size_t n) { buf_.append(n, '\0'); }
    void patch32be(size_t offset, uint32_t v) {
        buf_[offset] = static_cast<char>((v >> 24) & 0xFF);
        buf_[offset + 1] = static_cast<char>((v >> 16) & 0xFF);
        buf_[offset + 2] = static_cast<char>((v >> 8) & 0xFF);
        buf_[offset + 3] = static_cast<char>(v & 0xFF);
    }
    size_t size() const { return buf_.size(); }
    const std::string& str() const { return buf_; }

private:
    std::string buf_;
};

// Builds a minimal, spec-shaped PARAM.SFO blob (magic + 4-field header +
// TEXT-only index/key/value tables) - see PkgFileParser.cpp's parseSfo for
// the format this must satisfy.
std::string buildSfo(const std::vector<std::pair<std::string, std::string>>& entries) {
    // Each text value is stored with its trailing null included in `length`,
    // matching real PARAM.SFO files.
    std::string keys;
    std::vector<uint32_t> keyRelative;
    for (const auto& [key, value] : entries) {
        keyRelative.push_back(static_cast<uint32_t>(keys.size()));
        keys += key;
        keys.push_back('\0');
    }
    std::string values;
    std::vector<uint32_t> valueRelative;
    std::vector<uint32_t> valueLength;
    for (const auto& [key, value] : entries) {
        valueRelative.push_back(static_cast<uint32_t>(values.size()));
        values += value;
        values.push_back('\0');
        valueLength.push_back(static_cast<uint32_t>(value.size() + 1));
    }

    const uint32_t headerSize = 0x14;
    const uint32_t indexSize = static_cast<uint32_t>(entries.size()) * 0x10;
    const uint32_t keyTableOffset = headerSize + indexSize;
    const uint32_t dataTableOffset = keyTableOffset + static_cast<uint32_t>(keys.size());

    ByteBuilder b;
    b.raw(std::string("\x00PSF", 4));
    b.u32le(0x00000101);  // version, unused by the parser
    b.u32le(keyTableOffset);
    b.u32le(dataTableOffset);
    b.u32le(static_cast<uint32_t>(entries.size()));

    for (size_t i = 0; i < entries.size(); ++i) {
        b.u16le(static_cast<uint16_t>(keyRelative[i]));
        b.u16be(0x0204);  // PSF_FORMAT_TEXT
        b.u32le(valueLength[i]);
        b.u32le(valueLength[i]);  // max_length == length: never trips the "exceeds max" check
        b.u32le(valueRelative[i]);
    }
    b.raw(keys);
    b.raw(values);
    return b.str();
}

// Builds a minimal, spec-shaped PS4 \x7fCNT package: fixed 0xA0 header, one
// entry table row for PARAM_SFO_ID (0x1000), and the SFO blob itself. See
// PkgFileParser.cpp's parsePs4 for the offsets this must satisfy.
std::string buildPs4Pkg(const std::string& contentId36, const std::string& sfo) {
    ByteBuilder b;
    b.u32be(0x7F434E54);  // magic
    b.u32be(0);           // pkg_type
    b.u32be(0);           // pkg_0x008
    b.u32be(0);           // pkg_file_count
    b.u32be(1);           // pkg_entry_count (offset 0x10)
    b.u16be(0);           // pkg_sc_entry_count
    b.u16be(0);           // pkg_entry_count_2
    b.u32be(0xA0);        // pkg_table_offset (offset 0x18)
    b.u32be(0);           // pkg_entry_data_size
    b.u64be(0);
    b.u64be(0);
    b.u64be(0);
    b.u64be(0);  // 4Q, offsets 0x20-0x3F
    std::string contentId = contentId36;
    contentId.resize(36, '\0');
    b.raw(contentId);  // offset 0x40, 36 bytes
    b.zeros(12);        // padding, offset 0x64
    for (int i = 0; i < 12; ++i) b.u32be(0);  // 12I, offset 0x70-0x9F
    EXPECT_EQ(b.size(), 0xA0u);

    const uint32_t sfoOffset = 0xA0 + 0x20;  // right after the one entry-table row
    b.u32be(0x1000);           // file_id = PARAM_SFO_ID
    b.u32be(0);                // name_offset
    b.u32be(0);                // flags1 (not encrypted)
    b.u32be(0);                // flags2
    b.u32be(sfoOffset);        // data_offset
    b.u32be(static_cast<uint32_t>(sfo.size()));
    b.u64be(0);  // padding

    b.raw(sfo);
    return b.str();
}

// Same fixed 0xA0 PS4 header as buildPs4Pkg, but with one entry table row under `fileId` holding
// `data` verbatim - for exercising extractPoster's own entry-table walk (a different fixed ID,
// 0x1200, than parsePs4's PARAM_SFO_ID, but the identical table layout).
std::string buildPs4PkgWithEntry(const std::string& contentId36, uint32_t fileId, const std::string& data) {
    ByteBuilder b;
    b.u32be(0x7F434E54);
    b.u32be(0);
    b.u32be(0);
    b.u32be(0);
    b.u32be(1);  // pkg_entry_count
    b.u16be(0);
    b.u16be(0);
    b.u32be(0xA0);  // pkg_table_offset
    b.u32be(0);
    b.u64be(0);
    b.u64be(0);
    b.u64be(0);
    b.u64be(0);
    std::string contentId = contentId36;
    contentId.resize(36, '\0');
    b.raw(contentId);
    b.zeros(12);
    for (int i = 0; i < 12; ++i) b.u32be(0);
    EXPECT_EQ(b.size(), 0xA0u);

    const uint32_t dataOffset = 0xA0 + 0x20;  // right after the one entry-table row
    b.u32be(fileId);
    b.u32be(0);  // name_offset
    b.u32be(0);  // flags1 (not encrypted)
    b.u32be(0);  // flags2
    b.u32be(dataOffset);
    b.u32be(static_cast<uint32_t>(data.size()));
    b.u64be(0);  // padding

    b.raw(data);
    return b.str();
}

std::string writeTempFile(const std::string& data, const std::string& filename) {
    const auto path = gth::test::testTempRoot() / filename;
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    return path.string();
}

}  // namespace

TEST(PkgFileParserTest, ParsesRealPs4HeaderAndSfoForTheTitle) {
    const std::string sfo = buildSfo({{"TITLE", "My Test Game"}, {"TITLE_ID", "CUSA00001"}, {"APP_VER", "01.00"}});
    const std::string pkg = buildPs4Pkg("UP0001-CUSA00001_00-TESTGAME00000000", sfo);
    const auto path = writeTempFile(pkg, "gth_test_ps4.pkg");

    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->title, "My Test Game");
    EXPECT_EQ(metadata->titleId, "CUSA00001");
    EXPECT_EQ(metadata->platform, "PS4");
    // "UP0001" - the 'U' distributor-code letter is the more precise signal
    // (see detectRegion): this specific content ID is a North America
    // release, not the broader "could be NA or EU" CUSA-prefix bucket.
    EXPECT_EQ(metadata->region, "North America");
    EXPECT_EQ(metadata->version, "01.00");
    EXPECT_EQ(metadata->contentId, "UP0001-CUSA00001_00-TESTGAME00000000");
    EXPECT_TRUE(metadata->titleIsVerified);

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, ExtractsPosterFromPs4EntryTableById) {
    const std::string fakePng = "\x89PNG\r\n\x1a\nFAKE_ICON0_BYTES_FOR_TESTING";
    const std::string pkg = buildPs4PkgWithEntry("UP0001-CUSA00003_00-POSTERTEST0000000", 0x1200, fakePng);
    const auto path = writeTempFile(pkg, "gth_test_ps4_poster.pkg");

    const auto poster = PkgFileParser::extractPoster(path);
    ASSERT_TRUE(poster.has_value());
    EXPECT_EQ(*poster, fakePng);

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, ExtractPosterReturnsNulloptWhenNoIcon0EntryPresent) {
    // Same SFO-only fixture ParsesRealPs4HeaderAndSfoForTheTitle uses - a real package with no
    // ICON0_PNG entry at all (only PARAM_SFO_ID).
    const std::string sfo = buildSfo({{"TITLE", "My Test Game"}, {"TITLE_ID", "CUSA00001"}});
    const std::string pkg = buildPs4Pkg("UP0001-CUSA00001_00-TESTGAME00000000", sfo);
    const auto path = writeTempFile(pkg, "gth_test_ps4_no_poster.pkg");

    EXPECT_FALSE(PkgFileParser::extractPoster(path).has_value());

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, FallsBackToContentIdWhenNoSfoEntryPresent) {
    ByteBuilder b;
    b.u32be(0x7F434E54);
    b.u32be(0);
    b.u32be(0);
    b.u32be(0);
    b.u32be(1);  // one entry, but not param.sfo
    b.u16be(0);
    b.u16be(0);
    b.u32be(0xA0);
    b.u32be(0);
    b.u64be(0);
    b.u64be(0);
    b.u64be(0);
    b.u64be(0);
    std::string contentId = "UP0001-CUSA00002_00-NOSFOTEST00000000";
    contentId.resize(36, '\0');
    b.raw(contentId);
    b.zeros(12);
    for (int i = 0; i < 12; ++i) b.u32be(0);
    ASSERT_EQ(b.size(), 0xA0u);
    // One irrelevant entry (some other file ID), no PARAM_SFO_ID entry at all.
    b.u32be(0x0400);
    b.u32be(0);
    b.u32be(0);
    b.u32be(0);
    b.u32be(0xC0);
    b.u32be(4);
    b.u64be(0);
    b.raw(std::string("\x00\x00\x00\x00", 4));

    const auto path = writeTempFile(b.str(), "gth_test_ps4_nosfo.pkg");
    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->titleId, "CUSA00002");
    // "NOSFOTEST00000000" has a long run of trailing zeros - flagged as
    // padding, not a real squashed title, so this still falls back to the
    // title ID rather than guessing "NOSFOTEST".
    EXPECT_EQ(metadata->title, "CUSA00002");
    EXPECT_EQ(metadata->platform, "PS4");
    EXPECT_FALSE(metadata->titleIsVerified);

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, HumanizesAnInformativeContentIdLabelIntoABestEffortTitle) {
    std::string data = "not a real package, just filler bytes";
    // "EASPORTSFC2025BG" is exactly the real-world label this feature was
    // built for - squashed-together letters/digits with no padding.
    const auto path = writeTempFile(data, "EP0006-PPSA20050_00-EASPORTSFC2025BG_2.pkg");

    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->titleId, "PPSA20050");
    EXPECT_EQ(metadata->title, "EASPORTSFC 2025 BG")
        << "expected a letter/digit-boundary-spaced guess instead of the bare title ID";
    EXPECT_FALSE(metadata->titleIsVerified) << "a label guess is never verified, however good it looks";

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, LeavesAnOpaquePaddingLabelAsTheTitleIdFallback) {
    std::string data = "not a real package, just filler bytes";
    // "NUTSEU0000000000" is a real-world label with no game-name content at
    // all - ten trailing zeros should still be recognized as padding.
    const auto path = writeTempFile(data, "EP0006-PPSA02343_00-NUTSEU0000000000_5.pkg");

    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->titleId, "PPSA02343");
    EXPECT_EQ(metadata->title, "PPSA02343") << "an opaque/padding label should not be humanized into a fake title";

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, FallsBackToFilenameWhenMagicIsUnrecognized) {
    std::string data = "not a real package, just filler bytes so it looks like a plausible file size 12345";
    const auto path = writeTempFile(data, "EP0006-PPSA02343_00-NUTSEU0000000000_5.pkg");

    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->titleId, "PPSA02343");
    EXPECT_EQ(metadata->platform, "PS5");
    // PS5 assigns "PPSA" to every digital title regardless of region (see
    // detectRegion's comment), but the content ID's "EP0006" distributor
    // code still carries it: 'E' is this app's real, verified EU/Europe
    // signal (matches "It Takes Two"'s actual EU PlayStation Store listing,
    // EP0006-PPSA02343-ITTAKESTWORETAIL).
    EXPECT_EQ(metadata->region, "Europe");
    EXPECT_EQ(metadata->contentId, "EP0006-PPSA02343_00-NUTSEU0000000000");

    std::filesystem::remove(path);
}

// Each of these is a real, verified distributor code: EA's US entity is
// UP0006 (matches en-us PlayStation Store listings), EA's European entity
// is EP0006 (already covered above), CD Projekt's Cyberpunk 2077 uses
// HP4497 on the Hong Kong/Indonesia store and JP9002 is Sony Interactive
// Entertainment Inc.'s Japan-registered publisher code.
TEST(PkgFileParserTest, ResolvesRegionFromEachVerifiedDistributorCodeLetter) {
    const struct {
        const char* contentId;
        const char* expectedRegion;
    } cases[] = {
        {"UP0006-PPSA20049_00-25STANDARDBUNDLE", "North America"},
        {"HP4497-PPSA04028_00-00000000000000N5", "Asia"},
        {"JP9002-PPSA00001_00-SOMEJAPANESEGAME", "Japan"},
    };

    for (const auto& [contentId, expectedRegion] : cases) {
        std::string data = "not a real package, just filler bytes so it looks like a plausible file size 12345";
        const std::string filename = std::string(contentId) + "_5.pkg";
        const auto path = writeTempFile(data, filename);

        auto metadata = PkgFileParser::parseFile(path);
        ASSERT_TRUE(metadata.has_value()) << contentId;
        EXPECT_EQ(metadata->region, expectedRegion) << contentId;

        std::filesystem::remove(path);
    }
}

// The Current Version column used to say "1.00" for every game because that
// was hard-coded as the default - indistinguishable from a real 1.00. The real
// answer for a package that doesn't state a version is "unknown" (empty).
TEST(PkgFileParserTest, ReportsAnUnknownVersionInsteadOfMakingOneUp) {
    const auto path = writeTempFile("filler bytes with no recognizable package magic at all 1234567890",
                                     "EP0006-PPSA20050_00-EASPORTSFC2025BG_0.pkg");

    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_TRUE(metadata->version.empty()) << "got \"" << metadata->version << "\"";

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, ReadsTheAppVersionFromAnUpdatePackagesFileNameTag) {
    EXPECT_EQ(PkgFileParser::versionFromFilename("EP0006-PPSA20050_00-EASPORTSFC2025BG-A0110-V0100_0.pkg"), "01.10");
    EXPECT_EQ(PkgFileParser::versionFromFilename("UP0002-CUSA00419_00-GTAVDIGITALDOWNL-A0113-V0100.pkg"), "01.13");
    EXPECT_EQ(PkgFileParser::versionFromFilename("lowercase-a0207-v0100.pkg"), "02.07");
    EXPECT_TRUE(PkgFileParser::versionFromFilename("EP0006-PPSA20050_00-EASPORTSFC2025BG_0.pkg").empty());
    EXPECT_TRUE(PkgFileParser::versionFromFilename("Game-A01-V01.pkg").empty()) << "malformed tag is not a version";

    const auto path = writeTempFile("no recognizable magic here, filename is all there is 1234567890",
                                     "EP0006-PPSA20050_00-EASPORTSFC2025BG-A0110-V0100_0.pkg");
    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->version, "01.10");
    EXPECT_EQ(metadata->titleId, "PPSA20050") << "the version tag must not disturb the content ID match";
    std::filesystem::remove(path);
}

// A PS4 package without APP_VER (rare) still carries the master VERSION.
TEST(PkgFileParserTest, FallsBackToTheSfoVersionWhenThereIsNoAppVersion) {
    const std::string sfo = buildSfo({{"TITLE", "No App Ver"}, {"TITLE_ID", "CUSA00002"}, {"VERSION", "01.03"}});
    const std::string pkg = buildPs4Pkg("UP0001-CUSA00002_00-NOAPPVER00000000", sfo);
    const auto path = writeTempFile(pkg, "gth_test_ps4_nover.pkg");

    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->version, "01.03");

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, ReturnsNulloptWhenFilenameHasNoContentIdEither) {
    const auto path = writeTempFile("junk", "totally_unrelated_file.pkg");

    auto metadata = PkgFileParser::parseFile(path);
    EXPECT_FALSE(metadata.has_value());

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, ParsesReachablePs5FihCntAndParamJsonForTheTitle) {
    // sc_offset is set to right after the 0x60-byte FIH prefix this parser
    // reads, not a realistic 64KB PFS gap - the parser never validates that
    // gap's contents, only that sc_offset + CNT_HEADER_SIZE fits in the
    // file, so this keeps the fixture small without weakening what's tested.
    constexpr uint64_t scOffset = 0x60;
    constexpr uint32_t cntHeaderSize = 0x5A0;
    constexpr uint32_t entryTableRelative = cntHeaderSize;  // right after the CNT header
    constexpr uint32_t entrySize = 0x20;
    constexpr uint32_t entryCount = 2;
    const uint64_t entryTableAbsolute = scOffset + entryTableRelative;
    const uint64_t namesAbsolute = entryTableAbsolute + entryCount * entrySize;

    const std::string names = std::string("unused", 7) + std::string("sce_sys/param.json", 19);
    ASSERT_EQ(names.size(), 26u);  // "unused\0" (7) + "sce_sys/param.json\0" (19)
    const uint64_t paramJsonAbsolute = namesAbsolute + names.size();
    const std::string paramJson =
        R"({"titleId":"PPSA99999","contentId":"IV0001-PPSA99999_00-TESTJSON0000000",)"
        R"("contentVersion":"01.002.000","masterVersion":"01.00","localizedParameters":)"
        R"({"en-US":{"titleName":"My PS5 Test Game"}}})";

    ByteBuilder fih;
    fih.raw(std::string("\x7f\x46\x49\x48", 4));  // magic
    fih.raw(std::string(1, '\x00'));                // 0x04
    fih.raw(std::string(1, '\x80'));                // 0x05 signed_byte = retail
    fih.u16le(3);                                    // 0x06 format_version
    fih.u32le(0);                                    // 0x08
    fih.zeros(4);                                    // 0x0C-0x0F
    fih.u64le(0x10000);                              // 0x10 pfs_offset (unchecked beyond equality)
    fih.u64le(0);                                    // 0x18 pfs_size
    fih.zeros(0x58 - fih.size());
    fih.u64le(scOffset);                             // 0x58 sc_offset
    fih.zeros(0x60 - fih.size());
    ASSERT_EQ(fih.size(), 0x60u);

    ByteBuilder cnt;
    cnt.u32be(0x7F434E54);  // magic
    cnt.u32be(0);           // cnt_flags
    cnt.zeros(8);           // 0x08-0x0F
    cnt.u32be(entryCount);  // pkg_file_count @0x10
    cnt.u16be(0);           // pkg_sc_entry_count @0x14
    cnt.zeros(2);           // pad to 0x18
    cnt.u32be(entryTableRelative);  // @0x18
    cnt.zeros(4);           // pad to 0x20
    cnt.u64be(0);           // body_relative @0x20
    cnt.u64be(0);           // body_size @0x28
    cnt.zeros(16);          // pad to 0x40
    std::string contentId = "IV0001-PPSA99999_00-TESTJSON0000000";
    contentId.resize(48, '\0');
    cnt.raw(contentId);  // @0x40, 48 bytes -> ends at 0x70
    cnt.zeros(cntHeaderSize - cnt.size());
    ASSERT_EQ(cnt.size(), cntHeaderSize);

    ByteBuilder entries;
    // Entry 0: ENTRY_NAMES (0x0200)
    entries.u32be(0x0200);
    entries.u32be(0);  // name_table_offset (unused - this entry names itself)
    entries.u32be(0);
    entries.u32be(0);
    entries.u32be(static_cast<uint32_t>(namesAbsolute - scOffset));
    entries.u32be(static_cast<uint32_t>(names.size()));
    entries.zeros(8);
    // Entry 1: param.json (any nonzero ID - PS5 finds it by name, not ID)
    entries.u32be(0x1000);
    entries.u32be(7);  // name_table_offset -> "sce_sys/param.json" in `names`
    entries.u32be(0);
    entries.u32be(0);
    entries.u32be(static_cast<uint32_t>(paramJsonAbsolute - scOffset));
    entries.u32be(static_cast<uint32_t>(paramJson.size()));
    entries.zeros(8);
    ASSERT_EQ(entries.size(), entryCount * entrySize);

    ByteBuilder full;
    full.raw(fih.str());
    full.raw(cnt.str());
    full.raw(entries.str());
    full.raw(names);
    full.raw(paramJson);

    const auto path = writeTempFile(full.str(), "gth_test_ps5.pkg");
    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->title, "My PS5 Test Game");
    EXPECT_EQ(metadata->titleId, "PPSA99999");
    EXPECT_EQ(metadata->version, "01.002.000") << "PS5 param.json contentVersion is the version a console shows";
    EXPECT_EQ(metadata->contentId, "IV0001-PPSA99999_00-TESTJSON0000000");
    EXPECT_EQ(metadata->platform, "PS5");
    EXPECT_EQ(metadata->region, "Unknown");  // PS5 doesn't encode region in the title ID - see detectRegion
    EXPECT_TRUE(metadata->titleIsVerified);

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, ExtractsPosterFromPs5EntryTableByName) {
    // Same shape as ParsesReachablePs5FihCntAndParamJsonForTheTitle above, plus a third entry -
    // "sce_sys/icon0.png" - since PS5 entries (unlike PS4's fixed ICON0_PNG id) are only found by
    // name, exactly like param.json itself.
    constexpr uint64_t scOffset = 0x60;
    constexpr uint32_t cntHeaderSize = 0x5A0;
    constexpr uint32_t entryTableRelative = cntHeaderSize;
    constexpr uint32_t entrySize = 0x20;
    constexpr uint32_t entryCount = 3;
    const uint64_t entryTableAbsolute = scOffset + entryTableRelative;
    const uint64_t namesAbsolute = entryTableAbsolute + entryCount * entrySize;

    const std::string names = std::string("unused", 7) + std::string("sce_sys/param.json", 19) +
                               std::string("sce_sys/icon0.png", 18);
    ASSERT_EQ(names.size(), 44u);
    const uint64_t paramJsonAbsolute = namesAbsolute + names.size();
    const std::string paramJson = R"({"titleId":"PPSA88888","contentId":"IV0001-PPSA88888_00-TESTPOSTER000000",)"
                                   R"("contentVersion":"02.000.000"})";
    const uint64_t iconAbsolute = paramJsonAbsolute + paramJson.size();
    const std::string fakeIcon = "\x89PNG\r\n\x1a\nFAKE_PS5_ICON0_BYTES";

    ByteBuilder fih;
    fih.raw(std::string("\x7f\x46\x49\x48", 4));
    fih.raw(std::string(1, '\x00'));
    fih.raw(std::string(1, '\x80'));
    fih.u16le(3);
    fih.u32le(0);
    fih.zeros(4);
    fih.u64le(0x10000);
    fih.u64le(0);
    fih.zeros(0x58 - fih.size());
    fih.u64le(scOffset);
    fih.zeros(0x60 - fih.size());
    ASSERT_EQ(fih.size(), 0x60u);

    ByteBuilder cnt;
    cnt.u32be(0x7F434E54);
    cnt.u32be(0);
    cnt.zeros(8);
    cnt.u32be(entryCount);
    cnt.u16be(0);
    cnt.zeros(2);
    cnt.u32be(entryTableRelative);
    cnt.zeros(4);
    cnt.u64be(0);
    cnt.u64be(0);
    cnt.zeros(16);
    std::string contentId = "IV0001-PPSA88888_00-TESTPOSTER000000";
    contentId.resize(48, '\0');
    cnt.raw(contentId);
    cnt.zeros(cntHeaderSize - cnt.size());
    ASSERT_EQ(cnt.size(), cntHeaderSize);

    ByteBuilder entries;
    // Entry 0: ENTRY_NAMES
    entries.u32be(0x0200);
    entries.u32be(0);
    entries.u32be(0);
    entries.u32be(0);
    entries.u32be(static_cast<uint32_t>(namesAbsolute - scOffset));
    entries.u32be(static_cast<uint32_t>(names.size()));
    entries.zeros(8);
    // Entry 1: param.json
    entries.u32be(0x1000);
    entries.u32be(7);
    entries.u32be(0);
    entries.u32be(0);
    entries.u32be(static_cast<uint32_t>(paramJsonAbsolute - scOffset));
    entries.u32be(static_cast<uint32_t>(paramJson.size()));
    entries.zeros(8);
    // Entry 2: icon0.png - name_table_offset 26 -> "sce_sys/icon0.png" in `names`
    entries.u32be(0x1200);
    entries.u32be(26);
    entries.u32be(0);
    entries.u32be(0);
    entries.u32be(static_cast<uint32_t>(iconAbsolute - scOffset));
    entries.u32be(static_cast<uint32_t>(fakeIcon.size()));
    entries.zeros(8);
    ASSERT_EQ(entries.size(), entryCount * entrySize);

    ByteBuilder full;
    full.raw(fih.str());
    full.raw(cnt.str());
    full.raw(entries.str());
    full.raw(names);
    full.raw(paramJson);
    full.raw(fakeIcon);

    const auto path = writeTempFile(full.str(), "gth_test_ps5_poster.pkg");
    const auto poster = PkgFileParser::extractPoster(path);
    ASSERT_TRUE(poster.has_value());
    EXPECT_EQ(*poster, fakeIcon);

    std::filesystem::remove(path);
}

TEST(PkgFileParserTest, Ps5FihMagicWithUnreachableEmbeddedCntFallsBackToFilename) {
    // A well-formed FIH wrapper (correct magic, pfs_offset == 0x10000) whose
    // sc_offset points past the end of this (deliberately short) file -
    // exactly the shape of a split-part PS5 package's first part.
    ByteBuilder b;
    b.raw(std::string("\x7f\x46\x49\x48", 4));  // magic
    b.raw(std::string(1, '\x00'));               // 0x04
    b.raw(std::string(1, '\x80'));               // 0x05 signed_byte = retail
    b.u16le(3);                                   // 0x06 format_version
    b.u32le(0);                                   // 0x08
    b.zeros(4);                                   // 0x0C-0x0F
    b.u64le(0x10000);                             // 0x10 pfs_offset
    b.u64le(0x1000000000ULL);                     // 0x18 pfs_size (huge - image not actually present)
    b.zeros(0x58 - 0x20);
    b.u64le(0x2000000000ULL);                     // 0x58 sc_offset - way past this tiny test file
    b.zeros(0x100 - b.size());

    const auto path = writeTempFile(b.str(), "EP0006-PPSA20050_00-EASPORTSFC2025BG_0.pkg");
    auto metadata = PkgFileParser::parseFile(path);
    ASSERT_TRUE(metadata.has_value());
    // Structured PS5 parsing correctly gave up (sc_offset is unreachable),
    // so this should be the filename fallback's result.
    EXPECT_EQ(metadata->titleId, "PPSA20050");
    EXPECT_EQ(metadata->platform, "PS5");

    std::filesystem::remove(path);
}
