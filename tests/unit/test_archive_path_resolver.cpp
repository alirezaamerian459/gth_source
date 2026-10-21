#include <gtest/gtest.h>

#include "gth/core/index/DefaultArchivePathResolver.h"

using namespace gth::core;

TEST(DefaultArchivePathResolverTest, ParsesFourValidSegments) {
    DefaultArchivePathResolver resolver;
    auto result = resolver.parse({"PS5", "Spider-Man 2", "Europe", "1.06"});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->platform, "PS5");
    EXPECT_EQ(result->title, "Spider-Man 2");
    EXPECT_EQ(result->region, "Europe");
    EXPECT_EQ(result->version, "1.06");
}

TEST(DefaultArchivePathResolverTest, RejectsWrongSegmentCount) {
    DefaultArchivePathResolver resolver;
    EXPECT_FALSE(resolver.parse({"PS5", "Spider-Man 2", "Europe"}).has_value());
    EXPECT_FALSE(resolver.parse({"PS5", "Spider-Man 2", "Europe", "1.06", "extra"}).has_value());
}

TEST(DefaultArchivePathResolverTest, RejectsEmptySegment) {
    DefaultArchivePathResolver resolver;
    EXPECT_FALSE(resolver.parse({"PS5", "", "Europe", "1.06"}).has_value());
}

TEST(DefaultArchivePathResolverTest, BuildRoundTripsWithParse) {
    DefaultArchivePathResolver resolver;
    ArchivePathComponents components{"PS5", "Spider-Man 2", "Europe", "1.06"};
    const std::string built = resolver.build(components);

    EXPECT_EQ(built, "PS5\\Spider-Man 2\\Europe\\1.06");
}
