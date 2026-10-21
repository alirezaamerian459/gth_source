#include <gtest/gtest.h>

#include "gth/network/OnlineTitleLookupService.h"

using gth::network::ParseTitleLookupResponse;

// Real response captured from https://prosperopatches.com/api/internal/search?term=PPSA20050
TEST(OnlineTitleLookupServiceTest, ParsesARealProsperoPatchesResponse) {
    const std::string body =
        R"({"success":true,"results":[{"titleid":"PPSA20050","name":"EA SPORTS FC 25","region":"EU","icon":"https:\/\/cdn.prosperopatches.com\/titles\/PPSA20050_x\/icon0.webp"}]})";

    auto title = ParseTitleLookupResponse(body, "PPSA20050");

    ASSERT_TRUE(title.has_value());
    EXPECT_EQ(*title, "EA SPORTS FC 25");
}

// Real response captured from https://orbispatches.com/api/internal/search?term=CUSA07410
TEST(OnlineTitleLookupServiceTest, ParsesARealOrbisPatchesResponse) {
    const std::string body =
        R"({"success":true,"results":[{"titleid":"CUSA07410","name":"God of War","region":"EU","icon":"https:\/\/cdn.orbispatches.com\/titles\/CUSA07410_x\/icon0.webp"}]})";

    auto title = ParseTitleLookupResponse(body, "CUSA07410");

    ASSERT_TRUE(title.has_value());
    EXPECT_EQ(*title, "God of War");
}

TEST(OnlineTitleLookupServiceTest, MatchesTheQueriedTitleIdCaseInsensitively) {
    const std::string body = R"({"success":true,"results":[{"titleid":"PPSA20050","name":"EA SPORTS FC 25"}]})";

    EXPECT_TRUE(ParseTitleLookupResponse(body, "ppsa20050").has_value());
}

// This endpoint also serves fuzzy name search - trusting a result whose
// titleid doesn't match the query would silently mislabel the wrong game.
TEST(OnlineTitleLookupServiceTest, RejectsAResultWhoseTitleIdDoesNotMatchTheQuery) {
    const std::string body = R"({"success":true,"results":[{"titleid":"PPSA00001","name":"Some Other Game"}]})";

    EXPECT_FALSE(ParseTitleLookupResponse(body, "PPSA20050").has_value());
}

TEST(OnlineTitleLookupServiceTest, ReturnsNulloptWhenTheSiteReportsFailure) {
    const std::string body = R"({"success":false,"results":[]})";

    EXPECT_FALSE(ParseTitleLookupResponse(body, "PPSA20050").has_value());
}

TEST(OnlineTitleLookupServiceTest, ReturnsNulloptWhenThereAreNoResults) {
    const std::string body = R"({"success":true,"results":[]})";

    EXPECT_FALSE(ParseTitleLookupResponse(body, "PPSA20050").has_value());
}

TEST(OnlineTitleLookupServiceTest, ReturnsNulloptForGarbageOrEmptyInput) {
    EXPECT_FALSE(ParseTitleLookupResponse("", "PPSA20050").has_value());
    EXPECT_FALSE(ParseTitleLookupResponse("<html>not json</html>", "PPSA20050").has_value());
}

TEST(OnlineTitleLookupServiceTest, UnescapesABackslashEscapedQuoteInTheName) {
    const std::string body = R"({"success":true,"results":[{"titleid":"PPSA20050","name":"Ratchet \"and\" Clank"}]})";

    auto title = ParseTitleLookupResponse(body, "PPSA20050");

    ASSERT_TRUE(title.has_value());
    EXPECT_EQ(*title, "Ratchet \"and\" Clank");
}

// PS3 has no tracker site: it must answer instantly with nothing, and without
// counting as a failed lookup (there was nothing to fail at).
TEST(OnlineTitleLookupServiceTest, UnsupportedPlatformsReturnNothingWithoutAFailureReason) {
    gth::network::OnlineTitleLookupService service;

    EXPECT_FALSE(service.lookupTitle("PS3", "BCUS98114").has_value());
    EXPECT_TRUE(service.lastFailureReason().empty());
    EXPECT_FALSE(service.lookupTitle("PS5", "").has_value());
    EXPECT_TRUE(service.lastFailureReason().empty());
}

