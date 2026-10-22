#include <gtest/gtest.h>

#include "gth/network/PsPricesClient.h"

using gth::network::ExtractFirstPsPricesCoverUrl;
using gth::network::PsPricesClient;

TEST(PsPricesClientTest, ParsesARealSearchResultCard) {
    const std::string html =
        R"(<a href="/region-us/game/4285056/astros-playroom">)"
        R"(<div class="relative overflow-hidden aspect-square">)"
        R"(<img src="https://image.api.playstation.com/vulcan/ap/rnd/202502/1908/cover.png" alt="">)"
        R"(<img src="https://image.api.playstation.com/vulcan/ap/rnd/202502/1908/cover.png" alt="Astro's Playroom">)"
        R"(</a>)";

    const auto url = ExtractFirstPsPricesCoverUrl(html);
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(*url, "https://image.api.playstation.com/vulcan/ap/rnd/202502/1908/cover.png");
}

TEST(PsPricesClientTest, ReturnsNulloptWhenAResultHasNoCover) {
    const std::string html =
        R"(<a href="/region-us/game/1/no-cover"><h3>No cover art</h3></a>)";
    EXPECT_FALSE(ExtractFirstPsPricesCoverUrl(html).has_value());
}

TEST(PsPricesClientTest, ReturnsNulloptOnEmptyOrUnrelatedText) {
    EXPECT_FALSE(ExtractFirstPsPricesCoverUrl("").has_value());
    EXPECT_FALSE(ExtractFirstPsPricesCoverUrl("<html><body>no results</body></html>").has_value());
}

TEST(PsPricesClientTest, SkipsAResultWithoutCoverAndUsesTheNextResult) {
    const std::string html =
        R"(<a href="/region-us/game/1/no-cover"><span>No cover</span></a>)"
        R"(<a href="/region-us/game/2/has-cover"><img src="https://image.api.playstation.com/cover.jpg" alt="Game"></a>)";
    const auto url = ExtractFirstPsPricesCoverUrl(html);
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(*url, "https://image.api.playstation.com/cover.jpg");
}

TEST(PsPricesClientTest, DoesNotReadAnImageOutsideTheGameCard) {
    const std::string html =
        R"(<img src="https://image.api.playstation.com/ad.jpg">)"
        R"(<a href="/region-us/game/3/game"><span>No poster in card</span></a>)";
    EXPECT_FALSE(ExtractFirstPsPricesCoverUrl(html).has_value());
}

TEST(PsPricesClientLiveTest, FetchesARealPlayStationCover) {
    char* text = nullptr;
    size_t length = 0;
    if (_dupenv_s(&text, &length, "GTH_LIVE_NETWORK") != 0 || text == nullptr) {
        GTEST_SKIP() << "GTH_LIVE_NETWORK not set";
    }

    PsPricesClient client;
    const auto poster = client.fetchPosterByTitle("Astro's Playroom");
    ASSERT_TRUE(poster.has_value()) << "no cover found - check network connectivity or PSPrices markup";
    EXPECT_GT(poster->size(), 1000u) << "got a response, but it's too small to be a real image";
}
