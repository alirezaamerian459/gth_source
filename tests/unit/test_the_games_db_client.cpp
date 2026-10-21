#include <gtest/gtest.h>

#include "gth/network/TheGamesDbClient.h"

using gth::network::ExtractFirstBoxArtUrl;
using gth::network::TheGamesDbClient;

// The snippet below is the real markup thegamesdb.net/search.php returns for a match with a cover
// (captured live from a real search - not guessed), trimmed to what matters here.
TEST(TheGamesDbClientTest, ParsesTheRealSearchPageMarkup) {
    const std::string html =
        R"(<a href="./game.php?id=77451">)"
        R"(<div class="card border-primary" style="height: 100%">)"
        R"(<img class="card-img-top" alt="Astro's Playroom cover" )"
        R"(src="https://cdn.thegamesdb.net/images/thumb/boxart/front/77451-1.jpg">)"
        R"(<div class="card-body card-noboday" style="text-align:center;">)";

    auto url = ExtractFirstBoxArtUrl(html);
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(*url, "https://cdn.thegamesdb.net/images/thumb/boxart/front/77451-1.jpg");
}

TEST(TheGamesDbClientTest, ReturnsNulloptWhenTheResultHasNoCover) {
    const std::string html =
        R"(<a href="./game.php?id=1">)"
        R"(<div class="card border-primary"><div class="card-body">No cover art</div></div>)";
    EXPECT_FALSE(ExtractFirstBoxArtUrl(html).has_value());
}

TEST(TheGamesDbClientTest, ReturnsNulloptOnEmptyOrUnrelatedText) {
    EXPECT_FALSE(ExtractFirstBoxArtUrl("").has_value());
    EXPECT_FALSE(ExtractFirstBoxArtUrl("<html><body>no results</body></html>").has_value());
}

TEST(TheGamesDbClientTest, TakesTheFirstCoverWhenSeveralResultsHaveOne) {
    const std::string html =
        R"(src="https://cdn.thegamesdb.net/images/thumb/boxart/front/1-1.jpg")"
        R"( ... )"
        R"(src="https://cdn.thegamesdb.net/images/thumb/boxart/front/2-1.jpg")";
    auto url = ExtractFirstBoxArtUrl(html);
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(*url, "https://cdn.thegamesdb.net/images/thumb/boxart/front/1-1.jpg");
}

// Real end-to-end check against the live site (search + parse + image download), for a title
// that's genuinely PlayStation-only (never on PC/Steam) - the exact case this client exists for.
// Not run normally (network tests don't belong in the regular suite): set GTH_LIVE_NETWORK and
// run this test on its own.
TEST(TheGamesDbClientLiveTest, FetchesARealPlayStationExclusiveCover) {
    char* text = nullptr;
    size_t length = 0;
    if (_dupenv_s(&text, &length, "GTH_LIVE_NETWORK") != 0 || text == nullptr) GTEST_SKIP() << "GTH_LIVE_NETWORK not set";

    TheGamesDbClient client;
    const auto poster = client.fetchPosterByTitle("Astro's Playroom");
    ASSERT_TRUE(poster.has_value()) << "no cover found - check network connectivity or the site's markup";
    EXPECT_GT(poster->size(), 1000u) << "got a response, but it's too small to be a real image";
}