// Failure path against a host that can never resolve (".invalid" is reserved
// by RFC 2606): the lookup must come back empty *with a reason* - that's what
// lets the Games tab say why names didn't change - and after a few failures in
// a row it must stop trying rather than burn a timeout on every remaining game.
TEST(OnlineTitleLookupServiceTest, ReportsAReasonAndPausesAfterRepeatedNetworkFailures) {
    gth::network::OnlineTitleLookupService service(L"gth-test-host.invalid", L"gth-test-host.invalid");

    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(service.lookupTitle("PS5", "PPSA02343").has_value());
        EXPECT_FALSE(service.lastFailureReason().empty()) << "attempt " << i;
        EXPECT_EQ(service.lastFailureReason().find("skipped"), std::string::npos) << "attempt " << i;
    }

    EXPECT_FALSE(service.lookupTitle("PS5", "PPSA02343").has_value());
    EXPECT_NE(service.lastFailureReason().find("skipped after repeated failures"), std::string::npos)
        << "got: " << service.lastFailureReason();
}

// The site writes a trademark sign (and any accented letter) as a \uXXXX escape. The parser used to keep just the
// character after the backslash, so "Sackboy(TM)" came out as "Sackboyu2122" in the library.
TEST(OnlineTitleLookupServiceTest, DecodesUnicodeEscapesInNames) {
    const std::string body = R"({"success":true,"results":[{"titleid":"PPSA01288","name":"Sackboy\u2122: A Big Adventure"}]})";

    const auto title = ParseTitleLookupResponse(body, "PPSA01288");

    ASSERT_TRUE(title.has_value());
    EXPECT_EQ(*title, "Sackboy\xE2\x84\xA2: A Big Adventure");  // U+2122 as UTF-8
}

TEST(OnlineTitleLookupServiceTest, DecodesAccentsAndSurrogatePairsAndOtherEscapes) {
    const std::string accents = R"({"success":true,"results":[{"titleid":"PPSA00001","name":"Pok\u00e9mon \u201cGo\u201d"}]})";
    const auto accented = ParseTitleLookupResponse(accents, "PPSA00001");
    ASSERT_TRUE(accented.has_value());
    EXPECT_EQ(*accented, "Pok\xC3\xA9mon \xE2\x80\x9CGo\xE2\x80\x9D");

    // U+1F3AE (a joystick) is outside the basic plane, so JSON spells it as a surrogate pair.
    const std::string pair = R"({"success":true,"results":[{"titleid":"PPSA00002","name":"Play \ud83c\udfae"}]})";
    const auto emoji = ParseTitleLookupResponse(pair, "PPSA00002");
    ASSERT_TRUE(emoji.has_value());
    EXPECT_EQ(*emoji, "Play \xF0\x9F\x8E\xAE");

    const std::string plain = R"({"success":true,"results":[{"titleid":"PPSA00003","name":"Say \"Hi\" \\ Ho \/ Ha"}]})";
    const auto quoted = ParseTitleLookupResponse(plain, "PPSA00003");
    ASSERT_TRUE(quoted.has_value());
    EXPECT_EQ(*quoted, "Say \"Hi\" \\ Ho / Ha");
}

TEST(OnlineTitleLookupServiceTest, AMalformedUnicodeEscapeDoesNotInventACharacter) {
    const std::string broken = R"({"success":true,"results":[{"titleid":"PPSA00004","name":"Bad \u12 name \udc00 end"}]})";

    const auto title = ParseTitleLookupResponse(broken, "PPSA00004");

    ASSERT_TRUE(title.has_value());
    // The broken escape is dropped and what follows it stays as plain text; the lone surrogate is dropped whole.
    EXPECT_EQ(*title, "Bad 12 name  end");
    EXPECT_EQ(title->find('\\'), std::string::npos);
}
